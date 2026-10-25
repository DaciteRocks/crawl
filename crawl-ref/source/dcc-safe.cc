/**
 * @file
 * @brief Crawl Live (DCC) safe cells.
 *
 * A cell with FPROP_DCC_SAFE (des KPROP "dcc_safe") is closed to monsters
 * that would attack the player. Allies and good neutrals come and go freely.
 * A hostile monster that is already standing on a safe cell (an ally that
 * turned, say) is not trapped, but it may only head for the way out: it may
 * step off the safe cells, or onto a safe cell of the same room that is
 * strictly closer to the way out. It never lands on any other safe cell.
 *
 * No combat in a safe room (user decision, 2026-10-25): nothing standing on
 * a safe cell can harm anything, and nothing standing on a safe cell can be
 * harmed, like Zin's sanctuary. The player is refused melee, firing and
 * throwing, harmful spells and harmful evocations while inside; monsters
 * neither target nor melee across the edge, and allies inside stand down.
 *
 * Trunk reaches this file through one-line hooks guarded by game_is_dcc():
 *  - mon_can_move_to_pos() (mon-act.cc): walking;
 *  - handle_monster_move() (mon-act.cc): a hostile inside heads for the way
 *    out, and no monster melees across or inside a safe room;
 *  - _monster_move() (mon-act.cc): flopping aquatic monsters;
 *  - monster_habitable_grid(const monster*, ...) (mon-place.cc): the check
 *    behind monster::is_habitable(), so blink, teleport, knockback, pushes
 *    and pulls, constriction drags, monster swaps, follower and lost-monster
 *    placement, wander targets and monster pathfinding all refuse safe cells;
 *  - _valid_monster_generation_location() (mon-place.cc): every new monster,
 *    random spawns, vault monsters, band members and summons;
 *  - find_habitable_spot_near() (mon-place.cc): searches made for a hostile
 *    (its no_sanctuary flag: unfriendly summons, lurkers) skip safe cells;
 *  - player.cc and spl-transloc.cc: monsters dragged along with the player
 *    get a spot outside instead of a refused one;
 *  - monster::move_to() (monster.cc): translocations whose destination was
 *    picked by monster class rather than by this monster, a backstop;
 *  - monster::find_home_near_place() (monster.cc): arriving followers queue
 *    outside the door instead of being scattered;
 *  - could_harm() (mon-util.cc): the harm backstop for melee, beams,
 *    explosions and clouds;
 *  - _is_valid_foe() (mon-behv.cc): monster targeting;
 *  - player_fight() (fight.cc), fire_warn_if_impossible() (throw.cc),
 *    spell_uselessness_reason() (spl-util.cc), cannot_evoke_item_reason()
 *    (evoke.cc): the player's refusals.
 * Each callee re-checks game_is_dcc() itself.
**/

#include "AppHdr.h"

#include "dcc-safe.h"

#include <map>

#include "beh-type.h"
#include "coord.h"
#include "coordit.h"
#include "env.h"
#include "item-def.h"
#include "item-prop-enum.h"
#include "message.h"
#include "mgen-data.h"
#include "mon-act.h"
#include "mon-place.h"
#include "mon-util.h"
#include "monster.h"
#include "player.h"
#include "spl-book.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "stringutil.h"
#include "terrain.h"

bool is_dcc_safe_cell(const coord_def& p)
{
    return crawl_state.game_is_dcc()
           && map_bounds(p)
           && testbits(env.pgrid(p), FPROP_DCC_SAFE);
}

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

// Could something walk through this cell? Doors count, open or closed.
static bool _passable(const coord_def &p)
{
    return in_bounds(p) && (!cell_is_solid(p) || feat_is_door(env.grid(p)));
}

/**
 * Steps from each cell of the safe room around start to the way out: the
 * room is the safe cells connected to start, and a safe cell next to a
 * passable unsafe cell is 1 step out. A room with no way out has no
 * entries. Rooms are small, so this is computed on demand.
 */
static map<coord_def, int> _steps_out(const coord_def &start)
{
    const int max_cells = 2000;
    map<coord_def, int> steps;
    vector<coord_def> room;
    map<coord_def, bool> in_room;

    queue<coord_def> todo;
    todo.push(start);
    in_room[start] = true;
    while (!todo.empty() && (int)room.size() < max_cells)
    {
        const coord_def c = todo.front();
        todo.pop();
        room.push_back(c);
        for (adjacent_iterator ai(c); ai; ++ai)
        {
            if (!in_room.count(*ai) && is_dcc_safe_cell(*ai) && _passable(*ai))
            {
                in_room[*ai] = true;
                todo.push(*ai);
            }
        }
    }

    for (const coord_def &c : room)
        for (adjacent_iterator ai(c); ai; ++ai)
            if (!is_dcc_safe_cell(*ai) && _passable(*ai))
            {
                steps[c] = 1;
                todo.push(c);
                break;
            }

    while (!todo.empty())
    {
        const coord_def c = todo.front();
        todo.pop();
        for (adjacent_iterator ai(c); ai; ++ai)
            if (in_room.count(*ai) && !steps.count(*ai))
            {
                steps[*ai] = steps[c] + 1;
                todo.push(*ai);
            }
    }
    return steps;
}

/**
 * May this monster not end up on pos? True for a monster that would attack
 * the player, when pos is a safe cell, unless the monster already stands on
 * a safe cell and pos is a cell of the same room strictly closer to the way
 * out (or its own cell).
 */
bool dcc_monster_barred_from(const monster &mon, const coord_def &pos)
{
    if (!is_dcc_safe_cell(pos) || mon.wont_attack())
        return false;
    if (!_on_safe_cell(mon))
        return true;
    if (pos == mon.pos())
        return false;

    const map<coord_def, int> steps = _steps_out(mon.pos());
    const auto here = steps.find(mon.pos());
    const auto there = steps.find(pos);
    return here == steps.end() || there == steps.end()
           || there->second >= here->second;
}

// Will the monster mg describes attack the player? Mirrors the attitude
// place_monster() gives it. BEH_CHARMED gets no ENCH_CHARM there (its base
// attitude stays hostile), so the new monster's wont_attack() is false and
// counting it hostile here agrees with dcc_monster_barred_from(). BEH_COPY
// copies the summoner.
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
    return is_dcc_safe_cell(pos) && _generated_hostile(mg);
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

// Would the monster attack act by moving into it?
static bool _would_fight(const monster &mon, const actor &act)
{
    return !mons_aligned(&mon, &act)
           || mon.confused()
           || mon.has_ench(ENCH_FRENZIED);
}

// Is a melee swing from mon at whoever stands on pos barred?
static bool _melee_barred(const monster &mon, const coord_def &pos)
{
    const actor *act = actor_at(pos);
    return act && act != &mon
           && (is_dcc_safe_cell(mon.pos()) || is_dcc_safe_cell(pos))
           && _would_fight(mon, *act);
}

/// May a flopping aquatic monster not flop onto pos (move or attack)?
bool dcc_flop_barred(const monster &mon, const coord_def &pos)
{
    return crawl_state.game_is_dcc()
           && (dcc_monster_barred_from(mon, pos) || _melee_barred(mon, pos));
}

/**
 * Adjust a monster's chosen step, mmov, before handle_monster_move() acts
 * on it. A hostile on a safe cell drops its foe and steps towards the way
 * out (like a monster fleeing a sanctuary); and no monster melees into, out
 * of or inside a safe room, so such a step is cancelled.
 */
void dcc_adjust_monster_step(monster &mon, coord_def &mmov)
{
    if (!crawl_state.game_is_dcc())
        return;

    if (!mon.wont_attack() && _on_safe_cell(mon) && !mon.confused()
        && !mon.caught() && !mon.is_stationary())
    {
        const map<coord_def, int> steps = _steps_out(mon.pos());
        const auto here = steps.find(mon.pos());
        if (here != steps.end())
        {
            int best = here->second;
            int count = 0;
            coord_def step;
            for (adjacent_iterator ai(mon.pos()); ai; ++ai)
            {
                if (actor_at(*ai) || !mon_can_move_to_pos(&mon, *ai - mon.pos()))
                    continue;
                const auto there = steps.find(*ai);
                const int d = is_dcc_safe_cell(*ai)
                              ? (there == steps.end() ? INT_MAX : there->second)
                              : 0;
                if (d < best)
                {
                    best = d;
                    count = 1;
                    step = *ai;
                }
                else if (d == best && count && one_chance_in(++count))
                    step = *ai;
            }
            mon.foe = MHITNOT;
            mon.firing_pos.reset();
            if (count)
            {
                mmov = step - mon.pos();
                mon.target = step;
                if (mon.behaviour != BEH_SLEEP)
                    mon.behaviour = BEH_WANDER;
            }
        }
    }

    if (!mmov.origin() && _melee_barred(mon, mon.pos() + mmov))
        mmov.reset();
}

/**
 * For callers that move an existing monster to a spot picked by monster
 * class: if mon may not land on spot, look again around where for a spot
 * it may land on. False if there is none (spot is then unusable).
 */
bool dcc_respot_near(const monster &mon, const coord_def &where, int radius,
                     coord_def &spot, int exclude_radius,
                     const actor *in_sight_of)
{
    if (!crawl_state.game_is_dcc() || !dcc_monster_barred_from(mon, spot))
        return true;
    // no_sanctuary also makes the search skip safe cells.
    return find_habitable_spot_near(where, mon.type, radius, spot,
                                    exclude_radius, in_sight_of, true);
}

/**
 * For find_home_near_place()'s search: a safe cell the monster may not
 * land on, but could walk on, is still searched through (queued here), so
 * the spot chosen lies outside the room. True if pos was queued.
 */
bool dcc_home_search_through(const monster &mon, const coord_def &pos,
                             queue<coord_def> &todo)
{
    if (!crawl_state.game_is_dcc() || !in_bounds(pos)
        || !dcc_monster_barred_from(mon, pos)
        || !monster_habitable_feat(&mon, env.grid(pos)))
    {
        return false;
    }
    todo.push(pos);
    return true;
}

// ---------------------------------------------------------------------
// No combat in a safe room.
// ---------------------------------------------------------------------

static const char *DCC_NO_COMBAT_LINE =
    "\"No fighting in the safe room,\" the Announcer reminds you. "
    "\"House rules.\"";

static bool _actor_on_safe_cell(const actor *act)
{
    return act && is_dcc_safe_cell(act->pos());
}

/**
 * The could_harm() backstop: nothing on a safe cell can harm anything, and
 * nothing on a safe cell can be harmed (agentless harm included), mirroring
 * the sanctuary clauses there.
 */
bool dcc_harm_barred(const actor *agent, const actor *target, bool announce)
{
    if (!crawl_state.game_is_dcc() || !target)
        return false;

    if (_actor_on_safe_cell(agent))
    {
        if (announce && agent->is_player())
            mprf(MSGCH_DCC, "%s", DCC_NO_COMBAT_LINE);
        return true;
    }

    if (_actor_on_safe_cell(target))
    {
        if (announce && you.see_cell(target->pos()))
        {
            mprf(MSGCH_DCC, "The safe room protects %s from harm.",
                 target->name(DESC_THE).c_str());
        }
        return true;
    }
    return false;
}

/**
 * Monster targeting (_is_valid_foe()): a monster on a safe cell has no
 * valid foe, and a foe on a safe cell is not a valid one. A friendly's
 * MHITYOU (follow the player) is left alone.
 */
bool dcc_foe_barred(const monster &mon, int foe)
{
    if (!crawl_state.game_is_dcc() || foe == MHITNOT)
        return false;
    if (foe == MHITYOU && mon.wont_attack())
        return false;
    if (_on_safe_cell(mon))
        return true;

    const actor *target = foe == MHITYOU ? static_cast<const actor *>(&you)
                        : foe >= 0 && foe < MAX_MONSTERS ? &env.mons[foe]
                        : nullptr;
    return target && target->alive() && _actor_on_safe_cell(target);
}

/// Refuse a player attack (melee, firing, throwing) from a safe cell.
bool dcc_player_attack_refused(bool quiet)
{
    if (!crawl_state.game_is_dcc() || !is_dcc_safe_cell(you.pos()))
        return false;
    if (!quiet)
        mprf(MSGCH_DCC, "%s", DCC_NO_COMBAT_LINE);
    return true;
}

// Spells that count as combat: anything aimed at a target that is not
// helpful, conjurations, destructive and damage-only spells, clouds, hexes
// and explosions. Spells that only move or summon allies are allowed;
// anything this misses is still stopped by the could_harm() backstop.
static bool _harmful_spell(spell_type spell)
{
    const spell_flags flags = get_spell_flags(spell);
    if (flags & spflag::helpful)
        return false;
    return spell_harms_target(spell)
           || spell_is_direct_explosion(spell)
           || (get_spell_disciplines(spell) & spschool::conjuration)
           || (flags & (spflag::destructive | spflag::direct_damage_only
                        | spflag::cloud | spflag::WL_check));
}

/// Refuse a harmful spell (cast, or from a wand or ability) from a safe cell.
bool dcc_spell_refused(spell_type spell, bool temp)
{
    return temp && crawl_state.game_is_dcc()
           && is_dcc_safe_cell(you.pos())
           && _harmful_spell(spell);
}

/// Refuse a harmful evocation from a safe cell.
bool dcc_evocation_refused(const item_def &item, bool temp)
{
    if (!temp || !crawl_state.game_is_dcc() || !is_dcc_safe_cell(you.pos()))
        return false;

    if (item.base_type == OBJ_WANDS)
        return _harmful_spell(spell_in_wand(static_cast<wand_type>(item.sub_type)));

    if (item.base_type != OBJ_MISCELLANY)
        return false;

    switch (item.sub_type)
    {
    case MISC_LIGHTNING_ROD:
    case MISC_PHIAL_OF_FLOODS:
    case MISC_TIN_OF_TREMORSTONES:
    case MISC_CONDENSER_VANE:
    case MISC_GRAVITAMBOURINE:
        return true;
    default:
        return false;
    }
}

/// Why a spell or evocation is refused: a clause, or a sentence.
string dcc_no_combat_reason(bool capitalise)
{
    const string reason = "the Announcer allows no combat in a safe room.";
    return capitalise ? uppercase_first(reason) : reason;
}
