/**
 * @file
 * @brief Crawl Live (DCC) loot boxes: opened at the safe-room kiosk.
**/

#pragma once

#include "dcc-achieve.h" // dcc_box_tier, DCC_BOXES_KEY
#include "items.h"       // item_source_type values

// The acquirement agent for box items (acquirement_create_item(), items()).
// Above every trunk item_source_type code (AQ_WIZMODE is 200) and every god,
// so no trunk check mistakes a box item for a god gift or a wizard item.
const int AQ_DCC_BOX = 250;
COMPILE_CHECK(AQ_DCC_BOX > AQ_WIZMODE);

// Turns the kiosk takes per box opened.
const int DCC_BOX_OPEN_TURNS = 2;

// The marker property that makes a cell a kiosk (safe_rooms.des).
#define DCC_KIOSK_PROP "dcc_kiosk"

/// Why opening was refused, or that it went ahead.
enum class dcc_open_result
{
    opened,       // boxes opened (or, in a live game, the opening started)
    not_dcc,      // not a Crawl Live game
    no_kiosk,     // the player is not on a safe-room kiosk
    no_boxes,     // nothing to open
    declined,     // the player said no, or the step onto the kiosk was not a
                  // deliberate one (travel, a blink, a teleport): no question
    jammed,       // the level has no room for more items; boxes stay sealed
};

int dcc_box_item_count(dcc_box_tier tier);
int dcc_box_open_turns(int boxes);
bool dcc_at_kiosk();
int dcc_boxes_that_fit();
dcc_open_result dcc_open_boxes(bool force, bool live = false);
dcc_open_result dcc_kiosk_stepped();
int dcc_free_item_slots();
