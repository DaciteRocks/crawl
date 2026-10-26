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
**/

#include "AppHdr.h"

#include "dcc-theme.h"

#include "branch.h"
#include "dcc.h" // dcc_announce
#include "level-id.h"
#include "notes.h"
#include "player.h"
#include "random.h"
#include "state.h"

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
      "place:{here} zombie w:5 / place:{here} skeleton w:3 / "
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

/// " Theme: <name>." for the current floor's theme, or "" if it has none.
string dcc_theme_summary()
{
    const dcc_theme *theme = dcc_theme_for(level_id::current());
    return theme ? string(" Theme: ") + theme->name + "." : string();
}

/**
 * Give the level being built its theme, unless it has one already or takes
 * none (D:1, floors off the connected branches). Uses the builder's RNG.
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
        return;

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
