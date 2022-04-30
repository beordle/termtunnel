/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

// 在 termtunnel 中使用 lwip 实现一个p2p的虚拟网络
#include "lwip/api.h"
#include "lwip/sockets.h"
#include "lwipopts.h"
#include "lwip/debug.h"
#include "lwip/tcpip.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "agent.h"
#include "config.h"
#include "log.h"
#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "netif/etharp.h"
#include "pipe.h"
#include "state.h"
#include "thirdparty/queue/queue.h"
#include "utils.h"
#include "vnet.h"
#include "fileexchange.h"
#include "portforward.h"
#include "socksproxy.h"

// TODO: 都固定为相同 ip，如果 ip 不同，这里 arp 将不匹配，暂时没有去分析原因。
char *agent_ip = "192.168.1.111";
char *server_ip = "192.168.1.111";

int16_t listen_port = 700;

void set_vnet_socket_nodelay(int nfd) {
  int flags = 1;
  size_t flglen = sizeof(flags);
  lwip_setsockopt(nfd, SOL_SOCKET, TCP_NODELAY, &flags, &flglen);
}



struct lwip_sockaddr_in {
  uint8_t sin_len;
  uint8_t sin_family;
  uint16_t sin_port;
  struct in_addr sin_addr;
#define SIN_ZERO_LEN 8
  char sin_zero[SIN_ZERO_LEN];
};

int vnet_listen_at(uint16_t port, void *cb, char* thread_desc) {
  int sock, new_sd;
  struct lwip_sockaddr_in address, remote;
  int size;
  int ret;

  if ((sock = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = INADDR_ANY;  // inet_addr(server_ip); //
  ret = lwip_bind(sock, (struct sockaddr *)&address, sizeof(address));
  CHECK(ret == 0, "bind error");
  ret = lwip_listen(sock, 10);
  CHECK(ret >= 0, "lwip_listen error %d", ret);
  log_info("do listen");
  while (true) {
    if ((new_sd = lwip_accept(sock, (struct sockaddr *)&remote, (socklen_t *)&size)) >= 0) {
      log_info("new tcp");
      sys_thread_new(thread_desc, cb,
                     (void *)&new_sd, DEFAULT_THREAD_STACKSIZE,
                     DEFAULT_THREAD_PRIO);
    } else {
      log_info("abort %d %d %s", new_sd, errno, strerror(errno));
      
    }
  }
}


int vnet_tcp_connect(uint16_t port) {
  int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
  LWIP_ASSERT("s >= 0", s >= 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = AF_INET;
  addr.sin_port = lwip_htons(port);
  addr.sin_addr.s_addr = inet_addr(server_ip);
  set_vnet_socket_nodelay(s);
  /* connect */
  log_info("start connect");
  int ret = lwip_connect(s, (struct sockaddr *)&addr, sizeof(addr));
  if (ret == 0) {
    log_info("connect succ return fd %d", ret);
    return s;
  }
  log_info("connect failed");
  return ret;
}

int vnet_send(int s, const void *data, size_t size) {
  return lwip_send(s, data, size, 0);
}

int vnet_recv(int s, const void *data, size_t size) {
  return lwip_recv(s, data, size, 0);
}

int vnet_close(int s) { return lwip_close(s); }

struct netif g_netif;

struct tapif {
  struct eth_addr *ethaddr;
  /* Add whatever per-interface state that is needed here. */
  int fd;
  char *name;
  ip_addr_t ip_addr;
  ip_addr_t netmask;
  ip_addr_t gw;
};

#define IFNAME0 't'
#define IFNAME1 'p'

#ifndef TAPIF_DEBUG
#define TAPIF_DEBUG LWIP_DBG_OFF
#endif

callback_t callback;

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
  struct pbuf *q;
  char buf[1514];
  char *bufptr;
  bufptr = &buf[0];

  for (q = p; q != NULL; q = q->next) {
    /* Send the data from the pbuf to the interface, one pbuf at a
       time. The size of the data in each pbuf is kept in the ->len
       variable. */
    /* send data from(q->payload, q->len); */
    memcpy(bufptr, q->payload, q->len);
    bufptr += q->len;
  }
  CHECK(p->tot_len < 2000, "writebytes too big(%d)", p->tot_len);

  callback(buf, p->tot_len);
  return ERR_OK;
}
static struct pbuf *low_level_input(char *buf, u16_t len) {
  struct pbuf *p, *q;
  // TODO(jdz) max 1514

  /* We allocate a pbuf chain of pbufs from the pool. */
  p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

  if (p != NULL) {
    /* We iterate over the pbuf chain until we have read the entire
       packet into the pbuf. */
    char *bufptr = &buf[0];
    for (q = p; q != NULL; q = q->next) {
      /* Read enough bytes to fill this pbuf in the chain. The
         available data in the pbuf is given by the q->len
         variable. */
      /* read data into(q->payload, q->len); */
      memcpy(q->payload, bufptr, q->len);
      bufptr += q->len;
    }
    /* acknowledge that packet has been read(); */
  }
  return p;
}

err_t tapif_init(struct netif *netif) {
  struct tapif *tapif;
  char *name = NULL;
  err_t err;

  if (netif->state == NULL) {
    tapif = (struct tapif *)mem_malloc(sizeof(struct tapif));
    if (!tapif) {
      return ERR_MEM;
    }
    netif->state = tapif;
  } else {
    tapif = (struct tapif *)netif->state;
    name = tapif->name;
  }
  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;
  netif->mtu = VIR_MTU;  // hack!
  /* hardware address length */
  // netif->hwaddr_len = 6;
  netif->hwaddr_len = ETHARP_HWADDR_LEN;
  uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x07};
  if (get_state_mode() != MODE_SERVER_PROCESS) {
    mac[3] = 0x01;
  }

  mac[5] = mac[5] ^ 1;
  memcpy(netif->hwaddr, mac, 6);
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_LINK_UP |
                 NETIF_FLAG_ETHARP;  // NETIF_FLAG_POINTTOPOINT;
  return ERR_OK;
}

struct tapif tapif;
bool init_done = false;
void *vnet_init(callback_t cb) {
  if (init_done) {
    return NULL;
  }
  init_done = true;
  callback = cb;
  tcpip_init(NULL, NULL);
  memset(&tapif, 0, sizeof(tapif));
  memset(&g_netif, 0, sizeof(g_netif));

  if (get_state_mode() == MODE_SERVER_PROCESS) {
    tapif.ip_addr.addr = ipaddr_addr(server_ip);  // server
    tapif.gw.addr = ipaddr_addr(server_ip);
    log_info("server init");
  } else {
    tapif.ip_addr.addr = ipaddr_addr(agent_ip);  // agent
    tapif.gw.addr = ipaddr_addr(agent_ip);
    log_info("agent init");
  }
  tapif.netmask.addr = ipaddr_addr("0.0.0.0");  // all subnet

  netif_add(&g_netif, &tapif.ip_addr, &tapif.netmask, &tapif.gw, &tapif,
            tapif_init, tcpip_input);
  netif_set_default(&g_netif);

  netif_set_up(&g_netif);

  if (get_state_mode() == MODE_AGENT_PROCESS) {
    // pass
      sys_thread_new("file_receiver", file_receiver_start, NULL,
                   DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
      sys_thread_new("file_sender", file_sender_start, NULL,
                   DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  }
  sys_thread_new("portforward_static_server",
                   portforward_static_remote_server_start, NULL,
                   DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  sys_thread_new("socksproxy_server", socksproxy_remote_start, NULL,
                   DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);

  return &g_netif;
}

void vnet_deinit() { return; }

void vnet_data_income(char *buf, size_t size) {
  struct tapif *tapif;
  struct eth_hdr *ethhdr;
  struct pbuf *p;

  tapif = (struct tapif *)g_netif.state;

  p = low_level_input(buf, size);

  if (p == NULL) {
    log_error("tapif_input: low_level_input returned NULL\n");
    return;
  }
  ethhdr = (struct eth_hdr *)p->payload;

  switch (htons(ethhdr->type)) {
    /* IP or ARP packet? */
    case ETHTYPE_IP:
    case ETHTYPE_ARP:
      if (g_netif.input(p, &g_netif) != ERR_OK) {
        log_error("data incom error ");
        pbuf_free(p);
        p = NULL;
      }
      break;
    default:
      log_info("other packet!!!");
      pbuf_free(p);
      break;
  }
  return;
}
