#include "panic.h"
#include "console.h"
#include "io.h"
#include "serial.h"
#include "string.h"

/*
 * panic() is allowed to run before the console exists, so every console call
 * here is gated on console_is_ready(). Serial output is best effort too.
 */

static volatile int panicking;

static const char *const banner[] = {
    " ____   _    _   _ ___ ____ ",
    "|  _ \\ / \\  | \\ | |_ _/ ___|",
    "| |_) / _ \\ |  \\| || | |    ",
    "|  __/ ___ \\| |\\  || | |___ ",
    "|_| /_/   \\_\\_| \\_|___\\____|",
};

static void serial_report(const char *reason, const struct trap_frame *frame) {
    serial_puts("KERNEL PANIC: ");
    serial_puts(reason);
    serial_puts("\n");

    if (!frame) {
        return;
    }

    serial_puts("  vector=");
    serial_put_hex32(frame->vector);
    serial_puts(" error=");
    serial_put_hex32(frame->error);
    serial_puts(" eip=");
    serial_put_hex32(frame->eip);
    serial_puts(" cs=");
    serial_put_hex32(frame->cs);
    serial_puts(" eflags=");
    serial_put_hex32(frame->eflags);
    serial_puts(" cr2=");
    serial_put_hex32(read_cr2());
    serial_puts("\n");
}

static void console_center_fixed(const char *text, usize width) {
    usize padding = width < CONSOLE_COLS ? (CONSOLE_COLS - width) / 2u : 0u;

    for (usize i = 0; i < padding; i++) {
        console_putc(' ');
    }
    console_puts(text);
    console_putc('\n');
}

static void console_rule(const char *label) {
    /* Leave the last column free: an exactly 80-column line wraps by itself,
     * and the extra newline would push the panic screen over 25 rows. */
    usize width = CONSOLE_COLS - 1u;
    usize length = strlen(label) + 4u; /* "[ " + label + " ]" */
    usize left = length < width ? (width - length) / 2u : 0u;
    usize right = length < width ? width - length - left : 0u;

    for (usize i = 0; i < left; i++) {
        console_putc('-');
    }

    console_puts("[ ");
    console_puts(label);
    console_puts(" ]");

    for (usize i = 0; i < right; i++) {
        console_putc('-');
    }
    console_putc('\n');
}

static void hex_field(const char *label, uint32_t value, int last) {
    console_puts(label);
    console_putc(' ');
    console_put_hex32(value);
    if (!last) {
        console_puts("   ");
    }
}

static void draw_screen(const char *reason, const struct trap_frame *frame) {
    console_clear();
    console_putc('\n');

    console_rule("KERNEL PANIC");
    console_putc('\n');

    for (usize i = 0; i < sizeof(banner) / sizeof(banner[0]); i++) {
        console_center_fixed(banner[i], 28u);
    }

    console_putc('\n');
    console_rule("cut here");
    console_putc('\n');

    console_puts("Exception:  ");
    console_puts(reason);
    console_putc('\n');

    if (!frame) {
        console_puts("No trap frame was recorded.\n");
        return;
    }

    console_puts("Vector:     ");
    console_put_hex32(frame->vector);
    console_puts("   Error code: ");
    console_put_hex32(frame->error);
    console_putc('\n');

    console_putc('\n');

    hex_field("EIP", frame->eip, 0);
    hex_field("CS", frame->cs, 0);
    hex_field("EFLAGS", frame->eflags, 1);
    console_putc('\n');

    console_putc('\n');

    hex_field("EAX", frame->eax, 0);
    hex_field("EBX", frame->ebx, 0);
    hex_field("ECX", frame->ecx, 0);
    hex_field("EDX", frame->edx, 1);
    console_putc('\n');

    hex_field("ESI", frame->esi, 0);
    hex_field("EDI", frame->edi, 0);
    hex_field("EBP", frame->ebp, 0);
    hex_field("ESP", frame->esp, 1);
    console_putc('\n');

    console_putc('\n');

    hex_field("CR0", read_cr0(), 0);
    hex_field("CR2", read_cr2(), 0);
    hex_field("CR3", read_cr3(), 0);
    hex_field("CR4", read_cr4(), 1);
    console_putc('\n');

    console_putc('\n');
    console_puts("asbestOS has stopped. It is not sorry.\n");
    console_putc('\n');
    console_puts("Please reboot the machine.\n");
}

void panic(const char *reason, const struct trap_frame *frame) {
    cpu_cli();

    if (panicking) {
        /* A fault inside the panic path: stop without trying again. */
        for (;;) {
            cpu_halt();
        }
    }
    panicking = 1;

    serial_report(reason, frame);

    if (console_is_ready()) {
        draw_screen(reason, frame);
    }

    for (;;) {
        cpu_halt();
    }
}
