/**
 * @file err_sink.c
 * @brief Reporting, the end of a chain (err_sink.h, err_report in errors.h): logs it, hands it to the sink the app
 * set at boot, frees it.
 */

#include "err_sink.h"

#include <stdlib.h>

#include "err_log.h"

static err_sink_f s_sink;

void err_set_sink(err_sink_f fn) {
  s_sink = fn;
}

void err_report_ex(err_h chain, const char* log_ctx, const cmd_req_t* cmd) {
  if (chain == NULL) {
    return;
  }
  err_log_ctx(chain, log_ctx);
  if (s_sink != NULL) {
    s_sink(chain, cmd);
  }
  err_free(chain);
}

void err_boot(err_h chain) {
  if (chain != NULL) {
    err_log(chain);
    abort();
  }
}

static err_board_sink_f s_board_sink;

void err_set_board_sink(err_board_sink_f fn) {
  s_board_sink = fn;
}

void err_report_board(err_h chain, uint8_t board) {
  if (chain == NULL) {
    return;
  }
  err_log_ctx(chain, "board");
  if (s_board_sink != NULL) {
    s_board_sink(chain, board);
  }
  err_free(chain);
}

void err_report(err_h chain) {
  err_report_ex(chain, NULL, NULL);
}
