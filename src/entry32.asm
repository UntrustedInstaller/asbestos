; =====================================================================
;  asbestOS 32-bit - kernel entry
; =====================================================================
; Entered at 0x00100000 after Stage 2 has copied the kernel image from
; low memory. This file sets up a flat 32-bit environment, clears BSS,
; and calls the C kernel.
; =====================================================================

bits 32

global _start
extern kernel_main
extern _bss_start
extern _bss_end

section .text
_start:
    cli

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; The stack lives below the 1 MiB kernel region.
    mov esp, 0x00090000
    xor ebp, ebp

    ; The binary image contains .text/.rodata/.data only, so BSS must
    ; be zeroed here.
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
