/*
 fb_logo.c

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

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <signal.h>

// USE_BITMAP_FILE

#ifdef USE_BITMAP_FILE
#define LOGO_PATH "/opt/elecard/share/elecard_logo.bmp"
#else
#include "logo.c"
#endif

#define PROGRESS_MAX 7

typedef struct {
	unsigned char magic[2];
} bmpfile_magic;
 
typedef struct {
	uint32_t filesz;
	uint16_t creator1;
	uint16_t creator2;
	uint32_t bmp_offset;
} bmpfile_header;

typedef struct {
	uint32_t header_sz;
	int32_t width;
	int32_t height;
	uint16_t nplanes;
	uint16_t bitspp;
	uint32_t compress_type;
	uint32_t bmp_bytesz;
	int32_t hres;
	int32_t vres;
	uint32_t ncolors;
	uint32_t nimpcolors;
} BITMAPINFOHEADER;

int g_keep_alive = 1;

void signal_handler(int signal)
{
	(void)signal;
	g_keep_alive = 0;
}

void fill_square(char *dest, int x, int y,
                 int xoffset, int yoffset, int bits_per_pixel, int line_length,
                 const char *pixel,
                 int square_size, int square_x, int square_y)
{
	size_t fb_offset;
	int row, col;
	size_t pixel_size = bits_per_pixel/8;

	for (row = 0; row < square_size; row++)
	{
		fb_offset = (x + square_x + xoffset) * pixel_size + (y + row + square_y + yoffset) * line_length;
		for (col = 0; col < square_size; col++)
		{
			memcpy(dest + fb_offset, pixel, pixel_size);
			fb_offset += pixel_size;
		}
	}
}

void draw_indicator(char *dest, int x, int y,
                    int xoffset, int yoffset, int bits_per_pixel, int line_length,
                    const char *pixel, int progress)
{
	int square_x = 0;
	int square_y = 0;
	const int square_size = 12;

	switch (progress) {
		case 0: square_x = 0;  square_y = 20; break;
		case 1: square_x = 20; square_y = 20; break;
		case 2: square_x = 0;  square_y = 40; break;
		case 3: square_x = 20; square_y = 40; break;
		case 4: square_x = 40; square_y = 40; break;
		case 5: square_x = 0;  square_y = 60; break;
		case 6: square_x = 20; square_y = 60; break;
		case 7: square_x = 40; square_y = 60; break;
	}

	fill_square(dest, x, y,
	            xoffset, yoffset,
	            bits_per_pixel, line_length,
	            pixel, square_size, square_x, square_y);
}

int main()
{
	int fb_fd = 0;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	size_t screensize = 0;
	char *fb_p = 0;
	int x = 0, y = 0;
	size_t fb_offset = 0;

	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);

	// Open the file for reading and writing
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd == -1) {
		perror("Error: cannot open framebuffer device");
		exit(1);
	}
	printf("The framebuffer device was opened successfully.\n");

	// Get fixed screen information
	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
		perror("Error reading fixed information");
		close(fb_fd);
		exit(2);
	}

	// Get variable screen information
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
		perror("Error reading variable information");
		close(fb_fd);
		exit(3);
	}

	printf("Framebuffer: %ux%u @ %u bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

	// Figure out the size of the screen in bytes
	screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

	// Map the device to memory
	fb_p = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_p == (void*)-1) {
		perror("Error: failed to map framebuffer device to memory");
		close(fb_fd);
		exit(4);
	}
	printf("The framebuffer device was mapped to memory successfully.\n");

	// clear screen
	memset(fb_p, 0, screensize);

	x = vinfo.xres / 2;
	y = vinfo.yres / 2;

	// Draw bitmap logo
#ifdef USE_BITMAP_FILE
{
	int bmp_fd = open(LOGO_PATH, O_RDONLY);
	if (bmp_fd < 0)
	{
		perror("Error: failed to open bitmap file");
		goto bmp_finish;
	}

	int bmp_w = 0;
	int bmp_h = 0;
	bmpfile_magic    bmp_magic;
	bmpfile_header   bmp_header;
	BITMAPINFOHEADER bmp_info;

	read(bmp_fd, &bmp_magic,  sizeof(bmp_magic));
	read(bmp_fd, &bmp_header, sizeof(bmp_header));
	read(bmp_fd, &bmp_info,   sizeof(bmp_info));

	bmp_w = bmp_info.width  > 0 ? bmp_info.width  : -bmp_info.width;
	bmp_h = bmp_info.height > 0 ? bmp_info.height : -bmp_info.height;

	printf("%s: %dx%d @ %hu bbp\n", LOGO_PATH, bmp_w, bmp_h, bmp_info.bitspp);

	x = vinfo.xoffset + (vinfo.xres - vinfo.xoffset - bmp_w) / 2;
	y = vinfo.yoffset + (vinfo.yres - vinfo.yoffset - bmp_h) / 2;

	size_t row_size = 3*bmp_w;

	int row;
	for (row = bmp_h-1; row >= 0; row--)
	{
		fb_offset = (x + vinfo.xoffset) * (vinfo.bits_per_pixel/8) + (y + row + vinfo.yoffset) * finfo.line_length;
		read(bmp_fd, fb_p+fb_offset, row_size);
	}
	close(bmp_fd);

bmp_finish:;
}
#else
{
	int bmp_w = 0;
	int bmp_h = 0;

	bmpfile_magic    *bmp_magic;
	bmpfile_header   *bmp_header;
	BITMAPINFOHEADER *bmp_info;

	size_t bmp_offset = sizeof(bmpfile_magic)+sizeof(bmpfile_header)+sizeof(BITMAPINFOHEADER);

	if (sizeof(bmp_data) < bmp_offset)
		goto bmp_finish;

	bmp_magic  = (bmpfile_magic *) bmp_data;
	bmp_header = (bmpfile_header *) ((char*)bmp_magic  + sizeof(bmpfile_magic));
	bmp_info   = (BITMAPINFOHEADER*)((char*)bmp_header + sizeof(bmpfile_header));

	bmp_w = bmp_info->width  > 0 ? bmp_info->width  : -bmp_info->width;
	bmp_h = bmp_info->height > 0 ? bmp_info->height : -bmp_info->height;

	if (sizeof(bmp_data) < bmp_offset + (bmp_w*bmp_h*bmp_info->bitspp)/8)
	{
		printf("Wrong bitmap size!\n");
		goto bmp_finish;
	}

	printf("Bitmap logo: %dx%d @ %hu bbp\n", bmp_w, bmp_h, bmp_info->bitspp);

	x = vinfo.xoffset + (vinfo.xres - vinfo.xoffset - bmp_w) / 2;
	y = vinfo.yoffset + (vinfo.yres - vinfo.yoffset - bmp_h) / 2;

	size_t row_size = 3*bmp_w;

	int row;
	for (row = bmp_h-1; row >= 0; row--)
	{
		fb_offset = (x + vinfo.xoffset) * (vinfo.bits_per_pixel/8) + (y + row + vinfo.yoffset) * finfo.line_length;
		memcpy(fb_p+fb_offset, bmp_data+bmp_offset, row_size);
		bmp_offset += row_size;
	}
bmp_finish:;
}
#endif // USE_BITMAP_FILE

	// Draw load indicator
{
	char pixel[3] = { 255, 255, 255 };
	int progress = 0;

	while (g_keep_alive)
	{
		draw_indicator(fb_p, x, y,
		               vinfo.xoffset, vinfo.yoffset,
		               vinfo.bits_per_pixel, finfo.line_length,
		               pixel, progress);

		usleep (300*1000);

		progress++;
		if (progress > PROGRESS_MAX) {
			progress = 0;
			pixel[0] = 255-pixel[0]; 
			pixel[1] = 255-pixel[1]; 
			pixel[2] = 255-pixel[2];
		}
	}

	// Finish load indicator
	memset(pixel, 255, sizeof(pixel));
	for (progress = 0; progress<=PROGRESS_MAX; progress++)
		draw_indicator(fb_p, x, y,
		               vinfo.xoffset, vinfo.yoffset,
		               vinfo.bits_per_pixel, finfo.line_length,
		               pixel, progress);
}

	printf("Closing framebuffer\n");
	munmap(fb_p, screensize);
	close(fb_fd);
	return 0;
}
