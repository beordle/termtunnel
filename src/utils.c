/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include "utils.h"
#include "log.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

//如果仅仅 ~ICANON; ~ECHO
//那么，我们还是可以通过键盘触发SIGINT，SIGSTOP等信号。此时可以手动选择转发给远端。
//但是依据ssh，这样设置可以直接将信号转发给远程终端，从而如果我们的程序收到了键盘产生的信号的话，一定是从另一个console中手动kill过来的。
//因此我们可以对这种信号，选择直接退出。
struct termios ttystate_backup;
static bool _stdin_is_raw = false;

const char *green_encode(const char *buf, int len, int *result_len) {
  const char *prefix = "\e[32;42m";
  const int prefix_len = sizeof("\e[32;42m") - 1;
  const char *postfix = "\e[0m";
  const int postfix_len = sizeof("\e[0m") - 1;
  *result_len = prefix_len + postfix_len + len;
  char *ret = (char *)malloc(*result_len + 1);
  char *s = ret;
  memcpy(s, prefix, prefix_len);
  s += prefix_len;
  memcpy(s, buf, len);
  s += len;
  memcpy(s, postfix, postfix_len);
  s += postfix_len;
  *s = '\0';
  return ret;
}

void restore_stdin() {
  _stdin_is_raw = false;
  tcsetattr(STDIN_FILENO, TCSANOW, &ttystate_backup);
}

void *memdup(const void *src, size_t n) {
  void *dest;

  dest = malloc(n);
  if (dest == NULL) return NULL;

  return memcpy(dest, src, n);
}

bool stdin_is_raw() { return _stdin_is_raw; }

int writen(int fd, void *buf, int n) {
  int nwrite = 0;
  int left = n;
  while (left > 0) {
    if ((nwrite = write(fd, buf, left)) == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
    } else {
        left -= nwrite;
        buf += nwrite;
    }
  }
  return n;
}


void set_stdin_raw() {
  setvbuf(stdout, NULL, _IONBF, 0);
  struct termios ttystate;
  tcgetattr(STDIN_FILENO, &ttystate);
  if (!_stdin_is_raw) {
    atexit(restore_stdin);
    memcpy(&ttystate_backup, &ttystate, sizeof(struct termios));
  }
  ttystate.c_iflag |= IGNPAR;
  ttystate.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
  ttystate.c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
  ttystate.c_oflag &= ~OPOST;
  ttystate.c_cc[VMIN] = 1;  // TODO
  ttystate.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &ttystate);
  _stdin_is_raw = true;
}

void utils_counter_init(struct counter_t *c) {
  c->value = 0;
  pthread_mutex_init(&c->lock, NULL);
}

void utils_counter_increment_by(struct counter_t *c, int by) {
  pthread_mutex_lock(&c->lock);
  c->value += by;
  pthread_mutex_unlock(&c->lock);
}

void utils_counter_increment(struct counter_t *c) {
  utils_counter_increment_by(c, 1);
}

int utils_counter_get(struct counter_t *c) {
  pthread_mutex_lock(&c->lock);
  int rc = c->value;
  pthread_mutex_unlock(&c->lock);
  return rc;
}

char* safe_gethostbyname(char *host, uint16_t port) {
    log_info("gethostbyname %s:%hu", host, port);
    char portaddr[6];
    struct addrinfo *res;
    snprintf(portaddr, sizeof(portaddr), "%d", port);
    int ret = getaddrinfo((char *)host, portaddr, NULL, &res);
    if (ret == EAI_NODATA) {
      printf("gethostbyname EAI_NODATA");
      return NULL;
    } else if (ret == 0) {
      struct addrinfo *r;
      for (r = res; r != NULL; r = r->ai_next) {
        // TODO(jdz) if == ipv4 
        // int fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        struct sockaddr_in *addr;
        addr = (struct sockaddr_in *)r->ai_addr;
        char *ret = malloc(INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &(addr->sin_addr), ret, INET_ADDRSTRLEN);
        // inet_pton(AF_INET, "192.0.2.33", &(sa.sin_addr));
        freeaddrinfo(res);
        return ret;
    }
    freeaddrinfo(res);
    }
    return NULL;
}

