/**
 * @file
 * @brief Crawl Live (DCC) floor themes.
 *
 * Every connected-branch floor after D:1 gets one theme row from the table
 * below, drawn with the level builder's RNG when the floor is built
 * (dcc_assign_theme(), called first thing in dcc_place_safe_room()) and
 * kept in you.props[DCC_THEMES_KEY]. A floor that already has a theme (a
 * vetoed and rebuilt level, a script regeneration, or a theme a script set)
 * keeps it. Two floors assigned one after the other never share a theme.
 * On the player's first arrival the Announcer names it
 * (dcc_announce_theme(), from dcc_enter_floor()), and the time/status
 * command's floor clock line names it too.
 *
 * A theme bites through existing trunk levers only: every build of the floor
 * sets its random-monster list (set_vault_mon_list()) and its floor and rock
 * colours (dcc_apply_theme_build()); the first arrival places its extra
 * monsters (dcc_apply_theme_arrival()); dcc_floor_turns_for() scales the
 * floor clock by its percent; dcc_quests.lua moves the quest band's box tier
 * up by its quest_tier_bump (dcc.theme_quest_bump()).
**/

#include "AppHdr.h"

#include "dcc-theme.h"

#include "branch.h"
#include "dcc.h" // dcc_announce
#include "dungeon.h" // MMT_NO_MONS
#include "env.h"
#include "level-id.h"
#include "mapdef.h" // mons_list, mons_spec
#include "message.h"
#include "mgen-data.h"
#include "mon-pick.h" // branch_has_monsters
#include "mon-place.h" // set_vault_mon_list, mons_place, pick_random_monster
#include "mon-util.h"
#include "notes.h"
#include "player.h"
#include "random.h"
#include "state.h"
#include "stringutil.h" // replace_all

static const vector<dcc_theme> theme_table =
{
    { "crowd", "Crowd Work",
      "The studio packed the floor. Try to stay charismatic.",
      "place:{here} band w:30 / place:{here} w:10",
      LIGHTGREY, BROWN, 100, 0, 8 },
    { "headliners", "Headliners",
      "Tonight's guests came up from four floors down. They were promised "
      "a bigger room.",
      "place:{deeper}",
      BLACK, BLACK, 120, 1, 0 },
    { "reruns", "Reruns",
      "Nothing on this floor is new. Most of it is not even alive.",
      "place:{here} zombie w:5 / place:{here} draugr w:3 / "
      "place:{here} spectre w:1",
      DARKGREY, LIGHTGREY, 100, 0, 0 },
    { "lightning", "Lightning Round",
      "Half the time, twice the prize money. The sponsors insisted.",
      "",
      YELLOW, RED, 60, 1, 0 },
};

const vector<dcc_theme> &dcc_theme_table()
{
    return theme_table;
}

const dcc_theme *dcc_theme_by_id(const string &id)
{
    for (const dcc_theme &theme : theme_table)
        if (id == theme.id)
            return &theme;
    return nullptr;
}

/// The theme of a floor, or nullptr if it has none (D:1, a floor off the
/// connected branches, a floor not built yet, a game other than DCC).
const dcc_theme *dcc_theme_for(const level_id &place)
{
    if (!crawl_state.game_is_dcc() || !is_connected_branch(place)
        || !you.props.exists(DCC_THEMES_KEY))
    {
        return nullptr;
    }
    const CrawlHashTable &themes = you.props[DCC_THEMES_KEY].get_table();
    const string key = place.describe();
    if (!themes.exists(key))
        return nullptr;
    return dcc_theme_by_id(themes[key].get_string());
}

// A derived-type spec ("place:D:5 zombie") becomes up to this many concrete
// "<base> zombie" entries, each base rolled from the spec's place, in this
// many rolls at most.
static const int DCC_THEME_DERIVED_BASES = 12;
static const int DCC_THEME_DERIVED_TRIES = 200;

/**
 * A floor theme's random-monster list spec with {here} and {deeper} filled
 * in: {here} is the floor itself ("D:5"), {deeper} the floor four deeper in
 * the same branch, clamped to its bottom (D:12 and Zot:5 name themselves).
 *
 * @return "" if the floor has no theme, its theme keeps trunk's list, or
 *         its branch has no monster population (the Temple).
 */
string dcc_theme_mon_list(const level_id &place)
{
    const dcc_theme *theme = dcc_theme_for(place);
    if (!theme || !*theme->mon_list || !branch_has_monsters(place.branch))
        return string();
    const level_id deeper(place.branch,
                          min(place.depth + 4, brdepth[place.branch]));
    const string spec = replace_all(theme->mon_list, "{here}",
                                    place.describe());
    return replace_all(spec, "{deeper}", deeper.describe());
}

/// How many box tiers the floor's theme moves its quest up (0 if none).
int dcc_theme_quest_bump(const level_id &place)
{
    const dcc_theme *theme = dcc_theme_for(place);
    return theme ? theme->quest_tier_bump : 0;
}

/// Can a monster of class base be raised as the derived type (MONS_ZOMBIE,
/// MONS_DRAUGR, MONS_SIMULACRUM, MONS_SPECTRAL_THING)? The same tests a des
/// "<base> zombie" spec passes (mons_list::get_zombie_type()).
static bool _can_derive(monster_type derived, monster_type base)
{
    if (invalid_monster_type(base) || needs_resolution(base)
        || mons_is_unique(base))
    {
        return false;
    }
    switch (derived)
    {
    case MONS_SIMULACRUM:
    case MONS_SPECTRAL_THING:
        return mons_class_can_be_spectralised(base);
    case MONS_DRAUGR:
        return mons_has_skeleton(base) && mons_class_can_be_zombified(base);
    default:
        return mons_class_can_be_zombified(base);
    }
}

/**
 * set_vault_mon_list() keeps only the place of a "place:" spec and drops a
 * derived-type modifier on it, so "place:D:5 zombie" alone would give live
 * D:5 monsters. Replace each such spec with concrete "<base> zombie" entries
 * (same weight each) whose bases are rolled from that place; other specs
 * pass through.
 */
static vector<mons_spec> _concrete_specs(const vector<mons_spec> &specs)
{
    vector<mons_spec> out;
    for (const mons_spec &spec : specs)
    {
        if (!spec.place.is_valid() || spec.monbase == MONS_NO_MONSTER
            || !mons_class_is_zombified(spec.monbase))
        {
            out.push_back(spec);
            continue;
        }
        set<monster_type> bases;
        for (int tries = 0; tries < DCC_THEME_DERIVED_TRIES
                            && (int) bases.size() < DCC_THEME_DERIVED_BASES;
             ++tries)
        {
            const monster_type base = pick_random_monster(spec.place);
            if (bases.count(base) || !_can_derive(spec.monbase, base))
                continue;
            bases.insert(base);
            mons_spec derived = spec;
            derived.place = level_id();
            derived.type = spec.monbase;
            derived.monbase = base;
            out.push_back(derived);
        }
    }
    return out;
}

/// " Theme: <name>." for the current floor's theme, or "" if it has none.
string dcc_theme_summary()
{
    const dcc_theme *theme = dcc_theme_for(level_id::current());
    return theme ? string(" Theme: ") + theme->name + "." : string();
}

/**
 * Give the level being built its theme, unless it has one already or takes
 * none (D:1, floors off the connected branches), and apply the theme's
 * build-time levers (dcc_apply_theme_build()) either way. Uses the builder's
 * RNG.
 */
void dcc_assign_theme()
{
    if (!crawl_state.game_is_dcc() || !player_in_connected_branch())
        return;
    const level_id here = level_id::current();
    if (here == level_id(BRANCH_DUNGEON, 1))
        return;

    CrawlHashTable &themes = you.props[DCC_THEMES_KEY].get_table();
    const string key = here.describe();
    if (themes.exists(key))
    {
        if (const dcc_theme *theme = dcc_theme_for(here))
            dcc_apply_theme_build(*theme);
        return;
    }

    const string last = you.props.exists(DCC_LAST_THEME_KEY)
                        ? you.props[DCC_LAST_THEME_KEY].get_string()
                        : string();
    vector<const dcc_theme *> choices;
    for (const dcc_theme &theme : theme_table)
        if (last != theme.id)
            choices.push_back(&theme);
    ASSERT(!choices.empty());

    const dcc_theme *theme = choices[random2(choices.size())];
    themes[key] = string(theme->id);
    you.props[DCC_LAST_THEME_KEY] = string(theme->id);
    dcc_apply_theme_build(*theme);
}

/**
 * Apply a theme's build-time levers to the level being built (the current
 * one): its random-monster list, which the level's own population
 * (_builder_monsters()) and later spawns draw from, and its floor and rock
 * colours, which trunk's _dgn_set_floor_colours() keeps when not BLACK.
 * Runs on every build, since a vetoed level is rebuilt from a reset env. A
 * vault placed later that sets its own monster list wins.
 */
void dcc_apply_theme_build(const dcc_theme &theme)
{
    const string spec = dcc_theme_mon_list(level_id::current());
    if (!spec.empty())
    {
        mons_list ml;
        const string err = ml.add_mons(spec);
        if (!err.empty())
        {
            mprf(MSGCH_ERROR, "Bad monster list for floor theme %s: %s",
                 theme.id, err.c_str());
        }
        else
        {
            vector<mons_spec> specs;
            for (size_t i = 0; i < ml.slot_size(0); ++i)
                specs.push_back(ml.get_monster(0, i));
            specs = _concrete_specs(specs);
            if (!specs.empty())
                set_vault_mon_list(specs);
        }
    }
    if (theme.floor_colour != BLACK)
        env.floor_colour = theme.floor_colour;
    if (theme.rock_colour != BLACK)
        env.rock_colour = theme.rock_colour;
}

/// The Announcer names the current floor's theme, if it has one, and notes
/// it.
void dcc_announce_theme()
{
    const dcc_theme *theme = dcc_theme_for(level_id::current());
    if (!theme)
        return;
    dcc_announce(string("Tonight's floor: ") + theme->name + ". "
                 + theme->blurb);
    take_note(Note(NOTE_MESSAGE, 0, 0, string("Floor theme: ") + theme->name));
}

/**
 * The first arrival on a themed floor: place the theme's extra monsters,
 * asleep, away from the player and outside vaults, bands allowed, drawn
 * from the floor's random-monster list as the level's own population is.
 * Safe cells are already closed to hostiles (dcc_generation_barred_at()).
 * A few may find no spot.
 */
void dcc_apply_theme_arrival()
{
    const dcc_theme *theme = dcc_theme_for(level_id::current());
    if (!theme || !branch_has_monsters(you.where_are_you))
        return;
    for (int i = 0; i < theme->extra_monsters; ++i)
    {
        mgen_data mg(RANDOM_MONSTER);
        mg.behaviour = BEH_SLEEP;
        mg.flags    |= MG_PERMIT_BANDS;
        mg.proximity = PROX_AWAY_FROM_PLAYER;
        mg.map_mask |= MMT_NO_MONS;
        if (player_in_branch(BRANCH_SHOALS))
            mg.flags |= MG_PREFER_LAND;
        mons_place(mg);
    }
}
