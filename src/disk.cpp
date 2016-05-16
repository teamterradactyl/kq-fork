/*! \page License
   KQ is Copyright (C) 2002 by Josh Bolduc

   This file is part of KQ... a freeware RPG.

   KQ is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   KQ is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with KQ; see the file COPYING.  If not, write to
   the Free Software Foundation,
       675 Mass Ave, Cambridge, MA 02139, USA.
*/


/*! \file
 * \brief Functions to load/save to disk
 *
 * These functions are endian independent
 * \author PH
 * \date 20030629
 */

#include <assert.h>
#include <stdio.h>
#include <string>
#include <stdint.h>
#include <vector>
#include <map>
#include <algorithm>
#include <iterator>
#include <tinyxml2.h>

#include "kq.h"
#include "bounds.h"
#include "disk.h"
#include "markers.h"
#include "platform.h"
#include "heroc.h"
#include "shopmenu.h"
#include "random.h"

using tinyxml2::XMLElement;
using tinyxml2::XMLDocument;
using std::vector;
using std::copy;
using std::begin;
using std::end;

/*! Iteration helper class.
 * Allows use of C++11's range-based for syntax to iterate
 * through child elements.
 */
typedef std::pair<XMLElement*, const char*> xiterator;
struct xiterable : public std::pair<XMLElement*, const char*> {
	using std::pair<XMLElement*, const char*>::pair;
	xiterator begin() {
		return xiterator(first->FirstChildElement(second), second);
	}
	xiterator end() {
		return xiterator(nullptr, second);
	}
};


xiterator& operator++(xiterator& it) {
	it.first = it.first->NextSiblingElement(it.second);
	return it;
}
XMLElement* operator*(xiterator& it) {
	return it.first;
}

xiterable children(XMLElement* parent, const char* tag = nullptr) {
  return xiterable(parent, tag);
}

/** Convert a comma-separated list of ints into a vector.
 * Supplied string can be null or empty (giving an empty list)
 * \param str a string containing the list
 * \returns the numbers in a vector
 */ 
static vector<int> parse_list(const char* str) {
  vector<int> list;
  while (str && *str) {
    const char* next = strchr(str, ',');
    list.push_back(static_cast<int>(strtol(str, nullptr, 10)));
    if (next) {
      str = next + 1;
    } else {
      str = nullptr;
    }
  }
  return list;
}
/** Generate a comma-separated list from
 * a range specified by two iterators
 * \param begin the start of the range (inclusive)
 * \param end the end of the range (exclusive)
 * \returns a new comma-separated list
 */
template <typename _InputIterator>
static std::string make_list(_InputIterator begin, _InputIterator end) {
  bool first = true;
  std::string ans;
  for (auto i = begin; i!=end; ++i) {
    if (first) {
      first = false;
    } else {
      ans += ',';
    }
    ans += std::to_string(static_cast<int>(*i));
  }
  return ans;
}
/* Insert a list of things into the content of an element */
template <typename _InputIterator>
static XMLElement* value_list(XMLElement* elem, _InputIterator begin, _InputIterator end) {
	tinyxml2::XMLText* content = elem->GetDocument()->NewText(make_list(begin, end).c_str());
	elem->DeleteChildren();
	elem->InsertFirstChild(content);
	return elem;
}
/** Trim a range.
 * Shorten the given range to exclude any zero elements at the end.
 * \returns a new 'end' iterator
 */
template <typename _InputIterator>
_InputIterator trim_range(_InputIterator begin, _InputIterator end) {
  typedef typename std::iterator_traits<_InputIterator>::value_type vt;
  while (end != begin) {
    _InputIterator n = std::prev(end);
    if (*n != vt()) {
      return end;
    } else {
      end = n;
    }
  }
  return begin;  
}
/*! Check if a range is all default.
 * Scan a range, return true if all the elements are
 * the same as their 'default' values (e.g. 0 for integers)
 */
template <typename _InputIterator>
bool range_is_default(_InputIterator first, _InputIterator last) {
  typedef typename std::iterator_traits<_InputIterator>::value_type vt;
  vt v0 = vt();
  while (first != last) {
    if (*first != v0) { return false; }
    else {
      ++first;
    }
  }
  return true;
}
  
  
int save_s_entity(s_entity *s, PACKFILE *f)
{
    pack_putc(s->chrx, f);
    pack_putc(0, f);             /* alignment */
    pack_iputw(s->x, f);
    pack_iputw(s->y, f);
    pack_iputw(s->tilex, f);
    pack_iputw(s->tiley, f);
    pack_putc(s->eid, f);
    pack_putc(s->active, f);
    pack_putc(s->facing, f);
    pack_putc(s->moving, f);
    pack_putc(s->movcnt, f);
    pack_putc(s->framectr, f);
    pack_putc(s->movemode, f);
    pack_putc(s->obsmode, f);
    pack_putc(s->delay, f);
    pack_putc(s->delayctr, f);
    pack_putc(s->speed, f);
    pack_putc(s->scount, f);
    pack_putc(s->cmd, f);
    pack_putc(s->sidx, f);
    pack_putc(s->extra, f);
    pack_putc(s->chasing, f);
    pack_iputw(0, f);            /* alignment */
    pack_iputl(s->cmdnum, f);
    pack_putc(s->atype, f);
    pack_putc(s->snapback, f);
    pack_putc(s->facehero, f);
    pack_putc(s->transl, f);
    pack_fwrite(s->script, sizeof(s->script), f);
    return 0;
}

static int load_resistances(s_player* s, XMLElement* node) {
	std::fill(std::begin(s->res), std::end(s->res), 0);
	XMLElement* resistances = node->FirstChildElement("resistances");
	if (resistances) {
		auto values = parse_list(resistances->FirstChild()->Value());
		if (!values.empty()) {
			// Gave some, has to be the right number of elements
			if (values.size() == NUM_RES) {
				copy(values.begin(), values.end(), s->res);
			}
			else {
				TRACE("Wrong number of resistances, expected %d and got %d", NUM_RES, values.size());
				program_death("Error loading XML");
			}
		}
	}
	return 0;
}
static int load_spelltypes(s_player* s, XMLElement* node) {
	std::fill(std::begin(s->sts), std::end(s->sts), 0);
	XMLElement* spelltypes = node->FirstChildElement("spelltypes");
	if (spelltypes) {
		auto values = parse_list(spelltypes->FirstChild()->Value());
		if (!values.empty()) {
			if (values.size() == NUM_SPELLTYPES) {
				copy(values.begin(), values.end(), s->sts);
			}
			else {
				TRACE("Wrong number of spelltypes, expected %d and got %d", NUM_SPELLTYPES, values.size());
				program_death("Error loading XML");
			}
		}
	}
	return 0;
}
static int load_spells(s_player* s, XMLElement* node) {
	std::fill(std::begin(s->spells), std::end(s->spells), 0);
	XMLElement* spells = node->FirstChildElement("spells");
	if (spells) {
		auto values = parse_list(spells->FirstChild()->Value());
		if (!values.empty()) {
			if (values.size() == NUM_SPELLS) {
				copy(values.begin(), values.end(), s->spells);
			}
			else {
				TRACE("Wrong number of spells, expected %d and got %d", NUM_SPELLS, values.size());
				program_death("Error loading XML");
			}
		}
	}
	return 0;
}
static int load_equipment(s_player* s, XMLElement* node) {
	std::fill(std::begin(s->eqp), std::end(s->eqp), 0);
	XMLElement* eqp = node->FirstChildElement("equipment");
	if (eqp) {
		auto values = parse_list(eqp->FirstChild()->Value());
		if (!values.empty()) {
			if (values.size() == NUM_EQUIPMENT) {
				copy(values.begin(), values.end(), s->sts);
			}
			else {
				TRACE("Wrong number of equipment, expected %d and got %d", NUM_EQUIPMENT, values.size());
				program_death("Error loading XML");
			}
		}
	}
	return 0;
}
static int load_attributes(s_player* s, XMLElement* node) {
	XMLElement* attributes = node->FirstChildElement("attributes");
	if (attributes) {
		for (auto property : children(attributes, "property")) {
			if (property->Attribute("name", "str")) {
				s->stats[A_STR] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "agi")) {
				s->stats[A_AGI] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "vit")) {
				s->stats[A_VIT] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "int")) {
				s->stats[A_INT] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "sag")) {
				s->stats[A_SAG] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "spd")) {
				s->stats[A_SPD] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "aur")) {
				s->stats[A_AUR] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "spi")) {
				s->stats[A_SPI] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "att")) {
				s->stats[A_ATT] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "hit")) {
				s->stats[A_HIT] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "def")) {
				s->stats[A_DEF] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "evd")) {
				s->stats[A_EVD] = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "mag")) {
				s->stats[A_MAG] = property->IntAttribute("value");
			}
		}
	}
	return 0;
}
static int load_core_properties(s_player* s, XMLElement* node) {
	XMLElement* properties = node->FirstChildElement("properties");
	if (properties) {
		for (auto property : children(properties, "property")) {
			if (property->Attribute("name", "name")) {
				const char* name = property->Attribute("value");
				strncpy(s->name, name, sizeof(s->name) - 1);
			}
			else if (property->Attribute("name", "xp")) {
				s->xp = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "next")) {
				s->next = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "lvl")) {
				s->lvl = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "mrp")) {
				s->mrp = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "hp")) {
				s->hp = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "mhp")) {
				s->mhp = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "mp")) {
				s->mp = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "mmp")) {
				s->mmp = property->IntAttribute("value");
			}
		}
	}
	else {
		program_death("Core properties missing from XML");
	}
	return 0;
}
static int load_lup(s_player* s, XMLElement* node) {
	XMLElement* elem = node->FirstChildElement("level-up");
	if (elem && !elem->NoChildren()) {
		auto vals = parse_list(elem->FirstChild()->Value());
		copy(vals.begin(), vals.end(), s->lup);
		return 0;
	}
	else {
		   // ???
		return 1;
	}
}
/** Get player (hero) data from an XML node.
 * @param s the structure to write to
 * @param node a node within an XML document.
 * @returns 0 if OK otherwise -1
 */
int load_s_player(s_player* s, XMLElement* node) {
	load_core_properties(s, node);
	load_attributes(s, node);
	load_resistances(s, node);
	load_spelltypes(s, node);
	load_spells(s, node);
	load_equipment(s, node);
	load_lup(s, node);
	return 0;
}

int load_s_player(s_player *s, PACKFILE *f)
{
    size_t i;

    pack_fread(s->name, sizeof(s->name), f);
    pack_getc(f);                // alignment 
    pack_getc(f);                // alignment 
    pack_getc(f);                // alignment 
    s->xp = pack_igetl(f);
    s->next = pack_igetl(f);
    s->lvl = pack_igetl(f);
    s->mrp = pack_igetl(f);
    s->hp = pack_igetl(f);
    s->mhp = pack_igetl(f);
    s->mp = pack_igetl(f);
    s->mmp = pack_igetl(f);

    for (i = 0; i < NUM_STATS; ++i)
    {
        s->stats[i] = pack_igetl(f);
    }

    for (i = 0; i < R_TOTAL_RES; ++i)
    {
        s->res[i] = pack_getc(f);
    }

    for (i = 0; i < 24; ++i)
    {
        s->sts[i] = pack_getc(f);
    }

    for (i = 0; i < NUM_EQUIPMENT; ++i)
    {
        s->eqp[i] = pack_getc(f);
    }

    for (i = 0; i < 60; ++i)
    {
        s->spells[i] = pack_getc(f);
    }
    pack_getc(f);                // alignment 
pack_getc(f);                // alignment 
    return 0;
}
// Helper function - insert a property element.
template <typename T>
static XMLElement* addprop(XMLElement* parent, const char* name, T value) {
  XMLElement* property = parent->GetDocument()->NewElement("property");
  property->SetAttribute("name", name);
  property->SetAttribute("value", value);
  parent->InsertEndChild(property);
  return property;
}

static XMLElement* addprop(XMLElement* parent, const char* name, const std::string& value) {
	return addprop(parent, name, value.c_str());
}
// Store spell info or nothing if all spells are 'zero'
static int store_spells(const s_player*s, XMLElement* node) {
  auto startp = std::begin(s->spells);
  auto endp = std::end(s->spells);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("spells");
	value_list(elem, startp, endp);
    node->InsertEndChild(elem);
  }
  return 0;
}
static int store_equipment(const s_player*s, XMLElement* node) {
  auto startp = std::begin(s->eqp);
  auto endp = std::end(s->eqp);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("equipment");
	value_list(elem, startp, endp);
    node->InsertEndChild(elem);
  }
  return 0;
}
static int store_spelltypes(const s_player*s, XMLElement* node) {
  auto startp = std::begin(s->sts);
  auto endp = std::end(s->sts);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("spelltypes");
	value_list(elem, startp, endp);
	node->InsertEndChild(elem);
  }
  return 0;
}
static int store_resistances(const s_player*s, XMLElement* node) {
  auto startp = std::begin(s->res);
  auto endp = std::end(s->res);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("resistances");
	value_list(elem, startp, endp);
	node->InsertEndChild(elem);
  }
  return 0;
}
static int store_stats(const s_player*s, XMLElement* node) {
  auto startp = std::begin(s->stats);
  auto endp = std::end(s->stats);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("attributes");
	value_list(elem, startp, endp);
	node->InsertEndChild(elem);
  }
  return 0;
}

static int store_lup(const s_player* s, XMLElement* node) {
	XMLElement* elem = node->GetDocument()->NewElement("level-up");
	value_list(elem, std::begin(s->lup), std::end(s->lup));
	node->InsertEndChild(elem);
	return 0;
}
int save_s_player(s_player *s, PACKFILE *f)
{
    size_t i;

    pack_fwrite(s->name, sizeof(s->name), f);
    pack_putc(0, f);             // alignment 
pack_putc(0, f);             // alignment 
pack_putc(0, f);             // alignment 
    pack_iputl(s->xp, f);
    pack_iputl(s->next, f);
    pack_iputl(s->lvl, f);
    pack_iputl(s->mrp, f);
    pack_iputl(s->hp, f);
    pack_iputl(s->mhp, f);
    pack_iputl(s->mp, f);
    pack_iputl(s->mmp, f);
    for (i = 0; i < NUM_STATS; ++i)
    {
        pack_iputl(s->stats[i], f);
    }
    for (i = 0; i < R_TOTAL_RES; ++i)
    {
        pack_putc(s->res[i], f);
    }
    for (i = 0; i < 24; ++i)
    {
        pack_putc(s->sts[i], f);
    }
    for (i = 0; i < NUM_EQUIPMENT; ++i)
    {
        pack_putc(s->eqp[i], f);
    }
    for (i = 0; i < 60; ++i)
    {
        pack_putc(s->spells[i], f);
    }
    pack_putc(0, f);             // alignment 
pack_putc(0, f);             // alignment 
    return 0;
}

struct cstring_less {
  bool operator()(const char* const& a, const char* const&b) const {
    return strcmp(a, b) < 0;
  }
};

static const std::map<const char*, ePIDX, cstring_less> id_lookup = {
  {"sensar", SENSAR},
  {"sarina", SARINA},
  {"corin", CORIN},
  {"ajathar", AJATHAR},
  {"casandra", CASANDRA},
  {"temmin", TEMMIN},
  {"ayla", AYLA},
  {"noslom", NOSLOM}
};
/** Store player inside a node that you supply.
 */
static int save_player(const s_player * s, XMLElement* node) {
  XMLDocument* doc = node->GetDocument();
  XMLElement* hero = doc->NewElement("hero");
  // Crufty way to get the ID of a party member
  ePIDX pid = static_cast<ePIDX>(s - party);
  for (const auto& entry : id_lookup) {
    if (entry.second == pid) {
      hero->SetAttribute("id", entry.first);
      break;
    }
  }
  node->InsertEndChild(hero);
  XMLElement* properties = doc->NewElement("properties");
  hero->InsertFirstChild(properties);
  // Core properties
  addprop(properties, "name", s->name);
  addprop(properties, "hp", s->hp);
  addprop(properties, "xp", s->xp);
  addprop(properties, "next",s->next);
  addprop(properties, "lvl", s->lvl);
  addprop(properties, "mhp", s->mhp);
  addprop(properties, "mp",s->mp);
  addprop(properties, "mmp", s->mmp);
  addprop(properties, "mrp", s->mrp);
  // All other data
  store_stats(s, hero);
  store_resistances(s, hero);
  store_spelltypes(s, hero);
  store_equipment(s, hero);
  store_spells(s, hero);
  store_lup(s, hero);
  return 0;
}

static int load_players(XMLElement* root) {
	XMLElement* heroes_elem = root->FirstChildElement("heroes");
	if (heroes_elem) {
		for (auto hero : children(heroes_elem, "hero")) {
			const char* attr = hero->Attribute("id");
			if (attr) {
				auto it = id_lookup.find(attr);
				if (it != std::end(id_lookup)) {
					load_s_player(&party[it->second], hero);
				}
			}
		}
	}
	else {
		program_death("Error loading heroes");
	}
	return 1;
}



/** Save all hero data into an XML node.
 * \param heroes array of all heroes
 * \param node a node to save into
 * \returns 0 if error otherwise 1
 */
int save_players(XMLElement* node)
{
  XMLDocument* doc = node->GetDocument();
  XMLElement* hs = doc->NewElement("heroes");
  for (const auto& p : party) {
    save_player(&p, hs);
  }
  node->InsertEndChild(hs);
  return 1;
}
// Helper functions for various chunks of data that need saving or loading
static int save_treasures(XMLElement* node) {
  auto startp = std::begin(treasure);
  auto endp = std::end(treasure);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("treasures");
	value_list(elem, startp, endp);
	node->InsertEndChild(elem);
  }
  return 1;
}
static int load_treasures(XMLElement* node) {
	auto startp = std::begin(treasure);
	auto endp = std::end(treasure);
	std::fill(startp, endp, 0);
	XMLElement* elem = node->FirstChildElement("treasure");
	if (elem && !elem->NoChildren()) {
		auto vs = parse_list(elem->FirstChild()->Value());
		auto it = startp;
		for (auto& v : vs) {
			*it++ = v;
			if (it == endp) {
				// Too much data supplied...
				program_death("Too much data supplied");
			}
		}
	}
	return 1;
}
static int save_progress(XMLElement* node) {
  auto startp = std::begin(progress);
  auto endp = std::end(progress);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("progress");
	value_list(elem, startp, endp);
	node->InsertEndChild(elem);
  }
  return 1;
}
static int  load_progress(XMLElement* node) {
	auto startp = std::begin(progress);
	auto endp = std::end(progress);
	std::fill(startp, endp, 0);
	XMLElement* elem = node->FirstChildElement("progress");
	if (elem && !elem->NoChildren()) {
		auto vs = parse_list(elem->FirstChild()->Value());
		auto it = startp;
		for (auto& v : vs) {
			*it++ = v;
			if (it == endp) {
				// Too much data supplied...
				program_death("Too much data supplied");
			}
		}
	}
	return 1;
	return 0;
}
static int save_save_spells(XMLElement* node) {
  auto startp = std::begin(save_spells);
  auto endp = std::end(save_spells);
  if (!range_is_default(startp, endp)) {
      XMLElement* elem = node->GetDocument()->NewElement("save-spells");
	  value_list(elem, startp, endp);
	  node->InsertEndChild(elem);
  }
  return 1;
}
static int  load_save_spells(XMLElement* node) {
	auto startp = std::begin(save_spells);
	auto endp = std::end(save_spells);
	std::fill(startp, endp, 0);
	XMLElement* elem = node->FirstChildElement("save-spells");
	if (elem && !elem->NoChildren()) {
		auto vs = parse_list(elem->FirstChild()->Value());
		auto it = startp;
		for (auto& v : vs) {
			*it++ = v;
			if (it == endp) {
				// Too much data supplied...
				program_death("Too much data supplied");
			}
		}
	}
	return 1;
}
static int save_specials(XMLElement* node) {
  auto startp = std::begin(player_special_items);
  auto endp = std::end(player_special_items);
  if (!range_is_default(startp, endp)) {
    XMLElement* elem = node->GetDocument()->NewElement("special");
	value_list(elem, startp, endp);
	node->InsertEndChild(elem);
  }
  return 1;
}
static int  load_specials(XMLElement* node) {
	auto startp = std::begin(treasure);
	auto endp = std::end(treasure);
	std::fill(startp, endp, 0);
	XMLElement* elem = node->FirstChildElement("treasure");
	if (elem && !elem->NoChildren()) {
		auto vs = parse_list(elem->FirstChild()->Value());
		auto it = startp;
		for (auto& v : vs) {
			*it++ = v;
			if (it == endp) {
				// Too much data supplied...
				program_death("Too much data supplied");
			}
		}
	}
	return 1;
}
static int save_global_inventory(XMLElement* node) {
	XMLDocument* doc = node->GetDocument();
	XMLElement* inventory = doc->NewElement("inventory");
	for (auto& item : g_inv) {
		if (item.quantity > 0) {
			XMLElement* item_elem = doc->NewElement("item");
			item_elem->SetAttribute("id", item.item);
			item_elem->SetAttribute("quantity", item.quantity);
			inventory->InsertEndChild(item_elem);
		}
	}
	node->InsertEndChild(inventory);
	return 1;
}
static int  load_global_inventory(XMLElement* node) {
	for (auto& item : g_inv) {
		item.item = 0;
		item.quantity = 0;
	}
	XMLElement* inventory = node->FirstChildElement("inventory");
	if (inventory) {
		auto gptr = g_inv;
		for (auto item : children(inventory, "item")) {
			gptr->item = item->IntAttribute("id");
			gptr->quantity = item->IntAttribute("quantity");
			++gptr;
		}
	}
	return 0;
}
static int save_shop_info(XMLElement* node) {
	bool visited = false;
	// Check if any shops have been visited
	for (int i = 0; i < num_shops; ++i) {
		if (shops[i].time > 0) { visited = true; break; }
	}
	// If so, we've got something to save.
	if (visited) {
		XMLDocument* doc = node->GetDocument();
		XMLElement* shops_elem = doc->NewElement("shops");
		for (int i = 0; i < num_shops; ++i) {
			s_shop& shop = shops[i];
			if (shop.time > 0) {
				XMLElement* shop_elem = doc->NewElement("shop");
				shop_elem->SetAttribute("id", i);
				shop_elem->SetAttribute("time", shop.time);
				value_list(shop_elem, std::begin(shop.items_current), std::end(shop.items_current));
				shops_elem->InsertEndChild(shop_elem);
			}
		}
		node->InsertEndChild(shops_elem);
	}
	return 1;
}
static int  load_shop_info(XMLElement* node) {
  for (auto& shop : shops) {
    shop.time = 0;
    std::fill(std::begin(shop.items_current), std::end(shop.items_current), 0);
  }
  XMLElement* shops_elem = node->FirstChildElement("shops");
  if (shops_elem) {
    for (auto el : children(shops_elem, "shop")) {
      int index = el->IntAttribute("id");
      auto items = parse_list(el->FirstChild()->Value());
      s_shop& shop = shops[index];
      shop.time = el->IntAttribute("time");
      int item_index = 0;
      for (auto& item : items) {
	if (item_index < SHOPITEMS) {
	  shop.items_current[item_index] = item;
	  ++item_index;
	}
      }
    }
  }
  return 1;
}

static int save_general_props(XMLElement* node) {
  XMLElement* properties = node->GetDocument()->NewElement("properties");
  addprop(properties, "gold", gp);
  addprop(properties, "time", khr * 60 + kmin);
  addprop(properties, "mapname", curmap);
  addprop(properties, "mapx", g_ent[0].tilex);
  addprop(properties, "mapy", g_ent[0].tiley);
  addprop(properties, "party", make_list(std::begin(pidx), std::end(pidx)));
  addprop(properties, "random-state", kq_get_random_state());
  // Save-Game Stats - id, level, hp (as a % of mhp), mp% for each member of the party
  vector<int> sgs;
  for (auto id : pidx) {
	  if (id >= 0 && id < MAXCHRS) {
		  auto& p = party[id];
		  sgs.push_back(id);
		  sgs.push_back(p.lvl);
		  sgs.push_back(p.mhp > 0 ? p.hp * 100 / p.mhp : 0);
		  sgs.push_back(p.mmp > 0 ? p.mp * 100 / p.mmp : 0);
	  }
	  else {
		  break;
	  }
  }
  addprop(properties, "sgstats", make_list(sgs.begin(), sgs.end()));
  node->InsertEndChild(properties);
  return 1;
}
static int load_general_props(XMLElement* node) {
	XMLElement* properties = node->FirstChildElement("properties");
	if (properties) {
		for (auto property : children(properties, "property")) {
			if (property->Attribute("name", "gold")) {
				gp = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "random-state")) {
				std::string state = property->Attribute("value");
				kq_set_random_state(state);
			}
			else if (property->Attribute("name", "time")) {
				int tt = property->IntAttribute("value");
				kmin = tt % 60;
				khr = (tt - kmin) / 60;
			}
			else if (property->Attribute("name", "mapname")) {
				curmap = std::string(property->Attribute("value"));
			}
			else if (property->Attribute("name", "mapx")) {
				g_ent[0].tilex = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "mapy")) {
				g_ent[0].tiley = property->IntAttribute("value");
			}
			else if (property->Attribute("name", "party")) {
				auto pps = parse_list(property->Attribute("value"));
				auto it = pps.begin();
				for (int i = 0; i < MAXCHRS; ++i) {
					if (it != pps.end()) {
						pidx[i] = static_cast<ePIDX>(*it++);
					}
					else {
						pidx[i] = PIDX_UNDEFINED;
					}
				}
			}
			// Don't need to restore anything from <sgstats>
		}
	}
	return 0;
}
/** Save everything into a node
 */
int save_game_xml(XMLElement* node) {
  node->SetAttribute("version", "93");
  save_general_props(node);
  save_players(node);
  save_treasures(node);
  save_progress(node);
  save_save_spells(node);
  save_specials(node);
  save_global_inventory(node);
  save_shop_info(node);
  return 1;
}

int save_game_xml() {
  XMLDocument doc;
  XMLElement* save = doc.NewElement("save");
  int k = save_game_xml(save);
  doc.InsertFirstChild(save);
  doc.Print();
  doc.SaveFile("test-save.xml");
  return k;
}







/** Load everything from a node
 */
int load_game_xml(XMLElement* node) {
  load_general_props(node);
  load_players(node);
  load_treasures(node);
  load_progress(node);
  load_save_spells(node);
  load_specials(node);
  load_global_inventory(node);
  load_shop_info(node);
  return 1;
}

/** Load everything from a file */
int load_game_xml(const char* filename) {
	XMLDocument doc;
	doc.LoadFile(filename);
	if (!doc.Error()) {
		return load_game_xml(doc.RootElement());
	}
	else {
		program_death("Unable to load XML file");
	}
	return 0;
}
