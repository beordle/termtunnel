/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */


#include <fcntl.h>
#include <stdbool.h>
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
  memcpy(tmp, data, data_size);
  tmp[data_size] = '!';
  char *result = green_encode(tmp, data_size + 1, &len_result);
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
    if (data == NULL) {
      data = (char *)malloc(2 * max_suggested_size);
    }
    static int data_size = 0;
    CHECK(nread <= max_suggested_size, "nread<=max_suggested_size");
    memcpy(data + data_size, buf->base, nread);
    data_size += nread;
    CHECK(data_size <= 2 * max_suggested_size, "data_size>=2*max_suggested_size");

    int used_data_size = process_stdin(data, data_size);
    int unused_data_size = data_size - used_data_size;
    if (unused_data_size > 0) {
      CHECK(unused_data_size < max_suggested_size, "erorr");
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
  char *ebuf = base64_encode(buf, size, &elen);

  char *tmp = (char *)malloc(elen + 1);
  tmp[0] = 'B';
  memcpy(tmp + 1, ebuf, elen);
  write_frame_to_server(tmp, elen + 1);

  free(tmp);
  free(ebuf);
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
    char *result = base64_decode(str_data + 1, data_size - 1, &result_len);
    if (result_len == 0) {
      // TODO!
      log_error("found a error base64 [%*s]\n", data_size - 1, str_data + 1);
      return 0;
    }
    agent_handle_binary(result, result_len);
    free(result);
    return 0;
  } else {
    log_error("error packet [%*s](%d)\n", data_size, str_data, data_size);
    return 0;
  }

  return 0;
}

int agent_handle_binary(char *buf, int size) {
  log_info("agent_handle_binary data (%d)\n", size);
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
  b->base = buf;
  b->len = s;
  req1->data = b;
  if (autofree) {
    int ret = uv_write(req1, (uv_stream_t *)&agent_stdout_tty, b, 1,
                       write_cb_with_free);
    // assert(ret==0);
  } else {
    int ret = uv_write(req1, (uv_stream_t *)&agent_stdout_tty, b, 1,
                       write_cb_without_free);
    // assert(ret==0);
  }
}

static void sigint_handler(int sig) {
  exit(0);
  return;
}

void agent(int argc, char** argv) {
  /** if (argc != 0) {
    // printf("argv %s\n", argv[0]);
    // 我们仍旧预期这是一个标准参数列表。因为后续我们要支持如同 busybox 一样的能力。
    // termtunnel -- rz -bye
    /*int opt = 0;
    bool p_g_confix = false;
    while ((opt = ya_getopt(argc, argv, "l:r:dvchr:g:p::")) != -1)
    {
        switch (opt)
        {
          
        }
    }
  }*/
  // 判断是否使用 oneshot 模式
  set_agent_process();
  setvbuf(stdin, NULL, _IONBF, 0);
  agent_set_stdin_noecho();
  atexit(agent_restore_stdin);
  signal(SIGINT, sigint_handler);
  int len_result;
  char *result = green_encode("MAGIC!", strlen("MAGIC!"), &len_result);
  usleep(2000);  // 防止粘连，优化显示的目的。 
  writen(STDOUT_FILENO, result, len_result);
  free(result);

  static uv_timer_t timer_watcher;
  uv_loop_t *loop = uv_default_loop();
  uv_timer_init(loop, &timer_watcher);
  uv_timer_start(&timer_watcher, agent_timer_callback, TIMEOUT_MS, REPEAT_MS);

  uv_tty_init(loop, &agent_stdin_tty, STDIN_FILENO, 1);
  uv_tty_init(loop, &agent_stdout_tty, STDOUT_FILENO, 0);
  uv_read_start((uv_stream_t *)&agent_stdin_tty, alloc_buffer,
                agent_read_stdin);

  libuv_add_vnet_notify();
  vnet_init(vnet_notify_to_libuv);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  log_info("agent evloop exit");
  exit(EXIT_SUCCESS);
}
