/**
 * @file
 * @brief Crawl Live (DCC) loot boxes: opened at the safe-room kiosk.
 *
 * Boxes are not items: you.props[DCC_BOXES_KEY] holds the tiers awarded
 * (dcc_award_box(), dcc-achieve.cc) and not yet opened. They open only while
 * the player stands on a safe room's kiosk cell, a safe cell carrying the
 * DCC_KIOSK_PROP marker property (safe_rooms.des). Stepping onto the kiosk
 * runs dcc_kiosk_activate() (dat/dlua/dcc.lua), which calls
 * dcc_kiosk_stepped().
 *
 * Opening:
 * - happens only on a deliberate step onto the kiosk (a plain or safe move
 *   command). Travel, autoexplore, running, a blink or a teleport that
 *   crosses or lands on it does nothing and says nothing, and the kiosk is
 *   no stop point for travel or autoexplore (it has no stop_explore
 *   property). To open after declining, step off and back on;
 * - first works out how many of the held boxes, oldest first, the level's
 *   item table has room for (dcc_boxes_that_fit()). None: the kiosk jams
 *   and nothing is asked. Otherwise it asks (skipped for headless scripts
 *   and tests, or when forced) about exactly the boxes that fit;
 * - in a live game takes DCC_BOX_OPEN_TURNS turns per box, a box popping
 *   open as each one's turns are up, and the floor clock runs meanwhile. A
 *   keypress, a forced interrupt or leaving the kiosk stops it between
 *   boxes: the opened ones stay opened, the rest stay held. Boxes leave the
 *   player's winnings only as they open, so a delay that is cut short (a
 *   floor collapse, a reload) loses nothing;
 * - drops every item on the floor, on and around the kiosk (plain floor
 *   cells of the safe room within radius 2, the emptiest first), never into
 *   the pack, so a full inventory does not matter;
 * - never makes trunk cull the level's items: a box opens only if the item
 *   table has a free slot for each item it holds, re-checked as each box
 *   opens (a jam leaves it and the boxes after it sealed for later).
 *
 * Every roll uses the game's own RNG, which the save file keeps, so a
 * restored save opens the same boxes into the same items: save-scumming the
 * kiosk is no better than save-scumming anything else in DCSS. The boxes
 * come only from one-shot achievements (and later one quest per floor), so
 * there is nothing to farm. No box hands over an item the player's god
 * forbids.
 *
 * The game_is_dcc() checks here are the authoritative guard (repo CLAUDE.md
 * isolation rule).
**/

#include "AppHdr.h"

#include "dcc-boxes.h"

#include "acquire.h"
#include "artefact.h" // make_item_randart, is_artefact
#include "command-type.h"
#include "coordit.h"
#include "dcc.h" // dcc_announce
#include "dcc-safe.h" // is_dcc_safe_cell
#include "delay.h"
#include "env.h"
#include "god-item.h" // god_forbids_item
#include "item-name.h" // is_useless_item
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
    // Artefacts: an item of acquirement quality from a randart-capable class
    // (weapons, armour, jewellery, staves, talismans), made into a randart.
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

// How many times a box rerolls an item it may not hand over (an artefact
// the player's god forbids or cannot use; a forbidden random item).
static const int DCC_REROLLS = 20;

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

/// How many of the held boxes, oldest first, the level has item slots for
/// right now. Each item takes at most one slot (a random item may stack
/// into one already there and take none), so this never over-promises.
int dcc_boxes_that_fit()
{
    if (!crawl_state.game_is_dcc() || !you.props.exists(DCC_BOXES_KEY))
        return 0;
    const CrawlVector &boxes = you.props[DCC_BOXES_KEY].get_vector();
    int slots = dcc_free_item_slots();
    int fit = 0;
    for (const auto &box : boxes)
    {
        slots -= dcc_box_item_count(_valid_tier(box.get_int()));
        if (slots < 0)
            break;
        ++fit;
    }
    return fit;
}

/// Is the player standing on a safe-room kiosk?
bool dcc_at_kiosk()
{
    return crawl_state.game_is_dcc()
           && is_dcc_safe_cell(you.pos())
           && env.markers.property_at(you.pos(), MAT_ANY, DCC_KIOSK_PROP)
              == "1";
}

/// The kiosk's one jam message: the level's item table has no room for the
/// next box, so it and the rest stay sealed.
static void _announce_jam(int sealed)
{
    dcc_announce(make_stringf("The kiosk jams: this floor is too cluttered "
                              "for another prize. Your %d remaining box%s "
                              "stay%s sealed.",
                              sealed, sealed == 1 ? "" : "es",
                              sealed == 1 ? "s" : ""));
}

/// Would the player's god forbid this item? Checked as acquirement's own
/// last check does, with the item treated as identified (so, e.g., an
/// unidentified evil brand still counts).
static bool _god_forbids(item_def &item)
{
    const auto oldflags = item.flags;
    item.flags |= ISFLAG_IDENTIFIED;
    const bool forbidden = god_forbids_item(item);
    item.flags = oldflags;
    return forbidden;
}

/// As _god_forbids(), or useless to the player.
static bool _unwanted(item_def &item)
{
    const auto oldflags = item.flags;
    item.flags |= ISFLAG_IDENTIFIED;
    const bool useless = is_useless_item(item, false);
    item.flags = oldflags;
    return useless || _god_forbids(item);
}

/// A random item of the floor's level. The level is absolute depth plus
/// three (orchestrator decision, 2026-10-25), not you.depth + 3: a branch
/// floor rolls for how deep it really is, where Lair:1 would otherwise roll
/// level-4 junk. D:N gives N + 3 either way. Rerolls an item the player's
/// god forbids; otherwise it is ordinary floor loot, useful or not.
static int _make_rand_item()
{
    const int level = env.absdepth0 + 1 + 3;
    for (int tries = 0; tries < DCC_REROLLS; ++tries)
    {
        const int idx = items(true, OBJ_RANDOM, OBJ_RANDOM, level);
        if (idx == NON_ITEM)
            continue;
        if (_god_forbids(env.item[idx]))
        {
            destroy_item(idx, true);
            continue;
        }
        if (!is_stackable_item(env.item[idx]))
            origin_acquired(env.item[idx], AQ_DCC_BOX);
        return idx;
    }
    return NON_ITEM;
}

/// An item of acquirement quality from the first of these classes that
/// yields one; failing all of them, a random item. Acquirement itself never
/// makes an item the player's god forbids or cannot use.
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

static bool _randart_class(object_class_type c)
{
    return c == OBJ_WEAPONS || c == OBJ_ARMOUR || c == OBJ_JEWELLERY
           || c == OBJ_STAVES || c == OBJ_TALISMANS;
}

/// The player's acquirement classes that can be randarts (those
/// make_item_randart() takes), shuffled. Acquirement's own class list
/// decides what the player can use and how often staves and talismans
/// come up; each call rolls it afresh.
static vector<object_class_type> _randart_classes()
{
    vector<object_class_type> classes = shuffled_acquirement_classes(false);
    classes.erase(remove_if(classes.begin(), classes.end(),
                            [](object_class_type c)
                            { return !_randart_class(c); }),
                  classes.end());
    return classes;
}

/**
 * An artefact of acquirement quality. Acquirement's god and usefulness
 * checks run before the randart conversion, which can add a property the
 * god forbids (an evil or hasty one) or make the item useless, so the
 * finished artefact is checked again and rerolled until it passes.
 * Unrandarts from acquirement count as they are. Final fallback, after
 * DCC_REROLLS failed tries (no randart-capable class the player can use,
 * or every roll rejected): a plain acquirement item from all the player's
 * classes. Never an item the god forbids.
 */
static int _make_randart_item()
{
    for (int tries = 0; tries < DCC_REROLLS; ++tries)
    {
        for (object_class_type cls : _randart_classes())
        {
            const int idx = acquirement_create_item(cls, AQ_DCC_BOX, true);
            if (idx == NON_ITEM)
                continue;
            item_def &item = env.item[idx];
            if (!is_artefact(item))
                make_item_randart(item, true);
            if (is_artefact(item) && !_unwanted(item))
            {
                item_set_appearance(item);
                return idx;
            }
            destroy_item(idx, true);
            break; // a fresh class order for the next try
        }
    }
    return _make_acq_item(shuffled_acquirement_classes(false));
}

/// A wand of acquirement quality. If acquirement can find no wand the
/// player can use, an acquirement item of another class instead.
static int _make_wand_item()
{
    vector<object_class_type> classes = shuffled_acquirement_classes(false);
    classes.insert(classes.begin(), OBJ_WANDS);
    return _make_acq_item(classes);
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
        keep(_make_wand_item());
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
 * Open the oldest held box onto the floor around the player.
 *
 * @return false, opening nothing, if there is no box or the item table has
 *         no room for it.
 */
static bool _open_next_box()
{
    if (!crawl_state.game_is_dcc() || !dcc_boxes_that_fit())
        return false;

    CrawlVector &boxes = you.props[DCC_BOXES_KEY].get_vector();
    const dcc_box_tier tier = _valid_tier(boxes[0].get_int());
    boxes.erase(0);
    if (boxes.empty())
        you.props.erase(DCC_BOXES_KEY);

    const vector<coord_def> cells = _drop_cells(you.pos());
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
    return true;
}

/**
 * After a sitting at the kiosk: say why boxes are left, if a jam left them,
 * and run the box-related achievements (none yet) if any box opened.
 *
 * @param opened  how many boxes opened this sitting.
 * @param jammed  whether opening stopped because a box did not fit (as
 *                opposed to the player stopping it).
 */
static void _end_sitting(int opened, bool jammed)
{
    if (jammed && dcc_box_count())
        _announce_jam(dcc_box_count());
    if (opened)
        dcc_check_achievements();
}

/// Open up to `n` boxes at once (headless scripts and tests).
static int _open_boxes_now(int n)
{
    int opened = 0;
    while (opened < n && _open_next_box())
        ++opened;
    _end_sitting(opened, true);
    return opened;
}

/// The kiosk at work: DCC_BOX_OPEN_TURNS turns a box, each box opening as
/// its turns are up (the last one as the delay finishes). A keypress, a
/// forced interrupt or the player leaving the kiosk (a floor collapse moved
/// them) stops it between boxes; opened boxes stay opened, the rest stay
/// held for the next kiosk.
class DccUnboxDelay : public Delay
{
    coord_def kiosk;
    level_id where;
    int to_open;
    int opened = 0;
    int elapsed = 0;
    bool ended = false;

    bool still_there() const
    {
        return level_id::current() == where && you.pos() == kiosk
               && dcc_at_kiosk();
    }

    void stop(const char *why)
    {
        if (ended)
            return;
        ended = true;
        const int left = dcc_box_count();
        if (left)
        {
            mprf(MSGCH_DCC, "%s %d box%s stay%s sealed for later.", why,
                 left, left == 1 ? "" : "es", left == 1 ? "s" : "");
        }
        _end_sitting(opened, false);
    }

    // A box that will not fit any more (the level filled up meanwhile) ends
    // the sitting as a jam.
    bool open_one()
    {
        if (_open_next_box())
        {
            ++opened;
            return true;
        }
        ended = true;
        _end_sitting(opened, true);
        return false;
    }

    bool invalidated() override
    {
        if (ended)
            return true;
        if (!still_there())
        {
            stop("You are not at the kiosk any more.");
            return true;
        }
        // Any key stops the kiosk between boxes, as it stops travel; the
        // key is then read as the next command.
        if (kbhit())
        {
            stop("You step back from the kiosk.");
            return true;
        }
        return false;
    }

    void tick() override
    {
        ++elapsed;
        if (elapsed % DCC_BOX_OPEN_TURNS == 0 && opened < to_open - 1)
        {
            open_one();
            return;
        }
        mprf(MSGCH_MULTITURN_ACTION, "%s",
             random_choose("The kiosk whirs and plays a drumroll.",
                           "The kiosk's lights chase each other around its "
                           "rim.",
                           "Somewhere, a recorded audience gasps."));
    }

    void finish() override
    {
        if (ended)
            return;
        if (!still_there())
        {
            stop("You are not at the kiosk any more.");
            return;
        }
        while (opened < to_open)
            if (!open_one())
                return;
        ended = true;
        _end_sitting(opened, true);
    }

public:
    DccUnboxDelay(int boxes, const coord_def &pos, const level_id &place)
        : Delay(dcc_box_open_turns(boxes) - 1), kiosk(pos), where(place),
          to_open(boxes)
    { }

    bool try_interrupt(bool /*force*/) override
    {
        stop("You stop the kiosk.");
        return true;
    }

    const char *name() const override
    {
        return "dcc_unbox";
    }
};

/**
 * Open the player's boxes at a safe-room kiosk.
 *
 * Refused unless the player stands on a kiosk cell inside a safe room.
 * Works out first how many held boxes the level has room for; if none, the
 * kiosk jams without asking. Otherwise asks about those boxes, unless
 * forced or headless. In a live game the opening is a delay of
 * dcc_box_open_turns() turns for them; headless scripts and tests open
 * them at once.
 *
 * @param force  skip the question.
 * @param live   use the live game's delay even in a headless script, so a
 *               script can drive it (dcc.run_delays() in l-dcc.cc).
 * @return what happened.
 */
dcc_open_result dcc_open_boxes(bool force, bool live)
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
        mprf(MSGCH_DCC, "The kiosk's screen reads: NO BOXES ON FILE. "
                        "Achievements pay out here; go and earn one.");
        return dcc_open_result::no_boxes;
    }

    const int fit = dcc_boxes_that_fit();
    if (!fit)
    {
        _announce_jam(boxes);
        return dcc_open_result::jammed;
    }

    const int turns = dcc_box_open_turns(fit);
    const bool headless = crawl_state.script || crawl_state.test;
    if (!force && !headless)
    {
        const string what = fit == boxes
            ? make_stringf("Open your %d loot box%s?", boxes,
                           boxes == 1 ? "" : "es")
            : make_stringf("This floor has room for %d of your %d loot "
                           "boxes. Open %s?", fit, boxes,
                           fit == 1 ? "it" : "them");
        const string prompt = make_stringf("%s It takes %d turns; any key "
                                           "stops it.", what.c_str(), turns);
        if (!yesno(prompt.c_str(), true, 'n'))
        {
            canned_msg(MSG_OK);
            return dcc_open_result::declined;
        }
    }

    if (headless && !live)
        return _open_boxes_now(fit) ? dcc_open_result::opened
                                    : dcc_open_result::jammed;

    mprf("You feed %s into the kiosk.",
         fit == 1 ? "a box" : make_stringf("%d boxes", fit).c_str());
    start_delay<DccUnboxDelay>(fit, you.pos(), level_id::current());
    return dcc_open_result::opened;
}

/// Was the move that just put the player here a deliberate step: a plain or
/// safe move command, not running, travel or autoexplore (you.running), and
/// not a blink, teleport or other move made by some other command?
/// crawl_state.prev_cmd is the command being processed (main.cc sets it
/// before process_command()). Headless scripts and tests issue no commands,
/// so there only you.running counts.
static bool _deliberate_step()
{
    if (you.running || you_are_delayed())
        return false;
    if (crawl_state.script || crawl_state.test)
        return true;
    const command_type cmd = crawl_state.prev_cmd;
    return (cmd >= CMD_MOVE_LEFT && cmd <= CMD_MOVE_DOWN_RIGHT)
           || (cmd >= CMD_SAFE_MOVE_LEFT && cmd <= CMD_SAFE_MOVE_DOWN_RIGHT);
}

/**
 * The kiosk marker's callback (dcc_kiosk_activate() in dat/dlua/dcc.lua):
 * the player has just moved onto the kiosk. A deliberate step opens the
 * boxes (asking first, as dcc_open_boxes()); anything else (travel,
 * autoexplore, running, a blink or teleport, a move during a delay) is
 * declined silently, so crossing the kiosk never interrupts anything.
 */
dcc_open_result dcc_kiosk_stepped()
{
    if (!crawl_state.game_is_dcc())
        return dcc_open_result::not_dcc;
    if (!dcc_at_kiosk())
        return dcc_open_result::no_kiosk;
    if (!_deliberate_step())
        return dcc_open_result::declined;
    return dcc_open_boxes(false);
}
