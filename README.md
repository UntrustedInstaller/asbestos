<img src="asbestos.svg" alt="asbestOS" width="50%">

An intentionally abrasive operating system inspired by INTERCAL.

## Why? ##

I was talking to a friend in class about the humor of INTERCAL's "PLEASE" instructions, to which he remarked:  
*"Why not do that with an OS?"*  
And it clicked. I got home and got to work.

## brash

`brash` is the Asbestos SHell. Its prompt is always `usr@brash>`.

Prefix a command with `please`. Any error creates an apology debt: the next
command must start with `sorry`. A successful command clears the debt. Compound
statements are rejected outright.

Built-ins are `help`, `about`, `echo`, `clear`, `halt`, `panic`, and `reboot`.
The full manners rules and the current error strings are documented in
[docs/DOCS.md](docs/DOCS.md).

## Quick start

```bash
./build.sh
qemu-system-i386 \
    -drive file=build/os.img,format=raw,if=ide \
    -vga std -serial stdio
```

Or build and run in one step:

```bash
./build.sh --run
```

Running `./build.sh` in a terminal also asks whether to boot the image when the
build finishes. Pass `--no-run` to skip the prompt.

Dependencies: `nasm`, `gcc` with 32-bit support (`gcc-multilib` on
Debian/Ubuntu), `ld`, `objcopy`, `python3`, and optionally `qemu-system-i386`

## What is in the box

- **32-bit protected mode** — flat ring 0, no paging, kernel at 1 MiB
- **Two-stage BIOS boot** — LBA loader plus a stage 2 that sets up VBE, A20,
  the GDT, and the protected-mode transition
- **VBE console** — 80×25 cells of 8×16 pixels, centred in the selected mode
- **Exact colours** — `#4B2222` background, `#C39393` foreground, no palette
  approximation
- **Serial diagnostics** — COM1 output for headless boot verification
- **IDT and panic screen** — 256 exception stubs, a C dispatcher, and an
  oops-style report with a `PANIC` banner and register dump
- **Polled PS/2 keyboard** — scancode set 1, modifier tracking, and a small line
  editor
- **`brash`** — a fixed-prompt shell with built-ins and a `please` / `sorry`
  manners gate
- **Terminus bitmap font** — 8×16 glyphs for ASCII `0x20`–`0x7E`,
  rasterised from Terminus Bold 16px and licensed under the SIL OFL 1.1
  *I like Terminus.*

## Project structure

```
├── build.sh            — Build pipeline; produces build/os.img
├── src/
│   ├── boot.asm        — Stage 1 boot sector
│   ├── stage2.asm      — Disk load, VBE setup, GDT, protected-mode entry
│   ├── entry32.asm     — 32-bit entry, BSS zeroing, C call
│   ├── kernel.c        — Kernel entry: validate BootInfo, start console and brash
│   ├── console.c       — VBE framebuffer 80×25 console
│   ├── console.h
│   ├── boot_info.h     — BootInfo passed from stage 2 to the kernel
│   ├── font8x16.h      — Terminus 8×16 bitmap
│   ├── serial.c        — COM1 output for testing
│   ├── serial.h
│   ├── string.c        — Freestanding memory/string helpers
│   ├── string.h
│   ├── io.h            — Port I/O, CPU helpers, control-register reads
│   ├── types32.h       — Fixed-width types
│   ├── idt.c           — IDT setup, exception names, C dispatcher
│   ├── idt.h           — IDT API and trap-frame layout
│   ├── isr.asm         — 256 exception stubs and isr_common
│   ├── panic.c         — Panic screen and serial fault report
│   ├── panic.h
│   ├── keyboard.c      — Polled PS/2 keyboard, scancode set 1
│   ├── keyboard.h
│   ├── shell.c         — brash: line editor, parser, manners gate, built-ins
│   ├── shell.h
│   └── linker32.ld     — Links the kernel at 1 MiB
├── docs/
│   ├── DOCS.md        — Architecture, boot path, console, IDT, keyboard, brash
│   └── BUILD.md        — Build, run, shell, and headless test instructions
├── LICENSES/
│   └── JetBrainsMono-OFL.txt — SIL OFL 1.1 text for the bitmap font
├── LICENSE
└── asbestos.svg
```

## Documentation

- **[docs/DOCS.md](docs/DOCS.md)** — memory and disk layout, boot sequence,
  VBE mode selection, BootInfo, console rendering, IDT and panic path, keyboard
  and `brash`, limitations, and the milestone-2 plan
- **[docs/BUILD.md](docs/BUILD.md)** — build, run, verify the image, drive the
  shell headlessly, and test the exception path

## Roadmap

1. ~~IDT and exception handling~~ — done
2. PIC remap and PIT
3. PS/2 keyboard driver — polled first pass done; IRQ-driven input later
4. ATA PIO driver
5. FAT16/FAT32 read-only filesystem
6. **`brash`** — the Asbestos SHell — first iteration done; grow it once there is
   a filesystem
7. Write support and loadable modules

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).

The bitmap font in `src/font8x16.h` is derived from Terminus Bold and is
distributed under the SIL Open Font License, Version 1.1. See
[LICENSES/JetBrainsMono-OFL.txt](LICENSES/JetBrainsMono-OFL.txt).

---

*Built from the BIOS up, one `int` at a time. Now with more attitude.*
