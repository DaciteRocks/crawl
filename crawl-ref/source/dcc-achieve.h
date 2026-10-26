/**
 * @file
 * @brief Crawl Live (DCC) achievements and the box stub they award.
**/

#pragma once

#include "killer-type.h"

class monster;

// you.props keys.
// Vector of achievement ids, in unlock order. One entry per achievement:
// an id is never appended twice, so every achievement is one-shot per game.
#define DCC_ACHIEVEMENTS_KEY "dcc_achievements"
// Vector of box tiers (dcc_box_tier ints) awarded and not yet opened.
#define DCC_BOXES_KEY "dcc_boxes"
// Vector of elapsed-time stamps (aut) of the last few counted kills, for
// kill_streak. Dropped once kill_streak is unlocked.
#define DCC_KILL_TIMES_KEY "dcc_kill_times"
// Uniques the player or an ally killed.
#define DCC_UNIQUES_KILLED_KEY "dcc_uniques_killed"

enum dcc_box_tier
{
    DCC_BOX_BRONZE,
    DCC_BOX_SILVER,
    DCC_BOX_GOLD,
    DCC_BOX_PLATINUM,
    DCC_BOX_LEGENDARY,
    DCC_BOX_CELESTIAL,
    NUM_DCC_BOX_TIERS
};

struct dcc_achievement
{
    const char *id;
    const char *name;
    const char *line;    // the Announcer's line on unlocking
    dcc_box_tier tier;
    // State predicate checked by dcc_check_achievements(); nullptr for
    // achievements that only an event unlocks (a kill, leaving a floor).
    bool (*met)();
};

const char *dcc_box_tier_name(dcc_box_tier tier);
const dcc_achievement *dcc_find_achievement(const string &id);
const vector<dcc_achievement> &dcc_achievement_table();

bool dcc_has_achievement(const string &id);
bool dcc_unlock(const string &id);
void dcc_check_achievements();
void dcc_monster_killed(const monster &mons, killer_type killer,
                        bool pet_kill, bool counted);
void dcc_floor_left(int aut_left, int aut_total);

void dcc_award_box(dcc_box_tier tier);
int dcc_box_count();
