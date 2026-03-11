#include "user_lib.h"
#include "util/string.h"
#include "util/types.h"

int main(int argc, char *argv[]) {
  char *path = argv[0];
  int width = 20;

  // try to open as directory first; if that fails, treat as regular file
  int dir_fd = opendir_u(path);
  printu("---------- ls command -----------\n");
  printu("ls \"%s\":\n", path);

  if (dir_fd >= 0) {
    printu("[name]               [type]  [size]\n");
    struct dir dir;
    while (readdir_u(dir_fd, &dir) == 0) {
      // pad name to width, same technique as before
      char name[width + 1];
      memset(name, ' ', width + 1);
      name[width] = '\0';
      if (strlen(dir.name) < width) {
        strcpy(name, dir.name);
        name[strlen(dir.name)] = ' ';
      }

      // build child path to stat the entry
      char child_path[64];
      strcpy(child_path, path);
      strcat(child_path, "/");
      strcat(child_path, dir.name);

      // try open() to get stat; directories may fail on hostfs, fall back to opendir_u
      struct istat st;
      int fd = open(child_path, O_RDONLY);
      if (fd >= 0) {
        stat_u(fd, &st);
        close(fd);
        if (st.st_type == DIR_I)
          printu("%s [DIR]   -\n", name);
        else
          printu("%s [FILE]  %d\n", name, st.st_size);
      } else {
        printu("%s [DIR]   -\n", name);
      }
    }
    closedir_u(dir_fd);
  } else {
    // regular file: print its stat info
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
      printu("ls: cannot access \"%s\"\n", path);
      printu("------------------------------\n");
      exit(1);
    }
    struct istat st;
    stat_u(fd, &st);
    close(fd);

    // extract basename from path
    const char *basename = path;
    for (int i = 0; path[i]; i++)
      if (path[i] == '/') basename = path + i + 1;

    printu("[name]               [size]    [inode]\n");
    char name[width + 1];
    memset(name, ' ', width + 1);
    name[width] = '\0';
    if (strlen(basename) < width) {
      strcpy(name, basename);
      name[strlen(basename)] = ' ';
    }
    printu("%s %d         %d\n", name, st.st_size, st.st_inum);
  }

  printu("------------------------------\n");
  exit(0);
  return 0;
}
