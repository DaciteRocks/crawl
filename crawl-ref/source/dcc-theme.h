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

/// One floor theme: its name and blurb, and the levers it pulls on its floor
/// (dcc_apply_theme_build(), dcc_apply_theme_arrival(), the floor clock and
/// the quest band).
struct dcc_theme
{
    const char *id;
    const char *name;
    const char *blurb;          // the Announcer's line after the name
    const char *mon_list;       // random-monster list spec ("" = trunk's);
                                // {here} and {deeper} name the floor and
                                // the floor four deeper (dcc_theme_mon_list)
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
string dcc_theme_mon_list(const level_id &place);
int dcc_theme_quest_bump(const level_id &place);
void dcc_assign_theme();
void dcc_apply_theme_build(const dcc_theme &theme);
void dcc_announce_theme();
void dcc_apply_theme_arrival();
