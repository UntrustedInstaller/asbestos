# Building asbestOS

This tree builds the 32-bit milestone-1 image: protected mode plus a VBE
framebuffer console. See [32BIT.md](32BIT.md) for the architecture.

## Prerequisites

- NASM
- GCC with 32-bit support (`gcc-multilib` on Debian/Ubuntu)
- GNU `ld` and `objcopy`
- Python 3 (writes the kernel size header)
- QEMU (optional, for testing)
- `socat` and ImageMagick (optional, for the headless screenshot check)

On Debian/Ubuntu:

```bash
sudo apt install nasm gcc-multilib qemu-system-x86 socat imagemagick
```

## Build

```bash
./build.sh
```

The script:

1. Assembles `src/boot.asm` into a 512-byte boot sector.
2. Assembles `src/stage2.asm`, checks that it fits in 8 sectors, and pads it.
3. Assembles `src/entry32.asm`.
4. Compiles `src/kernel.c`, `console.c`, `string.c`, and `serial.c` with
   `-m32 -march=i386`.
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
asbestOS 32-bit kernel entered
Milestone 1 complete; halting in kernel_main
```

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

## Troubleshooting

**The build says stage 2 is too big.** Raise `STAGE2_SECTORS` in `src/boot.asm`
and `build.sh` together. Stage 2 is loaded from LBA 1 and must end before the
kernel at LBA 9.

**The build says the kernel is too big.** Raise `KERNEL_SECTORS` in
`src/stage2.asm` and update the image layout notes in `build.sh` and
[docs/32BIT.md](32BIT.md). The kernel must still fit in the 128 KiB staging
area at `0x10000`; raise the stage-2 copy loop and the disk region together.

**“no 32-bpp VBE linear framebuffer mode found.”** The firmware did not offer an
acceptable mode. Try a different QEMU `-vga` option, or a machine with VBE 3.0
support.

**Serial output is empty.** Check that `-serial stdio` (or
`-serial file:...`) is present and that QEMU is actually booting the disk. A
firmware rejected by stage 2 will fall back to BIOS text mode instead of
reaching the kernel.
