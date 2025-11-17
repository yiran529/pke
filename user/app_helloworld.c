/*
 * Below is the given application for lab1_1. 
 * 
 * You can build this app (as well as our PKE OS kernel) by command:
 * $ make
 *
 * Or run this app (with the support from PKE OS kernel) by command:
 * $ make run 
 */

#include "user_lib.h"
#include "../kernel/riscv.h"

int main(void) {
  // 获取当前栈指针地址
  uint64 stack_pointer;
  asm volatile("mv %0, sp" : "=r"(stack_pointer));
  
  printu("Hello world!\n");
  printu("Current stack pointer: 0x%lx\n", stack_pointer);
  // printu("Expected stack pointer (USER_STACK): 0x81100000\n");
  printu("Difference from 0x81100000: 0x%lx bytes\n", 0x81100000 - stack_pointer);
  
  // 展示一些额外的信息
  uint64 ra;
  asm volatile("mv %0, ra" : "=r"(ra));
  printu("Return address: 0x%lx\n", ra);
  
  // 展示当前函数地址
  printu("main function address: %p\n", main);
  
  // 测试栈增长情况
  volatile char stack_var;
  printu("Address of stack variable: 0x%lx\n", (uint64)&stack_var);
  printu("Stack grows downward: %s\n", (uint64)&stack_var < stack_pointer ? "Yes" : "No");

  exit(0);
}