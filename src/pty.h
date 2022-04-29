/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_PTY_H
#define TERMTUNNEL_PTY_H
#include <termios.h>
typedef int tell_exitcode_callback(int exitcode);
extern int pty_run(int argc, char *argv[], tell_exitcode_callback *cb);
extern int get_pty_fd();
typedef struct winsize winsize_t;
extern void resize_pty(winsize_t *a);
#endif