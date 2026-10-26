/**
 * @file
 * @brief Crawl Live (DCC) sponsors: signing with a god at the D:3 booth
 *        pays one Gold box, once per game.
**/

#pragma once

#include "god-type.h"
#include "state.h"         // crawl_state, for the trunk hook's guard

// The marker prop on a booth cell (props_marker { dcc_sponsor = "1" } in
// the sponsor safe-room variants, dat/des/dcc/safe_rooms.des).
#define DCC_SPONSOR_PROP "dcc_sponsor"
// you.props: the god (int) the player signed with at a booth. Its presence
// is the one-shot: a later booth signing pays nothing.
#define DCC_SPONSOR_GOD_KEY "dcc_sponsor_god"

bool dcc_at_sponsor_booth();
void dcc_god_joined(god_type god);
void dcc_sponsor_intro();
god_type dcc_sponsor_god();
