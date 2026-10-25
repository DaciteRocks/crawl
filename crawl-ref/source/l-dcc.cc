/*
--- Crawl Live (DCC) game mode bindings

module "dcc"
*/

#include "AppHdr.h"

#include "l-libs.h"

#include "cluautil.h"
#include "dcc.h"
#include "branch.h"
#include "dcc-clock.h"
#include "dcc-safe.h"
#include "coord.h"
#include "dungeon.h"
#include "fight.h"
#include "mgen-data.h"
#include "mon-act.h"
#include "mon-death.h"
#include "mon-place.h"
#include "mon-util.h"
#include "monster.h"
#include "player.h"
#include "spl-util.h"
#include "state.h"
#include "terrain.h"

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
 * @function enter_floor
 */
LUAFN(dcc_enter_floor_lua)
{
    const bool just_created = lua_toboolean(ls, 1);
    if (!_dcc_script_only(ls, "enter_floor"))
        return 0;
    dcc_enter_floor(just_created);
    return 0;
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

/*** Where would the player land on arriving at the current level, as
 * load_level() places them? Mirrors the destination rule of
 * _get_dest_stair_type() (files.cc) for these arrivals, then asks the same
 * dgn_find_nearby_stair() the real arrival uses, from a random spot (the
 * player's position on the level left behind).
 * @tparam string how "new_game" (D:1 start), "stairs_i", "stairs_ii",
 *   "stairs_iii" (down the matching stone stair from above) or "branch"
 *   (through the branch entrance, onto the branch exit at X:1)
 * @treturn int x
 * @treturn int y
 * @function arrival_spot
 */
LUAFN(dcc_arrival_spot_lua)
{
    const string how = luaL_checkstring(ls, 1);
    dungeon_feature_type dest;
    bool find_first = true;
    if (how == "new_game" || how == "stairs_i")
        dest = DNGN_STONE_STAIRS_UP_I;
    else if (how == "stairs_ii")
        dest = DNGN_STONE_STAIRS_UP_II;
    else if (how == "stairs_iii")
        dest = DNGN_STONE_STAIRS_UP_III;
    else if (how == "branch")
        dest = your_branch().exit_stairs;
    else
        return luaL_error(ls, "dcc.arrival_spot(): unknown arrival '%s'",
                          how.c_str());

    // In Hell a stone stair down lands on a random branch exit.
    if (how != "new_game" && how != "branch" && player_in_hell())
    {
        dest = your_branch().exit_stairs;
        find_first = false;
    }

    const coord_def spot = dgn_find_nearby_stair(dest, random_in_bounds(),
                                                 find_first, "");
    lua_pushnumber(ls, spot.x);
    lua_pushnumber(ls, spot.y);
    return 2;
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
{ nullptr, nullptr }
};

void dluaopen_dcc(lua_State *ls)
{
    lua_newtable(ls);
    luaL_setfuncs(ls, dcc_dlib, 0);
    lua_setglobal(ls, "dcc");
}
