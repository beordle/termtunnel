/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "agentcall.h"
#include "config.h"
#include "intent.h"
#include "log.h"
#include "lwip/api.h"
#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwipopts.h"
#include "netif/etharp.h"
#include "utils.h"
#include "vnet.h"
#include "portforward.h"
static uint16_t agentcall_service_port = 300;

typedef struct thread_arg_pass_t {
  char *strbuf;
  int32_t method;
} thread_arg_pass_t;

static void agentcall_server_request(void *p) {
  int sd = (int)p;
  set_vnet_socket_nodelay(sd);
  char recv_buf[READ_CHUNK_SIZE];
  int n, nwrote;
  log_info("sd: %d", sd);
  int readbytes = 0;
  int32_t method = 0;
  if (vnet_readn(sd, &method, sizeof(int32_t)) == 0) {
    lwip_close(sd);
    return;
  }
  if (vnet_readstring(sd, recv_buf, READ_CHUNK_SIZE) == 0) {
    lwip_close(sd);
    return;
  }
  if (method == METHOD_CALL_FORWARD_STATIC) {
    char local_host[IPV4_AND_IPV6_MAX_LENGTH];
    char remote_host[IPV4_AND_IPV6_MAX_LENGTH];
    uint16_t local_port;
    uint16_t remote_port;
    sscanf(recv_buf, "%64[^:]:%hu:%64[^:]:%hu",
             &local_host, &local_port, &remote_host, &remote_port);
    log_info("agent will bind %s:%hu, write to %s:%hu",
             local_host, local_port, remote_host, remote_port);
    portforward_static_start(local_host, local_port, remote_host,
                                 remote_port);
  }
  lwip_close(sd);
  return;
}

int agentcall_server_start() {
  return vnet_listen_at(agentcall_service_port, agentcall_server_request,
                        "agentcall_server_worker");
}

static void call_send_request(thread_arg_pass_t *tmp) {
  int nfd = vnet_tcp_connect(agentcall_service_port);
  if (nfd < 0) {
    log_debug("connect agentcall_service_port error");
    return;
  }
  vnet_send(nfd, &tmp->method, sizeof(tmp->method));
  vnet_send(nfd, tmp->strbuf, strlen(tmp->strbuf) + 1);
  free(tmp->strbuf);
  vnet_close(nfd);
  return;
}

int server_call_agent(int32_t method, char *strbuf) {
  thread_arg_pass_t *tmp = malloc(sizeof(thread_arg_pass_t));
  tmp->method = method;
  tmp->strbuf = strdup(strbuf);
  sys_thread_new("call_send", call_send_request, tmp, DEFAULT_THREAD_STACKSIZE,
                 DEFAULT_THREAD_PRIO);
  return 0;
}
