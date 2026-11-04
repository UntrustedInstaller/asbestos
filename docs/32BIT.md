# asbestOS 32-bit — Milestone 1

> **Status:** working draft. Milestone 1 boots, renders, and halts correctly
> under QEMU. This document describes the entire current tree in `src/`.

Milestone 1 is deliberately narrow: leave the BIOS behind, enter flat 32-bit
protected mode, put the machine into a VBE linear-framebuffer mode, and draw an
80×25 text console in exactly the two colours the project requires. There is no
interrupt controller, no keyboard driver, no filesystem, and no shell yet. Those
are milestone 2.

**Only the BIOS-required boot stub and stage 2 loader are 16-bit.** Once the CPU
enters protected mode, every instruction executed is 32-bit.

---

## 1. Target architecture

| Property        | Milestone 1 choice                                   |
|-----------------|------------------------------------------------------|
| CPU mode        | 32-bit protected mode                                |
| Privilege level | Ring 0                                               |
| Segmentation    | Flat: code `0x08`, data `0x10`, base 0, limit 4 GiB  |
| Paging          | Off                                                  |
| Interrupts      | Disabled after protected-mode entry                  |
| Kernel location | Linked and run at `0x00100000` (1 MiB)               |
| Kernel stack    | Top at `0x00090000`, grows down                      |
| Firmware        | BIOS (no UEFI)                                       |
| Display         | VBE linear framebuffer, 32 bpp                       |
| Storage target  | ATA PIO (not used yet), FAT16/FAT32 later            |

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
├── kernel.c        Milestone-1 kernel: validate BootInfo, print, halt
├── console.c       VBE framebuffer 80×25 text console
├── console.h
├── boot_info.h     BootInfo struct shared with stage2.asm
├── font8x16.h      Public-domain IBM VGA 8×16 glyphs, ASCII 0x20–0x7F
├── serial.c        COM1 output for headless testing
├── serial.h
├── string.c        memset/memcpy/memmove/strlen/strcmp
├── string.h
├── io.h            Port I/O and CPU helpers
├── types32.h       Fixed-width types (freestanding, no libc)
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
   - Initialises COM1.
   - Validates `BootInfo`.
   - Initialises the console, prints the banner and diagnostics, and halts.

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
one byte per scanline with the most-significant bit on the left. It is the
public-domain IBM VGA 8×16 bitmap font extracted from the standard Linux
`default8x16.psfu` console font.

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

## 8. Build and run

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
asbestOS 32-bit kernel entered
Milestone 1 complete; halting in kernel_main
```

A correct screenshot contains only two colours: `#4B2222` and `#C39393`.

---

## 9. Current limitations

Milestone 1 intentionally lacks:

- **Interrupts** — no IDT, no PIC remap, no clock, no keyboard.
- **Input** — the console is output-only.
- **Storage** — no ATA PIO driver and no FAT16/FAT32 layer yet.
- **Shell** — no command loop, no manners gate, no user commands.
- **Memory management** — no paging, no allocator, no memory map parsing.
- **Multiple consoles** — one global console, no colour attributes.
- **Text modes** — VBE graphics only; if no 32-bpp linear mode exists, boot fails
  with a BIOS text message.
- **Unicode** — ASCII `0x20`–`0x7F` only.
- **Cursor blinking** — the underline is static.

The kernel also assumes a single CPU, no SMIs interfering with the framebuffer,
and a firmware that leaves A20 controllable through port `0x92`.

---

## 10. Milestone 2 and beyond

The intended order is:

1. **IDT and exception handling.** Catch faults with a readable panic screen
   instead of a triple-fault reboot.
2. **PIC remap and PIT.** A timer tick is the foundation for input timeouts and
   later scheduling.
3. **PS/2 keyboard driver.** Scan-code set 1, shifted characters, arrows,
   backspace, Enter.
4. **ATA PIO driver.** Identify the disk from `BootInfo.boot_drive`, read sectors
   with LBA28. Polling is fine to start.
5. **FAT16/FAT32 read-only layer.** BPB validation, cluster chains, directory
   traversal, `open`/`read`.
6. **Shell and manners gate.** Reintroduce the alternating `please` / `sorry`
   protocol, exact error strings, and no compound statements.
7. **Write support and loadable modules.** Only after the read path is stable.

Each step should keep the serial log and screenshot checks working so
regressions are obvious.

---

## 11. Design notes and gotchas

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

---

*Milestone 1: the screen is the right colour, the CPU is the right width, and
the machine stops when asked. That is enough to build on.*
