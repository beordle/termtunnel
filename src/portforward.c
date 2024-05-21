/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include "portforward.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "state.h"
#include "intent.h"
#include "log.h"
#include "lwip/api.h"
#include "lwipopts.h"
#include "pipe.h"
#include "socksproxy.h"
#include "utils.h"
#include "vclient.h"
#include "vnet.h"

struct lwip_sockaddr_in {
  uint8_t sin_len;
  uint8_t sin_family;
  uint16_t sin_port;
  struct in_addr sin_addr;
#define SIN_ZERO_LEN 8
  char sin_zero[SIN_ZERO_LEN];
};




static int port_forward_static_service_port = 7000;

typedef struct port_listen {
  char host[IPV4_AND_IPV6_MAX_LENGTH];
  int local_fd;
  uint16_t port;
} port_listen_t;

static void portforward_static_server_request(void *p) {
  int sd = (int)p;
  vnet_setsocketdefaultopt(sd);
  char recv_buf[READ_CHUNK_SIZE];
  int n, nwrote;
  CHECK(sd >= 0, "sd: %d", sd);
  int readbytes = 0;
  //TODO (jdz) readn
  do {
    n = lwip_read(sd, recv_buf + readbytes, 1);
    if (n <= 0) {
      lwip_close(sd);
      log_info("error stream");
      return;
    }
    readbytes += 1;
  } while (*(recv_buf + readbytes - 1) != '\0');
  char *host = strdup(recv_buf);

  readbytes = 0;
  do {
    n = lwip_read(sd, recv_buf + readbytes, sizeof(uint16_t));
    if (n <= 0) {
      lwip_close(sd);
      return;
    }
    readbytes += n;
  } while (readbytes < sizeof(uint16_t));
  uint16_t port = ntohs(*((uint16_t *)(recv_buf)));

  log_info("target %s %hu", host, port);

  int sock;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    lwip_close(sd);
    return;
  }

  char portaddr[6];
  struct addrinfo *res;
  snprintf(portaddr, sizeof(portaddr), "%d", port);

  char* ip = safe_gethostbyname(host, port);
  if (ip == NULL) {
    close(sock);
    lwip_close(sd);
    return;
  }

  log_info("use ip %s:%hu for domain", ip, port);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
#ifdef __APPLE__
  addr.sin_len = sizeof(addr);
#endif
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  log_info("start connect %s:%hu", ip, port);
  free(ip);
  int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (ret != 0) {
    lwip_close(sd);
    close(sock);
    log_info("connect error return %d errno:%d %s", ret, errno,
             strerror(errno));
    return;
  }
  log_info("connect succ");
  pipe_lwip_socket_and_socket_pair(sd, sock);
  // free(host);
  log_info("lwip_close %d", sd);
  ret = lwip_close(sd);
  CHECK(ret == 0, "lwip_close %d", ret);
  close(sock);
  return;
}

int  portforward_static_remote_server_start() {
    return vnet_listen_at(port_forward_static_service_port,
        portforward_static_server_request, "portforward_static_server");
}

pthread_mutex_t lock;

static void loop_lwipsocket_to_socket(int *arg) {
  int lwip_fd = arg[0];
  int fd = arg[1];
  while (true) {
    char buf[READ_CHUNK_SIZE];
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(lwip_fd, &fdset);
    struct timeval tv;
    tv.tv_usec = 500;
    tv.tv_sec = 0;
    int ret = lwip_select(lwip_fd + 1, &fdset, NULL, NULL, &tv);
    if (ret < 0) {
      if (errno == EINTR) continue;
      CHECK(ret > 0, "select ret:%d", ret);
    }
    if (ret == 0) {
      continue;
    }
    log_info("select ret %d", ret);
    if (!FD_ISSET(lwip_fd, &fdset)) {
      // pass
    }
    int r = lwip_recv(lwip_fd, buf, READ_CHUNK_SIZE,
                      MSG_DONTWAIT);
    log_info("lwip_readed %d", r);
    if (r < 0) {
      close(fd);
      return;
    }
    if (r == 0) {
      close(fd);
      return;
    }
    log_info("write expect %d %d", fd, r);
    int writebytes = write(fd, buf, r);
    log_info("written %d", writebytes);
    if (writebytes < 0) {
      lwip_shutdown(lwip_fd, SHUT_RDWR);
      return;
    }
    if (writebytes == 0) {
      lwip_shutdown(lwip_fd, SHUT_RDWR);
      return;
    }
  }
  return;
}

static void loop_socket_to_lwipsocket(int *arg) {
  int lwip_fd = arg[0];
  int fd = arg[1];
  while (true) {
    char buf[READ_CHUNK_SIZE];

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    struct timeval tv;
    tv.tv_usec = 500;
    tv.tv_sec = 0;
    int ret = select(fd + 1, &fdset, NULL, NULL, &tv);
    if (ret < 0) {
      if (errno == EINTR) continue;
      if (errno == EBADF) {
        log_fatal("fd: %d", fd);
        return;  // TODO(jdz) fix
      }
      CHECK(ret > 0, "select ret:%d %s", ret, strerror(errno));
    }
    if (ret == 0) {
      continue;
    }
    log_info("select ret %d", ret);
    if (!FD_ISSET(fd, &fdset)) {
      // pass
    }
    // TODO 这中间可能被close 导致read到 无效的fd
    int r = read(fd, buf, READ_CHUNK_SIZE);  // TODO
    if (r < 0) {
      lwip_shutdown(lwip_fd, SHUT_RDWR);  // TODO(jdz) 最好去掉
      return;
    }
    if (r == 0) {
      lwip_shutdown(lwip_fd, SHUT_RDWR);  // TODO(jdz) 最好去掉
      return;
    }

    int writebytes = lwip_write(lwip_fd, buf, r);
    if (writebytes < 0) {
      close(fd);
      lwip_close(lwip_fd);
      return;
    }
    if (writebytes == 0) {
      close(fd);
      lwip_close(lwip_fd);
      return;
    }
    log_info("lwip_write %d %d", r, writebytes);
  }
  return;
}

// 就直接启动两个线程即可。无法一起select。因为一个是lwip的fd一个是真实的fd。
int pipe_lwip_socket_and_socket_pair(int lwip_fd, int fd) {
  int *array = (int *)malloc(2 * sizeof(int));
  *(array) = lwip_fd;
  *(array + 1) = fd;
  pthread_t worker2;
  pthread_create(&worker2, NULL, (void*)&loop_socket_to_lwipsocket, (void *)array);
  loop_lwipsocket_to_socket((void *)array);
  pthread_join(worker2, NULL);
  log_info("lwip ip pair %d %d", lwip_fd, fd);
  free(array);
  return 0;
}

void portforward_static_server_pipe(port_listen_t *pe) {
  log_info("connect %s", pe->host);
  int lwip_fd = vnet_tcp_connect(port_forward_static_service_port);
  vnet_send(lwip_fd, pe->host, strlen(pe->host) + 1);  // with_zero_as_split
  uint16_t tmp = htons(pe->port);
  vnet_send(lwip_fd, &tmp, sizeof(uint16_t));
  log_info("connect sent %s, do pipe", pe->host);
  pipe_lwip_socket_and_socket_pair(lwip_fd, pe->local_fd);
  close(pe->local_fd);
  free(pe);
  int ret = lwip_close(lwip_fd);
  CHECK(ret == 0, "lwip_close");
  return;
}

void portforward_transparent_server_pipe(port_listen_t *pe) {
  int lwip_fd = vnet_tcp_connect(socks5_port);
  pipe_lwip_socket_and_socket_pair(lwip_fd, pe->local_fd);
  close(pe->local_fd);
  int ret = lwip_close(lwip_fd);
  CHECK(ret == 0, "lwip_close");
  free(pe);
  return;
}

static void portforward_service_handler(port_listen_t *pe) {
  int new_sd;
  int listen_fd = pe->local_fd;
  // set_running_task_changed(1);
  while (true) {
    if ((new_sd = accept(listen_fd, NULL, NULL)) >= 0) {
      pthread_t *worker = (pthread_t *)malloc(sizeof(pthread_t));  // TODO(jdz)  free
      port_listen_t *child_pe = (port_listen_t *)malloc(sizeof(port_listen_t));
      strcpy(child_pe->host, pe->host);
      child_pe->port = pe->port;
      child_pe->local_fd = new_sd;
      if (pe->port == 0) {  // socks/http proxy
        pthread_create(worker, NULL, (void*)&portforward_transparent_server_pipe, (void *)child_pe);
      } else {
        pthread_create(worker, NULL, (void*)&portforward_static_server_pipe, (void *)child_pe);
      } 

    } else {
      log_info("abort the accept");
    }
  }
  // set_running_task_changed(-1);
  return;
}

int portforward_static_start(char *src_host, uint16_t src_port, char *dst_host,
                             uint16_t dst_port) {
  int sock;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    log_error("local socket error");
    comm_write_packet_to_cli(COMMAND_RETURN, strdup("local socket error\n"),
                             sizeof("local socket error\n"));
    return 0;
  }
  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(src_port);
  address.sin_addr.s_addr = inet_addr(src_host);

  int flag = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
    log_warn("SO_REUSEADDR error");
  }

  log_debug("listen %d", src_port);
  int ret = bind(sock, (struct sockaddr *)&address, sizeof(address));
  if (ret != 0) {
    log_error("bind local port error\n");
    if (get_state_mode() != MODE_AGENT_PROCESS) {
      // TODO(jdz) agent监听模式，cli暂时无法获取结果
      comm_write_packet_to_cli(COMMAND_RETURN, strdup("bind local port error\n"),
                              sizeof("bind local port error\n"));
    }
    goto fail;
  }
  ret = listen(sock, 20);
  if (ret != 0) {
    log_error("listen error");
    if (get_state_mode() != MODE_AGENT_PROCESS) {
       // TODO(jdz) agent监听模式，cli暂时无法获取结果
      comm_write_packet_to_cli(COMMAND_RETURN, strdup("listen error\n"),
                             sizeof("listen error\n"));
    }
    goto fail;
  }

  port_listen_t *pe = (port_listen_t *)malloc(sizeof(port_listen_t));
  strcpy(pe->host, dst_host);
  pe->port = dst_port;
  pe->local_fd = sock;
  log_debug("portforward_static_start");
  if (get_state_mode() != MODE_AGENT_PROCESS) {
    comm_write_packet_to_cli(COMMAND_RETURN, strdup("bind local port done\n"),
                           sizeof("bind local port done\n"));  
                           // TODO(jdz) agent监听模式，cli暂时无法获取结果
  }

  sys_thread_new("portforward_static", (lwip_thread_fn)portforward_service_handler, (void *)pe,
                 DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  return 0;

  fail:
  close(sock);
  return 0;
}
