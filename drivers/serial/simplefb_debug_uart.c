#include <linux/compiler.h>
#include <config.h>
#include <debug_uart.h>
#include <video_font.h>
#include <video_font_8x16.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <cpu_func.h>
#include <asm/cache.h>
#include <asm/io.h>

#define FB_CLEAR_DELAY
#define FB_HAS_NOTCH

#define FONTW 8
#define FONTH 16
#define FB_BASE 0x7bdf0000
#define FB_WIDTH 736
#define FB_HEIGHT 1520
#define FB_SIZE (FB_HEIGHT * FB_WIDTH * 4) // 32bpp
#define NOTCH_Y_OFFSET FONTH * 4

#ifdef FB_HAS_NOTCH
static u32 cur_x = 0, cur_y = NOTCH_Y_OFFSET;
#else
static u32 cur_x = 0, cur_y = 0;
#endif

static inline void fb_clear(void)
{
#ifdef FB_HAS_NOTCH
	cur_x = 0;
	cur_y = NOTCH_Y_OFFSET;
#else
	cur_x = 0;
	cur_y = 0;
#endif

	memset((void*)FB_BASE, 0, FB_SIZE);
	ulong start = (ulong)FB_BASE & ~(ARCH_DMA_MINALIGN - 1);
	ulong end = (FB_BASE + FB_SIZE + (ARCH_DMA_MINALIGN - 1)) & ~(ARCH_DMA_MINALIGN - 1);
	flush_dcache_range(start,end);
}

static inline void _debug_uart_init(void)
{
	/* disable wdt (mediatek) */
	writel(0x22000000, (void*)0x10007000);

	fb_clear();
}

static inline void _debug_uart_putc(int ch)
{
    u32 *fb = (uint*)FB_BASE;
    int i, j = 0;

    if (ch == '\n') {
        cur_y += FONTH;
        goto check_bottom;
    }
    if (ch == '\r') {
        cur_x = 0;
        return;
    }

    /* 1. draw the char */
    u8 *glyph = (u8 *)&video_fontdata_8x16[(uchar)ch * FONTH];
    for (i = 0; i < FONTH; i++) {
        for (j = 0; j < FONTW; j++) {
            u32 idx = (cur_y + i) * FB_WIDTH + (cur_x + j);
            fb[idx] = (glyph[i] & (0x80 >> j)) ? 0xffffffff : 0xff000000;
        }
    }

    /* 2. flush d cache */
    ulong char_start = FB_BASE + (((cur_y * FB_WIDTH) + cur_x) * sizeof(u32));
    ulong char_end = FB_BASE + (((cur_y + FONTH) * FB_WIDTH) + cur_x + FONTW) * sizeof(u32);
    flush_dcache_range(char_start, char_end);

    /* 3. advance cursor */
    cur_x += FONTW;
    if (cur_x >= FB_WIDTH){
        cur_x = 0;
        cur_y += FONTH;
    }

check_bottom:
    /* if bottom of fb reached, clear and return to top */
    if (cur_y >= FB_HEIGHT) {
#ifdef FB_CLEAR_DELAY
        mdelay(2500);
#endif
	fb_clear();
    }
}

DEBUG_UART_FUNCS
