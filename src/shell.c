#include "shell.h"
#include "console.h"
#include "io.h"
#include "keyboard.h"
#include "panic.h"
#include "serial.h"
#include "string.h"

/*
 * brash: the Asbestos SHell.
 *
 * First iteration. Built-in commands only, no disk, no filesystem, and no
 * quoting. Commands are prefixed with "please". Any error puts the shell in a
 * state where the next command must be prefixed with "sorry". The prompt itself
 * is always "usr@brash> ".
 *
 * These strings are the provisional contract. Keep them in one place so they
 * are easy to change when the exact wording is settled.
 */
#define SHELL_VERSION "0.1"

#define SHELL_MANNERS_ERROR_PLEASE "That is not the magic word. Say \"please\"."
#define SHELL_MANNERS_ERROR_SORRY "An apology would be nice. Say \"sorry\"."
#define SHELL_ERROR_COMPOUND "Compound statements are not permitted. I can't multitask well"
#define SHELL_ERROR_EMPTY "No command was given. Do you normally stare blankly at people?"
#define SHELL_ERROR_TOO_MANY "Too many arguments. I can't keep track of all of that."
#define SHELL_ERROR_UNKNOWN "Unknown command: "

#define SHELL_LINE_MAX 64
#define SHELL_MAX_ARGS 8

/*
 * Commands are prefixed with "please" under normal circumstances. Any error
 * sets apology_due, so the next command must be prefixed with "sorry". A
 * successful command clears it again. The prompt stays fixed either way.
 */
static int apology_due;
static char line[SHELL_LINE_MAX];

static const char *required_word(void) {
    return apology_due ? "sorry" : "please";
}

static void note_error(void) {
    apology_due = 1;
}

static void note_success(void) {
    apology_due = 0;
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

static void read_line(char *buffer, usize capacity) {
    usize length = 0;

    for (;;) {
        char c = keyboard_poll();

        if (!c) {
            cpu_pause();
            continue;
        }

        if (c == '\n') {
            console_putc('\n');
            buffer[length] = '\0';
            return;
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
    console_puts("  echo <text>    print text\n");
    console_puts("  clear          clear the screen\n");
    console_puts("  halt           stop the machine\n");
    console_puts("  panic          force a CPU fault\n");
    console_puts("\n");
    console_puts("Prefix a command with \"please\".\n");
    console_puts("After an error, apologise with \"sorry\".\n");
}

static void command_about(void) {
    console_puts("Brash " SHELL_VERSION " - the Asbestos SHell.\n");
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
        if (apology_due) {
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

    if (strcmp(command, "help") == 0) {
        command_help();
    } else if (strcmp(command, "about") == 0) {
        command_about();
    } else if (strcmp(command, "echo") == 0) {
        command_echo(argc, argv);
    } else if (strcmp(command, "clear") == 0) {
        console_clear();
    } else if (strcmp(command, "halt") == 0) {
        command_halt();
    } else if (strcmp(command, "panic") == 0) {
        command_panic();
    } else {
        console_puts(SHELL_ERROR_UNKNOWN);
        console_puts(command);
        console_puts("\n");
        note_error();
        return;
    }

    note_success();
}

void shell_run(void) {
    console_puts("\nbrash " SHELL_VERSION " - the Asbestos SHell\n");
    console_puts("Use your manners.\n\n");

    for (;;) {
        console_puts("usr@brash> ");

        read_line(line, sizeof(line));

        serial_puts("brash: ");
        serial_puts(line);
        serial_puts("\n");

        dispatch(line);
    }
}
