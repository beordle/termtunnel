/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "portforward.h"
#include "vclient.h"
enum socks { RESERVED = 0x00, VERSION4 = 0x04, VERSION5 = 0x05 };

enum socks_auth_methods { NOAUTH = 0x00, USERPASS = 0x02, NOMETHOD = 0xff };

enum socks_auth_userpass {
  AUTH_OK = 0x00,
  AUTH_VERSION = 0x01,
  AUTH_FAIL = 0xff
};

enum socks_command { CONNECT = 0x01 };

enum socks_command_type { IP = 0x01, DOMAIN = 0x03 };

enum socks_status { OK = 0x00, FAILED = 0x05 };

void *app_thread_process(void *fd);

#define BUFSIZE 65536
#define IPSIZE 4
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define ARRAY_INIT \
  { 0 }

unsigned short int socks5_port = 1080;
int auth_type = NOAUTH;
char *arg_username;
char *arg_password;

int readn(int fd, void *buf, int n) {
  int nread, left = n;
  while (left > 0) {
    if ((nread = lwip_read(fd, buf, left)) == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
    } else {
      if (nread == 0) {
        return 0;
      } else {
        left -= nread;
        buf += nread;
      }
    }
  }
  return n;
}

int writen(int fd, void *buf, int n) {
  int nwrite, left = n;
  while (left > 0) {
    if ((nwrite = lwip_write(fd, buf, left)) == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
    } else {
      if (nwrite == n) {
        return 0;
      } else {
        left -= nwrite;
        buf += nwrite;
      }
    }
  }
  return n;
}

void app_thread_exit(int ret, int fd) {
  lwip_close(fd);
  pthread_exit((void *)&ret);
}

int app_connect(int type, void *buf, unsigned short int portnum) {
  int fd;
  struct sockaddr_in remote;
  char address[16];

  memset(address, 0, ARRAY_SIZE(address));

  if (type == IP) {
    char *ip = (char *)buf;
    snprintf(address, ARRAY_SIZE(address), "%hhu.%hhu.%hhu.%hhu", ip[0], ip[1],
             ip[2], ip[3]);
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = inet_addr(address);
    remote.sin_port = htons(portnum);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&remote, sizeof(remote)) < 0) {
      log_info("connect() in app_connect");
      close(fd);
      return -1;
    }

    return fd;
  } else if (type == DOMAIN) {
    char portaddr[6];
    struct addrinfo *res;
    snprintf(portaddr, ARRAY_SIZE(portaddr), "%d", portnum);
    log_info("getaddrinfo: %s %s", (char *)buf, portaddr);
    int ret = getaddrinfo((char *)buf, portaddr, NULL, &res);
    log_info("getaddrinfo done");
    if (ret == EAI_NODATA) {
      return -1;
    } else if (ret == 0) {
      struct addrinfo *r;
      for (r = res; r != NULL; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (fd == -1) {
          continue;
        }
        log_info("connect host");
        struct sockaddr_in *addr;
        addr = (struct sockaddr_in *)r->ai_addr;
        log_info("port %hu", ntohs(addr->sin_port));
        log_info("inet_ntoa(in_addr)sin = %s\n",
                 inet_ntoa((struct in_addr)addr->sin_addr));

        ret = connect(fd, r->ai_addr, r->ai_addrlen);
        if (ret == 0) {
          log_info("connect host succ %d", fd);
          freeaddrinfo(res);
          return fd;
        } else {
          close(fd);
        }
      }
    }
    freeaddrinfo(res);
    return -1;
  }

  return -1;
}

int socks_invitation(int fd, int *version) {
  char init[2];
  int nread = readn(fd, (void *)init, ARRAY_SIZE(init));
  if (nread == 2 && init[0] != VERSION5 && init[0] != VERSION4) {
    log_info("They send us %hhX %hhX", init[0], init[1]);
    log_info("Incompatible version!");
    app_thread_exit(0, fd);
  }
  log_info("Initial %hhX %hhX", init[0], init[1]);
  *version = init[0];
  return init[1];
}

char *socks5_auth_get_user(int fd) {
  unsigned char size;
  readn(fd, (void *)&size, sizeof(size));

  char *user = (char *)malloc(sizeof(char) * size + 1);
  readn(fd, (void *)user, (int)size);
  user[size] = 0;

  return user;
}

char *socks5_auth_get_pass(int fd) {
  unsigned char size;
  readn(fd, (void *)&size, sizeof(size));

  char *pass = (char *)malloc(sizeof(char) * size + 1);
  readn(fd, (void *)pass, (int)size);
  pass[size] = 0;

  return pass;
}

int socks5_auth_userpass(int fd) {
  char answer[2] = {VERSION5, USERPASS};
  writen(fd, (void *)answer, ARRAY_SIZE(answer));
  char resp;
  readn(fd, (void *)&resp, sizeof(resp));
  log_info("auth %hhX", resp);
  char *username = socks5_auth_get_user(fd);
  char *password = socks5_auth_get_pass(fd);
  log_info("l: %s p: %s", username, password);
  if (strcmp(arg_username, username) == 0 &&
      strcmp(arg_password, password) == 0) {
    char answer[2] = {AUTH_VERSION, AUTH_OK};
    writen(fd, (void *)answer, ARRAY_SIZE(answer));
    free(username);
    free(password);
    return 0;
  } else {
    char answer[2] = {AUTH_VERSION, AUTH_FAIL};
    writen(fd, (void *)answer, ARRAY_SIZE(answer));
    free(username);
    free(password);
    return 1;
  }
}

int socks5_auth_noauth(int fd) {
  char answer[2] = {VERSION5, NOAUTH};
  writen(fd, (void *)answer, ARRAY_SIZE(answer));
  return 0;
}

void socks5_auth_notsupported(int fd) {
  char answer[2] = {VERSION5, NOMETHOD};
  writen(fd, (void *)answer, ARRAY_SIZE(answer));
}

void socks5_auth(int fd, int methods_count) {
  int supported = 0;
  int num = methods_count;
  for (int i = 0; i < num; i++) {
    char type;
    readn(fd, (void *)&type, 1);
    log_info("Method AUTH %hhX", type);
    if (type == auth_type) {
      supported = 1;
    }
  }
  if (supported == 0) {
    socks5_auth_notsupported(fd);
    app_thread_exit(1, fd);
  }
  int ret = 0;
  switch (auth_type) {
    case NOAUTH:
      ret = socks5_auth_noauth(fd);
      break;
    case USERPASS:
      ret = socks5_auth_userpass(fd);
      break;
  }
  if (ret == 0) {
    return;
  } else {
    app_thread_exit(1, fd);
  }
}

int socks5_command(int fd) {
  char command[4];
  readn(fd, (void *)command, ARRAY_SIZE(command));
  log_info("Command %hhX %hhX %hhX %hhX", command[0], command[1], command[2],
           command[3]);
  return command[3];
}

unsigned short int socks_read_port(int fd) {
  unsigned short int p;
  readn(fd, (void *)&p, sizeof(p));
  log_info("Port %hu", ntohs(p));
  return p;
}

char *socks_ip_read(int fd) {
  char *ip = (char *)malloc(sizeof(char) * IPSIZE);
  readn(fd, (void *)ip, IPSIZE);
  log_info("IP %hhu.%hhu.%hhu.%hhu", ip[0], ip[1], ip[2], ip[3]);
  return ip;
}

void socks5_ip_send_response(int fd, char *ip, unsigned short int port) {
  char response[4] = {VERSION5, OK, RESERVED, IP};
  writen(fd, (void *)response, ARRAY_SIZE(response));
  writen(fd, (void *)ip, IPSIZE);
  writen(fd, (void *)&port, sizeof(port));
}

char *socks5_domain_read(int fd, unsigned char *size) {
  unsigned char s;
  readn(fd, (void *)&s, sizeof(s));
  char *address = (char *)malloc((sizeof(char) * s) + 1);
  readn(fd, (void *)address, (int)s);
  address[s] = 0;
  log_info("Address %s", address);
  *size = s;
  return address;
}

/*
void socks5_ip_send_response(int fd, char *ip, unsigned short int port)
{
        char response[4] = { VERSION5, OK, RESERVED, IP };
        writen(fd, (void *)response, ARRAY_SIZE(response));
        writen(fd, (void *)ip, IPSIZE);
        writen(fd, (void *)&port, sizeof(port));
}
*/

void socks5_domain_send_response(int fd, char *domain, unsigned char size,
                                 unsigned short int port) {
  char response[4] = {VERSION5, OK, RESERVED, DOMAIN};
  writen(fd, (void *)response, ARRAY_SIZE(response));
  // log_info("size %hu",size);
  // log_info("sizeof size %d",sizeof(size));
  // writen(fd, "\0\0\0\0\0\0\0\0\0\0", IPSIZE);
  writen(fd, (void *)&size, sizeof(size));
  writen(fd, (void *)domain, size * sizeof(char));
  writen(fd, (void *)&port, sizeof(port));
}

/*
void socks5_domain_send_response(int fd, char *domain, unsigned char size,
                                 unsigned short int port)
{
        char response[4] = { VERSION5, OK, RESERVED, IP };
        writen(fd, (void *)response, ARRAY_SIZE(response));
        writen(fd, "1.1.1.1.1", IPSIZE);
        writen(fd, (void *)&port, sizeof(port));
}
*/
int socks4_is_4a(char *ip) {
  return (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] != 0);
}

int socks4_read_nstring(int fd, char *buf, int size) {
  char sym = 0;
  int nread = 0;
  int i = 0;

  while (i < size) {
    nread = lwip_recv(fd, &sym, sizeof(char), 0);

    if (nread <= 0) {
      break;
    } else {
      buf[i] = sym;
      i++;
    }

    if (sym == 0) {
      break;
    }
  }

  return i;
}

void socks4_send_response(int fd, int status) {
  char resp[8] = {0x00, (char)status, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  writen(fd, (void *)resp, ARRAY_SIZE(resp));
}

void *app_thread_process(void *fd) {
  log_info("new socks client xx ");
  int net_fd = *(int *)fd;
  int version = 0;
  int inet_fd = -1;
  log_info("new socks client xx %d", net_fd);
  char methods = socks_invitation(net_fd, &version);
  log_info("new socks client 2 ");
  switch (version) {
    case VERSION5: {
      socks5_auth(net_fd, methods);
      int command = socks5_command(net_fd);
      log_info("new socks client 3 ");
      if (command == IP) {
        char *ip = socks_ip_read(net_fd);
        unsigned short int p = socks_read_port(net_fd);
        inet_fd = app_connect(IP, (void *)ip, ntohs(p));
        if (inet_fd == -1) {
          app_thread_exit(1, net_fd);
        }
        socks5_ip_send_response(net_fd, ip, p);
        free(ip);
        break;
      } else if (command == DOMAIN) {
        unsigned char size;
        char *address = socks5_domain_read(net_fd, &size);
        unsigned short int p = socks_read_port(net_fd);
        inet_fd = app_connect(DOMAIN, (void *)address, ntohs(p));
        if (inet_fd == -1) {
          app_thread_exit(1, net_fd);
        }
        socks5_domain_send_response(net_fd, address, size, p);
        log_info("domain sent");
        free(address);
        break;
      } else {
        app_thread_exit(1, net_fd);
      }
    }
    case VERSION4: {
      if (methods == 1) {
        char ident[255];
        unsigned short int p = socks_read_port(net_fd);
        char *ip = socks_ip_read(net_fd);
        socks4_read_nstring(net_fd, ident, sizeof(ident));

        if (socks4_is_4a(ip)) {
          char domain[255];
          socks4_read_nstring(net_fd, domain, sizeof(domain));
          log_info("Socks4A: ident:%s; domain:%s;", ident, domain);
          inet_fd = app_connect(DOMAIN, (void *)domain, ntohs(p));
        } else {
          log_info("Socks4: connect by ip & port");
          inet_fd = app_connect(IP, (void *)ip, ntohs(p));
        }

        if (inet_fd != -1) {
          socks4_send_response(net_fd, 0x5a);
        } else {
          socks4_send_response(net_fd, 0x5b);
          free(ip);
          app_thread_exit(1, net_fd);
        }

        free(ip);
      } else {
        log_info("Unsupported mode");
      }
      break;
    }
  }
  pipe_lwip_socket_and_socket_pair(net_fd, inet_fd);
  close(inet_fd);
  app_thread_exit(0, net_fd);

  return NULL;
}

struct lwip_sockaddr_in {
  uint8_t sin_len;
  uint8_t sin_family;
  uint16_t sin_port;
  uint32_t sin_addr;
#define SIN_ZERO_LEN 8
  char sin_zero[SIN_ZERO_LEN];
};

int socksproxy_remote_start() {
  log_info("4444");
  int sock_fd, net_fd;
  int optval = 1;
  struct lwip_sockaddr_in local, remote;
  socklen_t remotelen;
  if ((sock_fd = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    log_info("socket()");
    return -1;
  }
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_addr = htonl(INADDR_ANY);
  local.sin_port = htons(socks5_port);

  if (lwip_bind(sock_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
    log_info("bind()");
    return -1;
  }

  if (lwip_listen(sock_fd, 25) < 0) {
    log_info("listen()");
    return -1;
  }

  remotelen = sizeof(remote);
  memset(&remote, 0, sizeof(remote));

  log_info("Listening port %d...", socks5_port);

  pthread_t worker;
  while (true) {
    if ((net_fd = lwip_accept(sock_fd, (struct sockaddr *)&remote,
                              &remotelen)) < 0) {
      log_info("accept()");
      // exit(1);
    }
    log_info("new socks client");
    if (pthread_create(&worker, NULL, app_thread_process, (void *)&net_fd) ==
        0) {
      log_info("pthread_create() succ");
      pthread_detach(worker);
    } else {
      log_info("pthread_create()");
    }
  }
  return 0;
}

//       pipe                               pipe
// connect <-> lwip_server  lwip_client <- bind_listen
