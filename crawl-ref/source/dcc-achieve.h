/**
 * @file
 * @brief Crawl Live (DCC) achievements and the boxes they award.
**/

#pragma once

#include "dungeon-feature-type.h"
#include "killer-type.h"

class monster;
class level_id;

// you.props keys.
// Vector of achievement ids, in unlock order. One entry per achievement:
// an id is never appended twice, so every achievement is one-shot per game.
#define DCC_ACHIEVEMENTS_KEY "dcc_achievements"
// Vector of box tiers (dcc_box_tier ints) awarded and not yet opened.
#define DCC_BOXES_KEY "dcc_boxes"
// Vector of elapsed-time stamps (aut) of the last few counted kills, for
// kill_streak. Dropped once kill_streak is unlocked.
#define DCC_KILL_TIMES_KEY "dcc_kill_times"
// Vector of the monster_types (ints) of the uniques the player or an ally
// killed, each once: a unique that comes back (Boris) or revives (Natasha)
// counts once.
#define DCC_UNIQUES_KEY "dcc_uniques"
// Legacy int from the first Phase 4a build: a raw count of unique kills.
// Read (as a floor for the count) but never written.
#define DCC_UNIQUES_KILLED_LEGACY_KEY "dcc_uniques_killed"
// The number of the deepest floor reached: the first floor is 1 (D:1), and
// each newly reached connected-branch floor adds the floors descended to get
// there (one, or more for a fall within a branch).
#define DCC_FLOORS_KEY "dcc_floors"
// level_id::describe() of the connected-branch floor of the last counted
// kill, for quick_exit.
#define DCC_KILL_PLACE_KEY "dcc_kill_place"
// level_id::describe() of the connected-branch floor the player last left
// for the Abyss, a portal vault, Pan or a Zig (the floor whose clock paused).
#define DCC_DETOUR_PLACE_KEY "dcc_detour_place"

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

// What unlocks an achievement. The state kinds compare a value with the
// achievement's threshold in dcc_check_achievements(); the event kinds only
// unlock where the event happens.
enum class dcc_ach_kind
{
    event,        // unlocked directly (first_blood, big_game, quick_exit)
    streak,       // kill_streak: an event; its line takes the streak tunables
    floors,       // floor number reached >= threshold
    uniques,      // distinct uniques killed >= threshold
    xl,           // experience level >= threshold
    runes,        // runes in pack >= threshold
    collapses,    // floor collapses survived >= threshold
    god,          // worshipping any god
    orb,          // carrying the Orb of Zot
};

struct dcc_achievement
{
    const char *id;
    const char *name;
    // The Announcer's line on unlocking. For dcc_ach_kind::streak a format
    // taking the kill count and the turn window (two %d).
    const char *line;
    dcc_box_tier tier;
    dcc_ach_kind kind;
    int threshold;  // for the state kinds that compare a number
};

const char *dcc_box_tier_name(dcc_box_tier tier);
const dcc_achievement *dcc_find_achievement(const string &id);
const vector<dcc_achievement> &dcc_achievement_table();
string dcc_achievement_line(const dcc_achievement &ach);

bool dcc_has_achievement(const char *id);
bool dcc_has_achievement(const string &id);
bool dcc_unlock(const string &id);
void dcc_check_achievements();
void dcc_monster_killed(const monster &mons, killer_type killer,
                        bool pet_kill, bool counted);
void dcc_floor_reached(const string &old_place, const level_id &here);
void dcc_floor_left(const string &old_place, int aut_left, int aut_total,
                    dungeon_feature_type how);
void dcc_left_for_detour();
int dcc_floors_reached();
int dcc_uniques_killed();

void dcc_award_box(dcc_box_tier tier);
int dcc_box_count();

/// While one of these is alive, unlocks still print their line at once but
/// interrupt the player's activity only once, when the outermost batch ends.
/// The hook points (floor arrival, a kill, the per-turn check) open one, so
/// several unlocks from one event stop travel or rest a single time.
class dcc_unlock_batch
{
public:
    dcc_unlock_batch();
    ~dcc_unlock_batch();
    dcc_unlock_batch(const dcc_unlock_batch &) = delete;
    dcc_unlock_batch &operator=(const dcc_unlock_batch &) = delete;
};
