/**
 * @file
 * @brief Crawl Live (DCC) per-floor clock and floor collapse.
**/

#pragma once

#include "level-id.h"

// you.props keys. Clock time is stored in aut (BASELINE_DELAY per turn).
#define DCC_FLOOR_AUT_LEFT_KEY "dcc_floor_aut_left"
// level_id::describe() of the connected-branch floor the running clock
// belongs to. Only connected-branch floors are ever stored, so the key is
// unique (Branch:depth); in portals, the Abyss, Pan and Zigs the clock pauses.
#define DCC_FLOOR_CLOCK_PLACE_KEY "dcc_floor_clock_place"
// Hash of place -> { "aut", "turn" } for connected floors the player left
// with time remaining (the Descent stair peek, a Hell exit back to the
// Vestibule, an Abyss exit onto an older floor). Capped; oldest go first.
#define DCC_FLOOR_CLOCKS_KEY "dcc_floor_clocks"
#define DCC_COLLAPSED_COUNT_KEY "dcc_collapsed_count"

enum class dcc_collapse_result
{
    none,         // the clock did not run out
    fell,         // the player fell to the floor below
    fall_skipped, // there is a floor below, but -script/-test skips the fall
    no_floor,     // no floor below (or the fall failed): max-HP penalty
};

int dcc_floor_turns_for(const level_id &place);

void dcc_enter_floor(bool just_created);
void dcc_start_floor_clock();
void dcc_tick_clock();
dcc_collapse_result dcc_tick_clock_by(int aut);
dcc_collapse_result dcc_collapse_floor();

int dcc_floor_turns_left();
void dcc_set_floor_turns_left(int turns);
