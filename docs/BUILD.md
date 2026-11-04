# Building asbestOS

This tree builds the 32-bit image: protected mode, a VBE framebuffer console, a
polled PS/2 keyboard, and the first iteration of `brash`. See
[DOCS.md](DOCS.md) for the architecture.

## Prerequisites

- NASM
- GCC with 32-bit support (`gcc-multilib` on Debian/Ubuntu)
- GNU `ld` and `objcopy`
- Python 3 (writes the kernel size header)
- QEMU (optional, for testing)


On Debian/Ubuntu:

```bash
sudo apt install nasm gcc-multilib qemu-system-x86 
```

Or Arch:

```bash
sudo pacman -Syu nasm gcc-multilib qemu-system-x86 
```

## Build

```bash
./build.sh
```

The script:

1. Assembles `src/boot.asm` into a 512-byte boot sector.
2. Assembles `src/stage2.asm`, checks that it fits in 8 sectors, and pads it.
3. Assembles `src/entry32.asm`.
4. Compiles `src/kernel.c`, `console.c`, `string.c`, `serial.c`, `idt.c`,
   `panic.c`, `keyboard.c`, and `shell.c` with `-m32 -march=i386`.
5. Links with `src/linker32.ld` at `0x00100000` and produces a flat binary.
6. Prefixes the binary with a little-endian `uint32_t` byte count and pads the
   kernel region to 128 KiB.
7. Concatenates stage 1 + stage 2 + kernel into `build/os.img` and pads the disk
   to 4 MiB.

### Output

```
build/boot.bin       512-byte boot sector
build/stage2.bin     stage 2, padded to 4,096 bytes
build/kernel.elf     linked ELF with symbols
build/kernel.bin     flat kernel binary
build/kernel.hdr     size header + padded kernel region
build/os.img         4 MiB raw ATA disk image
```

`build/` is ignored by Git. Run `rm -rf build` for a clean rebuild.

### Disk layout

| Region   | LBA   | Size    |
|----------|-------|---------|
| Stage 1  | 0     | 512 B   |
| Stage 2  | 1–8   | 4 KiB   |
| Kernel   | 9–264 | 128 KiB |
| Unused   | 265–… | to 4 MiB |

The kernel region starts with a four-byte size header. The kernel binary itself
must be at most 131,068 bytes. If stage 2 or the kernel outgrows its region, the
build fails with the actual size and the constant to increase.

### Regenerating the font

`src/font8x16.h` is generated from JetBrains Mono and checked in, so the normal
build has no font dependencies.

The generator script is not included in this tree. To change the font, rasterise
ASCII `0x20`–`0x7F` at 13 px with the baseline on row 12 — the largest size where
every JetBrains Mono glyph fits an 8×16 cell — into 96 glyphs of 16 bytes each,
one byte per scanline with the most-significant bit on the left. Pillow is a
convenient way to do it, but nothing in the build depends on Pillow. Rebuild and
re-run the screenshot check afterwards: the glyphs change, the two-colour
histogram must not.

The bitmap is a derivative of JetBrains Mono and stays under the SIL OFL 1.1.
Keep `LICENSES/JetBrainsMono-OFL.txt` with the tree if you redistribute it.

## Run

```bash
qemu-system-i386 \
    -drive file=build/os.img,format=raw,if=ide \
    -vga std \
    -serial stdio
```

Or build and run in one step:

```bash
./build.sh --run
```

When run from an interactive terminal, `./build.sh` also offers to boot the
image after a successful build. Answer `y` to launch QEMU, or pass `--no-run` to
skip the prompt.

`-vga std` is recommended because it provides a 32-bpp VBE mode with a linear
framebuffer. `-vga cirrus` and `-vga vmware` generally work too, but their mode
numbers differ.

Expected serial output:

```
asbestOS kernel entered
```

The log stays quiet until you type at the shell. Each entered line is mirrored
as `brash: <line>`, and `halt` also prints `brash: halted by user`.

## Headless verification

Run QEMU without a display and capture both serial output and a screenshot:

```bash
rm -f /tmp/qemu-mon /tmp/serial.log /tmp/screen.ppm
qemu-system-i386 \
    -drive file=build/os.img,format=raw,if=ide \
    -vga std -display none \
    -serial file:/tmp/serial.log \
    -monitor unix:/tmp/qemu-mon,server,nowait &
QPID=$!
sleep 4
printf 'screendump /tmp/screen.ppm\n' | socat - UNIX-CONNECT:/tmp/qemu-mon
sleep 1
printf 'quit\n' | socat - UNIX-CONNECT:/tmp/qemu-mon
wait $QPID 2>/dev/null || true

cat /tmp/serial.log
magick /tmp/screen.ppm -format %c histogram:info:- | sort -nr | head
```

A correct screenshot has exactly two colours:

```
(75,34,34)    #4B2222   background
(195,147,147) #C39393   foreground
```

There is no third colour because glyphs are not anti-aliased.

### Driving the shell headlessly

The QEMU monitor can inject key presses, so the shell can be exercised without a
display. Start QEMU as above, then define a helper that sends one key at a time:

```bash
type_keys() {
    for key in "$@"; do
        printf 'sendkey %s\n' "$key" | socat - UNIX-CONNECT:/tmp/qemu-mon
        sleep 0.1
    done
}
```

Key names come from QEMU, not ASCII. Useful ones:

| Key | Monitor name |
|-----|--------------|
| space | `spc` |
| Enter | `ret` |
| Backspace | `backspace` |
| `;` | `semicolon` |
| shifted `h` | `shift-h` |

Type a command one key at a time:

```bash
type_keys p l e a s e spc h e l p ret
sleep 0.5
printf 'screendump /tmp/shell.ppm\n' | socat - UNIX-CONNECT:/tmp/qemu-mon
```

The serial log should gain `brash: please help`, and the screenshot should show
the command list at `usr@brash>`. To verify the manners gate:

```bash
type_keys h e l p ret                       # wrong word, sets apology debt
type_keys s o r r y spc h e l p ret         # apology accepted, debt cleared
```

The first line should produce `That is not the magic word. Say "please".` and
the second should run `help`.

## Testing the panic screen

The shell has a built-in `panic` command that deliberately divides by zero:

```text
usr@brash> please panic
```

If an earlier command failed, the usual manners rule applies: use
`sorry panic` instead. The command logs `brash: panic requested`, raises a real
`#DE`, and enters the normal panic screen. Serial should report:

```
KERNEL PANIC: Divide Error (#DE)
  vector=0x00000000 error=0x00000000 eip=... cs=0x00000008 eflags=... cr2=...
```

and the screenshot should show the `PANIC` banner, the `[ cut here ]` rule, and
the register dump.

For a fault that happens before the shell starts, or to test a different vector,
temporarily add code just before `keyboard_init(); shell_run();` at the end of
`kernel_main()` in `src/kernel.c`:

```c
/* TEMPORARY: divide by zero to force #DE. */
__asm__ __volatile__("xor %%edx, %%edx\n\t"
                     "xor %%ecx, %%ecx\n\t"
                     "mov $1, %%eax\n\t"
                     "div %%ecx\n\t"
                     : : : "eax", "ecx", "edx");
```

It must go before `shell_run()`, because `shell_run()` never returns. Build and
run headless as above.

To test the error-code path instead, load an invalid data selector:

```c
__asm__ __volatile__("mov $0x20, %%ax\n\t"
                     "mov %%ax, %%ds\n\t"
                     : : : "eax", "memory");
```

That raises `#GP` and the report should show `error=0x00000020`. **Remove the
injection afterwards** and confirm normal boot returns to the banner and the
`usr@brash>` prompt.

## Troubleshooting

**The build says stage 2 is too big.** Raise `STAGE2_SECTORS` in `src/boot.asm`
and `build.sh` together. Stage 2 is loaded from LBA 1 and must end before the
kernel at LBA 9.

**The build says the kernel is too big.** Raise `KERNEL_SECTORS` in
`src/stage2.asm` and update the image layout notes in `build.sh` and
[docs/DOCS.md](DOCS.md). The kernel must still fit in the 128 KiB staging
area at `0x10000`; raise the stage-2 copy loop and the disk region together.

**“no 32-bpp VBE linear framebuffer mode found.”** The firmware did not offer an
acceptable mode. Try a different QEMU `-vga` option, or a machine with VBE 3.0
support.

**Serial output is empty.** Check that `-serial stdio` (or
`-serial file:...`) is present and that QEMU is actually booting the disk. A
firmware rejected by stage 2 will fall back to BIOS text mode instead of
reaching the kernel.

**Serial stops after `asbestOS kernel entered`.** That is normal. The shell is
waiting at `usr@brash>` and only logs a line once it is entered. If the screen
shows the prompt, the machine is fine.

**Typing does nothing at `usr@brash>`.** Click the QEMU window to give it focus.
With `-display none`, drive the guest through the monitor's `sendkey` command as
shown above. Also make sure the keyboard is a PS/2 device; the driver does not
speak USB HID.

**Injected keys produce the wrong characters.** QEMU key names are not ASCII:
use `spc` for space, `ret` for Enter, `semicolon` for `;`, and `shift-h` for a
capital `H`. Characters that need Shift must be sent with the `shift-` prefix.

**Every command now demands `sorry`.** An earlier line was an error, so the
shell owes an apology. Enter `sorry <command>` once; a successful command clears
the debt and `please` works again. A blank line does not clear it.

**The machine reboots in a loop instead of showing a panic.** That is usually a
triple fault before the IDT is installed. Check that `idt_init()` runs before
anything that can fault, and that the stubs push both a vector and an error code
in the order `struct trap_frame` expects.

**A panic screen is one line too tall and the banner scrolls off.** A line of
exactly 80 columns wraps and then the trailing newline consumes another row.
Keep panic rules at 79 columns and the whole screen at 24 printed rows.
