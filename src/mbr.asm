; =====================================================================
;  asbestOS - MBR + Stage 1 boot code (combined)
; =====================================================================
; The BIOS loads sector 0 (this MBR) and executes it. The first 446 bytes
; are boot code that loads Stage 2 from LBA 1 and jumps to it. The remaining
; 66 bytes are the partition table and 0x55AA signature, so tools like PLoP
; recognize this as a bootable USB-HDD.
;
; Disk layout:
;   LBA 0        MBR (Stage 1 boot code + partition table + signature)
;   LBA 1..8     Stage 2 loader (8 sectors, padded)
;   LBA 9..264   kernel image (128 KiB max, 4-byte size header + binary)
;   LBA 265..    FAT12 partition (also embedded in the kernel)
; =====================================================================

[org 0x7c00]
bits 16

STAGE2_LBA      equ 1
STAGE2_SECTORS  equ 8
STAGE2_SEG      equ 0x0800

; The partition table describes the FAT partition, which lives after the
; kernel region. build.sh passes the real values.
%ifndef FAT_LBA
%define FAT_LBA 265
%endif
%ifndef FAT_SECTORS
%define FAT_SECTORS 128
%endif
%ifndef FAT_TYPE
%define FAT_TYPE 0x01
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; DL is the BIOS boot drive. Keep it for Stage 2.
    mov [boot_drive], dl

    ; Require BIOS LBA read extensions.
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .no_lba
    cmp bx, 0xAA55
    jne .no_lba

    ; Disk Address Packet for "read Stage 2".
    mov word [dap], 16
    mov word [dap + 2], STAGE2_SECTORS
    mov word [dap + 4], 0
    mov word [dap + 6], STAGE2_SEG
    mov dword [dap + 8], STAGE2_LBA
    mov dword [dap + 12], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .read_fail

    jmp STAGE2_SEG:0x0000

.no_lba:
    mov si, msg_no_lba
    jmp print_halt

.read_fail:
    mov si, msg_read
    jmp print_halt

; ---------------------------------------------------------------------
;  Real-mode text output. This is only used before Stage 2 changes the
;  video mode.
; ---------------------------------------------------------------------
print_halt:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_halt
.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------
;  Data
; ---------------------------------------------------------------------
boot_drive db 0
align 4
dap times 16 db 0

msg_no_lba db "asbestOS: BIOS LBA disk reads unavailable.", 13, 10, 0
msg_read   db "asbestOS: Stage 2 read failed.", 13, 10, 0

; ---------------------------------------------------------------------
;  Pad boot code to 446 bytes (offset 0x1BE = start of partition table)
; ---------------------------------------------------------------------
times 446 - ($ - $$) db 0

; ---------------------------------------------------------------------
;  Partition table entry 1 (16 bytes at offset 0x1BE)
; ---------------------------------------------------------------------
    db 0x80                              ; boot flag: bootable
    db 0xFE, 0xFF, 0xFF                  ; CHS start: max (LBA is authoritative)
    db FAT_TYPE                          ; type: FAT12 (0x01)
    db 0xFE, 0xFF, 0xFF                  ; CHS end: max
    dd FAT_LBA                           ; LBA start of the FAT partition
    dd FAT_SECTORS                       ; sector count of the FAT partition

; ---------------------------------------------------------------------
;  Partition table entries 2-4 (empty, 48 bytes)
; ---------------------------------------------------------------------
    times 48 db 0

; ---------------------------------------------------------------------
;  MBR signature
; ---------------------------------------------------------------------
times 510 - ($ - $$) db 0
dw 0xAA55
