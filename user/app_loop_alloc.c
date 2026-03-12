#include "user_lib.h"
#include "util/string.h"
#include "util/types.h"

int main(int argc, char *argv[]) {
  if (!argv || !argv[0] || argv[0][0] == '\0') {
    printu("loop_alloc: missing size argument\n");
    exit(1);
    return 1;
  }

  long req = atol(argv[0]);
  if (req <= 0 || req > 0x7fffffffL) {
    printu("loop_alloc: invalid size %s\n", argv[0]);
    exit(1);
    return 1;
  }

  int size = (int)req;
  unsigned int iter = 0;
  printu("loop_alloc: size=%d\n", size);

  while (1) {
    char *p = (char *)better_malloc(size);
    if (!p) {
      printu("loop_alloc: better_malloc failed at iter=%d\n", iter);
      yield();
      continue;
    }

    p[0] = (char)(iter & 0xff);
    if (size > 1)
      p[size - 1] = (char)((iter + 1) & 0xff);

    better_free((void *)p);
    if(iter % 10000 == 0)
      printu("loop_alloc(sz=%d): iter=%d\n", size, iter);
    iter++;
  }

  return 0;
}
