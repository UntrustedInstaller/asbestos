#!/bin/bash
# =====================================================================
#  asbestOS
# =====================================================================
# Produces build/os.img, a raw disk image with an MBR partition table:
#   LBA 0        MBR + Stage 1 boot code
#   LBA 1..8     Stage 2 loader (8 sectors, padded)
#   LBA 9..264   kernel image (128 KiB max, 4-byte size header + binary)
#   LBA 265..    FAT12 partition with example files
#
# The FAT partition is appended as-is so a host can mount it, and the same
# image is linked into the kernel so the shell works when booting from USB
# or an AHCI-only controller (where the kernel cannot reach the medium).
#
# Run with QEMU:
#   qemu-system-i386 -drive file=build/os.img,format=raw,if=ide \
#                    -vga std -serial stdio
#
# Or:
#   ./build.sh --run
#
# Running ./build.sh in a terminal also offers to boot the image when the
# build finishes. Pass --no-run to skip that prompt.
# =====================================================================
set -e

OUT=build
STAGE2_SECTORS=8
KERNEL_BYTES=$((128 * 1024))
KERNEL_MAX=$((KERNEL_BYTES - 4))

# Partition offset: 0 = MBR is Stage 1 (boot code at LBA 0).
#                   1 = MBR at LBA 0, partition at LBA 1 (not used).
PART_OFFSET=0
# Partition size in sectors (must cover Stage 2 + kernel region).
PART_SECTORS=$((STAGE2_SECTORS + 256))

# FAT partition layout. It lives right after the 128 KiB kernel region and
# is both embedded in the kernel and appended to the disk image.
FAT_LBA=265
FAT_KB=64
FAT_SECTORS=$((FAT_KB * 2))   # 512-byte sectors
FAT_TYPE=0x01                 # FAT12

mkdir -p "$OUT"

echo "=============================================="
echo "               ASBESTOS BUILDER               "  
echo "=============================================="

echo "[*] Assembling MBR + Stage 1 (src/mbr.asm)..."
nasm -f bin -dPART_OFFSET=$PART_OFFSET -dPART_SECTORS=$PART_SECTORS \
    -dFAT_LBA=$FAT_LBA -dFAT_SECTORS=$FAT_SECTORS -dFAT_TYPE=$FAT_TYPE \
    src/mbr.asm -o "$OUT/mbr.bin"

echo "[*] Assembling Stage 2 (src/stage2.asm)..."
nasm -f bin -dPART_OFFSET=$PART_OFFSET \
    src/stage2.asm -o "$OUT/stage2.raw.bin"

STAGE2_SIZE=$(stat -c%s "$OUT/stage2.raw.bin")
STAGE2_MAX=$((STAGE2_SECTORS * 512))
if (( STAGE2_SIZE > STAGE2_MAX )); then
    echo "    !! Stage 2 is ${STAGE2_SIZE} bytes; the loader reserves ${STAGE2_MAX}." >&2
    echo "    !! Increase STAGE2_SECTORS in src/boot.asm and build.sh." >&2
    exit 1
fi
cp "$OUT/stage2.raw.bin" "$OUT/stage2.bin"
truncate -s "$STAGE2_MAX" "$OUT/stage2.bin"

echo "[*] Assembling kernel entry (src/entry32.asm)..."
nasm -f elf32 src/entry32.asm -o "$OUT/entry32.o"

echo "[*] Assembling interrupt stubs (src/isr.asm)..."
nasm -f elf32 src/isr.asm -o "$OUT/isr.o"

CFLAGS="-m32 -march=i386 -mtune=i386 -ffreestanding -fno-builtin \
        -fno-pic -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -fno-unwind-tables \
        -nostdlib -O2 -Wall -Wextra -Isrc"

for src in kernel console string serial idt panic keyboard shell ata_pio pic pit fat paging; do
    echo "[*] Compiling src/${src}.c..."
    # shellcheck disable=SC2086
    gcc $CFLAGS -c "src/${src}.c" -o "$OUT/${src}.o"
done

# Build a small FAT12 image with example files. It is embedded in the
# kernel so the shell works when the boot medium is not reachable through
# the ATA driver (USB, AHCI-only controllers).
FAT_IMG="$OUT/fat_part.img"
echo "[*] Building embedded FAT image..."
rm -f "$FAT_IMG"
mkfs.fat -C -F 12 "$FAT_IMG" "$FAT_KB" >/dev/null

TMP_FAT=$(mktemp -d)
echo "HELLO WORLD" > "$TMP_FAT/HELLO.TXT"
echo "This is a test file for the brash shell FAT driver." > "$TMP_FAT/README.TXT"
mkdir -p "$TMP_FAT/SUBDIR"
echo "nested file content" > "$TMP_FAT/SUBDIR/NESTED.TXT"
mcopy -i "$FAT_IMG" "$TMP_FAT/HELLO.TXT" "$TMP_FAT/README.TXT" ::
mmd -i "$FAT_IMG" ::SUBDIR 2>/dev/null
mcopy -i "$FAT_IMG" "$TMP_FAT/SUBDIR/NESTED.TXT" ::SUBDIR/ 2>/dev/null
rm -rf "$TMP_FAT"

FAT_BYTES=$(stat -c%s "$FAT_IMG")
if (( FAT_BYTES != FAT_SECTORS * 512 )); then
    echo "    !! FAT image is ${FAT_BYTES} bytes; expected $((FAT_SECTORS * 512))." >&2
    exit 1
fi

objcopy -I binary -O elf32-i386 -B i386 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$FAT_IMG" "$OUT/fat_part.o"

echo "[*] Linking kernel..."
ld -m elf_i386 -T src/linker32.ld \
    "$OUT/entry32.o" \
    "$OUT/isr.o" \
    "$OUT/kernel.o" \
    "$OUT/console.o" \
    "$OUT/string.o" \
    "$OUT/serial.o" \
    "$OUT/idt.o" \
    "$OUT/panic.o" \
    "$OUT/keyboard.o" \
    "$OUT/shell.o" \
    "$OUT/ata_pio.o" \
    "$OUT/pic.o" \
    "$OUT/pit.o" \
    "$OUT/fat.o" \
    "$OUT/paging.o" \
    "$OUT/fat_part.o" \
    -o "$OUT/kernel.elf"

objcopy -O binary -j .text -j .rodata -j .data "$OUT/kernel.elf" "$OUT/kernel.bin"

KERNEL_SIZE=$(stat -c%s "$OUT/kernel.bin")
if (( KERNEL_SIZE > KERNEL_MAX )); then
    echo "    !! Kernel is ${KERNEL_SIZE} bytes; Stage 2 loads at most ${KERNEL_MAX}." >&2
    echo "    !! Increase KERNEL_SECTORS in src/stage2.asm and rebuild." >&2
    exit 1
fi

# The loader reads a u32 byte count followed by the flat kernel binary.
python3 - "$OUT/kernel.bin" "$OUT/kernel.hdr" <<'PY'
import struct
import sys

data = open(sys.argv[1], "rb").read()
with open(sys.argv[2], "wb") as out:
    out.write(struct.pack("<I", len(data)))
    out.write(data)
PY
truncate -s "$KERNEL_BYTES" "$OUT/kernel.hdr"

echo "[*] Assembling raw disk image..."
# Image layout:
#   LBA 0:       mbr.bin       (512 bytes)  MBR + Stage 1 boot code
#   LBA 1..8:    stage2.bin    (4096 bytes)
#   LBA 9..264:  kernel.hdr    (128 KiB)
#   LBA 265..:   fat_part.img  (FAT12 partition with example files)
# The kernel region ends at LBA 264, so the FAT image starts at LBA 265.
cat "$OUT/mbr.bin" "$OUT/stage2.bin" "$OUT/kernel.hdr" "$FAT_IMG" > "$OUT/os.img"

# Pad the image. Some BIOSes (and SeaBIOS on q35) refuse to boot a disk
# image that is too small. Only grow the image; never shrink it, or a large
# FAT partition would be cut off.
IMAGE_BYTES=$(stat -c%s "$OUT/os.img")
if (( IMAGE_BYTES < 4 * 1024 * 1024 )); then
    truncate -s 4M "$OUT/os.img"
fi

echo "[+] Build complete: $OUT/os.img"
echo "    MBR+Stage1: 512 bytes"
echo "    Stage 2:    ${STAGE2_SIZE} bytes (padded to ${STAGE2_MAX})"
echo "    Kernel:     ${KERNEL_SIZE} bytes (limit ${KERNEL_MAX})"
echo "    Disk:       $(stat -c%s "$OUT/os.img") bytes"

if [ "${1:-}" = "--run" ]; then
    RUN_QEMU=1
elif [ "${1:-}" = "--no-run" ]; then
    RUN_QEMU=0
elif [ -t 0 ]; then
    RUN_QEMU=0
    read -r -p "[?] Boot in QEMU now? [y/N] " answer || true
    case "$answer" in
        [yY] | [yY][eE][sS]) RUN_QEMU=1 ;;
    esac
else
    RUN_QEMU=0
fi

if [ "$RUN_QEMU" = 1 ]; then
    echo "[*] Launching QEMU..."
    exec qemu-system-i386 \
        -drive file="$OUT/os.img",format=raw,if=ide \
        -vga std \
        -serial stdio
fi
