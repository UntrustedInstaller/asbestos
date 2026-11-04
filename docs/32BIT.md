# asbestOS 32-bit — Milestone 2 in progress

> **Status:** working draft. Milestone 1 boots and renders correctly under QEMU.
> Milestone 2 has an IDT, a panic screen, a polled PS/2 keyboard, and the first
> iteration of `brash`. This document describes the entire current tree in
> `src/`.

Milestone 1 is deliberately narrow: leave the BIOS behind, enter flat 32-bit
protected mode, put the machine into a VBE linear-framebuffer mode, and draw an
80×25 text console in exactly the two colours the project requires.

Milestone 2 adds exception handling, a polled keyboard, and `brash`. There is
still no interrupt controller, no storage driver, and no filesystem; hardware
interrupts remain masked.

**Only the BIOS-required boot stub and stage 2 loader are 16-bit.** Once the CPU
enters protected mode, every instruction executed is 32-bit.

---

## 1. Target architecture

| Property        | Current choice                                      |
|-----------------|-----------------------------------------------------|
| CPU mode        | 32-bit protected mode                               |
| Privilege level | Ring 0                                              |
| Segmentation    | Flat: code `0x08`, data `0x10`, base 0, limit 4 GiB |
| Paging          | Off                                                 |
| Interrupts      | IDT installed; exceptions caught, hardware IRQs masked |
| Input           | Polled PS/2 keyboard, scancode set 1                |
| Shell           | `brash`, fixed prompt, built-in commands only       |
| Kernel location | Linked and run at `0x00100000` (1 MiB)              |
| Kernel stack    | Top at `0x00090000`, grows down                     |
| Firmware        | BIOS (no UEFI)                                      |
| Display         | VBE linear framebuffer, 32 bpp                      |
| Storage target  | ATA PIO (not used yet), FAT16/FAT32 later           |

No paging means addresses in the kernel are physical addresses. That is fine
while the kernel is the only thing running and the firmware has set up the
framebuffer. Paging can be introduced later with an identity map for low memory
and the framebuffer.

### Why `-march=i386`?

`build.sh` compiles with `-m32 -march=i386 -mtune=i386`. The target is a plain
i386-class machine; avoiding later instruction sets keeps the kernel honest and
avoids accidentally depending on SSE or CMOV before they are enabled.

---

## 2. Source layout

```
src/
├── boot.asm        Stage 1 boot sector (16-bit, BIOS LBA read)
├── stage2.asm      Stage 2 loader (16-bit: disk, VBE, A20, GDT, PM entry)
├── entry32.asm     32-bit kernel entry: segments, stack, BSS, call C
├── kernel.c        Kernel entry: validate BootInfo, start console and brash
├── console.c       VBE framebuffer 80×25 text console
├── console.h
├── boot_info.h     BootInfo struct shared with stage2.asm
├── font8x16.h      JetBrains Mono 8×16 glyphs, ASCII 0x20–0x7F (generated)
├── serial.c        COM1 output for headless testing
├── serial.h
├── string.c        memset/memcpy/memmove/strlen/strcmp
├── string.h
├── io.h            Port I/O, CPU helpers, control-register reads
├── types32.h       Fixed-width types (freestanding, no libc)
├── idt.c           IDT setup, exception names, C dispatcher
├── idt.h           IDT API and struct trap_frame
├── isr.asm         256 generated exception stubs, isr_common
├── panic.c         Panic screen and serial fault report
├── panic.h
├── keyboard.c      Polled PS/2 keyboard, scancode set 1
├── keyboard.h
├── shell.c         brash: line editor, parser, manners gate, built-ins
├── shell.h
└── linker32.ld     Links the kernel at 0x00100000
```

The build script is `build.sh` at the repository root.

---

## 3. Disk layout

`build/os.img` is a raw 4 MiB ATA disk image. It is not a filesystem image yet;
it is a flat sequence of regions:

| Region      | LBA        | Size          | Contents                                  |
|-------------|------------|---------------|-------------------------------------------|
| Stage 1     | 0          | 512 B         | `boot.bin`, ends with `0xAA55`             |
| Stage 2     | 1–8        | 4,096 B       | `stage2.bin`, padded                      |
| Kernel      | 9–264      | 128 KiB       | 4-byte size header + flat kernel binary   |
| Unused      | 265–…      | rest of 4 MiB | Zeroed                                    |

The kernel region is fixed at 256 sectors (128 KiB). The first four bytes are a
little-endian `uint32_t` byte count; the kernel binary follows immediately.
Stage 2 reads the count, validates it, and copies that many bytes.

Why a size header instead of using the ELF headers? The loaded image is a flat
binary (`objcopy -O binary`), so there are no ELF program headers left at run
time. The four-byte count is the smallest thing that lets stage 2 know where the
real image ends.

**Limits enforced by the build:**

- Stage 2 must fit in 8 sectors. If not, the build says which constants to raise
  in `src/boot.asm` and `build.sh`.
- The kernel binary must be at most 131,068 bytes (128 KiB minus the header). If
  not, the build says to raise `KERNEL_SECTORS` in `src/stage2.asm`.

---

## 4. Memory layout during boot

| Address           | Use                                                    |
|-------------------|--------------------------------------------------------|
| `0x00005000`      | `BootInfo` (32 bytes), written by stage 2               |
| `0x00006000`      | VBE controller info buffer (512 bytes)                 |
| `0x00006200`      | VBE mode info buffer (256 bytes)                       |
| `0x00007C00`      | Stage 2 real-mode stack top (grows down)               |
| `0x00008000`      | Stage 2 load address (`org 0x8000`)                    |
| `0x00010000`      | Kernel image staging area (header + binary, 128 KiB)   |
| `0x0002FFFF`      | End of staging area                                    |
| `0x00090000`      | 32-bit kernel stack top (grows down)                   |
| `0x00100000`      | Kernel link address and final run address              |
| `fb_addr`         | VBE linear framebuffer (firmware-assigned, e.g. `0xFD000000`) |

Stage 2 loads the kernel into low memory first because BIOS disk reads use
real-mode addressing. After the protected-mode switch, a 32-bit `rep movsd`
copies the image from `0x00010000` to `0x00100000`. Copying from low to high is
safe: the source and destination ranges do not overlap.

---

## 5. Boot sequence

1. **BIOS** loads LBA 0 to `0x7C00` and jumps to it.
2. **Stage 1 (`boot.asm`)**
   - Sets up a small real-mode stack at `0x7C00`.
   - Saves the BIOS boot drive from `DL`.
   - Checks for INT 13h LBA extensions (`AH=0x41`, `BX=0x55AA`).
   - Reads 8 sectors from LBA 1 into `0x0800:0x0000` using a Disk Address
     Packet.
   - Far-jumps to `0x0800:0x0000`.
   - On any failure, prints a message in BIOS text mode and halts.
3. **Stage 2 (`stage2.asm`)**
   - Loads 256 kernel sectors in four 64-sector reads. Each read targets a
     32 KiB-aligned low-memory buffer, keeping every transfer inside one 64 KiB
     window (a common BIOS limitation).
   - Validates the kernel size header.
   - Queries VBE (`INT 10h AX=4F00h`), walks the mode list, and scores modes.
   - Sets the best mode with `AX=4F02h`, `BX=mode|0x4000` (linear framebuffer
     requested), then re-queries mode info.
   - Fills `BootInfo` at `0x5000`.
   - Enables A20 through port `0x92`.
   - Loads a flat GDT, sets `CR0.PE`, and far-jumps to the 32-bit entry.
   - Copies the staged kernel to `0x00100000` and jumps to it.
4. **Kernel entry (`entry32.asm`)**
   - Reloads all segment registers with the flat data selector.
   - Sets `ESP` to `0x00090000` and clears `EBP`.
   - Zeroes `.bss` between `_bss_start` and `_bss_end`.
   - Calls `kernel_main()`.
5. **`kernel_main()`**
   - Initialises COM1 and installs the IDT.
   - Validates `BootInfo`.
   - Initialises the console and prints the banner and diagnostics.
   - Drains the keyboard controller and enters `brash`, which never returns.

---

## 6. VBE mode selection

Stage 2 does not assume a particular mode number. It asks the firmware for the
controller's mode list and evaluates every entry.

A mode is accepted only if all of these are true:

- `ModeAttributes` bit 0 set — mode is supported.
- `ModeAttributes` bit 4 set — graphics mode.
- `ModeAttributes` bit 7 set — linear framebuffer available.
- `BitsPerPixel == 32`.
- `MemoryModel` is 4 (packed pixel) or 6 (direct colour).
- Red, green, and blue mask sizes are non-zero.
- Width ≥ 640 and height ≥ 400.

Accepted modes are scored by:

```
score = |width - 640| + |height - 480|
```

The lowest score wins. Ties keep the first mode encountered. This is why QEMU's
`-vga std` selects mode `0x142` (`640×480×32`), while a firmware with only
`800×600×32` will still boot into something sensible.

If no acceptable mode exists, stage 2 switches back to BIOS text mode (`INT 10h
AX=0003h`) and prints a diagnostic before halting. This is the one place the
kernel can fail and still be visible on a machine without serial output.

### BootInfo

`src/boot_info.h` defines the handoff structure. The layout is packed and must
stay in sync with the offsets written in `fill_boot_info`:

| Offset | Field        | Meaning                                      |
|--------|--------------|----------------------------------------------|
| `0x00` | `magic`      | `0x33425341` (`"ASB3"` little-endian)        |
| `0x04` | `fb_addr`    | Physical linear framebuffer address          |
| `0x08` | `pitch`      | Bytes per scanline (`LinBytesPerScanLine`)   |
| `0x0C` | `width`      | Horizontal pixels                            |
| `0x0E` | `height`     | Vertical pixels                              |
| `0x10` | `bpp`        | Bits per pixel (32 in milestone 1)           |
| `0x11` | `red_size`   | Red channel width in bits                    |
| `0x12` | `red_pos`    | Red channel bit position                     |
| `0x13` | `green_size` | Green channel width in bits                  |
| `0x14` | `green_pos`  | Green channel bit position                   |
| `0x15` | `blue_size`  | Blue channel width in bits                   |
| `0x16` | `blue_pos`   | Blue channel bit position                    |
| `0x17` | `boot_drive` | BIOS drive number (for the future ATA driver) |
| `0x18` | `mode`       | Selected VBE mode number                     |
| `0x1A` | `reserved`   | Zero                                         |
| `0x1C` | `reserved2`  | Zero                                         |

The kernel validates the magic, the framebuffer address, `bpp == 32`, and that
the pitch is at least `width * 4` before touching the framebuffer.

> **Draft note:** milestone 2 may extend `BootInfo` with a memory map and an ATA
> drive count. If the struct grows, keep the magic and bump it to `"ASB4"` so an
> old kernel rejects a new loader instead of misreading fields.

---

## 7. Framebuffer console

### Geometry

The console is a logical **80 columns × 25 rows** grid:

- Cell size: **8 × 16 pixels**.
- Console pixel area: **640 × 400**.
- The area is centred in the selected mode. On `640×480`, the top and bottom
  margins are 40 pixels each. On wider modes, left and right margins grow.
- The whole screen is filled with the background colour first, so the margins
  match the console background.

Each cell stores a character byte in `cells[row][col]`. There is no colour
attribute per cell yet; foreground and background are global console state.

### Font

`src/font8x16.h` contains 96 glyphs (ASCII `0x20`–`0x7F`), 16 bytes per glyph,
one byte per scanline with the most-significant bit on the left.

The glyphs are rasterised from **JetBrains Mono Regular** at 13 px with the
baseline on row 12. Thirteen pixels is the largest size at which the typeface
fits the 8×16 cell: at 14 px the advance is 8.41 px and `W`, `%`, and `&` grow
to nine pixels wide, while at 12 px the glyphs get noticeably smaller than the
cell. Rendering at the cell size directly (rather than at a large size and
downscaling) keeps every stem on a whole pixel and every glyph inside its cell.

The table is generated, not hand-edited. The generator script is deliberately
not part of this tree; the checked-in header was produced with a small
Pillow-based rasteriser from JetBrains Mono Regular at 13 px with the baseline
on row 12, one byte per scanline, most-significant bit on the left, for ASCII
`0x20`–`0x7F`.

The header is checked in, so a normal build needs neither Pillow nor the
TrueType file. JetBrains Mono is licensed under the SIL Open Font License 1.1
and the bitmap is a derivative of it; the licence text is in
`LICENSES/JetBrainsMono-OFL.txt`. Do not replace the header with a font that has
no compatible licence.

To swap in another monospace face, rasterise it with the same cell size,
baseline, bit order, and table layout, then re-run the boot checks: the glyph
shapes change but the `font8x16[96][16]` interface does not.

Characters outside `0x20`–`0x7E` render as `?`. Control characters are handled
by the console itself:

| Byte | Behaviour                                             |
|------|-------------------------------------------------------|
| `\n` | Next line, scrolling if at the bottom                 |
| `\r` | Column 0                                              |
| `\b` | Back one cell, erase it                               |
| `\t` | Advance to the next multiple of 8                     |

Other bytes are stored and drawn as-is.

### Colours

Milestone 1 uses exactly two colours:

| Role       | Hex       | RGB             |
|------------|-----------|-----------------|
| Background | `#4B2222` | `(75, 34, 34)`  |
| Foreground | `#C39393` | `(195, 147, 147)` |

The values live in `src/console.h` as `CONSOLE_DEFAULT_BG` and
`CONSOLE_DEFAULT_FG`.

`rgb_to_pixel()` converts an 8-bit-per-channel colour into the framebuffer's
mask layout using the `red_size`/`red_pos`, `green_size`/`green_pos`, and
`blue_size`/`blue_pos` fields from `BootInfo`. For the common 32-bpp `8:8:8`
layout this reduces to the usual `0x00RRGGBB` word, but the general path means
the console also works on 16-bpp or unusual mask layouts if stage 2 is later
relaxed.

`put_pixel()` understands 32-, 24-, and 16-bpp framebuffers. Milestone 1 only
selects 32-bpp modes, so the other paths are defensive rather than tested.

### Cursor and scrolling

The cursor is drawn as a solid underline across the bottom two rows of the
current cell. Before drawing a character or moving the cursor, the old cell is
repainted so the underline does not smear.

Scrolling is intentionally simple: the cell array is moved up by one row with
`memmove`, the last row is blanked, and the whole screen is redrawn. Redrawing
80×25 cells is cheap at this stage and keeps the code easy to audit. A later
milestone can replace it with a framebuffer blit.

---

## 8. Interrupts, exceptions, and the panic screen

The IDT is installed early in `kernel_main()`, before the console exists. That
ordering matters: a fault during BootInfo validation should produce a panic
report, not a triple-fault reboot with no useful output.

### IDT

`src/idt.c` fills all 256 gates with ring-0, present, 32-bit interrupt gates
(`0x8E`) that point at stubs in `src/isr.asm`. All gates use the flat code
selector `0x08`. The table is loaded with `lidt`:

```c
idtp.limit = sizeof(idt) - 1;
idtp.base  = &idt[0];
asm volatile("lidt %0" : : "m"(idtp));
```

Hardware IRQs remain masked (`eflags.IF == 0`), so the timer, keyboard, and
other devices cannot fire yet. Vector `0x80` is not special-cased either.

### Stubs and the trap frame

The CPU pushes `eip`, `cs`, and `eflags` on an exception. For some vectors it
also pushes an error code; for the rest it does not. The stubs normalise this:

| Vectors with a CPU error code | Vectors without |
|-------------------------------|-----------------|
| 8, 10, 11, 12, 13, 14, 17, 21, 29, 30 | all others |

Every stub pushes the vector number, and the "without" case pushes a dummy zero
error code first. That keeps one uniform stack layout. `isr_common` then clears
the direction flag, saves the general registers with `pushad`, loads the flat
data selector into `ds`/`es`/`fs`/`gs`, and calls `isr_dispatch(frame)`.

`struct trap_frame` in `src/idt.h` describes exactly what the C handler sees:

```c
struct trap_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pushad */
    uint32_t vector, error;                          /* stub   */
    uint32_t eip, cs, eflags;                        /* CPU    */
};
```

If a handler ever returns, `isr_common` restores the registers, drops the vector
and error code, and executes `iretd`. For exceptions the dispatcher never
returns: it calls `panic()`.

### Dispatcher

`isr_dispatch()` maps vectors `0`–`31` to human-readable names (`Divide Error
(#DE)`, `General Protection Fault (#GP)`, and so on). Anything outside the
architectural exceptions is treated as `"Unexpected interrupt"`. There is no
recovery path and no attempt to continue after a fault; continuing after a
corrupted state is how you get mystery bugs.

### Panic path

`panic()` is written to survive being called at almost any point:

1. `cli`, then check a recursion guard. A fault inside the panic path halts
   instead of panicking again.
2. Write a compact report to COM1. Serial output is best-effort and is skipped
   if `serial_init()` has not run yet, so an early panic cannot hang waiting for
   a UART that was never configured.
3. If the framebuffer console is ready, clear it and draw the panic screen.
4. Halt forever.

The screen is a Linux-oops-style report: a centred rule, a FIGlet `PANIC`
banner, a `[ cut here ]` rule, the exception name, vector and error code, the
instruction pointer and segment state, general registers, control registers,
and a closing line. Serial carries the same core facts for headless runs:

```
KERNEL PANIC: Divide Error (#DE)
  vector=0x00000000 error=0x00000000 eip=0x00100D1A cs=0x00000008 eflags=0x00010046 cr2=0x00000000
```

The screen is laid out to fit exactly 25 rows:

| Row | Content |
|-----|---------|
| 0 | blank |
| 1 | `[ KERNEL PANIC ]` rule |
| 3–7 | `PANIC` banner |
| 9 | `[ cut here ]` rule |
| 11 | exception name |
| 12 | vector and error code |
| 14 | `eip`, `cs`, `eflags` |
| 16–17 | general registers |
| 19 | control registers |
| 21 | closing line |
| 23 | reboot request |
| 24 | cursor |

Two details keep it from scrolling: the rules are **79** columns wide, because
an exactly 80-column line makes `console_putc` wrap and then the trailing
newline pushes the screen past 25 rows; and each banner row is centred against
a fixed width of 28, with its trailing spaces preserved, so the glyph columns
line up.

---

## 9. Keyboard and brash

### Polled PS/2 keyboard

`src/keyboard.c` reads the 8042 controller directly through ports `0x64`
(status) and `0x60` (data). It is polling-only because the PIC has not been
remapped and hardware interrupts are still masked. `kernel_main()` calls
`keyboard_init()` to drain anything the firmware left in the output buffer, then
`brash` spins on `keyboard_poll()`.

`keyboard_poll()` returns `0` when no key is ready. Otherwise it returns ASCII
for printable keys, `\b` for Backspace, `\n` for Enter, and `\t` for Tab. It
consumes key releases, tracks left/right Shift and Caps Lock, and drops extended
(`0xE0`-prefixed) keys such as arrows. It also ignores bytes flagged as AUX
(mouse) data.

The driver assumes scancode set 1, which is what the 8042 translation mode
produces on the target machines. Unsupported keys are simply ignored. The shell
idles with `cpu_pause()` rather than `hlt` because there is no interrupt to wake
it; that busy-wait is a deliberate first-iteration cost, not the final design.

### brash

`brash` is the Asbestos SHell. Its prompt is always `usr@brash>` followed by a
space, so it looks like:

```text
usr@brash>
```

The prompt does **not** reveal the required manners word. The rule is:

- A command normally starts with `please`.
- Any error sets an **apology debt**. The next command must start with `sorry`.
- A successful command clears the debt.
- Blank lines are ignored and do not clear the debt.
- `sorry` used when no error occurred is itself an error, which creates an
  apology debt.

Everything the shell counts as an error sets the debt: the wrong magic word, an
empty command, a compound statement, too many arguments, and an unknown command.
A malformed apology does not clear an existing debt.

The error strings are provisional and live together at the top of `src/shell.c`:

| Situation | Output |
|-----------|--------|
| `please` required | `That is not the magic word. Say "please".` |
| `sorry` required | `An apology would be nice. Say "sorry".` |
| Compound statement | `Compound statements are not permitted. I can't multitask well` |
| No command after the magic word | `No command was given. Do you normally stare blankly at people?` |
| Too many tokens | `Too many arguments. I can't keep track of all of that.` |
| Unknown command | `Unknown command: <name>` |

### Commands and parsing

The first iteration is built-in only:

| Command | Effect |
|---------|--------|
| `help` | List the commands and the manners rule |
| `about` | Print the shell name and version |
| `echo <text>` | Print the remaining arguments, separated by single spaces |
| `clear` | Clear the console |
| `halt` | Stop the machine |
| `panic` | Force a divide error and enter the panic screen |
| `reboot` | Reset the machine |

The line editor supports printable ASCII and Backspace. The input buffer is 64
bytes, and the parser accepts at most 8 whitespace-separated tokens. There is no
quoting, escaping, history, cursor movement, globbing, or tab completion.

There is also no compound-statement support. Any of `;`, `&`, `|`, `<`, `>`,
`(`, `)`, or a backtick causes the whole line to be rejected before it is
tokenised. That covers chaining, pipes, redirection, and subshell syntax without
pretending to parse them.

A short session:

```text
usr@brash> help
That is not the magic word. Say "please".
usr@brash> please help
An apology would be nice. Say "sorry".
usr@brash> sorry help
Commands
  help           show this list
  about          identify this shell
  echo <text>    print text
  clear          clear the screen
  halt           stop the machine
  panic          force a CPU fault
  reboot         reboot the machine

Prefix a command with "please".
After an error, apologise with "sorry".
usr@brash> please echo hi
hi
usr@brash>
```

Each entered line is mirrored to COM1 as `brash: <line>`, which makes headless
testing possible without decoding the framebuffer.

---

## 10. Build and run

See [BUILD.md](BUILD.md) for the full instructions. In short:

```bash
./build.sh
qemu-system-i386 \
    -drive file=build/os.img,format=raw,if=ide \
    -vga std -serial stdio
```

The build produces:

```
build/boot.bin       512 bytes
build/stage2.bin     854 bytes (padded to 4,096)
build/kernel.bin     flat kernel image
build/kernel.elf     linked ELF (symbols useful for debugging)
build/os.img         4 MiB raw disk image
```

Expected serial output from a successful boot:

```
asbestOS kernel entered
```

The serial log then stays quiet until a line is entered at `usr@brash>`. Each
entered line is mirrored as `brash: <line>`; `halt` also prints
`brash: halted by user`. A correct screenshot contains only two colours:
`#4B2222` and `#C39393`.

---

## 11. Current limitations

The tree is still deliberately incomplete:

- **Hardware interrupts** — the IDT and exception path exist, but the PIC is
  not remapped and no IRQ fires. Input is therefore polled.
- **Polled input** — `brash` busy-waits on the keyboard with `cpu_pause()`.
  There is no IRQ-driven keyboard and no input timeout yet.
- **Storage** — no ATA PIO driver and no FAT16/FAT32 layer yet.
- **`brash`** — built-in commands only. It cannot run programs, read files, or
  touch the disk.
- **Line editing** — printable ASCII and Backspace only. No cursor movement, no
  history, no quoting or escaping, no globbing, no tab completion.
- **Compound statements** — rejected outright rather than parsed.
- **Memory management** — no paging, no allocator, no memory map parsing.
- **Multiple consoles** — one global console, no colour attributes.
- **Text modes** — VBE graphics only; if no 32-bpp linear mode exists, boot fails
  with a BIOS text message.
- **Unicode** — ASCII `0x20`–`0x7F` only.
- **Cursor blinking** — the underline is static.

The kernel also assumes a single CPU, no SMIs interfering with the framebuffer,
and a firmware that leaves A20 controllable through port `0x92`.

---

## 12. Milestone 2 and beyond

Milestone 2 is in progress. The IDT and panic screen, the polled PS/2 keyboard,
and the first iteration of `brash` are done.

The shell is called **`brash`**, the Asbestos SHell. Its prompt is `usr@brash>`.
Commands are prefixed with `please`; after an error, the next command must be
prefixed with `sorry`. The exact error strings and the ban on compound
statements are not optional features; they are the point.

The intended order for the rest is:

1. ~~**IDT and exception handling.**~~ Done: faults now produce a readable panic
   screen instead of a triple-fault reboot.
2. **PIC remap and PIT.** A timer tick is the foundation for IRQ-driven input
   and later scheduling.
3. ~~**PS/2 keyboard driver.**~~ Polled first pass done: scancode set 1,
   shifted characters, Caps Lock, Backspace, Enter, and Tab. Move it behind
   IRQ 1 once the PIC is remapped.
4. **ATA PIO driver.** Identify the disk from `BootInfo.boot_drive`, read sectors
   with LBA28. Polling is fine to start.
5. **FAT16/FAT32 read-only layer.** BPB validation, cluster chains, directory
   traversal, `open`/`read`.
6. ~~**`brash` and the manners gate.**~~ First iteration done: a fixed
   `usr@brash>` prompt, `please` normally, `sorry` after an error, exact error
   strings, and no compound statements. Grow it once there is a filesystem.
7. **Write support and loadable modules.** Only after the read path is stable.

Each step should keep the serial log and screenshot checks working so
regressions are obvious.

---

## 13. Design notes and gotchas

A few decisions are worth recording because they are easy to undo by accident.

- **The kernel is loaded twice.** Stage 2 reads it to `0x10000` (real-mode
  addressable) and then copies it to `0x100000` (link address). Do not "optimise"
  the real-mode load to a high address; BIOS disk reads use segment:offset
  addressing and cannot reach 1 MiB without unreal-mode tricks.
- **The size header is not part of the kernel.** `objcopy` output starts after
  the four-byte count. If you change the header format, change both `build.sh`
  and `.loaded` in `stage2.asm`.
- **VBE mode info is re-queried after `4F02h`.** Some firmware only fills
  `LinBytesPerScanLine` and the physical address once the mode is active.
- **`LinBytesPerScanLine` is preferred over `BytesPerScanLine`.** They are often
  equal, but VBE 3.0 explicitly provides the linear value at offset `0x32`.
- **The framebuffer is not cached or write-combined.** Every pixel is a normal
  memory write through the CPU cache. That is correct, if not fast. MTRRs can
  come later.
- **BSS is zeroed in `entry32.asm`, not by the loader.** The flat binary omits
  BSS entirely, so the kernel must clear it before calling C.
- **The linker script uses separate RX and RW load segments.** This removes the
  RWX warning from `ld` and keeps the eventual page permissions honest, even
  though paging is currently off.
- **Every stub must push a vector and an error code.** `isr_common` assumes the
  stack shape in `struct trap_frame`. If you add or renumber a vector that
  carries an error code, update the `%if` list in `src/isr.asm`; otherwise the
  handler will read `eip` as the error code and the frame will be wrong.
- **`console_putc` wraps at 80 columns.** A line of exactly 80 characters
  consumes two rows if it is followed by `\n`. The panic rules are 79 columns
  for that reason. Keep the panic screen at or below 24 printed rows so the
  final newline does not scroll the banner away.
- **The keyboard is polling-only on purpose.** The PIC is not remapped, so
  `keyboard_init()` drains whatever the firmware left in the 8042 output buffer
  and `brash` spins on `keyboard_poll()`. The idle loop uses `pause`, not `hlt`;
  `hlt` without a wake-up interrupt would hang the machine. Replace the polling
  loop when IRQ 1 is wired up.
- **Scancode set 1 is assumed.** That is what the 8042 translation mode produces
  on the target machines. If translation is disabled later, the key tables in
  `src/keyboard.c` must change.
- **The manners gate is a debt, not an alternating turn.** `apology_due` is set
  by every error and cleared only by a successful command. `sorry` with no debt
  is itself an error. Do not replace this with a two-state please/sorry toggle;
  that changes the intended behaviour.
- **Compound detection runs before tokenising.** The raw line is scanned for
  `; & | < > ( )` and backticks, so redirection or chaining cannot hide inside a
  token. Adding a quoting layer later means revisiting that scan.
- **Input limits are fixed and visible.** The line buffer is 64 bytes including
  the terminator, and the parser accepts at most 8 whitespace-separated tokens.
  Excess characters are dropped; excess tokens are an error rather than being
  ignored.
- **`clear` does not reset shell state.** It only clears the framebuffer. The
  apology debt, and therefore the need for `sorry` or `please`, survives.
- **`shell_run()` never returns.** Panic-injection code for testing must be
  placed before the `keyboard_init(); shell_run();` pair at the end of
  `kernel_main()`, or it will never execute.

---

*Milestone 1: the screen is the right colour, the CPU is the right width, and
the machine stops when asked. Milestone 2: when it stops, it now explains why —
and if you want to do anything else, you had better ask nicely.*
