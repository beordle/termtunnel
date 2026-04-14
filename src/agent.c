/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */


#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"
#include "log.h"
#include "pipe.h"
#include "state.h"
#include "thirdparty/base64.h"
#include "thirdparty/ya_getopt/ya_getopt.h"
#include "utils.h"
#include "uv.h"
#include "vnet.h"
#define FLUASH_QUEUE_ON_TIMER
static struct termios ttystate_backup;
uv_tty_t agent_stdout_tty;
uv_tty_t agent_stdin_tty;
static int64_t pending_send = 0;  //记录待转发的字节，用于tty 流控
int max_suggested_size = 10240;

int32_t g_oneshot_argc;
char** g_oneshot_argv;


void agent_read_stdin(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size,
                         uv_buf_t *buf) {
  if (suggested_size > max_suggested_size) {
    suggested_size = max_suggested_size;
  }
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

static void write_cb_with_free(uv_write_t *req, int status) {
  pending_send--;
  if (pending_send == 0) {
    uv_read_start((uv_stream_t *)&agent_stdin_tty, alloc_buffer,
                  agent_read_stdin);
  }
  free(((uv_buf_t *)(req->data))->base);
  free(req->data);
  free(req);
}

static void write_cb_without_free(uv_write_t *req, int status) {
  pending_send--;
  if (pending_send == 0) {
    uv_read_start((uv_stream_t *)&agent_stdin_tty, alloc_buffer,
                  agent_read_stdin);
  }
  free(req->data);
  free(req);
}

int agent_process_frame(char *data, int data_size);

void agent_timer_callback() {
#ifdef FLUASH_QUEUE_ON_TIMER
  // 如果队列没有清空，那么就提醒一下。因为async_send是一个不可靠的提醒，另外，提醒后，因为没有逻辑锁，会出现：
  // uv_aysnc_send:call queue_pop queue_push 的情况，从而导致残留
  if (!queue_empty(q)) {  // data_income_notify.data == 1){
    // clear q
    //log_info("flush");
    log_debug("agent_timer_callback useful");
    push_data();
  }
#endif
  return;
}

void agent_restore_stdin() {
  tcsetattr(STDIN_FILENO, TCSANOW, &ttystate_backup);
}

void agent_set_stdin_noecho() {
  struct termios ttystate;
  tcgetattr(STDIN_FILENO, &ttystate);
  memcpy(&ttystate_backup, &ttystate, sizeof(struct termios));

  ttystate.c_iflag |= IGNPAR;
  ttystate.c_lflag &= ~(ICANON | ECHO);

  ttystate.c_lflag &= ~ECHO;
  ttystate.c_oflag &= ~OPOST;
  ttystate.c_cc[VMIN] = 1;  // TODO
  ttystate.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
}

void write_frame_to_server(char *data, int data_size) {
  int len_result;
  char *tmp = (char *)malloc(data_size + 1);
  if (tmp == NULL) {
    log_error("malloc frame buffer failed");
    return;
  }
  memcpy(tmp, data, data_size);
  tmp[data_size] = '!';
  char *result = green_encode(tmp, data_size + 1, &len_result);
  if (result == NULL) {
    free(tmp);
    log_error("green_encode failed");
    return;
  }
  agent_write_data_to_server(result, len_result, true);
  free(tmp);
}

int process_stdin(char *data, int data_size) {
  int pre_frame_end = -1;
  for (int i = 0; i < data_size; i++) {
    if (data[i] == '!') {
      data[i] = '\0';
      agent_process_frame(data + pre_frame_end + 1, i - pre_frame_end - 1);
      pre_frame_end = i;
    }
    if (data[i] == '\n') {
      pre_frame_end = i;
    }
    if (data[i] == '\0') {
      pre_frame_end = i;
    }
  }
  int used_data_size = pre_frame_end + 1;
  return used_data_size;
}

void agent_read_stdin(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  if (nread == 0) {
    log_error("agent read_zero");
    return;
  }
  if (nread < 0) {
    if (nread != UV_EOF) {
      log_error("has some xxx");
    }
    log_error("agent read_stop");
    uv_read_stop(stream);
    return;
  } else {
    if (pending_send > TTY_WATERMARK) {
      uv_read_stop(stream);
    }

    static char *data = NULL;
    static size_t data_cap = 0;
    if (data == NULL) {
      data_cap = 2 * (size_t)max_suggested_size;
      data = (char *)malloc(data_cap);
      if (data == NULL) {
        log_error("malloc stdin buffer failed");
        if (buf->base) {
          free(buf->base);
        }
        return;
      }
    }
    static int data_size = 0;
    CHECK(nread <= max_suggested_size, "nread<=max_suggested_size");
    if ((size_t)data_size + (size_t)nread > data_cap) {
      size_t need = (size_t)data_size + (size_t)nread;
      size_t new_cap = data_cap;
      while (new_cap < need) {
        if (new_cap > SIZE_MAX / 2) {
          log_error("stdin buffer overflow risk");
          uv_read_stop(stream);
          if (buf->base) {
            free(buf->base);
          }
          return;
        }
        new_cap *= 2;
      }
      char *new_data = realloc(data, new_cap);
      if (new_data == NULL) {
        log_error("realloc stdin buffer failed");
        uv_read_stop(stream);
        if (buf->base) {
          free(buf->base);
        }
        return;
      }
      data = new_data;
      data_cap = new_cap;
    }
    memcpy(data + data_size, buf->base, nread);
    data_size += nread;

    int used_data_size = process_stdin(data, data_size);
    int unused_data_size = data_size - used_data_size;
    if (unused_data_size > 0) {
      CHECK((size_t)unused_data_size <= data_cap, "unused_data_size overflow");
      memmove(data, data + used_data_size, unused_data_size);
    }
    data_size = unused_data_size;
  }

  if (buf->base) {
    free(buf->base);
  }
}

int write_binary_to_server(const char *buf, size_t size) {
  // base64

  size_t elen = 0;
  unsigned char *ebuf =
      base64_encode((const unsigned char *)buf, size, &elen);
  if (ebuf == NULL) {
    log_error("base64_encode failed");
    return -1;
  }

  char *tmp = (char *)malloc(elen + 1);
  if (tmp == NULL) {
    free(ebuf);
    log_error("malloc base64 buffer failed");
    return -1;
  }
  tmp[0] = 'B';
  memcpy(tmp + 1, (const char *)ebuf, elen);
  write_frame_to_server(tmp, elen + 1);

  free(tmp);
  free(ebuf);
  return 0;
}
int agent_handle_binary(char *buf, int size);

int agent_process_frame(char *str_data, int data_size) {
  if (data_size == 0) {
    // TODO
    log_error("empty packet");
    return 0;
  }
  // log_trace("process_frame %*s\n", data_size, str_data);
  // simple command
  if (data_size == strlen("EXIT") && strcmp(str_data, "EXIT") == 0) {
    agent_restore_stdin();
    printf("\r");  // pretty
    log_info("exit");
    exit(EXIT_SUCCESS);
  }
  if (data_size == strlen("PING") && strcmp(str_data, "PING") == 0) {
    write_frame_to_server("PING", 4);
    return 0;
  }
  if (data_size == strlen("NOP") && strcmp(str_data, "NOP") == 0) {
    write_frame_to_server("NOP", 4);
    return 0;
  }

  // base64 type
  if (data_size > 1 && str_data[0] == 'B') {
    size_t result_len = 0;
    unsigned char *result =
        base64_decode((const unsigned char *)(str_data + 1), data_size - 1,
                      &result_len);
    if (result_len == 0) {
      // TODO!
      log_error("found a error base64 [%*s]\n", data_size - 1, str_data + 1);
      return 0;
    }
    agent_handle_binary((char *)result, result_len);
    free(result);
    return 0;
  } else {
    log_error("error packet [%*s](%d)\n", data_size, str_data, data_size);
    return 0;
  }

  return 0;
}

int agent_handle_binary(char *buf, int size) {
  // simple echo
  // block_write_binary_to_server(buf, size);
  vnet_data_income(buf, size);
  // block_write_binary_to_server(buf, size);
  return 0;
}

void agent_write_data_to_server(char *buf, size_t s, bool autofree) {
  pending_send++;
  uv_write_t *req1 = malloc(sizeof(uv_write_t));
  uv_buf_t *b = malloc(sizeof(uv_buf_t));
  if (req1 == NULL || b == NULL) {
    free(req1);
    free(b);
    if (autofree) {
      free(buf);
    }
    pending_send--;
    log_error("malloc uv write request failed");
    return;
  }
  b->base = buf;
  b->len = s;
  req1->data = b;
  if (autofree) {
    int ret = uv_write(req1, (uv_stream_t *)&agent_stdout_tty, b, 1,
                       write_cb_with_free);
    if (ret != 0) {
      pending_send--;
      free(((uv_buf_t *)(req1->data))->base);
      free(req1->data);
      free(req1);
    }
  } else {
    int ret = uv_write(req1, (uv_stream_t *)&agent_stdout_tty, b, 1,
                       write_cb_without_free);
    if (ret != 0) {
      pending_send--;
      free(req1->data);
      free(req1);
    }
  }
}

static void sigint_handler(int sig) {
  exit(0);
  return;
}

void agent(int argc, char** argv) {
  bool opt_is_repl = false;
  if (argc != 0) {
    // printf("argv %s\n", argv[0]);
    g_oneshot_argc = argc;
    g_oneshot_argv = argv;
  } else {
    opt_is_repl = true;
  }

  set_agent_process();
  setvbuf(stdin, NULL, _IONBF, 0);
  agent_set_stdin_noecho();
  atexit(agent_restore_stdin);
  signal(SIGINT, sigint_handler);
  int len_result;
  char *str_trigger;
  // 判断是否使用 oneshot 模式
  if (opt_is_repl) {
    str_trigger = "MAGIC!";
  } else {
    str_trigger = "ONESHOT!";
  }
  char *result = green_encode(str_trigger, strlen(str_trigger), &len_result);
  usleep(2000);  // 防止粘连，优化显示的目的。 
  writen(STDOUT_FILENO, result, len_result);
  free(result);

  static uv_timer_t timer_watcher;
  uv_loop_t *loop = uv_default_loop();
  int rc = uv_timer_init(loop, &timer_watcher);
  if (rc != 0) {
    log_error("uv_timer_init failed: %d", rc);
    exit(EXIT_FAILURE);
  }
  rc = uv_timer_start(&timer_watcher, agent_timer_callback, TIMEOUT_MS, REPEAT_MS);
  if (rc != 0) {
    log_error("uv_timer_start failed: %d", rc);
    exit(EXIT_FAILURE);
  }

  rc = uv_tty_init(loop, &agent_stdin_tty, STDIN_FILENO, 1);
  if (rc != 0) {
    log_error("uv_tty_init stdin failed: %d", rc);
    exit(EXIT_FAILURE);
  }
  rc = uv_tty_init(loop, &agent_stdout_tty, STDOUT_FILENO, 0);
  if (rc != 0) {
    log_error("uv_tty_init stdout failed: %d", rc);
    exit(EXIT_FAILURE);
  }
  rc = uv_read_start((uv_stream_t *)&agent_stdin_tty, alloc_buffer,
                     agent_read_stdin);
  if (rc != 0) {
    log_error("uv_read_start agent stdin failed: %d", rc);
    exit(EXIT_FAILURE);
  }

  libuv_add_vnet_notify();
  vnet_init(vnet_notify_to_libuv);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  log_info("agent evloop exit");
  exit(EXIT_SUCCESS);
}
