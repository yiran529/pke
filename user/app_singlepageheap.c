/*
 * Below is the given application for lab2_challenge2_singlepageheap.
 * This app performs malloc memory.
 */

#include "user_lib.h"
//#include "util/string.h"

typedef unsigned long long uint64;

char* strcpy(char* dest, const char* src) {
  char* d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}
int main(void) {
  
  char str[20] = "hello, world!!!";
  char *m = (char *)better_malloc(100);
  char *p = (char *)better_malloc(50);
  if((uint64)p - (uint64)m > 512 ){
    printu("you need to manage the vm space precisely!");
    exit(-1);
  } // else printu("malloc space ok!\n");
  better_free((void *)m);

  strcpy(p,str);
  printu("%s\n",p);
  char *n = (char *)better_malloc(50);
  
  if(m != n)
  {
    printu("your malloc is not complete.\n");
    exit(-1);
  } // else printu("malloc/free work well!\n");

  // // more tests
  // // 简单重复申请/释放验证碎片合并
  // better_free(n);
  // char *a = (char *)better_malloc(30);
  // char *b = (char *)better_malloc(60);
  // better_free(a);
  // better_free(b);
  // char *c = (char *)better_malloc(80);
  // if(c != a) {
  //   printu("fragmentation not merged as expected.\n");
  //   exit(-1);
  // } else {
  //   printu("c = a = 0x%x, fragmentation merged well!\n", (uint64)c);
  // }

//  else{
//    printu("0x%lx 0x%lx\n", m, n);
//  }
  exit(0);
  return 0;
}
 /*
 * Below is the given application for lab2_challenge2_singlepageheap.
 * This app performs malloc memory.
 */

// #include "user_lib.h"
// #include "util/types.h"
// #include "util/string.h"
// int main(void) {
  
//   char str[20] = "cross page";
//   char *m = (char *)better_malloc(100);
//   char *p = (char *)better_malloc(4096);  // cross page
//   if((uint64)p - (uint64)m > 512 ){
//     printu("you need to manage the vm space precisely!");
//     exit(-1);
//   }
//   better_free((void *)m);

//   strcpy(p,str);
//   printu("%s\n",p);
//   exit(0);
//   return 0;
// }
