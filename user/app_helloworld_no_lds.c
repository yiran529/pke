/*
 * Below is the given application for lab2_1.
 * This app runs in its own address space, in contrast with in direct mapping.
 */

#include "user_lib.h"
#include "util/types.h"

int main(void) {
  int stack_var = 42;
  uint64 stack_addr = (uint64)&stack_var;
  
  printu("=== USER MODE: Automatic Translation ===\n");
  printu("Stack VA: 0x%lx (hardware MMU translates this)\n", stack_addr);
  printu("Value: %d (accessed via VA, MMU auto-converts to PA)\n", stack_var);
  printu("About to call printu() which triggers syscall...\n\n");
  
  printu("Hello world!\n");
  exit(0);
}
