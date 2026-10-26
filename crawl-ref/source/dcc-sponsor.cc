/**
 * @file
 * @brief Crawl Live (DCC) sponsors.
 *
 * Sponsors are gods. D:3's safe room holds a booth of three altars
 * (dcc-room.cc builds and settles it); on the first arrival the Announcer
 * names them (dcc_sponsor_intro(), from dcc_enter_floor()). Joining a god
 * while standing on a booth altar (join_religion() calls dcc_god_joined())
 * is the signing: the Announcer announces the deal and the player gets one
 * Gold box, once per game. Joining anywhere else, or a second booth god
 * later, pays nothing beyond the ordinary `signed` achievement. The gods
 * keep their DCSS names and rules; only the Announcer calls them sponsors.
**/

#include "AppHdr.h"

#include "dcc-sponsor.h"

#include "branch-type.h"
#include "dcc.h" // dcc_announce
#include "dcc-achieve.h" // dcc_award_box, dcc_check_achievements
#include "dcc-room.h" // dcc_sponsor_altars
#include "dcc-safe.h" // is_dcc_safe_cell
#include "english.h" // number_in_words
#include "env.h"
#include "hiscores.h" // mark_milestone
#include "level-id.h"
#include "mapmark.h"
#include "notes.h"
#include "player.h"
#include "religion.h" // god_name
#include "stringutil.h" // make_stringf, comma_separated_line
#include "terrain.h" // feat_is_altar

/// Is the player standing on one of the booth's altars?
bool dcc_at_sponsor_booth()
{
    return crawl_state.game_is_dcc()
           && is_dcc_safe_cell(you.pos())
           && env.markers.property_at(you.pos(), MAT_ANY, DCC_SPONSOR_PROP)
              == "1"
           && feat_is_altar(env.grid(you.pos()));
}

/// The god the player signed with at a booth, or GOD_NO_GOD.
god_type dcc_sponsor_god()
{
    if (!you.props.exists(DCC_SPONSOR_GOD_KEY))
        return GOD_NO_GOD;
    return static_cast<god_type>(you.props[DCC_SPONSOR_GOD_KEY].get_int());
}

/**
 * The player has just joined a god (the last line of join_religion()).
 * From a booth altar, and only the first time, that is a signing: the
 * Announcer's line, a Gold box, a note and a milestone. Every join checks
 * the achievements at once, so `signed` unlocks now rather than next turn.
 */
void dcc_god_joined(god_type god)
{
    if (!crawl_state.game_is_dcc())
        return;

    dcc_unlock_batch batch;
    if (dcc_at_sponsor_booth() && !you.props.exists(DCC_SPONSOR_GOD_KEY))
    {
        you.props[DCC_SPONSOR_GOD_KEY].get_int() = god;
        const string name = god_name(god);
        dcc_announce(make_stringf("%s has signed you. Welcome to the family; "
                                  "legal says the contract is binding. A "
                                  "Gold box for the signature.",
                                  name.c_str()));
        dcc_award_box(DCC_BOX_GOLD);
        take_note(Note(NOTE_MESSAGE, 0, 0, "Signed with sponsor " + name));
        mark_milestone("dcc.sponsor", "signed with " + name + ".");
    }
    dcc_check_achievements();
}

/**
 * The first arrival on D:3 (dcc_enter_floor(), after the booth is settled):
 * the Announcer names the booth's sponsors, or says they passed when the
 * booth holds no altar (a player no temple god will take).
 */
void dcc_sponsor_intro()
{
    if (!crawl_state.game_is_dcc()
        || level_id::current() != level_id(BRANCH_DUNGEON, 3))
    {
        return;
    }

    vector<string> names;
    for (const auto &altar : dcc_sponsor_altars())
        names.push_back(god_name(altar.second));

    if (names.empty())
    {
        dcc_announce("The sponsors looked at your file and passed.");
        return;
    }

    const string list = comma_separated_line(names.begin(), names.end());
    if (names.size() == 1)
    {
        dcc_announce(make_stringf("A sponsor has a booth on this floor: %s. "
                                  "Sign at its altar and the studio throws "
                                  "in a Gold box.", list.c_str()));
    }
    else
    {
        dcc_announce(make_stringf("%s sponsors have booths on this floor: "
                                  "%s. Sign with one at its altar and the "
                                  "studio throws in a Gold box.",
                                  uppercase_first(number_in_words(
                                      names.size())).c_str(),
                                  list.c_str()));
    }
}
