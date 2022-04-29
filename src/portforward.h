/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_PORTFORWARD_H
#define TERMTUNNEL_PORTFORWARD_H
#include <stdint.h>
void portforward_static_remote_server_start();
int portforward_static_start(char *src_host, uint16_t src_port, char *dst_host,
                             uint16_t dst_port);
int pipe_lwip_socket_and_socket_pair(int lwip_fd, int fd);

#endif
