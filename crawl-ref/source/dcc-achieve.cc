/**
 * @file
 * @brief Crawl Live (DCC) achievements and the box stub they award.
 *
 * Achievements are one-shot per game: each id is appended to
 * you.props[DCC_ACHIEVEMENTS_KEY] once and never again, so nothing here
 * rewards grinding. Each is either a state predicate (floor reached, XL,
 * runes, uniques killed, ...) checked by dcc_check_achievements() at the hook
 * points (floor entry, every player turn, after a counted kill, and later box
 * opening and quest completion), or an event that unlocks it directly (a
 * kill, leaving a floor early).
 *
 * Which kills count (dcc_monster_killed()): the ones trunk gives the player
 * XP for, made by the player or a player ally (summons and charmed monsters
 * included, as trunk's pet_kill has it). Killing a summon, a no-reward or
 * tesseract spawn, a neutral, or your own ally (raised dead included) does
 * not count; neither does a banishment. Kills count wherever they
 * happen, the Abyss and portal vaults included: only the floor clock pauses
 * off the connected branches.
 *
 * The game_is_dcc() checks in the public entry points are the authoritative
 * guard; trunk call sites repeat it (repo CLAUDE.md isolation rule).
**/

#include "AppHdr.h"

#include "dcc-achieve.h"

#include "branch.h"
#include "dcc.h"
#include "dcc-clock.h" // DCC_COLLAPSED_COUNT_KEY
#include "hiscores.h" // mark_milestone
#include "items.h" // runes_in_pack
#include "mon-death.h" // YOU_KILL
#include "mon-util.h" // mons_is_unique
#include "monster.h"
#include "notes.h"
#include "place.h" // absdungeon_depth
#include "player.h"
#include "religion.h" // you_worship
#include "state.h"
#include "stringutil.h" // make_stringf

// Tunables.
// kill_streak: this many counted kills within DCC_STREAK_TURNS turns.
static const int DCC_STREAK_KILLS = 10;
static const int DCC_STREAK_TURNS = 50;
// big_game: a counted kill of a monster with HD >= XL + this.
static const int DCC_BIG_GAME_HD_OVER_XL = 5;
// quick_exit: leave a floor with more than this percentage of its clock left.
static const int DCC_QUICK_EXIT_PERCENT = 60;

static int _uniques_killed()
{
    return you.props.exists(DCC_UNIQUES_KILLED_KEY)
           ? you.props[DCC_UNIQUES_KILLED_KEY].get_int() : 0;
}

/// The deepest-numbered floor the player stands on: absolute depth, so D:N
/// is floor N. Only connected-branch floors count; portal vaults, the Abyss,
/// Pan and Zigs have depths that say nothing about progress.
static int _floor_number()
{
    if (!is_connected_branch(you.where_are_you))
        return 0;
    return absdungeon_depth(you.where_are_you, you.depth) + 1;
}

static bool _floor_at_least(int n) { return _floor_number() >= n; }
static bool _floor_2() { return _floor_at_least(2); }
static bool _floor_4() { return _floor_at_least(4); }
static bool _floor_6() { return _floor_at_least(6); }
static bool _floor_8() { return _floor_at_least(8); }
static bool _floor_10() { return _floor_at_least(10); }
static bool _floor_12() { return _floor_at_least(12); }
static bool _unique_1() { return _uniques_killed() >= 1; }
static bool _unique_3() { return _uniques_killed() >= 3; }
static bool _unique_5() { return _uniques_killed() >= 5; }
static bool _fell_through()
{
    return you.props.exists(DCC_COLLAPSED_COUNT_KEY)
           && you.props[DCC_COLLAPSED_COUNT_KEY].get_int() >= 1;
}
static bool _signed() { return !you_worship(GOD_NO_GOD); }
static bool _xl_10() { return you.experience_level >= 10; }
static bool _xl_20() { return you.experience_level >= 20; }
static bool _rune_1() { return runes_in_pack() >= 1; }
static bool _rune_3() { return runes_in_pack() >= 3; }
static bool _orb() { return player_has_orb(); }

// The table. Announcer tone: a game-show host who finds you mildly amusing.
static const vector<dcc_achievement> dcc_achievements =
{
    { "first_blood", "First Blood",
      "First blood. Try to look like you meant it.",
      DCC_BOX_BRONZE, nullptr },
    { "floor_2", "Still Standing",
      "Floor two, and you're still upright. The over-under had you at one "
      "and a half.",
      DCC_BOX_BRONZE, _floor_2 },
    { "floor_4", "Recurring Character",
      "Floor four. The writers have decided to keep you around. For now.",
      DCC_BOX_BRONZE, _floor_4 },
    { "floor_6", "Fan Favourite",
      "Floor six! Somebody out there is knitting your face onto a scarf.",
      DCC_BOX_SILVER, _floor_6 },
    { "floor_8", "Prime Time",
      "Floor eight. We've moved you to the evening slot. Please don't die "
      "during the ad break.",
      DCC_BOX_SILVER, _floor_8 },
    { "floor_10", "Season Finale Material",
      "Floor ten. Legal has started drafting your obituary, just to be "
      "ready.",
      DCC_BOX_GOLD, _floor_10 },
    { "floor_12", "Bottom of the Barrel",
      "Floor twelve, the bottom of the Dungeon. Everything below here is the "
      "bonus round.",
      DCC_BOX_GOLD, _floor_12 },
    { "unique_1", "Celebrity Guest Star",
      "You killed a named guest star! Their agent is going to be furious.",
      DCC_BOX_SILVER, _unique_1 },
    { "unique_3", "Typecast as the Villain",
      "Three famous faces, gone. The tabloids have started calling you the "
      "Cancellation.",
      DCC_BOX_GOLD, _unique_3 },
    { "unique_5", "Ratings Killer",
      "Five celebrities down. Casting would like a word about the "
      "replacement budget.",
      DCC_BOX_PLATINUM, _unique_5 },
    { "big_game", "Punching Up",
      "You took down something well out of your weight class. The bookies "
      "are in tears.",
      DCC_BOX_GOLD, nullptr },
    { "kill_streak", "Combo Meter",
      "Ten kills in fifty turns! Somebody cue the confetti cannon.",
      DCC_BOX_SILVER, nullptr },
    { "quick_exit", "Speed Run",
      "In and out with most of the clock to spare. The cleaning crew thanks "
      "you for your brevity.",
      DCC_BOX_SILVER, nullptr },
    { "fell_through", "Consolation Prize",
      "Gravity: one. You: still breathing. A Bronze box for the effort.",
      DCC_BOX_BRONZE, _fell_through },
    { "signed", "Brand Deal",
      "You've signed with a god! Please mention them in every kill.",
      DCC_BOX_BRONZE, _signed },
    { "xl_10", "Character Development",
      "Experience level ten. You're almost a protagonist now.",
      DCC_BOX_SILVER, _xl_10 },
    { "xl_20", "Main Character Energy",
      "Experience level twenty. The other contestants have started filing "
      "complaints.",
      DCC_BOX_GOLD, _xl_20 },
    { "rune_1", "Shiny Rock",
      "A rune! It glows, it hums, and it's worth more than your life "
      "insurance.",
      DCC_BOX_GOLD, _rune_1 },
    { "rune_3", "Collector's Edition",
      "Three runes. The gift shop would like to discuss merchandising.",
      DCC_BOX_PLATINUM, _rune_3 },
    { "orb", "Grand Prize",
      "The Orb of Zot! Now carry it out alive and the whole world gets to "
      "clap.",
      DCC_BOX_CELESTIAL, _orb },
};

static const char *dcc_box_tier_names[] =
{
    "Bronze", "Silver", "Gold", "Platinum", "Legendary", "Celestial",
};
COMPILE_CHECK(ARRAYSZ(dcc_box_tier_names) == NUM_DCC_BOX_TIERS);

const char *dcc_box_tier_name(dcc_box_tier tier)
{
    if (tier < 0 || tier >= NUM_DCC_BOX_TIERS)
        return "Mystery";
    return dcc_box_tier_names[tier];
}

const vector<dcc_achievement> &dcc_achievement_table()
{
    return dcc_achievements;
}

const dcc_achievement *dcc_find_achievement(const string &id)
{
    for (const dcc_achievement &ach : dcc_achievements)
        if (id == ach.id)
            return &ach;
    return nullptr;
}

bool dcc_has_achievement(const string &id)
{
    if (!you.props.exists(DCC_ACHIEVEMENTS_KEY))
        return false;
    const CrawlVector &got = you.props[DCC_ACHIEVEMENTS_KEY].get_vector();
    for (const CrawlStoreValue &v : got)
        if (v.get_string() == id)
            return true;
    return false;
}

/// Record a box for the player. A stub until Phase 4b makes boxes openable
/// at the safe-room kiosk.
void dcc_award_box(dcc_box_tier tier)
{
    if (!crawl_state.game_is_dcc())
        return;
    you.props[DCC_BOXES_KEY].get_vector().push_back(static_cast<int>(tier));
}

int dcc_box_count()
{
    if (!you.props.exists(DCC_BOXES_KEY))
        return 0;
    return you.props[DCC_BOXES_KEY].get_vector().size();
}

/**
 * Unlock an achievement: announce it, note it, mark a milestone, record it
 * and award its box. One-shot: an achievement already held does nothing.
 *
 * @param id  An id from the table. Unknown ids do nothing.
 * @return whether the achievement was newly unlocked.
 */
bool dcc_unlock(const string &id)
{
    if (!crawl_state.game_is_dcc())
        return false;
    const dcc_achievement *ach = dcc_find_achievement(id);
    if (!ach || dcc_has_achievement(id))
        return false;

    you.props[DCC_ACHIEVEMENTS_KEY].get_vector().push_back(string(ach->id));
    dcc_award_box(ach->tier);

    const char *tier = dcc_box_tier_name(ach->tier);
    dcc_announce(make_stringf("Achievement unlocked: %s! %s", ach->name,
                              ach->line));
    mprf(MSGCH_DCC, "Your prize: one %s box. Boxes open in a safe room.",
         tier);
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   make_stringf("Achievement unlocked: %s (%s box)",
                                ach->name, tier)));
    mark_milestone("dcc.achievement",
                   make_stringf("unlocked the achievement \"%s\".",
                                ach->name));
    return true;
}

/// Evaluate every state predicate and unlock what is now met.
void dcc_check_achievements()
{
    if (!crawl_state.game_is_dcc())
        return;
    for (const dcc_achievement &ach : dcc_achievements)
        if (ach.met && !dcc_has_achievement(ach.id) && ach.met())
            dcc_unlock(ach.id);
}

/// Keep the last DCC_STREAK_KILLS kill times; unlock kill_streak when they
/// all fall within DCC_STREAK_TURNS turns.
static void _track_kill_streak()
{
    if (dcc_has_achievement("kill_streak"))
    {
        you.props.erase(DCC_KILL_TIMES_KEY);
        return;
    }

    CrawlVector &times = you.props[DCC_KILL_TIMES_KEY].get_vector();
    times.push_back(you.elapsed_time);
    while ((int)times.size() > DCC_STREAK_KILLS)
        times.erase(0);

    if ((int)times.size() == DCC_STREAK_KILLS
        && times[DCC_STREAK_KILLS - 1].get_int() - times[0].get_int()
           <= DCC_STREAK_TURNS * BASELINE_DELAY)
    {
        dcc_unlock("kill_streak");
        you.props.erase(DCC_KILL_TIMES_KEY);
    }
}

/**
 * A monster died (mon-death.cc monster_die(), after the killer is settled:
 * spectral weapon and shadow kills are the player's, kills by a monster you
 * confused or charmed are yours).
 *
 * @param mons      The monster that died.
 * @param killer    The settled killer type.
 * @param pet_kill  Whether a player ally made the kill (trunk's pet_kill).
 * @param counted   Whether trunk counts it as a real kill that gives XP: not
 *                  a summon, timeout, reset, tentacle segment or banishment,
 *                  and mons_gives_xp().
 */
void dcc_monster_killed(const monster &mons, killer_type killer,
                        bool pet_kill, bool counted)
{
    if (!crawl_state.game_is_dcc() || !counted)
        return;
    if (!YOU_KILL(killer) && !pet_kill)
        return;
    // Your own allies, and monsters you made peace with, are no prize.
    if (mons.wont_attack() || testbits(mons.flags, MF_PACIFIED))
        return;

    dcc_unlock("first_blood");

    if (mons_is_unique(mons.type))
        you.props[DCC_UNIQUES_KILLED_KEY].get_int()++;

    if (mons.get_hit_dice() >= you.experience_level + DCC_BIG_GAME_HD_OVER_XL)
        dcc_unlock("big_game");

    _track_kill_streak();
    dcc_check_achievements();
}

/**
 * The player left a connected-branch floor for a newly created one
 * (dcc_enter_floor()), with this much of the floor's clock left. Not called
 * for the branch-bottom retry clock.
 */
void dcc_floor_left(int aut_left, int aut_total)
{
    if (!crawl_state.game_is_dcc() || aut_total <= 0)
        return;
    if (aut_left * 100LL > (long long)aut_total * DCC_QUICK_EXIT_PERCENT)
        dcc_unlock("quick_exit");
}
