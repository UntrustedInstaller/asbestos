<img src="asbestos.svg" alt="asbestOS" width="50%">

# asbestOS

*difficult. by design.*

An intentionally abrasive operating system, built from the BIOS up with **NASM
assembly** and **C**. asbestOS boots into flat 32-bit protected mode and draws
its own text console on a VBE linear framebuffer.

## Current state: milestone 1

The kernel boots, enters ring 0 with flat segments and no paging, selects a
32-bpp VBE mode, and draws an 80×25 text console in exact 24-bit colour:

- **Background:** `#4B2222`
- **Foreground:** `#C39393`

That is the whole of milestone 1. There is no keyboard, disk driver, filesystem,
or shell yet. It prints its diagnostics and halts.

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
- **Public-domain IBM VGA font** — 8×16 glyphs for ASCII `0x20`–`0x7F`

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
│   ├── font8x16.h      — Public-domain IBM VGA font
│   ├── serial.c        — COM1 output for testing
│   ├── serial.h
│   ├── string.c        — Freestanding memory/string helpers
│   ├── string.h
│   ├── io.h            — Port I/O and CPU helpers
│   ├── types32.h       — Fixed-width types
│   └── linker32.ld     — Links the kernel at 1 MiB
├── docs/
│   ├── 32BIT.md        — Architecture, boot path, console internals
│   └── BUILD.md        — Build, run, and headless test instructions
├── LICENSE
└── asbestos.svg
```

## Documentation

- **[docs/32BIT.md](docs/32BIT.md)** — memory and disk layout, boot sequence,
  VBE mode selection, BootInfo, console rendering, limitations, and the
  milestone-2 plan
- **[docs/BUILD.md](docs/BUILD.md)** — build, run, and verify the image

## Roadmap

1. IDT and exception handling
2. PIC remap and PIT
3. PS/2 keyboard driver
4. ATA PIO driver
5. FAT16/FAT32 read-only filesystem
6. Shell and the alternating `please` / `sorry` manners gate
7. Write support and loadable modules

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).

---

*Built from the BIOS up, one `int` at a time. Now with more attitude.*
