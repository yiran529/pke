#include "user_lib.h"
#include "util/types.h"

static int is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

int main(int argc, char *argv[]) {
  if (!argv || !argv[0] || argv[0][0] == '\0') {
    printu("wc: missing input file\n");
    exit(-1);
    return -1;
  }

  int fd = open(argv[0], O_RDONLY);
  if (fd < 0) {
    printu("wc: cannot open %s\n", argv[0]);
    exit(-1);
    return -1;
  }

  int lines = 0;
  int words = 0;
  int bytes = 0;
  int in_word = 0;
  char buf[128];

  while (1) {
    int n = read_u(fd, buf, sizeof(buf));
    if (n <= 0)
      break;

    bytes += n;
    for (int i = 0; i < n; i++) {
      char c = buf[i];
      if (c == '\n')
        lines++;
      if (is_space(c)) {
        in_word = 0;
      } else if (!in_word) {
        words++;
        in_word = 1;
      }
    }
    if (n < (int)sizeof(buf))
      break;
  }

  close(fd);
  printu("%d %d %d\n", lines, words, bytes);
  exit(0);
  return 0;
}
