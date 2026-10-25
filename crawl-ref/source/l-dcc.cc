/*
--- Crawl Live (DCC) game mode bindings

module "dcc"
*/

#include "AppHdr.h"

#include "l-libs.h"

#include "cluautil.h"
#include "dcc.h"
#include "dcc-clock.h"
#include "state.h"

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

static const struct luaL_Reg dcc_dlib[] =
{
{ "is_active", dcc_is_active },
{ "enable_for_script", dcc_enable_for_script_lua },
{ "floor_turns_left", dcc_floor_turns_left_lua },
{ "set_floor_turns_left", dcc_set_floor_turns_left_lua },
{ "tick", dcc_tick_lua },
{ "start_floor_clock", dcc_start_floor_clock_lua },
{ "enter_floor", dcc_enter_floor_lua },
{ nullptr, nullptr }
};

void dluaopen_dcc(lua_State *ls)
{
    lua_newtable(ls);
    luaL_setfuncs(ls, dcc_dlib, 0);
    lua_setglobal(ls, "dcc");
}
