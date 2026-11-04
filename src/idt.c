#include "idt.h"
#include "io.h"
#include "keyboard.h"
#include "panic.h"
#include "pic.h"
#include "pit.h"
#include "string.h"

#define IDT_ENTRIES 256u
#define KERNEL_CS 0x08u
#define GATE_INTERRUPT 0x8Eu /* present, ring 0, 32-bit interrupt gate */

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

/* One stub per vector, plus a table of their addresses, both in isr.asm. */
extern void (*isr_stub_table[IDT_ENTRIES])(void);

static void idt_set_gate(uint32_t vector, void (*handler)(void)) {
    uint32_t address = (uint32_t)handler;

    idt[vector].offset_low = (uint16_t)(address & 0xFFFFu);
    idt[vector].selector = KERNEL_CS;
    idt[vector].zero = 0;
    idt[vector].type_attr = GATE_INTERRUPT;
    idt[vector].offset_high = (uint16_t)((address >> 16) & 0xFFFFu);
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));

    for (uint32_t vector = 0; vector < IDT_ENTRIES; vector++) {
        idt_set_gate(vector, isr_stub_table[vector]);
    }

    idtp.limit = (uint16_t)(sizeof(idt) - 1u);
    idtp.base = (uint32_t)&idt[0];
    __asm__ __volatile__("lidt %0" : : "m"(idtp));
}

static const char *exception_name(uint32_t vector) {
    switch (vector) {
        case 0:  return "Divide Error (#DE)";
        case 1:  return "Debug (#DB)";
        case 2:  return "Non-Maskable Interrupt";
        case 3:  return "Breakpoint (#BP)";
        case 4:  return "Overflow (#OF)";
        case 5:  return "BOUND Range Exceeded (#BR)";
        case 6:  return "Invalid Opcode (#UD)";
        case 7:  return "Device Not Available (#NM)";
        case 8:  return "Double Fault (#DF)";
        case 9:  return "Coprocessor Segment Overrun";
        case 10: return "Invalid TSS (#TS)";
        case 11: return "Segment Not Present (#NP)";
        case 12: return "Stack-Segment Fault (#SS)";
        case 13: return "General Protection Fault (#GP)";
        case 14: return "Page Fault (#PF)";
        case 15: return "Reserved Exception";
        case 16: return "x87 Floating-Point Exception (#MF)";
        case 17: return "Alignment Check (#AC)";
        case 18: return "Machine Check (#MC)";
        case 19: return "SIMD Floating-Point Exception (#XM)";
        case 20: return "Virtualization Exception (#VE)";
        case 21: return "Control Protection Exception (#CP)";
        case 28: return "Hypervisor Injection Exception (#HV)";
        case 29: return "VMM Communication Exception (#VC)";
        case 30: return "Security Exception (#SX)";
        default: return "Reserved Exception";
    }
}

void isr_dispatch(struct trap_frame *frame) {
    if (frame->vector < 32u) {
        panic(exception_name(frame->vector), frame);
    }

    if (frame->vector < 48u) {
        uint8_t irq = (uint8_t)(frame->vector - 32u);

        if (irq == IRQ_KEYBOARD) {
            keyboard_irq_handler();
        } else {
            pic_eoi(irq);
        }
        return;
    }

    panic("Unexpected interrupt", frame);
}
