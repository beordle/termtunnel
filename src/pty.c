/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include "pty.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "utils.h"

static int myself_fd;
static int pty_fd;

typedef struct thread_pass_arg_t {
  pid_t process_pid;
  tell_exitcode_callback *cb_func;
} thread_pass_arg_t;

int get_pty_fd() { return pty_fd; }

static int do_exec(const char *name, int ptypt, char **argv, int argc) {
  struct winsize ttysize;
  if (ioctl(myself_fd, TIOCGWINSZ, &ttysize) != 0) {
    // fallback
    ttysize.ws_col = 20;
    ttysize.ws_row = 20;
    ttysize.ws_xpixel = 0;
    ttysize.ws_ypixel = 0;
  }

  setsid();

  char **new_argv = (char **)malloc(sizeof(char *) * (argc + 1));
  for (int i = 0; i < argc; ++i) {
    new_argv[i] = argv[i];
  }

  new_argv[argc] = NULL;

  int slavept = open(name, O_RDWR);

  if (slavept < 0) {
    fprintf(stderr, "open slave error\n");
    exit(EXIT_FAILURE);
  }

  ioctl(slavept, TIOCSWINSZ, &ttysize);

  close(0);
  close(1);
  close(2);
  dup2(slavept, 0);
  dup2(slavept, 1);
  dup2(slavept, 2);

  close(slavept);
  close(pty_fd);

  int32_t fdlimit = (int32_t)sysconf(_SC_OPEN_MAX);
  for (int i = STDERR_FILENO + 1; i < fdlimit; i++) {
    close(i);
  }
  //unsetenv("TERMTUNNEL_VERBOSE");
  execvp(new_argv[0], new_argv);
  fprintf(stderr, "%s: %s\n", new_argv[0], strerror(errno));
  exit(EXIT_FAILURE);
  return 0;
}

int do_waitpid(pid_t childpid) {
  int status = 0;
  pid_t wait_id = 0;
  do {
    errno = 0;
    wait_id = waitpid(childpid, &status, NULL);

    if (wait_id < 0 && errno == EINTR) {
      wait_id = 0;
      continue;
    } else {
    }
  } while (wait_id == 0 || (!WIFEXITED(status) && !WIFSIGNALED(status)));
  if (wait_id < 0) {
    return 255;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    // TODO kill(getpid(), WTERMSIG(status));
    // //为了让父进程获得相同的行为,不过这样做我们需要先还原tty printf("Tracee
    // Exit by Signal %s\n", strsignal(WTERMSIG(status)));
  }
  return 255;
}

void resize_pty(winsize_t *ttysize) { ioctl(pty_fd, TIOCSWINSZ, ttysize); }

int do_waitpid_wrap(thread_pass_arg_t *arg) {
  int exitcode = do_waitpid(arg->process_pid);
  log_info("pty exit");
  arg->cb_func(exitcode);
  free(arg);
  return exitcode;
}

int pty_run(int argc, char *argv[], tell_exitcode_callback *cb) {
  pty_fd = open("/dev/ptmx", O_RDWR | O_CLOEXEC);
  if (pty_fd == -1) {
    perror("Failed to get a pseudo terminal");
    exit(EXIT_FAILURE);
  }
  if (grantpt(pty_fd) != 0) {
    perror("Failed to change pseudo terminal's permission");
    exit(EXIT_FAILURE);
  }
  if (unlockpt(pty_fd) != 0) {
    perror("Failed to unlock pseudo terminal");
    exit(EXIT_FAILURE);
  }

  myself_fd = STDIN_FILENO;  // open("/dev/tty", O_RDONLY| O_NOCTTY);
  char *name = (char *)malloc(PATH_MAX);
  ptsname_r(pty_fd, name, PATH_MAX);
  if (name == NULL) {
    exit(EXIT_FAILURE);
  }
  // fcntl(pty_fd, F_SETFL, O_NONBLOCK);

  pid_t pid;
  pid = fork();
  if (pid == 0) {
    // TODO !!!!
    if (argc == 1) {
      // 没有命令
      // printf("Command not found.\n");
      exit(0);
    }
    argv++;
    argc--;

    do_exec(name, pty_fd, argv, argc);
    exit(0);
  } else {
    // spwan a thread to watch the process. out of libuv
    // printf("PID: %d\n",pid);
    // watche_pid();
    // uv_thredo_waitpid();
    pthread_t thr;

    thread_pass_arg_t *temp =
        (thread_pass_arg_t *)malloc(sizeof(thread_pass_arg_t));
    temp->cb_func = cb;
    temp->process_pid = pid;
    pthread_create(&thr, NULL, do_waitpid_wrap, temp);
    pthread_detach(thr);
  }
  free(name);
  return pty_fd;
}
