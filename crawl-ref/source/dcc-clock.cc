/**
 * @file
 * @brief Crawl Live (DCC) per-floor clock and floor collapse.
 *
 * Every floor gets its own countdown when the player first arrives. It ticks
 * with player time (inside safe rooms too) and at zero the floor collapses:
 * the player falls to the next floor, hurt but alive. The Zot clock's
 * per-branch model (zot.cc) is the template; this clock is per floor.
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
#include "stairs.h" // down_stairs
#include "stringutil.h" // make_stringf
#include "traps.h" // is_valid_shaft_level

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
 * arrival makes changes). Starts the floor's clock the first time the player
 * sets foot on it. A floor left with time remaining keeps that time: if the
 * player returns (the Descent stair peek lets you step back up for free, Hell
 * exits lead back to the Vestibule, banishment returns you), its clock
 * resumes instead of restarting, so the clock starts once per floor, not once
 * per visit.
 */
void dcc_enter_floor()
{
    if (!crawl_state.game_is_dcc())
        return;

    const string here = level_id::current().describe();
    if (you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY))
    {
        const string old_place = you.props[DCC_FLOOR_CLOCK_PLACE_KEY].get_string();
        if (old_place == here)
            return;

        // Park the clock of the floor we just left.
        CrawlHashTable &parked = you.props[DCC_FLOOR_CLOCKS_KEY].get_table();
        parked[old_place] = _floor_aut_left();

        if (parked.exists(here))
        {
            _floor_aut_left() = parked[here].get_int();
            you.props[DCC_FLOOR_CLOCK_PLACE_KEY] = here;
            parked.erase(here);
            return;
        }
    }

    dcc_start_floor_clock();
}

/**
 * No floor below to fall to (bottom of a branch, Zot:5, the Abyss, portal
 * vaults): the trunk Zot-touch penalty instead, then a short clock so it
 * repeats while the player lingers.
 */
static void _collapse_without_fall()
{
    mpr("The floor shudders, but there is nowhere left to fall. "
        "The strain tears at your life force!");
    // As zot.cc: base max HP sets the size, current max HP caps it so you
    // never drop below 1 max HP.
    const int mhp = get_real_hp(false, false);
    const int loss = min(3 + mhp / 6, you.hp_max - 1);
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   make_stringf("Floor collapse with no way down: max HP "
                                "%d -> %d", you.hp_max, you.hp_max - loss)));
    if (loss > 0)
        dec_max_hp(loss);
    interrupt_activity(activity_interrupt::force);
    _floor_aut_left() = DCC_COLLAPSE_RETRY_TURNS * BASELINE_DELAY;
}

/**
 * The clock hit zero. Collapse is a forced descent, never death: the player
 * falls one floor (Descent shafts always drop exactly one) and loses a fifth
 * of max HP. Kept in one function so a later "hardcore" mode can swap it.
 */
void dcc_collapse_floor()
{
    // Headless scripts and tests have no real player to move between levels;
    // down_stairs() would save and load level files mid-script. The smoke
    // script only checks that reaching zero does not crash.
    if (crawl_state.script || crawl_state.test)
    {
        mpr("Floor collapse skipped (script mode).");
        return;
    }

    if (you.where_are_you == BRANCH_ABYSS || !is_valid_shaft_level(false))
    {
        _collapse_without_fall();
        return;
    }

    mpr("The floor clock hits zero. With a long groan the floor gives way, "
        "and you plunge through it!");
    mpr("Somewhere, an audience applauds.");
    const int old_hp = you.hp;
    dec_hp(max(1, you.hp_max / DCC_COLLAPSE_HP_DIV), false);
    you.props[DCC_COLLAPSED_COUNT_KEY].get_int()++;
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   make_stringf("Floor collapsed; fell through, HP %d -> %d",
                                old_hp, you.hp)));
    interrupt_activity(activity_interrupt::force);

    const level_id before = level_id::current();
    down_stairs(DNGN_TRAP_SHAFT);

    // If the fall did not happen after all, don't collapse again every turn.
    if (level_id::current() == before)
        _collapse_without_fall();
}

/// Spend aut from the current floor's clock; collapse the floor at zero.
void dcc_tick_clock_by(int aut)
{
    if (!crawl_state.game_is_dcc() || !you.props.exists(DCC_FLOOR_AUT_LEFT_KEY))
        return;

    int &left = _floor_aut_left();
    left -= aut;
    if (left <= 0)
    {
        left = 0;
        dcc_collapse_floor();
    }
}

/// Called once per player turn from player_reacts().
void dcc_tick_clock()
{
    dcc_tick_clock_by(you.time_taken);
}

/// Turns left on the current floor's clock, rounded up; 0 if no clock runs.
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
