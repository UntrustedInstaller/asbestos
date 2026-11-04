; =====================================================================
;  asbestOS 32-bit - Stage 1 boot sector
; =====================================================================
; The BIOS enters here in 16-bit real mode. This sector does one job:
; load Stage 2 from LBA 1 into 0x0800:0x0000 and jump to it.
;
; Disk layout expected by the 32-bit image:
;   LBA 0        this boot sector
;   LBA 1..8     Stage 2 (8 sectors, padded)
;   LBA 9..264   kernel image (128 KiB, 4-byte size header + binary)
;
; Only the boot path is 16-bit. Everything after protected-mode entry is
; 32-bit code.
; =====================================================================

[org 0x7c00]
bits 16

STAGE2_LBA      equ 1
STAGE2_SECTORS  equ 8
STAGE2_SEG      equ 0x0800

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

    ; Require BIOS LBA read extensions. The 32-bit image is an ATA disk,
    ; so this is available on every target we care about.
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

times 510 - ($ - $$) db 0
dw 0xAA55
