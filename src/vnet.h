/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_VNET_H
#define TERMTUNNEL_VNET_H
#include <stdint.h>
typedef int (*callback_t)(char *buf, size_t size);

void *vnet_init(callback_t cb);
void vnet_data_income(char *buf, size_t size);
void vnet_deinit();
int vnet_tcp_connect(uint16_t port);
int vnet_send(int s, const void *data, size_t size);
int vnet_recv(int s, const void *data, size_t size);
int vnet_listen_at(uint16_t port, void *cb,char* thread_desc);
int vnet_close(int s);
void vnet_setsocketdefaultopt(int nfd);
int vnet_readn(int fd, void *buf, int n);
int vnet_readstring(int fd, char *buf, int n);
#endif
