/**
 * @file
 * @brief Crawl Live (DCC) loot boxes: opened at the safe-room kiosk.
 *
 * Boxes are not items: you.props[DCC_BOXES_KEY] holds the tiers awarded
 * (dcc_award_box(), dcc-achieve.cc) and not yet opened. They open only while
 * the player stands on a safe room's kiosk cell, a safe cell carrying the
 * DCC_KIOSK_PROP marker property (safe_rooms.des). Stepping onto the kiosk
 * runs dcc_kiosk_activate() (dat/dlua/dcc.lua), which calls
 * dcc_open_boxes().
 *
 * Opening:
 * - asks first (skipped for headless scripts and tests, or when forced);
 * - in a live game takes DCC_BOX_OPEN_TURNS turns per box, as an
 *   uninterruptible delay (nothing can hurt the player on a safe cell), and
 *   the floor clock runs meanwhile; the boxes leave the player's winnings
 *   only when the delay finishes, so a delay that is cut short (a floor
 *   collapse, a reload) loses nothing;
 * - drops every item on the floor, on and around the kiosk (plain floor
 *   cells of the safe room within radius 2, the emptiest first), never into
 *   the pack, so a full inventory does not matter;
 * - never makes trunk cull the level's items: a box opens only if the item
 *   table has a free slot for each item it holds; otherwise it and the boxes
 *   after it stay sealed for later (a "jam").
 *
 * Every roll uses the game's own RNG, which the save file keeps, so a
 * restored save opens the same boxes into the same items: save-scumming the
 * kiosk is no better than save-scumming anything else in DCSS. The boxes
 * come only from one-shot achievements (and later one quest per floor), so
 * there is nothing to farm.
 *
 * The game_is_dcc() checks here are the authoritative guard (repo CLAUDE.md
 * isolation rule).
**/

#include "AppHdr.h"

#include "dcc-boxes.h"

#include "acquire.h"
#include "artefact.h" // make_item_randart, is_artefact
#include "coordit.h"
#include "dcc.h" // dcc_announce
#include "dcc-safe.h" // is_dcc_safe_cell
#include "delay.h"
#include "env.h"
#include "items.h"
#include "level-id.h"
#include "makeitem.h" // items, item_set_appearance
#include "mapmark.h"
#include "message.h"
#include "notes.h"
#include "player.h"
#include "prompt.h" // yesno
#include "random.h" // random_choose
#include "state.h"
#include "stringutil.h" // make_stringf, comma_separated_line

// What a box holds. "rand": a random item of the floor's level, as the
// dungeon generates them; "acq": acquirement quality, from a random
// acquirement class.
struct dcc_box_contents
{
    int rand;
    int acq;
    // One of the acq items is a weapon or armour, if the player can use
    // either.
    bool forced_gear;
    // Randarts: weapons, armour or jewellery of acquirement quality made
    // into randarts.
    int randart;
    // Wands of acquirement quality.
    int wand;
    // The Announcer's line as the box opens.
    const char *line;
};

// Indexed by dcc_box_tier.
static const dcc_box_contents dcc_box_table[] =
{
    // Bronze
    { 2, 0, false, 0, 0,
      "A Bronze box! Inside: whatever fell behind the studio couch." },
    // Silver
    { 1, 1, false, 0, 0,
      "Silver! The audience claps politely, the way you clap for a "
      "cousin's recital." },
    // Gold
    { 0, 2, false, 0, 0,
      "A Gold box. Somebody in the control room actually sat up." },
    // Platinum
    { 0, 3, true, 0, 0,
      "Platinum! Accounting would like to know who approved this." },
    // Legendary
    { 0, 3, false, 1, 0,
      "A Legendary box. The lights dim, the drums roll, and the insurance "
      "premium doubles." },
    // Celestial
    { 0, 4, false, 1, 1,
      "A Celestial box! Grown producers are weeping. That one was supposed "
      "to be a prop." },
};
COMPILE_CHECK(ARRAYSZ(dcc_box_table) == NUM_DCC_BOX_TIERS);

// Item creation (items(), get_mitm_slot()) culls the level's items once no
// slot below MAX_ITEMS minus this is free; boxes never let it get there.
static const int DCC_ITEM_RESERVE = 10;

static dcc_box_tier _valid_tier(int tier)
{
    if (tier < 0 || tier >= NUM_DCC_BOX_TIERS)
        return DCC_BOX_BRONZE;
    return static_cast<dcc_box_tier>(tier);
}

/// How many items a box of this tier holds.
int dcc_box_item_count(dcc_box_tier tier)
{
    const dcc_box_contents &box = dcc_box_table[_valid_tier(tier)];
    return box.rand + box.acq + box.randart + box.wand;
}

/// How many turns the kiosk takes to open this many boxes.
int dcc_box_open_turns(int boxes)
{
    return max(0, boxes) * DCC_BOX_OPEN_TURNS;
}

/// How many items can still be made on this level without trunk culling
/// any: free item slots below the reserve get_mitm_slot() keeps.
int dcc_free_item_slots()
{
    int n = 0;
    for (int i = 0; i < MAX_ITEMS - DCC_ITEM_RESERVE; ++i)
        if (!env.item[i].defined())
            ++n;
    return n;
}

/// Is the player standing on a safe-room kiosk?
bool dcc_at_kiosk()
{
    return crawl_state.game_is_dcc()
           && is_dcc_safe_cell(you.pos())
           && env.markers.property_at(you.pos(), MAT_ANY, DCC_KIOSK_PROP)
              == "1";
}

/// A random item of the floor's level. The level is the Dungeon depth plus
/// three, counted from absolute depth so a branch floor rolls for how deep
/// it really is (D:N gives N + 3, as you.depth + 3 would).
static int _make_rand_item()
{
    const int level = env.absdepth0 + 1 + 3;
    for (int tries = 0; tries < 10; ++tries)
    {
        const int idx = items(true, OBJ_RANDOM, OBJ_RANDOM, level);
        if (idx != NON_ITEM)
        {
            if (!is_stackable_item(env.item[idx]))
                origin_acquired(env.item[idx], AQ_DCC_BOX);
            return idx;
        }
    }
    return NON_ITEM;
}

/// An item of acquirement quality from the first of these classes that
/// yields one; failing all of them, a random item.
static int _make_acq_item(const vector<object_class_type> &classes)
{
    for (object_class_type cls : classes)
    {
        const int idx = acquirement_create_item(cls, AQ_DCC_BOX, true);
        if (idx != NON_ITEM)
            return idx;
    }
    return _make_rand_item();
}

/// The player's acquirement classes, with the ones in `first` (in the
/// shuffled order) moved to the front.
static vector<object_class_type> _classes_preferring(
    const vector<object_class_type> &first)
{
    vector<object_class_type> classes = shuffled_acquirement_classes(false);
    stable_partition(classes.begin(), classes.end(),
                     [&first](object_class_type c)
                     {
                         return find(first.begin(), first.end(), c)
                                != first.end();
                     });
    return classes;
}

static int _make_randart_item()
{
    const int idx = _make_acq_item(
        _classes_preferring({ OBJ_WEAPONS, OBJ_ARMOUR, OBJ_JEWELLERY }));
    if (idx == NON_ITEM)
        return idx;
    item_def &item = env.item[idx];
    if ((item.base_type == OBJ_WEAPONS || item.base_type == OBJ_ARMOUR
         || item.base_type == OBJ_JEWELLERY)
        && !is_artefact(item)
        && make_item_randart(item, true))
    {
        item_set_appearance(item);
    }
    return idx;
}

/// The items of one box, made but not yet on the floor.
static vector<int> _make_box_items(dcc_box_tier tier)
{
    const dcc_box_contents &box = dcc_box_table[tier];
    vector<int> made;
    auto keep = [&made](int idx)
    {
        if (idx != NON_ITEM)
            made.push_back(idx);
    };

    for (int i = 0; i < box.rand; ++i)
        keep(_make_rand_item());
    for (int i = 0; i < box.acq; ++i)
    {
        if (box.forced_gear && i == 0)
            keep(_make_acq_item(_classes_preferring({ OBJ_WEAPONS,
                                                      OBJ_ARMOUR })));
        else
            keep(_make_acq_item(shuffled_acquirement_classes(false)));
    }
    for (int i = 0; i < box.randart; ++i)
        keep(_make_randart_item());
    for (int i = 0; i < box.wand; ++i)
        keep(_make_acq_item(_classes_preferring({ OBJ_WANDS })));
    return made;
}

static int _stacks_at(const coord_def &c)
{
    int n = 0;
    for (stack_iterator si(c); si; ++si)
        ++n;
    return n;
}

/// Where box items may land: plain floor cells of the safe room within
/// radius 2 of the kiosk, in its line of sight, kiosk included, nearest
/// first. No randomness here: the prizes alone use the RNG.
static vector<coord_def> _drop_cells(const coord_def &centre)
{
    vector<coord_def> cells;
    for (radius_iterator ri(centre, 2, C_SQUARE, LOS_NO_TRANS); ri; ++ri)
        if (is_dcc_safe_cell(*ri) && env.grid(*ri) == DNGN_FLOOR)
            cells.push_back(*ri);
    if (cells.empty())
        cells.push_back(centre);
    stable_sort(cells.begin(), cells.end(),
                [&centre](const coord_def &a, const coord_def &b)
                {
                    return grid_distance(a, centre) < grid_distance(b, centre);
                });
    return cells;
}

/// Drop an item on the drop cell holding the fewest item stacks (the
/// nearest on a tie), so a box spreads out.
static void _drop_item(int idx, const vector<coord_def> &cells)
{
    coord_def best = cells[0];
    int best_n = INT_MAX;
    for (const coord_def &c : cells)
    {
        const int n = _stacks_at(c);
        if (n < best_n)
        {
            best = c;
            best_n = n;
        }
    }
    move_item_to_grid(&idx, best, true);
}

/**
 * Open the held boxes, oldest first, onto the floor around the player.
 * Stops at the first box the item table has no room for (a jam); it and
 * the rest stay sealed.
 *
 * @return how many boxes opened.
 */
static int _open_held_boxes()
{
    if (!crawl_state.game_is_dcc() || !you.props.exists(DCC_BOXES_KEY))
        return 0;

    const vector<coord_def> cells = _drop_cells(you.pos());
    CrawlVector &boxes = you.props[DCC_BOXES_KEY].get_vector();
    int opened = 0;
    while (!boxes.empty())
    {
        const dcc_box_tier tier = _valid_tier(boxes[0].get_int());
        if (dcc_free_item_slots() < dcc_box_item_count(tier))
        {
            dcc_announce(make_stringf("The kiosk jams: this floor is too "
                                      "cluttered for another prize. Your "
                                      "%d remaining box%s stay%s sealed.",
                                      (int)boxes.size(),
                                      boxes.size() == 1 ? "" : "es",
                                      boxes.size() == 1 ? "s" : ""));
            break;
        }
        boxes.erase(0);
        ++opened;

        const vector<int> made = _make_box_items(tier);
        vector<string> names;
        for (int idx : made)
        {
            names.push_back(env.item[idx].name(DESC_A));
            _drop_item(idx, cells);
        }

        mprf(MSGCH_DCC, "%s", dcc_box_table[tier].line);
        mprf("The %s box pops open%s.", dcc_box_tier_name(tier),
             made.empty() ? ", and it is empty. Rigged, as promised"
             : made.size() == 1 ? ", and a prize drops out"
             : make_stringf(", and %d prizes spill out",
                            (int)made.size()).c_str());
        take_note(Note(NOTE_MESSAGE, 0, 0,
                       make_stringf("Opened a %s box: %s",
                                    dcc_box_tier_name(tier),
                                    names.empty() ? "nothing"
                                    : comma_separated_line(names.begin(),
                                                           names.end())
                                      .c_str())));
    }
    if (boxes.empty())
        you.props.erase(DCC_BOXES_KEY);

    // Box-related achievements (none yet) unlock here.
    dcc_check_achievements();
    return opened;
}

/// The kiosk at work: DCC_BOX_OPEN_TURNS turns a box, then they open. It
/// cannot be interrupted (nothing can hurt the player on a safe cell); if
/// the player is no longer on that kiosk by then (a floor collapse moved
/// them), the boxes stay sealed.
class DccUnboxDelay : public Delay
{
    coord_def kiosk;
    level_id where;

    bool still_there() const
    {
        return level_id::current() == where && you.pos() == kiosk
               && dcc_at_kiosk();
    }

    bool invalidated() override
    {
        if (still_there())
            return false;
        mprf(MSGCH_DCC, "You are not at the kiosk any more. Your boxes stay "
                        "sealed for the next one.");
        return true;
    }

    void tick() override
    {
        mprf(MSGCH_MULTITURN_ACTION, "%s",
             random_choose("The kiosk whirs and plays a drumroll.",
                           "The kiosk's lights chase each other around its "
                           "rim.",
                           "Somewhere, a recorded audience gasps."));
    }

    void finish() override
    {
        if (still_there())
            _open_held_boxes();
    }

public:
    DccUnboxDelay(int dur, const coord_def &pos, const level_id &place)
        : Delay(dur), kiosk(pos), where(place)
    { }

    const char *name() const override
    {
        return "dcc_unbox";
    }
};

/**
 * Open the player's boxes at a safe-room kiosk (stepping onto the kiosk
 * calls this, through dcc_kiosk_activate() in dat/dlua/dcc.lua).
 *
 * Refused unless the player stands on a kiosk cell inside a safe room.
 * Asks first, unless forced or headless. In a live game the opening is a
 * delay of dcc_box_open_turns() turns that ends with the boxes opening;
 * headless scripts and tests open them at once.
 *
 * @param force  skip the question.
 * @return what happened.
 */
dcc_open_result dcc_open_boxes(bool force)
{
    if (!crawl_state.game_is_dcc())
        return dcc_open_result::not_dcc;

    if (!dcc_at_kiosk())
    {
        dcc_announce(is_dcc_safe_cell(you.pos())
            ? "Boxes open at the kiosk. That is a floor tile."
            : "Boxes open at a safe-room kiosk and nowhere else. Out here "
              "they're just very heavy paperweights.");
        return dcc_open_result::no_kiosk;
    }

    const int boxes = dcc_box_count();
    if (!boxes)
    {
        // Travel often crosses the kiosk; only a deliberate step says so.
        if (!you.running)
        {
            mprf(MSGCH_DCC, "The kiosk's screen reads: NO BOXES ON FILE. "
                            "Achievements pay out here; go and earn one.");
        }
        return dcc_open_result::no_boxes;
    }

    const int turns = dcc_box_open_turns(boxes);
    const bool headless = crawl_state.script || crawl_state.test;
    if (!force && !headless)
    {
        const string prompt =
            make_stringf("Open your %d loot box%s? It takes %d turns.",
                         boxes, boxes == 1 ? "" : "es", turns);
        if (!yesno(prompt.c_str(), true, 'n'))
        {
            canned_msg(MSG_OK);
            return dcc_open_result::declined;
        }
    }

    const dcc_box_tier first = _valid_tier(
        you.props[DCC_BOXES_KEY].get_vector()[0].get_int());
    if (dcc_free_item_slots() < dcc_box_item_count(first))
    {
        dcc_announce("The kiosk jams: this floor is too cluttered for "
                     "another prize. Your boxes stay sealed.");
        return dcc_open_result::jammed;
    }

    if (headless)
    {
        return _open_held_boxes() > 0 ? dcc_open_result::opened
                                      : dcc_open_result::jammed;
    }

    stop_delay(true);
    mprf("You feed %s into the kiosk.",
         boxes == 1 ? "your box" : "your boxes");
    start_delay<DccUnboxDelay>(turns, you.pos(), level_id::current());
    return dcc_open_result::opened;
}
