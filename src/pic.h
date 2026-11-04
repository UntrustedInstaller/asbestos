#ifndef PIC_H
#define PIC_H

#include "types32.h"

/*
 * 8259 Programmable Interrupt Controller.
 *
 * Remaps IRQs out of the CPU exception range (default 0x08-0x0F collides
 * with #DE-#NP) to 0x20-0x2F. Polling/EOI helpers so the PIT and later
 * the keyboard can fire through the IDT instead of triple-faulting.
 */

/* Master 8259 ports */
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21

/* Slave 8259 ports */
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

/* ICW1: init, edge-triggered, ICW4 needed */
#define PIC_ICW1 0x11

/* ICW4: 8086/88 mode */
#define PIC_ICW4 0x01

/* Remapped offsets: master IRQs at 0x20, slave IRQs at 0x28 */
#define PIC1_OFFSET 0x20u
#define PIC2_OFFSET 0x28u

/* IRQ numbers for common devices */
#define IRQ_TIMER   0
#define IRQ_KEYBOARD 1

void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void pic_eoi(uint8_t irq);

#endif
