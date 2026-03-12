#include "user_lib.h"
#include "util/string.h"
#include "util/types.h"

static int split_pipe_arg(const char *arg, char *text, int text_max, char *path, int path_max) {
  int sep = -1;
  for (int i = 0; arg[i] != '\0' && arg[i + 1] != '\0'; i++) {
    if (arg[i] == ':' && arg[i + 1] == ':') {
      sep = i;
      break;
    }
  }
  if (sep < 0)
    return -1;

  int pos = 0;
  for (int i = 0; i < sep && pos < text_max - 1; i++)
    text[pos++] = arg[i];
  text[pos] = '\0';

  pos = 0;
  for (int i = sep + 2; arg[i] != '\0' && pos < path_max - 1; i++)
    path[pos++] = arg[i];
  path[pos] = '\0';

  if (text[0] == '\0' || path[0] == '\0')
    return -1;
  return 0;
}

int main(int argc, char *argv[]) {
  if (!argv || !argv[0] || argv[0][0] == '\0') {
    printu("\n");
    exit(0);
    return 0;
  }

  char text[96];
  char path[96];
  if (split_pipe_arg(argv[0], text, sizeof(text), path, sizeof(path)) == 0) {
    int fd = open(path, O_RDWR | O_CREAT);
    if (fd < 0) {
      printu("echo0: cannot open %s\n", path);
      exit(-1);
      return -1;
    }
    write_u(fd, text, strlen(text));
    write_u(fd, "\n", 1);
    close(fd);
  } else {
    printu("%s\n", argv[0]);
  }

  exit(0);
  return 0;
}
