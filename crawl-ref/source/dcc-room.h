/**
 * @file
 * @brief Crawl Live (DCC) safe-room vault: one per connected-branch floor,
 *        holding all of the floor's up stairs.
**/

#pragma once

#include "dungeon-feature-type.h"
#include "files.h"         // load_mode_type
#include "state.h"         // crawl_state, for the trunk hooks' guards

class map_def;

const map_def *dcc_arrival_map(bool minivault);
void dcc_place_safe_room();
void dcc_fix_safe_room_stairs();
dungeon_feature_type dcc_start_stair(dungeon_feature_type stair_taken,
                                     load_mode_type load_mode);

// Defined in files.cc, next to the static _place_player() it wraps; only
// dcc.arrival_spot() (l-dcc.cc) calls it.
void dcc_place_player_as_arriving(dungeon_feature_type stair_taken);
