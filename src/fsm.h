/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_FSM_H
#define TERMTUNNEL_FSM_H
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

typedef struct {
  int argc;
  int argv[16];
  int cur;
} csi_info;

typedef struct {
  int state;
  int run_count;
  int processed;
  int offset;
  char *input;
  int input_cap;
  int input_size;

  int bg_colorflag;
  int fg_colorflag;

  char *output;
  int output_cap;
  int output_size;
  // ringbuf_t buf;
  csi_info *csi;

} fsm_context;

fsm_context *fsm_alloc();
void fsm_free(fsm_context *ctx);
void fsm_append_input(fsm_context *ctx, const char *input, int input_size);
void fsm_run(fsm_context *ctx);
int fsm_pop_output(fsm_context *ctx, char *dst, int max_size);

#endif
