/*
 * Mini shell for PKE user apps.
 * It reads /shellrc once, then parses and executes commands line by line.
 */
#include "user_lib.h"
#include "string.h"
#include "util/types.h"

#define HISTORY_MAX_ITEMS 32
#define HISTORY_LINE_MAX 96
#define ENV_MAX_ITEMS 32
#define ENV_NAME_MAX 32
#define ENV_VALUE_MAX 64
#define PIPE_TMP_PATH_MAX 64
#define PIPE_LEFT_ARG_MAX 160

typedef struct env_item_t {
  char name[ENV_NAME_MAX];
  char value[ENV_VALUE_MAX];
} env_item;

typedef struct history_item_t {
  char line[HISTORY_LINE_MAX];
} history_item;

/* ===== History helpers ===== */
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

/* ===== Environment-variable helpers ===== */
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

/* Print command wrapper with a unified shell output format. */
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

/* ===== Pseudo-pipe helpers (temp-file relay) ===== */
static void append_uint(char *dst, int *pos, int max_len, int value) {
  char rev[16];
  int n = 0;
  if (value == 0) {
    if (*pos < max_len - 1)
      dst[(*pos)++] = '0';
    return;
  }
  while (value > 0 && n < (int)sizeof(rev)) {
    rev[n++] = '0' + (value % 10);
    value /= 10;
  }
  while (n > 0 && *pos < max_len - 1)
    dst[(*pos)++] = rev[--n];
}

static void build_pipe_tmp_path(char *path, int max_len, int seq) {
  const char *prefix = "/RAMDISK0/.pipe_";
  int pos = 0;
  for (int i = 0; prefix[i] != '\0' && pos < max_len - 1; i++)
    path[pos++] = prefix[i];
  append_uint(path, &pos, max_len, seq);
  path[pos] = '\0';
}

static int build_pipe_left_arg(char *dst, int max_len, const char *left_para, const char *tmp_path) {
  int pos = 0;
  if (!left_para || !tmp_path || left_para[0] == '\0')
    return -1;
  for (int i = 0; left_para[i] != '\0' && pos < max_len - 1; i++)
    dst[pos++] = left_para[i];
  if (pos >= max_len - 3)
    return -1;
  dst[pos++] = ':';
  dst[pos++] = ':';
  for (int i = 0; tmp_path[i] != '\0' && pos < max_len - 1; i++)
    dst[pos++] = tmp_path[i];
  if (pos >= max_len)
    return -1;
  dst[pos] = '\0';
  return 0;
}


/*
 * Parse one command line from /shellrc.
 * - outer strtok_r: split by '\n' (one line each time)
 * - inner strtok_r: split one line by spaces/tabs
 * Returns 1 when one command is parsed, 0 when no more lines exist.
 */
static int parse_next(char *buf, char *command, char *para, int *bg, char *set_name, char *set_value,
                      int *is_pipe, char *pipe_right_command, char *pipe_right_para) {
  static char *lsave;  /* outer state: tracks position across lines */

  /* 1) Advance to the next non-empty line from shellrc. */
  char *line;
  do {
    line = strtok_r(buf, "\n", &lsave);
    buf = NULL;  /* after first call, always pass NULL to strtok_r */
  } while (line != NULL && line[0] == '\0');

  if (line == NULL)
    return 0;

  /* 2) Parse command token and reset all output fields for this round. */
  char *tsave;
  char *tok = strtok_r(line, " \t", &tsave);
  if (tok == NULL)
    return 0;
  strcpy(command, tok);
  *bg = 0;
  *is_pipe = 0;
  para[0] = '\0';
  set_name[0] = '\0';
  set_value[0] = '\0';
  pipe_right_command[0] = '\0';
  pipe_right_para[0] = '\0';

  /* 3) Handle dedicated "set <name> = <value>" syntax path. */
  if (strcmp(command, "set") == 0) {
    if (parse_set_assignment(&tsave, set_name, set_value, bg) == 0) {
      strcpy(para, set_name);
      return 1;
    }
    return 1;
  }

  /* 4) Generic syntax path: [arg] [| right_cmd [right_arg]] [&]. */
  tok = strtok_r(NULL, " \t", &tsave);
  if (tok == NULL)
    return 1;

  /* Case A: command starts directly with a pipe, e.g., cmd | right. */
  if (strcmp(tok, "|") == 0) {
    *is_pipe = 1;
    tok = strtok_r(NULL, " \t", &tsave);
    if (tok == NULL)
      return 1;
    strcpy(pipe_right_command, tok);

    tok = strtok_r(NULL, " \t", &tsave);
    if (tok == NULL)
      return 1;
    if (strcmp(tok, "&") == 0) {
      *bg = 1;
      return 1;
    }
    strcpy(pipe_right_para, tok);

    tok = strtok_r(NULL, " \t", &tsave);
    if (tok != NULL && strcmp(tok, "&") == 0)
      *bg = 1;
    return 1;
  }

  /* Case B: no argument, only background marker. */
  if (strcmp(tok, "&") == 0) {
    *bg = 1;
    return 1;
  }

  /* Case C: first normal argument. */
  strcpy(para, tok);

  tok = strtok_r(NULL, " \t", &tsave);
  if (tok == NULL)
    return 1;

  /* Case D: argument followed by background marker. */
  if (strcmp(tok, "&") == 0) {
    *bg = 1;
    return 1;
  }

  /* Case E: argument followed by pseudo-pipe suffix. */
  if (strcmp(tok, "|") == 0) {
    *is_pipe = 1;
    tok = strtok_r(NULL, " \t", &tsave);
    if (tok == NULL)
      return 1;
    strcpy(pipe_right_command, tok);

    tok = strtok_r(NULL, " \t", &tsave);
    if (tok == NULL)
      return 1;
    if (strcmp(tok, "&") == 0) {
      *bg = 1;
      return 1;
    }
    strcpy(pipe_right_para, tok);

    tok = strtok_r(NULL, " \t", &tsave);
    if (tok != NULL && strcmp(tok, "&") == 0)
      *bg = 1;
    return 1;
  }

  return 1;
}

int main(int argc, char *argv[]) {
  printu("\n======== Shell Start ========\n\n");
  int fd;
  int nread;
  int MAXBUF = 1024;
  char buf[MAXBUF];

  /* 1) Load all shell commands from /shellrc into memory. */
  fd = open("/shellrc_pressure", O_RDONLY);

  nread = read_u(fd, buf, MAXBUF - 1);
  close(fd);
  if (nread < 0) {
    printu("read /shellrc_pressure failed\n");
    exit(-1);
    return -1;
  }
  buf[nread] = '\0';

  /* 2) Prepare runtime state: history/env storage and parse buffers. */
  history_item *history = (history_item *)naive_malloc();
  int history_count = 0;
  env_item *envs = (env_item *)naive_malloc();
  int env_count = 0;

  char *command = naive_malloc();
  char *para = naive_malloc();
  char *set_name = naive_malloc();
  char *set_value = naive_malloc();
  char *pipe_right_command = naive_malloc();
  char *pipe_right_para = naive_malloc();
  int is_pipe = 0;
  int bg;
  int first = 1;
  int pipe_seq = 0;

  /* 3) Main loop: parse one command, then dispatch by command type. */
  while (1)
  {
    /* 3.1 Fetch next command from buffer. */
    if (!parse_next(first ? buf : NULL, command, para, &bg, set_name, set_value,
                    &is_pipe, pipe_right_command, pipe_right_para))
      break;
    first = 0;

    /* 3.2 Stop on END marker. */
    if (strcmp(command, "END") == 0)
      break;

    /* 3.3 Record command into history first. */
    history_append(history, &history_count, command, para, bg);

    /* 3.4 Pseudo-pipe path: run left then right via temp file. */
    if (is_pipe) {
      char pipe_tmp_path[PIPE_TMP_PATH_MAX];
      char pipe_left_arg[PIPE_LEFT_ARG_MAX];
      if (pipe_right_command[0] == '\0' || para[0] == '\0') {
        printu("Next command: %s %s | %s\n\n", command, para, pipe_right_command);
        printu("==========Command Start============\n\n");
        printu("pipe: invalid syntax, use: <cmd1> <arg> | <cmd2>\n");
        print_command_end();
        continue;
      }

      build_pipe_tmp_path(pipe_tmp_path, PIPE_TMP_PATH_MAX, pipe_seq++);
      if (build_pipe_left_arg(pipe_left_arg, PIPE_LEFT_ARG_MAX, para, pipe_tmp_path) != 0) {
        printu("Next command: %s %s | %s\n\n", command, para, pipe_right_command);
        printu("==========Command Start============\n\n");
        printu("pipe: failed to build left command argument.\n");
        print_command_end();
        continue;
      }

      if (bg)
        printu("pipe: background mode is ignored in pseudo pipe.\n");

      printu("Next command: %s %s | %s", command, para, pipe_right_command);
      if (pipe_right_para[0] != '\0')
        printu(" %s", pipe_right_para);
      printu("\n\n");
      printu("==========Command Start============\n\n");

      if (pipe_right_para[0] != '\0')
        printu("[PIPE] right command extra arg ignored in temp-file mode.\n");

      printu("[PIPE] stage1: %s %s\n", command, para);
      int left_pid = fork();
      if (left_pid == 0) {
        int ret = exec(command, pipe_left_arg);
        if (ret == -1)
          printu("exec failed!\n");
      } else {
        wait(left_pid);
        printu("[DEBUG] pid %d finished.\n", left_pid);
      }

      printu("[PIPE] stage2: %s %s\n", pipe_right_command, pipe_tmp_path);
      int right_pid = fork();
      if (right_pid == 0) {
        int ret = exec(pipe_right_command, pipe_tmp_path);
        if (ret == -1)
          printu("exec failed!\n");
      } else {
        wait(right_pid);
        printu("[DEBUG] pid %d finished.\n", right_pid);
      }

      unlink_u(pipe_tmp_path);
      print_command_end();
      continue;
    }

    /* 3.5 Builtin: print command history. */
    if (strcmp(command, "/bin/app_history") == 0 || strcmp(command, "app_history") == 0) {
      print_command_banner(command, para);
      history_print_all(history, history_count);
      print_command_end();
      continue;
    }

    /* 3.6 Builtin: set environment variable. */
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

    /* 3.7 Builtin: print all environment variables. */
    if (strcmp(command, "env") == 0) {
      print_command_banner(command, para);
      env_print_all(envs, env_count);
      print_command_end();
      continue;
    }

    /* 3.8 External command path: fork + exec (+ optional wait). */
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
  /* 4) Exit when END is seen or /shellrc is fully consumed. */
  printu("\n ========== Shell End ==========\n\n");
  exit(0);
  return 0;
}
