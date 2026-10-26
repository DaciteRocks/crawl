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

#include "branch.h"
#include "dcc.h" // dcc_announce
#include "dcc-achieve.h"
#include "dcc-boss.h" // dcc_place_boss
#include "dcc-quest.h" // dcc_quest_floor_left
#include "message.h"
#include "notes.h"
#include "player.h"
#include "state.h"
#include "status.h"
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

// Announcer warnings as the clock runs down. A threshold is either a
// percentage of the floor's full clock or a fixed number of turns left; each
// fires once, as the clock crosses it.
struct dcc_clock_warning
{
    int percent; // of the floor's full clock; 0 if `turns` is used
    int turns;   // turns left; 0 if `percent` is used
    const char *line;
};

static const dcc_clock_warning dcc_clock_warnings[] =
{
    { 50, 0, "Halfway there, contestant! Our viewers would love to see you "
             "pick up the pace. Our sponsors would love it even more." },
    { 25, 0, "A quarter of the clock remains. This is usually the part of the "
             "show where people start running." },
    { 10, 0, "Ten percent left. The structural engineers have stopped taking "
             "your calls." },
    { 0, 50, "Fifty turns until this floor comes down. Please keep your hands "
             "and feet inside the dungeon at all times." },
    { 0, 20, "Twenty turns! The studio audience is on its feet. Mostly to "
             "get a better view of the drop." },
    { 0, 10, "Ten turns. The camera crew has already packed up this floor." },
};

static const char *DCC_COLLAPSE_LINE =
    "And that's time! Mind the drop, contestant. Do try to land on "
    "something soft.";
// %d is DCC_COLLAPSE_RETRY_TURNS.
static const char *DCC_NO_FLOOR_LINE =
    "Nowhere left to drop you, so we'll take it out of your hide instead, "
    "every %d turns until you leave. Stay tuned!";

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

static bool _in_retry()
{
    return you.props.exists(DCC_FLOOR_RETRY_KEY)
           && you.props[DCC_FLOOR_RETRY_KEY].get_bool();
}

/// Set the running clock: its time left, its full length and whether it is
/// the branch-bottom retry clock. Every place that sets the clock goes
/// through here so the three props never disagree.
static void _set_clock(int aut_left, int aut_total, bool retry)
{
    _floor_aut_left() = aut_left;
    you.props[DCC_FLOOR_AUT_TOTAL_KEY] = aut_total;
    if (retry)
        you.props[DCC_FLOOR_RETRY_KEY] = true;
    else
        you.props.erase(DCC_FLOOR_RETRY_KEY);
}

/// The full length, in aut, of the clock for the floor named by place (a
/// level_id::describe() string). Used where no total was stored: saves from
/// before Phase 2b.
static int _derived_aut_total(const string &place)
{
    try
    {
        return dcc_floor_turns_for(level_id::parse_level_id(place))
               * BASELINE_DELAY;
    }
    catch (const bad_level_id &)
    {
        return DCC_CLOCK_DEFAULT_TURNS * BASELINE_DELAY;
    }
}

/// The full length, in aut, of the clock that is running (or paused) now.
/// 0 if no clock runs.
static int _floor_aut_total()
{
    if (you.props.exists(DCC_FLOOR_AUT_TOTAL_KEY))
        return you.props[DCC_FLOOR_AUT_TOTAL_KEY].get_int();
    if (!you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY))
        return 0;
    return _derived_aut_total(
               you.props[DCC_FLOOR_CLOCK_PLACE_KEY].get_string());
}

static CrawlHashTable& _parked_clocks()
{
    return you.props[DCC_FLOOR_CLOCKS_KEY].get_table();
}

/// Keep a floor's remaining time for a later return. A spent (or collapsed)
/// clock is never parked: its entry is dropped instead, so a floor can never
/// be re-entered at zero and collapse again on the spot.
static void _park_clock(const string &place, int aut, int total,
                        bool retry)
{
    CrawlHashTable &parked = _parked_clocks();
    if (aut <= 0)
    {
        parked.erase(place);
        return;
    }

    CrawlHashTable &entry = parked[place].get_table();
    entry["aut"] = aut;
    entry["total"] = total;
    entry["retry"] = retry;
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
    _set_clock(turns * BASELINE_DELAY, turns * BASELINE_DELAY, false);
    you.props[DCC_FLOOR_CLOCK_PLACE_KEY] = here.describe();

    const string msg = make_stringf("Floor clock started: %d turns", turns);
    mpr(msg + ".");
    take_note(Note(NOTE_MESSAGE, 0, 0, msg));
}

/**
 * The clock side of arriving on a level; see dcc_enter_floor().
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
 * The achievement side rides along: a newly created floor advances the
 * floor count (dcc_floor_reached()) and may earn quick_exit for the floor
 * left (dcc_floor_left()); arriving off the connected branches marks the
 * paused floor as detoured (dcc_left_for_detour()).
 *
 * @param just_created  True on the first visit to this level (load_level's
 *                      just_created_level), as the Zot clock uses.
 * @param how           The feature the player arrived by (load_level's
 *                      stair_taken).
 */
static void _enter_floor_clock(bool just_created, dungeon_feature_type how)
{
    const level_id here_id = level_id::current();
    if (!is_connected_branch(here_id))
    {
        dcc_left_for_detour();
        return;
    }

    const string here = here_id.describe();
    if (just_created)
    {
        dcc_floor_reached(you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY)
                          ? you.props[DCC_FLOOR_CLOCK_PLACE_KEY].get_string()
                          : string(),
                          here_id);
    }
    if (you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY))
    {
        const string old_place =
            you.props[DCC_FLOOR_CLOCK_PLACE_KEY].get_string();
        if (old_place == here && !just_created)
            return;

        // Leaving a floor for a new one: maybe quick_exit. The retry clock
        // at a branch bottom does not count.
        if (just_created && old_place != here && !_in_retry())
        {
            dcc_floor_left(old_place, _floor_aut_left(), _floor_aut_total(),
                           how);
        }

        _park_clock(old_place, _floor_aut_left(), _floor_aut_total(),
                    _in_retry());

        CrawlHashTable &parked = _parked_clocks();
        if (!just_created && parked.exists(here))
        {
            CrawlHashTable &entry = parked[here].get_table();
            const int aut = entry["aut"].get_int();
            // Entries parked before Phase 2b have no total or retry flag.
            const int total = entry.exists("total")
                              ? entry["total"].get_int()
                              : _derived_aut_total(here);
            const bool retry = entry.exists("retry")
                               && entry["retry"].get_bool();
            parked.erase(here);
            _set_clock(aut, total, retry);
            you.props[DCC_FLOOR_CLOCK_PLACE_KEY] = here;
            return;
        }
        parked.erase(here);
    }

    dcc_start_floor_clock();
}

/**
 * The player has arrived on a level (files.cc load_level(), whenever the
 * arrival makes changes): run the floor clock's arrival logic, settle the
 * fetch quest of the floor left behind when this is a new floor, place a
 * new connected floor's boss if it has one (dcc_place_boss()), then check
 * the achievements (a floor reached, a floor left early). Unlocks from one
 * arrival interrupt the player once.
 *
 * @param just_created  True on the first visit to this level.
 * @param how           The feature the player arrived by (load_level's
 *                      stair_taken): quick_exit needs a down stair or a
 *                      branch entrance.
 */
void dcc_enter_floor(bool just_created, dungeon_feature_type how)
{
    if (!crawl_state.game_is_dcc())
        return;

    dcc_unlock_batch batch;
    // The connected floor the clock belonged to before this arrival.
    const string old_place = you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY)
                             ? you.props[DCC_FLOOR_CLOCK_PLACE_KEY].get_string()
                             : string();
    _enter_floor_clock(just_created, how);
    const level_id here = level_id::current();
    if (just_created && is_connected_branch(here)
        && old_place != here.describe())
    {
        dcc_quest_floor_left(old_place);
    }
    if (just_created && is_connected_branch(here))
        dcc_place_boss();
    dcc_check_achievements();
}

/**
 * No floor below to fall to (bottom of a branch such as D:12, Zot:5 or the
 * Vestibule), or the fall failed: the trunk Zot-touch penalty instead, then a
 * short retry clock so it repeats while the player lingers. The Announcer
 * line and the explanation print on the first such collapse only; later
 * retry cycles leave just the Zot-touch penalty (its own note and interrupt).
 */
static void _collapse_without_fall()
{
    if (!_in_retry())
    {
        dcc_announce(make_stringf(DCC_NO_FLOOR_LINE,
                                  DCC_COLLAPSE_RETRY_TURNS));
        mpr("The floor shudders, but there is nowhere left to fall. "
            "The strain tears at your life force!");
        take_note(Note(NOTE_MESSAGE, 0, 0,
                       "Floor clock ran out with no floor below"));
    }
    else
        mpr("The strain tears at your life force!");
    zot_touch_max_hp();
    _set_clock(DCC_COLLAPSE_RETRY_TURNS * BASELINE_DELAY,
               DCC_COLLAPSE_RETRY_TURNS * BASELINE_DELAY, true);
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

    floor_transition(DNGN_TRAP_SHAFT, orig_terrain(you.pos()), below,
                     true, false, true, false);

    // Only a fall that happened costs HP and counts as a collapse; otherwise
    // the no-floor penalty applies once and the short clock restarts. The
    // fall is announced only once it is known to have happened.
    if (level_id::current() != below)
    {
        _collapse_without_fall();
        return dcc_collapse_result::no_floor;
    }

    dcc_announce(DCC_COLLAPSE_LINE);
    mpr("With a long groan the floor gave way, and you plunged through it!");

    const int old_hp = you.hp;
    dec_hp(max(1, you.hp_max / DCC_COLLAPSE_HP_DIV), false);
    you.props[DCC_COLLAPSED_COUNT_KEY].get_int()++;
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   make_stringf("Floor %s collapsed; fell to %s, HP %d -> %d",
                                before.describe().c_str(),
                                below.describe().c_str(), old_hp, you.hp)));
    // fell_through: the arrival check ran before the count went up.
    dcc_check_achievements();
    return dcc_collapse_result::fell;
}

/// Announce the warning threshold the clock just crossed, going from
/// old_aut to new_aut left. Old-vs-new crossing, as incr_zot_clock() does
/// with bezotting_level(). If one tick crosses several thresholds, only the
/// most urgent (fewest turns left) is announced. Silent on the branch-bottom
/// retry clock: there is no drop to warn about.
static void _warn_on_threshold(int old_aut, int new_aut)
{
    if (_in_retry())
        return;
    const int total_aut = _floor_aut_total();
    const dcc_clock_warning *crossed = nullptr;
    int crossed_aut = 0;
    for (const dcc_clock_warning &warning : dcc_clock_warnings)
    {
        const int at = warning.percent
                       ? total_aut * warning.percent / 100
                       : warning.turns * BASELINE_DELAY;
        if (old_aut > at && new_aut <= at && (!crossed || at < crossed_aut))
        {
            crossed = &warning;
            crossed_aut = at;
        }
    }
    if (crossed)
        dcc_announce(crossed->line);
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

    const int old_left = _floor_aut_left();
    const int left = old_left - aut;
    if (left > 0)
    {
        _floor_aut_left() = left;
        _warn_on_threshold(old_left, left);
        return dcc_collapse_result::none;
    }
    _floor_aut_left() = 0;
    return dcc_collapse_floor();
}

/// Called once per player turn from player_reacts(). Also the per-turn
/// achievement check, so state achievements (XL, runes, the Orb, a god)
/// unlock on the turn they are met, on any level.
dcc_collapse_result dcc_tick_clock()
{
    dcc_check_achievements();
    return dcc_tick_clock_by(you.time_taken);
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

/// The full length, in turns, of the clock the player is running (or has
/// paused): the tunable for the floor it belongs to, or the retry clock's
/// length at a branch bottom. 0 if no clock runs.
int dcc_floor_turns_total()
{
    return _floor_aut_total() / BASELINE_DELAY;
}

/// Is there a floor clock that is not ticking because the player is off the
/// connected dungeon branches (portal vault, Abyss, Pan, Zig)?
bool dcc_floor_clock_paused()
{
    return crawl_state.game_is_dcc()
           && you.props.exists(DCC_FLOOR_AUT_LEFT_KEY)
           && !is_connected_branch(you.where_are_you);
}

/**
 * The "Floor (N)" status light (STATUS_DCC_FLOOR), shown next to Zot's slot.
 * Its colour follows the share of the floor's clock left: WHITE above 50%,
 * YELLOW above 25%, RED above 10%, LIGHTMAGENTA at 10% or less. While the
 * clock is paused (off the connected branches) it is DARKGREY and says so.
 * On the branch-bottom retry clock the next penalty is never far off: RED,
 * LIGHTMAGENTA for the last 10 turns.
 */
void dcc_describe_floor_clock(status_info &inf)
{
    if (!crawl_state.game_is_dcc() || !you.props.exists(DCC_FLOOR_AUT_LEFT_KEY))
        return;

    const int left = dcc_floor_turns_left();
    const int total = max(1, dcc_floor_turns_total());

    if (dcc_floor_clock_paused())
    {
        inf.light_text = make_stringf("Floor (%d, paused)", left);
        inf.light_colour = DARKGREY;
        inf.short_text = "floor clock paused";
        inf.long_text = make_stringf("The floor clock is paused here, with %d "
                                     "turns left on your last floor.", left);
        return;
    }

    inf.light_text = make_stringf("Floor (%d)", left);
    if (_in_retry())
    {
        inf.light_colour = left > 10 ? RED : LIGHTMAGENTA;
        inf.short_text = "no floor below";
        inf.long_text = make_stringf("There is no floor below to fall to. In "
                                     "%d turns the strain tears at your life "
                                     "force again, unless you leave.", left);
        return;
    }

    if (left * 100 > total * 50)
        inf.light_colour = WHITE;
    else if (left * 100 > total * 25)
        inf.light_colour = YELLOW;
    else if (left * 100 > total * 10)
        inf.light_colour = RED;
    else
        inf.light_colour = LIGHTMAGENTA;
    inf.short_text = make_stringf("floor collapses in %d turns", left);
    inf.long_text = make_stringf("This floor collapses in %d turns.", left);
}

/// One line for the time/status command (main.cc), in place of Zot's.
string dcc_floor_clock_summary()
{
    if (!crawl_state.game_is_dcc() || !you.props.exists(DCC_FLOOR_AUT_LEFT_KEY))
        return "No floor clock is running.";

    const int left = dcc_floor_turns_left();
    if (dcc_floor_clock_paused())
    {
        return make_stringf("The floor clock is paused here, with %d turns "
                            "left on your last floor.", left);
    }
    if (_in_retry())
    {
        return make_stringf("There is no floor below. In %d turns the strain "
                            "tears at your life force again, unless you "
                            "leave.", left);
    }
    return make_stringf("This floor collapses in %d turns.", left);
}

void dcc_set_floor_turns_left(int turns)
{
    if (!you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY))
        you.props[DCC_FLOOR_CLOCK_PLACE_KEY] = level_id::current().describe();
    _floor_aut_left() = max(0, turns) * BASELINE_DELAY;
}
