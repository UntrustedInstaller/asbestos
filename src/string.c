#include "string.h"

void *memset(void *dst, int value, usize count) {
    uint8_t *d = (uint8_t *)dst;
    while (count--) {
        *d++ = (uint8_t)value;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, usize count) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (count--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, usize count) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || count == 0) {
        return dst;
    }
    if (d < s) {
        while (count--) {
            *d++ = *s++;
        }
    } else {
        d += count;
        s += count;
        while (count--) {
            *--d = *--s;
        }
    }
    return dst;
}

usize strlen(const char *s) {
    usize n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}
