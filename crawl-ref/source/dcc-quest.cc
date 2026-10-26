/**
 * @file
 * @brief Crawl Live (DCC) floor quests: the C++ helpers behind the Lua
 *        quest engine.
 *
 * The engine itself is Lua (dat/dlua/dcc_quests.lua): each connected-branch
 * floor's safe room runs dcc_quest_setup() from its epilogue on the player's
 * first arrival, which picks the floor's one quest from a depth table, keeps
 * its state in dgn.persist, and places a DccQuestMarker whose dungeon-event
 * listeners track it. This file gives that engine what Lua cannot see well:
 * a far reachable floor cell, the floor's monsters worth hunting, whether a
 * death counts as the player's kill, the box reward, and the call that
 * settles a fetch quest when the player moves on to a new floor.
**/

#include "AppHdr.h"

#include "dcc-quest.h"

#include <queue>

#include "act-iter.h" // monster_iterator
#include "coord.h"
#include "coordit.h"
#include "dcc-safe.h" // is_dcc_safe_cell
#include "dlua.h"
#include "dungeon.h" // map_masked
#include "env.h"
#include "fprop.h"
#include "mapmark.h"
#include "message.h"
#include "mon-tentacle.h"
#include "mon-util.h"
#include "monster.h"
#include "player.h"
#include "state.h"
#include "terrain.h"

/// Can the quest's path finder walk through this cell? Floor of any kind
/// (shallow water and traps included) and doors the player can just open;
/// not deep water, lava, walls, or runed and sealed doors (vault interiors).
static bool _quest_walkable(const coord_def &c)
{
    const dungeon_feature_type feat = env.grid(c);
    if (feat_is_closed_door(feat))
        return !feat_is_runed(feat) && !feat_is_sealed(feat);
    return feat_has_solid_floor(feat);
}

/// Could a quest target (the reach cell, the fetch item) go here? Plain
/// empty floor outside every vault and safe room, with no marker, item or
/// monster on it, that a teleport could land on.
static bool _good_quest_spot(const coord_def &c)
{
    return env.grid(c) == DNGN_FLOOR
           && !is_dcc_safe_cell(c)
           && !map_masked(c, MMT_VAULT)
           && !(env.pgrid(c) & FPROP_NO_TELE_INTO)
           && env.igrid(c) == NON_ITEM
           && !monster_at(c)
           && env.markers.get_markers_at(c).empty();
}

/**
 * Pick a quest target cell: walking distance from `from` (the safe room's
 * kiosk) over cells the player can walk, among the good spots at least
 * `percent` percent of the farthest good spot's distance away, uniformly.
 *
 * @param from     Where the walk starts.
 * @param percent  0 for any reachable spot, 100 for only the farthest.
 * @param[out] spot The chosen cell.
 * @return false if no good spot is reachable.
 */
bool dcc_quest_spot(const coord_def &from, int percent, coord_def &spot)
{
    if (!in_bounds(from))
        return false;
    percent = max(0, min(100, percent));

    FixedArray<int, GXM, GYM> dist(-1);
    queue<coord_def> todo;
    dist(from) = 0;
    todo.push(from);
    int far_dist = 0;
    while (!todo.empty())
    {
        const coord_def c = todo.front();
        todo.pop();
        if (_good_quest_spot(c))
            far_dist = max(far_dist, dist(c));
        for (adjacent_iterator ai(c); ai; ++ai)
        {
            if (!in_bounds(*ai) || dist(*ai) >= 0 || !_quest_walkable(*ai))
                continue;
            dist(*ai) = dist(c) + 1;
            todo.push(*ai);
        }
    }
    if (far_dist <= 0)
        return false;

    const int min_dist = max(1, far_dist * percent / 100);
    int seen = 0;
    for (rectangle_iterator ri(1); ri; ++ri)
    {
        if (dist(*ri) < min_dist || !_good_quest_spot(*ri))
            continue;
        if (one_chance_in(++seen))
            spot = *ri;
    }
    return seen > 0;
}

/**
 * The monster types a kill quest could ask for on the current level, with
 * how many of each it holds: hostile monsters that would give the player XP,
 * outside the safe room, and not uniques (floor bosses are Phase 6a's),
 * summons, firewood or tentacles. Sorted by type, so the choice from it is
 * reproducible.
 */
vector<dcc_kill_target> dcc_quest_kill_targets()
{
    map<monster_type, int> counts;
    for (monster_iterator mi; mi; ++mi)
    {
        const monster &mons = **mi;
        if (mons.wont_attack() || mons.neutral()
            || mons.is_summoned()
            || mons_is_unique(mons.type)
            || mons.is_firewood()
            || mons_is_tentacle_or_tentacle_segment(mons.type)
            || !mons_gives_xp(mons, you)
            || is_dcc_safe_cell(mons.pos()))
        {
            continue;
        }
        ++counts[mons.type];
    }

    vector<dcc_kill_target> targets;
    for (const auto &entry : counts)
        targets.push_back({ entry.first, entry.second });
    return targets;
}

// The last death that counted as the player's kill (dcc_monster_killed()'s
// rule), set just before trunk fires DET_MONSTER_DIED for it.
static mid_t _last_kill_mid = MID_NOBODY;
static monster_type _last_kill_type = MONS_NO_MONSTER;

/**
 * A death counted as the player's (or an ally's) kill: dcc_monster_killed()
 * (dcc-achieve.cc) calls this. mon-death.cc runs that hook before it fires
 * the DET_MONSTER_DIED event, so the kill quest's listener can ask
 * dcc_quest_kill_type() whether the death it hears about counts.
 */
void dcc_quest_kill_counted(const monster &mons)
{
    _last_kill_mid = mons.mid;
    _last_kill_type = mons.type;
}

/// The type of the monster with this mid, if its death was the last one to
/// count as the player's kill; MONS_NO_MONSTER otherwise.
monster_type dcc_quest_kill_type(int mid)
{
    if (_last_kill_mid == MID_NOBODY
        || static_cast<mid_t>(mid) != _last_kill_mid)
    {
        return MONS_NO_MONSTER;
    }
    return _last_kill_type;
}

/// How many floor quests the player has completed.
int dcc_quests_done()
{
    if (!you.props.exists(DCC_QUESTS_DONE_KEY))
        return 0;
    return you.props[DCC_QUESTS_DONE_KEY].get_int();
}

/**
 * A floor quest is complete: award its box, count it, and check the
 * achievements (quest completion is one of their hook points). The quest's
 * own Announcer line is the Lua engine's.
 */
void dcc_quest_reward(dcc_box_tier tier)
{
    if (!crawl_state.game_is_dcc())
        return;
    dcc_unlock_batch batch;
    dcc_award_box(tier);
    you.props[DCC_QUESTS_DONE_KEY] = dcc_quests_done() + 1;
    dcc_check_achievements();
}

/**
 * The player has arrived on a new connected-branch floor, leaving
 * old_place behind (dcc_enter_floor()). A fetch quest left unfinished there
 * still completes if the item is in the pack, and otherwise expires
 * silently (dcc_quest_floor_left() in dcc_quests.lua).
 */
void dcc_quest_floor_left(const string &old_place)
{
    if (!crawl_state.game_is_dcc() || old_place.empty())
        return;
    if (!dlua.callfn("dcc_quest_floor_left", "s", old_place.c_str()))
    {
        mprf(MSGCH_ERROR, "dcc_quest_floor_left failed: %s",
             dlua.error.c_str());
    }
}
