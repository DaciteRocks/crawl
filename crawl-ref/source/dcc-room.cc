/**
 * @file
 * @brief Crawl Live (DCC) safe-room vault.
 *
 * Every connected-branch floor of a DCC game gets exactly one safe-room
 * vault (dat/des/dcc/safe_rooms.des, tag dcc_safe_room): a walled room with
 * one door, a shop, a placeholder loot box kiosk, and all of the floor's up
 * stairs, every cell inside the walls dcc_safe (dcc-safe.cc). Arrivals by
 * stairs or branch entrance land on an up stair or on the branch exit trunk
 * makes from one, so every arrival lands inside; a new game starts on the
 * room's '{' (dcc_start_stair()). Portal vaults, the Abyss, Pandemonium and
 * Ziggurats get none (Resolved decision 8 treats them the same way for the
 * floor clock).
 *
 * Trunk reaches this file through one-line hooks guarded by game_is_dcc():
 *  - _dgn_random_map_for_place() (dungeon.cc): on D:1 the safe room is the
 *    arrival vault, the level's primary vault;
 *  - _build_dungeon_level() (dungeon.cc): dcc_place_safe_room() after the
 *    primary vault and layout, before branch entrances and random vaults;
 *    dcc_fix_safe_room_stairs() just before the connectivity checks and the
 *    stone stair fixup;
 *  - load_level() (files.cc): dcc_start_stair().
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
 * the main area (open halls such as Dis:$). Every pass that covers anything
 * but wall is checked with trunk's own zone count (the one its connectivity
 * veto uses) to cut nothing off. Only if nothing fits does it fall back to
 * the normal secondary-vault placement (which connects the door with a dug
 * corridor), and only if that fails too is the level vetoed.
 *
 * Stairs. Once every vault is down, dcc_fix_safe_room_stairs() removes each
 * stone up stair outside the room, so trunk's stair fixup finds exactly the
 * room's three, keeps them, and has nothing to add. On floors that take a
 * single up stair (X:1, Hell) only the room's '{' is kept, so the fixup does
 * not have to choose. Descent turns every up stair to floor once the player
 * acts on a floor anyway, so this only moves the arrival.
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
#include "flood-find.h"
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

/**
 * The stair a new game "arrives by" (load_level(), files.cc). startup.cc
 * starts a Delver on D:2 as if they had climbed a stair (DNGN_STONE_STAIRS_
 * UP_I), which lands them on a down stair, outside the safe room. In Crawl
 * Live every game starts as if it came down a stair, on the room's '{'.
 * Other loads, including a Descent stair peek stepping back up, are left
 * alone.
 */
dungeon_feature_type dcc_start_stair(dungeon_feature_type stair_taken,
                                     load_mode_type load_mode)
{
    if (crawl_state.game_is_dcc() && load_mode == LOAD_START_GAME
        && feat_is_stone_stair_up(stair_taken))
    {
        return DNGN_STONE_STAIRS_DOWN_I;
    }
    return stair_taken;
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

    // _room_shapes()'s cache: the names of every map tagged dcc_safe_room
    // when it was filled, and the usable shapes among them.
    vector<string> shape_names;
    vector<room_shape> shape_cache;
    bool shapes_cached = false;
}

/// Read one variant's geometry from its map lines; false if unusable.
static bool _read_room_shape(const map_def *mdef, room_shape &shape)
{
    // The map lines come from running the map's Lua chunk, as vault
    // placement does (maps.cc _resolve_map_lua()); work on a copy. Like
    // placement, load the shared definition from the des cache first.
    const_cast<map_def &>(*mdef).load();
    map_def m = *mdef;
    dlua.callfn("dgn_flush_map_environment_for", "s", m.name.c_str());
    m.reinit();
    if (!m.run_lua(true).empty())
        return false;

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
        return false;
    if (d.y == 0)
        shape.outward = coord_def(0, -1);
    else if (d.y == shape.size.y - 1)
        shape.outward = coord_def(0, 1);
    else if (d.x == 0)
        shape.outward = coord_def(-1, 0);
    else if (d.x == shape.size.x - 1)
        shape.outward = coord_def(1, 0);
    else
        return false; // a door inside the map cannot open onto the level
    return true;
}

/**
 * The usable safe-room variants. Reading them runs each map's Lua, so the
 * result is cached for the process. The cache holds while the same maps
 * (by name, in order) carry the tag; their map_def pointers are refreshed
 * on every call, because reread_maps() (a des file changed under a running
 * game) rebuilds every definition. A reread that changes a variant's lines
 * under the same name is caught when the placed vault does not match its
 * cached shape (_dig_in_safe_room()), which empties the cache.
 */
static vector<room_shape> _room_shapes()
{
    const mapref_vector maps = find_maps_for_tag(SAFE_ROOM_TAG, false, false);
    vector<string> names;
    for (const map_def *mdef : maps)
        names.push_back(mdef->name);

    if (!shapes_cached || names != shape_names)
    {
        shape_cache.clear();
        for (const map_def *mdef : maps)
        {
            room_shape shape;
            if (_read_room_shape(mdef, shape))
                shape_cache.push_back(shape);
        }
        shape_names = names;
        shapes_cached = true;
        return shape_cache;
    }

    vector<room_shape> shapes = shape_cache;
    for (room_shape &shape : shapes)
        for (const map_def *mdef : maps)
            if (mdef->name == shape.map->name)
                shape.map = mdef;
    return shapes;
}

/// Forget the cached shapes; the next _room_shapes() call rereads them.
static void _forget_room_shapes()
{
    shapes_cached = false;
    shape_names.clear();
    shape_cache.clear();
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
        bool open;    // may build over floor of the main area too
    };

    // What the room could be built over at a cell, as far as any pass goes.
    enum class ground
    {
        none,    // never
        solid,   // plain opaque wall
        liquid,  // deep water or lava
        open,    // plain floor of the main area
    };

    // The level as every pass sees it: nothing changes between passes (a
    // pass places the room only once it has a spot), so it is surveyed once.
    struct level_survey
    {
        FixedArray<int, GXM, GYM> zone;     // travel-connected areas
        int main = 0;                       // the largest one, 0 if none
        FixedArray<ground, GXM, GYM> ground_at;
        int trunk_zones = 0;                // dgn_count_disconnected_zones()
    };

    struct room_spot
    {
        const room_shape *shape;
        coord_def corner;
    };
}

/**
 * Label the level's travel-connected areas, as the player could walk them
 * (dgn_square_travel_ok(), through opaque vault interiors too, which trunk's
 * zone helpers never enter; encompass levels are nothing but such
 * interiors). Returns the largest area's label, 0 if no cell is passable.
 */
static int _label_zones(FixedArray<int, GXM, GYM> &zone)
{
    zone.init(0);
    int next = 0, best = 0, best_size = 0;
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
        if (size > best_size)
            best = next, best_size = size;
    }
    return best;
}

/// What could the room be built over here? Nothing near the map edge, and
/// nothing with an item, a monster or a marker on it. Wall must be opaque
/// and not permarock (which vaults use to keep areas sealed); floor must
/// belong to the main area.
static ground _ground_at(const coord_def &c,
                         const FixedArray<int, GXM, GYM> &zone, int main,
                         const FixedArray<bool, GXM, GYM> &item_at)
{
    if (c.x < MAPGEN_BORDER || c.x >= GXM - MAPGEN_BORDER
        || c.y < MAPGEN_BORDER || c.y >= GYM - MAPGEN_BORDER
        || item_at(c)
        || monster_at(c)
        || env.markers.find(c, MAT_ANY))
    {
        return ground::none;
    }

    const dungeon_feature_type feat = env.grid(c);
    if (feat_is_wall(feat) && feat_is_opaque(feat) && !feat_is_permarock(feat))
        return ground::solid;
    if (feat == DNGN_DEEP_WATER || feat == DNGN_LAVA)
        return ground::liquid;
    if (feat == DNGN_FLOOR && main && zone(c) == main)
        return ground::open;
    return ground::none;
}

static void _survey_level(level_survey &survey)
{
    survey.main = _label_zones(survey.zone);

    // Vault items are not linked into env.igrid until the build is done
    // (link_items()), so find them in the item list.
    FixedArray<bool, GXM, GYM> item_at(false);
    for (const item_def &item : env.item)
        if (item.defined() && !item.held_by_monster() && in_bounds(item.pos))
            item_at(item.pos) = true;

    for (rectangle_iterator ri(0); ri; ++ri)
    {
        survey.ground_at(*ri) = _ground_at(*ri, survey.zone, survey.main,
                                           item_at);
    }
    survey.trunk_zones = dgn_count_disconnected_zones(false);
}

/// May this pass dig the room into this cell?
static bool _diggable(const coord_def &c, const dig_rules &rules,
                      const level_survey &survey)
{
    const ground g = survey.ground_at(c);
    return (g == ground::solid
            || rules.liquid && g == ground::liquid
            || rules.open && g == ground::open)
           && (rules.vault || !map_masked(c, MMT_VAULT));
}

/// Could the room's door open onto this cell? Plain floor of the main area,
/// not a no-teleport pocket, and if need_seen, seen by trunk's connectivity
/// analysis.
static bool _good_doorstep(const coord_def &c, const level_survey &survey,
                           bool need_seen)
{
    return in_bounds(c)
           && survey.main
           && survey.zone(c) == survey.main
           && env.grid(c) == DNGN_FLOOR
           && !testbits(env.pgrid(c), FPROP_NO_TELE_INTO)
           && (!need_seen || _seen_by_connectivity(c));
}

/**
 * Would trunk's connectivity veto (_dgn_verify_connectivity(), and Descent's
 * one-zone rule in _add_connecting_escape_hatches()) still pass with the
 * room's cells walled off? Counts zones with trunk's own helper, which uses
 * the same passability rule as the veto, with the spot temporarily turned
 * to rock; the room's interior later joins its doorstep's zone through the
 * door, so it adds none.
 */
static bool _keeps_trunk_zones(const room_spot &spot, int zones_before)
{
    vector<pair<coord_def, dungeon_feature_type>> saved;
    for (int dy = 0; dy < spot.shape->size.y; ++dy)
        for (int dx = 0; dx < spot.shape->size.x; ++dx)
        {
            const coord_def c = spot.corner + coord_def(dx, dy);
            saved.emplace_back(c, env.grid(c));
            env.grid(c) = DNGN_ROCK_WALL;
        }
    const int zones = dgn_count_disconnected_zones(false);
    for (const auto &cell : saved)
        env.grid(cell.first) = cell.second;
    return zones <= zones_before;
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
                              const dig_rules &rules,
                              const level_survey &survey)
{
    if (!survey.main)
        return false;

    vector<room_spot> spots;
    for (const room_shape &shape : shapes)
    {
        for (int y = 0; y + shape.size.y <= GYM; ++y)
            for (int x = 0; x + shape.size.x <= GXM; ++x)
            {
                const coord_def corner(x, y);
                if (!_good_doorstep(corner + shape.door + shape.outward,
                                    survey, rules.seen))
                {
                    continue;
                }

                bool fits = true;
                for (int dy = 0; fits && dy < shape.size.y; ++dy)
                    for (int dx = 0; fits && dx < shape.size.x; ++dx)
                        fits = _diggable(corner + coord_def(dx, dy), rules,
                                         survey);
                if (fits)
                    spots.push_back({ &shape, corner });
            }
    }

    // Take a random spot. Over anything but wall, take the first that keeps
    // trunk's zone count; each check walks the level, so only a bounded
    // number are tried. (Over liquid alone the count cannot change, since
    // liquid is not passable to it; and trunk's flight-aware "ever
    // passable" check only seeds at passable cells, which Descent's
    // one-zone rule keeps in a single area with the room. The check stays
    // on for every non-wall pass anyway: it is cheap, and these passes are
    // rare.)
    const bool check = rules.liquid || rules.open;
    shuffle_array(spots);
    const room_spot *pick = nullptr;
    const int max_checks = 200;
    for (int i = 0; i < (int)spots.size() && i < max_checks && !pick; ++i)
        if (!check || _keeps_trunk_zones(spots[i], survey.trunk_zones))
            pick = &spots[i];
    if (!pick)
        return false;

    // A float vault given a position is centred on it: start = pos - size/2.
    const room_shape &shape = *pick->shape;
    const coord_def centre = pick->corner + shape.size / 2;
    const vault_placement *vp = dgn_place_map(shape.map, false, true, centre);
    if (!vp)
        return false;

    // The vault is written by now, so a mismatch cannot be undone by trying
    // another spot: a second room would follow. It only happens if the map
    // changed under the cached shape (a des reread); reread the shapes and
    // rebuild the level.
    if (vp->pos != pick->corner || vp->size != shape.size
        || !feat_is_door(env.grid(pick->corner + shape.door)))
    {
        _forget_room_shapes();
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

    level_survey survey;
    _survey_level(survey);

    // From choosiest to least choosy: wall first (it takes no ground from
    // anyone), then deep water and lava, then walkable open ground.
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
        if (_dig_in_safe_room(shapes, rules, survey))
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

/// Is this feature a way on from a floor: down, into a branch, or out of
/// the dungeon the Descent way?
static bool _is_way_on(dungeon_feature_type feat)
{
    return feat_is_stone_stair_down(feat)
           || feat == DNGN_ESCAPE_HATCH_DOWN
           || feat_is_branch_entrance(feat)
           || feat_is_descent_exitable(feat);
}

/**
 * Can the player walk from this cell to the safe room or to some other way
 * on? Trunk's own flood (flood_find, as _has_connected_downstairs_from()
 * uses), walking where travel does, vaults included.
 */
static bool _reaches_room_or_way_on(const coord_def &from,
                                    const coord_def &room_cell)
{
    flood_find<feature_grid, coord_predicate> to_room(env.grid, in_bounds);
    to_room.add_point(room_cell);
    if (to_room.points_connected_from(from))
        return true;

    flood_find<feature_grid, coord_predicate> to_exit(env.grid, in_bounds);
    for (int f = 0; f < NUM_FEATURES; ++f)
        if (_is_way_on(static_cast<dungeon_feature_type>(f)))
            to_exit.add_feat(f);
    return to_exit.find_first_from(from, env.level_map_mask).x != 0;
}

/**
 * Leave the safe room's up stairs as the floor's only way up: every other
 * stone up stair, any branch exit a vault placed directly, and every up
 * hatch goes, and on single-up-stair floors (X:1, Hell; the same rule as
 * trunk's stone stair fixup) only the room's '{' stays. Trunk turns the
 * room's stair into the branch exit later where one is due. Descent floors
 * up hatches at the end of the build anyway, but trunk first turns vault
 * hatches into the branch exit on one-floor branches (the Vestibule of
 * Hell), so they go here. The Zot:$ dungeon exit Descent adds later is the
 * way out with the Orb, not an arrival, and stays where it lands.
 *
 * A removed stair becomes floor, unless it was the only way out of an area
 * that cannot reach the room or any other way on (a closed chamber in a
 * vault, which trunk's checks never look into): then it becomes a down
 * hatch, the way trunk's _fixup_branch_stairs() replaces a vault stair it
 * cannot keep, so the area keeps an exit. At a branch end nothing leads
 * down, and it becomes floor, as Descent's crumbling would leave it.
 */
void dcc_fix_safe_room_stairs()
{
    if (!_wants_safe_room() || !_level_has_safe_room())
        return;

    coord_def room_cell;
    for (rectangle_iterator ri(1); ri; ++ri)
        if (testbits(env.pgrid(*ri), FPROP_DCC_SAFE))
        {
            room_cell = *ri;
            break;
        }

    const bool single = you.depth == 1 || player_in_hell();
    const bool bottom = at_branch_bottom();
    for (rectangle_iterator ri(1); ri; ++ri)
    {
        const dungeon_feature_type feat = env.grid(*ri);
        const bool stone = feat_is_stone_stair_up(feat);
        const bool hatch = feat == DNGN_ESCAPE_HATCH_UP;
        // The exits are those Descent crumbles (terrain.cc), with Hell's.
        if (!stone && !hatch && !feat_is_branch_exit(feat)
            && feat != DNGN_EXIT_VAULTS && feat != DNGN_EXIT_ZOT
            && feat != DNGN_EXIT_HELL)
        {
            continue;
        }

        const bool in_room = testbits(env.pgrid(*ri), FPROP_DCC_SAFE);
        if (in_room && stone && (!single || feat == DNGN_STONE_STAIRS_UP_I))
            continue;

        const bool sealed = !in_room && !hatch && !bottom
                            && !room_cell.origin()
                            && !_reaches_room_or_way_on(*ri, room_cell);
        env.grid(*ri) = sealed ? DNGN_ESCAPE_HATCH_DOWN : DNGN_FLOOR;
    }
}
