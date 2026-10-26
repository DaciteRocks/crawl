/**
 * @file
 * @brief Crawl Live (DCC) per-floor clock and floor collapse.
**/

#pragma once

#include "dungeon-feature-type.h"
#include "level-id.h"

struct status_info;

// you.props keys. Clock time is stored in aut (BASELINE_DELAY per turn).
#define DCC_FLOOR_AUT_LEFT_KEY "dcc_floor_aut_left"
// level_id::describe() of the connected-branch floor the running clock
// belongs to. Only connected-branch floors are ever stored, so the key is
// unique (Branch:depth); in portals, the Abyss, Pan and Zigs the clock pauses.
#define DCC_FLOOR_CLOCK_PLACE_KEY "dcc_floor_clock_place"
// Full length, in aut, of the clock that is running now: the floor's tunable,
// or the short retry clock at a branch bottom. Set wherever the clock is set
// (start, resume, retry); saves from before Phase 2b lack it and derive it
// from the place instead.
#define DCC_FLOOR_AUT_TOTAL_KEY "dcc_floor_aut_total"
// Set while the running clock is the short retry clock that follows a
// collapse with no floor below (branch bottom). No drop warnings fire on it,
// and the no-floor Announcer line is not repeated.
#define DCC_FLOOR_RETRY_KEY "dcc_floor_retry"
// Hash of place -> { "aut", "total", "retry", "turn" } for connected floors the player left
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

void dcc_enter_floor(bool just_created, dungeon_feature_type how);
void dcc_start_floor_clock();
dcc_collapse_result dcc_tick_clock();
dcc_collapse_result dcc_tick_clock_by(int aut);
dcc_collapse_result dcc_collapse_floor();

int dcc_floor_turns_left();
int dcc_floor_turns_total();
bool dcc_floor_clock_paused();
void dcc_set_floor_turns_left(int turns);

void dcc_describe_floor_clock(status_info &inf);
string dcc_floor_clock_summary();
