/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include "pipe.h"

#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <uv.h>

#include "agent.h"
#include "agentcall.h"
#include "config.h"
#include "fileexchange.h"
#include "fsm.h"
#include "intent.h"
#include "log.h"
#include "portforward.h"
#include "pty.h"
#include "repl.h"
#include "state.h"
#include "thirdparty/base64.h"
#include "thirdparty/queue/queue.h"
#include "thirdparty/queue/queue_internal.h"
#include "thirdparty/setproctitle.h"
#include "utils.h"
#include "vnet.h"
static uv_pipe_t in_pipe;
static uv_pipe_t out_pipe;

uv_async_t *async_process_exit;
static uv_async_t data_income_notify;
//#define FLUASH_QUEUE_ON_TIMER
void send_base64binary_to_agent(const char *buf, size_t size);

bool server_see_agent_is_repl = false;
void server_handle_agent_data(char *buf, int size);
fsm_context *global_fsm_context;
static int64_t pending_send = 0;  //记录待转发的字节，用于tty 流控
bool exiting = false;

int in_fd[2];
int out_fd[2];

bool first = true;

uv_tty_t tty;

void find_a_packet(char *buf, ssize_t len);

uv_pipe_t *write_client_pipe;
void server_handle_green_packet(char *buf, int size);
static void common_read_tty(uv_stream_t *stream, ssize_t nread,
                            const uv_buf_t *buf);
void server_handle_client_packet(int64_t type, char *buf, ssize_t len);

typedef struct {
  char *buf;
  size_t len;
} frame_data;

int8_t queue_get_frame_data(queue_t *q, frame_data **e) {
  return queue_get_internal(q, (void **)e, NULL, NULL, NULL);
};
DEFINE_Q_DESTROY(queue_destroy_complete_frame_data, frame_data)

void handle_green_data(char *buf, int size);

extern void agent_read_stdin(uv_stream_t *stream, ssize_t nread,
                             const uv_buf_t *buf);

#include "vnet.h"

void free_frame_data(frame_data *f) {
  if (f->buf != NULL) free(f->buf);
  free(f);
}

queue_t *q;

// from libuv
void uvloop_process_income(uv_async_t *handle) {
  // log_info("process income queue\n");
  frame_data *f;
  int dry = handle->data;
  if (!dry) {
    return;
  }
  queue_lock_internal(q);

  while (!queue_empty_internal(q)) {
    int ret = queue_get_frame_data(q, &f);
    if (ret != 0) {
      log_info("queue pass");
      break;
    }

    if (get_state_mode() == MODE_SERVER_PROCESS) {
      // ERROR !!! 需notify到主循环··
      //  如果阻塞了，就丢了算了。
      // ERROR
      // comm_write_packet_to_cli(COMMAND_TTY_PING, NULL, 0);
      if (!server_see_agent_is_repl) {
        return;
      }
      send_base64binary_to_agent(f->buf, f->len);
    } else {
      // agent
      //  同理，如果阻塞，就丢了。
      block_write_binary_to_server(f->buf, f->len);
    }
    free_frame_data(f);
  }
  handle->data = 0;
  queue_unlock_internal(q);
  // int r = uv_async_send(&data_income_notify);
  return;
}

int vnet_notify_to_libuv(char *buf, size_t size) {
  frame_data *f = (frame_data *)malloc(sizeof(frame_data));
  f->buf = memdup(buf, size);
  f->len = size;
  queue_put(q, f);
  log_info("add queue");
  data_income_notify.data = 1;  // set dry
  int r = uv_async_send(&data_income_notify);
  return 0;
}

void TERMTUNNEL_uv_prepare_cb(uv_prepare_t *handle) {
  if (!queue_empty(q)) {  // data_income_notify.data == 1){
    // clear q
    log_info("do flush");
    uvloop_process_income(NULL);
    // int r = uv_async_send(&data_income_notify);
  }
  return;
}

int libuv_add_vnet_notify() {
  static bool added = false;
  if (added) {
    return 0;
  }
  added = true;
  data_income_notify.data = 1;
  int r = uv_async_init(uv_default_loop(), &data_income_notify,
                        uvloop_process_income);
  log_info("libuv_add_vnet_notify %d", r);

  return 0;
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size,
                         uv_buf_t *buf) {
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

static void tty_write_cb_with_free(uv_write_t *req, int status) {
  /* Logic which handles the write result */
  // printf("writeback done.\n");

  // printf("%d\n",pending_send);
  // printf("%s\n",((uv_buf_t*)(req->data))->base);
  free(((uv_buf_t *)(req->data))->base);
  free(req->data);
  free(req);
}

static void write_cb_with_free(uv_write_t *req, int status) {
  pending_send--;
  if (pending_send == 0) {
    uv_read_start((uv_stream_t *)&tty, alloc_buffer, common_read_tty);
  }
  free(((uv_buf_t *)(req->data))->base);
  free(req->data);
  free(req);
}

static void write_cb_without_free(uv_write_t *req, int status) {
  pending_send--;
  if (pending_send == 0) {
    uv_read_start((uv_stream_t *)&tty, alloc_buffer, common_read_tty);
  }
  free(req->data);
  free(req);
}

static void comm_write_data_to_cli(char *buf, size_t s, bool autofree) {
  pending_send++;
  if (pending_send > TTY_WATERMARK) {
    uv_read_stop(&tty);
  }
  uv_write_t *req1 = malloc(sizeof(uv_write_t));
  uv_buf_t *b = malloc(sizeof(uv_buf_t));
  b->base = buf;
  b->len = s;
  req1->data = b;
  if (autofree) {
    int ret = uv_write(req1, (uv_stream_t *)write_client_pipe, b, 1,
                       write_cb_with_free);
    // assert(ret==0);
  } else {
    int ret = uv_write(req1, (uv_stream_t *)write_client_pipe, b, 1,
                       write_cb_without_free);
    // assert(ret==0);
  }
}

void comm_write_static_packet_to_cli(int64_t type, char *buf, size_t s) {
  int64_t *size = (int64_t *)malloc(sizeof(int64_t));  //可能不行
  *size = s;
  comm_write_data_to_cli(size, sizeof(int64_t), true);
  int64_t *ptr_type = (int64_t *)malloc(sizeof(int64_t));
  *ptr_type = type;
  comm_write_data_to_cli(ptr_type, sizeof(int64_t), true);
  comm_write_data_to_cli(buf, s, false);
}

void comm_write_packet_to_cli(int64_t type, char *buf, size_t s) {
  int64_t *size = (int64_t *)malloc(sizeof(int64_t));  //可能不行
  *size = s;
  comm_write_data_to_cli(size, sizeof(int64_t), true);
  int64_t *ptr_type = (int64_t *)malloc(sizeof(int64_t));
  *ptr_type = type;
  comm_write_data_to_cli(ptr_type, sizeof(int64_t), true);
  comm_write_data_to_cli(buf, s, true);
}

ssize_t parser(char *buf, ssize_t nread) {
  CHECK(nread > 0, "nread > 0");
  // state machine
  static int expect_content = 0;
  static int expect_content_len = 0;
  // history buf

  static int queue_size = 0;
  static int queue_valid_size = 0;

  static char *queue = NULL;
  if (queue == NULL) {
    queue = (char *)malloc(100);
    queue_size = 100;
  }
  if (queue_size < nread + queue_valid_size) {
    int new_size = 2 * (queue_valid_size + nread);
    queue = realloc(queue, new_size);
    queue_size = new_size;
  }

  memcpy(queue + queue_valid_size, buf, nread);  // append
  queue_valid_size += nread;
  int ptr = 0;
  while (ptr < queue_valid_size) {
    if (expect_content) {
      CHECK(expect_content_len != 0, "expect_content_len!=0");
      //是否足够
      if ((queue_valid_size - ptr) < expect_content_len) {
        break;
      }
      find_a_packet(queue + ptr, expect_content_len);
      expect_content = 0;
      // buff and type
      ptr += expect_content_len;
    } else {
      if ((queue_valid_size - ptr) < sizeof(int64_t)) {
        break;
      }
      expect_content_len =
          (*((int64_t *)(queue + ptr))) + sizeof(int64_t);  // add type
      expect_content = 1;
      ptr += sizeof(int64_t);
    }
  }
  if (queue_valid_size - ptr != 0) {
    memmove(queue, queue + ptr, queue_valid_size - ptr);  // append
  }
  queue_valid_size = queue_valid_size - ptr;
  return 0;
}

static void common_read_data_from_cli(uv_stream_t *stream, ssize_t nread,
                                      const uv_buf_t *buf) {
  if (nread == 0) {
    return;
  }
  if (nread < 0) {
    if (nread != UV_EOF) {
      CHECK(nread == UV_EOF, "read_cb");
    }
    // free(buf);
    uv_read_stop(stream);
    return;
  } else if (nread > 0) {
    parser(buf->base, nread);
  }

  if (buf->base) {
    free(buf->base);
  }
}

// 快速、及时发送给 console
void send_tty_to_client(char *buf, int nread) {
  // 这里的拆分是有原因的，首先，如果生产的速度单次过快，即TTY_WATERMARK过大从而一次写入几万字节，cli标准输出的终端也会来不及处理从而阻塞在write中，此时我们没有办法及时接收对于键盘的响应（尤其是ctrl+c，此时ctrl+c已经不会由本地tty解释为中断，而是等待写入server持有的外部程序的tty），
  int rest = nread;
  int block = 512;  // TODO magic number
  while (rest > 0) {
    int will_send_block_size = 0;
    if (block < rest) {
      will_send_block_size = block;
    } else {
      will_send_block_size = rest;
    }
    char *s = (char *)malloc(will_send_block_size);
    memcpy(s, buf + (nread - rest), will_send_block_size);
    comm_write_packet_to_cli(COMMAND_TTY_PLAIN_DATA, s, will_send_block_size);
    rest -= will_send_block_size;
  }
}

static void common_read_tty(uv_stream_t *stream, ssize_t nread,
                            const uv_buf_t *buf) {
  if (nread == 0) {
    return;
  }
  if (nread < 0) {
    if (nread != UV_EOF) {
      // printf("[%s]\n",uv_strerror(nread));
      if (nread == UV_EIO) {
        uv_read_stop(stream);
      }
      // CHECK(nread == UV_EOF, "read_cb %s",uv_strerror(nread));
    }

    free(buf->base);
    uv_read_stop(stream);
    return;
  } else {
    if (!server_see_agent_is_repl) {
      send_tty_to_client(buf->base, nread);
    }
    fsm_append_input(global_fsm_context, buf->base, nread);
    fsm_run(global_fsm_context);
    char *dst = (char *)malloc(5 * VIR_MTU);
    int sz = 0;
    while (true) {
      sz = fsm_pop_output(global_fsm_context, dst, 5 * VIR_MTU);
      if (sz > 0) {
        server_handle_green_packet(dst, sz);
      } else {
        break;
      }
    }
    free(dst);
  }

  if (buf->base) {
    free(buf->base);
  }
  return;
}

void find_a_packet(char *buf, ssize_t len) {
  int64_t type = *((int64_t *)buf);
  server_handle_client_packet(type, buf + sizeof(int64_t),
                              len - sizeof(int64_t));
}

void timer_callback() {
  // 如果队列没有清空，那么就提醒一下。因为async_send是一个不可靠的提醒，另外，提醒后，因为没有逻辑锁，会出现：
  // uv_aysnc_send:call queue_pop queue_push 的情况，从而导致残留
#ifdef FLUASH_QUEUE_ON_TIMER
  if (!queue_empty(q)) {  // data_income_notify.data == 1){
    // clear q
    log_info("flush");
    int r = uv_async_send(&data_income_notify);
  }
#endif

  if (exiting) {
    // TODO (jdz）实际上设置exiting的时候，有没有写入成功的可能，因此，最好来说，我们要握手退出)
    exit(EXIT_SUCCESS);
  }
}

void cli_loop(int in, int out, int argc, const char *argv[]) {
  repl_init();

  // 不要贸然进入模式
  while (true) {
    interact_run(in, out);
    kill(getpid(), SIGWINCH);
    repl_run(in, out);
    kill(getpid(), SIGWINCH);
  }
}

void cli(int argc, const char *argv[], pid_t child_pid) {
  set_client_process();
  // Close write end
  close(in_fd[1]);
  close(out_fd[0]);

  cli_loop(in_fd[0], out_fd[1], argc, argv);
  close(in_fd[0]);
  close(out_fd[1]);
  kill(child_pid, SIGTERM);
  exit(EXIT_SUCCESS);
}

static int pty_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void background_loop(int in, int out, int argc, const char *argv[]) {
  static uv_timer_t timer_watcher;
  uv_loop_t *loop = uv_default_loop();
  uv_timer_init(loop, &timer_watcher);
  uv_timer_start(&timer_watcher, timer_callback, TIMEOUT_MS, REPEAT_MS);

  uv_pipe_init(loop, &in_pipe, 0);
  uv_pipe_init(loop, &out_pipe, 0);
  log_info("in:%d out:%d", in, out);
  uv_pipe_open(&in_pipe, in);
  uv_pipe_open(&out_pipe, out);
  write_client_pipe = &out_pipe;
  uv_read_start((uv_stream_t *)&in_pipe, alloc_buffer,
                common_read_data_from_cli);
  const int UV_HANDLE_BLOCKING_WRITES = 0x00100000;

  int fd = get_pty_fd();
  uv_tty_init(loop, &tty, fd, 1);

  pty_nonblock(fd);
  tty.flags &= ~UV_HANDLE_BLOCKING_WRITES;

  // uv_stream_set_blocking(write_client_pipe, true);
  //  uv_tty_set_mode(&tty, UV_TTY_MODE_NORMAL);

  uv_read_start((uv_stream_t *)&tty, alloc_buffer, common_read_tty);
  log_info("server6");
  // uv_prepare_t pp;
  // uv_prepare_init(loop, &pp);
  // uv_prepare_start(&pp, TERMTUNNEL_uv_prepare_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  log_error("uv_run exit");
}

// PASS
void send_exit_message(uv_async_t *handle) {
  // uv_msg_t *socket = (uv_msg_t *)handle->data;
  int exitcode = (int)handle->data;
  comm_write_static_packet_to_cli(COMMAND_CMD_EXIT, &exitcode, sizeof(int));
  exiting = true;
  free(handle);
}

void pty_process_on_exit(int exitcode) {
  // TODO 模拟信号错误
  // tell client our exitcode.
  async_process_exit->data = (int)exitcode;
  uv_async_send(async_process_exit);
}

void server(int argc, const char *argv[]) {
  // Close read end
  log_info("server");
  set_server_process();
  uv_loop_t *loop = uv_default_loop();
  async_process_exit = (uv_async_t *)malloc(sizeof(uv_async_t));
  uv_async_init(loop, async_process_exit, send_exit_message);
  pty_run(argc, argv, pty_process_on_exit);
  // close(1);
  // close(0);  //TODO linux 可以close，但是mac 关闭后会导致 libuv 异常 exit
  close(STDERR_FILENO);
  setsid();  // 维持父子关系，但是脱离终端。从而避免我们的server接收到键盘信号，并不只是关闭0，1，2就可以了。

  global_fsm_context = fsm_alloc();
  close(in_fd[0]);
  close(out_fd[1]);
  // printf("fff\n");
  background_loop(out_fd[0], in_fd[1], argc, argv);
  close(in_fd[1]);
  close(out_fd[0]);
  exit(EXIT_SUCCESS);
}

void send_data_to_agent(char *buf, size_t size) {
  uv_write_t *req1 = malloc(sizeof(uv_write_t));
  uv_buf_t *b = malloc(sizeof(uv_buf_t));
  size_t elen;
  char *buf_tmp = (char *)malloc(size + 2);
  memcpy(buf_tmp, buf, size);
  memcpy(buf_tmp + size, "!\n", 2);
  b->base = buf_tmp;
  b->len = size + 2;
  req1->data = b;
  uv_write(req1, (uv_stream_t *)&tty, b, 1, tty_write_cb_with_free);
}

void send_base64binary_to_agent(const char *buf, size_t size) {
  size_t elen = 0;
  char *ebuf = base64_encode(buf, size, &elen);
  log_debug("server will write base64 %*s(%d)", elen, ebuf, elen);
  char *tmp = (char *)malloc(elen + 1);
  tmp[0] = 'B';
  memcpy(tmp + 1, ebuf, elen);
  send_data_to_agent(tmp, elen + 1);
  free(tmp);
  free(ebuf);
}

void server_handle_client_packet(int64_t type, char *buf, ssize_t len) {
  switch (type) {
    case COMMAND_TTY_PLAIN_DATA: {
      // from cli keyborad stream
      uv_write_t *req1 = malloc(sizeof(uv_write_t));
      uv_buf_t *b = malloc(sizeof(uv_buf_t));
      b->base = memdup(buf, len);
      b->len = len;
      req1->data = b;
      uv_write(req1, (uv_stream_t *)&tty, b, 1, tty_write_cb_with_free);
      break;
    }
    case COMMAND_TTY_WIN_RESIZE: {
      struct winsize *ttysize = (struct winsize *)buf;  // The size of our tty
      resize_pty(ttysize);
      break;
    }
    case COMMAND_EXIT_REPL: {
      server_see_agent_is_repl = false;
      send_data_to_agent("EXIT", 4);
      break;
    }

    // case :
    case COMMAND_TTY_PING: {
      // DATA
      send_data_to_agent("PING", 4);
      break;
    }

    case COMMAND_FILE_EXCHANGE: {
      file_exchange_intent_t *a = (file_exchange_intent_t *)buf;
      // upload command
      // forward_agent()
      log_info("server do open file");
      log_info("%s->%s %d\n", a->src_path, a->dst_path, a->trans_mode);
      if (a->trans_mode == TRANS_MODE_RECV_FILE) {
        file_recv_start(a->src_path, a->dst_path);
      } else if (a->trans_mode == TRANS_MODE_SEND_FILE) {
        file_send_start(a->src_path, a->dst_path);
      } else {
        // TODO add folder
      }

      break;
    }
    case COMMAND_PORT_FORWARD: {
      port_forward_intent_t *a = (port_forward_intent_t *)buf;
      log_info("portforward %s:%hu <-> %s:%hu", a->src_host, a->src_port,
               a->dst_host, a->dst_port);
      if (a->forward_type == FORWARD_DYNAMIC_PORT_MAP) {
        // portforward_st(a->src_path, a->dst_path);
      } else if (a->forward_type == FORWARD_STATIC_PORT_MAP)  // TODO(jdz)
      {
        portforward_static_start(a->src_host, a->src_port, a->dst_host,
                                 a->dst_port);
      } else if (a->forward_type == FORWARD_STATIC_PORT_MAP_LISTEN_ON_AGENT) {
        log_info("remote mode");
        char buf[READ_CHUNK_SIZE];
        snprintf(buf, READ_CHUNK_SIZE, "%s:%hu:%s:%hu",
            a->src_host, a->src_port, a->dst_host, a->dst_port);
        server_call_agent(METHOD_CALL_FORWARD_STATIC, buf);
        comm_write_packet_to_cli(COMMAND_RETURN, strdup("bind done (guess)\n"),
                              sizeof("bind done (guess)\n"));
      }

      break;
    }

    default: {
      log_error("server_handle_packet unknown type %d", type);
    }
  }
  return;
}

//命令
void server_handle_green_packet(char *buf, int size) {
  //首包认为是：AGENT_VERSION: 1
  // handshake()
  // MAGIC
  log_debug("server handle agent data: %*s(%d)", size, buf, size);
  int handshake_length = sizeof("MAGIC!") - 1;
  if (size == handshake_length &&
      memcmp("MAGIC!", buf, handshake_length) == 0) {
    server_see_agent_is_repl = true;
    comm_write_packet_to_cli(COMMAND_ENTER_REPL, NULL, 0);
    // TODO!
    libuv_add_vnet_notify();
    vnet_init(vnet_notify_to_libuv);
    return;
  }

  int ping_length = sizeof("PING!") - 1;
  if (size == ping_length && memcmp("PING!", buf, ping_length) == 0) {
    log_debug("server green ping!");
    comm_write_packet_to_cli(COMMAND_TTY_PING, NULL, 0);
    return;
  }

  int nop_length = sizeof("NOP!") - 1;
  if (size == nop_length && memcmp("NOP!", buf, nop_length) == 0) {
    log_debug("server green nop!");
    return;
  }
  if (size > 1 && buf[0] == 'B') {
    size_t result_len = 0;
    char *result = base64_decode(buf + 1, size - 1, &result_len);
    if (result_len == 0 || result == NULL) {
      log_error("found a error base64 [%*s]\n", size - 1, buf + 1);
      return;
    }
    server_handle_agent_data(result, result_len);
    free(result);
  }
  return;
}

//流量
void server_handle_agent_data(char *buf, int size) {
  log_debug("server handle agent binary data: %*s(%d)", size, buf, size);
  // TCPIP
  vnet_data_income(buf, size);

  //其他可能的扩展操作
  // send_base64binary_to_agent(buf, size);
  // comm_write_packet_to_cli(COMMAND_TTY_PING, NULL, 0);
  return;
}
