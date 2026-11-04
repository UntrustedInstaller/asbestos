#ifndef IO32_H
#define IO32_H

#include "types32.h"

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cpu_halt(void) {
    __asm__ __volatile__("hlt");
}

static inline void cpu_cli(void) {
    __asm__ __volatile__("cli");
}

static inline void cpu_sti(void) {
    __asm__ __volatile__("sti");
}

#endif
