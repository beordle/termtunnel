/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include "state.h"
#include "utils.h"
static int state_mode;

int get_state_mode() { return state_mode; }
int set_state_mode(int mode) {
  state_mode = mode;
  return 0;
}
int set_server_process() {
  state_mode = MODE_SERVER_PROCESS;
  return 0;
}
int set_client_process() {
  state_mode = MODE_CLIENT_PROCESS;
  return 0;
}
int set_agent_process() {
  state_mode = MODE_AGENT_PROCESS;
  return 0;
}

static struct counter_t task_counter;

void set_running_task_changed(int value) {
  utils_counter_increment_by(&task_counter, value);
  return;
}

int get_running_task_count() {
  return utils_counter_get(&task_counter);
}

int termtunnel_state_init() {
  utils_counter_init(&task_counter);
  return 0;
}
