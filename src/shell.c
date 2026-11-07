#include "shell.h"
#include "console.h"
#include "io.h"
#include "keyboard.h"
#include "pit.h"
#include "panic.h"
#include "serial.h"
#include "string.h"
#include "fat.h"

/*
 * brash: the Asbestos SHell.
 *
 * Built-in commands only, no quoting. Commands are prefixed with "please".
 * Any error puts the shell in a state where the next command must be prefixed
 * with "sorry". The prompt makes that state obvious: its feelings are hurt
 * until the apology arrives, and it shows the current directory relative to
 * the FAT root.
 *
 * These strings are the provisional contract. Keep them in one place so they
 * are easy to change when the exact wording is settled.
 */
#define SHELL_VERSION "0.1"

#define SHELL_PROMPT_HOST "usr@brash"
#define SHELL_PROMPT_HURT "(hurt feelings)"
#define SHELL_PROMPT_UPSET "(visibly upset)"
#define SHELL_PROMPT_OFFENDED "(deeply offended)"

#define SHELL_DEBT_MAX 3

#define SHELL_PATH_MAX 64

#define SHELL_MANNERS_ERROR_PLEASE "That is not the magic word. Say \"please\"."
#define SHELL_MANNERS_ERROR_SORRY "An apology would be nice. Say \"sorry\"."
#define SHELL_ERROR_COMPOUND "I can't multitask well. Please avoid compound statements."
#define SHELL_ERROR_EMPTY "No command was given. Do you normally stare blankly at people?"
#define SHELL_ERROR_TOO_MANY "Too many arguments. I can't keep track of all of that."
#define SHELL_ERROR_UNKNOWN_START "Unknown command: "
#define SHELL_ERROR_UNKNOWN_END ". You know, you can try asking for help."

#define SHELL_LINE_MAX 64
#define SHELL_MAX_ARGS 8

/*
 * Commands are prefixed with "please" under normal circumstances. Every error
 * adds to apology_debt, so the next command must be prefixed with "sorry". A
 * successful command clears the debt. The prompt gets more upset as the debt
 * grows, so the user can tell an apology is owed without triggering another
 * error.
 */
static int apology_debt;
static int idle_count;
static char line[SHELL_LINE_MAX];

/* Current directory relative to the FAT root ("" means root). */
static char cwd[SHELL_PATH_MAX];

static const char *required_word(void) {
    return apology_debt ? "sorry" : "please";
}

static void note_error(void) {
    if (apology_debt < SHELL_DEBT_MAX) {
        apology_debt++;
    }
}

static void note_success(void) {
    apology_debt = 0;
}

static void print_prompt(void) {
    console_puts(SHELL_PROMPT_HOST);

    if (apology_debt == 1) {
        console_puts(SHELL_PROMPT_HURT);
    } else if (apology_debt == 2) {
        console_puts(SHELL_PROMPT_UPSET);
    } else if (apology_debt >= SHELL_DEBT_MAX) {
        console_puts(SHELL_PROMPT_OFFENDED);
    }

    console_puts(":~");
    if (cwd[0]) {
        console_putc('/');
        console_puts(cwd);
    }
    console_puts("> ");
}

/*
 * Turn a user-supplied path into a normalised path relative to the FAT
 * root. Absolute paths start at the root; relative paths start at cwd.
 * Handles "." and ".." and collapses repeated slashes.
 */
static int resolve_path(const char *input, char *out) {
    char combined[SHELL_PATH_MAX * 2];
    int length = 0;
    int out_length = 0;
    int i;

    if (input[0] != '/') {
        for (i = 0; cwd[i]; i++) {
            if (length + 1 >= (int)sizeof(combined)) {
                return -1;
            }
            combined[length++] = cwd[i];
        }
        if (length + 1 >= (int)sizeof(combined)) {
            return -1;
        }
        combined[length++] = '/';
    }

    for (i = 0; input[i]; i++) {
        if (length + 1 >= (int)sizeof(combined)) {
            return -1;
        }
        combined[length++] = input[i];
    }
    combined[length] = '\0';

    out[0] = '\0';
    i = 0;

    while (combined[i]) {
        int start;

        while (combined[i] == '/') {
            i++;
        }
        if (!combined[i]) {
            break;
        }

        start = i;
        while (combined[i] && combined[i] != '/') {
            i++;
        }

        int component_length = i - start;

        if (component_length == 1 && combined[start] == '.') {
            continue;
        }

        if (component_length == 2 && combined[start] == '.' &&
            combined[start + 1] == '.') {
            while (out_length > 0 && out[out_length - 1] != '/') {
                out_length--;
            }
            if (out_length > 0) {
                out_length--;
            }
            out[out_length] = '\0';
            continue;
        }

        if (out_length > 0) {
            if (out_length + 1 >= SHELL_PATH_MAX) {
                return -1;
            }
            out[out_length++] = '/';
        }

        if (out_length + component_length >= SHELL_PATH_MAX) {
            return -1;
        }

        for (int k = 0; k < component_length; k++) {
            out[out_length++] = combined[start + k];
        }
        out[out_length] = '\0';
    }

    return 0;
}

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

/*
 * Compound statements are a hard no. The shell has no parser for them and,
 * more importantly, no patience for them.
 */
static int contains_compound(const char *text) {
    for (; *text; text++) {
        switch (*text) {
            case ';':
            case '&':
            case '|':
            case '<':
            case '>':
            case '(':
            case ')':
            case '`':
                return 1;
            default:
                break;
        }
    }
    return 0;
}

static int tokenize(char *text, char *argv[], int max_args) {
    int argc = 0;
    char *p = text;

    for (;;) {
        while (is_space(*p)) {
            p++;
        }
        if (!*p) {
            return argc;
        }
        if (argc == max_args) {
            return -1;
        }

        argv[argc++] = p;
        while (*p && !is_space(*p)) {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
}

/*
 * Read a line of input with a timeout. Returns 0 when Enter is pressed,
 * -1 if no key arrives within the timeout window (partial input is erased
 * from the screen before returning).
 */
static int read_line(char *buffer, usize capacity) {
    usize length = 0;

    for (;;) {
        /* 30 seconds at 100 Hz PIT ticks. */
        char c = keyboard_read_char_timeout(3000u);

        if (c == 0) {
            /* Timeout: erase any partial input and return. */
            while (length > 0) {
                console_putc('\b');
                console_putc(' ');
                console_putc('\b');
                length--;
            }
            console_putc('\n');
            return -1;
        }

        if (c == '\n') {
            console_putc('\n');
            buffer[length] = '\0';
            return 0;
        }

        if (c == '\b') {
            if (length > 0) {
                length--;
                console_putc('\b');
            }
            continue;
        }

        if (c >= 0x20 && c < 0x7F && length + 1 < capacity) {
            buffer[length++] = c;
            console_putc(c);
        }
    }
}

static void command_help(void) {
    console_puts("Commands\n");
    console_puts("  help           show this list\n");
    console_puts("  about          identify this shell\n");
    console_puts("  uname          print OS name and version\n");
    console_puts("  echo <text>    print text\n");
    console_puts("  clear          clear the screen\n");
    console_puts("  cd [dir]       change working directory\n");
    console_puts("  pwd            print current directory\n");
    console_puts("  true           do nothing, successfully\n");
    console_puts("  false          do nothing, unsuccessfully\n");
    console_puts("  sleep <secs>   busy-wait for the given seconds\n");
    console_puts("  stat <file>    print file size\n");
    console_puts("  head <file> [n]  print first N bytes (default 256)\n");
    console_puts("  tail <file> [n]  print last N bytes (default 256)\n");
    console_puts("  wc <file>        count bytes, words, and lines\n");
    console_puts("  grep <pat> <file>  search for a pattern in a file\n");
    console_puts("  basename <path>  print the final component of a path\n");
    console_puts("  dirname <path>   print the directory portion of a path\n");
    console_puts("  mkdir <dir>      create a directory\n");
    console_puts("  touch <file>     create an empty file\n");
    console_puts("  rm <file>        delete a file\n");
    console_puts("  cp <src> <dst>   copy a file\n");
    console_puts("  mv <src> <dst>   move a file\n");
    console_puts("  ls [-l] [-a] [dir]  list directory contents\n");
    console_puts("  cat <file>...  display file contents\n");
    console_puts("  halt           stop the machine\n");
    console_puts("  panic          force a CPU fault\n");
    console_puts("  reboot         reboot the machine\n");
    console_puts("\n");
    console_puts("Prefix a command with \"please\".\n");
    console_puts("After an error, apologise with \"sorry\".\n");
    console_puts("Each error upsets the prompt more. Apologise to calm it down.\n");
}

static void command_about(void) {
    console_puts("Brash " SHELL_VERSION " - the Asbestos SHell.\n");
    console_puts("Use your manners.\n");
}

static void command_uname(int argc, char *argv[]) {
    int all = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            all = 1;
        }
    }
    if (all) {
        console_puts("asbestOS brash " SHELL_VERSION " i386\n");
    } else {
        console_puts("asbestOS\n");
    }
}

static void command_echo(int argc, char *argv[]) {
    for (int i = 2; i < argc; i++) {
        if (i > 2) {
            console_putc(' ');
        }
        console_puts(argv[i]);
    }
    console_putc('\n');
}

static int command_true(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return 0;
}

static int command_false(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return -1;
}

static int command_sleep(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: sleep <secs>\n");
        return -1;
    }

    uint32_t secs = 0;
    for (const char *p = argv[2]; *p; p++) {
        if (*p < '0' || *p > '9') {
            console_puts("sleep: invalid number\n");
            return -1;
        }
        secs = secs * 10u + (uint32_t)(*p - '0');
    }

    uint32_t start = pit_ticks_get();
    uint32_t target = start + secs * 100u;  /* PIT runs at 100 Hz */
    if (target < start) {
        target = 0xFFFFFFFFu;  /* overflow: wait effectively forever */
    }
    while (pit_ticks_get() < target) {
        cpu_pause();
    }

    return 0;
}

static int command_stat(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: stat <file>\n");
        return -1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], path) != 0) {
        console_puts("stat: path too long\n");
        return -1;
    }

    if (fat_is_dir(path)) {
        console_puts("stat: ");
        console_puts(argv[2]);
        console_puts(": Is a directory\n");
        return -1;
    }

    if (fat_open(path) != 0) {
        console_puts("stat: ");
        console_puts(argv[2]);
        console_puts(": No such file or directory\n");
        return -1;
    }

    int32_t size = fat_size();
    if (size < 0) {
        console_puts("stat: ");
        console_puts(argv[2]);
        console_puts(": Cannot read size\n");
        fat_close();
        return -1;
    }

    console_put_dec((uint32_t)size);
    console_puts(" bytes\n");
    fat_close();
    return 0;
}

static int command_head(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: head <file> [n]\n");
        return -1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], path) != 0) {
        console_puts("head: path too long\n");
        return -1;
    }

    if (fat_is_dir(path)) {
        console_puts("head: ");
        console_puts(argv[2]);
        console_puts(": Is a directory\n");
        return -1;
    }

    if (fat_open(path) != 0) {
        console_puts("head: ");
        console_puts(argv[2]);
        console_puts(": No such file or directory\n");
        return -1;
    }

    uint32_t want = 256u;
    if (argc >= 4) {
        want = 0;
        for (const char *p = argv[3]; *p; p++) {
            if (*p < '0' || *p > '9') {
                console_puts("head: invalid count\n");
                fat_close();
                return -1;
            }
            want = want * 10u + (uint32_t)(*p - '0');
        }
    }

    int32_t size = fat_size();
    if (size < 0) {
        console_puts("head: ");
        console_puts(argv[2]);
        console_puts(": Cannot read size\n");
        fat_close();
        return -1;
    }

    if (want > (uint32_t)size) {
        want = (uint32_t)size;
    }

    char buf[256];
    int32_t n = fat_read(buf, want);
    if (n <= 0) {
        fat_close();
        return -1;
    }

    for (int32_t i = 0; i < n; i++) {
        console_putc(buf[i]);
    }
    console_putc('\n');

    fat_close();
    return 0;
}

static int command_tail(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: tail <file> [n]\n");
        return -1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], path) != 0) {
        console_puts("tail: path too long\n");
        return -1;
    }

    if (fat_is_dir(path)) {
        console_puts("tail: ");
        console_puts(argv[2]);
        console_puts(": Is a directory\n");
        return -1;
    }

    if (fat_open(path) != 0) {
        console_puts("tail: ");
        console_puts(argv[2]);
        console_puts(": No such file or directory\n");
        return -1;
    }

    int32_t size = fat_size();
    if (size < 0) {
        console_puts("tail: ");
        console_puts(argv[2]);
        console_puts(": Cannot read size\n");
        fat_close();
        return -1;
    }

    uint32_t want = 256u;
    if (argc >= 4) {
        want = 0;
        for (const char *p = argv[3]; *p; p++) {
            if (*p < '0' || *p > '9') {
                console_puts("tail: invalid count\n");
                fat_close();
                return -1;
            }
            want = want * 10u + (uint32_t)(*p - '0');
        }
    }

    if (want > (uint32_t)size) {
        want = (uint32_t)size;
    }

    char buf[512];
    if ((uint32_t)size > sizeof(buf)) {
        console_puts("tail: file too large to buffer\n");
        fat_close();
        return -1;
    }

    int32_t n = fat_read(buf, (uint32_t)size);
    if (n <= 0) {
        fat_close();
        return -1;
    }

    uint32_t skip = (uint32_t)n - want;
    for (uint32_t i = skip; i < (uint32_t)n; i++) {
        console_putc(buf[i]);
    }
    console_putc('\n');

    fat_close();
    return 0;
}

static int command_wc(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: wc <file>\n");
        return -1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], path) != 0) {
        console_puts("wc: path too long\n");
        return -1;
    }

    if (fat_is_dir(path)) {
        console_puts("wc: ");
        console_puts(argv[2]);
        console_puts(": Is a directory\n");
        return -1;
    }

    if (fat_open(path) != 0) {
        console_puts("wc: ");
        console_puts(argv[2]);
        console_puts(": No such file or directory\n");
        return -1;
    }

    int32_t size = fat_size();
    if (size < 0) {
        console_puts("wc: ");
        console_puts(argv[2]);
        console_puts(": Cannot read size\n");
        fat_close();
        return -1;
    }

    char buf[512];
    if ((uint32_t)size > sizeof(buf)) {
        console_puts("wc: file too large to buffer\n");
        fat_close();
        return -1;
    }

    int32_t n = fat_read(buf, (uint32_t)size);
    if (n <= 0) {
        fat_close();
        return -1;
    }

    uint32_t bytes = (uint32_t)n;
    uint32_t words = 0;
    uint32_t lines = 0;
    int in_word = 0;

    for (int32_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            lines++;
            if (in_word) {
                words++;
                in_word = 0;
            }
        } else if (c == ' ' || c == '\t' || c == '\r') {
            if (in_word) {
                words++;
                in_word = 0;
            }
        } else {
            in_word = 1;
        }
    }
    if (in_word) {
        words++;
    }

    console_put_dec(bytes);
    console_puts(" bytes, ");
    console_put_dec(words);
    console_puts(" words, ");
    console_put_dec(lines);
    console_puts(" lines\n");

    fat_close();
    return 0;
}

static int command_grep(int argc, char *argv[]) {
    if (argc < 4) {
        console_puts("Usage: grep <pat> <file>\n");
        return -1;
    }

    const char *pat = argv[2];
    uint32_t pat_len = 0;
    for (const char *p = pat; *p; p++) {
        pat_len++;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[3], path) != 0) {
        console_puts("grep: path too long\n");
        return -1;
    }

    if (fat_is_dir(path)) {
        console_puts("grep: ");
        console_puts(argv[3]);
        console_puts(": Is a directory\n");
        return -1;
    }

    if (fat_open(path) != 0) {
        console_puts("grep: ");
        console_puts(argv[3]);
        console_puts(": No such file or directory\n");
        return -1;
    }

    int32_t size = fat_size();
    if (size < 0) {
        console_puts("grep: ");
        console_puts(argv[3]);
        console_puts(": Cannot read size\n");
        fat_close();
        return -1;
    }

    char buf[512];
    if ((uint32_t)size > sizeof(buf)) {
        console_puts("grep: file too large to buffer\n");
        fat_close();
        return -1;
    }

    int32_t n = fat_read(buf, (uint32_t)size);
    if (n <= 0) {
        fat_close();
        return -1;
    }

    int found = 0;
    for (int32_t i = 0; i + (int32_t)pat_len <= n; i++) {
        int match = 1;
        for (uint32_t j = 0; j < pat_len; j++) {
            if (buf[i + j] != pat[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            found = 1;
            /* Print from start of line to end of line */
            int32_t line_start = i;
            while (line_start > 0 && buf[line_start - 1] != '\n') {
                line_start--;
            }
            int32_t line_end = i + (int32_t)pat_len;
            while (line_end < n && buf[line_end] != '\n') {
                line_end++;
            }
            for (int32_t k = line_start; k < line_end; k++) {
                console_putc(buf[k]);
            }
            console_putc('\n');
        }
    }

    if (!found) {
        console_puts("grep: no matches\n");
        fat_close();
        return -1;
    }

    fat_close();
    return 0;
}

static int command_basename(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: basename <path>\n");
        return -1;
    }

    const char *p = argv[2];
    const char *last_slash = 0;
    for (const char *q = p; *q; q++) {
        if (*q == '/') {
            last_slash = q;
        }
    }

    if (last_slash) {
        console_puts(last_slash + 1);
    } else {
        console_puts(p);
    }
    console_putc('\n');
    return 0;
}

static int command_dirname(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: dirname <path>\n");
        return -1;
    }

    const char *p = argv[2];
    const char *last_slash = 0;
    for (const char *q = p; *q; q++) {
        if (*q == '/') {
            last_slash = q;
        }
    }

    if (!last_slash) {
        console_puts(".\n");
        return 0;
    }

    for (const char *q = p; q < last_slash; q++) {
        console_putc(*q);
    }
    console_putc('\n');
    return 0;
}

static int command_cd(int argc, char *argv[]) {
    char path[SHELL_PATH_MAX];

    if (argc < 3) {
        cwd[0] = '\0';
        return 0;
    }

    if (resolve_path(argv[2], path) != 0) {
        console_puts("cd: path too long\n");
        return -1;
    }

    if (!fat_is_dir(path)) {
        console_puts("cd: ");
        console_puts(argv[2]);
        console_puts(": No such directory\n");
        return -1;
    }

    int i;
    for (i = 0; path[i] && i < SHELL_PATH_MAX - 1; i++) {
        cwd[i] = path[i];
    }
    cwd[i] = '\0';
    return 0;
}

static void command_pwd(void) {
    console_putc('/');
    if (cwd[0]) {
        console_puts(cwd);
    }
    console_putc('\n');
}

static int command_mkdir(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: mkdir <dir>\n");
        return -1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], path) != 0) {
        console_puts("mkdir: path too long\n");
        return -1;
    }

    /* Allocate a cluster for the new directory. */
    uint32_t new_cluster = fat_alloc_cluster();
    if (new_cluster == 0xFFFFFFFFu) {
        console_puts("mkdir: disk full\n");
        return -1;
    }

    /* Create the directory entry in the parent. */
    if (fat_create_dir_entry(path, ATTR_DIRECTORY, new_cluster) != 0) {
        /* Roll back the cluster allocation. */
        fat_free_cluster_chain(new_cluster);
        console_puts("mkdir: ");
        console_puts(argv[2]);
        console_puts(": cannot create directory\n");
        return -1;
    }

    /* Initialize . and .. entries in the new directory.
     * The parent cluster is embedded in `path`: walk back to find it.
     * For simplicity, resolve the parent path separately. */
    char parent_path[SHELL_PATH_MAX];
    int i;
    /* Find the last slash to separate parent from name */
    int last_slash = -1;
    for (i = 0; path[i]; i++) {
        if (path[i] == '/') {
            last_slash = i;
        }
    }
    if (last_slash < 0) {
        /* Parent is root */
        if (fat_init_dir(new_cluster, 0) != 0) {
            fat_free_cluster_chain(new_cluster);
            console_puts("mkdir: failed to initialize directory\n");
            return -1;
        }
    } else {
        /* Parent path is everything before the last slash */
        for (i = 0; i < last_slash; i++) {
            parent_path[i] = path[i];
        }
        parent_path[last_slash] = '\0';

        uint32_t parent_cluster = 0;
        if (parent_path[0] != '\0') {
            /* Resolve parent path to a cluster number */
            if (fat_get_dir_cluster(parent_path, &parent_cluster) != 0) {
                fat_free_cluster_chain(new_cluster);
                console_puts("mkdir: parent directory not found\n");
                return -1;
            }
        }

        if (fat_init_dir(new_cluster, parent_cluster) != 0) {
            fat_free_cluster_chain(new_cluster);
            console_puts("mkdir: failed to initialize directory\n");
            return -1;
        }
    }

    console_puts("mkdir: created ");
    console_puts(argv[2]);
    console_putc('\n');
    return 0;
}

static int command_touch(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: touch <file>\n");
        return -1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], path) != 0) {
        console_puts("touch: path too long\n");
        return -1;
    }

    /* Empty files use cluster 0 (no data clusters). */
    if (fat_create_dir_entry(path, 0, 0) != 0) {
        console_puts("touch: ");
        console_puts(argv[2]);
        console_puts(": cannot create file\n");
        return -1;
    }

    console_puts("touch: created ");
    console_puts(argv[2]);
    console_putc('\n');
    return 0;
}

static int command_rm(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: rm <file>\n");
        return -1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], path) != 0) {
        console_puts("rm: path too long\n");
        return -1;
    }

    if (!fat_is_dir(path)) {
        /* Not a directory: plain file deletion. */
        if (fat_delete_dir_entry(path) != 0) {
            console_puts("rm: ");
            console_puts(argv[2]);
            console_puts(": No such file\n");
            return -1;
        }
        console_puts("rm: deleted ");
        console_puts(argv[2]);
        console_putc('\n');
        return 0;
    }

    console_puts("rm: ");
    console_puts(argv[2]);
    console_puts(": Is a directory\n");
    return -1;
}

static int command_cp(int argc, char *argv[]) {
    if (argc < 4) {
        console_puts("Usage: cp <src> <dst>\n");
        return -1;
    }

    char src_path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], src_path) != 0) {
        console_puts("cp: source path too long\n");
        return -1;
    }

    char dst_path[SHELL_PATH_MAX];
    if (resolve_path(argv[3], dst_path) != 0) {
        console_puts("cp: destination path too long\n");
        return -1;
    }

    /* Source must exist and not be a directory */
    if (fat_is_dir(src_path)) {
        console_puts("cp: ");
        console_puts(argv[2]);
        console_puts(": Is a directory\n");
        return -1;
    }

    /* Open source file and read its content */
    if (fat_open(src_path) != 0) {
        console_puts("cp: ");
        console_puts(argv[2]);
        console_puts(": No such file\n");
        return -1;
    }

    int32_t size = fat_size();
    if (size < 0) {
        console_puts("cp: ");
        console_puts(argv[2]);
        console_puts(": Cannot read size\n");
        fat_close();
        return -1;
    }

    char buf[512];
    if ((uint32_t)size > sizeof(buf)) {
        console_puts("cp: ");
        console_puts(argv[2]);
        console_puts(": File too large to buffer\n");
        fat_close();
        return -1;
    }

    int32_t n = fat_read(buf, (uint32_t)size);
    fat_close();
    if (n <= 0) {
        console_puts("cp: ");
        console_puts(argv[2]);
        console_puts(": Read error\n");
        return -1;
    }

    /* Create destination entry */
    if (fat_create_dir_entry(dst_path, 0, 0) != 0) {
        console_puts("cp: ");
        console_puts(argv[3]);
        console_puts(": cannot create destination\n");
        return -1;
    }

    /* Write content to destination */
    if (fat_write_file(dst_path, buf, (uint32_t)n) != 0) {
        console_puts("cp: ");
        console_puts(argv[3]);
        console_puts(": Write error\n");
        return -1;
    }

    console_puts("cp: copied ");
    console_puts(argv[2]);
    console_puts(" to ");
    console_puts(argv[3]);
    console_putc('\n');
    return 0;
}

static int command_mv(int argc, char *argv[]) {
    if (argc < 4) {
        console_puts("Usage: mv <src> <dst>\n");
        return -1;
    }

    char src_path[SHELL_PATH_MAX];
    if (resolve_path(argv[2], src_path) != 0) {
        console_puts("mv: source path too long\n");
        return -1;
    }

    char dst_path[SHELL_PATH_MAX];
    if (resolve_path(argv[3], dst_path) != 0) {
        console_puts("mv: destination path too long\n");
        return -1;
    }

    /* Source must exist and not be a directory */
    if (fat_is_dir(src_path)) {
        console_puts("mv: ");
        console_puts(argv[2]);
        console_puts(": Is a directory\n");
        return -1;
    }

    /* Copy source to destination */
    if (fat_open(src_path) != 0) {
        console_puts("mv: ");
        console_puts(argv[2]);
        console_puts(": No such file\n");
        return -1;
    }

    int32_t size = fat_size();
    if (size < 0) {
        console_puts("mv: ");
        console_puts(argv[2]);
        console_puts(": Cannot read size\n");
        fat_close();
        return -1;
    }

    char buf[512];
    if ((uint32_t)size > sizeof(buf)) {
        console_puts("mv: ");
        console_puts(argv[2]);
        console_puts(": File too large to buffer\n");
        fat_close();
        return -1;
    }

    int32_t n = fat_read(buf, (uint32_t)size);
    fat_close();
    if (n <= 0) {
        console_puts("mv: ");
        console_puts(argv[2]);
        console_puts(": Read error\n");
        return -1;
    }

    /* Create destination entry */
    if (fat_create_dir_entry(dst_path, 0, 0) != 0) {
        console_puts("mv: ");
        console_puts(argv[3]);
        console_puts(": cannot create destination\n");
        return -1;
    }

    /* Write content to destination */
    if (fat_write_file(dst_path, buf, (uint32_t)n) != 0) {
        console_puts("mv: ");
        console_puts(argv[3]);
        console_puts(": Write error\n");
        return -1;
    }

    /* Remove source entry */
    if (fat_delete_dir_entry(src_path) != 0) {
        console_puts("mv: ");
        console_puts(argv[2]);
        console_puts(": failed to remove source\n");
        return -1;
    }

    console_puts("mv: moved ");
    console_puts(argv[2]);
    console_puts(" to ");
    console_puts(argv[3]);
    console_putc('\n');
    return 0;
}

static int command_ls(int argc, char *argv[]) {
    int long_format = 0;
    int show_all = 0;
    const char *given = "";
    char path[SHELL_PATH_MAX];

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            long_format = 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else {
            given = argv[i];
        }
    }

    if (given[0]) {
        if (resolve_path(given, path) != 0) {
            console_puts("ls: path too long\n");
            return -1;
        }
    } else {
        int i;
        for (i = 0; cwd[i] && i < SHELL_PATH_MAX - 1; i++) {
            path[i] = cwd[i];
        }
        path[i] = '\0';
    }

    if (!fat_is_dir(path)) {
        console_puts("ls: ");
        console_puts(given[0] ? given : ".");
        console_puts(": No such directory\n");
        return -1;
    }

    if (show_all) {
        if (long_format) {
            console_puts("        <DIR> .\n");
            console_puts("        <DIR> ..\n");
        } else {
            console_puts(".  <DIR>\n");
            console_puts("..  <DIR>\n");
        }
    }

    return fat_list_dir(path, long_format) == 0 ? 0 : -1;
}

static int command_cat(int argc, char *argv[]) {
    if (argc < 3) {
        console_puts("Usage: cat <file>...\n");
        return -1;
    }

    int failed = 0;

    for (int f = 2; f < argc; f++) {
        char path[SHELL_PATH_MAX];

        if (resolve_path(argv[f], path) != 0) {
            console_puts("cat: ");
            console_puts(argv[f]);
            console_puts(": Path too long\n");
            failed = 1;
            continue;
        }

        if (fat_is_dir(path)) {
            console_puts("cat: ");
            console_puts(argv[f]);
            console_puts(": Is a directory\n");
            failed = 1;
            continue;
        }

        if (fat_open(path) != 0) {
            console_puts("cat: ");
            console_puts(argv[f]);
            console_puts(": No such file or directory\n");
            failed = 1;
            continue;
        }

        int32_t size = fat_size();
        if (size < 0) {
            console_puts("cat: ");
            console_puts(argv[f]);
            console_puts(": Cannot read size\n");
            fat_close();
            failed = 1;
            continue;
        }

        char buf[256];
        int32_t total = 0;
        while (total < size) {
            uint32_t want = (uint32_t)(size - total);
            if (want > (uint32_t)sizeof(buf)) {
                want = (uint32_t)sizeof(buf);
            }
            int32_t n = fat_read(buf, want);
            if (n <= 0) {
                break;
            }
            for (int32_t i = 0; i < n; i++) {
                console_putc(buf[i]);
            }
            total += n;
        }

        fat_close();
    }

    return failed ? -1 : 0;
}

static void command_halt(void) {
    console_puts("Stopping. Do not make me regret this.\n");
    serial_puts("brash: halted by user\n");

    for (;;) {
        cpu_halt();
    }
}

/*
 * Force a real CPU exception rather than calling panic() directly, so this
 * exercises the same path as an accidental fault.
 */
static void command_panic(void) {
    serial_puts("brash: panic requested\n");

    __asm__ __volatile__(
        "xor %%edx, %%edx\n\t"
        "xor %%ecx, %%ecx\n\t"
        "mov $1, %%eax\n\t"
        "div %%ecx\n\t"
        : : : "eax", "ecx", "edx", "cc");

    /* The divide above raises #DE. If it somehow returns, stop anyway. */
    panic("panic command failed to fault", 0);
}

/*
 * Send 0xFE (the 8042 reboot signal) to 0x64
 * This pulses the CPU's reset line, forcing a reboot.
 */
static void command_reboot(void) {
    cpu_cli();
    /* Give controller a (limited) chance to be ready */
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) {
            /* Send the reboot signal to 0x64 */
            outb(0x64, 0xFE);
            break;
        }
        io_wait();
    }

    /* Fallback in case the machine ignores 8042 signals */
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    /* Last resort: halt and do not return to the shell. */
    for (;;) {
        cpu_halt();
    }
}

static void dispatch(char *text) {
    char *argv[SHELL_MAX_ARGS];

    if (contains_compound(text)) {
        console_puts(SHELL_ERROR_COMPOUND "\n");
        note_error();
        return;
    }

    int argc = tokenize(text, argv, SHELL_MAX_ARGS);
    if (argc < 0) {
        console_puts(SHELL_ERROR_TOO_MANY "\n");
        note_error();
        return;
    }
    if (argc == 0) {
        return;
    }

    if (strcmp(argv[0], required_word()) != 0) {
        if (apology_debt) {
            console_puts(SHELL_MANNERS_ERROR_SORRY "\n");
        } else {
            console_puts(SHELL_MANNERS_ERROR_PLEASE "\n");
        }
        note_error();
        return;
    }

    if (argc < 2) {
        console_puts(SHELL_ERROR_EMPTY "\n");
        note_error();
        return;
    }

    const char *command = argv[1];
    int result = 0;

    if (strcmp(command, "help") == 0) {
        command_help();
    } else if (strcmp(command, "about") == 0) {
        command_about();
    } else if (strcmp(command, "uname") == 0) {
        command_uname(argc, argv);
    } else if (strcmp(command, "echo") == 0) {
        command_echo(argc, argv);
    } else if (strcmp(command, "clear") == 0) {
        console_clear();
    } else if (strcmp(command, "cd") == 0) {
        result = command_cd(argc, argv);
    } else if (strcmp(command, "pwd") == 0) {
        command_pwd();
    } else if (strcmp(command, "true") == 0) {
        result = command_true(argc, argv);
    } else if (strcmp(command, "false") == 0) {
        result = command_false(argc, argv);
    } else if (strcmp(command, "sleep") == 0) {
        result = command_sleep(argc, argv);
    } else if (strcmp(command, "stat") == 0) {
        result = command_stat(argc, argv);
    } else if (strcmp(command, "head") == 0) {
        result = command_head(argc, argv);
    } else if (strcmp(command, "tail") == 0) {
        result = command_tail(argc, argv);
    } else if (strcmp(command, "wc") == 0) {
        result = command_wc(argc, argv);
    } else if (strcmp(command, "grep") == 0) {
        result = command_grep(argc, argv);
    } else if (strcmp(command, "basename") == 0) {
        result = command_basename(argc, argv);
    } else if (strcmp(command, "dirname") == 0) {
        result = command_dirname(argc, argv);
    } else if (strcmp(command, "ls") == 0) {
        result = command_ls(argc, argv);
    } else if (strcmp(command, "mkdir") == 0) {
        result = command_mkdir(argc, argv);
    } else if (strcmp(command, "touch") == 0) {
        result = command_touch(argc, argv);
    } else if (strcmp(command, "rm") == 0) {
        result = command_rm(argc, argv);
    } else if (strcmp(command, "cp") == 0) {
        result = command_cp(argc, argv);
    } else if (strcmp(command, "mv") == 0) {
        result = command_mv(argc, argv);
    } else if (strcmp(command, "cat") == 0) {
        result = command_cat(argc, argv);
    } else if (strcmp(command, "halt") == 0) {
        command_halt();
    } else if (strcmp(command, "panic") == 0) {
        command_panic();
    } else if (strcmp(command, "reboot") == 0) {
        command_reboot();
    } else {
        console_puts(SHELL_ERROR_UNKNOWN_START);
        console_puts(command);
        console_puts(SHELL_ERROR_UNKNOWN_END);
        console_puts("\n");
        note_error();
        return;
    }

    if (result == 0) {
        note_success();
    } else {
        note_error();
    }
}

void shell_run(void) {

    for (;;) {
        print_prompt();

        if (read_line(line, sizeof(line)) != 0) {
            /* Input timeout: simulate pressing enter. */
            idle_count++;
            if (idle_count >= 5) {
                console_puts("Are you still there? Did you leave me?\n");
                idle_count = 0;
            }
            continue;
        }

        idle_count = 0;

        serial_puts("brash: ");
        serial_puts(line);
        serial_puts("\n");

        dispatch(line);
    }
}
