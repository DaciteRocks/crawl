/**
 * @file
 * @brief Crawl Live (DCC) safe cells.
 *
 * A cell with FPROP_DCC_SAFE (des KPROP "dcc_safe") is closed to monsters
 * that would attack the player. Allies and good neutrals come and go freely.
 * A hostile monster that is already standing on a safe cell (an ally that
 * turned, say) is not trapped: like Zin's sanctuary, it may move from one
 * safe cell to another and may always leave. Only the target cell is ever
 * refused.
 *
 * Trunk reaches this file through one-line hooks guarded by game_is_dcc():
 *  - mon_can_move_to_pos() (mon-act.cc): walking, and melee from outside;
 *  - monster_habitable_grid(const monster*, ...) (mon-place.cc): the check
 *    behind monster::is_habitable(), so blink, teleport, knockback, pushes
 *    and pulls, constriction drags, monster swaps, follower and lost-monster
 *    placement, wander targets and monster pathfinding all refuse safe cells;
 *  - _valid_monster_generation_location() (mon-place.cc): every new monster,
 *    random spawns, vault monsters, band members and summons;
 *  - find_habitable_spot_near() (mon-place.cc): unfriendly summons look for
 *    a spot outside, instead of picking a safe cell and then failing;
 *  - monster::move_to() (monster.cc): translocations whose destination was
 *    picked by monster class rather than by this monster, a backstop;
 *  - _try_to_place_lurker() (mon-lurk.cc): ambushers do not pop up inside.
 * Each callee re-checks game_is_dcc() itself.
**/

#include "AppHdr.h"

#include "dcc-safe.h"

#include "beh-type.h"
#include "coord.h"
#include "env.h"
#include "mgen-data.h"
#include "monster.h"

// Is the monster really standing on a safe cell of the current level? A
// monster that is not on the level yet (being placed, or arriving from
// another level with its old coordinates) is not.
static bool _on_safe_cell(const monster &mon)
{
    const coord_def p = mon.pos();
    return in_bounds(p)
           && env.mgrid(p) == mon.mindex()
           && is_dcc_safe_cell(p);
}

/**
 * May this monster not end up on pos? True for a monster that would attack
 * the player, when pos is a safe cell and the monster is not already on one.
 */
bool dcc_monster_barred_from(const monster &mon, const coord_def &pos)
{
    if (!crawl_state.game_is_dcc() || !is_dcc_safe_cell(pos))
        return false;
    if (mon.wont_attack())
        return false;
    return !_on_safe_cell(mon);
}

// Will the monster mg describes attack the player? Mirrors the attitude
// place_monster() gives it; BEH_COPY copies the summoner.
static bool _generated_hostile(const mgen_data &mg)
{
    switch (mg.behaviour)
    {
    case BEH_FRIENDLY:
    case BEH_GOOD_NEUTRAL:
        return false;
    case BEH_COPY:
        return !mg.summoner || !mg.summoner->wont_attack();
    default:
        return true;
    }
}

/// May a new monster from mg not be placed on pos?
bool dcc_generation_barred_at(const mgen_data &mg, const coord_def &pos)
{
    return crawl_state.game_is_dcc()
           && is_dcc_safe_cell(pos)
           && _generated_hostile(mg);
}

/**
 * Backstop for monster::move_to(): refuse a translocation that would put a
 * hostile monster on a safe cell. Internal moves, moves that skip the mgrid
 * update, and deliberate overlaps (swaps) are never refused: their callers
 * rely on the move happening and check habitability themselves.
 */
bool dcc_translocation_barred(const monster &mon, const coord_def &pos,
                              movement_type mvflags)
{
    if (!(mvflags & MV_TRANSLOCATION)
        || (mvflags & (MV_INTERNAL | MV_NO_MGRID_UPDATE | MV_ALLOW_OVERLAP)))
    {
        return false;
    }
    return dcc_monster_barred_from(mon, pos);
}
