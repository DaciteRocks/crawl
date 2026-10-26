/**
 * @file
 * @brief Crawl Live (DCC) game mode: shared entry points.
**/

#pragma once

// Bumped when the shape of DCC state in you.props changes. There is no save
// compatibility between DCC development phases.
#define DCC_VERSION_KEY "dcc_version"
const int DCC_STATE_VERSION = 1;

void dcc_setup_new_game();
void dcc_enable_for_script();
void dcc_announce(const string &msg);
void dcc_before_command();
