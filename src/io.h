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

static inline void cpu_pause(void) {
    __asm__ __volatile__("pause");
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t read_cr0(void) {
    uint32_t value;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline uint32_t read_cr2(void) {
    uint32_t value;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline uint32_t read_cr3(void) {
    uint32_t value;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline uint32_t read_cr4(void) {
    uint32_t value;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(value));
    return value;
}

static inline void write_cr0(uint32_t value) {
    __asm__ __volatile__("mov %0, %%cr0" :: "r"(value));
}

static inline void write_cr3(uint32_t value) {
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(value));
}

#endif
