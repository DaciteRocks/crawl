/**
 * @file
 * @brief Crawl Live (DCC) floor themes: one per connected-branch floor after
 *        D:1, picked when the floor is built and announced on arrival.
**/

#pragma once

class level_id;

// you.props keys.
// Hash: level_id::describe() of a floor ("D:3") -> the id of its theme.
#define DCC_THEMES_KEY "dcc_themes"
// String: the id of the theme assigned last, so the next differs.
#define DCC_LAST_THEME_KEY "dcc_last_theme"

/// One floor theme. Phase 6c uses the id, name and blurb; the other columns
/// are the levers Phase 6d pulls.
struct dcc_theme
{
    const char *id;
    const char *name;
    const char *blurb;          // the Announcer's line after the name
    const char *mon_list;       // random-monster list spec ("" = trunk's)
    colour_t floor_colour;      // BLACK = leave trunk's
    colour_t rock_colour;       // BLACK = leave trunk's
    int clock_percent;          // of the floor's clock tunable
    int quest_tier_bump;        // added to the floor's quest band tier
    int extra_monsters;         // placed on the first arrival
};

const vector<dcc_theme> &dcc_theme_table();
const dcc_theme *dcc_theme_by_id(const string &id);
const dcc_theme *dcc_theme_for(const level_id &place);
string dcc_theme_summary();
void dcc_assign_theme();
void dcc_announce_theme();
