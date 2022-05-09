/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include "repl.h"

#include <assert.h>
#include <ctype.h>
#include <libgen.h>  // basename
#include <math.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

#include "intent.h"
#include "log.h"
#include "repl.h"
#include "state.h"
#include "thirdparty/tinyfiledialogs/tinyfiledialogs.h"
#include "utils.h"
static int in;
static int out;
int print_command_usage(char *command_name);
bool keyboard_break = false;
int64_t ping_count = 0;

int get_repl_stdout() { return out; }

ssize_t expect_write(int fd, char *buf, size_t expect_size) {
  size_t writebytes = 0;
  while (writebytes < expect_size) {
    int wb = write(fd, buf + writebytes, expect_size - writebytes);
    if (wb < 0) {
      if (errno == EINTR) {
        keyboard_break = true;
        continue;
      }
      return wb;
    }
    writebytes += wb;
  }
  return expect_size;
}

// 一般会在 server_handle_client_packet进行处理
void send_binary(int fd, int64_t type, char *addr, int len) {
  int ret = 0;
  ret = expect_write(fd, &len, sizeof(int64_t));
  CHECK(ret == sizeof(int64_t), "ret:%d sizeof(len):%d", ret, sizeof(int64_t));
  ret = expect_write(fd, &type, sizeof(type));
  CHECK(ret == sizeof(int64_t), "ret:%d sizeof(type):%d", ret, sizeof(int64_t));
  ret = expect_write(fd, addr, len);
  CHECK(ret == len, "ret:%d len:%d", ret, len);
}

ssize_t expect_read(int fd, char *buf, size_t expect_size) {
  size_t readbytes = 0;
  while (readbytes < expect_size) {
    int rb = read(fd, buf + readbytes, expect_size - readbytes);
    if (rb < 0) {
      if (errno == EINTR) {
        keyboard_break = true;
        continue;
      }
      return rb;
    }
    if (rb == 0) {
      CHECK(readbytes == expect_size, "EOF");
    }
    readbytes += rb;
  }
  return expect_size;
}

void recv_data(int fd, int *type, char **retbuf, int64_t *sz) {
  char *buf = (char *)malloc(sizeof(int64_t));
  *sz = 0;
  int a = expect_read(fd, buf, sizeof(int64_t));
  if (a == 0) {
    log_error("expect read 0");
    free(buf);
    return;
  }
  // printf("a==sizeof(int64_t) %d %d\n",a,sizeof(int64_t));
  CHECK(a == sizeof(int64_t), "a: %d", a);
  int64_t msg_size = *((int64_t *)buf);
  // printf("recv %d",*(int64_t*)buf);

  a = expect_read(fd, buf, sizeof(int64_t));
  if (a == 0) {
    free(buf);
    return;
  }
  *type = *((int64_t *)buf);
  if (msg_size != 0) {
    buf = realloc(buf, msg_size);
  }
  a = expect_read(fd, buf, msg_size);
  CHECK(a == msg_size, "a: %d,msg_size: %lld\n", a, msg_size);
  // printf("a:%d\n",a);
  *retbuf = buf;
  *sz = msg_size;
}

size_t split(char *buffer, char *argv[], size_t argv_size) {
  char *p, *start_of_word;
  int c;
  enum states { DULL, IN_WORD, IN_STRING } state = DULL;
  size_t argc = 0;

  for (p = buffer; argc < argv_size && *p != '\0'; p++) {
    c = (unsigned char)*p;
    switch (state) {
      case DULL:
        if (isspace(c)) {
          continue;
        }

        if (c == '"') {
          state = IN_STRING;
          start_of_word = p + 1;
          continue;
        }
        state = IN_WORD;
        start_of_word = p;
        continue;

      case IN_STRING:
        if (c == '"') {
          *p = 0;
          argv[argc++] = start_of_word;
          state = DULL;
        }
        continue;

      case IN_WORD:
        if (isspace(c)) {
          *p = 0;
          argv[argc++] = start_of_word;
          state = DULL;
        }
        continue;
    }
  }

  if (state != DULL && argc < argv_size) argv[argc++] = start_of_word;

  return argc;
}

void test_split(const char *s) {
  char buf[1024];
  size_t i, argc;
  char *argv[20];

  strcpy(buf, s);
  argc = split(buf, argv, 20);
  printf("input: '%s'\n", s);
  for (i = 0; i < argc; i++) printf("[%u] '%s'\n", i, argv[i]);
}

void completion(const char *buf, linenoiseCompletions *lc) {
#ifdef DEV
  if (buf[0] == 'h') {
    linenoiseAddCompletion(lc, "hello");
    linenoiseAddCompletion(lc, "hello there");
  }
#endif
}

char *hints(const char *buf, int *color, int *bold) {
#ifdef DEV
  if (!strcasecmp(buf, "hello")) {
    *color = 35;
    *bold = 0;
    return " World";
  }
  if (!strcasecmp(buf, "h")) {
    *color = 35;
    *bold = 0;
    return "elp: Show all help";
  }

#endif
  return NULL;
}

void repl_init() {
  linenoiseSetCompletionCallback(completion);
  linenoiseSetHintsCallback(hints);
}

typedef struct {
  char *funcname;
  void *funcptr;
  char *desc;
  char *usage;
} actionfinder_t;

int hello_func(int argc, char **argv) {
  if (argc < 2) {
    printf("hello [message]\n");
    return 0;
  }
}

file_exchange_intent_t *new_file_exchange_intent(char *src, char *dst,
                                                 int trans_mode) {
  file_exchange_intent_t *tmp =
      (file_exchange_intent_t *)malloc(sizeof(file_exchange_intent_t));
  strcpy(tmp->src_path, src);
  strcpy(tmp->dst_path, dst);
  tmp->trans_mode = trans_mode;
  return tmp;
}

port_forward_intent_t *new_port_forward_intent(int forward_type, char *src_host,
                                               uint16_t src_port,
                                               char *dst_host,
                                               uint16_t dst_port) {
  port_forward_intent_t *tmp =
      (port_forward_intent_t *)malloc(sizeof(port_forward_intent_t));
  tmp->forward_type = forward_type;
  strcpy(tmp->src_host, src_host);
  strcpy(tmp->dst_host, dst_host);
  tmp->src_port = src_port;
  tmp->dst_port = dst_port;
  return tmp;
}

int portforward_func(int argc, char **argv) {
  if (argc != 5) {
    print_command_usage(argv[0]);
    return 0;
  }

  char *src_host = argv[1];
  int src_port = atoi(argv[2]);
  char *dst_host = argv[3];
  int dst_port = atoi(argv[4]);
  int forward_type;
  if (strcmp(argv[0], "remote_listen") == 0) {
    forward_type = FORWARD_STATIC_PORT_MAP_LISTEN_ON_AGENT;
  } else {
    forward_type = FORWARD_STATIC_PORT_MAP;
  }

  port_forward_intent_t *a = new_port_forward_intent(
      forward_type, src_host, src_port, dst_host, dst_port);
  send_binary(out, COMMAND_PORT_FORWARD, a, sizeof(port_forward_intent_t));
  free(a);

  int type;
  char *buf;
  int64_t size;
  recv_data(in, &type, &buf, &size);
  if (type == COMMAND_RETURN) {
    printf("%s", buf);
  } else {
    printf("type: %d", type);
  }
  free(buf);

  return 0;
}


int upload_func(int argc, char **argv) {
  char *url = (char *)malloc(PATH_MAX);
  if (argc == 1) {
    // static
    char *_url = tinyfd_openFileDialog("Upload", "", 0, NULL,
                                       "select a file to upload", false);
    if (_url == NULL) {
      free(url);
      printf("Path not exist\n");
      return 0;
    }
    strcpy(url, _url);
  } else {
    strcpy(url, argv[1]);
  }
  printf("upload %s\n", url);
  char *tmp = strdup(url);
  char *bname = basename(tmp);
  file_exchange_intent_t *a =
      new_file_exchange_intent(url, bname, TRANS_MODE_SEND_FILE);
  free(tmp);

  send_binary(out, COMMAND_FILE_EXCHANGE, a, sizeof(file_exchange_intent_t));
  free(a);
  free(url);
  return 0;
}

int download_func(int argc, char **argv) {
  if (argc == 1) {
    printf("download [remote_file]\n");
    return 0;
  }
  char *url = (char *)malloc(PATH_MAX);

  // select folder to download
  if (argc == 2) {
    // staic
    char *_url = tinyfd_selectFolderDialog("select a folder to download", ".");
    if (_url == NULL) {
      free(url);
      printf("Path not exist\n");
      return 0;
    }
    strcpy(url, _url);
  } else {
    // TODO 后续允许自行制定文件夹
    printf("download [remote_file]\n");
    return 0;
  }
  char *src = argv[1];
  printf("download %s to %s\n", src, url);
  char *tmp_pf = strdup(src);
  char *bname = basename(tmp_pf);
  file_exchange_intent_t *a =
      new_file_exchange_intent(src, url, TRANS_MODE_RECV_FILE);

  
  strcat(a->dst_path, "/"); // linux without /
  // TODO macos ok, linux is ok?
  strcat(a->dst_path, bname);
  send_binary(out, COMMAND_FILE_EXCHANGE, a, sizeof(file_exchange_intent_t));
  free(url);
  free(a);
  free(tmp_pf);
  return 0;
}

int exit_func(int argc, char **argv) { return -1; }

int command_not_found(int argc, char **argv) {
  printf("command %s not found, type help to show command list.\n", argv[0]);
  return 0;
}
int help_func(int argc, char **argv);

actionfinder_t action_table[] = {
    {"local_listen", portforward_func, "port forward bind on local host",
     "local_listen [local_host] [local_port] [remote_host] [remote_port]\n"
     "when remote_port==0, the service listen on remote_port will be a "
     "socks5+http proxy server."},
    {"remote_listen", portforward_func, "port forward bind on remote host",
     "remote_listen [remote_host] [remote_port] [local_host] [local_port]\n"
     "when local_port==0, the service listen on remote_port  will be a "
     "socks5+http proxy server."},
    {"upload", upload_func, "upload a file", "usage"},
    {"rz", upload_func, "alias upload", "usage"},
    {"download", download_func, "download a file", "usage"},
    {"sz", download_func, "alias download", "usage"},
    {"help", help_func, "view help manpage", "usage"},
    {"exit", exit_func, "exit application", "usage"},
};

int help_func(int argc, char **argv) {
  printf("List of command\n\n");
  int func_count = sizeof(action_table) / sizeof(actionfinder_t);
  for (int i = 0; i < func_count; i++) {
    printf("  Command: %-12s\n", action_table[i].funcname);
    printf("\t%s\n\n", action_table[i].desc);
  }
  return 0;
}

int print_command_usage(char *command_name) {
  int func_count = sizeof(action_table) / sizeof(actionfinder_t);
  for (int i = 0; i < func_count; i++) {
    if (strcmp(command_name, action_table[i].funcname) == 0) {
      printf("%s\n", action_table[i].usage);
    }
  }
  return 0;
}

int repl_execve(int argc, char **argv) {
  if (argc == 0) {
    return 0;
  }
  char *binname = argv[0];
  int func_count = sizeof(action_table) / sizeof(actionfinder_t);
  for (int i = 0; i < func_count; i++) {
    if (strcmp(binname, action_table[i].funcname) == 0) {
      int (*entry)(int argc, char **argv);
      entry = action_table[i].funcptr;
      return entry(argc, argv);
    }
  }
  return command_not_found(argc, argv);
}

static void handler(int sig) {
  keyboard_break = true;
  return;
}

void repl_run(int _in, int _out) {
  restore_stdin();
  char *command_history_file = ".replhistory";
  // signal(SIGINT, SIG_IGN);
  // signal(SIGQUIT, SIG_IGN);
  signal(SIGINT, handler);
  linenoiseHistoryLoad(command_history_file);
  // char clear_string[] = "\033[H\033[2J\033[3J";
  // write(STDOUT_FILENO, clear_string, sizeof(clear_string));

  char *line;
  in = _in;
  out = _out;

  while ((line = linenoise("termtunnel> ")) != NULL) {
    /* Do something with the string. */
    // printf("line[%s] %p\n",line,line);
    linenoiseHistoryAdd(line); /* Add to the history. */

    int argc;
    char *argv[20];
    argc = split(line, argv, 20);  // NOTICE  这个函数会修改原来的string
    int ret = repl_execve(argc, argv);
    if (ret < 0) {
      free(line);
      break;
    }
    free(line);
  }
  // TODO 支持环境变量设置只读，不保存history
  linenoiseHistorySave(command_history_file);

  send_binary(out, COMMAND_EXIT_REPL, NULL, 0);
}

void interact_run(int _in, int _out) {
  set_stdin_raw();
  in = _in;
  out = _out;
  char ibuf[BUFSIZ];
  fd_set fdset;
  while (true) {
    FD_ZERO(&fdset);
    FD_SET(_in, &fdset);
    FD_SET(STDIN_FILENO, &fdset);
    int ret = select(_in + 1, &fdset, NULL, NULL, NULL);
    if (ret < 0) {
      if (errno == EINTR) continue;
      CHECK(ret > 0, "select ret:%d", ret);
    }
    if (FD_ISSET(STDIN_FILENO, &fdset)) {
      int cc = read(STDIN_FILENO, ibuf, BUFSIZ);
      CHECK(cc > 0, "cc>0");
      if (cc < 0) {
        log_trace("error %s", strerror("read"));
        exit(EXIT_FAILURE);
      }
      send_binary(_out, COMMAND_TTY_PLAIN_DATA, ibuf, cc);
    }
    if (FD_ISSET(_in, &fdset)) {
      char *buf = NULL;
      int64_t size;
      int64_t type = 0;

      recv_data(_in, &type, &buf, &size);
      switch (type) {
        case COMMAND_TTY_PLAIN_DATA:  // output pty data
        {
          int writtenbytes = writen(STDOUT_FILENO, buf, size);
          CHECK(writtenbytes == size, "writtenbytes==size");
          break;
        }
        case COMMAND_CMD_EXIT: {
          // 以指定错误码退出
          CHECK(size == sizeof(int), "exitcode!=sizeof(int)");
          exit(*(int *)buf);
          break;
        }
        case COMMAND_ENTER_REPL:  // output pty data
        {
          if (buf) {
            free(buf);
          }
          return;

          break;
        }
        case COMMAND_TTY_PING: {
          ping_count++;
          break;
        }
        default: {
          CHECK(0, "repl error\n");
        }
      }
      if (buf) {
        free(buf);
      }
    }
  }
}
