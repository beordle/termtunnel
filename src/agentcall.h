/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_AGENTCALL_H
#define TERMTUNNEL_AGENTCALL_H
#define METHOD_CALL_FORWARD_STATIC 1
int agentcall_server_start();
int server_call_agent(int32_t method, char *strbuf);
#endif
