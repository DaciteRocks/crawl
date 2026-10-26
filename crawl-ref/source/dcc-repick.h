/**
 * @file
 * @brief Crawl Live (DCC) floor-3 species re-pick: one re-roll per game,
 *        offered on the first arrival at D:3 (or the first Dungeon floor
 *        below it, after a shaft past D:3).
**/

#pragma once

#include "species-type.h"

// you.props keys.
// Bool: an offer is pending (rolled on the first arrival at D:3, not yet
// taken or declined).
#define DCC_REPICK_OFFER_KEY "dcc_repick_offer"
// Vector of the three offered species (species_type ints), while the offer
// is pending.
#define DCC_REPICK_CHOICES_KEY "dcc_repick_choices"
// String: the new species' name, or "declined". Its presence is the
// one-shot: the offer never comes back once it is set.
#define DCC_REPICKED_KEY "dcc_repicked"
// How many species an offer holds.
const int DCC_REPICK_CHOICE_COUNT = 3;

vector<species_type> dcc_repick_pool();
void dcc_repick_roll();
void dcc_repick_arrived();
bool dcc_repick_pending();
vector<species_type> dcc_repick_choices();
bool dcc_repick_apply(species_type sp);
void dcc_repick_decline();
