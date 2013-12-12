/*
 teletext.c

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
#ifdef ENABLE_TELETEXT

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "teletext.h"
#include "gfx.h"
#include "interface.h"
#include "debug.h"
#include "StbMainApp.h"
#include "stsdk.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <poll.h>
#include <client.h>
#include <error.h>

/***********************************************
LOCAL MACROS                                  *
************************************************/

#define TELETEXT_SYMBOL_WIDTH				(15)	//TELETEXT_SYMBOL_WIDTH = TELETEXT_SYMBOL_RIGHR_WIDTH + TELETEXT_SYMBOL_LEFT_WIDTH
#define TELETEXT_SYMBOL_HEIGHT				(20)	//TELETEXT_SYMBOL_HEIGHT = 2 * TELETEXT_SYMBOL_EDGE_HEIGHT + TELETEXT_SYMBOL_MIDDLE_HEIGHT
#define TELETEXT_SYMBOL_HEIGHT_480I			(17)	//TELETEXT_SYMBOL_HEIGHT_480I = 2 * TELETEXT_SYMBOL_EDGE_HEIGHT_480I + TELETEXT_SYMBOL_MIDDLE_HEIGHT_480I
#define TELETEXT_SYMBOL_RIGHR_WIDTH			(8)
#define TELETEXT_SYMBOL_LEFT_WIDTH			(7)
#define TELETEXT_SYMBOL_EDGE_HEIGHT			(6)
#define TELETEXT_SYMBOL_MIDDLE_HEIGHT		(8)
#define TELETEXT_SYMBOL_EDGE_HEIGHT_480I	(5)
#define TELETEXT_SYMBOL_MIDDLE_HEIGHT_480I	(7)

#define TELETEXT_SYMBOL_ROW_COUNT			(40)
#define TELETEXT_SYMBOL_LINE_COUNT			(24)

#define PG_ACTIVE	0x100
#define BAD_CHAR 0xb8 

#define TS_PACKET_PAYLOAD_SIZE 184				// size of a TS packet payload in bytes
#define PAYLOAD_BUFFER_SIZE 4096					// size of a packet payload buffer


#define DEBUG_TTX	0
#define debug_print(fmt, ...) \
	do { if (DEBUG_TTX) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/
typedef enum {
	DATA_UNIT_EBU_TELETEXT_NONSUBTITLE = 0x02,
	DATA_UNIT_EBU_TELETEXT_SUBTITLE = 0x03
} data_unit_t;

typedef enum {
	black = 0xFF,
	white = 0x00
} background_t;

typedef enum {
	NO =	 0x00,
	YES = 0x01,
	UNDEF = 0xff
} bool_t;

typedef struct {
	uint8_t sync : 8;
	uint8_t transport_error : 1;
	uint8_t payload_unit_start : 1;
	uint8_t transport_priority : 1;
	uint16_t pid : 13;
	uint8_t scrambling_control : 2;
	uint8_t adaptation_field_exists : 2;
	uint8_t continuity_counter : 4;
} ts_header_t;

struct vt_page {
    int pgno, subno;	// the wanted page number
    int lang;		// language code
    int flags;		// misc flags (see PG_xxx below)
    int errors;		// number of single bit errors in page
    u32 lines;		// 1 bit for each line received
    u8 data[25][40];	// page contents
    int flof;		// page has FastText links
    bool_t	subtitle;
    struct {
		int pgno;
		int subno;
    } link[6]; // FastText links (FLOF)
};

typedef struct {
	char		text[25][40];	//display page [25][40]			hat = text[0]
	bool_t	subtitle;
} my_page;

typedef struct {
	uint32_t				enabled;
	int32_t				selectedPage;
	uint8_t				numberPage[8];
	uint8_t				cyrillic[1000];
	uint32_t				fresh[3];
	uint32_t				freshCounter;
	uint32_t				nextPage[3];	
	uint32_t				previousPage;
	uint8_t				showTeletext;
	uint8_t				global_timestamp[16]; // global TS PCR value
	background_t			background;			//white or black
	pthread_mutex_t 		mutex;				//for functions display on screen
	pthread_t 			thread;
	bool_t				subtitle;   			// subtiles
	int32_t 			hash;
	my_page*				pages[1000];			// Fix: replace structure on the "vt_page"
} teletextInfo_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/**
*   Function convert latin symbol c to cyrillic
*
*   @param c	I	Latin symbol
*   @param str	O	Multibyte cyrrilic UTF-8 sequence
*/
static void teletext_convToCyrillic(unsigned char c, unsigned char *str);

static int32_t teletext_nextPageNumber(int32_t pageNumber);
static int32_t teletext_previousPageNumber(int32_t pageNumber);

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
teletextInfo_t teletextInfo;
int32_t ttx_pipe = -1;

const uint8_t REVERSE_8[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c	, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05	, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};

static const unsigned char unhamtab[256] = {
	0x01, 0xff, 0x81, 0x01, 0xff, 0x00, 0x01, 0xff,
	0xff, 0x02, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x07,
	0xff, 0x00, 0x01, 0xff, 0x00, 0x80, 0xff, 0x00,
	0x06, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x03, 0xff,
	0xff, 0x0c, 0x01, 0xff, 0x04, 0xff, 0xff, 0x07,
	0x06, 0xff, 0xff, 0x07, 0xff, 0x07, 0x07, 0x87,
	0x06, 0xff, 0xff, 0x05, 0xff, 0x00, 0x0d, 0xff,
	0x86, 0x06, 0x06, 0xff, 0x06, 0xff, 0xff, 0x07,
	0xff, 0x02, 0x01, 0xff, 0x04, 0xff, 0xff, 0x09,
	0x02, 0x82, 0xff, 0x02, 0xff, 0x02, 0x03, 0xff,
	0x08, 0xff, 0xff, 0x05, 0xff, 0x00, 0x03, 0xff,
	0xff, 0x02, 0x03, 0xff, 0x03, 0xff, 0x83, 0x03,
	0x04, 0xff, 0xff, 0x05, 0x84, 0x04, 0x04, 0xff,
	0xff, 0x02, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x07,
	0xff, 0x05, 0x05, 0x85, 0x04, 0xff, 0xff, 0x05,
	0x06, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x03, 0xff,
	0xff, 0x0c, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x09,
	0x0a, 0xff, 0xff, 0x0b, 0x8a, 0x0a, 0x0a, 0xff,
	0x08, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x0d, 0xff,
	0xff, 0x0b, 0x0b, 0x8b, 0x0a, 0xff, 0xff, 0x0b,
	0x0c, 0x8c, 0xff, 0x0c, 0xff, 0x0c, 0x0d, 0xff,
	0xff, 0x0c, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x07,
	0xff, 0x0c, 0x0d, 0xff, 0x0d, 0xff, 0x8d, 0x0d,
	0x06, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x0d, 0xff,
	0x08, 0xff, 0xff, 0x09, 0xff, 0x09, 0x09, 0x89,
	0xff, 0x02, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x09,
	0x88, 0x08, 0x08, 0xff, 0x08, 0xff, 0xff, 0x09,
	0x08, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x03, 0xff,
	0xff, 0x0c, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x09,
	0x0f, 0xff, 0x8f, 0x0f, 0xff, 0x0e, 0x0f, 0xff,
	0x08, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x0d, 0xff,
	0xff, 0x0e, 0x0f, 0xff, 0x0e, 0x8e, 0xff, 0x0e
};

static unsigned short hammtab[256] = {
	0x0101, 0x100f, 0x0001, 0x0101, 0x100f, 0x0100, 0x0101, 0x100f,
	0x100f, 0x0102, 0x0101, 0x100f, 0x010a, 0x100f, 0x100f, 0x0107,
	0x100f, 0x0100, 0x0101, 0x100f, 0x0100, 0x0000, 0x100f, 0x0100,
	0x0106, 0x100f, 0x100f, 0x010b, 0x100f, 0x0100, 0x0103, 0x100f,
	0x100f, 0x010c, 0x0101, 0x100f, 0x0104, 0x100f, 0x100f, 0x0107,
	0x0106, 0x100f, 0x100f, 0x0107, 0x100f, 0x0107, 0x0107, 0x0007,
	0x0106, 0x100f, 0x100f, 0x0105, 0x100f, 0x0100, 0x010d, 0x100f,
	0x0006, 0x0106, 0x0106, 0x100f, 0x0106, 0x100f, 0x100f, 0x0107,
	0x100f, 0x0102, 0x0101, 0x100f, 0x0104, 0x100f, 0x100f, 0x0109,
	0x0102, 0x0002, 0x100f, 0x0102, 0x100f, 0x0102, 0x0103, 0x100f,
	0x0108, 0x100f, 0x100f, 0x0105, 0x100f, 0x0100, 0x0103, 0x100f,
	0x100f, 0x0102, 0x0103, 0x100f, 0x0103, 0x100f, 0x0003, 0x0103,
	0x0104, 0x100f, 0x100f, 0x0105, 0x0004, 0x0104, 0x0104, 0x100f,
	0x100f, 0x0102, 0x010f, 0x100f, 0x0104, 0x100f, 0x100f, 0x0107,
	0x100f, 0x0105, 0x0105, 0x0005, 0x0104, 0x100f, 0x100f, 0x0105,
	0x0106, 0x100f, 0x100f, 0x0105, 0x100f, 0x010e, 0x0103, 0x100f,
	0x100f, 0x010c, 0x0101, 0x100f, 0x010a, 0x100f, 0x100f, 0x0109,
	0x010a, 0x100f, 0x100f, 0x010b, 0x000a, 0x010a, 0x010a, 0x100f,
	0x0108, 0x100f, 0x100f, 0x010b, 0x100f, 0x0100, 0x010d, 0x100f,
	0x100f, 0x010b, 0x010b, 0x000b, 0x010a, 0x100f, 0x100f, 0x010b,
	0x010c, 0x000c, 0x100f, 0x010c, 0x100f, 0x010c, 0x010d, 0x100f,
	0x100f, 0x010c, 0x010f, 0x100f, 0x010a, 0x100f, 0x100f, 0x0107,
	0x100f, 0x010c, 0x010d, 0x100f, 0x010d, 0x100f, 0x000d, 0x010d,
	0x0106, 0x100f, 0x100f, 0x010b, 0x100f, 0x010e, 0x010d, 0x100f,
	0x0108, 0x100f, 0x100f, 0x0109, 0x100f, 0x0109, 0x0109, 0x0009,
	0x100f, 0x0102, 0x010f, 0x100f, 0x010a, 0x100f, 0x100f, 0x0109,
	0x0008, 0x0108, 0x0108, 0x100f, 0x0108, 0x100f, 0x100f, 0x0109,
	0x0108, 0x100f, 0x100f, 0x010b, 0x100f, 0x010e, 0x0103, 0x100f,
	0x100f, 0x010c, 0x010f, 0x100f, 0x0104, 0x100f, 0x100f, 0x0109,
	0x010f, 0x100f, 0x000f, 0x010f, 0x100f, 0x010e, 0x010f, 0x100f,
	0x0108, 0x100f, 0x100f, 0x0105, 0x100f, 0x010e, 0x010d, 0x100f,
	0x100f, 0x010e, 0x010f, 0x100f, 0x010e, 0x000e, 0x100f, 0x010e,
};

static const unsigned char vtx2iso8559_1_table[96] = {
/* English */
	0x20,0x21,0x22,0xa3,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,   // 0x20-0x2f
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,   // 0x30-0x3f
	0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,   // 0x40-0x4f
	0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x23,   // 0x50-0x5f
	0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,   // 0x60-0x6f
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0xbe,0xf7,0x7f    // 0x70-0x7f
};

static const unsigned char cyrillic_table[256] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xAE, 0x90, 0x91, 0xA6, 0x94, 0x95, 0xA4, 0x93, 0xA5, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E,
	0x9F, 0xAF, 0xA0, 0xA1, 0xA2, 0xA3, 0x96, 0x92, 0xAC, 0xAA, 0x97, 0xA8, 0xAD, 0xA9, 0xA7, 0x00,
	0x00, 0xB0, 0xB1, 0x00, 0xB4, 0xB5, 0x84, 0xB3, 0xC5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE,
	0xBF, 0x8F, 0x80, 0x81, 0x82, 0x83, 0x00, 0x00, 0x8C, 0x8A, 0x00, 0x88, 0x8D, 0x00, 0x87, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static char hamm24par[3][256] = {
    { // parities of first byte
	 0, 33, 34,  3, 35,  2,  1, 32, 36,  5,  6, 39,  7, 38, 37,  4,
	37,  4,  7, 38,  6, 39, 36,  5,  1, 32, 35,  2, 34,  3,  0, 33,
	38,  7,  4, 37,  5, 36, 39,  6,  2, 35, 32,  1, 33,  0,  3, 34,
	 3, 34, 33,  0, 32,  1,  2, 35, 39,  6,  5, 36,  4, 37, 38,  7,
	39,  6,  5, 36,  4, 37, 38,  7,  3, 34, 33,  0, 32,  1,  2, 35,
	 2, 35, 32,  1, 33,  0,  3, 34, 38,  7,  4, 37,  5, 36, 39,  6,
	 1, 32, 35,  2, 34,  3,  0, 33, 37,  4,  7, 38,  6, 39, 36,  5,
	36,  5,  6, 39,  7, 38, 37,  4,  0, 33, 34,  3, 35,  2,  1, 32,
	40,  9, 10, 43, 11, 42, 41,  8, 12, 45, 46, 15, 47, 14, 13, 44,
	13, 44, 47, 14, 46, 15, 12, 45, 41,  8, 11, 42, 10, 43, 40,  9,
	14, 47, 44, 13, 45, 12, 15, 46, 42, 11,  8, 41,  9, 40, 43, 10,
	43, 10,  9, 40,  8, 41, 42, 11, 15, 46, 45, 12, 44, 13, 14, 47,
	15, 46, 45, 12, 44, 13, 14, 47, 43, 10,  9, 40,  8, 41, 42, 11,
	42, 11,  8, 41,  9, 40, 43, 10, 14, 47, 44, 13, 45, 12, 15, 46,
	41,  8, 11, 42, 10, 43, 40,  9, 13, 44, 47, 14, 46, 15, 12, 45,
	12, 45, 46, 15, 47, 14, 13, 44, 40,  9, 10, 43, 11, 42, 41,  8
    }, { // parities of second byte
	 0, 41, 42,  3, 43,  2,  1, 40, 44,  5,  6, 47,  7, 46, 45,  4,
	45,  4,  7, 46,  6, 47, 44,  5,  1, 40, 43,  2, 42,  3,  0, 41,
	46,  7,  4, 45,  5, 44, 47,  6,  2, 43, 40,  1, 41,  0,  3, 42,
	 3, 42, 41,  0, 40,  1,  2, 43, 47,  6,  5, 44,  4, 45, 46,  7,
	47,  6,  5, 44,  4, 45, 46,  7,  3, 42, 41,  0, 40,  1,  2, 43,
	 2, 43, 40,  1, 41,  0,  3, 42, 46,  7,  4, 45,  5, 44, 47,  6,
	 1, 40, 43,  2, 42,  3,  0, 41, 45,  4,  7, 46,  6, 47, 44,  5,
	44,  5,  6, 47,  7, 46, 45,  4,  0, 41, 42,  3, 43,  2,  1, 40,
	48, 25, 26, 51, 27, 50, 49, 24, 28, 53, 54, 31, 55, 30, 29, 52,
	29, 52, 55, 30, 54, 31, 28, 53, 49, 24, 27, 50, 26, 51, 48, 25,
	30, 55, 52, 29, 53, 28, 31, 54, 50, 27, 24, 49, 25, 48, 51, 26,
	51, 26, 25, 48, 24, 49, 50, 27, 31, 54, 53, 28, 52, 29, 30, 55,
	31, 54, 53, 28, 52, 29, 30, 55, 51, 26, 25, 48, 24, 49, 50, 27,
	50, 27, 24, 49, 25, 48, 51, 26, 30, 55, 52, 29, 53, 28, 31, 54,
	49, 24, 27, 50, 26, 51, 48, 25, 29, 52, 55, 30, 54, 31, 28, 53,
	28, 53, 54, 31, 55, 30, 29, 52, 48, 25, 26, 51, 27, 50, 49, 24
    }, { // parities of third byte
	63, 14, 13, 60, 12, 61, 62, 15, 11, 58, 57,  8, 56,  9, 10, 59,
	10, 59, 56,  9, 57,  8, 11, 58, 62, 15, 12, 61, 13, 60, 63, 14,
	 9, 56, 59, 10, 58, 11,  8, 57, 61, 12, 15, 62, 14, 63, 60, 13,
	60, 13, 14, 63, 15, 62, 61, 12,  8, 57, 58, 11, 59, 10,  9, 56,
	 8, 57, 58, 11, 59, 10,  9, 56, 60, 13, 14, 63, 15, 62, 61, 12,
	61, 12, 15, 62, 14, 63, 60, 13,  9, 56, 59, 10, 58, 11,  8, 57,
	62, 15, 12, 61, 13, 60, 63, 14, 10, 59, 56,  9, 57,  8, 11, 58,
	11, 58, 57,  8, 56,  9, 10, 59, 63, 14, 13, 60, 12, 61, 62, 15,
	31, 46, 45, 28, 44, 29, 30, 47, 43, 26, 25, 40, 24, 41, 42, 27,
	42, 27, 24, 41, 25, 40, 43, 26, 30, 47, 44, 29, 45, 28, 31, 46,
	41, 24, 27, 42, 26, 43, 40, 25, 29, 44, 47, 30, 46, 31, 28, 45,
	28, 45, 46, 31, 47, 30, 29, 44, 40, 25, 26, 43, 27, 42, 41, 24,
	40, 25, 26, 43, 27, 42, 41, 24, 28, 45, 46, 31, 47, 30, 29, 44,
	29, 44, 47, 30, 46, 31, 28, 45, 41, 24, 27, 42, 26, 43, 40, 25,
	30, 47, 44, 29, 45, 28, 31, 46, 42, 27, 24, 41, 25, 40, 43, 26,
	43, 26, 25, 40, 24, 41, 42, 27, 31, 46, 45, 28, 44, 29, 30, 47
    }
};

// table to extract the lower 4 bit from hamm24/18 encoded bytes
static char hamm24val[256] = {
      0,  0,  0,  0,  1,  1,  1,  1,  0,  0,  0,  0,  1,  1,  1,  1,
      2,  2,  2,  2,  3,  3,  3,  3,  2,  2,  2,  2,  3,  3,  3,  3,
      4,  4,  4,  4,  5,  5,  5,  5,  4,  4,  4,  4,  5,  5,  5,  5,
      6,  6,  6,  6,  7,  7,  7,  7,  6,  6,  6,  6,  7,  7,  7,  7,
      8,  8,  8,  8,  9,  9,  9,  9,  8,  8,  8,  8,  9,  9,  9,  9,
     10, 10, 10, 10, 11, 11, 11, 11, 10, 10, 10, 10, 11, 11, 11, 11,
     12, 12, 12, 12, 13, 13, 13, 13, 12, 12, 12, 12, 13, 13, 13, 13,
     14, 14, 14, 14, 15, 15, 15, 15, 14, 14, 14, 14, 15, 15, 15, 15,
      0,  0,  0,  0,  1,  1,  1,  1,  0,  0,  0,  0,  1,  1,  1,  1,
      2,  2,  2,  2,  3,  3,  3,  3,  2,  2,  2,  2,  3,  3,  3,  3,
      4,  4,  4,  4,  5,  5,  5,  5,  4,  4,  4,  4,  5,  5,  5,  5,
      6,  6,  6,  6,  7,  7,  7,  7,  6,  6,  6,  6,  7,  7,  7,  7,
      8,  8,  8,  8,  9,  9,  9,  9,  8,  8,  8,  8,  9,  9,  9,  9,
     10, 10, 10, 10, 11, 11, 11, 11, 10, 10, 10, 10, 11, 11, 11, 11,
     12, 12, 12, 12, 13, 13, 13, 13, 12, 12, 12, 12, 13, 13, 13, 13,
     14, 14, 14, 14, 15, 15, 15, 15, 14, 14, 14, 14, 15, 15, 15, 15
};

// mapping from parity checks made by table hamm24par to error
// results return by hamm24.
// (0 = no error, 0x0100 = single bit error, 0x1000 = double error)
static short hamm24err[64] = {
    0x0000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
    0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
    0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
    0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
    0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100,
    0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100,
    0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100,
    0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
};

// mapping from parity checks made by table hamm24par to faulty bit
// in the decoded 18 bit word.
static int hamm24cor[64] = {
    0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000,
    0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000,
    0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000,
    0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000,
    0x00000, 0x00000, 0x00000, 0x00001, 0x00000, 0x00002, 0x00004, 0x00008,
    0x00000, 0x00010, 0x00020, 0x00040, 0x00080, 0x00100, 0x00200, 0x00400,
    0x00000, 0x00800, 0x01000, 0x02000, 0x04000, 0x08000, 0x10000, 0x20000,
    0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000,
};

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/
uint32_t teletext_isEnable(void)
{
	return teletextInfo.enabled;
}

uint32_t teletext_enable(uint32_t enable)
{
	teletextInfo.enabled = enable;
    teletextInfo.background = black;
	return 0;
}

void teletext_init(void)
{
	pthread_mutex_init(&teletextInfo.mutex, NULL);
	teletextInfo.thread = 0;
	teletextInfo.hash = 0;
	teletextInfo.background = black;
	teletextInfo.freshCounter = 0;
	teletextInfo.selectedPage = 100;
	teletextInfo.showTeletext = 1;
	teletextInfo.numberPage[0] = 0x20;
	teletextInfo.numberPage[1] = 0x20;
	teletextInfo.numberPage[2] = 0x20;
	teletextInfo.numberPage[3] = 0x20;
	teletextInfo.numberPage[4] = 1 + 48;
	teletextInfo.numberPage[5] = 0 + 48;
	teletextInfo.numberPage[6] = 0 + 48;
	teletextInfo.numberPage[7] = 0x20;
}

void teletext_destroy(void)
{
	teletextInfo.showTeletext = 0;
	teletext_enable(0);
	pthread_mutex_destroy(&teletextInfo.mutex);
	//fclose(file);
}

static int32_t teletext_nextPageNumber(int32_t pageNumber)
{
	int32_t nextPage = pageNumber;

	do {
		nextPage++;
		if(nextPage == 1000) {
			nextPage = 0;
		}
		if (teletextInfo.pages[nextPage])
			break;
	} while(pageNumber != nextPage);

	return nextPage;
}

static int32_t teletext_previousPageNumber(int32_t pageNumber)
{
	int32_t prevPage = pageNumber;

	do {
		prevPage--;
		if(prevPage == 0) {
			prevPage = 999;
		}
		if (teletextInfo.pages[prevPage])
			break;
	} while(pageNumber != prevPage);
	return prevPage;
}

static void teletext_convToCyrillic(unsigned char c, char unsigned *str)
{
	uint32_t i;
	struct {
		char symbol;
		char code[2];
	} symbol_map[] = {
		{'h',	{0xD1, 0x85}},
		{'f',	{0xD1, 0x84}},
		{'c',	{0xD1, 0x86}},
		{'v',	{0xD0, 0xB6}},
		{'w',	{0xD0, 0xB2}},
		{'&',	{0xD1, 0x8B}},
		{'z',	{0xD0, 0xB7}},
		{189,	{0xD0, 0xAD}},
		{96,	{0xD1, 0x8E}},
		{188,	{0xD1, 0x88}},
		{190,	{0xD1, 0x89}},
	};

	str[2] = '\0';
	for(i = 0; i < ARRAY_SIZE(symbol_map); i++) {
		if(symbol_map[i].symbol == c) {
			str[0] = symbol_map[i].code[0];
			str[1] = symbol_map[i].code[1];
			return;
		}
	}
	//not found
	if(c < 'q') {
		str[0] = 0xD0; //Cyrillic P
	} else {
		str[0] = 0xD1; //Cyrillic C
	}
	str[1] = cyrillic_table[c];

	return;
}

static inline unsigned char unham(unsigned char low, unsigned char high)
{
	return (unhamtab[high] << 4) | (unhamtab[low] & 0x0F);
}

int hamm16(u8 *p, int *err)
{
    int a = hammtab[p[0]];
    int b = hammtab[p[1]];
    *err += a;
    *err += b;
    return (a & 15) | (b & 15) * 16;
}

int chk_parity(u8 *p, int n)
{
    int err;
    for (err = 0; n--; p++)
	if (hamm24par[0][*p] & 32)
	    *p &= 0x7f;
	else
	    *p = BAD_CHAR, err++;
    return err;
}

int hamm8(u8 *p, int *err)
{
    int a = hammtab[p[0]];
    *err += a;
    return a & 15;
}

int hamm24(u8 *p, int *err)
{
    int e = hamm24par[0][p[0]] ^ hamm24par[1][p[1]] ^ hamm24par[2][p[2]];
    int x = hamm24val[p[0]] + p[1] % 128 * 16 + p[2] % 128 * 2048;
    *err += hamm24err[e];
    return x ^ hamm24cor[e];
}

void allocate_memory(my_page **pages)
{
	(*pages) = malloc(sizeof( my_page));
	(*pages)->subtitle = UNDEF;
}

int setLine(unsigned char* data, struct vt_page *cvtp_t)
{
	u8 *p = data;
	struct vt_page *cvtp;
    int hdr, mag, mag8, pkt;
    int err = 0;
	char tmp[40] ;

    hdr = hamm16(p, &err);
	if (err & 0xf000)
		return -1;
    mag = hdr & 7;
    mag8 = mag?: 8;
    pkt = (hdr >> 3) & 0x1f;
    p += 2;

	cvtp = (cvtp_t + mag);
	switch (pkt)
    {
		case 0: {
			int b1, b2, b3, b4;
			b1 = hamm16(p, &err); // page number
			b2 = hamm16(p+2, &err); // subpage number + flags
			b3 = hamm16(p+4, &err); // subpage number + flags
			b4 = hamm16(p+6, &err); // language code + more flags

		    if (err & 0xf000)
				return -1;

			cvtp->errors = (err >> 8) + chk_parity(p + 8, 32);
			cvtp->pgno = mag8 * 256 + b1;
			cvtp->pgno = (((cvtp->pgno & 0xF00) >> 8 ) * 100) + ((cvtp->pgno & 0xF0) >> 4) * 10 + (cvtp->pgno & 0xF);
		    cvtp->subno = (b2 + b3 * 256) & 0x3f7f;
		    cvtp->lang = "\0\4\2\6\1\5\3\7"[b4 >> 5] ;//+ (latin1==LATIN1 ? 0 : 8);
		    cvtp->flags = b4 & 0x1f;
		    cvtp->flags |= b3 & 0xc0;
		    cvtp->flags |= (b2 & 0x80) >> 2;
		    cvtp->lines = 1;
		    cvtp->flof = 0;

		    if (b1 == 0xff)
				return 0;

			cvtp->flags |= PG_ACTIVE;

			u_char x;
			int    c4,c5,c6,c7,c8,c9,c10,c11;

			c4 = *(p + 3) & 0x80;		// bit 8

			x  = *(p + 5);
			c5 = x & 0x20;			// bit 6
			c6 = x & 0x80;			// bit 7
			
			x  = *(p + 6);
			c7 = x & 0x02;			// bit 2
			c8 = x & 0x08;			// bit 4
			c9 = x & 0x20;			// bit 6
			c10= x & 0x80;			// bit 8
			c11  = *(p + 7) & 0x02;		// bit 2

			//Control bits
			if (c4|c5|c6|c7|c8|c9|c10|c11) {
				if (c4) { //printf("C4 = Erase page\n");
					cvtp->lines = 0;
					memset(&cvtp->data[1], '\0', 40 * 24);

					if (!teletextInfo.pages[cvtp->pgno])
						allocate_memory(&teletextInfo.pages[cvtp->pgno]);

					pthread_mutex_lock(&teletextInfo.mutex);
					memset(&teletextInfo.pages[cvtp->pgno]->text[1], '\0', 40*24);
					pthread_mutex_unlock(&teletextInfo.mutex);
				}
				cvtp->subtitle = NO;
				if (	c6) cvtp->subtitle = YES;
				if (c8) { 							//printf("C8 = Update indicator\n");
					if (teletextInfo.selectedPage == cvtp->pgno)
						interface_displayMenu(1);
				}
		/*		if (c5) printf("C5 = Newsflash\n");
				if (c7) printf("C7 = Suppress header\n");
				if (c9) printf("C9 = Interrupted sequence\n");
				if (c10) printf("C10 = Inhibit display\n");
				if (c11) printf("C11 = Magazine serial\n");		*/
			}

			teletextInfo.cyrillic[cvtp->pgno] = (unham(data[8], data[9]) >> 5) & 0x07;
			memcpy(&tmp[8], p+8, 32);
			
			int i;
			for (i = 9; i < 40; i++) {
				char ch = tmp[i] & 0x7f;
;
				if (ch >= ' ')
					tmp[i] = vtx2iso8559_1_table[ch - 0x20];
				else
					tmp[i] = ch;
			}
			memcpy(&cvtp->data[0][7], &tmp[7], 17);
			for(i = 0; i < sizeof(teletextInfo.global_timestamp); i++) {
				if ( tmp[24 + i] < teletextInfo.global_timestamp[i])
					break;
				if ( tmp[24 + i] > teletextInfo.global_timestamp[i]){
					memcpy(teletextInfo.global_timestamp, &tmp[24], 16);
					interface_displayMenu(1);
					return 0;
				}
			}
			memcpy(teletextInfo.global_timestamp, &tmp[24], 16);
			return 0;
		}
		case 1 ... 24: {
		    if (~cvtp->flags & PG_ACTIVE)
				return 1;

		    cvtp->errors += err;
		    cvtp->lines |= 1 << pkt;

			int i;
			for (i = 0; i < 40; i++) {
				char ch = p[i] & 0x7f;

				if (ch >= ' ')
					p[i] = vtx2iso8559_1_table[ch - 0x20];
				else
					p[i] = ch;
			}
		    memcpy(cvtp->data[pkt], p, 40);

			if  (cvtp->lines == 0xFFFFFF || cvtp->subtitle == YES) {
				if (!teletextInfo.pages[cvtp->pgno])
					allocate_memory(&teletextInfo.pages[cvtp->pgno]);
				pthread_mutex_lock(&teletextInfo.mutex);
				memcpy(&teletextInfo.pages[cvtp->pgno]->text[0][0], cvtp->data, 40*24);
				pthread_mutex_unlock(&teletextInfo.mutex);

				if (cvtp->subtitle == YES) {
					for (i = 1; i < 40; i++) {
						if (cvtp->data[pkt][i - 1] != cvtp->data[pkt][i])
							break;
					}
					if (i == 40)
						return 0;
				}
				if (teletextInfo.selectedPage == cvtp->pgno) {
					teletextInfo.pages[cvtp->pgno]->subtitle = cvtp->subtitle;
					interface_displayMenu(1);
				}
			}
			return 0;
		}
		case 26:
		 /*   int d, t[13];
	    	if (~cvtp->flags & PG_ACTIVE)
				return 0;

		    d = hamm8(p, &err);
		    if (err & 0xf000)
				return 4;

		    for (i = 0; i < 13; ++i)
			t[i] = hamm24(p + 1 + 3*i, &err);
		    if (err & 0xf000)
				return 4;

		    //add_enhance(rvtp->enh, d, t);
		*/
		    return 0;
		case 27:
		 /*   int b1,b2,b3,x;
		    if (~cvtp->flags & PG_ACTIVE)
				return 0; // -1 flushes all pages. We may never resync again

		    b1 = hamm8(p, &err);
		    b2 = hamm8(p + 37, &err);
		    if (err & 0xf000)
				return 4;

		    if (b1 != 0 || !(b2 & 8))
				return 0;

		    for (i = 0; i < 6; ++i)
		    {
				err = 0;
				b1 = hamm16(p+1+6*i, &err);
				b2 = hamm16(p+3+6*i, &err);
				b3 = hamm16(p+5+6*i, &err);
				if (err & 0xf000)
				    return 1;
				x = (b2 >> 7) | ((b3 >> 5) & 0x06);
				cvtp->link[i].pgno = ((mag ^ x) ?: 8) * 256 + b1;
				cvtp->link[i].subno = (b2 + b3 * 256) & 0x3f7f;
		    }
		    cvtp->flof = 1;
		*/
			return 0;
		case 30:
		 /*   if (mag8 != 8)
				return 0;
		    p[0] = hamm8(p, &err); // designation code
		    p[1] = hamm16(p+1, &err); // initial page
		    p[3] = hamm16(p+3, &err); // initial subpage + mag
		    p[5] = hamm16(p+5, &err); // initial subpage + mag

			if (err & 0xf000)
				return 4;

			err += chk_parity(p+20, 20);
		*/
			return 0;
		default:
		return 0;
    }
}

void ttx_PES_decode ( uint8_t *buffer, uint16_t size, struct vt_page *cvtp_t)
{
	if (size < 6) return;

	// Packetized Elementary Stream (PES) 32-bit start code
	uint64_t pes_prefix = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
	uint8_t pes_stream_id = buffer[3];

	// check for PES header
	if (pes_prefix != 0x000001) return;

	// stream_id is not "Private Stream 1" (0xbd)
	if (pes_stream_id != 0xbd) return;

	// PES packet length
	// ETSI EN 301 775 V1.2.1 (2003-05) chapter 4.3: (N × 184) - 6 + 6 B header
	uint16_t pes_packet_length = 6 + ((buffer[4] << 8) | buffer[5]);
	// Can be zero. If the "PES packet length" is set to zero, the PES packet can be of any length.
	// A value of zero for the PES packet length can be used only when the PES packet payload is a video elementary stream.
	if (pes_packet_length == 6) return;

	// truncate incomplete PES packets
	if (pes_packet_length > size) pes_packet_length = size;

	uint16_t optional_pes_header_length = 0;
	uint16_t i_size = 7;

	// optional PES header marker bits (10.. ....)
	if ((buffer[6] & 0xc0) == 0x80) {
		optional_pes_header_length = buffer[8];
		i_size += 3 + optional_pes_header_length;
	}
	// skip optional PES header and process each 46-byte teletext packet
	while (i_size <= pes_packet_length - 6) {
		uint8_t data_unit_id = buffer[i_size++];
		uint8_t data_unit_len = buffer[i_size++];

		if ((data_unit_id == DATA_UNIT_EBU_TELETEXT_NONSUBTITLE) || (data_unit_id == DATA_UNIT_EBU_TELETEXT_SUBTITLE)) {
		// teletext payload has always size 44 bytes
			if (data_unit_len == 44) {
				for (uint8_t j = 0; j < data_unit_len; j++) buffer[i_size + j] = REVERSE_8[buffer[i_size + j]];
				setLine(&buffer[i_size + 2] ,cvtp_t);
			}
		}
		i_size += data_unit_len;
	}
}

void getNumber(char *buffer)
{
	memcpy(buffer, &teletextInfo.numberPage, sizeof(teletextInfo.numberPage));
}

void getClock(char *buffer)
{
	memcpy(buffer + 24, &teletextInfo.global_timestamp, sizeof(teletextInfo.global_timestamp));
}

void getLinks(char *buffer)
{
 	teletextInfo.nextPage[0] = teletext_nextPageNumber(teletextInfo.selectedPage);
 	teletextInfo.nextPage[1] = teletext_nextPageNumber(teletextInfo.nextPage[0]);
 	teletextInfo.nextPage[2] = teletext_nextPageNumber(teletextInfo.nextPage[1]);
 	teletextInfo.previousPage = teletext_previousPageNumber(teletextInfo.selectedPage);

	memset(buffer, ' ', 40);
													//red		green		yellow		cyan
	snprintf(buffer, 38, "   \x01%03d      \x02%03d      \x03%03d      \x06%03d",
	teletextInfo.nextPage[0], teletextInfo.nextPage[1], teletextInfo.nextPage[2],	teletextInfo.previousPage);
}

int teletext_displayPage(void)
{
	pthread_mutex_lock(&teletextInfo.mutex);

	int beginLine, endLine;
	DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX );

	char (*curPageTextBuf)[40];
	int line, column;
	int red, green, blue, alpha, Lang;
	int symbolWidth, symbolHeight, horIndent, verIndent, lWidth, rWidth, eHeight, mHeight, emHeight, upText;
	unsigned char str[2], fu[3];
	int flagDH, flagDW, flagDS;
	int box;
	int rowCount = TELETEXT_SYMBOL_ROW_COUNT;
	int lineCount = TELETEXT_SYMBOL_LINE_COUNT;

	str[1]='\0';
	fu[2]='\0';

	symbolWidth		= TELETEXT_SYMBOL_WIDTH;
    lWidth			= TELETEXT_SYMBOL_RIGHR_WIDTH;
	rWidth			= TELETEXT_SYMBOL_LEFT_WIDTH;
	if(interfaceInfo.screenHeight == 480) {
		symbolHeight	= TELETEXT_SYMBOL_HEIGHT_480I;
		eHeight			= TELETEXT_SYMBOL_EDGE_HEIGHT_480I;
		mHeight			= TELETEXT_SYMBOL_MIDDLE_HEIGHT_480I;
		emHeight		= TELETEXT_SYMBOL_EDGE_HEIGHT_480I + TELETEXT_SYMBOL_MIDDLE_HEIGHT_480I;
		upText			= 1;		//Text lifting
	} else {
		symbolHeight	= TELETEXT_SYMBOL_HEIGHT + 2;
		eHeight			= TELETEXT_SYMBOL_EDGE_HEIGHT;
		mHeight			= TELETEXT_SYMBOL_MIDDLE_HEIGHT;
		emHeight		= TELETEXT_SYMBOL_EDGE_HEIGHT + TELETEXT_SYMBOL_MIDDLE_HEIGHT;
		upText			= 2;		//Text lifting
	}
	horIndent		= (interfaceInfo.screenWidth - rowCount*symbolWidth)/2;
	verIndent		= (interfaceInfo.screenHeight - lineCount*symbolHeight)/2 + symbolHeight;

	teletextInfo.subtitle = UNDEF;
	if (teletextInfo.pages[teletextInfo.selectedPage]) {
		curPageTextBuf = teletextInfo.pages[teletextInfo.selectedPage]->text;
		if ( teletextInfo.background == white )
			teletextInfo.subtitle = teletextInfo.pages[teletextInfo.selectedPage]->subtitle;

		if ( teletextInfo.subtitle == YES ){
			beginLine = 1; 								// not display hat line
			endLine = TELETEXT_SYMBOL_LINE_COUNT - 1; 	// not display links line
			symbolWidth += 2;							//for display of text
		} else {
			beginLine = 0;
			endLine = TELETEXT_SYMBOL_LINE_COUNT;
			getNumber((char *)curPageTextBuf[0]);
			getClock((char *)curPageTextBuf[0]);
			getLinks((char *)curPageTextBuf[TELETEXT_SYMBOL_LINE_COUNT]);			
		}
		int32_t hash_t = 0;
		int i,j;
		for(i = beginLine; i <= endLine; i++)
			for(j = 0; j < rowCount; j++)
				hash_t += curPageTextBuf[i][j] * ( (i+1)*j+j)+( (i+1)*j+j);
		
		if (teletextInfo.hash == hash_t) {
			pthread_mutex_unlock(&teletextInfo.mutex);
			return 1;
		}
		teletextInfo.hash = hash_t;
			//clear display
		gfx_drawRectangle(DRAWING_SURFACE, 0x0, 0x0, 0x0, teletextInfo.background, 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenWidth);
	} else {
		teletextInfo.hash = 0;
			//clear display
		gfx_drawRectangle(DRAWING_SURFACE, 0x0, 0x0, 0x0, teletextInfo.background, 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenWidth);
		//display the number in corner of the screen
		gfx_drawText(DRAWING_SURFACE, pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, 2*symbolWidth+horIndent, verIndent-upText, (char*) teletextInfo.numberPage, 0, 0);	
		pthread_mutex_unlock(&teletextInfo.mutex);
		return 0;
	}
	
	line = beginLine;
	for(; line <= endLine; line++) {
		alpha = 1;
		red = 255;
		green = 255;
		blue = 255;
		Lang = teletextInfo.cyrillic[teletextInfo.selectedPage];
		flagDH=0;
		flagDW=0;
		flagDS=0;
		box=0;
		if (curPageTextBuf[line][0] == '\0')
			continue;
		for(column = 0; column < rowCount; column++) {
			str[0] = curPageTextBuf[line][column];

			if(str[0] < 0x20) {//Special simbols
				uint32_t found = 0;
				uint32_t i;
				struct {
					uint8_t	symbol;
					uint8_t	ARGB[4]; //alpha, red, green, blue
				} symbolsColor[] = {
					{0x00, {0x01, 0x00, 0x00, 0x00}}, //alpha black
					{0x01, {0x01, 0xff, 0x00, 0x00}}, //alpha red
					{0x02, {0x01, 0x00, 0xff, 0x00}}, //alpha green
					{0x03, {0x01, 0xff, 0xff, 0x00}}, //alpha yellow
					{0x04, {0x01, 0x00, 0x00, 0xff}}, //alpha blue
					{0x05, {0x01, 0xff, 0x00, 0xff}}, //alpha magneta
					{0x06, {0x01, 0x00, 0xff, 0xff}}, //alpha cyan
					{0x07, {0x01, 0xff, 0xff, 0xff}}, //alpha white

					{0x10, {0x00, 0x00, 0x00, 0x00}}, //mosaic black
					{0x11, {0x00, 0xff, 0x00, 0x00}}, //mosaic red
					{0x12, {0x00, 0x00, 0xff, 0x00}}, //mosaic green
					{0x13, {0x00, 0xff, 0xff, 0x00}}, //mosaic yellow
					{0x14, {0x00, 0x00, 0x00, 0xff}}, //mosaic blue
					{0x15, {0x00, 0xff, 0x00, 0xff}}, //mosaic magneta
					{0x16, {0x00, 0x00, 0xff, 0xff}}, //mosaic cyan
					{0x17, {0x00, 0xff, 0xff, 0xff}}, //mosaic white
				};
				struct {
					uint8_t	symbol;
					uint8_t	flagDH;
					uint8_t	flagDW;
					uint8_t	flagDS;
				} symbolsSize[] = {
					{0x0c, 0x00, 0x00, 0x00}, //normal size
					{0x0d, 0x01, 0x00, 0x00}, //double height
					{0x0e, 0x00, 0x01, 0x00}, //double width
					{0x0f, 0x00, 0x00, 0x01}, //double size
				};

				for(i = 0; i < ARRAY_SIZE(symbolsColor); i++) {
					if(str[0] == symbolsColor[i].symbol) {
						alpha	= symbolsColor[i].ARGB[0];
						red		= symbolsColor[i].ARGB[1];
						green	= symbolsColor[i].ARGB[2];
						blue	= symbolsColor[i].ARGB[3];
						found	= 1;
						break;
					}
				}
				if(!found) {
					for(i = 0; i < ARRAY_SIZE(symbolsSize); i++) {
						if(str[0] == symbolsSize[i].symbol) {
							flagDH	= symbolsSize[i].flagDH;
							flagDW	= symbolsSize[i].flagDW;
							flagDS	= symbolsSize[i].flagDS;
							found	= 1;
							break;
						}
					}
				}
				if(!found) {
					switch(str[0]) {
					case 0x8: // Start Flash (Set After)
					case 0x9: // Steady (Set At)
						break;
					case 0xa: // End Box (Set After)
						if (box > 0)
							box --; //--;
						break;
					case 0xb: // Start Box (Set After)
						box ++; //++;
						break;
					case 0x18: // Conceal (Set At)
					case 0x19: // Contiguous Mosaic Graphics (Set At)
					case 0x1A:// Seperated Mosaic Graphics (Set At)
						break;
					case 0x1B: // Escape (Set After)
						if(Lang) {
							Lang = 0;
						} else {
							Lang = 1;
						}
						break;
					case 0x1C: // Black background (Set At)
						gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0xFF,
										column*symbolWidth+horIndent,
										line*symbolHeight+verIndent-symbolHeight,
										interfaceInfo.screenWidth-2*horIndent - column*symbolWidth,
										symbolHeight);
							//dprintf("%s: <black background>\n", __FUNCTION__);
						break;
					case 0x1D: // New background
						// The foreground colour becomes the background colour
						// any new characters until foreground would be invisible.
						gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF,
										column*symbolWidth+horIndent,
										line*symbolHeight+verIndent-symbolHeight,
										interfaceInfo.screenWidth-2*horIndent - column*symbolWidth,
										symbolHeight);
						//dprintf("%s: <new background>\n", __FUNCTION__);
						break;
					case 0x1E: // Hold Mosaics (Set At)
					case 0x1F: // Release Mosaics (Set At)
					default:
						break;
					}
				}
				str[0]=' ';
			} else {
				if(box)
					gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, symbolHeight);

				if(alpha) { //Text
					if((teletextInfo.cyrillic)&&(Lang)&&(((str[0]>=64)&&(str[0]<=127))||(str[0]=='#')||(str[0]=='&')||(str[0]==247)||((str[0]>=188)&&(str[0]<=190)))) {
						teletext_convToCyrillic(str[0],fu);
						//printf("%02x/%02x/%02x/\n",red, green, blue);
						gfx_drawText(DRAWING_SURFACE, pgfx_font, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-upText, (char*) fu, 0, 0);
					} else {
						if(str[0] != ' ' && box) {
							gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, symbolHeight);
						}
							gfx_drawText(DRAWING_SURFACE, pgfx_font, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-upText, (char*) str, 0, 0);
					}
				} else {
					//Pseudographics
					// ETSI EN 300 706 V1.2.1 (2003-04)
					// Table47: G1 Block Mosaics Set
					switch (str[0]) {
						case 0x21:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							break;
						case 0x22:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						case 0x23:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							break;
						case 0x24:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							break;
						case 0x25:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							break;
						case 0x26:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						case 0x27:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						case 0x28:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						case 0x29:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						case 0x2a:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						case 0x2b:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						case 0x2c:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, mHeight);
							break;
						case 0x2d:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						case 0x2e:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						case 0x2f:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, emHeight);
							break;
						case 0x30:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							break;
						case 0x31:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							break;
						case 0x32:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x33:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							break;
						case 0x34:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							break;
						case 0x35:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							break;
						case 0x36:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						case 0x37:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						case 0x38:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						case 0x39:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						case 0x3a:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						case 0x3b:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						case 0x3c:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						case 0x3d:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						case 0x3e:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						case 0x3f:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						case 0x60:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x61:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x62:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x63:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x64:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x65:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x66:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x67:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x68:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						case 0x69:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						case 0x6a:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						case 0x6b:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						case 0x6c:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x6d:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						case 0x6e:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						case 0x6f:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						case 0x70:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						case 0x71:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						case 0x72:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						case 0x73:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						case 0x74:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x75:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x76:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x77:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						case 0x78:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						case 0x79:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						case 0x7a:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						case 0x7b:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight); //70
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight); //28
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight); // 23
							break;
						case 0x7c:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, emHeight);
							break;
						case 0x7d:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, emHeight); // 7c
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight); // 21
							break;
						case 0x7e:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, emHeight); // 7c
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight); // 22
							break;
						case 0x7f:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, symbolHeight);
							break;
						case 0xa3:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							break;
						case 0xbc:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						case 0xbe:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						case 0xf7:
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						default:
							gfx_drawText(DRAWING_SURFACE, pgfx_font, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-upText, (char*) str, 0, 0);
							break;
					}
				}
			}
				if(flagDH || flagDW || flagDS) {
					gfx_DoubleSize(DRAWING_SURFACE, line, column, flagDH, flagDW, flagDS, symbolWidth, symbolHeight, horIndent, verIndent);
					if((!flagDH)&&(str[0]!=' '))
						column++;
				}
		}
	}
	pthread_mutex_unlock(&teletextInfo.mutex);
	return 0;
}

static void *teletext_funcThread(void *pArg)
{
	int32_t fd = (int32_t)pArg;
	uint8_t ts_buffer[TELETEXT_PACKET_BUFFER_SIZE] = { 0 };
	int32_t len = 0;
	int32_t continuity_counter = 255;
	uint8_t payload_buffer[PAYLOAD_BUFFER_SIZE] = { 0 };
	uint16_t payload_counter = 0;
	ts_header_t ts_header;

	struct vt_page cvtp_t[8];
	memset(cvtp_t, 0, sizeof(cvtp_t));

	struct pollfd pfd[1];

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;

	while(1) {
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		if(poll(pfd, 1, 100) > 0) {
			if(pfd[0].revents & POLLIN) {
				len = read(fd, ts_buffer, TELETEXT_PACKET_BUFFER_SIZE);
				if(len < 0) {
					eprintf("%s: %d: errno=%d: %s\n", __func__, __LINE__, errno, strerror(errno));
					continue;
				}

				int32_t n_size = 0;
				while( n_size < len ) {
						// parsing TS header
					ts_header.sync = ts_buffer[0 + n_size];
					ts_header.transport_error = (ts_buffer[1 + n_size] & 0x80) >> 7;
					ts_header.payload_unit_start = (ts_buffer[1 + n_size] & 0x40) >> 6;
					ts_header.transport_priority = (ts_buffer[1 + n_size] & 0x20) >> 5;
					//ts_header.pid = ((ts_buffer[1] & 0x1f) << 8) | ts_buffer[2];			done in stapi
					ts_header.scrambling_control = (ts_buffer[3 + n_size] & 0xc0) >> 6;
					ts_header.adaptation_field_exists = (ts_buffer[3 + n_size] & 0x20) >> 5;
					ts_header.continuity_counter = ts_buffer[3 + n_size] & 0x0f;
						uint8_t af_discontinuity = 0;
					if (ts_header.adaptation_field_exists > 0) {
						af_discontinuity = (ts_buffer[5 + n_size] & 0x80) >> 7;
					}
						// not TS packet
					if (ts_header.sync != 0x47){
						eprintf("ttx: not TS packet!\nheader.sync = 0x%08x\n", ts_header.sync);
						n_size += TS_PACKET_SIZE;
						continue;
					}
						// uncorrectable error
					if (ts_header.transport_error > 0) {
						eprintf("ttx: ts uncorrectable error!\nheader.transport_error = 0x%08x\n", ts_header.transport_error);
						n_size += TS_PACKET_SIZE;
						continue;
					}
					//printf("ts_header.continuity_counter = %d\n",ts_header.continuity_counter);
					if (continuity_counter == 255) continuity_counter = ts_header.continuity_counter;
					else {
						if (af_discontinuity == 0) {
							continuity_counter = (continuity_counter + 1) % 16;
							if (ts_header.continuity_counter != continuity_counter) {
								dprintf("ttx: continuity_counter error\n");
							payload_counter = 0;
							continuity_counter = 255;
							}
						}
					}
						// waiting for first payload_unit_start indicator
					if ((ts_header.payload_unit_start == 0) && (payload_counter == 0)){
						n_size += TS_PACKET_SIZE;
						continue;
					}
						// proceed with payload buffer
					if ((ts_header.payload_unit_start > 0) && (payload_counter > 0))
						ttx_PES_decode(payload_buffer, payload_counter, cvtp_t);
						// new payload frame start
					if (ts_header.payload_unit_start > 0) payload_counter = 0;
						if (payload_counter < (PAYLOAD_BUFFER_SIZE - TS_PACKET_PAYLOAD_SIZE)) {
						memcpy(&payload_buffer[payload_counter], &ts_buffer[4 + n_size], TS_PACKET_PAYLOAD_SIZE);
						payload_counter += TS_PACKET_PAYLOAD_SIZE;
					}
					n_size += TS_PACKET_SIZE;
				}
			}
		}
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
	}
	return 0;
}

void updatePagekey (int page)
{
	teletextInfo.selectedPage = page;
	teletextInfo.numberPage[4 + 0] = page / 100 + 48;
	teletextInfo.numberPage[4 + 1] = page % 100 / 10 + 48;
	teletextInfo.numberPage[4 + 2] = page % 10 + 48;

	interface_displayMenu(1);
}

int32_t teletext_processCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	if(teletextInfo.showTeletext) {
		if(cmd->command == interfaceCommandTeletext) {
            if(teletext_isEnable() && teletextInfo.background == white) {
				teletext_enable(0);
                teletextInfo.background = black;
            } else {
				if (teletext_isEnable() && teletextInfo.background == black ){
					teletextInfo.background = white;
					interface_displayMenu(1);
					return 0;
                }
				else
					teletext_enable(1);
            }
            interface_displayMenu(1);
			teletextInfo.freshCounter = 0;
			if (teletextInfo.selectedPage == 0)
				teletextInfo.selectedPage = 100;
			return 0;
		}

		if(teletext_isEnable()) {
			if((cmd->command >= interfaceCommand0) && (cmd->command <= interfaceCommand9)) {
				uint32_t i;
				teletextInfo.fresh[teletextInfo.freshCounter] = cmd->command - interfaceCommand0;
				teletextInfo.freshCounter++;

				for(i = 0; i < teletextInfo.freshCounter; i++)
					teletextInfo.numberPage[4 + i] = teletextInfo.fresh[i] + 48;

				for(i = teletextInfo.freshCounter; i < 3; i++)
					teletextInfo.numberPage[4 + i] = ' ';

				if(teletextInfo.freshCounter == 3) {
					teletextInfo.selectedPage = teletextInfo.fresh[0] * 100 +
												teletextInfo.fresh[1] * 10 +
												teletextInfo.fresh[2];
					teletextInfo.freshCounter = 0;
				}
				interface_displayMenu(1);
				return 0;
			} else {
				switch (cmd->command) {
					case interfaceCommandLeft:
					case interfaceCommandBlue:
						updatePagekey(teletextInfo.previousPage);
						break;
					case interfaceCommandRight:
					case interfaceCommandRed:
						updatePagekey(teletextInfo.nextPage[0]);
						break;
					case interfaceCommandGreen:
						updatePagekey(teletextInfo.nextPage[1]);
						break;
					case interfaceCommandYellow:
						updatePagekey(teletextInfo.nextPage[2]);
						break;
					default:
						break;

					teletextInfo.freshCounter = 0;
					return 0;
				}
			}
		}
	}
	return -1;
}

int32_t teletext_isTeletextShowing(void)
{
	if ( !teletextInfo.showTeletext )
		return 0;
	if( teletext_isEnable() )
			return 1;
	return 0;
}

#if (defined STSDK)
static int32_t st_teletext_start(void)
{
	elcdRpcType_t type;
	cJSON *result = NULL;
	cJSON *params = cJSON_CreateObject();
	cJSON_AddItemToObject(params, "url", cJSON_CreateString(TELETEXT_pipe_TS));

	st_rpcSync(elcmd_ttxStart, params, &type, &result);

	if(result &&
			(result->valuestring != NULL) &&
			(strcmp(result->valuestring, "ok") == 0))
	{
		cJSON_Delete(result);
		cJSON_Delete(params);

		return 0;
	}

	cJSON_Delete(result);
	cJSON_Delete(params);
	return -1;
}
#endif

int32_t teletext_start(DvbParam_t *param)
{
	int32_t hasTeletext = 0;

	if(teletextInfo.thread != 0) {
		return -1;
	}
	ttx_pipe = -1;

#if (defined STSDK)
	unlink(TELETEXT_pipe_TS);
	if(mkfifo(TELETEXT_pipe_TS, 0666) < 0) {
		eprintf("ttx: Unable to create a fifo buffer\n");
		return -1;
	}
	ttx_pipe = open(TELETEXT_pipe_TS, O_RDONLY | O_NONBLOCK);
	if(ttx_pipe < 0) {
		unlink(TELETEXT_pipe_TS);
		eprintf("Error in opening file %s\n", TELETEXT_pipe_TS);
		return -2;
	}
	hasTeletext = st_teletext_start();
	if(hasTeletext < 0) {
		close(ttx_pipe);
		unlink(TELETEXT_pipe_TS);
		return -3;
	}
#else
	if(param->mode != DvbMode_Multi) {
		if(dvb_getTeletextFD(param->adapter, &ttx_pipe) == 0) {
			hasTeletext = 1;
		}
	}
#endif
	if(hasTeletext == 0 && (ttx_pipe >= 0)) {
		int32_t st;
		teletext_init();
		st = pthread_create(&teletextInfo.thread, NULL, teletext_funcThread, (void *)ttx_pipe);
		if(st != 0) {
			eprintf("%s: ERROR not create thread\n", __func__);
			return -4;
		}
	}
	return 0;
}

int32_t teletext_stop(void)
{
	teletext_destroy();
	if(teletextInfo.thread) {
		pthread_cancel(teletextInfo.thread);
		pthread_join(teletextInfo.thread, NULL);
		teletextInfo.thread = 0;
	}
	int i;
	for(i = 0; i < 1000; i++)	{
		if(teletextInfo.pages[i] != 0) {
			free(teletextInfo.pages[i]);
			teletextInfo.pages[i] = NULL;
		}
	}
#if (defined STSDK)
	close(ttx_pipe);
	unlink(TELETEXT_pipe_TS);

	elcdRpcType_t type;
	cJSON *result = NULL;
	st_rpcSync(elcmd_ttxStop, NULL, &type, &result);

	if(result &&
			(result->valuestring != NULL) &&
			(strcmp(result->valuestring, "ok") == 0))
	{
		cJSON_Delete(result);
		return 1;
	}
	cJSON_Delete(result);
#endif
	return 0;
}

#endif //ENABLE_TELETEXT