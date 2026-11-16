/*
 * granule-probe-el1.S - read ID_AA64MMFR0_EL1.TGran16 at EL1 on a Qualcomm
 * phone and report the answer without a console.
 *
 * Why EL1: the architecture has no EL0 access path to ID_AA64MMFR0_EL1 at all
 * (Arm ARM D24.2.87: at EL0 the read is UNDEFINED, or trapped by FEAT_IDST), so
 * a userspace probe gets SIGILL. At EL1 the register is readable, so this probe
 * runs before any OS and reads it.
 *
 * Expected on this handset: the Cortex-A53 r0p4 TRM (DDI 0500J) documents
 * TGran16 = 0x0 (no 16 KiB granule) and TCR_EL1.TG0 allows only 4 KB and 64 KB.
 * This probe is the on-unit confirmation of that answer.
 *
 * Packaged as a legacy Android boot image and started by ABL at EL1 with the
 * MMU off. Result reporting:
 *   - paint a band on the bootloader's splash framebuffer
 *       green = 16 KiB granule supported (would contradict the TRM)
 *       red   = not supported (expected)
 *   - hold the colour for about ten seconds
 *   - PSCI SYSTEM_OFF in both cases (no reboot loop on a dead-power-button unit)
 *
 * A phone that powers off has run the probe; the colour is the answer. A phone
 * that shows the normal splash, hangs or reboots did not run the probe; restore
 * the boot image.
 *
 * This file is diagnostic only. It runs before any OS and is not part of the
 * iOS boot path.
 */

#define FB_BASE      0x90001000   /* splash_region from the J8 device tree */
#define FB_WIDTH     720
#define FB_BAND      200          /* scanlines to paint */
#define FB_PIXEL     4            /* BGRA8888 */

#define GREEN        0x0000ff00
#define RED          0x00ff0000

    .section .text
    .globl _head
_head:
    b       _start                  /* code0: branch over the Image header */
    .long   0                       /* code1 */
    .quad   0x80000                 /* text_offset */
    .quad   _end - _head            /* image_size */
    .quad   0x8                     /* flags: LE, place anywhere */
    .quad   0, 0, 0                 /* res2, res3, res4 */
    .ascii  "ARM\x64"               /* magic at 0x38 */
    .long   0                       /* res5 */

_start:
    msr     daifset, #0xf           /* no interrupts while we poke hardware */
    isb

    mrs     x9, id_aa64mmfr0_el1
    ubfx    x10, x9, #20, #4        /* TGran16 [23:20] */

    movz    x11, #0x9000, lsl #16   /* x11 = FB_BASE = 0x90001000 */
    movk    x11, #0x1000
    movz    w12, #0xff, lsl #16     /* red: no 16K granule (0x00ff0000) */
    cbnz    x10, 1f
    b       2f
1:  mov     w12, #0xff00            /* green: 16K granule present (0x0000ff00) */
2:
    mov     x13, #FB_BAND
3:  mov     x14, #FB_WIDTH
4:  str     w12, [x11], #FB_PIXEL
    subs    x14, x14, #1
    b.ne    4b
    subs    x13, x13, #1
    b.ne    3b

    /*
     * Let the panel scan out the colour for roughly ten seconds. The phone
     * powers off in BOTH cases: a SYSTEM_RESET here would re-enter this image
     * (it is still the boot partition) and loop forever, which is painful on a
     * unit with a dead power button. Power-off is recoverable with
     * Volume Up + USB into TWRP.
     */
    mov     x13, #192
5:  mov     x15, #0x4000000
6:  subs    x15, x15, #1
    b.ne    6b
    subs    x13, x13, #1
    b.ne    5b

    mov     w0, #0x0008             /* PSCI SYSTEM_OFF */
    movk    w0, #0x8400, lsl #16
    /* smc #0, written as an encoding because some assemblers treat SMC as an
     * EL3-only instruction. The J8 device tree declares the PSCI conduit "smc". */
    .inst   0xd4000003

7:  wfe
    b       7b

    .balign 4096
_end:
