/**
 * @file
 * @brief Crawl Live (DCC) per-floor clock and floor collapse.
 *
 * Every connected-branch floor gets its own countdown when the player first
 * arrives. It ticks with player time (inside safe rooms too) and at zero the
 * floor collapses: the player falls to the next floor, hurt but alive. In
 * portal vaults, the Abyss, Pandemonium and Ziggurats the clock pauses and
 * resumes when the player is back on a real floor. The Zot clock's
 * per-branch model (zot.cc) is the template; this clock is per floor.
 *
 * The game_is_dcc() checks in the public entry points are the authoritative
 * guard. The trunk call sites repeat it because the isolation rule (repo
 * CLAUDE.md) requires every trunk hook to be visibly DCC-only.
**/

#include "AppHdr.h"

#include "dcc-clock.h"

#include "activity-interrupt-type.h"
#include "branch.h"
#include "delay.h" // interrupt_activity
#include "message.h"
#include "notes.h"
#include "player.h"
#include "state.h"
#include "stairs.h" // floor_transition
#include "stringutil.h" // make_stringf
#include "terrain.h" // orig_terrain
#include "traps.h" // is_valid_shaft_level
#include "zot.h" // zot_touch_max_hp

// Tunables. Clock lengths live in this table and nowhere else.
struct dcc_clock_rule
{
    branch_type branch;
    int min_depth;
    int max_depth;
    int turns;
};

static const dcc_clock_rule dcc_clock_rules[] =
{
    { BRANCH_DUNGEON, 1, 3, 1500 },
};

// Every floor no rule matches.
static const int DCC_CLOCK_DEFAULT_TURNS = 1200;
// Floor collapse damage: 1/DCC_COLLAPSE_HP_DIV of max HP (at least 1, never
// lethal).
static const int DCC_COLLAPSE_HP_DIV = 5;
// Where there is no floor to fall to, the clock restarts at this many turns
// after each max-HP penalty.
static const int DCC_COLLAPSE_RETRY_TURNS = 100;
// At most this many parked clocks are kept; the oldest-parked go first.
// Descent's crumbling stairs make old floors unreachable, so only the last
// few ever matter.
static const int DCC_PARKED_CLOCKS_MAX = 16;

/// How many turns the clock for a given floor starts with.
int dcc_floor_turns_for(const level_id &place)
{
    for (const dcc_clock_rule &rule : dcc_clock_rules)
    {
        if (place.branch == rule.branch
            && place.depth >= rule.min_depth
            && place.depth <= rule.max_depth)
        {
            return rule.turns;
        }
    }
    return DCC_CLOCK_DEFAULT_TURNS;
}

static int& _floor_aut_left()
{
    return you.props[DCC_FLOOR_AUT_LEFT_KEY].get_int();
}

static CrawlHashTable& _parked_clocks()
{
    return you.props[DCC_FLOOR_CLOCKS_KEY].get_table();
}

/// Keep a floor's remaining time for a later return. A spent (or collapsed)
/// clock is never parked: its entry is dropped instead, so a floor can never
/// be re-entered at zero and collapse again on the spot.
static void _park_clock(const string &place, int aut)
{
    CrawlHashTable &parked = _parked_clocks();
    if (aut <= 0)
    {
        parked.erase(place);
        return;
    }

    CrawlHashTable &entry = parked[place].get_table();
    entry["aut"] = aut;
    entry["turn"] = you.num_turns;

    while ((int)parked.size() > DCC_PARKED_CLOCKS_MAX)
    {
        auto oldest = parked.begin();
        for (auto it = parked.begin(); it != parked.end(); ++it)
        {
            if (it->second.get_table()["turn"].get_int()
                < oldest->second.get_table()["turn"].get_int())
            {
                oldest = it;
            }
        }
        parked.erase(oldest->first);
    }
}

/// Start a fresh clock for the current floor, whatever was running before.
void dcc_start_floor_clock()
{
    const level_id here = level_id::current();
    const int turns = dcc_floor_turns_for(here);
    _floor_aut_left() = turns * BASELINE_DELAY;
    you.props[DCC_FLOOR_CLOCK_PLACE_KEY] = here.describe();

    const string msg = make_stringf("Floor clock started: %d turns", turns);
    mpr(msg + ".");
    take_note(Note(NOTE_MESSAGE, 0, 0, msg));
}

/**
 * The player has arrived on a level (files.cc load_level(), whenever the
 * arrival makes changes).
 *
 * - Not a connected-branch floor (portal vault, Abyss, Pan, Zig): nothing
 *   changes; the clock of the floor the player came from is paused.
 * - Back on the floor the running clock belongs to (returning from a portal
 *   or the Abyss): the clock just resumes.
 * - A newly created floor: the old floor's clock is parked and a fresh clock
 *   starts, whatever a stale entry might say.
 * - A floor visited before (the Descent stair peek lets you step back up,
 *   Hell exits lead back to the Vestibule, an Abyss exit can land on an older
 *   floor): its parked clock resumes. A floor with nothing parked (it
 *   collapsed, or its entry was pruned) gets a fresh clock.
 *
 * @param just_created  True on the first visit to this level (load_level's
 *                      just_created_level), as the Zot clock uses.
 */
void dcc_enter_floor(bool just_created)
{
    if (!crawl_state.game_is_dcc())
        return;

    const level_id here_id = level_id::current();
    if (!is_connected_branch(here_id))
        return;

    const string here = here_id.describe();
    if (you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY))
    {
        const string old_place =
            you.props[DCC_FLOOR_CLOCK_PLACE_KEY].get_string();
        if (old_place == here && !just_created)
            return;

        _park_clock(old_place, _floor_aut_left());

        CrawlHashTable &parked = _parked_clocks();
        if (!just_created && parked.exists(here))
        {
            const int aut = parked[here].get_table()["aut"].get_int();
            parked.erase(here);
            _floor_aut_left() = aut;
            you.props[DCC_FLOOR_CLOCK_PLACE_KEY] = here;
            return;
        }
        parked.erase(here);
    }

    dcc_start_floor_clock();
}

/**
 * No floor below to fall to (bottom of a branch such as D:12, Zot:5 or the
 * Vestibule), or the fall failed: the trunk Zot-touch penalty instead, then a
 * short clock so it repeats while the player lingers.
 */
static void _collapse_without_fall()
{
    mpr("The floor shudders, but there is nowhere left to fall. "
        "The strain tears at your life force!");
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   "Floor clock ran out with no floor below"));
    zot_touch_max_hp();
    _floor_aut_left() = DCC_COLLAPSE_RETRY_TURNS * BASELINE_DELAY;
}

/**
 * The clock hit zero. Collapse is a forced descent, never death: the player
 * drops exactly one floor and loses a fifth of max HP. The descent is a plain
 * forced floor_transition() with the shaft arrival (random landing spot, no
 * stair peek), not the shaft trap path: no trap is destroyed, no shaft
 * messages print, and no shaft milestone is recorded. Kept in one function
 * so a later "hardcore" mode can swap it.
 */
dcc_collapse_result dcc_collapse_floor()
{
    // Covers non-connected branches and branch bottoms; ignores no_shafts
    // branch flags on purpose (a collapse is not a trap).
    if (!is_valid_shaft_level(false))
    {
        _collapse_without_fall();
        return dcc_collapse_result::no_floor;
    }

    // Headless scripts and tests have no real player to move between levels;
    // floor_transition() would save and load level files mid-script. The
    // smoke script checks this result instead.
    if (crawl_state.script || crawl_state.test)
    {
        mpr("Floor collapse skipped (script mode).");
        return dcc_collapse_result::fall_skipped;
    }

    const level_id before = level_id::current();
    const level_id below(before.branch, before.depth + 1);

    mpr("The floor clock hits zero. With a long groan the floor gives way, "
        "and you plunge through it!");
    mpr("Somewhere, an audience applauds.");
    interrupt_activity(activity_interrupt::force);

    floor_transition(DNGN_TRAP_SHAFT, orig_terrain(you.pos()), below,
                     true, false, true, false);

    // Only a fall that happened costs HP and counts as a collapse; otherwise
    // the no-floor penalty applies once and the short clock restarts.
    if (level_id::current() != below)
    {
        _collapse_without_fall();
        return dcc_collapse_result::no_floor;
    }

    const int old_hp = you.hp;
    dec_hp(max(1, you.hp_max / DCC_COLLAPSE_HP_DIV), false);
    you.props[DCC_COLLAPSED_COUNT_KEY].get_int()++;
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   make_stringf("Floor %s collapsed; fell to %s, HP %d -> %d",
                                before.describe().c_str(),
                                below.describe().c_str(), old_hp, you.hp)));
    return dcc_collapse_result::fell;
}

/// Spend aut from the current floor's clock; collapse the floor at zero.
/// Paused (no-op) outside connected branches.
dcc_collapse_result dcc_tick_clock_by(int aut)
{
    if (!crawl_state.game_is_dcc()
        || !you.props.exists(DCC_FLOOR_AUT_LEFT_KEY)
        || !is_connected_branch(you.where_are_you))
    {
        return dcc_collapse_result::none;
    }

    const int left = _floor_aut_left() - aut;
    if (left > 0)
    {
        _floor_aut_left() = left;
        return dcc_collapse_result::none;
    }
    _floor_aut_left() = 0;
    return dcc_collapse_floor();
}

/// Called once per player turn from player_reacts().
void dcc_tick_clock()
{
    dcc_tick_clock_by(you.time_taken);
}

/// Turns left on the floor clock, rounded up; 0 if no clock runs. While the
/// clock is paused this is the paused floor's time.
int dcc_floor_turns_left()
{
    if (!you.props.exists(DCC_FLOOR_AUT_LEFT_KEY))
        return 0;
    const int aut = _floor_aut_left();
    return aut <= 0 ? 0 : (aut + BASELINE_DELAY - 1) / BASELINE_DELAY;
}

void dcc_set_floor_turns_left(int turns)
{
    if (!you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY))
        you.props[DCC_FLOOR_CLOCK_PLACE_KEY] = level_id::current().describe();
    _floor_aut_left() = max(0, turns) * BASELINE_DELAY;
}
