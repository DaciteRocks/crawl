/**
 * @file
 * @brief Crawl Live (DCC) floor-3 species re-pick.
 *
 * The first arrival at D:3 (dcc_repick_arrived(), from dcc_enter_floor())
 * rolls three random starting species other than the player's and marks
 * the offer pending. Taking one (dcc_repick_apply()) changes the species
 * with trunk's change_species_to(), the wizard tool that save conversion
 * also uses, plus the follow-up the wizard command does; declining
 * (dcc_repick_decline()) keeps everything. Either spends the offer: it is
 * one re-roll per game, and a revisit, a reload or any other floor never
 * offers it again. Jobs cannot change in DCSS, so the re-pick is
 * species-only; kits (Phase 6h) stand in for the class.
**/

#include "AppHdr.h"

#include "dcc-repick.h"

#include <algorithm>

#include "act-iter.h" // monster_iterator
#include "branch-type.h"
#include "dcc.h" // dcc_announce
#include "dcc-achieve.h" // dcc_check_achievements, dcc_unlock_batch
#include "english.h" // article_a
#include "env.h"
#include "hiscores.h" // mark_milestone
#include "level-id.h"
#include "monster.h"
#include "notes.h"
#include "player.h"
#include "random.h" // shuffle_array
#include "species.h"
#include "state.h"
#include "stringutil.h" // make_stringf

/// Is the player on D:3, the re-pick floor?
static bool _on_repick_floor()
{
    return level_id::current() == level_id(BRANCH_DUNGEON, 3);
}

/// Forget the pending offer and its choices.
static void _clear_offer()
{
    you.props.erase(DCC_REPICK_OFFER_KEY);
    you.props.erase(DCC_REPICK_CHOICES_KEY);
}

/**
 * The player has just arrived on a newly created connected-branch floor
 * (dcc_enter_floor()). On D:3, if no re-pick was ever offered, roll the
 * three choices and mark the offer pending. Says nothing: the offer is
 * asked from the main loop (Phase 6i).
 */
void dcc_repick_arrived()
{
    if (!crawl_state.game_is_dcc() || !_on_repick_floor()
        || you.props.exists(DCC_REPICKED_KEY)
        || you.props.exists(DCC_REPICK_OFFER_KEY))
    {
        return;
    }

    vector<species_type> pool;
    for (species_type sp : species::get_all_species())
        if (species::is_starting_species(sp) && sp != you.species)
            pool.push_back(sp);
    if (pool.empty())
        return;
    shuffle_array(pool);
    if (pool.size() > static_cast<size_t>(DCC_REPICK_CHOICE_COUNT))
        pool.resize(DCC_REPICK_CHOICE_COUNT);

    CrawlVector &choices = you.props[DCC_REPICK_CHOICES_KEY].new_vector(SV_INT);
    for (species_type sp : pool)
        choices.push_back(static_cast<int>(sp));
    you.props[DCC_REPICK_OFFER_KEY].get_bool() = true;
}

/// Is a re-pick offer waiting to be taken or declined?
bool dcc_repick_pending()
{
    return crawl_state.game_is_dcc()
           && you.props.exists(DCC_REPICK_OFFER_KEY)
           && you.props[DCC_REPICK_OFFER_KEY].get_bool()
           && !you.props.exists(DCC_REPICKED_KEY);
}

/// The offered species, while an offer is pending; else empty.
vector<species_type> dcc_repick_choices()
{
    vector<species_type> choices;
    if (!dcc_repick_pending() || !you.props.exists(DCC_REPICK_CHOICES_KEY))
        return choices;
    for (const auto &sp : you.props[DCC_REPICK_CHOICES_KEY].get_vector())
        choices.push_back(static_cast<species_type>(sp.get_int()));
    return choices;
}

/**
 * Take the offer: become `sp`, one of the pending choices. The species
 * change is trunk's change_species_to() (skills rescaled, innate mutations
 * swapped for the new species' up to the current XL, worn items the new
 * body cannot use fall away into the pack), followed by the invisibility
 * fixup that wizard_change_species() does after it. Then the Announcer, a
 * note and a milestone, and the offer is spent.
 *
 * @param sp  the species to become.
 * @return    false (and no change) unless an offer is pending and `sp` is
 *            one of its choices.
 */
bool dcc_repick_apply(species_type sp)
{
    const vector<species_type> choices = dcc_repick_choices();
    if (find(choices.begin(), choices.end(), sp) == choices.end())
        return false;

    dcc_unlock_batch batch;
    change_species_to(sp);

    // As wizard_change_species() (wiz-you.cc) does after the change.
    if (you.can_see_invisible())
        env.invis_knowledge.clear();
    else
    {
        for (monster_iterator mi; mi; ++mi)
            if (testbits((*mi)->flags, MF_WAS_IN_VIEW) && !you.can_see(**mi))
                mi->sense_if_invisible();
    }

    const string name = species::name(sp);
    you.props[DCC_REPICKED_KEY].get_string() = name;
    _clear_offer();
    dcc_announce(make_stringf("Re-rolled. Please welcome the new %s. Same "
                              "contestant, different paperwork.",
                              name.c_str()));
    take_note(Note(NOTE_MESSAGE, 0, 0, "Re-rolled species to " + name));
    mark_milestone("dcc.repick", "became " + article_a(name) + ".");
    dcc_check_achievements();
    return true;
}

/// Decline the offer: keep everything, and the offer is spent.
void dcc_repick_decline()
{
    if (!dcc_repick_pending())
        return;
    you.props[DCC_REPICKED_KEY].get_string() = "declined";
    _clear_offer();
    dcc_announce("Keeping the original. Bold. The sponsors wanted a "
                 "redesign.");
}
