/*
	libptouch - functions to help accessing a brother ptouch

	Copyright (C) 2013-2019 Dominic Radermacher <blip@mockmoon-cybernetics.ch>

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

#define _POSIX_C_SOURCE	199309L	/* needed for nanosleep() when using -std=c11 */

#include <stdio.h>
#include <stdlib.h>	/* malloc() */
#include <string.h>	/* memcmp()  */
#include <sys/types.h>	/* open() */
#include <sys/stat.h>	/* open() */
#include <fcntl.h>	/* open() */
#include <time.h>	/* nanosleep(), struct timespec */
#ifdef __APPLE__
#include "TargetConditionals.h"
#endif
#if TARGET_OS_MAC // Oster: i.e., is Mac.
#include "include/config.h"
#else
#include "config.h"
#endif
#include "gettext.h"	/* gettext(), ngettext() */
#include "ptouch.h"
#if TARGET_OS_MAC
#include "include/btMac.h"
#endif

#define _(s) gettext(s)

/* Print area width in 180 DPI and 360 DPI pixels. The 360 DPI values are
   Brother's documented values for the PT-P900 series (taken from upstream
   ptouch-print); they are not simply double the 180 DPI ones, which are
   clamped by the 128 pin printheads of the 180 DPI printers. */
struct _pt_tape_info tape_info[]= {
	{ 4,  24,  48},	/* 3.5 mm tape */
	{ 6,  32,  64},	/* 6 mm tape */
	{ 9,  52, 106},	/* 9 mm tape */
	{12,  76, 150},	/* 12 mm tape */
	{18, 120, 234},	/* 18 mm tape */
	{24, 128, 320},	/* 24 mm tape */
	{36, 192, 454},	/* 36 mm tape */
	{ 0,   0,   0}	/* terminating entry */
};

struct _pt_dev_info ptdevs[] = {
	{0x04f9, 0x2007, "PT-2420PC", 128, 180, FLAG_RASTER_PACKBITS, 0},	/* 180dpi, 128px, maximum tape width 24mm, must send TIFF compressed pixel data */
	{0x04f9, 0x202c, "PT-1230PC", 128, 180, FLAG_NONE, 0},		/* 180dpi, supports tapes up to 12mm - I don't know how much pixels it can print! */
	/* Notes about the PT-1230PC: While it is true that this printer supports
	   max 12mm tapes, it apparently expects > 76px data - the first 32px
	   must be blank. */
	{0x04f9, 0x202d, "PT-2430PC", 128, 180, FLAG_NONE, 0},		/* 180dpi, maximum 128px */
	{0x04f9, 0x2030, "PT-1230PC (PLite Mode)", 128, 180, FLAG_PLITE, 0},
	{0x04f9, 0x2031, "PT-2430PC (PLite Mode)", 128, 180, FLAG_PLITE, 0},
	{0x04f9, 0x2041, "PT-2730", 128, 180, FLAG_NONE, 0},		/* 180dpi, maximum 128px, max tape width 24mm - reported to work with some quirks */
	/* Notes about the PT-2730: was reported to need 48px whitespace
	   within png-images before content is actually printed - can not check this */
	{0x04f9, 0x205f, "PT-E500", 128, 180, FLAG_RASTER_PACKBITS, 0},
	/* Note about the PT-E500: was reported by Jesse Becker with the
	   remark that it also needs some padding (white pixels) */
	{0x04f9, 0x2061, "PT-P700", 128, 180, FLAG_RASTER_PACKBITS|FLAG_P700_INIT, 0},
	{0x04f9, 0x2064, "PT-P700 (PLite Mode)", 128, 180, FLAG_PLITE, 0},
	{0x04f9, 0x2073, "PT-D450", 128, 180, FLAG_RASTER_PACKBITS, 0},
	/* Notes about the PT-D450: I'm unsure if print width really is 128px */
	{0x04f9, 0x2085, "PT-P900Wc", 560, 360, FLAG_RASTER_PACKBITS|FLAG_P700_INIT|FLAG_USE_INFO_CMD, -8},
	/* PT-P900Wc: verified upstream on 36mm tape. 560 pin printhead
	   (70 bytes per raster line), print area centered at pin 272. */
	{0x04f9, 0x20c7, "PT-P910BT", 560, 360, FLAG_RASTER_PACKBITS|FLAG_P700_INIT|FLAG_USE_INFO_CMD, -8},
	/* PT-P910BT: 360dpi, 36mm max tape, 32mm max print height - same
	   specs as the PT-P900 series, and Brother documents both in the same
	   raster reference: print area centered at pin 272 for all tapes.
	   Over Bluetooth it is reached through its SPP RFCOMM channel. */
	{0,0,"",0,0,0,0}
};

/* Copy the static device info for entry k into ptdev */
static void ptouch_set_devinfo(ptouch_dev ptdev, int k)
{
	ptdev->devinfo->vid=ptdevs[k].vid;
	ptdev->devinfo->pid=ptdevs[k].pid;
	ptdev->devinfo->name=ptdevs[k].name;
	ptdev->devinfo->max_px=ptdevs[k].max_px;
	ptdev->devinfo->dpi=ptdevs[k].dpi;
	ptdev->devinfo->flags=ptdevs[k].flags;
	ptdev->devinfo->pin_offset=ptdevs[k].pin_offset;
}

void ptouch_rawstatus(uint8_t raw[32]);

int ptouch_debug=0;	/* set by --debug */

static int ptouch_alloc(ptouch_dev *ptdev)
{
	if ((*ptdev=calloc(1, sizeof(struct _ptouch_dev))) == NULL) {
		fprintf(stderr, _("out of memory\n"));
		return -1;
	}
	if (((*ptdev)->devinfo=calloc(1, sizeof(struct _pt_dev_info))) == NULL) {
		fprintf(stderr, _("out of memory\n"));
		return -1;
	}
	if (((*ptdev)->status=calloc(1, sizeof(struct _ptouch_stat))) == NULL) {
		fprintf(stderr, _("out of memory\n"));
		return -1;
	}
	return 0;
}

/* Open a paired printer over Bluetooth. want is a (part of the) device
   name or address, or NULL for the first paired "PT-*" device. The printer
   model is derived from the Bluetooth device name, e.g. "PT-P910BT2863". */
int ptouch_open_bt(ptouch_dev *ptdev, const char *want)
{
	char name[128];
	int k, found=-1;

	if (ptouch_alloc(ptdev) != 0) {
		return -1;
	}
	if (ptbt_open(want, ptouch_debug, name, sizeof(name)) != 0) {
		return -1;
	}
	for (k=0; ptdevs[k].vid > 0; k++) {
		/* prefer the longest matching name */
		if (strstr(name, ptdevs[k].name) != NULL &&
		    (found < 0 || strlen(ptdevs[k].name) > strlen(ptdevs[found].name))) {
			found=k;
		}
	}
	if (found < 0 || (ptdevs[found].flags & (FLAG_PLITE|FLAG_UNSUP_RASTER))) {
		fprintf(stderr, _("printer model %s is not supported\n"), name);
		ptbt_close();
		return -1;
	}
	(*ptdev)->bt=1;
	ptouch_set_devinfo(*ptdev, found);
	fprintf(stderr, _("%s connected over Bluetooth\n"), ptdevs[found].name);
	return 0;
}

int ptouch_open(ptouch_dev *ptdev)
{
	libusb_device **devs;
	libusb_device *dev;
	libusb_device_handle *handle = NULL;
	struct libusb_device_descriptor desc;
	int r,i=0;

	if (ptouch_alloc(ptdev) != 0) {
		return -1;
	}
	if ((libusb_init(NULL)) < 0) {
		fprintf(stderr, _("libusb_init() failed\n"));
		return -1;
	}
//	libusb_set_debug(NULL, 3);
	if ((libusb_get_device_list(NULL, &devs)) < 0) {
		return -1;
	}
	while ((dev=devs[i++]) != NULL) {
		if ((libusb_get_device_descriptor(dev, &desc)) < 0) {
			fprintf(stderr, _("failed to get device descriptor"));
			libusb_free_device_list(devs, 1);
			return -1;
		}
		for (int k=0; ptdevs[k].vid > 0; k++) {
			if ((desc.idVendor == ptdevs[k].vid) && (desc.idProduct == ptdevs[k].pid) && (ptdevs[k].flags >= 0)) {
				fprintf(stderr, _("%s found on USB bus %d, device %d\n"),
					ptdevs[k].name,
					libusb_get_bus_number(dev),
					libusb_get_device_address(dev));
				if (ptdevs[k].flags & FLAG_PLITE) {
					printf("Printer is in P-Lite Mode, which is unsupported\n\n");
					printf("Turn off P-Lite mode by changing switch from position EL to position E\n");
					printf("or by pressing the PLite button for ~ 2 seconds (or consult the manual)\n");
					return -1;
				}
				if (ptdevs[k].flags & FLAG_UNSUP_RASTER) {
					printf("Unfortunately, that printer currently is unsupported (it has a different raster data transfer)\n");
					return -1;
				}
				if ((r=libusb_open(dev, &handle)) != 0) {
					fprintf(stderr, _("libusb_open error :%s\n"), libusb_error_name(r));
					return -1;
				}
				libusb_free_device_list(devs, 1);
				if ((libusb_kernel_driver_active(handle, 0)) == 1) {
					if ((r=libusb_detach_kernel_driver(handle, 0)) != 0) {
						fprintf(stderr, _("error while detaching kernel driver: %s\n"), libusb_error_name(r));
					}
				}
				if ((r=libusb_claim_interface(handle, 0)) != 0) {
					fprintf(stderr, _("interface claim error: %s\n"), libusb_error_name(r));
					return -1;
				}
				(*ptdev)->h=handle;
				ptouch_set_devinfo(*ptdev, k);
				return 0;
			}
		}
	}
	fprintf(stderr, _("No P-Touch printer found on USB (remember to put switch to position E)\n"));
	libusb_free_device_list(devs, 1);
	return -1;
}

int ptouch_close(ptouch_dev ptdev)
{
	if (ptdev->bt) {
		ptbt_close();
		return 0;
	}
	libusb_release_interface(ptdev->h, 0);
	libusb_close(ptdev->h);
	return 0;
}

int ptouch_send(ptouch_dev ptdev, uint8_t *data, size_t len)
{
	int r, tx;

	if ((ptdev == NULL) || (len > 128)) {
		return -1;
	}
	if (ptdev->bt) {
		return ptbt_write(data, len);
	}
	if ((r=libusb_bulk_transfer(ptdev->h, 0x02, data, (int)len, &tx, 0)) != 0) {
		fprintf(stderr, _("write error: %s\n"), libusb_error_name(r));
		return -1;
	}
	if (tx != (int)len) {
		fprintf(stderr, _("write error: could send only %i of %ld bytes\n"), tx, len);
		return -1;
	}
	return 0;
}

/* Read up to len bytes, waiting at most timeout_ms. Over Bluetooth the
   reply can arrive in pieces, so wait until len bytes arrived. */
int ptouch_read(ptouch_dev ptdev, uint8_t *buf, int len, int timeout_ms, int *tx)
{
	*tx=0;
	if (ptdev->bt) {
		return ptbt_read(buf, len, timeout_ms, tx);
	}
	return libusb_bulk_transfer(ptdev->h, 0x81, buf, len, tx, (unsigned int)timeout_ms);
}

int ptouch_init(ptouch_dev ptdev)
{
	char cmd[]="\x1b\x40";		/* 1B 40 = ESC @ = INIT */
	if ((ptdev->bt) || (ptdev->devinfo->flags & FLAG_USE_INFO_CMD)) {
		/* newer printers want 200 bytes of 0x00 (invalidate) before ESC @,
		   which also clears any half-received job. Sent in two parts
		   because ptouch_send() takes at most 128 bytes. */
		uint8_t inval[100];
		memset(inval, 0, sizeof(inval));
		if ((ptouch_send(ptdev, inval, sizeof(inval)) != 0) ||
		    (ptouch_send(ptdev, inval, sizeof(inval)) != 0)) {
			return -1;
		}
	}
	return ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
}

/* ESC i z: print information command, required by the PT-P900 series.
   Tells the printer the tape width and the number of raster lines. */
int ptouch_info_cmd(ptouch_dev ptdev, int raster_lines)
{
	uint8_t cmd[]={0x1b, 0x69, 0x7a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	cmd[3]=0x04;	/* n1: media width is valid */
	cmd[5]=ptdev->status->media_width;
	cmd[7]=(uint8_t)(raster_lines & 0xff);
	cmd[8]=(uint8_t)((raster_lines >> 8) & 0xff);
	cmd[9]=(uint8_t)((raster_lines >> 16) & 0xff);
	cmd[10]=(uint8_t)((raster_lines >> 24) & 0xff);
	return ptouch_send(ptdev, cmd, sizeof(cmd));
}

/* After the print command, wait until the printer has taken the whole job
   before disconnecting. The PT-P910BT sends no "printing completed" status
   on its own, so ask for a status: the printer answers once it has
   processed everything sent before the request. */
int ptouch_wait_print_done(ptouch_dev ptdev, int timeout_sec)
{
	char cmd[]="\x1biS";	/* ESC i S = status information request */
	uint8_t buf[32];
	int tx;
	time_t end=time(NULL)+timeout_sec;

	if (ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd)) != 0) {
		return -1;
	}
	while (time(NULL) < end) {
		ptouch_read(ptdev, buf, 32, 1000, &tx);
		if (tx > 0 && ptouch_debug) {
			fprintf(stderr, "status reply (%d bytes):\n", tx);
			ptouch_rawstatus(buf);
		}
		if (tx != 32 || buf[0] != 0x80 || buf[1] != 0x20) {
			continue;
		}
		if (buf[18] == 0x02) {		/* status type: error occurred */
			fprintf(stderr, _("printer reported error 0x%02x%02x\n"), buf[9], buf[8]);
			return -1;
		}
		return 0;
	}
	fprintf(stderr, _("timeout while waiting for the printer to finish\n"));
	return -1;
}

int ptouch_enable_packbits(ptouch_dev ptdev)
{				/* 4D 00 = disable compression */
	char cmd[] = "M\x02";	/* 4D 02 = enable packbits compression mode */
	return ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
}

int ptouch_rasterstart(ptouch_dev ptdev)
{
	/* 1B 69 52 01 = Select graphics transfer mode = Raster */
	char cmd[] = "\x1b\x69\x52\x01";
	/* 1B 69 61 01 = switch mode (0=esc/p, 1=raster mode) */
	char cmd2[] = "\x1b\x69\x61\x01";
	if (ptdev->devinfo->flags & FLAG_P700_INIT) {
		int r=ptouch_send(ptdev, (uint8_t *)cmd2, strlen(cmd2));
		if ((r == 0) && (ptdev->devinfo->flags & FLAG_USE_INFO_CMD)) {
			/* the PT-P900 series answers with a status reply; flush it
			   so later USB writes don't block */
			uint8_t buf[32];
			int tx;
			ptouch_read(ptdev, buf, sizeof(buf), 1000, &tx);
		}
		return r;
	} /* else */
	return ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
}

/* print an empty line */
int ptouch_lf(ptouch_dev ptdev)
{
	char cmd[]="\x5a";
	return ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
}

/* print and advance tape, but do not cut */
int ptouch_ff(ptouch_dev ptdev)
{
	char cmd[]="\x0c";
	return ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
}

/* print and cut tape */
int ptouch_eject(ptouch_dev ptdev)
{
	char cmd[]="\x1a";
	return ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
}

void ptouch_rawstatus(uint8_t raw[32])
{
	fprintf(stderr, _("debug: dumping raw status bytes\n"));
	for (int i=0; i<32; i++) {
		fprintf(stderr, "%02x ", raw[i]);
		if (((i+1) % 16) == 0) {
			fprintf(stderr, "\n");
		}
	}
	fprintf(stderr, "\n");
	return;
}

int ptouch_getstatus(ptouch_dev ptdev)
{
	char cmd[]="\x1biS";	/* 1B 69 53 = ESC i S = Status info request */
	uint8_t buf[32];
	int i, r, tx=0, tries=0;
	struct timespec w;

	if (ptdev->bt) {
		/* drop stale replies, then give the Bluetooth link time to answer */
		ptbt_discard_input();
		ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
		ptouch_read(ptdev, buf, 32, 10000, &tx);
		if (tx == 0) {
			fprintf(stderr, _("no status reply from the printer\n"));
			return -1;
		}
	} else {
		ptouch_send(ptdev, (uint8_t *)cmd, strlen(cmd));
	}
	while (tx == 0) {
		w.tv_sec=0;
		w.tv_nsec=100000000;	/* 0.1 sec */
		nanosleep(&w, NULL);
		if ((r=libusb_bulk_transfer(ptdev->h, 0x81, buf, 32, &tx, 0)) != 0) {
			fprintf(stderr, _("read error: %s\n"), libusb_error_name(r));
			return -1;
		}
		tries++;
		if (tries > 10) {
			fprintf(stderr, _("timeout while waiting for status response\n"));
			return -1;
		}
	}
	if (tx == 32) {
		if (buf[0]==0x80 && buf[1]==0x20) {
			memcpy(ptdev->status, buf, 32);
			ptdev->tape_width_px=0;
			for (i=0; tape_info[i].mm > 0; i++) {
				if (tape_info[i].mm == buf[10]) {
					ptdev->tape_width_px=(ptdev->devinfo->dpi == 360) ? tape_info[i].px360 : tape_info[i].px;
				}
			}
			/* never print wider than the printhead */
			if (ptdev->tape_width_px > ptdev->devinfo->max_px) {
				ptdev->tape_width_px=(uint16_t)ptdev->devinfo->max_px;
			}
			if (ptdev->tape_width_px == 0) {
				fprintf(stderr, _("unknown tape width of %imm, please report this.\n"), buf[10]);
			}
			return 0;
		}
	}
	if (tx == 16) {
		fprintf(stderr, _("got only 16 bytes... wondering what they are:\n"));
		ptouch_rawstatus(buf);
	}
	if (tx != 32) {
		fprintf(stderr, _("read error: got %i instead of 32 bytes\n"), tx);
		return -1;
	}
	fprintf(stderr, _("strange status:\n"));
	ptouch_rawstatus(buf);
	fprintf(stderr, _("trying to flush junk\n"));
	if ((r=ptouch_read(ptdev, buf, 32, 1000, &tx)) != 0) {
		fprintf(stderr, _("read error: %s\n"), libusb_error_name(r));
		return -1;
	}
	fprintf(stderr, _("got another %i bytes. now try again\n"), tx);
	return -1;
}

int ptouch_getmaxwidth(ptouch_dev ptdev)
{
	return ptdev->tape_width_px;
}

int ptouch_printinfo(ptouch_dev ptdev, uint8_t tape_width_mm)
{
	uint8_t cmd[] = {0x1b, 0x69, 0x63, 0x84, 0x00, tape_width_mm, 0x00, 0x00};
	return ptouch_send(ptdev, cmd, sizeof(cmd));
}

int ptouch_setmode(ptouch_dev ptdev, uint8_t mode_flags)
{
	uint8_t cmd[] = {0x1b, 0x69, 0x4d, mode_flags};
	return ptouch_send(ptdev, cmd, sizeof(cmd));
}

int ptouch_setadvanced(ptouch_dev ptdev, uint8_t advanced_flags)
{
	uint8_t cmd[] = {0x1b, 0x69, 0x4b, advanced_flags};
	return ptouch_send(ptdev, cmd, sizeof(cmd));
}

int ptouch_sendraster(ptouch_dev ptdev, uint8_t *data, size_t len)
{
	uint8_t buf[128];	/* 70 data bytes for a 560 pin head + header */
	int rc;

	if (len > (size_t)(ptdev->devinfo->max_px / 8)) {
		return -1;
	}
	buf[0]=0x47;
	if (ptdev->devinfo->flags & FLAG_RASTER_PACKBITS) {
		/* Fake compression by encoding a single uncompressed run */
		buf[1] = (uint8_t)(len + 1);
		buf[2] = 0;
		buf[3] = (uint8_t)(len - 1);
		memcpy(buf + 4, data, len);
		rc = ptouch_send(ptdev, buf, len + 4);
	} else {
		buf[1] = (uint8_t)len;
		buf[2] = 0;
		memcpy(buf + 3, data, len);
		rc = ptouch_send(ptdev, buf, len + 3);
	}
	return rc;
}
