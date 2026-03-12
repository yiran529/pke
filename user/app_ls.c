// #include "user_lib.h"
// #include "util/string.h"
// #include "util/types.h"

// int main(int argc, char *argv[]) {
//   char *path = argv[0];
//   int width = 20;

//   // try to open as directory first; if that fails, treat as regular file
//   int dir_fd = opendir_u(path);
//   printu("---------- ls command -----------\n");
//   printu("ls \"%s\":\n", path);

//   if (dir_fd >= 0) {
//     printu("[name]               [type]  [size]\n");
//     struct dir dir;
//     while (readdir_u(dir_fd, &dir) == 0) {
//       // pad name to width, same technique as before
//       char name[width + 1];
//       memset(name, ' ', width + 1);
//       name[width] = '\0';
//       if (strlen(dir.name) < width) {
//         strcpy(name, dir.name);
//         name[strlen(dir.name)] = ' ';
//       }

//       // build child path to stat the entry
//       char child_path[64];
//       strcpy(child_path, path);
//       strcat(child_path, "/");
//       strcat(child_path, dir.name);

//       // try open() to get stat; directories may fail on hostfs, fall back to opendir_u
//       struct istat st;
//       int fd = open(child_path, O_RDONLY);
//       if (fd >= 0) {
//         stat_u(fd, &st);
//         close(fd);
//         if (st.st_type == DIR_I)
//           printu("%s [DIR]   -\n", name);
//         else
//           printu("%s [FILE]  %d\n", name, st.st_size);
//       } else {
//         printu("%s [DIR]   -\n", name);
//       }
//     }
//     closedir_u(dir_fd);
//   } else {
//     // regular file: print its stat info
//     int fd = open(path, O_RDONLY);
//     if (fd < 0) {
//       printu("ls: cannot access \"%s\"\n", path);
//       printu("------------------------------\n");
//       exit(1);
//     }
//     struct istat st;
//     stat_u(fd, &st);
//     close(fd);

//     // extract basename from path
//     const char *basename = path;
//     for (int i = 0; path[i]; i++)
//       if (path[i] == '/') basename = path + i + 1;

//     printu("[name]               [size]    [inode]\n");
//     char name[width + 1];
//     memset(name, ' ', width + 1);
//     name[width] = '\0';
//     if (strlen(basename) < width) {
//       strcpy(name, basename);
//       name[strlen(basename)] = ' ';
//     }
//     printu("%s %d         %d\n", name, st.st_size, st.st_inum);
//   }

//   printu("------------------------------\n");
//   exit(0);
//   return 0;
// }

#include "user_lib.h"
#include "util/string.h"
#include "util/snprintf.h"
#include "util/types.h"

static int split_pipe_arg(const char *arg, char *path, int path_max, char *out_path, int out_path_max) {
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
  for (int i = 0; i < sep && pos < path_max - 1; i++)
    path[pos++] = arg[i];
  path[pos] = '\0';

  pos = 0;
  for (int i = sep + 2; arg[i] != '\0' && pos < out_path_max - 1; i++)
    out_path[pos++] = arg[i];
  out_path[pos] = '\0';

  if (path[0] == '\0' || out_path[0] == '\0')
    return -1;
  return 0;
}

static void ls_output(int out_fd, const char *fmt, ...) {
  char buf[256];
  va_list vl;
  va_start(vl, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, vl);
  va_end(vl);
  if (n <= 0)
    return;

  int len = n;
  if (len > (int)sizeof(buf) - 1)
    len = sizeof(buf) - 1;

  if (out_fd >= 0)
    write_u(out_fd, buf, len);
  else
    printu("%s", buf);
}

int main(int argc, char *argv[]) {
  if (!argv || !argv[0] || argv[0][0] == '\0') {
    printu("ls: missing path\n");
    exit(1);
    return 1;
  }

  char arg_path[96];
  char out_path[96];
  int out_fd = -1;
  char *path = argv[0];

  if (split_pipe_arg(argv[0], arg_path, sizeof(arg_path), out_path, sizeof(out_path)) == 0) {
    path = arg_path;
    out_fd = open(out_path, O_RDWR | O_CREAT);
    if (out_fd < 0) {
      printu("ls: cannot open %s\n", out_path);
      exit(1);
      return 1;
    }
  }

  int width = 20;

  // try to open as directory first; if that fails, treat as regular file
  int dir_fd = opendir_u(path);
  ls_output(out_fd, "---------- ls command -----------\n");
  ls_output(out_fd, "ls \"%s\":\n", path);

  if (dir_fd >= 0) {
    ls_output(out_fd, "[name]               [type]  [size]\n");
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

      // Detect directory first, then use open/stat for regular files.
      int child_dir_fd = opendir_u(child_path);
      if (child_dir_fd >= 0) {
        closedir_u(child_dir_fd);
        ls_output(out_fd, "%s [DIR]   -\n", name);
        continue;
      }

      struct istat st;
      int fd = open(child_path, O_RDONLY);
      if (fd >= 0) {
        stat_u(fd, &st);
        close(fd);
        ls_output(out_fd, "%s [FILE]  %d\n", name, st.st_size);
      } else {
        ls_output(out_fd, "%s [ERROR] cannot open\n", name);
      }
    }
    closedir_u(dir_fd);
  } else {
    // regular file: print its stat info
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
      ls_output(out_fd, "ls: cannot access \"%s\"\n", path);
      ls_output(out_fd, "------------------------------\n");
      if (out_fd >= 0)
        close(out_fd);
      exit(1);
      return 1;
    }
    struct istat st;
    stat_u(fd, &st);
    close(fd);

    // extract basename from path
    const char *basename = path;
    for (int i = 0; path[i]; i++)
      if (path[i] == '/') basename = path + i + 1;

    ls_output(out_fd, "[name]               [size]    [inode]\n");
    char name[width + 1];
    memset(name, ' ', width + 1);
    name[width] = '\0';
    if (strlen(basename) < width) {
      strcpy(name, basename);
      name[strlen(basename)] = ' ';
    }
    ls_output(out_fd, "%s %d         %d\n", name, st.st_size, st.st_inum);
  }

  ls_output(out_fd, "------------------------------\n");
  if (out_fd >= 0)
    close(out_fd);
  exit(0);
  return 0;
}
