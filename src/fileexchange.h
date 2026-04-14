/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_FILEEXCHANGE_H
#define TERMTUNNEL_FILEEXCHANGE_H
#include <stdint.h>

int file_send_start(char *src_path, char *dst_path);
int file_receiver_start(void);

int file_recv_start(char *src_path, char *dst_path);
int file_sender_start(void);

#endif
