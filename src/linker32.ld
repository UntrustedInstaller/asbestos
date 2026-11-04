ENTRY(_start)
OUTPUT_FORMAT("elf32-i386")

PHDRS
{
    text PT_LOAD FLAGS(5);   /* read + execute */
    data PT_LOAD FLAGS(6);   /* read + write */
}

SECTIONS
{
    /* The 32-bit kernel runs at 1 MiB with flat segments. */
    . = 0x00100000;

    .text : ALIGN(4) {
        *(.text)
        *(.text.*)
    } :text

    .rodata : ALIGN(4) {
        *(.rodata)
        *(.rodata.*)
    } :text

    .data : ALIGN(4) {
        *(.data)
        *(.data.*)
    } :data

    _bss_start = .;
    .bss : ALIGN(4) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data
    _bss_end = .;

    _kernel_end = .;

    /DISCARD/ : {
        *(.note*)
        *(.eh_frame)
        *(.comment)
    }
}
