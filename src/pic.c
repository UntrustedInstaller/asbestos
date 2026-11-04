#include "pic.h"
#include "io.h"

static uint16_t irq_mask = 0xFFFFu; /* all IRQs masked by default */

void pic_remap(uint8_t master_offset, uint8_t slave_offset) {
    /* ICW1: start initialization on both controllers */
    outb(PIC1_COMMAND, PIC_ICW1);
    io_wait();
    outb(PIC2_COMMAND, PIC_ICW1);
    io_wait();

    /* ICW2: vector offsets */
    outb(PIC1_DATA, master_offset);
    io_wait();
    outb(PIC2_DATA, slave_offset);
    io_wait();

    /* ICW3: master tells slave is on IRQ2; slave tells master its identity */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, PIC_ICW4);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4);
    io_wait();

    /* Restore saved masks (all masked for now) */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    irq_mask = 0xFFFFu;
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) | (1u << irq);
    outb(port, value);
    irq_mask |= (1u << irq);
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) & ~(1u << irq);
    outb(port, value);
    irq_mask &= ~(1u << irq);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20); /* slave EOI */
    }
    outb(PIC1_COMMAND, 0x20); /* master EOI */
}
