/*
 * This app starts a very simple shell and executes some simple commands.
 * The commands are stored in the hostfs_root/shellrc
 * The shell loads the file and executes the command line by line.                 
 */
#include "user_lib.h"
#include "string.h"
#include "util/types.h"

#define HISTORY_MAX_ITEMS 32
#define HISTORY_LINE_MAX 96
#define ENV_MAX_ITEMS 32
#define ENV_NAME_MAX 32
#define ENV_VALUE_MAX 64

typedef struct env_item_t {
  char name[ENV_NAME_MAX];
  char value[ENV_VALUE_MAX];
} env_item;

typedef struct history_item_t {
  char line[HISTORY_LINE_MAX];
} history_item;

/* Below are helper functions for history management */
static void history_append(history_item *history, int *history_count, const char *command, const char *para, int bg) {
  if (!history || !history_count || !command || command[0] == '\0')
    return;

  if (*history_count == HISTORY_MAX_ITEMS) {
    for (int i = 1; i < HISTORY_MAX_ITEMS; i++)
      history[i - 1] = history[i];
    (*history_count)--;
  }

  int pos = 0;
  for (int i = 0; command[i] != '\0' && pos < HISTORY_LINE_MAX - 1; i++)
    history[*history_count].line[pos++] = command[i];
  if (para[0] != '\0' && pos < HISTORY_LINE_MAX - 1) {
    history[*history_count].line[pos++] = ' ';
    for (int i = 0; para[i] != '\0' && pos < HISTORY_LINE_MAX - 1; i++)
      history[*history_count].line[pos++] = para[i];
  }
  if (bg && pos < HISTORY_LINE_MAX - 2) {
    history[*history_count].line[pos++] = ' ';
    history[*history_count].line[pos++] = '&';
  }
  history[*history_count].line[pos] = '\0';
  (*history_count)++;
}

static void history_print_all(history_item *history, int history_count) {
  if (!history)
    return;
  for (int i = 0; i < history_count; i++)
    printu("%5d  %s\n", i + 1, history[i].line);
}

/* Below are helper functions for environment variable management */
static void copy_with_limit(char *dst, const char *src, int max_len) {
  if (!dst || !src || max_len <= 0)
    return;
  int i = 0;
  for (; src[i] != '\0' && i < max_len - 1; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static int env_find(env_item *envs, int env_count, const char *name) {
  if (!envs || !name)
    return -1;
  for (int i = 0; i < env_count; i++) {
    if (strcmp(envs[i].name, name) == 0)
      return i;
  }
  return -1;
}

static void env_set(env_item *envs, int *env_count, const char *name, const char *value) {
  if (!envs || !env_count || !name || !value || name[0] == '\0')
    return;

  int idx = env_find(envs, *env_count, name);
  if (idx < 0) {
    if (*env_count == ENV_MAX_ITEMS) {
      for (int i = 1; i < ENV_MAX_ITEMS; i++)
        envs[i - 1] = envs[i];
      (*env_count)--;
    }
    idx = *env_count;
    (*env_count)++;
  }
  copy_with_limit(envs[idx].name, name, ENV_NAME_MAX);
  copy_with_limit(envs[idx].value, value, ENV_VALUE_MAX);
}

static void env_print_all(env_item *envs, int env_count) {
  if (!envs)
    return;
  for (int i = 0; i < env_count; i++)
    printu("%s=%s\n", envs[i].name, envs[i].value);
}

static void print_command_banner(const char *command, const char *para) {
  printu("Next command: %s", command);
  if (para && para[0] != '\0')
    printu(" %s", para);
  printu("\n\n");
  printu("==========Command Start============\n\n");
}

static void print_command_end(void) {
  printu("==========Command End============\n\n");
}

static int parse_set_assignment(char **tsave, char *set_name, char *set_value, int *bg) {
  char *tok = strtok_r(NULL, " \t", tsave);
  if (tok == NULL)
    return -1;
  copy_with_limit(set_name, tok, ENV_NAME_MAX);

  tok = strtok_r(NULL, " \t", tsave);
  if (tok == NULL || strcmp(tok, "=") != 0)
    return -1;

  tok = strtok_r(NULL, " \t", tsave);
  if (tok == NULL)
    return -1;
  copy_with_limit(set_value, tok, ENV_VALUE_MAX);

  tok = strtok_r(NULL, " \t", tsave);
  if (tok == NULL)
    return 0;
  if (strcmp(tok, "&") == 0) {
    *bg = 1;
    tok = strtok_r(NULL, " \t", tsave);
    if (tok == NULL)
      return 0;
  }
  return -1;
}

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
static int parse_next(char *buf, char *command, char *para, int *bg, char *set_name, char *set_value) {
  static char *lsave;  /* outer state: tracks position across lines */

  /* 1) Advance to the next non-empty line from shellrc. */
  char *line;
  do {
    line = strtok_r(buf, "\n", &lsave);
    buf = NULL;  /* after first call, always pass NULL to strtok_r */
  } while (line != NULL && line[0] == '\0');

  if (line == NULL)
    return 0;

  /* 2) Parse command token and initialize outputs for this line. */
  char *tsave;
  char *tok = strtok_r(line, " \t", &tsave);
  if (tok == NULL)
    return 0;
  strcpy(command, tok);
  *bg = 0;
  para[0] = '\0';
  set_name[0] = '\0';
  set_value[0] = '\0';

  /* 3) "set" has dedicated syntax: set <name> = <value>. */
  if (strcmp(command, "set") == 0) {
    if (parse_set_assignment(&tsave, set_name, set_value, bg) == 0) {
      strcpy(para, set_name);
      return 1;
    }
    return 1;
  }

  /* 4) Generic path: optional parameter and optional background '&'. */
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
  int nread;
  int MAXBUF = 1024;
  char buf[MAXBUF];

  /* 1) Load all shell commands from /shellrc into memory. */
  fd = open("/shellrc", O_RDONLY);

  nread = read_u(fd, buf, MAXBUF - 1);
  close(fd);
  if (nread < 0) {
    printu("read /shellrc failed\n");
    exit(-1);
    return -1;
  }
  buf[nread] = '\0';

  /* 2) Initialize runtime state for history/env and parse buffers. */
  history_item *history = (history_item *)naive_malloc();
  int history_count = 0;
  env_item *envs = (env_item *)naive_malloc();
  int env_count = 0;

  char *command = naive_malloc();
  char *para = naive_malloc();
  char *set_name = naive_malloc();
  char *set_value = naive_malloc();
  int bg;
  int first = 1;

  /* 3) Parse and execute shellrc line by line. */
  while (1)
  {
    if (!parse_next(first ? buf : NULL, command, para, &bg, set_name, set_value))
      break;
    first = 0;

    if (strcmp(command, "END") == 0)
      break;

    history_append(history, &history_count, command, para, bg);

    /* 4) Builtin: history printing. */
    if (strcmp(command, "/bin/app_history") == 0 || strcmp(command, "app_history") == 0) {
      print_command_banner(command, para);
      history_print_all(history, history_count);
      print_command_end();
      continue;
    }

    /* 5) Builtin: set environment variable. */
    if (strcmp(command, "set") == 0) {
      if (set_name[0] != '\0' && set_value[0] != '\0')
        printu("Next command: set %s = %s\n\n", set_name, set_value);
      else
        printu("Next command: set\n\n");
      printu("==========Command Start============\n\n");

      if (set_name[0] == '\0' || set_value[0] == '\0') {
        printu("set: invalid syntax, use: set <name> = <value>\n");
      } else {
        env_set(envs, &env_count, set_name, set_value);
        printu("%s=%s\n", set_name, set_value);
      }
      print_command_end();
      continue;
    }

    /* 6) Builtin: print all environment variables. */
    if (strcmp(command, "env") == 0) {
      print_command_banner(command, para);
      env_print_all(envs, env_count);
      print_command_end();
      continue;
    }

    /* 7) External command path: fork + exec (+ optional wait). */
    print_command_banner(command, para);
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
      print_command_end();
    }
  }
  /* 8) Shell exits after END or EOF of shellrc buffer. */
  printu("\n ========== Shell End ==========\n\n");
  exit(0);
  return 0;
}
