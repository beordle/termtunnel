/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_UTILS_H

#define TERMTUNNEL_UTILS_H
#include <assert.h>

#include "log.h"
#define CHECK(x, ...)       \
  if (!(x)) {               \
    log_fatal(__VA_ARGS__); \
    exit(0);                \
  }
// ALLOC_CHECK(ptr != NULL, "Pointer not initialized");

void set_stdin_raw();
void restore_stdin();
void *memdup(const void *src, size_t n);
const char *green_encode(const char *buf, int len, int *result_len);
#endif