/**
 * @file
 * @brief Crawl Live (DCC) achievements and the boxes they award.
 *
 * Achievements are one-shot per game: each id is appended to
 * you.props[DCC_ACHIEVEMENTS_KEY] once and never again, so nothing here
 * rewards grinding. Each is either a state achievement (floor reached, XL,
 * runes, uniques killed, ...) checked by dcc_check_achievements() at the hook
 * points (floor entry, every player turn, after a counted kill, and later box
 * opening and quest completion), or an event that unlocks it directly (a
 * kill, leaving a floor early).
 *
 * Which kills count (dcc_monster_killed()): the ones trunk gives the player
 * XP for, made by the player or a player ally (summons and charmed monsters
 * included, as trunk's pet_kill has it). Trunk's rule is mons_gives_xp()
 * plus "no XP twice for a pacified monster": killing a summon, a no-reward
 * or tesseract spawn, a neutral, a pacified monster or your own permanent
 * ally (raised dead included; they are created with MF_NO_REWARD) does not
 * count, and neither does a banishment, a timeout or a reset. A hostile
 * that is only charmed still gives XP, so it counts. Kills count wherever
 * they happen, the Abyss and portal vaults included: only the floor clock
 * pauses off the connected branches.
 *
 * The game_is_dcc() checks in the public entry points are the authoritative
 * guard; trunk call sites repeat it (repo CLAUDE.md isolation rule).
**/

#include "AppHdr.h"

#include "dcc-achieve.h"

#include "activity-interrupt-type.h"
#include "branch.h"
#include "dcc-boss.h" // dcc_boss_killed
#include "dcc-clock.h" // DCC_COLLAPSED_COUNT_KEY, DCC_FLOOR_CLOCK_PLACE_KEY
#include "dcc-quest.h" // dcc_quest_kill_counted
#include "delay.h" // interrupt_activity
#include "hiscores.h" // mark_milestone
#include "items.h" // runes_in_pack
#include "level-id.h"
#include "message.h"
#include "mon-death.h" // YOU_KILL
#include "mon-util.h" // mons_is_unique
#include "monster.h"
#include "notes.h"
#include "place.h" // absdungeon_depth
#include "player.h"
#include "religion.h" // you_worship
#include "state.h"
#include "stringutil.h" // make_stringf
#include "terrain.h" // feat_is_stone_stair_down, feat_is_branch_entrance

// Tunables.
// kill_streak: this many counted kills within DCC_STREAK_TURNS turns.
static const int DCC_STREAK_KILLS = 10;
static const int DCC_STREAK_TURNS = 50;
// big_game: a counted kill of a monster with HD >= XL + this.
static const int DCC_BIG_GAME_HD_OVER_XL = 5;
// quick_exit: leave a floor with more than this percentage of its clock left.
static const int DCC_QUICK_EXIT_PERCENT = 60;

// The table. Announcer tone: a game-show host who finds you mildly amusing.
// Floors are counted as floors descended (dcc_floors_reached()), not as a
// place, so the floor lines name a count, never a branch.
static const vector<dcc_achievement> dcc_achievements =
{
    { "first_blood", "First Blood",
      "First blood. Try to look like you meant it.",
      DCC_BOX_BRONZE, dcc_ach_kind::event, 0 },
    { "floor_2", "Still Standing",
      "Floor two, and you're still upright. The over-under had you at one "
      "and a half.",
      DCC_BOX_BRONZE, dcc_ach_kind::floors, 2 },
    { "floor_4", "Recurring Character",
      "Floor four. The writers have decided to keep you around. For now.",
      DCC_BOX_BRONZE, dcc_ach_kind::floors, 4 },
    { "floor_6", "Fan Favourite",
      "Floor six! Somebody out there is knitting your face onto a scarf.",
      DCC_BOX_SILVER, dcc_ach_kind::floors, 6 },
    { "floor_8", "Prime Time",
      "Floor eight. We've moved you to the evening slot. Please don't die "
      "during the ad break.",
      DCC_BOX_SILVER, dcc_ach_kind::floors, 8 },
    { "floor_10", "Season Finale Material",
      "Floor ten. Legal has started drafting your obituary, just to be "
      "ready.",
      DCC_BOX_GOLD, dcc_ach_kind::floors, 10 },
    { "floor_12", "Bottom of the Barrel",
      "Twelve floors down. Most contestants never see this far; everything "
      "from here on is the bonus round.",
      DCC_BOX_GOLD, dcc_ach_kind::floors, 12 },
    { "unique_1", "Celebrity Guest Star",
      "You killed a named guest star! Their agent is going to be furious.",
      DCC_BOX_SILVER, dcc_ach_kind::uniques, 1 },
    { "unique_3", "Typecast as the Villain",
      "Three famous faces, gone. The tabloids have started calling you the "
      "Cancellation.",
      DCC_BOX_GOLD, dcc_ach_kind::uniques, 3 },
    { "unique_5", "Ratings Killer",
      "Five celebrities down. Casting would like a word about the "
      "replacement budget.",
      DCC_BOX_PLATINUM, dcc_ach_kind::uniques, 5 },
    { "big_game", "Punching Up",
      "You took down something well out of your weight class. The bookies "
      "are in tears.",
      DCC_BOX_GOLD, dcc_ach_kind::event, 0 },
    { "kill_streak", "Combo Meter",
      "%d kills in %d turns! Somebody cue the confetti cannon.",
      DCC_BOX_SILVER, dcc_ach_kind::streak, 0 },
    { "quick_exit", "Speed Run",
      "In, a little light murder, and out with most of the clock to spare. "
      "The cleaning crew thanks you for your brevity.",
      DCC_BOX_SILVER, dcc_ach_kind::event, 0 },
    { "fell_through", "Consolation Prize",
      "Gravity: one. You: still breathing. Have a little something for the "
      "effort.",
      DCC_BOX_BRONZE, dcc_ach_kind::collapses, 1 },
    { "signed", "Brand Deal",
      "You've signed with a god! Please mention them in every kill.",
      DCC_BOX_BRONZE, dcc_ach_kind::god, 0 },
    { "xl_10", "Character Development",
      "Experience level ten. You're almost a protagonist now.",
      DCC_BOX_SILVER, dcc_ach_kind::xl, 10 },
    { "xl_20", "Main Character Energy",
      "Experience level twenty. The other contestants have started filing "
      "complaints.",
      DCC_BOX_GOLD, dcc_ach_kind::xl, 20 },
    { "rune_1", "Shiny Rock",
      "A rune! It glows, it hums, and it's worth more than your life "
      "insurance.",
      DCC_BOX_GOLD, dcc_ach_kind::runes, 1 },
    { "rune_3", "Collector's Edition",
      "Three runes. The gift shop would like to discuss merchandising.",
      DCC_BOX_PLATINUM, dcc_ach_kind::runes, 3 },
    { "orb", "Grand Prize",
      "The Orb of Zot! Now carry it out alive and the whole world gets to "
      "clap.",
      DCC_BOX_CELESTIAL, dcc_ach_kind::orb, 0 },
    // Floor bosses (dcc-boss.cc): unlocked by dcc_boss_killed() on a
    // counted kill of the boss whose table row names the id.
    { "boss_d3", "Opening Night",
      "The Opening Act has left the stage. Feet first. A Gold box for the "
      "headliner.",
      DCC_BOX_GOLD, dcc_ach_kind::event, 0 },
    { "boss_d6", "Mid-Season Twist",
      "Nobody saw that coming, least of all the Studio Enforcer. The "
      "writers are thrilled.",
      DCC_BOX_GOLD, dcc_ach_kind::event, 0 },
    { "boss_d9", "Sweeps Week",
      "The Camera Operator caught every second of its own defeat. Ratings "
      "gold.",
      DCC_BOX_PLATINUM, dcc_ach_kind::event, 0 },
    { "boss_d12", "Season Finale",
      "The Season Finale went out with a bang, mostly yours. Roll the "
      "credits.",
      DCC_BOX_PLATINUM, dcc_ach_kind::event, 0 },
    { "boss_zot5", "Series Finale",
      "You cancelled the Series Finale. There will be no reunion special.",
      DCC_BOX_LEGENDARY, dcc_ach_kind::event, 0 },
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

/// The Announcer's line for an achievement, with any tunables filled in.
string dcc_achievement_line(const dcc_achievement &ach)
{
    if (ach.kind == dcc_ach_kind::streak)
        return make_stringf(ach.line, DCC_STREAK_KILLS, DCC_STREAK_TURNS);
    return ach.line;
}

bool dcc_has_achievement(const char *id)
{
    if (!you.props.exists(DCC_ACHIEVEMENTS_KEY))
        return false;
    for (const CrawlStoreValue &v
         : you.props[DCC_ACHIEVEMENTS_KEY].get_vector())
    {
        if (v.get_string() == id)
            return true;
    }
    return false;
}

bool dcc_has_achievement(const string &id)
{
    return dcc_has_achievement(id.c_str());
}

/// How many distinct uniques the player or an ally has killed. A save from
/// the first Phase 4a build holds a raw count instead; it is a floor here.
int dcc_uniques_killed()
{
    int n = you.props.exists(DCC_UNIQUES_KEY)
            ? you.props[DCC_UNIQUES_KEY].get_vector().size() : 0;
    if (you.props.exists(DCC_UNIQUES_KILLED_LEGACY_KEY))
        n = max(n, you.props[DCC_UNIQUES_KILLED_LEGACY_KEY].get_int());
    return n;
}

static void _record_unique(monster_type mt)
{
    CrawlVector &got = you.props[DCC_UNIQUES_KEY].get_vector();
    for (const CrawlStoreValue &v : got)
        if (v.get_int() == mt)
            return;
    got.push_back(static_cast<int>(mt));
}

/// The deepest floor number reached (D:1 is floor 1; see DCC_FLOORS_KEY).
/// 0 before the first arrival.
int dcc_floors_reached()
{
    return you.props.exists(DCC_FLOORS_KEY)
           ? you.props[DCC_FLOORS_KEY].get_int() : 0;
}

/// The floor number a place has when it is where the count starts (a new
/// game, or a save from before the count existed): its absolute depth, so
/// D:N is floor N.
static int _start_floor_number(const level_id &here)
{
    return absdungeon_depth(here.branch, here.depth) + 1;
}

/**
 * The player has just reached a newly created connected-branch floor
 * (dcc_enter_floor(), dcc-clock.cc). Advance the floor count by the floors
 * descended: within a branch the depth gained (a shaft or a collapse can drop
 * more than one), and one for a floor in another branch, whatever its
 * absolute depth says. Descent's branches start at fixed absolute depths
 * that need not match the floor the player entered from (Lair:1 is
 * absolute depth 11 wherever its entrance is), so absolute depth would
 * over-count. Each level is created once, so the count cannot be farmed.
 *
 * @param old_place  level_id::describe() of the floor whose clock was
 *                   running before (the previous connected floor), or ""
 *                   at the start of a game.
 * @param here       The floor just reached.
 */
void dcc_floor_reached(const string &old_place, const level_id &here)
{
    if (!crawl_state.game_is_dcc() || !is_connected_branch(here))
        return;

    if (!you.props.exists(DCC_FLOORS_KEY) || old_place.empty())
    {
        you.props[DCC_FLOORS_KEY] = max(dcc_floors_reached(),
                                        _start_floor_number(here));
        return;
    }
    if (old_place == here.describe())
        return;

    int step = 1;
    try
    {
        const level_id old = level_id::parse_level_id(old_place);
        if (old.branch == here.branch && here.depth > old.depth)
            step = here.depth - old.depth;
    }
    catch (const bad_level_id &)
    {
    }
    you.props[DCC_FLOORS_KEY].get_int() += step;
}

/// Record a box for the player. It opens at a safe-room kiosk
/// (dcc_open_boxes(), dcc-boxes.cc).
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

// Unlock batching (dcc_unlock_batch): nesting depth and whether an unlock
// inside the batch still owes the player an interrupt.
static int _batch_depth = 0;
static bool _batch_interrupt_owed = false;

dcc_unlock_batch::dcc_unlock_batch()
{
    ++_batch_depth;
}

dcc_unlock_batch::~dcc_unlock_batch()
{
    if (--_batch_depth == 0 && _batch_interrupt_owed)
    {
        _batch_interrupt_owed = false;
        interrupt_activity(activity_interrupt::force);
    }
}

/**
 * Unlock an achievement: record it, award its box, and announce both in one
 * Announcer line (MSGCH_DCC), with a note and a milestone. The line
 * interrupts travel and rest, once per dcc_unlock_batch. One-shot: an
 * achievement already held does nothing.
 *
 * @param id  An id from the table. Unknown ids do nothing.
 * @return whether the achievement was newly unlocked.
 */
bool dcc_unlock(const string &id)
{
    if (!crawl_state.game_is_dcc())
        return false;
    const dcc_achievement *ach = dcc_find_achievement(id);
    if (!ach || dcc_has_achievement(ach->id))
        return false;

    you.props[DCC_ACHIEVEMENTS_KEY].get_vector().push_back(string(ach->id));
    dcc_award_box(ach->tier);

    const char *tier = dcc_box_tier_name(ach->tier);
    mprf(MSGCH_DCC, "Achievement unlocked: %s! %s A %s box is added to your "
                    "winnings.",
         ach->name, dcc_achievement_line(*ach).c_str(), tier);
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   make_stringf("Achievement unlocked: %s (%s box)",
                                ach->name, tier)));
    mark_milestone("dcc.achievement",
                   make_stringf("unlocked the achievement \"%s\".",
                                ach->name));
    if (_batch_depth > 0)
        _batch_interrupt_owed = true;
    else
        interrupt_activity(activity_interrupt::force);
    return true;
}

/// Is a state achievement's condition met now? False for the event kinds.
static bool _state_met(const dcc_achievement &ach)
{
    switch (ach.kind)
    {
    case dcc_ach_kind::floors:
        return dcc_floors_reached() >= ach.threshold;
    case dcc_ach_kind::uniques:
        return dcc_uniques_killed() >= ach.threshold;
    case dcc_ach_kind::xl:
        return you.experience_level >= ach.threshold;
    case dcc_ach_kind::runes:
        return runes_in_pack() >= ach.threshold;
    case dcc_ach_kind::collapses:
        return you.props.exists(DCC_COLLAPSED_COUNT_KEY)
               && you.props[DCC_COLLAPSED_COUNT_KEY].get_int()
                  >= ach.threshold;
    case dcc_ach_kind::god:
        return !you_worship(GOD_NO_GOD);
    case dcc_ach_kind::orb:
        return player_has_orb();
    case dcc_ach_kind::event:
    case dcc_ach_kind::streak:
    default:
        return false;
    }
}

static bool _is_state_kind(dcc_ach_kind kind)
{
    return kind != dcc_ach_kind::event && kind != dcc_ach_kind::streak;
}

/// Evaluate every state achievement and unlock what is now met. Runs every
/// player turn, so it allocates nothing: one pass over the held ids marks
/// which table rows are held, and once every state achievement is held it
/// stops there.
void dcc_check_achievements()
{
    if (!crawl_state.game_is_dcc())
        return;

    const size_t n = dcc_achievements.size();
    ASSERT(n <= 64);
    uint64_t held = 0;
    uint64_t state_rows = 0;
    for (size_t i = 0; i < n; ++i)
        if (_is_state_kind(dcc_achievements[i].kind))
            state_rows |= uint64_t(1) << i;

    if (you.props.exists(DCC_ACHIEVEMENTS_KEY))
    {
        for (const CrawlStoreValue &v
             : you.props[DCC_ACHIEVEMENTS_KEY].get_vector())
        {
            const string &got = v.get_string();
            for (size_t i = 0; i < n; ++i)
            {
                if (got == dcc_achievements[i].id)
                {
                    held |= uint64_t(1) << i;
                    break;
                }
            }
        }
    }
    if ((held & state_rows) == state_rows)
        return;

    dcc_unlock_batch batch;
    for (size_t i = 0; i < n; ++i)
    {
        const dcc_achievement &ach = dcc_achievements[i];
        if ((state_rows & ~held & (uint64_t(1) << i)) && _state_met(ach))
            dcc_unlock(ach.id);
    }
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
 * confused or charmed are yours). It runs before trunk awards the kill's XP,
 * so an XL achievement that XP earns unlocks on the per-turn check that
 * follows (dcc_tick_clock()), not here; the check here is for the uniques
 * count this kill just raised.
 *
 * @param mons      The monster that died.
 * @param killer    The settled killer type.
 * @param pet_kill  Whether a player ally made the kill (trunk's pet_kill).
 * @param counted   Whether trunk counts it as a real kill that gives XP: not
 *                  a summon, timeout, reset, tentacle segment or banishment,
 *                  and mons_gives_xp().
 *
 * Every death comes here, counted or not, so the Floor Steward's is seen
 * whatever killed it.
 */
void dcc_monster_killed(const monster &mons, killer_type killer,
                        bool pet_kill, bool counted)
{
    if (!crawl_state.game_is_dcc())
        return;
    // The Floor Steward, however it died, earns no achievement or quest
    // credit (and, MF_NO_REWARD, no XP); it voids its floor's quest
    // instead. Trunk's kill list (you.kills) still records it: that ran
    // earlier in monster_die().
    if (dcc_is_steward(mons))
    {
        dcc_steward_died(mons);
        return;
    }
    if (!counted)
        return;
    if (!YOU_KILL(killer) && !pet_kill)
        return;
    // Trunk's other no-XP case: a pacified monster already paid out its XP
    // (_calc_player_experience()). Permanent allies fail mons_gives_xp();
    // a charmed hostile passes it and counts, as it gives XP.
    if (testbits(mons.flags, MF_PACIFIED))
        return;

    // The kill quest's DET_MONSTER_DIED listener asks about this death next.
    dcc_quest_kill_counted(mons);

    dcc_unlock_batch batch;
    dcc_boss_killed(mons);
    dcc_unlock("first_blood");

    if (mons_is_unique(mons.type))
        _record_unique(mons.type);

    const level_id here = level_id::current();
    if (is_connected_branch(here))
        you.props[DCC_KILL_PLACE_KEY] = here.describe();

    if (mons.get_hit_dice() >= you.experience_level + DCC_BIG_GAME_HD_OVER_XL)
        dcc_unlock("big_game");

    _track_kill_streak();
    dcc_check_achievements();
}

/// The player is arriving off the connected branches (Abyss, portal vault,
/// Pan, Zig): remember which floor's clock paused, so leaving that floor
/// later cannot be a quick_exit.
void dcc_left_for_detour()
{
    if (!crawl_state.game_is_dcc()
        || !you.props.exists(DCC_FLOOR_CLOCK_PLACE_KEY))
    {
        return;
    }
    you.props[DCC_DETOUR_PLACE_KEY] =
        you.props[DCC_FLOOR_CLOCK_PLACE_KEY].get_string();
}

/**
 * The player left a connected-branch floor for a newly created one
 * (dcc_enter_floor()), with this much of the floor's clock left. Not called
 * for the branch-bottom retry clock. quick_exit needs all of:
 * - a deliberate descent: a down stone stair or a branch entrance (not a
 *   shaft, trapdoor, hatch, collapse, banishment or Abyss exit);
 * - no Abyss, portal, Pan or Zig visit from that floor;
 * - at least one counted kill on that floor (so diving D:1 on turn one, or
 *   any floor without a fight, earns nothing);
 * - more than DCC_QUICK_EXIT_PERCENT of its clock left.
 *
 * @param old_place  level_id::describe() of the floor left.
 * @param aut_left   Its clock's time left.
 * @param aut_total  Its clock's full length.
 * @param how        The feature the player arrived by (load_level()'s
 *                   stair_taken).
 */
void dcc_floor_left(const string &old_place, int aut_left, int aut_total,
                    dungeon_feature_type how)
{
    if (!crawl_state.game_is_dcc() || aut_total <= 0)
        return;
    if (!feat_is_stone_stair_down(how) && !feat_is_branch_entrance(how))
        return;
    if (!you.props.exists(DCC_KILL_PLACE_KEY)
        || you.props[DCC_KILL_PLACE_KEY].get_string() != old_place)
    {
        return;
    }
    if (you.props.exists(DCC_DETOUR_PLACE_KEY)
        && you.props[DCC_DETOUR_PLACE_KEY].get_string() == old_place)
    {
        return;
    }
    if (aut_left * 100LL > (long long)aut_total * DCC_QUICK_EXIT_PERCENT)
        dcc_unlock("quick_exit");
}
