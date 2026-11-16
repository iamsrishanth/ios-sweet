/* Link the J8 EL1 probe as a flat image at the kernel load address. */
ENTRY(_head)
SECTIONS
{
    . = 0x80008000;
    .text : { *(.text) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
