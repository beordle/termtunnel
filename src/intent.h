/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_INTENT_H
#define TERMTUNNEL_INTENT_H
#include <limits.h>
#include <stdint.h>
#include "config.h"

#ifndef MAX_ARG_STRLEN
#define MAX_ARG_STRLEN 512
#endif

#ifndef ARG_MAX
#define ARG_MAX 20
#endif

#define COMMAND_TTY_PLAIN_DATA 1
#define COMMAND_CMD_EXIT 2
#define COMMAND_TTY_WIN_RESIZE 3
#define COMMAND_ENTER_REPL 4
#define COMMAND_TTY_PING 5
#define COMMAND_EXIT_REPL 6
#define COMMAND_FILE_EXCHANGE 7
#define COMMAND_PORT_FORWARD 8
#define COMMAND_RETURN 9
#define COMMAND_GET_RUNNING_TASK_COUNT 10
typedef struct ci {
  int i;
  int mode;
  char *c;
} intent_t;

typedef struct {
  char src_path[PATH_MAX];
  char dst_path[PATH_MAX];
  int32_t trans_mode;
} file_exchange_intent_t;

#define TRANS_MODE_SEND_FILE 1
#define TRANS_MODE_RECV_FILE 2

#define IPV4_AND_IPV6_MAX_LENGTH 64
typedef struct {
  int32_t forward_type;
  char src_host[IPV4_AND_IPV6_MAX_LENGTH];
  char dst_host[IPV4_AND_IPV6_MAX_LENGTH];
  uint16_t src_port;
  uint16_t dst_port;
} port_forward_intent_t;

#define FORWARD_DYNAMIC_PORT_MAP 2
#define FORWARD_STATIC_PORT_MAP 3
#define FORWARD_STATIC_PORT_MAP_LISTEN_ON_AGENT 4

#endif
