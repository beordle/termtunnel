/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_REPL_H

#define TERMTUNNEL_REPL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "thirdparty/linenoise.h"
extern bool g_oneshot_mode;
extern void repl_init();
extern void repl_run(int _in, int _out);
extern void interact_run(int _in, int _out);
extern int get_repl_stdout();

extern void oneshot_run(int _in, int _out);
extern void send_binary(int fd, int64_t type, char *addr, int len);
#endif
