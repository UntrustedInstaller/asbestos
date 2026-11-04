<img src="asbestos.svg" alt="asbestOS" width="50%">

An intentionally abrasive operating system inspired by INTERCAL.

## Current state: milestone 1, milestone 2 underway

The kernel boots, enters ring 0 with flat segments and no paging, selects a
32-bpp VBE mode, and draws an 80×25 text console in exact 24-bit colour:

- **Background:** `#4B2222`
- **Foreground:** `#C39393`

That is the whole of milestone 1. There is no keyboard, disk driver, filesystem,
or shell yet. It prints its diagnostics and halts.

Milestone 2 has started with an IDT and a panic screen: any CPU exception now
stops the machine with a readable oops-style report instead of a triple-fault
reboot. Hardware interrupts are still masked.

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
and `socat` for the headless screenshot check.

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
- **JetBrains Mono bitmap font** — 8×16 glyphs for ASCII `0x20`–`0x7F`,
  rasterised from JetBrains Mono at 13 px and licensed under the SIL OFL 1.1

## Project structure

```
├── build.sh            — Build pipeline; produces build/os.img
├── src/
│   ├── boot.asm        — Stage 1 boot sector
│   ├── stage2.asm      — Disk load, VBE setup, GDT, protected-mode entry
│   ├── entry32.asm     — 32-bit entry, BSS zeroing, C call
│   ├── kernel.c        — Milestone-1 kernel
│   ├── console.c       — VBE framebuffer 80×25 console
│   ├── console.h
│   ├── boot_info.h     — BootInfo passed from stage 2 to the kernel
│   ├── font8x16.h      — JetBrains Mono 8×16 bitmap (generated)
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
│   └── linker32.ld     — Links the kernel at 1 MiB
├── docs/
│   ├── 32BIT.md        — Architecture, boot path, console internals
│   └── BUILD.md        — Build, run, and headless test instructions
├── LICENSES/
│   └── JetBrainsMono-OFL.txt — SIL OFL 1.1 text for the bitmap font
├── LICENSE
└── asbestos.svg
```

## Documentation

- **[docs/32BIT.md](docs/32BIT.md)** — memory and disk layout, boot sequence,
  VBE mode selection, BootInfo, console rendering, IDT and panic path,
  limitations, and the milestone-2 plan
- **[docs/BUILD.md](docs/BUILD.md)** — build, run, verify the image, and test the
  exception path

## Roadmap

1. ~~IDT and exception handling~~ — done
2. PIC remap and PIT
3. PS/2 keyboard driver
4. ATA PIO driver
5. FAT16/FAT32 read-only filesystem
6. Shell and the alternating `please` / `sorry` manners gate
7. Write support and loadable modules

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).

The bitmap font in `src/font8x16.h` is derived from JetBrains Mono and is
distributed under the SIL Open Font License, Version 1.1. See
[LICENSES/JetBrainsMono-OFL.txt](LICENSES/JetBrainsMono-OFL.txt).

---

*Built from the BIOS up, one `int` at a time. Now with more attitude.*
