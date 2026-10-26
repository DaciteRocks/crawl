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

// Kits (Phase 6h): they stand in for a class change (jobs cannot change in
// DCSS). One per game, given as part of an accepted re-roll (Phase 6i).
// String: the kit id given. Its presence is the one-shot.
#define DCC_KIT_KEY "dcc_kit"
// Bool: an accepted re-roll is owed its kit (set by dcc_repick_apply(),
// erased when dcc_give_kit() delivers one). Saved, so a reload between
// the species change and the kit choice still asks for the kit.
#define DCC_KIT_PENDING_KEY "dcc_kit_pending"

/// A kit: a few des ITEM: specs, dropped at the player's feet.
struct dcc_kit
{
    const char *id;
    const char *name;
    const char *blurb;
    vector<const char *> items;
};

const vector<dcc_kit> &dcc_kit_table();
const dcc_kit *dcc_find_kit(const string &id);
bool dcc_give_kit(const string &id, bool force = false);
bool dcc_kit_pending();
bool dcc_kit_suits(const dcc_kit &kit);
vector<const dcc_kit *> dcc_kit_offer();

// The live offer (Phase 6i), asked from the main loop.
void dcc_repick_turn();
void dcc_repick_before_command();
void dcc_repick_level_loaded();
bool dcc_repick_put_off();
void dcc_repick_set_put_off();
