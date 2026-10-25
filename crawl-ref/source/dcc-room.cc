/**
 * @file
 * @brief Crawl Live (DCC) safe-room vault.
 *
 * Every connected-branch floor of a DCC game gets exactly one safe-room
 * vault (dat/des/dcc/safe_rooms.des, tag dcc_safe_room): a walled room with
 * one door, a shop, a placeholder loot box kiosk, and all of the floor's up
 * stairs, every cell inside the walls dcc_safe (dcc-safe.cc). Arrivals by
 * stairs or branch entrance land on an up stair or on the branch exit trunk
 * makes from one, so every arrival lands inside. Portal vaults, the Abyss,
 * Pandemonium and Ziggurats get none (Resolved decision 8 treats them the
 * same way for the floor clock).
 *
 * Trunk reaches this file through one-line hooks guarded by game_is_dcc():
 *  - _dgn_random_map_for_place() (dungeon.cc): on D:1 the safe room is the
 *    arrival vault, the level's primary vault;
 *  - _build_dungeon_level() (dungeon.cc): dcc_place_safe_room() after the
 *    primary vault and layout, before branch entrances and random vaults;
 *    dcc_fix_safe_room_stairs() just before the connectivity checks and the
 *    stone stair fixup.
 * Each callee re-checks game_is_dcc() itself.
 *
 * Placement. The room is dug into solid wall wherever one of the variants
 * fits with its door opening onto floor of the level's largest connected
 * area. Overwriting only wall can neither block a path nor destroy anything,
 * and the room's own walls enclose it, so the door is its only opening: no
 * veto risk, and it works on encompass levels (Vaults:$, the Vestibule of
 * Hell, the Hell ends) where nothing can be placed beside the primary vault.
 * Wall outside any vault is preferred, then wall inside vaults, then deep
 * water and lava as well (the Cocytus and Gehenna ends), then open floor of
 * the main area where walling it off provably cuts no route (open halls such
 * as Dis:$). Only if nothing fits does it fall back to the normal
 * secondary-vault placement (which connects the door with a dug corridor),
 * and only if that fails too is the level vetoed. A stress run over every
 * connected floor (6 builds each) needed no such veto, and vetoed no more
 * often overall than plain Descent.
 *
 * Stairs. Once every vault is down, dcc_fix_safe_room_stairs() turns each
 * stone up stair outside the room to floor, so trunk's stair fixup finds
 * exactly the room's three, keeps them, and has nothing to add. On floors
 * that take a single up stair (X:1, Hell) only the room's '{' is kept, so
 * the fixup does not have to choose. Descent turns every up stair to floor
 * once the player acts on a floor anyway, so this only moves the arrival.
**/

#include "AppHdr.h"

#include "dcc-room.h"

#include <queue>

#include "branch.h"
#include "coord.h"
#include "coordit.h"
#include "dlua.h"
#include "dungeon.h"
#include "env.h"
#include "fprop.h"
#include "mapdef.h"
#include "mapmark.h"
#include "maps.h"
#include "player.h"
#include "random.h"
#include "terrain.h"

static const string SAFE_ROOM_TAG = "dcc_safe_room";

/// Does this floor of a DCC game get a safe room? The Tomb is left out: it
/// has no stairs (hatches only), and Descent never generates it anyway.
static bool _wants_safe_room()
{
    return crawl_state.game_is_dcc() && player_in_connected_branch()
           && !player_in_branch(BRANCH_TOMB);
}

/// Does trunk's connectivity analysis see this cell (dungeon.cc
/// _dgn_square_is_passable())? It skips opaque vaults.
static bool _seen_by_connectivity(const coord_def &c)
{
    return map_masked(c, MMT_PASSABLE) || !map_masked(c, MMT_OPAQUE);
}

/// Is a safe-room vault already placed on the level being built?
static bool _level_has_safe_room()
{
    for (const auto &vp : env.level_vaults)
        if (vp->map.has_tag(SAFE_ROOM_TAG))
            return true;
    return false;
}

/**
 * The D:1 arrival vault of a DCC game: a safe room, placed as the level's
 * primary vault in place of a trunk arrival vault.
 *
 * @param minivault  Whether the caller wants a minivault (never, here).
 * @return a safe-room map on D:1, nullptr anywhere else.
 */
const map_def *dcc_arrival_map(bool minivault)
{
    const level_id here = level_id::current();
    if (!crawl_state.game_is_dcc() || minivault
        || here.branch != root_branch || here.depth != 1)
    {
        return nullptr;
    }
    return random_map_for_tag(SAFE_ROOM_TAG, false, false);
}

namespace
{
    // One safe-room variant as the map lines spell it (they are never
    // rotated or mirrored), with its one door.
    struct room_shape
    {
        const map_def *map;
        coord_def size;
        coord_def door;       // in map coordinates
        coord_def outward;    // from the door to the cell outside it
    };
}

static vector<room_shape> _room_shapes()
{
    vector<room_shape> shapes;
    for (const map_def *mdef : find_maps_for_tag(SAFE_ROOM_TAG, false, false))
    {
        // The map lines come from running the map's Lua chunk, as vault
        // placement does (maps.cc _resolve_map_lua()); work on a copy. Like
        // placement, load the shared definition from the des cache first.
        const_cast<map_def &>(*mdef).load();
        map_def m = *mdef;
        dlua.callfn("dgn_flush_map_environment_for", "s", m.name.c_str());
        m.reinit();
        if (!m.run_lua(true).empty())
            continue;

        room_shape shape;
        shape.map = mdef;
        const vector<string> &lines = m.map.get_lines();
        shape.size = m.map.size();

        int doors = 0;
        for (int y = 0; y < shape.size.y; ++y)
            for (int x = 0; x < (int)lines[y].size(); ++x)
                if (lines[y][x] == '+')
                {
                    ++doors;
                    shape.door = coord_def(x, y);
                }

        const coord_def d = shape.door;
        if (doors != 1)
            continue;
        if (d.y == 0)
            shape.outward = coord_def(0, -1);
        else if (d.y == shape.size.y - 1)
            shape.outward = coord_def(0, 1);
        else if (d.x == 0)
            shape.outward = coord_def(-1, 0);
        else if (d.x == shape.size.x - 1)
            shape.outward = coord_def(1, 0);
        else
            continue; // a door inside the map cannot open onto the level

        shapes.push_back(shape);
    }
    return shapes;
}

namespace
{
    // How choosy one dig-in pass is; dcc_place_safe_room() relaxes these in
    // turn.
    struct dig_rules
    {
        bool vault;   // may dig into wall inside other vaults
        bool seen;    // the door must open where trunk's connectivity
                      // analysis looks (see _seen_by_connectivity())
        bool liquid;  // may build over deep water and lava too
        bool open;    // may build over floor of the main area too, where
                      // that cuts nothing off (see _keeps_main_whole())
    };
}

/// Can the room be dug into this cell? Plain opaque wall (no permarock,
/// which vaults use to keep areas sealed), or with rules.liquid deep water or
/// lava, which trunk's connectivity checks never walk through either, or
/// with rules.open plain floor; nothing on it, no marker.
static bool _diggable(const coord_def &c, const dig_rules &rules)
{
    if (c.x < MAPGEN_BORDER || c.x >= GXM - MAPGEN_BORDER
        || c.y < MAPGEN_BORDER || c.y >= GYM - MAPGEN_BORDER)
    {
        return false;
    }
    const dungeon_feature_type feat = env.grid(c);
    const bool solid = feat_is_wall(feat) && feat_is_opaque(feat)
                       && !feat_is_permarock(feat);
    const bool liquid = feat == DNGN_DEEP_WATER || feat == DNGN_LAVA;
    return (solid || rules.liquid && liquid
            || rules.open && feat == DNGN_FLOOR)
           && (rules.vault || !map_masked(c, MMT_VAULT))
           && env.igrid(c) == NON_ITEM
           && !monster_at(c)
           && !env.markers.find(c, MAT_ANY);
}

/// Label the level's travel-connected areas; returns the largest area's
/// label (0 if there is no passable cell) and its size in main_size.
static int _label_zones(FixedArray<int, GXM, GYM> &zone, int &main_size)
{
    zone.init(0);
    int next = 0, best = 0;
    main_size = 0;
    for (rectangle_iterator ri(1); ri; ++ri)
    {
        if (zone(*ri) || !dgn_square_travel_ok(*ri))
            continue;

        ++next;
        int size = 0;
        queue<coord_def> todo;
        todo.push(*ri);
        zone(*ri) = next;
        while (!todo.empty())
        {
            const coord_def c = todo.front();
            todo.pop();
            ++size;
            for (adjacent_iterator ai(c); ai; ++ai)
            {
                if (in_bounds(*ai) && !zone(*ai) && dgn_square_travel_ok(*ai))
                {
                    zone(*ai) = next;
                    todo.push(*ai);
                }
            }
        }
        if (size > main_size)
            best = next, main_size = size;
    }
    return best;
}

/// Could the room's door open onto this cell? Plain floor of the main area,
/// not a no-teleport pocket, and if need_seen, seen by trunk's connectivity
/// analysis.
static bool _good_doorstep(const coord_def &c,
                           const FixedArray<int, GXM, GYM> &zone, int main,
                           bool need_seen)
{
    return in_bounds(c)
           && main
           && zone(c) == main
           && env.grid(c) == DNGN_FLOOR
           && !testbits(env.pgrid(c), FPROP_NO_TELE_INTO)
           && (!need_seen || _seen_by_connectivity(c));
}

namespace
{
    struct room_spot
    {
        const room_shape *shape;
        coord_def corner;

        bool covers(const coord_def &c) const
        {
            const coord_def d = c - corner;
            return d.x >= 0 && d.y >= 0
                   && d.x < shape->size.x && d.y < shape->size.y;
        }
    };
}

/**
 * Would the main area stay in one piece with the room's cells walled off?
 * Walks the main area from the doorstep around the room and checks that it
 * reaches every main-area cell the room does not cover. Only open-ground
 * placements need this: wall, deep water and lava were never walkable.
 */
static bool _keeps_main_whole(const room_spot &spot,
                              const FixedArray<int, GXM, GYM> &zone, int main,
                              int main_size)
{
    int covered = 0;
    for (int dy = 0; dy < spot.shape->size.y; ++dy)
        for (int dx = 0; dx < spot.shape->size.x; ++dx)
            if (zone(spot.corner + coord_def(dx, dy)) == main)
                ++covered;

    const coord_def start = spot.corner + spot.shape->door
                            + spot.shape->outward;
    FixedArray<bool, GXM, GYM> seen;
    seen.init(false);
    queue<coord_def> todo;
    todo.push(start);
    seen(start) = true;
    int reached = 0;
    while (!todo.empty())
    {
        const coord_def c = todo.front();
        todo.pop();
        ++reached;
        for (adjacent_iterator ai(c); ai; ++ai)
        {
            if (in_bounds(*ai) && !seen(*ai) && zone(*ai) == main
                && !spot.covers(*ai))
            {
                seen(*ai) = true;
                todo.push(*ai);
            }
        }
    }
    return reached == main_size - covered;
}

/**
 * Dig a safe room into solid wall (or, as rules allow, liquid or open
 * ground).
 *
 * The room is transparent to trunk's connectivity analysis when its door
 * opens onto a cell that analysis sees, so the room's stairs count for that
 * area, and opaque like its surroundings otherwise (inside an opaque vault,
 * such as an encompass map), so it is not counted as an area of its own.
 *
 * @param rules  What this pass may dig into, and where the door may open.
 * @return whether a room was placed.
 */
static bool _dig_in_safe_room(const vector<room_shape> &shapes,
                              const dig_rules &rules)
{
    FixedArray<int, GXM, GYM> zone;
    int main_size;
    const int main = _label_zones(zone, main_size);
    if (!main)
        return false;

    FixedArray<bool, GXM, GYM> diggable;
    for (rectangle_iterator ri(0); ri; ++ri)
    {
        diggable(*ri) = _diggable(*ri, rules)
                        // Open ground only from the main area.
                        && (!dgn_square_travel_ok(*ri) || zone(*ri) == main);
    }

    vector<room_spot> spots;
    for (const room_shape &shape : shapes)
    {
        for (int y = 0; y + shape.size.y <= GYM; ++y)
            for (int x = 0; x + shape.size.x <= GXM; ++x)
            {
                const coord_def corner(x, y);
                if (!_good_doorstep(corner + shape.door + shape.outward,
                                    zone, main, rules.seen))
                {
                    continue;
                }

                bool fits = true;
                for (int dy = 0; fits && dy < shape.size.y; ++dy)
                    for (int dx = 0; fits && dx < shape.size.x; ++dx)
                        fits = diggable(corner + coord_def(dx, dy));
                if (fits)
                    spots.push_back({ &shape, corner });
            }
    }

    // Take a random spot; on open ground, the first that cuts nothing off.
    // Each check walks the level, so only a bounded number are tried.
    shuffle_array(spots);
    const room_spot *pick = nullptr;
    const int max_checks = 200;
    for (int i = 0; i < (int)spots.size() && i < max_checks && !pick; ++i)
        if (!rules.open || _keeps_main_whole(spots[i], zone, main, main_size))
            pick = &spots[i];
    if (!pick)
        return false;

    // A float vault given a position is centred on it: start = pos - size/2.
    const room_shape &shape = *pick->shape;
    const coord_def centre = pick->corner + shape.size / 2;
    const vault_placement *vp = dgn_place_map(shape.map, false, true, centre);
    if (!vp)
        return false;
    if (vp->pos != pick->corner)
    {
        throw dgn_veto_exception("Crawl Live safe room landed off its spot: "
                                 + shape.map->name);
    }

    const coord_def doorstep = pick->corner + shape.door + shape.outward;
    const bool seen = _seen_by_connectivity(doorstep);
    for (vault_place_iterator vi(*vp); vi; ++vi)
    {
        if (seen)
            env.level_map_mask(*vi) &= ~MMT_OPAQUE;
        else
            env.level_map_mask(*vi) |= MMT_OPAQUE;
    }
    return true;
}

/**
 * Place the floor's safe room, unless it already has one (D:1, where it is
 * the arrival vault). Vetoes the level only if no placement works at all.
 */
void dcc_place_safe_room()
{
    ASSERT(crawl_state.game_is_dcc());
    if (!_wants_safe_room() || _level_has_safe_room())
        return;

    const vector<room_shape> shapes = _room_shapes();
    if (shapes.empty())
        throw dgn_veto_exception("Crawl Live: no usable safe-room vault.");

    // From choosiest to least choosy. Liquid comes after wall: building over
    // deep water or lava is safe for walking routes but may still cut a
    // route only flyers or swimmers had, which trunk's checks would veto.
    // Open ground comes after that, checked to cut no route off.
    static const dig_rules passes[] =
    {
        { false, true,  false, false },  // wall outside vaults
        { true,  true,  false, false },  // any wall, door where trunk looks
        { true,  false, false, false },  // any wall (encompass maps)
        { true,  true,  true,  false },  // wall or liquid (water and lava)
        { true,  false, true,  false },
        { true,  true,  true,  true  },  // open ground (open halls)
        { true,  false, true,  true  },
    };
    for (const dig_rules &rules : passes)
        if (_dig_in_safe_room(shapes, rules))
            return;

    // Nothing fits: place it like any secondary vault, connected by a dug
    // corridor.
    for (int tries = 0; tries < 10; ++tries)
    {
        const map_def *mdef = random_map_for_tag(SAFE_ROOM_TAG, false, false);
        if (mdef && dgn_place_map(mdef, true, false))
            return;
    }
    const string primary = env.level_vaults.empty()
                           ? "no vault" : env.level_vaults[0]->map.name;
    throw dgn_veto_exception("Crawl Live: no room for the safe room (with "
                             + primary + ").");
}

/**
 * Leave the safe room's up stairs as the floor's only way up: every other
 * stone up stair, any branch exit a vault placed directly, and every up
 * hatch turns to floor, and on single-up-stair floors (X:1, Hell; the same
 * rule as trunk's stone stair fixup) only the room's '{' stays. Trunk turns
 * the room's stair into the branch exit later where one is due. Descent
 * floors up hatches at the end of the build anyway, but trunk first turns
 * vault hatches into the branch exit on one-floor branches (the Vestibule
 * of Hell), so they go here. The Zot:$ dungeon exit Descent adds later is
 * the way out with the Orb, not an arrival, and stays where it lands.
 */
void dcc_fix_safe_room_stairs()
{
    if (!_wants_safe_room() || !_level_has_safe_room())
        return;

    const bool single = you.depth == 1 || player_in_hell();
    for (rectangle_iterator ri(1); ri; ++ri)
    {
        const dungeon_feature_type feat = env.grid(*ri);
        const bool stone = feat_is_stone_stair_up(feat);
        // The exits are those Descent crumbles (terrain.cc), with Hell's.
        if (!stone && !feat_is_branch_exit(feat) && feat != DNGN_EXIT_VAULTS
            && feat != DNGN_EXIT_ZOT && feat != DNGN_EXIT_HELL
            && feat != DNGN_ESCAPE_HATCH_UP)
        {
            continue;
        }

        const bool in_room = testbits(env.pgrid(*ri), FPROP_DCC_SAFE);
        const bool keep = in_room && stone
                          && (!single || feat == DNGN_STONE_STAIRS_UP_I);
        if (!keep)
            env.grid(*ri) = DNGN_FLOOR;
    }
}
