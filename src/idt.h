#ifndef IDT32_H
#define IDT32_H

#include "types32.h"

/*
 * The frame the stub in isr.asm builds before calling isr_dispatch().
 *
 * pushad pushes eax, ecx, edx, ebx, esp, ebp, esi, edi (in that order), so
 * edi ends up at the lowest address. The stub then pushes the vector and the
 * error code. The CPU already pushed eip, cs, and eflags when the exception
 * fired.
 */
struct trap_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t vector;
    uint32_t error;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

void idt_init(void);
void isr_dispatch(struct trap_frame *frame);

#endif
