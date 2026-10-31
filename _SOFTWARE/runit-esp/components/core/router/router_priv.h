#pragma once

#include "router.h"

/* Inside the router component only. */

/** @brief Ends a command's chain: logged with the command and handed to the error sink with it as origin. */
void router_report_cmd(const err_cmd_t* cmd, err_h err);
