; =====================================================================
;  asbestOS - Stage 2 loader
; =====================================================================
; Stage 2 runs in 16-bit real mode because both BIOS disk calls and VBE
; mode selection require it. It:
;   1. loads the kernel image into low memory,
;   2. asks VBE for a 32-bpp linear framebuffer mode,
;   3. writes a BootInfo block at 0x5000,
;   4. enables A20, installs a flat GDT, enters protected mode,
;   5. copies the kernel to 0x00100000 and jumps to it.
;
; Memory used while still in real mode:
;   0x0500..0x051F  BootInfo (32 bytes)
;   0x6000..0x61FF  VBE controller info (512 bytes)
;   0x6200..0x62FF  VBE mode info (256 bytes)
;   0x7C00          Stage 2 stack (grows down)
;   0x8000..        this loader
;   0x10000..0x2FFFF  kernel image (128 KiB max, 4-byte size header)
;
; The kernel is linked at 0x00100000.
; =====================================================================

[org 0x8000]
bits 16

BOOT_INFO       equ 0x5000
VBE_CTRL        equ 0x6000
VBE_MODE        equ 0x6200
KERNEL_LOAD     equ 0x10000
KERNEL_LBA      equ 9
KERNEL_SECTORS  equ 256          ; 128 KiB
KERNEL_MAX      equ 128 * 1024 - 4

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Load first, while real-mode text output still works.
    call load_kernel
    call vbe_set_mode

    ; VBE calls should not touch A20, but enabling it again is harmless.
    call enable_a20
    call enter_pm

; =====================================================================
;  Kernel loading
; =====================================================================
; 256 sectors = four 64-sector (32 KiB) BIOS reads. Keeping each read
; inside a 64 KiB window avoids BIOS transfer-size surprises.
load_kernel:
    mov word [chunk], 0
.loop:
    cmp word [chunk], 4
    jae .loaded

    ; Buffer segment = 0x1000 + chunk * 0x800
    mov ax, [chunk]
    shl ax, 11
    add ax, 0x1000
    mov [dap + 6], ax

    ; LBA = 9 + chunk * 64
    mov ax, [chunk]
    shl ax, 6
    add ax, KERNEL_LBA
    mov [dap + 8], ax
    mov word [dap + 10], 0
    mov dword [dap + 12], 0

    mov word [dap], 16
    mov word [dap + 2], 64
    mov word [dap + 4], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .read_fail

    inc word [chunk]
    jmp .loop

.loaded:
    ; The kernel image begins with a little-endian u32 byte count.
    ; KERNEL_LOAD is above 64 KiB, so use a 32-bit addressing register
    ; rather than NASM's default 16-bit absolute displacement.
    mov ebx, KERNEL_LOAD
    mov eax, [ebx]
    test eax, eax
    jz .bad_size
    cmp eax, KERNEL_MAX
    ja .bad_size
    ret

.read_fail:
    mov si, msg_read
    jmp print_halt
.bad_size:
    mov si, msg_size
    jmp print_halt

; =====================================================================
;  VBE mode selection
; =====================================================================
; Iterate the controller's mode list and choose the supported 32-bpp
; linear-framebuffer mode closest to 640x480. This keeps the console
; geometry predictable while tolerating BIOS mode-list differences.
vbe_set_mode:
    mov ax, 0x4F00
    mov di, VBE_CTRL
    xor bx, bx
    mov es, bx
    int 0x10
    cmp ax, 0x004F
    jne .fail
    cmp dword [VBE_CTRL], 0x41534556       ; "VESA"
    jne .fail

    mov ax, [VBE_CTRL + 0x0E]
    mov [mode_off], ax
    mov ax, [VBE_CTRL + 0x10]
    mov [mode_seg], ax
    mov word [best_mode], 0
    mov word [best_score], 0xFFFF

.next_mode:
    mov ax, [mode_seg]
    mov fs, ax
    mov si, [mode_off]
    mov cx, [fs:si]
    add si, 2
    mov [mode_off], si
    cmp cx, 0xFFFF
    je .choose

    call check_mode
    jmp .next_mode

.choose:
    mov cx, [best_mode]
    test cx, cx
    jz .fail

    ; 0x4000 asks VBE to use the linear framebuffer.
    mov ax, 0x4F02
    mov bx, cx
    or bx, 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .fail

    ; Re-query after the mode set. Some BIOSes only fill the linear
    ; pitch fields once the mode is active.
    mov ax, 0x4F01
    mov cx, [best_mode]
    mov di, VBE_MODE
    xor bx, bx
    mov es, bx
    int 0x10
    cmp ax, 0x004F
    jne .fail

    call fill_boot_info
    ret

.fail:
    ; Return to 80x25 text so the error is visible.
    mov ax, 0x0003
    int 0x10
    mov si, msg_vbe
    jmp print_halt

; ---------------------------------------------------------------------
;  check_mode: score one VBE mode
;  Input:  CX = mode number
;  Uses:   VBE_MODE buffer, best_mode / best_score
; ---------------------------------------------------------------------
check_mode:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, 0x4F01
    mov di, VBE_MODE
    xor bx, bx
    mov es, bx
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov ax, [VBE_MODE + 0x00]
    test ax, 0x0001                 ; mode supported
    jz .done
    test ax, 0x0010                 ; graphics mode
    jz .done
    test ax, 0x0080                 ; linear framebuffer available
    jz .done

    cmp byte [VBE_MODE + 0x19], 32  ; 32 bits per pixel
    jne .done

    mov al, [VBE_MODE + 0x1B]       ; memory model
    cmp al, 4                       ; packed pixel
    je .model_ok
    cmp al, 6                       ; direct colour
    jne .done
.model_ok:
    cmp byte [VBE_MODE + 0x1F], 0   ; red mask
    je .done
    cmp byte [VBE_MODE + 0x21], 0   ; green mask
    je .done
    cmp byte [VBE_MODE + 0x23], 0   ; blue mask
    je .done

    mov ax, [VBE_MODE + 0x12]       ; width
    cmp ax, 640
    jb .done
    mov bx, [VBE_MODE + 0x14]       ; height
    cmp bx, 400
    jb .done

    ; Score = |width - 640| + |height - 480|
    mov dx, ax
    sub dx, 640
    mov ax, bx
    sub ax, 480
    jae .height_ok
    neg ax
.height_ok:
    add dx, ax
    cmp dx, [best_score]
    jae .done
    mov [best_score], dx
    mov [best_mode], cx

.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------
;  fill_boot_info: copy the selected mode into the C-visible struct
; ---------------------------------------------------------------------
fill_boot_info:
    mov dword [BOOT_INFO + 0], 0x33425341   ; "ASB3"
    mov eax, [VBE_MODE + 0x28]              ; physical framebuffer address
    mov [BOOT_INFO + 4], eax

    ; VBE 3.0 has LinBytesPerScanLine at 0x32. Fall back to BytesPerScanLine.
    movzx eax, word [VBE_MODE + 0x32]
    test eax, eax
    jnz .pitch_ok
    movzx eax, word [VBE_MODE + 0x10]
.pitch_ok:
    mov [BOOT_INFO + 8], eax

    mov ax, [VBE_MODE + 0x12]
    mov [BOOT_INFO + 12], ax                ; width
    mov ax, [VBE_MODE + 0x14]
    mov [BOOT_INFO + 14], ax                ; height

    mov al, [VBE_MODE + 0x19]
    mov [BOOT_INFO + 16], al                ; bpp
    mov al, [VBE_MODE + 0x1F]
    mov [BOOT_INFO + 17], al                ; red mask size
    mov al, [VBE_MODE + 0x20]
    mov [BOOT_INFO + 18], al                ; red position
    mov al, [VBE_MODE + 0x21]
    mov [BOOT_INFO + 19], al                ; green mask size
    mov al, [VBE_MODE + 0x22]
    mov [BOOT_INFO + 20], al                ; green position
    mov al, [VBE_MODE + 0x23]
    mov [BOOT_INFO + 21], al                ; blue mask size
    mov al, [VBE_MODE + 0x24]
    mov [BOOT_INFO + 22], al                ; blue position

    mov al, [boot_drive]
    mov [BOOT_INFO + 23], al

    mov ax, [best_mode]
    mov [BOOT_INFO + 24], ax                ; selected VBE mode number
    mov word [BOOT_INFO + 26], 0
    mov dword [BOOT_INFO + 28], 0
    ret

; =====================================================================
;  Protected-mode entry
; =====================================================================
enable_a20:
    in al, 0x92
    test al, 2
    jnz .done
    or al, 2
    and al, 0xFE
    out 0x92, al
.done:
    ret

enter_pm:
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp dword 0x08:pm_entry

; ---------------------------------------------------------------------
;  32-bit code starts here
; ---------------------------------------------------------------------
bits 32

pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00090000
    mov ebp, esp

    ; Copy the kernel from the 16-bit load address to its link address.
    ; The first dword at KERNEL_LOAD is the byte count written by the
    ; build script; the binary itself starts at KERNEL_LOAD + 4.
    mov esi, KERNEL_LOAD + 4
    mov edi, 0x00100000
    mov ecx, [KERNEL_LOAD]
    add ecx, 3
    shr ecx, 2
    cld
    rep movsd

    jmp 0x00100000

.hang:
    cli
    hlt
    jmp .hang

; =====================================================================
;  Real-mode helper and data
; =====================================================================
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

align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0
    db 10011010b
    db 11001111b
    db 0
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0
    db 10010010b
    db 11001111b
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_drive  db 0
align 4
dap         times 16 db 0
chunk       dw 0
mode_off    dw 0
mode_seg    dw 0
best_mode   dw 0
best_score  dw 0

msg_read    db "asbestOS: kernel read failed.", 13, 10, 0
msg_size    db "asbestOS: kernel size header is invalid.", 13, 10, 0
msg_vbe     db "asbestOS: no 32-bpp VBE linear framebuffer mode found.", 13, 10, 0
