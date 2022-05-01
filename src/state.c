/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include "state.h"
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