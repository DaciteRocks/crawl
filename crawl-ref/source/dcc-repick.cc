/**
 * @file
 * @brief Crawl Live (DCC) floor-3 species re-pick.
 *
 * The first arrival on a Dungeon floor at D:3 or below (dcc_repick_arrived(),
 * from dcc_enter_floor()) rolls three random starting species other than
 * the player's (dcc_repick_pool() lists what may be offered) and marks the
 * offer pending. Taking one (dcc_repick_apply()) changes the species
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
#include "dcc-room.h" // dcc_settle_sponsor_booth
#include "english.h" // article_a
#include "env.h"
#include "hiscores.h" // mark_milestone
#include "level-id.h"
#include "monster.h"
#include "mutation-type.h"
#include "notes.h"
#include "player.h"
#include "random.h" // shuffle_array
#include "religion.h" // god_name, is_good_god
#include "species.h"
#include "state.h"
#include "stringutil.h" // make_stringf

/// The first floor the offer rolls on: D:3, or the first Dungeon floor
/// below it the player reaches (a shaft from D:2 skips D:3).
const int REPICK_MIN_DEPTH = 3;

/// Is the player on a Dungeon floor at or below the re-pick depth?
static bool _on_repick_floor()
{
    const level_id here = level_id::current();
    return here.branch == BRANCH_DUNGEON && here.depth >= REPICK_MIN_DEPTH;
}

/// Forget the pending offer and its choices.
static void _clear_offer()
{
    you.props.erase(DCC_REPICK_OFFER_KEY);
    you.props.erase(DCC_REPICK_CHOICES_KEY);
}

/**
 * Would `god` refuse a follower of species `sp`? The species-bound half of
 * cannot_join_god_reason() (religion.cc): the forlorn (Demigods) with any
 * god, the undead and the demonic with a good god, innate casters with
 * Sif Muna, Vehumet or Kikubaaqudgha.
 */
static bool _god_rejects_species(god_type god, species_type sp)
{
    if (god == GOD_NO_GOD)
        return false;
    if (species::mutation_level(sp, MUT_FORLORN))
        return true;
    if (is_good_god(god)
        && (species::is_undead(sp) || sp == SP_DEMONSPAWN))
    {
        return true;
    }
    return species::mutation_level(sp, MUT_INNATE_CASTER)
           && (god == GOD_SIF_MUNA || god == GOD_VEHUMET
               || god == GOD_KIKUBAAQUDGHA);
}

/**
 * The species an offer may hold: starting species other than the player's
 * own, leaving out
 * - innate casters (Djinni): change_species_to() does not set up their
 *   innate spells (ng-setup.cc _setup_innate_spells), so spellcasting
 *   would not work;
 * - the base Draconian, for a Draconian of any colour;
 * - any species the player's current god would refuse.
 */
vector<species_type> dcc_repick_pool()
{
    vector<species_type> pool;
    for (species_type sp : species::get_all_species())
    {
        if (!species::is_starting_species(sp) || sp == you.species
            || species::mutation_level(sp, MUT_INNATE_CASTER)
            || (sp == SP_BASE_DRACONIAN
                && species::is_draconian(you.species))
            || _god_rejects_species(you.religion, sp))
        {
            continue;
        }
        pool.push_back(sp);
    }
    return pool;
}

/// Roll a fresh offer: three species from dcc_repick_pool(), pending.
void dcc_repick_roll()
{
    vector<species_type> pool = dcc_repick_pool();
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

/**
 * The player has just arrived on a newly created connected-branch floor
 * (dcc_enter_floor()). On the first Dungeon floor at D:3 or below, if no
 * re-pick was ever offered, roll the three choices and mark the offer
 * pending. Says nothing: the offer is asked from the main loop (Phase 6i).
 */
void dcc_repick_arrived()
{
    if (!crawl_state.game_is_dcc() || !_on_repick_floor()
        || you.props.exists(DCC_REPICKED_KEY)
        || you.props.exists(DCC_REPICK_OFFER_KEY))
    {
        return;
    }
    dcc_repick_roll();
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

    // The player may have joined a god since the roll.
    if (_god_rejects_species(you.religion, sp))
    {
        dcc_announce(make_stringf("Legal says %s will not have %s on the "
                                  "roster. Pick another, or keep the "
                                  "original.",
                                  god_name(you.religion).c_str(),
                                  article_a(species::name(sp)).c_str()));
        return false;
    }

    dcc_unlock_batch batch;
    const bool was_multilived = you.has_mutation(MUT_MULTILIVED);
    change_species_to(sp);
    // Extra lives belong to the Felid body; trunk's change leaves them.
    if (was_multilived && !you.has_mutation(MUT_MULTILIVED))
        you.lives = 0;

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
    // The booth was settled for the old body; the new one may not be able
    // to join some of its gods.
    dcc_settle_sponsor_booth();
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
