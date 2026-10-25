/*
--- Crawl Live (DCC) game mode bindings

module "dcc"
*/

#include "AppHdr.h"

#include "l-libs.h"

#include "cluautil.h"
#include "dcc.h"
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

static const struct luaL_Reg dcc_dlib[] =
{
{ "is_active", dcc_is_active },
{ "enable_for_script", dcc_enable_for_script_lua },
{ nullptr, nullptr }
};

void dluaopen_dcc(lua_State *ls)
{
    lua_newtable(ls);
    luaL_setfuncs(ls, dcc_dlib, 0);
    lua_setglobal(ls, "dcc");
}
