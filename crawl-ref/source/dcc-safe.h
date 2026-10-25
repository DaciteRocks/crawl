/**
 * @file
 * @brief Crawl Live (DCC) safe cells: monsters that would attack the player
 *        cannot enter or be placed on a cell with FPROP_DCC_SAFE, and there
 *        is no combat into, out of or inside a safe room.
**/

#pragma once

#include <queue>

#include "movement-type.h"
#include "monster-type.h"
#include "spell-type.h"
#include "state.h"         // crawl_state, for the trunk hooks' guards

class actor;
class monster;
struct coord_def;
struct item_def;
struct mgen_data;

bool is_dcc_safe_cell(const coord_def& p);

// Monster movement and placement.
bool dcc_monster_barred_from(const monster &mon, const coord_def &pos);
bool dcc_generation_barred_at(const mgen_data &mg, const coord_def &pos);
bool dcc_translocation_barred(const monster &mon, const coord_def &pos,
                              movement_type mvflags);
bool dcc_flop_barred(const monster &mon, const coord_def &pos);
void dcc_adjust_monster_step(monster &mon, coord_def &mmov);
void dcc_respot_new_monster(mgen_data &mg, monster_type montype);
coord_def dcc_hostile_spot(monster_type montype, const coord_def &spot,
                           const coord_def &centre, int preferred_radius,
                           int max_radius);
bool dcc_respot_near(const monster &mon, const coord_def &where, int radius,
                     coord_def &spot, int exclude_radius,
                     const actor *in_sight_of);
bool dcc_home_search_through(const monster &mon, const coord_def &pos,
                             std::queue<coord_def> &todo);

// No combat in a safe room.
bool dcc_harm_barred(const actor *agent, const actor *target, bool announce);
bool dcc_foe_barred(const monster &mon, int foe);
bool dcc_player_attack_refused(bool quiet);
bool dcc_player_melee_refused();
bool dcc_scroll_refused(const item_def &item, bool temp, bool ident);
bool dcc_spell_refused(spell_type spell, bool temp);
bool dcc_evocation_refused(const item_def &item, bool temp);
string dcc_no_combat_reason(bool capitalise);
