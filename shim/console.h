#pragma once
#include <stdint.h>

#ifdef J8_4K
/* J8: the live Android fb0 reports 32 bpp, virtual_size 720x2960 and
 * stride 2944 bytes (fb0 name mdssfb_80000). The stock FDT's panel bpp is
 * the DSI wire format, not the linear framebuffer pixel format. */
#define FB_BASE		0x90001000UL
#define FB_W		720
#define FB_H		1480
#define FB_BPP		32
#ifndef J8_FB_STRIDE
#define J8_FB_STRIDE	2944
#endif
#define FB_STRIDE	J8_FB_STRIDE
#else
#define FB_BASE		0x9c000000UL	/* reserved-memory cont_splash_region */
#define FB_W		1080
#define FB_H		2400
#define FB_BPP		32
#endif
#ifndef FB_STRIDE
#define FB_STRIDE	(FB_W * (FB_BPP / 8))
#endif

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

extern volatile uint8_t *con_fb;

void con_clear(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
