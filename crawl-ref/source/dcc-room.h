/**
 * @file
 * @brief Crawl Live (DCC) safe-room vault: one per connected-branch floor,
 *        holding all of the floor's up stairs.
**/

#pragma once

#include "coord-def.h"
#include "dungeon-feature-type.h"
#include "files.h"         // load_mode_type
#include "god-type.h"
#include "state.h"         // crawl_state, for the trunk hooks' guards

class map_def;

const map_def *dcc_arrival_map(bool minivault);
void dcc_place_safe_room();
void dcc_fix_safe_room_stairs();
dungeon_feature_type dcc_start_stair(dungeon_feature_type stair_taken,
                                     load_mode_type load_mode);
// The D:3 sponsor booth's altars on the current level, with their gods
// (dcc.sponsor_altars(), l-dcc.cc).
vector<pair<coord_def, god_type>> dcc_sponsor_altars();

// Defined in files.cc, next to the static _place_player() it wraps; only
// dcc.arrival_spot() (l-dcc.cc) calls it.
void dcc_place_player_as_arriving(dungeon_feature_type stair_taken);
