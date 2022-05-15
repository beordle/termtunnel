/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
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
#include "utils.h"
#include "log.h"
#include "portforward.h"
#include "vclient.h"
#include "vnet.h"
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
        return 0;  // TODO(jdz)
      } else {
        left -= nread;
        buf += nread;
      }
    }
  }
  return n;
}

int readtocharR(int fd, char *buf, int bufsize) {
  int readbytes = 0;
  char* last;
  do {
      int n = lwip_read(fd, buf + readbytes, 1);
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      if (n <= 0) {
        return 0;
      }
      readbytes += 1;
      last = buf + readbytes - 1;
    } while (strncmp(last-3 ,"\r\n\r\n", 4)!=0 && readbytes < bufsize);
    return readbytes;
}

int report_error_to_client(int fd, char *message) {
  if (message == NULL) {
        message = "(null)";
  }
  char buffer[512];
  snprintf(buffer, sizeof(buffer), "HTTP/1.1 500 Internal Server Error %s\r\n\r\n", message);
  lwip_writen(fd, buffer, strlen(buffer));
}

int http_proxy(int fd, char *prefetch_data, int prefetch_data_size) {
  uint16_t httpport = 80;
  char buffer[4096];
  char buf2[512];
  char *url;
  char host[128];
  char *h, *rest;
  uint16_t port;
  char cmd[16];
  int read_bytes = 0;
  char *header = NULL;
  // char *prefetch_data, prefetch_data_size);
  //  TODO read to HTTP1.1
  memcpy(buffer, prefetch_data, prefetch_data_size);
  //int n = lwip_read(fd, buffer + prefetch_data_size,
  //              sizeof(buffer) - prefetch_data_size);
  int n = readtocharR(fd, buffer + prefetch_data_size,
                sizeof(buffer) - prefetch_data_size);
  header = buffer;
  read_bytes = n + prefetch_data_size;
  if (n < 1) {
    if (1) {
      if (n < 0) perror("read");
      log_info("nothing read.");
    }
    return 0;
  }
  log_info("prefetch %d bytes", n);
  int i = 0;
  for (i = 0; i < 15; i++)
    if (buffer[i] && (buffer[i] != ' ')) {
      cmd[i] = (char)toupper((int)buffer[i]);
    } else {
      break;
    }
  // printf("i:[%d]\n",i);
  cmd[i] = '\0';
  // printf("cmd:[%s]\n",cmd);
  if (strcmp(cmd, "GET") && strcmp(cmd, "POST") && strcmp(cmd, "CONNECT")) {
    report_error_to_client(fd, "This command is not supported");
    lwip_close(fd);  // TODO (jdz) 遗留bug
    return 0;
  }

  if (strcmp(cmd, "CONNECT") == 0) {
    // CONNECT g.cn:443 HTTP/1.1
    url = buffer + 8;  //"CONNECT "
    h = host;
    for (; *url && (*url != ':') && (*url != '/'); url++) *(h++) = *url;
    *h = '\0';
    if (*url == ':') {
      port = strtoul(url + 1, NULL, 0);
      for (; *url != '/'; url++)
        ;
    } else {
      port = httpport;
    }
    for (rest = url; *rest && (*rest != '\n'); rest++);
    if (*rest) {
      *rest = '\0';
      rest++;
    }
  } else {
    //GET http://g.cn:80
    for (url = buffer; *url && (*url != '/'); url++);
    url += 2;
    h = host;
    for (; *url && (*url != ':') && (*url != '/'); url++) *(h++) = *url;
    *h = '\0';
    if (*url == ':') {
      port = strtoul(url + 1, NULL, 0);
      for (; *url != '/'; url++);
    } else {
      port = httpport;
    }
    for (rest = url; *rest && (*rest != '\n'); rest++);
    if (*rest) {
      *rest = '\0';
      rest++;
    }
  }
  int rfd;
  if ((rfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    log_info("socket error");
    return 0;
  }
  char* ip = safe_gethostbyname(host, port);
  if (ip == NULL) {
    log_info("safe_gethostbyname empty ip");
    report_error_to_client(fd, "empty ip");
    lwip_close(fd);
    close(rfd);
    return 0;
  }
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr(ip);
  free(ip);
  sin.sin_port = htons(port);
  if (connect(rfd, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) < 0) {
    report_error_to_client(fd, strerror(errno));
    lwip_close(fd);
    close(rfd);
    return 0;
  }
  if (strcmp(cmd, "CONNECT") != 0) {
    sprintf(buf2, "%s %s\n", cmd, url);
    if (write(rfd, buf2, strlen(buf2)) < 1) {
      lwip_close(fd);
      close(rfd);
      return 0;
    }
  } else {
    char *reply = "HTTP/1.1 200 Connection Established\r\n\r\n";
    if (lwip_writen(fd, reply, strlen(reply)) < 1) {
      lwip_close(fd);
      close(rfd);
      return 0;
    }
  }
  if (strcmp(cmd, "CONNECT") != 0) {
    int valid_bytes = read_bytes - (rest - header);
    if (valid_bytes > 0) {  // strlen(rest)
      if (write(rfd, rest, valid_bytes) < 1) {
        perror("write[b]");
        lwip_close(fd);
        close(rfd);
        return 0;
      }
    }
  }
  pipe_lwip_socket_and_socket_pair(fd, rfd);

  close(rfd);
  lwip_close(fd);
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

    char* ip = safe_gethostbyname(buf, portnum);
    if (ip == NULL) {
      log_info("safe_gethostbyname empty ip");
      close(fd);
      return -1;
    }
    log_info("connect %s:%hu",ip, portnum);
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = inet_addr(ip);
    free(ip);
    remote.sin_port = htons(portnum);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&remote, sizeof(struct sockaddr_in)) < 0) {
      log_info("connect() in app_connect");
      close(fd);
      return -1;
    }
    return fd;
  }
  return -1;
}

int socks_invitation(int fd, int *version, char *header) {
  int nread = readn(fd, header, 2);
  if (nread == 2 && header[0] != VERSION5 && header[0] != VERSION4) {
    log_info("They send us %hhX %hhX", header[0], header[1]);
    *version = 0;
    return 0;
  }
  log_info("Initial %hhX %hhX", header[0], header[1]);
  *version = header[0];
  return header[1];
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
  lwip_writen(fd, (void *)answer, ARRAY_SIZE(answer));
  char resp;
  readn(fd, (void *)&resp, sizeof(resp));
  log_info("auth %hhX", resp);
  char *username = socks5_auth_get_user(fd);
  char *password = socks5_auth_get_pass(fd);
  log_info("l: %s p: %s", username, password);
  if (strcmp(arg_username, username) == 0 &&
      strcmp(arg_password, password) == 0) {
    char answer[2] = {AUTH_VERSION, AUTH_OK};
    lwip_writen(fd, (void *)answer, ARRAY_SIZE(answer));
    free(username);
    free(password);
    return 0;
  } else {
    char answer[2] = {AUTH_VERSION, AUTH_FAIL};
    lwip_writen(fd, (void *)answer, ARRAY_SIZE(answer));
    free(username);
    free(password);
    return 1;
  }
}

int socks5_auth_noauth(int fd) {
  char answer[2] = {VERSION5, NOAUTH};
  lwip_writen(fd, (void *)answer, ARRAY_SIZE(answer));
  return 0;
}

void socks5_auth_notsupported(int fd) {
  char answer[2] = {VERSION5, NOMETHOD};
  lwip_writen(fd, (void *)answer, ARRAY_SIZE(answer));
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
  lwip_writen(fd, (void *)response, ARRAY_SIZE(response));
  lwip_writen(fd, (void *)ip, IPSIZE);
  lwip_writen(fd, (void *)&port, sizeof(port));
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
                                 unsigned short int port)
{
  char response[4] = { VERSION5, OK, RESERVED, DOMAIN };
  lwip_writen(fd, (void *)response, ARRAY_SIZE(response));
  lwip_writen(fd, (void *)&size, sizeof(size));
  lwip_writen(fd, (void *)domain, size * sizeof(char));
  lwip_writen(fd, (void *)&port, sizeof(port));
}

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
  lwip_writen(fd, (void *)resp, ARRAY_SIZE(resp));
}

void *app_thread_process(void *fd) {
  int net_fd = (int)fd;
  vnet_setsocketdefaultopt(net_fd);
  int version = 0;
  int inet_fd = -1;
  char header[255];
  char methods = socks_invitation(net_fd, &version, header);
  if (version == 0) {
    // fallback to http proxy
    log_info("fallback to http");
    http_proxy(net_fd, header, 2);
    return NULL;
  }
  switch (version) {
    case VERSION5: {
      socks5_auth(net_fd, methods);
      int command = socks5_command(net_fd);
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

int socksproxy_remote_start() {
  return vnet_listen_at(socks5_port, app_thread_process, "socksproxy");
}
