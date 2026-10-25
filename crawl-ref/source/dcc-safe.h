/**
 * @file
 * @brief Crawl Live (DCC) safe cells: monsters that would attack the player
 *        cannot enter or be placed on a cell with FPROP_DCC_SAFE.
**/

#pragma once

#include "fprop.h"         // is_dcc_safe_cell(), for the trunk hooks
#include "movement-type.h"
#include "state.h"         // crawl_state, for the trunk hooks' guards

class monster;
struct coord_def;
struct mgen_data;

bool dcc_monster_barred_from(const monster &mon, const coord_def &pos);
bool dcc_generation_barred_at(const mgen_data &mg, const coord_def &pos);
bool dcc_translocation_barred(const monster &mon, const coord_def &pos,
                              movement_type mvflags);
