/**
 * @file
 * @brief Crawl Live (DCC) floor bosses: one named, buffed monster placed on
 *        the first arrival at D:3, D:6, D:9, D:12 and Zot:5.
**/

#pragma once

#include "branch-type.h"

class monster;
class level_id;

// The monster prop that marks a floor boss; its value is the floor's
// level_id::describe() ("D:3").
#define DCC_BOSS_KEY "dcc_boss"

// How far out a boss stands: at least this percent of the farthest good
// spot's walking distance from where the player arrived (dcc_quest_spot()).
const int DCC_BOSS_FAR_PERCENT = 60;

/// One floor boss: where it lives and what it is.
struct dcc_boss
{
    branch_type branch;
    int depth;
    const char *name;        // as the boss's name reads, "the Opening Act"
    const char *base;        // the monster type the spec starts with, "ogre"
    const char *spec;        // a des MONS spec (syntax.txt)
    const char *intro;       // the Announcer's line on first sight (6b)
    const char *death;       // the Announcer's line on its death (6b)
    const char *achievement; // the kill achievement's id (6b)
};

const vector<dcc_boss> &dcc_boss_table();
const dcc_boss *dcc_boss_for(const level_id &place);
monster *dcc_find_boss();
bool dcc_is_boss(const monster &mons);
int dcc_boss_count();
monster *dcc_place_boss();
