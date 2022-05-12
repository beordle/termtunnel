/**
 * Copyright (c) 2022 Jindong Zhang
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_AGENT_H_
#define TERMTUNNEL_AGENT_H_
#include <stddef.h>
void agent_restore_stdin();
void agent_set_stdin_noecho();

// extern void block_write_frame_to_server(char* data, int data_size);
extern int write_binary_to_server(const char *buf, size_t size);
void agent(int argc, char** argv);
#endif
