#ifndef _KERNEL_H_
#define _KERNEL_H_

#include "util/types.h"
#include "process.h"
#include "elf.h"

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

size_t parse_args(arg_buf *arg_bug_msg);

#endif
