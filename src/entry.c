/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */
#include <assert.h>
#include <fcntl.h>
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
#include "repl.h"
#include "agent.h"
#include "config.h"
#include "fileexchange.h"
#include "fsm.h"
#include "intent.h"
#include "log.h"
#include "pipe.h"
#include "portforward.h"
#include "pty.h"
#include "repl.h"
#include "state.h"
#include "thirdparty/base64.h"
#include "thirdparty/setproctitle.h"
#include "utils.h"
#include "vnet.h"

#ifdef ENABLE_CREASH_HELPER
int crash_fd;
#include <execinfo.h>
#include <sys/stat.h>
void handler(int sig) {
  if (sig == SIGPIPE) {
    printf("exit: SIGPIPE\n");
    exit(EXIT_FAILURE);
  }
  void *array[10];
  size_t size;

  size = backtrace(array, 10);
  // fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, crash_fd);
  exit(1);
}
#endif

static void on_resize(int signum) {
  struct winsize ttysize;  // The size of our tty
  int ttyfd = open("/dev/tty", O_RDONLY | O_NOCTTY);
  int r = ioctl(ttyfd, TIOCGWINSZ, &ttysize);
  CHECK(r == 0, "resize ioctl");
  close(ttyfd);
  struct winsize *w = (struct winsize *)malloc(sizeof(struct winsize));
  memcpy(w, &ttysize, sizeof(struct winsize));
  // printf("%d\n",get_repl_stdout());
  send_binary(get_repl_stdout(), COMMAND_TTY_WIN_RESIZE, w,
              sizeof(struct winsize));
}


void cli_loop(int in, int out, int argc, const char *argv[]) {
  repl_init();
  while (true) {
    interact_run(in, out);
    kill(getpid(), SIGWINCH);
    // one shot 需要由 agent设定，然后传输给server，再控制到 client的行为。
    if (g_oneshot_mode) {
      oneshot_run(in, out);
    } else {
      repl_run(in, out);
    }
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

int main(int argc, const char *argv[]) {
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

#ifdef ENABLE_CREASH_HELPER
  void *array[10];
  size_t size = backtrace(array, 10);  // 提前触发 dlopen
  crash_fd = open("termtunnel_crash.log", O_APPEND);
  if (crash_fd < 0) {
    printf("crash log open error\n");
  }
  signal(SIGSEGV, handler);
  signal(SIGPIPE, handler);
  signal(SIGILL, handler);
  signal(SIGABRT, handler);
#endif

  log_set_quiet(true);
  char *verbose_env_str = getenv("TERMTUNNEL_VERBOSE");
  if (verbose_env_str != NULL) {
    int verbose_level = atoi(verbose_env_str);
    if (verbose_level < 0 || verbose_level > 9) {
      verbose_level = 0;
    }
    FILE *f = fopen("termtunnel.log", "a");
    if (!f) {
      fprintf(stderr, "Warning: logfile termtunnel.log open error\n");
    } else {
      log_add_fp(f, verbose_level);
    }
  }
  termtunnel_state_init();
  q = queue_create();
  // spt_init(argc, argv);
  // 其实这里仿照 lldb -- 去启动应用可能会更好？
  // if run as agent
  if (argc >= 2 && (strcmp(argv[1], "-a") == 0 || strcmp(argv[1], "--") == 0)) {
    log_info("agent pid %d", getpid());
    argv+= 2;
    agent(argc - 2, argv);
    return 0;
  }

  if (pipe(in_fd) == -1) {
    perror("Cannot create the pipe");
    exit(EXIT_FAILURE);
  }

  if (pipe(out_fd) == -1) {
    perror("Cannot create the pipe");
    exit(EXIT_FAILURE);
  }

  // do fork
  pid_t child_pid;
  if ((child_pid = fork()) == -1) {
    perror("Process cannot be fork");
    exit(EXIT_FAILURE);
  }
  if (child_pid == 0) {
    // setproctitle("server");
    //  uv_setup_args bug
    log_info("server pid %d", getpid());
    server(argc, argv);
    log_info("server exit");
    exit(0);
  } else {
    signal(SIGWINCH, on_resize);
    // setproctitle("termtunnel");
    log_info("client pid %d", getpid());
    cli(argc, argv, child_pid);
    log_info("client exit");
    exit(0);
  }
  return 0;
}
