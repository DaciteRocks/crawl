/**
 * @file
 * @brief Crawl Live (DCC) floor bosses.
 *
 * Five floors hold a boss: D:3, D:6, D:9, D:12 and Zot:5. A boss is a
 * placed monster, not a vault: Zot:5, Vaults:$ and the Hell ends are
 * encompass maps where no secondary vault fits, and a monster at a far cell
 * never fails to place. On the player's first arrival on such a floor
 * (dcc_enter_floor()), dcc_place_boss() parses the floor's des MONS spec
 * from the table below and puts the monster on a far reachable cell
 * outside the safe room, marked with DCC_BOSS_KEY. A boss never blocks the
 * way down; killing it is optional. Kill quests never ask for it
 * (dcc_quest_kill_targets()).
 *
 * The first time the player sees a boss (seen_monster() calls
 * dcc_monster_seen()) the Announcer introduces it. A counted kill
 * (dcc_monster_killed() calls dcc_boss_killed()) gets its death line and
 * unlocks its floor's boss_* achievement, which awards the box. A boss's
 * row comes from its prop, not from the level it stands on, so a boss that
 * followed the player downstairs keeps its identity.
**/

#include "AppHdr.h"

#include "dcc-boss.h"

#include "act-iter.h" // monster_iterator
#include "dcc.h" // dcc_announce
#include "dcc-achieve.h" // dcc_unlock
#include "dcc-quest.h" // dcc_quest_spot
#include "dungeon.h" // dgn_place_monster
#include "hiscores.h" // mark_milestone
#include "level-id.h"
#include "mapdef.h" // mons_list, mons_spec
#include "message.h"
#include "monster.h"
#include "notes.h"
#include "player.h"
#include "state.h"

// How many cells dcc_place_boss() tries before it gives up.
static const int DCC_BOSS_PLACE_TRIES = 10;

static const vector<dcc_boss> _boss_table =
{
    { BRANCH_DUNGEON, 3, "the Opening Act", "ogre",
      "ogre hd:7 hp:70 name:Opening_Act n_rpl n_the col:lightred",
      "Please welcome tonight's Opening Act! Nobody bought a ticket to see "
      "it, and it knows.",
      "The Opening Act is done. The crowd barely noticed. Neither did it.",
      "boss_d3" },
    { BRANCH_DUNGEON, 6, "the Studio Enforcer", "orc knight",
      "orc knight hd:11 hp:110 name:Studio_Enforcer n_rpl n_the "
      "col:lightmagenta",
      "The Studio Enforcer is on the floor. It handles contract disputes.",
      "The Studio Enforcer has been released from its contract. Permanently.",
      "boss_d6" },
    { BRANCH_DUNGEON, 9, "the Camera Operator", "cyclops",
      "cyclops hd:15 hp:170 name:Camera_Operator n_rpl n_the col:yellow",
      "The Camera Operator wants your good side. It will settle for any "
      "side it can hit.",
      "The Camera Operator is down. We are switching to the backup lens.",
      "boss_d9" },
    { BRANCH_DUNGEON, 12, "the Season Finale", "stone giant",
      "stone giant hd:19 hp:230 name:Season_Finale n_rpl n_the col:white",
      "It's the Season Finale! Ratings are up. Your odds are not.",
      "The Season Finale is over. Renewal looks likely. For you, anyway.",
      "boss_d12" },
    { BRANCH_ZOT, 5, "the Series Finale", "golden dragon",
      "golden dragon hd:25 hp:330 name:Series_Finale n_rpl n_the "
      "col:lightcyan",
      "The Series Finale has arrived. Every show ends. Let's see whose.",
      "The Series Finale is cancelled. The network did not see that coming.",
      "boss_zot5" },
};

/// Every floor boss, in floor order.
const vector<dcc_boss> &dcc_boss_table()
{
    return _boss_table;
}

/// The boss of this floor, or nullptr if the floor has none.
const dcc_boss *dcc_boss_for(const level_id &place)
{
    for (const dcc_boss &boss : _boss_table)
        if (place.branch == boss.branch && place.depth == boss.depth)
            return &boss;
    return nullptr;
}

/// Is this monster a floor boss?
bool dcc_is_boss(const monster &mons)
{
    return mons.props.exists(DCC_BOSS_KEY);
}

/// The current level's floor boss, if it has one alive.
monster *dcc_find_boss()
{
    for (monster_iterator mi; mi; ++mi)
        if (dcc_is_boss(**mi))
            return *mi;
    return nullptr;
}

/// How many monsters on the current level carry the boss prop.
int dcc_boss_count()
{
    int count = 0;
    for (monster_iterator mi; mi; ++mi)
        if (dcc_is_boss(**mi))
            ++count;
    return count;
}

/**
 * Place the current floor's boss, if it has one and does not hold it
 * already: on a far reachable cell from where the player stands (the safe
 * room, on a stair arrival), outside vaults where the level has room;
 * on encompass maps (Zot:5), where every cell is a vault cell, inside one.
 * Either way it is at least DCC_BOSS_FAR_PERCENT of the farthest good
 * spot's walking distance away. Called on the first arrival on a
 * connected floor (dcc_enter_floor()); a boss already on the level means a
 * revisit or a rebuild, so nothing is placed.
 *
 * @return The boss placed, or nullptr.
 */
monster *dcc_place_boss()
{
    if (!crawl_state.game_is_dcc())
        return nullptr;
    const level_id place = level_id::current();
    const dcc_boss *boss = dcc_boss_for(place);
    if (!boss || dcc_find_boss())
        return nullptr;

    mons_list ml;
    const string err = ml.add_mons(boss->spec);
    if (!err.empty())
    {
        mprf(MSGCH_ERROR, "DCC boss spec for %s is bad: %s",
             place.describe().c_str(), err.c_str());
        return nullptr;
    }

    for (int i = 0; i < DCC_BOSS_PLACE_TRIES; ++i)
    {
        coord_def spot;
        // Both searches keep the distance rule. A lower percent would not
        // help: dcc_quest_spot() fails only when no good spot is reachable
        // at all (the farthest one always qualifies).
        if (!dcc_quest_spot(you.pos(), DCC_BOSS_FAR_PERCENT, spot)
            && !dcc_quest_spot(you.pos(), DCC_BOSS_FAR_PERCENT, spot, true))
        {
            break;
        }
        mons_spec ms = ml.get_monster(0);
        if (monster *m = dgn_place_monster(ms, spot, true))
        {
            m->props[DCC_BOSS_KEY] = place.describe();
            return m;
        }
    }
    dprf("DCC: could not place the boss of %s", place.describe().c_str());
    return nullptr;
}

/// The table row of a floor boss, from its DCC_BOSS_KEY prop (the floor it
/// was placed on), or nullptr if the monster is no boss.
const dcc_boss *dcc_boss_row(const monster &mons)
{
    if (!dcc_is_boss(mons))
        return nullptr;
    try
    {
        return dcc_boss_for(
            level_id::parse_level_id(mons.props[DCC_BOSS_KEY].get_string()));
    }
    catch (const bad_level_id &)
    {
        return nullptr;
    }
}

/**
 * The player sees a hostile monster for the first time (seen_monster()).
 * A floor boss not yet introduced gets the Announcer's intro line, a note
 * and a milestone; DCC_BOSS_SEEN_KEY is saved with the monster, so a
 * reload or a second sighting introduces nothing.
 */
void dcc_monster_seen(monster &mons)
{
    if (!crawl_state.game_is_dcc() || mons.props.exists(DCC_BOSS_SEEN_KEY))
        return;
    const dcc_boss *boss = dcc_boss_row(mons);
    if (!boss)
        return;
    mons.props[DCC_BOSS_SEEN_KEY] = true;
    dcc_announce(boss->intro);
    take_note(Note(NOTE_MESSAGE, 0, 0, string("Met ") + boss->name));
    mark_milestone("dcc.boss", string("met ") + boss->name + ".");
}

/**
 * A counted kill (dcc_monster_killed(), inside its dcc_unlock_batch): if the
 * monster is a floor boss, the Announcer gives its death line and the
 * floor's boss_* achievement unlocks, awarding its box. The line goes out
 * with mprf rather than dcc_announce(), so the kill's batch interrupts the
 * player once for the line and the unlock together.
 */
void dcc_boss_killed(const monster &mons)
{
    if (!crawl_state.game_is_dcc())
        return;
    const dcc_boss *boss = dcc_boss_row(mons);
    if (!boss)
        return;
    mprf(MSGCH_DCC, "%s", boss->death);
    dcc_unlock(boss->achievement);
    take_note(Note(NOTE_MESSAGE, 0, 0, string("Defeated ") + boss->name));
    mark_milestone("dcc.boss", string("defeated ") + boss->name + ".");
}
