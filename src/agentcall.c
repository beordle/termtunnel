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

// static_assert(sizeof(int)==4);
static int agentcall_service_port = 700;

typedef struct path_exchange {
  char src_path[PATH_MAX];
  char dst_path[PATH_MAX];
  // uv_async_t *exchange_notify;
  //  TODO chmod etc
} path_exchange_t;
//static_assert(sizeof(path_exchange_t)==32);

static void agentcall_server_request(void *p) {
  int sd = (int)p;
  set_vnet_socket_nodelay(sd);
  char recv_buf[READ_CHUNK_SIZE];
  int n, nwrote;
  log_info("sd: %d", sd);
  int readbytes = 0;
  // sscanf(forward_arg, "%64[^:]:%d:%64[^:]:%d", &a_host, &a_port, &b_host, &b_port);
  do {
    n = lwip_read(sd, recv_buf + readbytes, 1);
    if (n <= 0) {
      lwip_close(sd);
      log_info("error stream");
      return;
    }
    readbytes += 1;
  } while (*(recv_buf + readbytes - 1) != '\0');
  lwip_close(sd);
  return;
}


int  agentcall_server_start() {
  return vnet_listen_at(agentcall_service_port,
        agentcall_server_request, "agentcall_server_worker");
}


static void call_send_request(path_exchange_t *pe) {
  
  int nfd = vnet_tcp_connect(agentcall_service_port);
  if (nfd < 0) {
      log_debug("connect agentcall_service_port error");
    return;
  }
  //send string
  vnet_send(nfd, pe->dst_path, strlen(pe->dst_path) + 1);  // with_zero_as_split
  //read
  //send_async_t
  // 提醒到主线程
  vnet_close(nfd);
  return;
}

// 使得agent监听某个端口等等等...
int server_call_agent(void *cb, char *src_path, char *dst_path) {
  path_exchange_t *pe = (path_exchange_t*)malloc(sizeof(path_exchange_t));
  strcpy(pe->src_path, src_path);
  strcpy(pe->dst_path, dst_path);
  char* buf=(char*)malloc(1024);
  snprintf(buf, 1024, "%d:%d:%d",0,0,0);
  sys_thread_new("call_send", call_send_request, (void *)pe,
                 DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  return 0;
}
