/**
 * @file
 * @brief Crawl Live (DCC) game mode: setup and shared state.
 *
 * DCC is a Descent variant: game_is_descent() is true for it, so the Descent
 * dungeon structure applies unchanged. This file holds the DCC-only setup;
 * trunk files reach it through one-line calls guarded by game_is_dcc().
**/

#include "AppHdr.h"

#include "dcc.h"

#include "game-type.h"
#include "ng-init.h"
#include "player.h"
#include "state.h"

/**
 * DCC state for a fresh game. Called from _setup_generic() for a real new
 * game, and from dcc_enable_for_script() for headless scripts. Branch depths
 * and the Descent branch keys are not set here: initialise_branch_depths()
 * already does that for every Descent-type game.
 */
void dcc_setup_new_game()
{
    ASSERT(crawl_state.game_is_dcc());
    you.props[DCC_VERSION_KEY] = DCC_STATE_VERSION;
}

/**
 * Turn the current (script or test) session into a DCC game, doing the setup
 * a new DCC game needs before levels can be generated.
 */
void dcc_enable_for_script()
{
    crawl_state.type = GAME_TYPE_DCC;
    // Sets brdepth[BRANCH_DUNGEON] = 12 and, via initialise_brentry(), the
    // DESCENT_WATER_BRANCH_KEY / DESCENT_POIS_BRANCH_KEY props that the
    // builder ASSERTs on when placing branch entrances in Descent.
    initialise_branch_depths();
    dcc_setup_new_game();
}
