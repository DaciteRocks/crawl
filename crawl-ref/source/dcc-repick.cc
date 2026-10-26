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
 * species-only; kits (Phase 6h, dcc_give_kit()) stand in for the class:
 * a few fixed item specs dropped at the player's feet, never written into
 * the pack, one kit per game.
**/

#include "AppHdr.h"

#include "dcc-repick.h"

#include <algorithm>

#include "act-iter.h" // monster_iterator
#include "branch-type.h"
#include "cio.h" // CK_ENTER, CK_REDRAW
#include "coordit.h" // distance_iterator
#include "dcc.h" // dcc_announce
#include "dcc-achieve.h" // dcc_check_achievements, dcc_unlock_batch
#include "dcc-boxes.h" // dcc_free_item_slots
#include "dcc-room.h" // dcc_settle_sponsor_booth
#include "dcc-safe.h" // is_dcc_safe_cell
#include "delay.h" // you_are_delayed
#include "english.h" // article_a
#include "env.h"
#include "hiscores.h" // mark_milestone
#include "dungeon.h" // dgn_place_multiple_items
#include "item-name.h" // is_useless_item
#include "item-prop.h" // item_type_is_equipment
#include "items.h" // link_items, identify_item
#include "libutil.h" // mouse_control, toalower
#include "macro.h" // has_pending_input
#include "level-id.h"
#include "mapdef.h" // item_list
#include "message.h"
#include "monster.h"
#include "mutation-type.h"
#include "notes.h"
#include "output.h" // redraw_screen
#include "player.h"
#include "random.h" // shuffle_array
#include "religion.h" // god_name, is_good_god
#include "species.h"
#include "state.h"
#include "stringutil.h" // make_stringf
#include "terrain.h" // feat_is_solid, feat_eliminates_items
#include "ui.h" // ui::getch
#ifdef USE_TILE_LOCAL
# include "tilesdl.h" // tiles.layout_statcol
#endif

/// The first floor the offer rolls on: D:3, or the first Dungeon floor
/// below it the player reaches (a shaft from D:2 skips D:3).
const int REPICK_MIN_DEPTH = 3;

// The player put the live question off (the re-roll or the kit owed after
// it; Escape at the question, or a jammed kit): it is not asked again
// until they have stepped off the safe room's cells and come back onto
// one. Not saved; a fresh roll, a new or restored game and every level
// change clear it (dcc_repick_level_loaded()).
static bool _put_off = false;

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
    _put_off = false;
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
    // The re-roll is one bundle: the new body is owed a kit.
    if (!you.props.exists(DCC_KIT_KEY))
        you.props[DCC_KIT_PENDING_KEY].get_bool() = true;
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

/// The kits. Specs are des ITEM: syntax (docs/develop/levels/syntax.txt);
/// the smoke checks that every line parses. "Good" items, not
/// acquirement-grade: boxes are the loot mechanic. The armour and the
/// weapon get a fixed plus rather than good_item, which makes them
/// artefacts as often as not; no_uniq keeps every piece of equipment from
/// rolling as a randart (a randart ring is not the ring on the label).
static const vector<dcc_kit> _kits =
{
    { "steel", "Steel",
      "A sword, a shirt, and something to hide behind.",
      { "long sword plus:2 no_uniq", "scale mail plus:2 no_uniq",
        "kite shield plus:1 no_uniq",
        "potion of might q:2" } },
    { "arcana", "Arcana",
      "Spells, a robe, and the ring the budget allowed.",
      { "book of flames", "robe ego:resistance no_uniq",
        "ring of magical power no_uniq",
        "potion of magic q:3" } },
    { "supplies", "Supplies",
      "For the contestant who plans to run.",
      { "potion of heal wounds q:3", "scroll of blinking q:2",
        "scroll of teleportation q:3", "potion of haste q:2" } },
};

const vector<dcc_kit> &dcc_kit_table()
{
    return _kits;
}

/// The kit with this id, or nullptr.
const dcc_kit *dcc_find_kit(const string &id)
{
    for (const dcc_kit &kit : _kits)
        if (id == kit.id)
            return &kit;
    return nullptr;
}

/// Can items lie here and stay reachable?
static bool _kit_cell_ok(const coord_def &c)
{
    const dungeon_feature_type feat = env.grid(c);
    return !feat_is_solid(feat) && !feat_eliminates_items(feat);
}

/**
 * Where the kit lands, as box drops prefer plain floor: the player's cell
 * if it is plain floor; else the nearest plain floor cell in sight; else
 * the nearest cell in sight (the player's first) where items stay
 * reachable. Never a cell that destroys or swallows items.
 *
 * @param[out] spot  the cell found.
 * @return           false if no cell in sight qualifies (a flyer over a
 *                   lake of lava or deep water).
 */
static bool _kit_spot(coord_def &spot)
{
    if (env.grid(you.pos()) == DNGN_FLOOR)
    {
        spot = you.pos();
        return true;
    }
    for (distance_iterator di(you.pos(), false, true, LOS_RADIUS); di; ++di)
    {
        if (env.grid(*di) == DNGN_FLOOR && you.see_cell_no_trans(*di))
        {
            spot = *di;
            return true;
        }
    }
    for (distance_iterator di(you.pos(), false, false, LOS_RADIUS); di; ++di)
    {
        if (_kit_cell_ok(*di) && (*di == you.pos()
                                  || you.see_cell_no_trans(*di)))
        {
            spot = *di;
            return true;
        }
    }
    return false;
}

/// The kit cannot be delivered now; the one-shot is kept for later.
static void _announce_kit_jam(const dcc_kit &kit, const char *why)
{
    dcc_announce(make_stringf("The %s kit is stuck in the chute: %s",
                              kit.name, why));
}

/**
 * Give the player a kit: its items, made from the table's specs with
 * trunk's dgn_place_multiple_items() (which puts every item of the list
 * on the one cell it is given, unlinked; link_items() follows), land
 * identified on one floor cell at or near the player's feet (_kit_spot();
 * never in the pack, so a full inventory does not matter). The Announcer
 * names it, a note records it, and the one-shot is spent.
 *
 * @param id     the kit's id in dcc_kit_table().
 * @param force  give it even if a kit was already given (scripts).
 * @return       false, giving nothing, outside Crawl Live, for an unknown
 *               id, when a kit was already given (unless forced), or when
 *               the kit cannot be put down: the level's item table has no
 *               room for it, or no cell in sight holds items safely. The
 *               Announcer says so and the one-shot is kept for later.
 */
bool dcc_give_kit(const string &id, bool force)
{
    if (!crawl_state.game_is_dcc())
        return false;
    const dcc_kit *kit = dcc_find_kit(id);
    if (!kit || (you.props.exists(DCC_KIT_KEY) && !force))
        return false;

    item_list list;
    for (const char *spec : kit->items)
    {
        const string err = list.add_item(spec);
        if (!err.empty())
        {
            mprf(MSGCH_ERROR, "DCC kit %s: bad item spec \"%s\": %s",
                 kit->id, spec, err.c_str());
        }
    }

    // Each spec makes one item; never make trunk cull the level's items.
    if (dcc_free_item_slots() < static_cast<int>(list.size()))
    {
        _announce_kit_jam(*kit, "this floor is too cluttered for a "
                                "delivery. Clear some space and ask again.");
        return false;
    }
    coord_def spot;
    if (!_kit_spot(spot))
    {
        _announce_kit_jam(*kit, "there is nowhere here to put it down. "
                                "Find some solid floor and ask again.");
        return false;
    }

    // dgn_place_multiple_items() returns no indices: the kit's items are
    // the slots that were free before it and are filled after.
    vector<bool> was_free(MAX_ITEMS);
    for (int i = 0; i < MAX_ITEMS; ++i)
        was_free[i] = !env.item[i].defined();
    dgn_place_multiple_items(list, spot);
    link_items();
    // A kit is a known quantity: what it holds is on the label.
    for (int i = 0; i < MAX_ITEMS; ++i)
        if (was_free[i] && env.item[i].defined())
            identify_item(env.item[i]);

    you.props[DCC_KIT_KEY].get_string() = kit->id;
    you.props.erase(DCC_KIT_PENDING_KEY);
    dcc_announce(make_stringf("Kit delivered: %s. %s", kit->name,
                              kit->blurb));
    take_note(Note(NOTE_MESSAGE, 0, 0,
                   make_stringf("Received the %s kit", kit->name)));
    return true;
}

/// Is an accepted re-roll still owed its kit?
bool dcc_kit_pending()
{
    return crawl_state.game_is_dcc()
           && you.props.exists(DCC_KIT_PENDING_KEY)
           && you.props[DCC_KIT_PENDING_KEY].get_bool()
           && !you.props.exists(DCC_KIT_KEY);
}

/**
 * Does this kit suit the player's body as it is now? Its equipment
 * (weapons, staves, armour, jewellery) must all be usable
 * (is_useless_item(): a Felid wields nothing, an Octopode wears no body
 * armour, a small body cannot take a large shield). Potions, scrolls and
 * books do not count: they are the part of a kit anyone can carry off.
 */
bool dcc_kit_suits(const dcc_kit &kit)
{
    item_list list;
    for (const char *spec : kit.items)
        list.add_item(spec);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const item_spec spec = list.get_item(i);
        if (!item_type_is_equipment(spec.base_type)
            || spec.sub_type == OBJ_RANDOM)
        {
            continue;
        }
        item_def item;
        item.base_type = spec.base_type;
        item.sub_type = spec.sub_type;
        item.quantity = 1;
        if (is_useless_item(item, false, true))
            return false;
    }
    return true;
}

/// The kits on offer to the player now: those that suit them
/// (dcc_kit_suits()), in table order; the Supplies kit if none does.
vector<const dcc_kit *> dcc_kit_offer()
{
    vector<const dcc_kit *> offer;
    for (const dcc_kit &kit : _kits)
        if (dcc_kit_suits(kit))
            offer.push_back(&kit);
    if (offer.empty())
        if (const dcc_kit *supplies = dcc_find_kit("supplies"))
            offer.push_back(supplies);
    return offer;
}

/// Once per player turn (dcc_tick_clock(), so it runs during travel and
/// rests as well): off the safe room's cells, a put-off question (the
/// re-roll or the kit) is asked again the next time the player stands on
/// one, which includes arriving in the next floor's safe room.
void dcc_repick_turn()
{
    if (_put_off && !is_dcc_safe_cell(you.pos()))
        _put_off = false;
}

/// A game was started or restored, or the player changed level
/// (dcc_start_stair(), from load_level()): forget any put-off question.
/// The static flag outlives a game in the process; this keeps one game's
/// Escape from carrying into the next save loaded.
void dcc_repick_level_loaded()
{
    _put_off = false;
}

/// Does this key put the question off? Escape (or Ctrl-G), Enter or
/// space; a hangup reads as Escape.
static bool _is_later_key(int key)
{
    return key_is_escape(key) || key == CK_ENTER || key == '\n'
           || key == ' ';
}

/// Show a prompt and read one key for it, as yesno() does (the keymap
/// context of a confirmation; the messages cleared after). A redraw
/// request (a resized terminal) redraws the screen and asks again. A
/// hangup reads as Escape.
static int _prompt_key(const string &prompt)
{
    mouse_control mc(MOUSE_MODE_YESNO);
    while (true)
    {
        if (crawl_state.seen_hups)
            return ESCAPE;
        mprf(MSGCH_PROMPT, "%s", prompt.c_str());
        const int key = ui::getch(KMC_CONFIRM);
        clear_messages();
        if (crawl_state.seen_hups)
            return ESCAPE;
        if (key != CK_REDRAW)
            return key;
        redraw_screen();
        update_screen();
    }
}

/// The question is put off until the player is next back on a safe cell.
static void _put_question_off()
{
    _put_off = true;
    if (!crawl_state.seen_hups)
        canned_msg(MSG_OK);
}

/// The species or the kit changed what the screen shows (the title line,
/// the stat column, the doll), as after wizard_change_species().
static void _redraw_after_repick()
{
#ifdef USE_TILE_LOCAL
    tiles.layout_statcol();
#endif
    redraw_screen();
    update_screen();
}

/// A kit's number in the prompt: its place in the full table, so a key
/// always means the same kit.
static int _kit_number(const dcc_kit &kit)
{
    return static_cast<int>(&kit - _kits.data()) + 1;
}

/**
 * Ask for the kit an accepted re-roll is owed, among dcc_kit_offer():
 * "(1) Steel, (2) Arcana, (3) Supplies", leaving out kits that do not suit
 * the player. There is no declining. A kit's number settles it; Escape,
 * Enter, space or a hangup puts it off, as does a jammed delivery (the
 * Announcer says why), until the player is next back on a safe cell; any
 * other key asks again.
 */
static void _ask_kit()
{
    const vector<const dcc_kit *> offer = dcc_kit_offer();
    string prompt = "Kit:";
    for (size_t i = 0; i < offer.size(); ++i)
    {
        prompt += make_stringf("%s (%d) %s", i ? "," : "",
                               _kit_number(*offer[i]), offer[i]->name);
    }
    prompt += " (Esc: ask next time you're in a safe room)";

    while (true)
    {
        const int key = _prompt_key(prompt);
        if (_is_later_key(key))
        {
            _put_question_off();
            return;
        }
        for (const dcc_kit *kit : offer)
        {
            if (key == '0' + _kit_number(*kit))
            {
                if (dcc_give_kit(kit->id))
                    _redraw_after_repick();
                else
                    _put_off = true; // jammed: the kit stays owed
                return;
            }
        }
    }
}

/**
 * Ask the re-roll: the Announcer's pitch, then one prompt naming the
 * offered species. A letter takes that species (dcc_repick_apply(); if
 * it refuses the pick for the player's god, the Announcer says why and the
 * prompt comes back) and goes on to the kit; n declines everything
 * (dcc_repick_decline(), no kit). Escape, Enter, space or a hangup puts
 * the offer off until the player has left the safe room's cells and come
 * back; any other key asks again.
 */
static void _ask_repick()
{
    dcc_announce("One-time offer from the studio: a re-roll. Three species "
                 "on the table, and a kit to go with the new look. Decline "
                 "and you keep everything you walked in with.");
    while (dcc_repick_pending())
    {
        const vector<species_type> choices = dcc_repick_choices();
        string prompt = "Re-roll as";
        for (size_t i = 0; i < choices.size(); ++i)
        {
            prompt += make_stringf(" (%c) %s,", static_cast<char>('a' + i),
                                   species::name(choices[i]).c_str());
        }
        prompt += " or (n) keep what you have? (Esc: later)";

        const int key = _prompt_key(prompt);
        if (_is_later_key(key))
        {
            _put_question_off();
            return;
        }
        const int lower = toalower(key);
        if (lower == 'n')
        {
            dcc_repick_decline();
            return;
        }
        const int index = lower - 'a';
        if (index >= 0 && index < static_cast<int>(choices.size()))
        {
            if (!dcc_repick_apply(choices[index]))
                continue;
            _redraw_after_repick();
            if (dcc_kit_pending())
                _ask_kit();
            return;
        }
    }
}

/**
 * The main loop is about to read the player's next command
 * (dcc_before_command(), from main.cc _input()): while the player stands
 * on a safe-room cell and has not put the question off, ask for the kit
 * an accepted re-roll is still owed, else the re-roll if one is pending.
 * Never while keys are queued (a macro, sendkeys, a repeated or replayed
 * command would answer it), while running or in a delay, or in headless
 * scripts and tests, which drive dcc_repick_apply() and dcc_give_kit()
 * themselves.
 */
void dcc_repick_before_command()
{
    if (!crawl_state.game_is_dcc() || crawl_state.script || crawl_state.test
        || crawl_state.seen_hups)
    {
        return;
    }
    const bool kit = dcc_kit_pending();
    if (!kit && !dcc_repick_pending())
        return;
    dcc_repick_turn();
    if (has_pending_input() || crawl_state.is_repeating_cmd()
        || crawl_state.is_replaying_keys()
        || you.running || you_are_delayed())
    {
        return;
    }
    if (_put_off || !is_dcc_safe_cell(you.pos()))
        return;
    if (kit)
        _ask_kit();
    else
        _ask_repick();
}

/// Has the player put the re-roll or kit question off? Script helper.
bool dcc_repick_put_off()
{
    return _put_off;
}

/// Put the question off, as Escape would. Script helper.
void dcc_repick_set_put_off()
{
    _put_off = true;
}
