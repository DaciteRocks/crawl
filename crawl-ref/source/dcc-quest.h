/**
 * @file
 * @brief Crawl Live (DCC) floor quests: the C++ helpers behind the Lua
 *        quest engine (dat/dlua/dcc_quests.lua).
**/

#pragma once

#include "dcc-achieve.h" // dcc_box_tier
#include "monster-type.h"

class monster;
struct coord_def;

// you.props key: how many floor quests the player has completed (an int).
#define DCC_QUESTS_DONE_KEY "dcc_quests_done"

// The table in dgn.persist (dat/dlua/dcc_quests.lua) holding each floor's
// quest state, keyed by level_id::describe().
#define DCC_QUESTS_PERSIST_KEY "dcc_quests"

/// A monster type a kill quest could ask for, and how many of it the floor
/// holds.
struct dcc_kill_target
{
    monster_type type;
    int count;
};

bool dcc_quest_spot(const coord_def &from, int percent, coord_def &spot);
vector<dcc_kill_target> dcc_quest_kill_targets();
void dcc_quest_kill_counted(const monster &mons);
monster_type dcc_quest_kill_type(int mid);
void dcc_quest_reward(dcc_box_tier tier);
int dcc_quests_done();
void dcc_quest_floor_left(const string &old_place);
