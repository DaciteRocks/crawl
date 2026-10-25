/**
 * @file
 * @brief Crawl Live (DCC) per-floor clock and floor collapse.
**/

#pragma once

#include "level-id.h"

// you.props keys. Clock time is stored in aut (BASELINE_DELAY per turn).
#define DCC_FLOOR_AUT_LEFT_KEY "dcc_floor_aut_left"
// level_id::describe() of the floor the running clock belongs to.
#define DCC_FLOOR_CLOCK_PLACE_KEY "dcc_floor_clock_place"
// Hash of place -> aut left, for floors the player left with time remaining
// (the Descent stair peek, Hell exits, a trip to the Abyss).
#define DCC_FLOOR_CLOCKS_KEY "dcc_floor_clocks"
#define DCC_COLLAPSED_COUNT_KEY "dcc_collapsed_count"

int dcc_floor_turns_for(const level_id &place);

void dcc_enter_floor();
void dcc_start_floor_clock();
void dcc_tick_clock();
void dcc_tick_clock_by(int aut);
void dcc_collapse_floor();

int dcc_floor_turns_left();
void dcc_set_floor_turns_left(int turns);
