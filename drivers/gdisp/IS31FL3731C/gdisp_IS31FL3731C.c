/*
Copyright 2016 Fred Sundvik <fsundvik@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "gfx.h"

#if GFX_USE_GDISP

#define GDISP_DRIVER_VMT			GDISPVMT_IS31FL3731C_ERGODOX
#include "drivers/gdisp/IS31FL3731C/gdisp_lld_config.h"
#include "src/gdisp/gdisp_driver.h"

#include "board_IS31FL3731C.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

#ifndef GDISP_SCREEN_HEIGHT
	#define GDISP_SCREEN_HEIGHT		32
#endif
#ifndef GDISP_SCREEN_WIDTH
	#define GDISP_SCREEN_WIDTH		128
#endif
#ifndef GDISP_INITIAL_CONTRAST
	#define GDISP_INITIAL_CONTRAST	0
#endif
#ifndef GDISP_INITIAL_BACKLIGHT
	#define GDISP_INITIAL_BACKLIGHT	100
#endif

#define GDISP_FLG_NEEDFLUSH			(GDISP_FLG_DRIVER<<0)

#define IS31_ADDR_DEFAULT 0x74

#define IS31_REG_CONFIG  0x00
// bits in reg
#define IS31_REG_CONFIG_PICTUREMODE 0x00
#define IS31_REG_CONFIG_AUTOPLAYMODE 0x08
#define IS31_REG_CONFIG_AUDIOPLAYMODE 0x18
// D2:D0 bits are starting frame for autoplay mode

#define IS31_REG_PICTDISP 0x01 // D2:D0 frame select for picture mode

#define IS31_REG_AUTOPLAYCTRL1 0x02
// D6:D4 number of loops (000=infty)
// D2:D0 number of frames to be used

#define IS31_REG_AUTOPLAYCTRL2 0x03 // D5:D0 delay time (*11ms)

#define IS31_REG_DISPLAYOPT 0x05
#define IS31_REG_DISPLAYOPT_INTENSITY_SAME 0x20 // same intensity for all frames
#define IS31_REG_DISPLAYOPT_BLINK_ENABLE 0x8
// D2:D0 bits blink period time (*0.27s)

#define IS31_REG_AUDIOSYNC 0x06
#define IS31_REG_AUDIOSYNC_ENABLE 0x1

#define IS31_REG_FRAMESTATE 0x07

#define IS31_REG_BREATHCTRL1 0x08
// D6:D4 fade out time (26ms*2^i)
// D2:D0 fade in time (26ms*2^i)

#define IS31_REG_BREATHCTRL2 0x09
#define IS31_REG_BREATHCTRL2_ENABLE 0x10
// D2:D0 extinguish time (3.5ms*2^i)

#define IS31_REG_SHUTDOWN 0x0A
#define IS31_REG_SHUTDOWN_OFF 0x0
#define IS31_REG_SHUTDOWN_ON 0x1

#define IS31_REG_AGCCTRL 0x0B
#define IS31_REG_ADCRATE 0x0C

#define IS31_COMMANDREGISTER 0xFD
#define IS31_FUNCTIONREG 0x0B    // helpfully called 'page nine'
#define IS31_FUNCTIONREG_SIZE 0xD

#define IS31_FRAME_SIZE 0xB4

#define IS31_TIMEOUT 5000

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

typedef struct{
    // Used both for the actual pixel data, and for initialization
    // The first byte is reserved for the offset of the write
    uint8_t ram[IS31_FRAME_SIZE + 1];
}PrivData;

// Some common routines and macros
#define PRIV(g)                         ((PrivData*)g->priv)
#define RAM(g)							(PRIV(g)->ram + 1)

// Some common routines and macros
#define delay(us)			gfxSleepMicroseconds(us)
#define delay_ms(ms)		gfxSleepMilliseconds(ms)

#define xyaddr(x, y)		((x) + ((y)>>3)*GDISP_SCREEN_WIDTH)
#define xybit(y)			(1<<((y)&7))

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

static GFXINLINE void write_register(GDisplay* g, uint8_t page, uint8_t reg, uint8_t data) {
    uint8_t tx[2] __attribute__((aligned(2)));
    tx[0] = reg;
    tx[1] = data;
    write_data(g, page, tx, 2);
}

static GFXINLINE void write_ram(GDisplay *g, uint8_t page, uint16_t offset, uint16_t length) {
    PRIV(g)->ram[0] = offset;
    write_data(g, page, PRIV(g)->ram, length + 1);
}

LLDSPEC bool_t gdisp_lld_init(GDisplay *g) {
	// The private area is the display surface.
	g->priv = gfxAlloc(sizeof(PrivData));
    __builtin_memset(PRIV(g), 0, sizeof(PrivData));

	// Initialise the board interface
	init_board(g);
	gfxSleepMilliseconds(10);

    // zero function page, all registers (assuming full_page is all zeroes)
    write_ram(g, IS31_FUNCTIONREG, 0, IS31_FUNCTIONREG_SIZE);
    set_hardware_shutdown(g, false);
    gfxSleepMilliseconds(10);
    // software shutdown
    write_register(g, IS31_FUNCTIONREG, IS31_REG_SHUTDOWN, IS31_REG_SHUTDOWN_OFF);
    gfxSleepMilliseconds(10);
    // zero function page, all registers
    write_ram(g, IS31_FUNCTIONREG, 0, IS31_FUNCTIONREG_SIZE);
    gfxSleepMilliseconds(10);


    // zero all LED registers on all 8 pages, and enable the mask
    __builtin_memcpy(RAM(g)+1, get_led_mask(g), 0x12);
    for(uint8_t i=0; i<8; i++) {
        write_ram(g, i, 0, 0xB4);
        gfxSleepMilliseconds(1);
    }

    // software shutdown disable (i.e. turn stuff on)
    write_register(g, IS31_FUNCTIONREG, IS31_REG_SHUTDOWN, IS31_REG_SHUTDOWN_ON);
    gfxSleepMilliseconds(10);

    // Finish Init
    post_init_board(g);

	/* Initialise the GDISP structure */
	g->g.Width = GDISP_SCREEN_WIDTH;
	g->g.Height = GDISP_SCREEN_HEIGHT;
	g->g.Orientation = GDISP_ROTATE_0;
	g->g.Powermode = powerOn;
	g->g.Backlight = GDISP_INITIAL_BACKLIGHT;
	g->g.Contrast = GDISP_INITIAL_CONTRAST;
	return TRUE;
}

#if GDISP_HARDWARE_FLUSH
	LLDSPEC void gdisp_lld_flush(GDisplay *g) {
		// Don't flush if we don't need it.
		if (!(g->flags & GDISP_FLG_NEEDFLUSH))
			return;

		g->flags &= ~GDISP_FLG_NEEDFLUSH;
	}
#endif

#if GDISP_HARDWARE_DRAWPIXEL
	LLDSPEC void gdisp_lld_draw_pixel(GDisplay *g) {
		coord_t		x, y;

		switch(g->g.Orientation) {
		default:
		case GDISP_ROTATE_0:
			x = g->p.x;
			y = g->p.y;
			break;
		case GDISP_ROTATE_90:
			x = g->p.y;
			y = GDISP_SCREEN_HEIGHT-1 - g->p.x;
			break;
		case GDISP_ROTATE_180:
			x = GDISP_SCREEN_WIDTH-1 - g->p.x;
			y = GDISP_SCREEN_HEIGHT-1 - g->p.y;
			break;
		case GDISP_ROTATE_270:
			x = GDISP_SCREEN_HEIGHT-1 - g->p.y;
			y = g->p.x;
			break;
		}
		if (gdispColor2Native(g->p.color) != Black)
			RAM(g)[xyaddr(x, y)] |= xybit(y);
		else
			RAM(g)[xyaddr(x, y)] &= ~xybit(y);
		g->flags |= GDISP_FLG_NEEDFLUSH;
	}
#endif

#if GDISP_HARDWARE_PIXELREAD
	LLDSPEC color_t gdisp_lld_get_pixel_color(GDisplay *g) {
		coord_t		x, y;

		switch(g->g.Orientation) {
		default:
		case GDISP_ROTATE_0:
			x = g->p.x;
			y = g->p.y;
			break;
		case GDISP_ROTATE_90:
			x = g->p.y;
			y = GDISP_SCREEN_HEIGHT-1 - g->p.x;
			break;
		case GDISP_ROTATE_180:
			x = GDISP_SCREEN_WIDTH-1 - g->p.x;
			y = GDISP_SCREEN_HEIGHT-1 - g->p.y;
			break;
		case GDISP_ROTATE_270:
			x = GDISP_SCREEN_HEIGHT-1 - g->p.y;
			y = g->p.x;
			break;
		}
		return (RAM(g)[xyaddr(x, y)] & xybit(y)) ? White : Black;
	}
#endif

#if GDISP_NEED_CONTROL && GDISP_HARDWARE_CONTROL
	LLDSPEC void gdisp_lld_control(GDisplay *g) {
		switch(g->p.x) {
		case GDISP_CONTROL_POWER:
			if (g->g.Powermode == (powermode_t)g->p.ptr)
				return;
			switch((powermode_t)g->p.ptr) {
			case powerOff:
			case powerSleep:
			case powerDeepSleep:
				break;
			case powerOn:
				break;
			default:
				return;
			}
			g->g.Powermode = (powermode_t)g->p.ptr;
			return;

		case GDISP_CONTROL_ORIENTATION:
			if (g->g.Orientation == (orientation_t)g->p.ptr)
				return;
			switch((orientation_t)g->p.ptr) {
			/* Rotation is handled by the drawing routines */
			case GDISP_ROTATE_0:
			case GDISP_ROTATE_180:
				g->g.Height = GDISP_SCREEN_HEIGHT;
				g->g.Width = GDISP_SCREEN_WIDTH;
				break;
			case GDISP_ROTATE_90:
			case GDISP_ROTATE_270:
				g->g.Height = GDISP_SCREEN_WIDTH;
				g->g.Width = GDISP_SCREEN_HEIGHT;
				break;
			default:
				return;
			}
			g->g.Orientation = (orientation_t)g->p.ptr;
			return;

		case GDISP_CONTROL_CONTRAST:
			return;
		}
	}
#endif // GDISP_NEED_CONTROL

#endif // GFX_USE_GDISP
