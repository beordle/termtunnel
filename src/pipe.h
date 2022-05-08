/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_PIPE_H
#define TERMTUNNEL_PIPE_H
#include <stdbool.h>
#include <stdint.h>

#include "thirdparty/queue/queue.h"
#include "thirdparty/queue/queue_internal.h"

extern queue_t *q;
extern int in_fd[2];
extern int out_fd[2];
extern void agent_write_data_to_server(char *buf, size_t s, bool autofree);
extern void send_base64binary_to_agent(const char *buf, size_t size);
void cli();
void server();
int libuv_add_vnet_notify();
extern int vnet_notify_to_libuv(char *buf, size_t size);
void comm_write_packet_to_cli(int64_t type, char *buf, size_t s);
void comm_write_static_packet_to_cli(int64_t type, char *buf, size_t s);
int push_data();
#endif
