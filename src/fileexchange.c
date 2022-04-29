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
static int receiver_service_port = 700;
static int sender_service_port = 701;

typedef struct path_exchange {
  char src_path[PATH_MAX];
  char dst_path[PATH_MAX];
  // uv_async_t *exchange_notify;
  //  TODO chmod etc
} path_exchange_t;

static void file_receiver_request(void *p) {
  int sd = (int)p;
  set_socket(sd);
  char recv_buf[READ_CHUNK_SIZE];
  int n, nwrote;
  log_info("sd: %d", sd);
  int readbytes = 0;

  do {
    n = lwip_read(sd, recv_buf + readbytes, 1);
    if (n <= 0) {
      lwip_close(sd);
      log_info("error stream");
      return;
    }
    readbytes += 1;
  } while (*(recv_buf + readbytes - 1) != '\0');

  char *target_file_path = recv_buf;
  log_info("target_file %s", target_file_path);
  int f = open(target_file_path, O_CREAT | O_RDWR, 0600);
  log_info("fd %d", f);
  // n = lwip_read(sd, recv_buf, RECV_BUF_SIZE)
  while (true) {
    log_info("file_receiver_request read");
    /* read a max of RECV_BUF_SIZE bytes from socket */
    if ((n = lwip_read(sd, recv_buf, READ_CHUNK_SIZE)) < 0) {
      log_error("read error");
      break;
    }
    /* break if client closed connectxion */
    if (n == 0) {
      log_error("read eof");
      break;
    }
    log_debug("write");
    int w = write(f, recv_buf, n);
    log_info("w: %d %d", w, errno);
  }
  close(f);
  log_error("virserver process_echo_request closeed!!");
  /* close connection */
  lwip_close(sd);
  return;
}

void file_receiver_start() {
  int sock, new_sd;
  struct sockaddr_in address, remote;
  int size;
  int ret;

  if ((sock = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return;
  }

  address.sin_family = AF_INET;
  address.sin_port = htons(receiver_service_port);
  address.sin_addr.s_addr = INADDR_ANY;  // inet_addr(server_ip); //
  ret = lwip_bind(sock, (struct sockaddr *)&address, sizeof(address));
  CHECK(ret == 0, "bind error");
  ret = lwip_listen(sock, 10);
  CHECK(ret >= 0, "lwip_listen error %d", ret);
  log_info("do listen");
  while (true) {
    if ((new_sd = lwip_accept(sock, (struct sockaddr *)&remote,
                              (socklen_t *)&size)) >= 0) {
      log_info("new tcp");
      sys_thread_new("file_receiver_worker", file_receiver_request,
                     (void *)new_sd, DEFAULT_THREAD_STACKSIZE,
                     DEFAULT_THREAD_PRIO);
    } else {
      log_info("abort");
    }
  }
}

static void file_sender_request(void *p) {
  int sd = (int)p;
  char recv_buf[READ_CHUNK_SIZE];
  int n, nwrote;
  log_info("sd: %d", sd);

  int readbytes = 0;
  do {
    n = lwip_read(sd, recv_buf + readbytes, 1);
    if (n <= 0) {
      lwip_close(sd);
      log_info("error stream");
      return;
    }
    readbytes += 1;
  } while (*(recv_buf + readbytes - 1) != '\0');

  char *target_file_path = recv_buf;
  log_info("target_file %s", target_file_path);
  int f = open(target_file_path, O_RDONLY);
  if (f < 0) {
    // TODO
  }
  log_info("read fd %d", f);
  // n = lwip_read(sd, recv_buf, RECV_BUF_SIZE)
  while (1) {
    log_info("file_sender_request read");
    /* read a max of RECV_BUF_SIZE bytes from socket */
    if ((n = read(f, recv_buf, READ_CHUNK_SIZE)) < 0) {
      // xil_printf("%s: error reading from socket %d, closing socket\r\n",
      // __FUNCTION__, sd);
      break;
    }
    /* break if client closed connectxion */
    if (n == 0) break;

    int w = lwip_write(sd, recv_buf, n);
  }
  close(f);
  log_error("virserver process_echo_request closeed!!");
  /* close connection */
  lwip_close(sd);
  return;
}

void file_sender_start() {
  int sock, new_sd;
  struct sockaddr_in address, remote;
  int size;
  int ret;

  if ((sock = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return;
  }

  address.sin_family = AF_INET;
  address.sin_port = htons(sender_service_port);
  address.sin_addr.s_addr = INADDR_ANY;  // inet_addr(server_ip); //
  ret = lwip_bind(sock, (struct sockaddr *)&address, sizeof(address));
  CHECK(ret == 0, "bind error");
  ret = lwip_listen(sock, 10);
  CHECK(ret >= 0, "lwip_listen error %d", ret);
  log_info("do listen");
  while (true) {
    if ((new_sd = lwip_accept(sock, (struct sockaddr *)&remote,
                              (socklen_t *)&size)) >= 0) {
      log_info("new tcp");
      sys_thread_new("file_sender_worker", file_sender_request, (void *)new_sd,
                     DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
    } else {
      log_info("abort");
    }
  }
}

static void file_send_request(path_exchange_t *pe) {
  int nfd = vnet_tcp_connect(receiver_service_port);
  log_debug("file_send_request file %s", pe->dst_path);
  vnet_send(nfd, pe->dst_path, strlen(pe->dst_path) + 1);  // with_zero_as_split
  int fd = open(pe->src_path, O_RDONLY);
  if (fd < 0) {
    log_error("open_error");
    vnet_close(nfd);
    return;
  }
  char buf[READ_CHUNK_SIZE];
  while (true) {
    int readbytes = read(fd, buf, READ_CHUNK_SIZE);
    if (readbytes < 0) {
      log_debug("readbytes<0");
    }
    if (readbytes == 0) {
      log_debug("readbytes==0");
      break;
    }
    log_debug("vnet_send");
    vnet_send(nfd, buf, readbytes);
    log_debug("vnet_send_end");
  }
  // check path is exist, file can writeable.
  vnet_close(nfd);
  close(fd);
  return;
}

#if 0
// from libuv
void exchange_notify_handler(uv_async_t *handle)
{
    log_info("exchange_notify_handler\n");
    //comm_write_packet_to_cli(COMMAND_TTY_PING, NULL, 0);
    return;
}
#endif 0

int file_send_start(char *src_path, char *dst_path) {
  path_exchange_t *pe = (path_exchange_t *)malloc(sizeof(path_exchange_t));
  strcpy(pe->src_path, src_path);
  strcpy(pe->dst_path, dst_path);
  // TODO
  // if (stat(src_path, NULL)==0){
  // pe->exchange_notify->data=(void*)-1;
  // uv_async_send(pe->exchange_notify);
  //}

  // uv_async_t* exchange_notify=(uv_async_t* )malloc(sizeof(uv_async_t));
  // uv_async_init(uv_default_loop(), exchange_notify, exchange_notify_handler);
  // pe->exchange_notify = exchange_notify;
  if (stat(src_path, NULL) == 0) {
    // pe->exchange_notify->data=(void*)-1;
    // uv_async_send(pe->exchange_notify);
  }
  sys_thread_new("file_send", file_send_request, (void *)pe,
                 DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  return 0;
}

static void file_recv_request(path_exchange_t *pe) {
  int nfd = vnet_tcp_connect(sender_service_port);
  vnet_send(nfd, pe->src_path, strlen(pe->src_path) + 1);  // with_zero_as_split
  log_info("open %s for write to recv", pe->dst_path);
  // TODO read stat info
  int fd = open(pe->dst_path, O_WRONLY | O_CREAT, 0600);  // TODO chmod
  if (fd < 0) {
    vnet_close(nfd);
    log_error("open error");
  }
  char buf[READ_CHUNK_SIZE];
  while (true) {
    int readbytes = vnet_recv(nfd, buf, READ_CHUNK_SIZE);
    if (readbytes < 0) {
      // TODO
    }
    if (readbytes == 0) {
      break;
    }
    write(fd, buf, readbytes);
  }
  // check path is exist, file can writeable.
  vnet_close(nfd);
  close(fd);
  return;
}

int file_recv_start(char *src_path, char *dst_path) {
  path_exchange_t *pe = (path_exchange_t *)malloc(sizeof(path_exchange_t));
  strcpy(pe->src_path, src_path);
  strcpy(pe->dst_path, dst_path);
  log_debug("file_recv_start");
  sys_thread_new("file_recv", file_recv_request, (void *)pe,
                 DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  return 0;
}
