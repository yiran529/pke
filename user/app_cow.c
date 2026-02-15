/*
 * This app fork a child process to read and write the heap data from parent process.
 * Because implemented copy on write, when child process only read the heap data,
 * the physical address is the same as the parent process.
 * But after writing, child process heap will have different physical address.              
 */

#include "user/user_lib.h"
#include "util/types.h"

int main(void) {
  int *heap_data = naive_malloc();
  printu("the physical address of parent process heap is: ");
  printpa(heap_data);
  int pid = fork();
  // if (pid != 0) {
  //   naive_free(heap_data);
  // }
  if (pid == 0) {
    printu("the physical address of child process heap before copy on write is: ");
    printpa(heap_data);
    // printu("*heap_data before write: %d\n", heap_data[0]);
    // printu("heap_data va: %lx\n", (uint64)heap_data);
    heap_data[0] = 0;
    printu("the physical address of child process heap after copy on write is: ");
    printpa(heap_data);
  } else {
    wait(pid); // 新加的。不加这个可能会导致父进程先退出，子进程还在运行，接着就可能导致程序直接结束
  }
  exit(0);
  return 0;
}
