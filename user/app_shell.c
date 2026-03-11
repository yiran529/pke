/*
 * This app starts a very simple shell and executes some simple commands.
 * The commands are stored in the hostfs_root/shellrc
 * The shell loads the file and executes the command line by line.                 
 */
#include "user_lib.h"
#include "string.h"
#include "util/types.h"

/*
 * parse_next - parse the next command from the shellrc buffer.
 *
 * Why strtok_r instead of strtok:
 *   strtok stores its position in a single internal static pointer, so only
 *   one tokenization can be active at a time.  strtok_r receives an explicit
 *   saveptr from the caller, giving each level of splitting its own state.
 *   Here we need two independent levels running at the same time:
 *     outer  — splits buf by '\n' to yield one line   (state: lsave)
 *     inner  — splits that line by ' \t' to yield tokens (state: tsave)
 *   Using strtok for both would cause the inner calls to corrupt the outer
 *   state, making the outer loop skip lines.
 *
 * Call with the original buffer pointer on the first invocation, then NULL
 * for subsequent calls (same convention as strtok).
 *
 * On return:
 *   command  - filled with the command token
 *   para     - filled with the argument token, or empty string "" if absent
 *   bg       - set to 1 if '&' was present, 0 otherwise
 *
 * Returns 1 if a token was successfully read, 0 if the buffer is exhausted.
 */
static int parse_next(char *buf, char *command, char *para, int *bg) {
  static char *lsave;  /* outer state: tracks position across lines */

  /* advance to the next non-empty line */
  char *line;
  do {
    line = strtok_r(buf, "\n", &lsave);
    buf = NULL;  /* after first call, always pass NULL to strtok_r */
  } while (line != NULL && line[0] == '\0');

  if (line == NULL)
    return 0;

  /* tokenize within this line only */
  char *tsave;
  char *tok = strtok_r(line, " \t", &tsave);
  if (tok == NULL)
    return 0;
  strcpy(command, tok);
  *bg = 0;
  para[0] = '\0';

  tok = strtok_r(NULL, " \t", &tsave);
  if (tok == NULL)
    return 1;

  if (strcmp(tok, "&") == 0) {
    *bg = 1;
    return 1;
  }

  strcpy(para, tok);

  tok = strtok_r(NULL, " \t", &tsave);
  if (tok != NULL && strcmp(tok, "&") == 0)
    *bg = 1;

  return 1;
}

int main(int argc, char *argv[]) {
  printu("\n======== Shell Start ========\n\n");
  int fd;
  int MAXBUF = 1024;
  char buf[MAXBUF];
  fd = open("/shellrc", O_RDONLY);

  read_u(fd, buf, MAXBUF);
  close(fd);
  char *command = naive_malloc();
  char *para = naive_malloc();
  int bg;
  int first = 1;
  while (1)
  {
    if (!parse_next(first ? buf : NULL, command, para, &bg))
      break;
    first = 0;

    if (strcmp(command, "END") == 0)
      break;

    printu("Next command: %s %s\n\n", command, para);
    printu("==========Command Start============\n\n");
    int pid = fork();
    if(pid == 0) {
      int ret = exec(command, para[0] != '\0' ? para : (char*)0);
      if (ret == -1)
        printu("exec failed!\n");
    }
    else
    {
      if (!bg) {
        wait(pid);
        printu("[DEBUG] pid %d finished.\n", pid);
      } else {
        printu("[DEBUG] pid %d running in background.\n", pid);
      }
      printu("==========Command End============\n\n");
    }
  }
  printu("\n ========== Shell End ==========\n\n");
  exit(0);
  return 0;
}
