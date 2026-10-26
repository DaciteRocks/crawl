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
**/

#include "AppHdr.h"

#include "dcc-boss.h"

#include "act-iter.h" // monster_iterator
#include "dcc-quest.h" // dcc_quest_spot
#include "dungeon.h" // dgn_place_monster
#include "level-id.h"
#include "mapdef.h" // mons_list, mons_spec
#include "message.h"
#include "monster.h"
#include "player.h"
#include "state.h"

// How many cells dcc_place_boss() tries before it gives up.
static const int DCC_BOSS_PLACE_TRIES = 10;

// How far out the boss stands: at least this percent of the farthest good
// spot's walking distance from the arrival.
static const int DCC_BOSS_FAR_PERCENT = 60;

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
 * on encompass maps (Zot:5), where every cell is a vault cell, anywhere
 * reachable. Called on the first arrival on a connected floor
 * (dcc_enter_floor()); a boss already on the level means a revisit or a
 * rebuild, so nothing is placed.
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
        if (!dcc_quest_spot(you.pos(), DCC_BOSS_FAR_PERCENT, spot)
            && !dcc_quest_spot(you.pos(), 0, spot, true))
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
