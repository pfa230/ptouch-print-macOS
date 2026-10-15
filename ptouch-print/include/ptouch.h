/*
	ptouch-print - Print labels with images or text on a Brother P-Touch
	
	Copyright (C) 2015-2019 Dominic Radermacher <blip@mockmoon-cybernetics.ch>

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License version 3 as
	published by the Free Software Foundation
	
	This program is distributed in the hope that it will be useful, but
	WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software Foundation,
	Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <stdint.h>

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif
#if TARGET_OS_MAC // Oster: i.e., is Mac.
#include "libusb.h"
#else
#include <libusb-1.0/libusb.h>
#endif

struct _pt_tape_info {
	uint8_t mm;		/* Tape width in mm */
	uint16_t px;		/* Printing area in px at 180 dpi */
	uint16_t px360;		/* Printing area in px at 360 dpi */
};

#define FLAG_NONE		(0x00)
#define FLAG_UNSUP_RASTER	(0x01)
#define FLAG_RASTER_PACKBITS	(0x02)
#define FLAG_PLITE		(0x04)
#define FLAG_P700_INIT		(0x08)
#define FLAG_USE_INFO_CMD	(0x10)	/* send ESC i z print information command */

struct _pt_dev_info {
	int vid;		/* USB vendor ID */
	int pid;		/* USB product ID */
	char *name;
	int max_px;		/* Maximum pixel width that can be printed (printhead pins) */
	int dpi;		/* Dots per inch of the printhead */
	int flags;
	/* Pins to shift the print area from the center of the printhead.
	   See Brother "Raster Command Reference PT-P900/P900W/P950NW/P910BT",
	   section 2.3.5 "Raster line". 0 = centered. */
	int pin_offset;
};
typedef struct _pt_dev_info *pt_dev_info;

struct __attribute__((packed, aligned(4))) _ptouch_stat {
	uint8_t printheadmark;	// 0x80
	uint8_t size;		// 0x20
	uint8_t brother_code;	// "B"
	uint8_t series_code;	// "0"
	uint8_t model;
	uint8_t country;	// "0"
	uint16_t reserved_1;
	uint16_t error;		// table 1 and 2
	uint8_t media_width;	// tape width in mm
	uint8_t media_type;	// table 4
	uint8_t ncol;		// 0
	uint8_t fonts;		// 0
	uint8_t jp_fonts;	// 0
	uint8_t mode;
	uint8_t density;	// 0
	uint8_t media_len;	// table length, always 0
	uint8_t status_type;	// table 5
	uint8_t phase_type;
	uint16_t phase_number;	// table 6
	uint8_t notif_number;
	uint8_t exp;		// 0
	uint8_t tape_color;	// table 8
	uint8_t text_color;	// table 9
	uint32_t hw_setting;
	uint16_t reserved_2;
};
typedef struct _ptouch_stat *pt_dev_stat;

struct _ptouch_dev {
	libusb_device_handle *h;
	pt_dev_info devinfo;
	pt_dev_stat status;
	uint16_t tape_width_px;
	int bt;			/* connected over Bluetooth instead of USB */
};
typedef struct _ptouch_dev *ptouch_dev;

extern int ptouch_debug;	/* verbose debug output (--debug) */

int ptouch_open(ptouch_dev *ptdev);
int ptouch_open_bt(ptouch_dev *ptdev, const char *want);
int ptouch_read(ptouch_dev ptdev, uint8_t *buf, int len, int timeout_ms, int *tx);
int ptouch_info_cmd(ptouch_dev ptdev, int raster_lines);
int ptouch_wait_print_done(ptouch_dev ptdev, int timeout_sec);
int ptouch_close(ptouch_dev ptdev);
int ptouch_send(ptouch_dev ptdev, uint8_t *data, size_t len);
int ptouch_init(ptouch_dev ptdev);
int ptouch_lf(ptouch_dev ptdev);
int ptouch_ff(ptouch_dev ptdev);
int ptouch_eject(ptouch_dev ptdev);
int ptouch_getstatus(ptouch_dev ptdev);
int ptouch_getmaxwidth(ptouch_dev ptdev);
int ptouch_printinfo(ptouch_dev ptdev, uint8_t tape_width_mm);
int ptouch_setmode(ptouch_dev ptdev, uint8_t mode_flags);
int ptouch_setadvanced(ptouch_dev ptdev, uint8_t advanced_flags);
int ptouch_enable_packbits(ptouch_dev ptdev);
int ptouch_rasterstart(ptouch_dev ptdev);
int ptouch_sendraster(ptouch_dev ptdev, uint8_t *data, size_t len);
