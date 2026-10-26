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

// The monster prop that marks a safe room's Floor Steward, the quest giver
// (set by the vault's MonPropsMarker, dat/des/dcc/safe_rooms.des).
#define DCC_STEWARD_KEY "dcc_steward"

/// A monster type a kill quest could ask for, and how many of it the floor
/// holds.
struct dcc_kill_target
{
    monster_type type;
    int count;
};

bool dcc_quest_spot(const coord_def &from, int percent, coord_def &spot);
vector<dcc_kill_target> dcc_quest_kill_targets(mid_t exclude = MID_NOBODY);
void dcc_quest_kill_counted(const monster &mons);
monster_type dcc_quest_kill_type(int mid);
void dcc_quest_reward(dcc_box_tier tier);
int dcc_quests_done();
void dcc_quest_floor_left(const string &old_place);

// The Floor Steward and the quest offer.
monster *dcc_find_steward();
monster *dcc_prepare_steward();
bool dcc_is_steward(const monster &mons);
void dcc_steward_seen();
string dcc_take_pending_offer();
void dcc_quest_before_command();
void dcc_steward_died(const monster &mons);
