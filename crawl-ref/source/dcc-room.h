/**
 * @file
 * @brief Crawl Live (DCC) safe-room vault: one per connected-branch floor,
 *        holding all of the floor's up stairs.
**/

#pragma once

#include "state.h"         // crawl_state, for the trunk hooks' guards

class map_def;

const map_def *dcc_arrival_map(bool minivault);
void dcc_place_safe_room();
void dcc_fix_safe_room_stairs();
