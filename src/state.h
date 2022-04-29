/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_STATE_H

#define TERMTUNNEL_STATE_H

#define STATE_MODE_INTERACT 0
#define MODE_CLIENT_PROCESS 1
#define MODE_AGENT_PROCESS 2
#define MODE_SERVER_PROCESS 4

extern int get_state_mode();

extern int set_server_process();
extern int set_client_process();
extern int set_agent_process();
#endif