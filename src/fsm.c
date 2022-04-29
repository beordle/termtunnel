/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include "fsm.h"
// 从终端字节流中，解析出绿色背景绿色文字的部分
// 带有main函数，可以直接编译，用于测试
// gcc fsm.c -DTEST_MAIN
// echo -ne
// '\e[42;32m\e[xf33g324234aflkjdasklfadsjflk234你好23\e[31;mfff898\e[0m'
// |./a.out C API

// https://zh.wikipedia.org/wiki/ANSI%E8%BD%AC%E4%B9%89%E5%BA%8F%E5%88%97

#define PLAINTEXT 0
#define ESCAPE_ENTER 1
#define CSI_ENTER 2
#define CSI_ARG 3
#define CSI_EXIT 4
#define CSI_OTHER 5
#include "log.h"

void enter_log(const char *name) {
  // printf("%s\n", name);
  return;
}

void memmerge(char *dest, const char *src, int dest_size, int src_size) {
  dest += dest_size;
  memcpy(dest, src, src_size);
}

void fallback_to_plaintext(fsm_context *ctx) {
  ctx->state = PLAINTEXT;
  ctx->processed = ctx->offset;
}

void fsm_append_output(fsm_context *ctx, char *output, int output_size) {
  if (!ctx->output) {
    ctx->output_cap = 1024;
    ctx->output = (char *)malloc(ctx->output_cap);
  }
  if (output_size + ctx->output_size > ctx->output_cap) {
    ctx->output_cap *= 2;
    ctx->output = realloc(ctx->output, ctx->output_cap);
  }
  memmerge(ctx->output, output, ctx->output_size, output_size);
  ctx->output_size += output_size;
}

void handle_csi_other(fsm_context *ctx) {
  enter_log("CSI Other");

#if 0
    setukg0               Set United Kingdom G0 character set    ^[(A
    setukg1               Set United Kingdom G1 character set    ^[)A
    setusg0               Set United States G0 character set     ^[(B
    setusg1               Set United States G1 character set     ^[)B
    setspecg0             Set G0 special chars. & line set       ^[(0
    setspecg1             Set G1 special chars. & line set       ^[)0
    setaltg0              Set G0 alternate character ROM         ^[(1
    setaltg1              Set G1 alternate character ROM         ^[)1
    setaltspecg0          Set G0 alt char ROM and spec. graphics ^[(2
    setaltspecg1          Set G1 alt char ROM and spec. graphics ^[)2
#endif

  char a = ctx->input[ctx->offset];
  // 容错，如果状态机异常，reset。
  if (a == '[' - 64) {
    ctx->state = ESCAPE_ENTER;
    ctx->offset++;
    return;
  }
  if (a == '(' || a == ')') {
    ctx->offset += 2;  // may risk
    ctx->state = PLAINTEXT;
    return;
  }

  fallback_to_plaintext(ctx);
  ctx->offset++;
}

void handle_plaintext(fsm_context *ctx) {
  enter_log("Plain Text");

  char a = ctx->input[ctx->offset];
  if (a == '[' - 64) {
    ctx->state = ESCAPE_ENTER;
  } else {
    // if (ctx->process)
    // printf("? %d %d\n",ctx->bg_colorflag, ctx->fg_colorflag);
    if (ctx->bg_colorflag && ctx->fg_colorflag) {
      fsm_append_output(ctx, &a, 1);
    }
    ctx->processed = ctx->offset;
  }
  ctx->offset++;
}

void handle_escape_enter(fsm_context *ctx) {
  enter_log("Escape Enter");
  char a = ctx->input[ctx->offset];
  if (a == '[' || a == '?') {
    ctx->state = CSI_ENTER;
    ctx->offset++;
  } else {
    ctx->state = CSI_OTHER;
    // fallback_to_plaintext(ctx);
  }
}

void handle_csi_enter(fsm_context *ctx) {
  enter_log("CSI Enter");
  char a = ctx->input[ctx->offset];
  // modesoff SGR0         Turn off character attributes          ^[[m
  // modesoff SGR0         Turn off character attributes          ^[[0m
  //  可以直接是m
  if (('0' <= a && a <= '9') || a == ';' || a == 'm')  //
  {
    memset(ctx->csi, 0, sizeof(csi_info));
    ctx->state = CSI_ARG;
    return;
  }
  // tabset HTS            Set a tab at the current column        ^[H
  if ('H' == a) {
    ctx->state = CSI_EXIT;
    return;
  } else {
    fallback_to_plaintext(ctx);
  }
}

void handle_csi_arg(fsm_context *ctx) {
  enter_log("CSI ARG\n");
  char a = ctx->input[ctx->offset];
  // 支持 [[ 嵌套 [ in [
  if (a == '[') {
    ctx->csi->argc = 0;
    ctx->state = CSI_ENTER;
    ctx->offset++;
    return;
  }

  if (a == 'm') {
    ctx->csi->argc++;
    ctx->state = CSI_EXIT;
    return;
  }
  if ('l' == a) {
    ctx->csi->argc = 0;
    ctx->state = CSI_EXIT;
    return;
  }

  if ('y' == a) {
    ctx->csi->argc = 0;
    ctx->state = CSI_EXIT;
    return;
  }

  if (a == ';') {
    ctx->csi->cur++;
    ctx->csi->argc++;
    ctx->offset++;
    return;
  }

  if ('0' <= a && a <= '9') {
    int digit = a - '0';
    ctx->csi->argv[ctx->csi->cur] = ctx->csi->argv[ctx->csi->cur] * 10 + digit;
    ctx->offset++;
    return;
  }

  fallback_to_plaintext(ctx);
  ctx->offset++;
}

void event_csi_callback(fsm_context *ctx) {
  // https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
  // To reset colors to their defaults, use ESC[39;49m (not supported on some
  // terminals), or reset all attributes with ESC[0m. tmux echo -ne
  // '\e[42;32m\e[xf33g324234aflkjdasklfadsjflk234你好23\e[31;mfff898\e[0m' 39
  // Default foreground color
  // 49	Default background color	Implementation defined (according to
  // standard)
  for (int i = 0; i < ctx->csi->argc; i++) {
    int num = ctx->csi->argv[i];
    // printf("%d -> %d\n", i, ctx->csi->argv[i]);

    if (num >= 40 && num <= 47) {
      ctx->bg_colorflag = false;
    }
    if (num == 42) {
      // printf("error\n");
      ctx->bg_colorflag = true;
    }
    if (num >= 30 && num <= 37) {
      ctx->fg_colorflag = false;
    }
    if (num == 32) {
      // printf("error\n");
      ctx->fg_colorflag = true;
    }
    if (num == 0) {
      // printf("error\n");
      ctx->fg_colorflag = false;
      ctx->bg_colorflag = false;
    }
    if (num == 39) {
      // printf("error\n");
      ctx->fg_colorflag = false;
    }
    if (num == 49) {
      // printf("error\n");
      ctx->bg_colorflag = false;
    }
    // 38	Set foreground color	Next arguments are 5;n or 2;r;g;b
    // 48	Set bacground color	Next arguments are 5;n or 2;r;g;b
    if (num == 38) {
      ctx->fg_colorflag = false;
      // printf("error\n");
      // for (int j=i;j<ctx->csi->argc;j++){
      //     int num=ctx->csi->argv[j];
      //     printf("num: %d\n",num);
      // }
      // printf("end\n");
      // exit(0);
    }
    if (num == 48) {
      ctx->bg_colorflag = false;
      // printf("error\n");
      // for (int j=i;j<ctx->csi->argc;j++){
      //     int num=ctx->csi->argv[j];
      //     printf("num: %d\n",num);
      // }
      // printf("end\n");
      // exit(0);
    }
  }
  // ctx->colorflag
}

static void handle_csi_exit(fsm_context *ctx) {
  enter_log("CSI Exit\n");

  event_csi_callback(ctx);

  fallback_to_plaintext(ctx);

  ctx->offset++;
}

static void fsm_gc(fsm_context *ctx) {
  // chage input by offset;
  //
  if (ctx->processed > 0) {
    memmove(ctx->input, ctx->processed + ctx->input,
            ctx->input_size - ctx->processed);
    ctx->input_size -= ctx->processed;
    ctx->offset -= ctx->processed;
    ctx->processed = 0;  // ctx->processed -= ctx->processed;
  }
}

void fsm_run(fsm_context *ctx) {
  ctx->run_count++;
  if (ctx->run_count % 100 == 0) {
    fsm_gc(ctx);
  }
  while (ctx->offset < ctx->input_size) {
    switch (ctx->state) {
      case PLAINTEXT:
        handle_plaintext(ctx);
        break;
      case ESCAPE_ENTER:
        handle_escape_enter(ctx);
        break;
      case CSI_ENTER:
        handle_csi_enter(ctx);
        break;
      case CSI_ARG:
        handle_csi_arg(ctx);
        break;
      case CSI_EXIT:
        handle_csi_exit(ctx);
        break;
      case CSI_OTHER:
        handle_csi_other(ctx);
        break;

      default:
        CHECK(0, "abort");

        break;
    }
  }
}

void fsm_init(fsm_context *ctx) {
  memset(ctx, 0, sizeof(fsm_context));
  ctx->state = PLAINTEXT;
  ctx->csi = (csi_info *)malloc(sizeof(csi_info));
  memset(ctx->csi, 0, sizeof(csi_info));
}

fsm_context *fsm_alloc() {
  fsm_context *stub = (fsm_context *)malloc(sizeof(fsm_context));
  fsm_init(stub);
  return stub;
}

void fsm_deinit(fsm_context *ctx) { free(ctx->csi); }

void fsm_free(fsm_context *ctx) {
  fsm_deinit(ctx);
  free(ctx);
}

void fsm_append_input(fsm_context *ctx, const char *input, int input_size) {
  if (!ctx->input) {
    ctx->input_cap = 1024;
    ctx->input = (char *)malloc(ctx->input_cap);
    CHECK(ctx->input, "!ctx->input");
  }
  if (input_size + ctx->input_size > ctx->input_cap) {
    ctx->input_cap = (input_size + ctx->input_size) * 2;

    ctx->input = realloc(ctx->input, ctx->input_cap);
  }
  memmerge(ctx->input, input, ctx->input_size, input_size);
  ctx->input_size += input_size;
}

int fsm_pop_output(fsm_context *ctx, char *dst, int max_size) {
  // return until !

  int size = 0;

  //======================== 如果没有碰到!，就不返回结果。

  int loop_size = ctx->output_size;
  int last = -1;
  for (int i = 0; i < loop_size; i++) {
    if (ctx->output[i] == '!') {
      last = i;
      break;
    }
  }
  //如果没有出现过 !
  if (last == -1) {
    return 0;
  } else {
    size = last + 1;  //算上！本身的大小
  }
  //========================
  // int opsize= max_size > size? size:max_size;
  // if (size>max_size){
  //至少可以写入一frame
  CHECK(size + 1 <= max_size, "no mem to write frame");
  int opsize = size;
  memcpy(dst, ctx->output, opsize);
  dst[opsize] = '\0';
  // debug
#if 0
    char* a = malloc(2048);
    memset(a,'\0',2048);
    memcpy(a, ctx->output, opsize);
    //duckdebug("fsm pop: %s\n", a);
    free(a);
#endif

  ctx->output_size -= opsize;

  memmove(ctx->output, opsize + ctx->output, ctx->output_size);
  return opsize;
}

#ifdef TEST_MAIN

int main() {
  fsm_context *global_fsm_context = fsm_alloc();
  char *c = (char *)malloc(1);
  int size;
  do {
    size = read(0, c, 1);
    if (size < 0) {
      perror("read error\n");
      exit(0);
    }

    fsm_append_input(global_fsm_context, c, size);
  } while (size);

  fsm_run(global_fsm_context);
  char *dst = (char *)malloc(2001);
  memset(dst, 0, 2001);
  int sz = 1;
  while (sz != 0) {
    sz = fsm_pop_output(global_fsm_context, dst, 1);
    printf("%s", dst);
  }
  fsm_free(global_fsm_context);
  return 0;
}
#endif
