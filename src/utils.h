/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_UTILS_H

#define TERMTUNNEL_UTILS_H
#include <assert.h>
#include <pthread.h>
#include "log.h"
#define CHECK(x, ...)       \
  if (!(x)) {               \
    log_fatal(__VA_ARGS__); \
    exit(0);                \
  }

struct counter_t {
  int value;
  pthread_mutex_t lock;
};


extern int writen(int fd, void *buf, int n);
extern void set_stdin_raw();
extern void restore_stdin();
extern void *memdup(const void *src, size_t n);
extern const char *green_encode(const char *buf, int len, int *result_len);


extern void utils_counter_init(struct counter_t *c);
extern void utils_counter_increment_by(struct counter_t *c, int by);
extern void utils_counter_increment(struct counter_t *c);
extern int utils_counter_get(struct counter_t *c);

#endif