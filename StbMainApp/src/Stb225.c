
/*
 Stb225.c

Copyright (C) 2012  Elecard Devices

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Elecard Devices nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ELECARD DEVICES BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef STB225

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "Stb225.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

#include <phStbRCProtocol.h>

#if 1
#include <phStbFB.h>
#else

#ifndef PHSTBFB_H	/** Multi-include protection.   <FILENAME>_H */
	#define PHSTBFB_H
	#include <linux/ioctl.h>
	#include <linux/types.h>

	#if defined(__cplusplus)
	extern "C" {
	#endif

	#define PHSTBFB_IOCTL_BASE 0xFB00
	#define PHSTBFB_IOCTL_GET_DEST_RECT _IOR(PHSTBFB_IOCTL_BASE, 0, unsigned int*)
	#define PHSTBFB_IOCTL_SET_DEST_RECT _IOW(PHSTBFB_IOCTL_BASE, 0, unsigned int*)

	#if defined(__cplusplus)
	}
	#endif
#endif /* PHSTBFB_H */
#endif

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static char protocol_dev[] = "/dev/input/protocol";
static char gpio_dev[]     = "/dev/phStbGpio";

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int32_t Stb225ChangeDestRect(char *path, uint32_t dest_x, uint32_t dest_y, uint32_t dest_width, uint32_t dest_height)
{
	struct fb_var_screeninfo vinfo;
	int32_t fd;
	int32_t i;
	bool dest_changed = false;
	uint32_t dest[4] = {dest_x, dest_y, dest_width, dest_height};
	uint32_t current_dest[4];

	// Open the framebuffer device
	fd = open(path, O_RDWR);
	if (fd < 0) {
		(void)printf("Error: cannot open framebuffer device %s\n", path);
		perror(path);
		exit(1);
	}

	// Get variable screen information
	if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo)) {
		(void)printf("Error reading variable information.\n");
		perror(path);
		exit(2);
	}

	// Get destination rectangle information
	if (ioctl(fd, PHSTBFB_IOCTL_GET_DEST_RECT, &current_dest)) {
		(void)printf("Error reading destination rectangle information.\n");
		perror(path);
		exit(3);
	}

	// Check to see if the destination rectangle has been changed
	for(i=0; ((!dest_changed) && (i<4)); i++)
	{
		if (((int32_t)dest[i]) != -1) 
		{
			dest_changed = (bool)(dest[i] != current_dest[i]);
		}
	}

	if (dest_changed) 
	{
		if (ioctl(fd, PHSTBFB_IOCTL_SET_DEST_RECT, &dest)) {
			(void)printf("Error writing destination rectangle information.\n");
			perror(path);
			exit(4);
		}

		if (ioctl(fd, PHSTBFB_IOCTL_GET_DEST_RECT, &current_dest)) {
			(void)printf("Error reading destination rectangle information.\n");
			perror(path);
			exit(5);
		}
	}

	(void)close(fd);
	return 0;
}


static phStbRCProtocolOpcode_t protocol_data[] = {
	{ 0x0000009E, 0x00000074}, // Power
	{ 0x00000092, 0x00000071}, // Mute
	{ 0x0000008A, 0x00000002}, // 1
	{ 0x00000089, 0x00000003}, // 2
	{ 0x00000088, 0x00000004}, // 3
	{ 0x00000086, 0x00000005}, // 4
	{ 0x00000085, 0x00000006}, // 5
	{ 0x00000084, 0x00000007}, // 6
	{ 0x00000082, 0x00000008}, // 7
	{ 0x00000081, 0x00000009}, // 8
	{ 0x00000080, 0x0000000A}, // 9
	{ 0x0000008F, 0x00000181}, // TV/RAD, Radio
	{ 0x000000C3, 0x0000000B}, // 0
	{ 0x0000008C, 0x00000043}, // V-FMT, F9
	{ 0x0000008E, 0x00000193}, // CH-, ChannelDown
	{ 0x0000008D, 0x00000192}, // CH+, ChannelUp
	{ 0x00000095, 0x00000072}, // VOL-, VolumeDown
	{ 0x00000094, 0x00000073}, // VOL+, VolumeUp
	{ 0x00000087, 0x0000008B}, // Menu
	{ 0x00000091, 0x00000195}, // Recall, Last
	{ 0x000000C5, 0x00000192}, // P-, ChannelUp
	{ 0x000000C6, 0x00000193}, // P+, ChannelDown
	{ 0x00000090, 0x00000184}, // Text
	{ 0x00000093, 0x00000067}, // Up
	{ 0x000000C7, 0x000000AE}, // Exit
	{ 0x000000C4, 0x00000069}, // Left
	{ 0x000000D0, 0x00000160}, // Ok
	{ 0x000000D7, 0x0000006A}, // Right
	{ 0x000000DB, 0x0000016C}, // Favorites
	{ 0x000000C1, 0x0000006C}, // Down
	{ 0x0000008B, 0x0000016D}, // EPG
	{ 0x00000097, 0x0000018E}, // red [FIND]
	{ 0x000000CE, 0x0000018F}, // green [ZOOM]
	{ 0x00000083, 0x00000190}, // yellow [M/P]
	{ 0x00000096, 0x00000191}, // blue [Timer]
	{ 0x000000DE, 0x00000166}, // Info
	{ 0x000000CB, 0x00000172}, // Subtitle
	{ 0x000000C0, 0x00000188}, // Audio
	{ 0x000000D1, 0x000001A1}, // Games
	{ 0x0000009C, 0x000000A7}, // Record
	{ 0x0000009D, 0x00000080}, // Stop
	{ 0x000000C9, 0x000000CF}, // Play
	{ 0x000000DA, 0x00000077}, // Pause
	{ 0x000000D4, 0x000000A8}, // Rewind
	{ 0x0000009B, 0x0000009F}, // Forward
	{ 0x000000CD, 0x00000197}, // Next
	{ 0x000000CA, 0x0000019C}, // Previous
	{ 0x0000009A, 0x0000003F}, // Movie, F5
	{ 0x0000009F, 0x00000040}, // Music, F6
	{ 0x00000099, 0x00000041}, // Photo, F7
	{ 0x00000098, 0x00000042}, // Preview, F8

/*	{         80, 0x00000067}, // Up
	{         83, 0x000000AE}, // Exit
	{         85, 0x00000069}, // Left
	{         87, 0x00000160}, // Ok
	{         45, 0x0000006A}, // Right
//	{         DB, 0x0000016C}, // Favorites
	{         81, 0x0000006C}, // Down
	{        120, 0x00000181}, // TV/RAD, Radio*/
	{ 0xFFFFFFFF, 0x000001FF} // END of Table
};


static int set_gpio (int ioctl_data, char data)
{
	int err = -1;
	int fd = open (gpio_dev, O_RDWR | O_LARGEFILE);

	if (fd < 0) {
		fprintf (stderr, "Could not open %s: ", gpio_dev);
		return err;
	}

	if (ioctl (fd, 0x80041000, ioctl_data) < 0) {
		fprintf (stderr, "%s: ioctl failed: ", gpio_dev);
		goto hack_gpio_exit;
	}

	if (write (fd, &data, 1) != 1) {
		fprintf (stderr, "Could not write to %s", gpio_dev);
		perror (NULL);
		goto hack_gpio_exit;
	}

	err = 0;

hack_gpio_exit:

	close (fd);
	return err;
}

//this used for initialise IR controller
// also setting green led on
int32_t Stb225initIR(void)
{
	int fd;
	int err = 0;


//setes gpios
	set_gpio(0x25, '0'); //this is RED led
	set_gpio(0x22, '1'); //this is GREEN led
	set_gpio(0x33, '1'); //unknown, maybe enable front panel

//	fd = open (dvb_frontend_dev, O_RDWR | O_LARGEFILE);
//	if (fd) close(fd);
//	else	hack_open_failed (dvb_frontend_dev);

	fd = open (protocol_dev, O_RDWR | O_LARGEFILE);
	if (fd < 0) {
		fprintf (stderr, "Could not open %s: ", protocol_dev);
		return -1;
	}

	if (ioctl (fd, PHSTBRCPROTOCOL_IOCTL_SET_PROTOCOL, protocol_data) < 0) {
		fprintf (stderr, "%s: ioctl failed: ", protocol_dev);
		err = -1;
	}

	close (fd);
	return err;
}

#endif // #ifdef STB225
