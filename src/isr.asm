; =====================================================================
;  asbestOS 32-bit - interrupt and exception stubs
; =====================================================================
; The CPU pushes an error code for some exceptions but not others. Each
; stub normalises the stack by pushing a vector number and, when the CPU
; did not supply one, a zero error code. Everything then joins
; isr_common, which saves the general registers and calls C.
;
; Stack when isr_dispatch() is called:
;
;       esp -> struct trap_frame {
;                edi, esi, ebp, esp, ebx, edx, ecx, eax,
;                vector, error, eip, cs, eflags
;              }
; =====================================================================

bits 32

section .text

extern isr_dispatch

%assign vector 0
%rep 256
global isr %+ vector
isr %+ vector:
%if vector == 8 || vector == 10 || vector == 11 || vector == 12 || \
    vector == 13 || vector == 14 || vector == 17 || vector == 21 || \
    vector == 29 || vector == 30
    ; The CPU already pushed an error code.
    push dword vector
%else
    push dword 0
    push dword vector
%endif
    jmp isr_common
%assign vector vector + 1
%endrep

isr_common:
    cld
    pushad

    ; The interrupted code should already use the flat data selector, but
    ; do not trust it: make the C handler's memory accesses well defined.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call isr_dispatch
    add esp, 4

    popad
    add esp, 8          ; drop the vector and error code
    iretd

section .rodata
align 4

global isr_stub_table
isr_stub_table:
%assign vector 0
%rep 256
    dd isr %+ vector
%assign vector vector + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
