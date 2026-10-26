/*
--- Crawl Live (DCC) game mode bindings

module "dcc"
*/

#include "AppHdr.h"

#include "l-libs.h"

#include "cluautil.h"
#include "dcc.h"
#include "branch.h"
#include "dcc-achieve.h"
#include "dcc-boxes.h"
#include "dcc-clock.h"
#include "dcc-room.h"
#include "dcc-safe.h"
#include "coord.h"
#include "delay.h" // handle_delay, stop_delay
#include "coordit.h"
#include "dungeon.h"
#include "dgn-event.h" // dungeon_events
#include "env.h"
#include "god-item.h" // god_forbids_item
#include "items.h" // stack_iterator
#include "mapmark.h"
#include "religion.h" // str_to_god
#include "fight.h"
#include "flood-find.h"
#include "maps.h"
#include "mgen-data.h"
#include "mon-act.h"
#include "mon-death.h"
#include "mon-place.h"
#include "mon-util.h"
#include "monster.h"
#include "player.h"
#include "shopping.h"
#include "spl-util.h"
#include "state.h"
#include "stringutil.h" // starts_with
#include "tag-version.h" // TAG_MINOR_VERSION
#include "tags.h" // writer, reader
#include "terrain.h"
#include "travel.h" // RMODE_TRAVEL
#include "unwind.h"

//
// Non-user-accessible bindings (dlua).
//

/*** Is the current game a Crawl Live (DCC) game?
 * @treturn boolean
 * @function is_active
 */
LUAFN(dcc_is_active)
{
    PLUARET(boolean, crawl_state.game_is_dcc());
}

/*** Make the current script session a DCC game, with the setup level
 * generation needs. For headless scripts and tests only: raises an error in
 * a live game, where it would change the game type and branch layout.
 * @function enable_for_script
 */
LUAFN(dcc_enable_for_script_lua)
{
    if (!crawl_state.script && !crawl_state.test)
        return luaL_error(ls, "dcc.enable_for_script() is for -script/-test only");
    dcc_enable_for_script();
    return 0;
}

// The clock helpers below change game state directly; like
// enable_for_script() they are refused outside headless scripts and tests.
static bool _dcc_script_only(lua_State *ls, const char *fn)
{
    if (crawl_state.script || crawl_state.test)
        return true;
    luaL_error(ls, "dcc.%s() is for -script/-test only", fn);
    return false;
}

/*** Turns left on the current floor's clock (rounded up; 0 if none runs).
 * @treturn int
 * @function floor_turns_left
 */
LUAFN(dcc_floor_turns_left_lua)
{
    PLUARET(number, dcc_floor_turns_left());
}

/*** Full length, in turns, of the running (or paused) floor clock: the
 * tunable for the floor it belongs to. 0 if no clock runs.
 * @treturn int
 * @function floor_turns_total
 */
LUAFN(dcc_floor_turns_total_lua)
{
    PLUARET(number, dcc_floor_turns_total());
}

/*** Is the floor clock paused (the player is off the connected branches)?
 * @treturn boolean
 * @function floor_clock_paused
 */
LUAFN(dcc_floor_clock_paused_lua)
{
    PLUARET(boolean, dcc_floor_clock_paused());
}

/*** Set the current floor's clock. Script/test only.
 * @tparam int turns
 * @function set_floor_turns_left
 */
LUAFN(dcc_set_floor_turns_left_lua)
{
    const int turns = luaL_safe_checkint(ls, 1);
    if (!_dcc_script_only(ls, "set_floor_turns_left"))
        return 0;
    dcc_set_floor_turns_left(turns);
    return 0;
}

/*** Spend aut from the floor clock as a player turn would, collapsing the
 * floor at zero. Paused outside connected branches. Script/test only.
 * @tparam int aut
 * @treturn string|nil nil if the clock did not run out; otherwise
 * "fall_skipped" (a floor below exists; the fall itself is skipped in script
 * mode), "no_floor" (max-HP penalty, clock restarted) or "fell".
 * @function tick
 */
LUAFN(dcc_tick_lua)
{
    const int aut = luaL_safe_checkint(ls, 1);
    if (!_dcc_script_only(ls, "tick"))
        return 0;
    switch (dcc_tick_clock_by(aut))
    {
    case dcc_collapse_result::fell:
        PLUARET(string, "fell");
    case dcc_collapse_result::fall_skipped:
        PLUARET(string, "fall_skipped");
    case dcc_collapse_result::no_floor:
        PLUARET(string, "no_floor");
    case dcc_collapse_result::none:
    default:
        return 0;
    }
}

/*** Start a fresh clock for the current floor. Script/test only.
 * @function start_floor_clock
 */
LUAFN(dcc_start_floor_clock_lua)
{
    if (!_dcc_script_only(ls, "start_floor_clock"))
        return 0;
    dcc_start_floor_clock();
    return 0;
}

/*** Run the level-arrival hook for the current place, as load_level() does
 * on a real arrival: starts a clock on a newly created floor, resumes a
 * parked one on a floor visited before, and pauses the clock outside
 * connected branches. Script/test only.
 * @tparam boolean just_created true for a first visit to the level
 * @tparam[opt] string how how the player arrived: "stairs" (down a stone
 *   stair; the default), "branch" (through this branch's entrance), "shaft"
 *   (a shaft, trapdoor or floor collapse), "hatch" (an escape hatch down),
 *   "abyss_exit" (out of the Abyss) or "abyss" (banished or into the Abyss)
 * @function enter_floor
 */
LUAFN(dcc_enter_floor_lua)
{
    const bool just_created = lua_toboolean(ls, 1);
    const string how = lua_isnoneornil(ls, 2) ? "stairs"
                                              : luaL_checkstring(ls, 2);
    if (!_dcc_script_only(ls, "enter_floor"))
        return 0;
    dungeon_feature_type taken;
    if (how == "stairs")
        taken = DNGN_STONE_STAIRS_DOWN_I;
    else if (how == "branch")
        taken = your_branch().entry_stairs;
    else if (how == "shaft")
        taken = DNGN_TRAP_SHAFT;
    else if (how == "hatch")
        taken = DNGN_ESCAPE_HATCH_DOWN;
    else if (how == "abyss_exit")
        taken = DNGN_EXIT_ABYSS;
    else if (how == "abyss")
        taken = DNGN_ENTER_ABYSS;
    else
    {
        return luaL_error(ls, "dcc.enter_floor(): unknown arrival '%s'",
                          how.c_str());
    }
    dcc_enter_floor(just_created, taken);
    return 0;
}

/*** Run one player turn's worth of the per-turn DCC hook, as
 * player_reacts() does (dcc_tick_clock()): the achievement check, then the
 * floor clock spends aut. Script/test only.
 * @tparam int aut the turn's length
 * @treturn string|nil as dcc.tick()
 * @function player_turn
 */
LUAFN(dcc_player_turn_lua)
{
    const int aut = luaL_safe_checkint(ls, 1);
    if (!_dcc_script_only(ls, "player_turn"))
        return 0;
    unwind_var<int> taken(you.time_taken, aut);
    switch (dcc_tick_clock())
    {
    case dcc_collapse_result::fell:
        PLUARET(string, "fell");
    case dcc_collapse_result::fall_skipped:
        PLUARET(string, "fall_skipped");
    case dcc_collapse_result::no_floor:
        PLUARET(string, "no_floor");
    case dcc_collapse_result::none:
    default:
        return 0;
    }
}

/*** Is this cell a Crawl Live safe cell (FPROP_DCC_SAFE)? False out of
 * bounds.
 * @tparam int x
 * @tparam int y
 * @treturn boolean
 * @function is_safe_cell
 */
LUAFN(dcc_is_safe_cell_lua)
{
    const coord_def c(luaL_safe_checkint(ls, 1), luaL_safe_checkint(ls, 2));
    PLUARET(boolean, is_dcc_safe_cell(c));
}

// The monster at (mx, my) and the target (tx, ty) for the two checks below.
static monster *_dcc_mons_and_target(lua_State *ls, coord_def &targ)
{
    const coord_def from(luaL_safe_checkint(ls, 1), luaL_safe_checkint(ls, 2));
    targ = coord_def(luaL_safe_checkint(ls, 3), luaL_safe_checkint(ls, 4));
    if (!in_bounds(from))
        return nullptr;
    return monster_at(from);
}

/*** May the monster at (mx, my) step to the adjacent cell (tx, ty), as its
 * own move or melee attack (mon_can_move_to_pos)? Raises an error if there
 * is no monster or the cells are not adjacent.
 * @tparam int mx
 * @tparam int my
 * @tparam int tx
 * @tparam int ty
 * @treturn boolean
 * @function monster_can_step
 */
LUAFN(dcc_monster_can_step_lua)
{
    coord_def targ;
    const monster *mons = _dcc_mons_and_target(ls, targ);
    if (!mons)
        return luaL_error(ls, "dcc.monster_can_step(): no monster there");
    const coord_def delta = targ - mons->pos();
    if (delta.rdist() != 1)
        return luaL_error(ls, "dcc.monster_can_step(): target not adjacent");
    PLUARET(boolean, mon_can_move_to_pos(mons, delta));
}

/*** Could the monster at (mx, my) end up on (tx, ty) at all: the check that
 * blink, teleport, knockback, pulls, swaps and monster pathfinding share
 * (monster_habitable_grid). Raises an error if there is no monster.
 * @tparam int mx
 * @tparam int my
 * @tparam int tx
 * @tparam int ty
 * @treturn boolean
 * @function monster_can_land
 */
LUAFN(dcc_monster_can_land_lua)
{
    coord_def targ;
    const monster *mons = _dcc_mons_and_target(ls, targ);
    if (!mons)
        return luaL_error(ls, "dcc.monster_can_land(): no monster there");
    PLUARET(boolean, monster_habitable_grid(mons, targ));
}

// The actor at (x, y): the player or a monster, or nullptr.
static actor *_dcc_actor_at(lua_State *ls, int argx)
{
    const coord_def c(luaL_safe_checkint(ls, argx),
                      luaL_safe_checkint(ls, argx + 1));
    return in_bounds(c) ? actor_at(c) : nullptr;
}

/*** Could the actor at (ax, ay) harm the actor at (tx, ty) at all
 * (could_harm, the check melee, beams, explosions and clouds share)? Raises
 * an error if either cell is empty.
 * @tparam int ax
 * @tparam int ay
 * @tparam int tx
 * @tparam int ty
 * @treturn boolean
 * @function can_harm
 */
LUAFN(dcc_can_harm_lua)
{
    const actor *agent = _dcc_actor_at(ls, 1);
    const actor *target = _dcc_actor_at(ls, 3);
    if (!agent || !target)
        return luaL_error(ls, "dcc.can_harm(): no actor there");
    PLUARET(boolean, could_harm(agent, target));
}

/*** Would a player melee, fire or throw be refused where the player stands
 * (a safe cell)? Prints nothing.
 * @treturn boolean
 * @function player_attack_refused
 */
LUAFN(dcc_player_attack_refused_lua)
{
    PLUARET(boolean, dcc_player_attack_refused(true));
}

/*** Why the player cannot cast the named spell right now, as the cast
 * would report it (spell_uselessness_reason), or "" if they could.
 * @tparam string name
 * @treturn string
 * @function spell_refusal
 */
LUAFN(dcc_spell_refusal_lua)
{
    const spell_type spell = spell_by_name(luaL_checkstring(ls, 1));
    if (spell == SPELL_NO_SPELL)
        return luaL_error(ls, "dcc.spell_refusal(): no such spell");
    PLUARET(string, spell_uselessness_reason(spell, true, true, true).c_str());
}

/*** Pick a spot within radius 3 of the player for the monster at (mx, my),
 * the way a translocation drags a monster along with the player, or recall
 * brings an ally. Returns x, y, or nothing if there is no spot it may land
 * on.
 * @tparam int mx
 * @tparam int my
 * @treturn int|nil
 * @treturn int|nil
 * @function spot_near_you
 */
LUAFN(dcc_spot_near_you_lua)
{
    const actor *act = _dcc_actor_at(ls, 1);
    if (!act || !act->is_monster())
        return luaL_error(ls, "dcc.spot_near_you(): no monster there");
    const monster &mons = *act->as_monster();
    coord_def spot;
    if (!find_habitable_spot_near(you.pos(), mons.type, 3, spot, 1, &you)
        || !dcc_respot_near(mons, you.pos(), 3, spot, 1, &you))
    {
        return 0;
    }
    lua_pushnumber(ls, spot.x);
    lua_pushnumber(ls, spot.y);
    return 2;
}

/*** Summon a monster by name near the player (create_monster, preferred
 * range 2, up to 3), hostile or friendly, note where it appeared, and
 * remove it again. Returns x, y, or nothing if it could not be placed.
 * @tparam string name
 * @tparam boolean hostile
 * @treturn int|nil
 * @treturn int|nil
 * @function summon_near_you
 */
LUAFN(dcc_summon_near_you_lua)
{
    const monster_type mt = get_monster_by_name(luaL_checkstring(ls, 1));
    if (mt == MONS_NO_MONSTER)
        return luaL_error(ls, "dcc.summon_near_you(): no such monster");
    const bool hostile = lua_toboolean(ls, 2);
    monster *mons = create_monster(
        mgen_data(mt, hostile ? BEH_HOSTILE : BEH_FRIENDLY, you.pos(),
                  MHITYOU).set_range(2, 3));
    if (!mons)
        return 0;
    const coord_def pos = mons->pos();
    monster_die(*mons, KILL_RESET, NON_MONSTER, true);
    lua_pushnumber(ls, pos.x);
    lua_pushnumber(ls, pos.y);
    return 2;
}

/*** Melee the monster at (x, y) the way the quiver does (the turn is
 * marked as over first), and say whether it took a turn.
 * @tparam int x
 * @tparam int y
 * @treturn boolean
 * @function melee_takes_turn
 */
LUAFN(dcc_melee_takes_turn_lua)
{
    actor *act = _dcc_actor_at(ls, 1);
    if (!act || !act->is_monster())
        return luaL_error(ls, "dcc.melee_takes_turn(): no monster there");
    you.turn_is_over = true;
    player_fight(act->as_monster());
    const bool took_turn = you.turn_is_over;
    you.turn_is_over = false;
    PLUARET(boolean, took_turn);
}

/*** Where does the player land on arriving at the current level? Runs the
 * real arrival placement (files.cc _place_player(), through
 * dcc_place_player_as_arriving()) and returns the player's new position.
 * The player arrives from a random spot (their position on the level left
 * behind).
 * @tparam string how "new_game" or "delver_start" (the two stairs startup.cc
 *   starts a game with, put through dcc_start_stair() as load_level() does),
 *   "stairs_i", "stairs_ii", "stairs_iii" (down the matching stone stair
 *   from above), "branch" (through the branch entrance) or "shaft" (a shaft
 *   or a floor collapse, which falls the same way)
 * @treturn int x
 * @treturn int y
 * @function arrival_spot
 */
LUAFN(dcc_arrival_spot_lua)
{
    const string how = luaL_checkstring(ls, 1);
    dungeon_feature_type taken;
    if (how == "new_game")
        taken = dcc_start_stair(DNGN_STONE_STAIRS_DOWN_I, LOAD_START_GAME);
    else if (how == "delver_start")
        taken = dcc_start_stair(DNGN_STONE_STAIRS_UP_I, LOAD_START_GAME);
    else if (how == "stairs_i")
        taken = DNGN_STONE_STAIRS_DOWN_I;
    else if (how == "stairs_ii")
        taken = DNGN_STONE_STAIRS_DOWN_II;
    else if (how == "stairs_iii")
        taken = DNGN_STONE_STAIRS_DOWN_III;
    else if (how == "branch")
        taken = your_branch().entry_stairs;
    else if (how == "shaft")
        taken = DNGN_TRAP_SHAFT;
    else
        return luaL_error(ls, "dcc.arrival_spot(): unknown arrival '%s'",
                          how.c_str());

    dcc_place_player_as_arriving(taken);
    lua_pushnumber(ls, you.pos().x);
    lua_pushnumber(ls, you.pos().y);
    return 2;
}

/*** Can the player walk from a cell to a way on from this floor (a stone
 * stair or hatch down, a branch entrance, a Descent exit)? Uses trunk's
 * flood_find, walking where travel does.
 * @tparam int x
 * @tparam int y
 * @treturn boolean whether the floor has any way on at all
 * @treturn boolean whether one is reachable from (x, y)
 * @function way_on_from
 */
LUAFN(dcc_way_on_from_lua)
{
    const coord_def from(luaL_safe_checkint(ls, 1), luaL_safe_checkint(ls, 2));
    auto way_on = [](dungeon_feature_type feat)
    {
        return feat_is_stone_stair_down(feat)
               || feat == DNGN_ESCAPE_HATCH_DOWN
               || feat_is_branch_entrance(feat)
               || feat_is_descent_exitable(feat);
    };

    bool any = false;
    for (rectangle_iterator ri(1); ri; ++ri)
        any = any || way_on(env.grid(*ri));

    flood_find<feature_grid, coord_predicate> ff(env.grid, in_bounds);
    for (int f = 0; f < NUM_FEATURES; ++f)
        if (way_on(static_cast<dungeon_feature_type>(f)))
            ff.add_feat(f);
    const bool reached = in_bounds(from)
                         && ff.find_first_from(from, env.level_map_mask).x;
    lua_pushboolean(ls, any);
    lua_pushboolean(ls, reached);
    return 2;
}

/*** The shop at a cell, if any.
 * @tparam int x
 * @tparam int y
 * @treturn string|nil the name the vault gave it (nil if no shop)
 * @treturn string its full displayed name
 * @treturn boolean whether it is a plain general store (not antique)
 * @treturn boolean whether its greed lies in the range trunk's default
 *   pricing rolls for its type on this floor (dungeon.cc
 *   greed_for_shop_type()), i.e. no greed override
 * @function shop_at
 */
LUAFN(dcc_shop_at_lua)
{
    const coord_def c(luaL_safe_checkint(ls, 1), luaL_safe_checkint(ls, 2));
    shop_struct *shop = in_bounds(c) ? shop_at(c) : nullptr;
    if (!shop)
    {
        lua_pushnil(ls);
        return 1;
    }
    // greed_for_shop_type(): base + random2(5) (stock-identifying types)
    // + level / 6 + random2(level * 2 / 3); shop.level is twice the level.
    const int level = shop->level / 2;
    const int lo = 10 + level / 6;
    const int hi = 14 + level / 6 + max(level * 2 / 3 - 1, 0);
    lua_pushstring(ls, shop->shop_name.c_str());
    lua_pushstring(ls, shop_name(*shop).c_str());
    lua_pushboolean(ls, shop->type == SHOP_GENERAL);
    lua_pushboolean(ls, shoptype_identifies_stock(shop->type)
                        && shop->greed >= lo && shop->greed <= hi);
    return 4;
}

/*** Are the safe-room maps kept out of other game types? Pretends the game
 * is a normal one and asks both guards: map selection (maps.cc
 * map_selector::accept()) must find no safe room, and placing one directly
 * (dungeon.cc _build_vault_impl()) must veto before writing anything.
 * @treturn boolean whether selection rejected the maps
 * @treturn boolean whether direct placement vetoed
 * @function dcc_only_rejected_outside
 */
LUAFN(dcc_only_rejected_outside_lua)
{
    unwind_var<game_type> normal(crawl_state.type, GAME_TYPE_NORMAL);
    const bool selection = !random_map_for_tag("dcc_safe_room", false, false);

    bool vetoed = false;
    if (const map_def *mdef = find_map_by_name("dcc_safe_room_a"))
    {
        try
        {
            dgn_place_map(mdef, false, true, coord_def(GXM / 2, GYM / 2));
        }
        catch (const dgn_veto_exception &)
        {
            vetoed = true;
        }
    }
    lua_pushboolean(ls, selection);
    lua_pushboolean(ls, vetoed);
    return 2;
}

/*** The achievements unlocked so far, in unlock order.
 * @treturn table array of achievement ids
 * @function achievements
 */
LUAFN(dcc_achievements_lua)
{
    lua_newtable(ls);
    if (!you.props.exists(DCC_ACHIEVEMENTS_KEY))
        return 1;
    int i = 1;
    for (const CrawlStoreValue &v
         : you.props[DCC_ACHIEVEMENTS_KEY].get_vector())
    {
        lua_pushstring(ls, v.get_string().c_str());
        lua_rawseti(ls, -2, i++);
    }
    return 1;
}

/*** Has the player unlocked this achievement?
 * @tparam string id
 * @treturn boolean
 * @function has_achievement
 */
LUAFN(dcc_has_achievement_lua)
{
    PLUARET(boolean, dcc_has_achievement(luaL_checkstring(ls, 1)));
}

/*** Unlock an achievement: the Announcer line, a note, its box. One-shot:
 * an achievement already held does nothing. Raises an error for an id that
 * is not in the table.
 * @tparam string id
 * @treturn boolean whether it was newly unlocked
 * @function unlock
 */
LUAFN(dcc_unlock_lua)
{
    const string id = luaL_checkstring(ls, 1);
    if (!dcc_find_achievement(id))
        return luaL_error(ls, "dcc.unlock(): no achievement '%s'", id.c_str());
    PLUARET(boolean, dcc_unlock(id));
}

/*** Evaluate the state achievements now (floor reached, XL, runes, ...), as
 * the hook points do.
 * @function check_achievements
 */
LUAFN(dcc_check_achievements_lua)
{
    UNUSED(ls);
    dcc_check_achievements();
    return 0;
}

/*** The deepest floor number reached: D:1 is floor 1, and each newly
 * reached floor adds the floors descended to get there.
 * @treturn int
 * @function floors_reached
 */
LUAFN(dcc_floors_reached_lua)
{
    PLUARET(number, dcc_floors_reached());
}

/*** How many distinct uniques the player or an ally has killed.
 * @treturn int
 * @function uniques_killed
 */
LUAFN(dcc_uniques_killed_lua)
{
    PLUARET(number, dcc_uniques_killed());
}

/*** How many boxes the player holds, unopened.
 * @treturn int
 * @function box_count
 */
LUAFN(dcc_box_count_lua)
{
    PLUARET(number, dcc_box_count());
}

/*** The box tiers the player holds, unopened, as tier names.
 * @treturn table array of tier names ("Bronze", ...)
 * @function boxes
 */
LUAFN(dcc_boxes_lua)
{
    lua_newtable(ls);
    if (!you.props.exists(DCC_BOXES_KEY))
        return 1;
    int i = 1;
    for (const CrawlStoreValue &v : you.props[DCC_BOXES_KEY].get_vector())
    {
        lua_pushstring(ls,
            dcc_box_tier_name(static_cast<dcc_box_tier>(v.get_int())));
        lua_rawseti(ls, -2, i++);
    }
    return 1;
}

/*** The whole achievement table.
 * @treturn table array of { id=, name=, line=, tier= } (tier is the box
 * tier's name)
 * @function achievement_table
 */
LUAFN(dcc_achievement_table_lua)
{
    lua_newtable(ls);
    int i = 1;
    for (const dcc_achievement &ach : dcc_achievement_table())
    {
        lua_newtable(ls);
        lua_pushstring(ls, ach.id);
        lua_setfield(ls, -2, "id");
        lua_pushstring(ls, ach.name);
        lua_setfield(ls, -2, "name");
        lua_pushstring(ls, dcc_achievement_line(ach).c_str());
        lua_setfield(ls, -2, "line");
        lua_pushstring(ls, dcc_box_tier_name(ach.tier));
        lua_setfield(ls, -2, "tier");
        lua_rawseti(ls, -2, i++);
    }
    return 1;
}

/*** Forget every achievement, box and kill-tracking record, as at the start
 * of a game. Script/test only.
 * @function reset_achievements
 */
LUAFN(dcc_reset_achievements_lua)
{
    if (!_dcc_script_only(ls, "reset_achievements"))
        return 0;
    you.props.erase(DCC_ACHIEVEMENTS_KEY);
    you.props.erase(DCC_BOXES_KEY);
    you.props.erase(DCC_KILL_TIMES_KEY);
    you.props.erase(DCC_UNIQUES_KEY);
    you.props.erase(DCC_UNIQUES_KILLED_LEGACY_KEY);
    you.props.erase(DCC_FLOORS_KEY);
    you.props.erase(DCC_KILL_PLACE_KEY);
    you.props.erase(DCC_DETOUR_PLACE_KEY);
    return 0;
}

/*** Kill the monster at (x, y) through the real damage path
 * (monster::hurt() for all its hit points, then monster_die()), by the
 * player or by the actor at (ax, ay). Script/test only.
 * @tparam int x
 * @tparam int y
 * @tparam[opt] int ax the killer's position; default the player
 * @tparam[opt] int ay
 * @treturn boolean whether it died
 * @function kill
 */
LUAFN(dcc_kill_lua)
{
    actor *victim = _dcc_actor_at(ls, 1);
    actor *agent = lua_isnoneornil(ls, 3) ? &you : _dcc_actor_at(ls, 3);
    if (!_dcc_script_only(ls, "kill"))
        return 0;
    if (!victim || !victim->is_monster())
        return luaL_error(ls, "dcc.kill(): no monster there");
    if (!agent)
        return luaL_error(ls, "dcc.kill(): no killer there");
    monster *mons = victim->as_monster();
    mons->hurt(agent, mons->hit_points);
    PLUARET(boolean, !mons->alive());
}

/*** Make the monster at (x, y) leave without a kill, through monster_die()
 * with the player as the cause: "banished" (KILL_BANISHED, as banishing it
 * does), "timeout" (KILL_TIMEOUT) or "reset" (KILL_RESET). Script/test only.
 * @tparam int x
 * @tparam int y
 * @tparam string how
 * @function remove_monster
 */
LUAFN(dcc_remove_monster_lua)
{
    actor *victim = _dcc_actor_at(ls, 1);
    const string how = luaL_checkstring(ls, 3);
    if (!_dcc_script_only(ls, "remove_monster"))
        return 0;
    if (!victim || !victim->is_monster())
        return luaL_error(ls, "dcc.remove_monster(): no monster there");
    killer_type killer;
    if (how == "banished")
        killer = KILL_BANISHED;
    else if (how == "timeout")
        killer = KILL_TIMEOUT;
    else if (how == "reset")
        killer = KILL_RESET;
    else
    {
        return luaL_error(ls, "dcc.remove_monster(): unknown way '%s'",
                          how.c_str());
    }
    monster_die(*victim->as_monster(), killer, MHITYOU);
    return 0;
}

/*** Move the game's elapsed time on by some aut, as passing turns would
 * (for the kill_streak window). The floor clock is not ticked.
 * Script/test only.
 * @tparam int aut
 * @function advance_time
 */
LUAFN(dcc_advance_time_lua)
{
    const int aut = luaL_safe_checkint(ls, 1);
    if (!_dcc_script_only(ls, "advance_time"))
        return 0;
    you.elapsed_time += max(0, aut);
    return 0;
}

/*** Put all the DCC state in you.props (every dcc_ key) through the save
 * file's marshalling and back, as saving and reloading the game would.
 * Script/test only.
 * @treturn int how many keys made the round trip
 * @function save_roundtrip
 */
LUAFN(dcc_save_roundtrip_lua)
{
    if (!_dcc_script_only(ls, "save_roundtrip"))
        return 0;
    CrawlHashTable saved;
    for (const auto &entry : you.props)
        if (starts_with(entry.first, "dcc_"))
            saved[entry.first] = entry.second;

    vector<unsigned char> buf;
    writer outf(&buf);
    saved.write(outf);

    CrawlHashTable loaded;
    reader inf(buf, TAG_MINOR_VERSION);
    loaded.read(inf);

    for (const auto &entry : saved)
        you.props.erase(entry.first);
    for (const auto &entry : loaded)
        you.props[entry.first] = entry.second;
    PLUARET(number, loaded.size());
}

static const char *_dcc_open_result_name(dcc_open_result res)
{
    switch (res)
    {
    case dcc_open_result::opened:   return "opened";
    case dcc_open_result::not_dcc:  return "not_dcc";
    case dcc_open_result::no_kiosk: return "no_kiosk";
    case dcc_open_result::no_boxes: return "no_boxes";
    case dcc_open_result::declined: return "declined";
    case dcc_open_result::jammed:   return "jammed";
    }
    return "unknown";
}

/*** Open the player's boxes at the kiosk they stand on: as many, oldest
 * first, as the level has item slots for (dcc.boxes_that_fit()). Asks
 * first in a live game, unless forced; headless scripts and tests never
 * ask and open at once, where a live game takes dcc.box_open_turns() turns
 * for them.
 * @tparam[opt] boolean force skip the question
 * @tparam[opt] boolean live script/test only: start the live game's delay
 *   instead of opening at once (drive it with dcc.run_delays())
 * @treturn boolean whether the boxes opened (or began opening)
 * @treturn string "opened", "not_dcc", "no_kiosk" (not on a safe-room
 *   kiosk), "no_boxes", "declined" or "jammed" (no room for more items on
 *   the level; the boxes stay sealed)
 * @function open_boxes
 */
LUAFN(dcc_open_boxes_lua)
{
    const bool live = lua_toboolean(ls, 2);
    if (live && !_dcc_script_only(ls, "open_boxes(force, live)"))
        return 0;
    const dcc_open_result res = dcc_open_boxes(lua_toboolean(ls, 1), live);
    lua_pushboolean(ls, res == dcc_open_result::opened);
    lua_pushstring(ls, _dcc_open_result_name(res));
    return 2;
}

/*** Is the player standing on a safe-room kiosk?
 * @treturn boolean
 * @function at_kiosk
 */
LUAFN(dcc_at_kiosk_lua)
{
    PLUARET(boolean, dcc_at_kiosk());
}

/*** The kiosk marker's callback: the player has just moved onto the kiosk.
 * Only a deliberate step opens the boxes (asking first, as open_boxes());
 * travel, autoexplore, running, blinks and teleports are declined silently.
 * Headless scripts make no commands, so there only dcc.set_running()
 * counts as not deliberate.
 * @treturn boolean whether the boxes opened (or began opening)
 * @treturn string as open_boxes()
 * @function kiosk_stepped
 */
LUAFN(dcc_kiosk_stepped_lua)
{
    const dcc_open_result res = dcc_kiosk_stepped();
    lua_pushboolean(ls, res == dcc_open_result::opened);
    lua_pushstring(ls, _dcc_open_result_name(res));
    return 2;
}

/*** How many of the held boxes, oldest first, the level has item slots for
 * right now (the kiosk opens at most these).
 * @treturn int
 * @function boxes_that_fit
 */
LUAFN(dcc_boxes_that_fit_lua)
{
    PLUARET(number, dcc_boxes_that_fit());
}

/*** Pretend the player is running or travelling (or stop pretending), so
 * a script can move them across the kiosk as travel would. Script/test
 * only.
 * @tparam boolean running
 * @function set_running
 */
LUAFN(dcc_set_running_lua)
{
    const bool running = lua_toboolean(ls, 1);
    if (!_dcc_script_only(ls, "set_running"))
        return 0;
    you.running = running ? RMODE_TRAVEL : RMODE_NOT_RUNNING;
    return 0;
}

/*** Make the player a follower of a god, by name (a unique part of it
 * will do), without the joining ceremony (gifts, piety, messages); "" or
 * "No God" for none. Only
 * you.religion changes, which is what god_forbids_item() reads.
 * Script/test only.
 * @tparam string god
 * @function set_god
 */
LUAFN(dcc_set_god_lua)
{
    const string name = luaL_checkstring(ls, 1);
    if (!_dcc_script_only(ls, "set_god"))
        return 0;
    const god_type god = str_to_god(name, false);
    if (god == GOD_NO_GOD && !name.empty()
        && lowercase_string(name) != "no god")
    {
        return luaL_error(ls, "dcc: no god '%s'", name.c_str());
    }
    you.religion = god;
    return 0;
}

/*** How many items on a cell the player's god forbids (checked as if
 * identified, as acquirement checks). Script/test only.
 * @tparam int x
 * @tparam int y
 * @treturn int
 * @function forbidden_items_at
 */
LUAFN(dcc_forbidden_items_at_lua)
{
    const coord_def c(luaL_safe_checkint(ls, 1), luaL_safe_checkint(ls, 2));
    if (!_dcc_script_only(ls, "forbidden_items_at"))
        return 0;
    int n = 0;
    if (in_bounds(c))
    {
        for (stack_iterator si(c); si; ++si)
        {
            item_def item = *si;
            item.flags |= ISFLAG_IDENTIFIED;
            if (god_forbids_item(item))
                ++n;
        }
    }
    PLUARET(number, n);
}

/*** Run the player's delays (the kiosk's, after dcc.open_boxes(true,
 * true)) one turn at a time, as the main loop does, until none is left or
 * after at most n turns. Script/test only.
 * @tparam int n
 * @treturn int how many turns ran
 * @function run_delays
 */
LUAFN(dcc_run_delays_lua)
{
    const int n = luaL_safe_checkint(ls, 1);
    if (!_dcc_script_only(ls, "run_delays"))
        return 0;
    int turns = 0;
    while (turns < n && you_are_delayed())
    {
        handle_delay();
        ++turns;
    }
    PLUARET(number, turns);
}

/*** Is the player in the middle of a delay?
 * @treturn boolean
 * @function delayed
 */
LUAFN(dcc_delayed_lua)
{
    PLUARET(boolean, you_are_delayed());
}

/*** Interrupt the player's delay, as a forced interrupt would
 * (stop_delay(true)). Script/test only.
 * @function stop_delay
 */
LUAFN(dcc_stop_delay_lua)
{
    if (!_dcc_script_only(ls, "stop_delay"))
        return 0;
    stop_delay(true);
    return 0;
}

/*** Put the level's markers through the save file's marshalling and back,
 * then activate them, as saving the level and arriving on it again would
 * (tags.cc writes env.markers; load_level() activates them). Script/test
 * only.
 * @treturn int how many markers made the round trip
 * @function markers_roundtrip
 */
LUAFN(dcc_markers_roundtrip_lua)
{
    if (!_dcc_script_only(ls, "markers_roundtrip"))
        return 0;
    vector<unsigned char> buf;
    writer outf(&buf);
    env.markers.write(outf);

    dungeon_events.clear();
    reader inf(buf, TAG_MINOR_VERSION);
    env.markers.read(inf);
    env.markers.activate_all(false);
    PLUARET(number, env.markers.get_all().size());
}

/*** Activate the level's markers, as arriving on a level does (load_level()
 * runs env.markers.activate_all()); test.regenerate_level() does not, so
 * without this a script's level has no live kiosk. Script/test only.
 * @function activate_markers
 */
LUAFN(dcc_activate_markers_lua)
{
    if (!_dcc_script_only(ls, "activate_markers"))
        return 0;
    env.markers.activate_all(false);
    return 0;
}

// A box tier by its name ("Bronze", ...); raises a Lua error otherwise.
static dcc_box_tier _dcc_tier_arg(lua_State *ls, int arg)
{
    const string name = luaL_checkstring(ls, arg);
    for (int t = 0; t < NUM_DCC_BOX_TIERS; ++t)
    {
        const dcc_box_tier tier = static_cast<dcc_box_tier>(t);
        if (name == dcc_box_tier_name(tier))
            return tier;
    }
    luaL_error(ls, "dcc: no box tier '%s'", name.c_str());
    return DCC_BOX_BRONZE;
}

/*** Give the player a box of a tier, as an achievement would. Script/test
 * only.
 * @tparam string tier "Bronze", "Silver", "Gold", "Platinum", "Legendary"
 *   or "Celestial"
 * @function award_box
 */
LUAFN(dcc_award_box_lua)
{
    const dcc_box_tier tier = _dcc_tier_arg(ls, 1);
    if (!_dcc_script_only(ls, "award_box"))
        return 0;
    dcc_award_box(tier);
    return 0;
}

/*** How many items a box of a tier holds.
 * @tparam string tier
 * @treturn int
 * @function box_item_count
 */
LUAFN(dcc_box_item_count_lua)
{
    PLUARET(number, dcc_box_item_count(_dcc_tier_arg(ls, 1)));
}

/*** How many turns the kiosk takes to open this many boxes.
 * @tparam int boxes
 * @treturn int
 * @function box_open_turns
 */
LUAFN(dcc_box_open_turns_lua)
{
    PLUARET(number, dcc_box_open_turns(luaL_safe_checkint(ls, 1)));
}

/*** How many more items the level can hold before trunk starts culling
 * items to make room.
 * @treturn int
 * @function free_item_slots
 */
LUAFN(dcc_free_item_slots_lua)
{
    PLUARET(number, dcc_free_item_slots());
}

static const struct luaL_Reg dcc_dlib[] =
{
{ "is_active", dcc_is_active },
{ "enable_for_script", dcc_enable_for_script_lua },
{ "floor_turns_left", dcc_floor_turns_left_lua },
{ "floor_turns_total", dcc_floor_turns_total_lua },
{ "floor_clock_paused", dcc_floor_clock_paused_lua },
{ "set_floor_turns_left", dcc_set_floor_turns_left_lua },
{ "tick", dcc_tick_lua },
{ "start_floor_clock", dcc_start_floor_clock_lua },
{ "enter_floor", dcc_enter_floor_lua },
{ "player_turn", dcc_player_turn_lua },
{ "is_safe_cell", dcc_is_safe_cell_lua },
{ "monster_can_step", dcc_monster_can_step_lua },
{ "monster_can_land", dcc_monster_can_land_lua },
{ "can_harm", dcc_can_harm_lua },
{ "player_attack_refused", dcc_player_attack_refused_lua },
{ "spell_refusal", dcc_spell_refusal_lua },
{ "spot_near_you", dcc_spot_near_you_lua },
{ "summon_near_you", dcc_summon_near_you_lua },
{ "melee_takes_turn", dcc_melee_takes_turn_lua },
{ "arrival_spot", dcc_arrival_spot_lua },
{ "way_on_from", dcc_way_on_from_lua },
{ "shop_at", dcc_shop_at_lua },
{ "dcc_only_rejected_outside", dcc_only_rejected_outside_lua },
{ "achievements", dcc_achievements_lua },
{ "has_achievement", dcc_has_achievement_lua },
{ "unlock", dcc_unlock_lua },
{ "check_achievements", dcc_check_achievements_lua },
{ "box_count", dcc_box_count_lua },
{ "floors_reached", dcc_floors_reached_lua },
{ "uniques_killed", dcc_uniques_killed_lua },
{ "boxes", dcc_boxes_lua },
{ "achievement_table", dcc_achievement_table_lua },
{ "reset_achievements", dcc_reset_achievements_lua },
{ "kill", dcc_kill_lua },
{ "remove_monster", dcc_remove_monster_lua },
{ "advance_time", dcc_advance_time_lua },
{ "save_roundtrip", dcc_save_roundtrip_lua },
{ "open_boxes", dcc_open_boxes_lua },
{ "at_kiosk", dcc_at_kiosk_lua },
{ "kiosk_stepped", dcc_kiosk_stepped_lua },
{ "boxes_that_fit", dcc_boxes_that_fit_lua },
{ "set_running", dcc_set_running_lua },
{ "set_god", dcc_set_god_lua },
{ "forbidden_items_at", dcc_forbidden_items_at_lua },
{ "markers_roundtrip", dcc_markers_roundtrip_lua },
{ "run_delays", dcc_run_delays_lua },
{ "delayed", dcc_delayed_lua },
{ "stop_delay", dcc_stop_delay_lua },
{ "activate_markers", dcc_activate_markers_lua },
{ "award_box", dcc_award_box_lua },
{ "box_item_count", dcc_box_item_count_lua },
{ "box_open_turns", dcc_box_open_turns_lua },
{ "free_item_slots", dcc_free_item_slots_lua },
{ nullptr, nullptr }
};

void dluaopen_dcc(lua_State *ls)
{
    lua_newtable(ls);
    luaL_setfuncs(ls, dcc_dlib, 0);
    lua_setglobal(ls, "dcc");
}
