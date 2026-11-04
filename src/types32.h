#ifndef TYPES32_H
#define TYPES32_H

/*
 * Minimal fixed-width types for the freestanding 32-bit kernel. Keeping
 * these local avoids depending on a hosted libc's headers.
 */
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;

typedef uint32_t           usize;

#endif
