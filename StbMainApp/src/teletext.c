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
#include "stsdk.h"

#include <stdio.h>
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
#define TELETEXT_SYMBOL_LINE_COUNT			(25)

#define TELETEXT_PES_PACKET_BUFFER_SIZE		(65536)	//I do not why


/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/
typedef enum {
	teletextStatus_disabled = 0,
	teletextStatus_begin,
	teletextStatus_processing,
	teletextStatus_demand,
	teletextStatus_finished,
	teletextStatus_ready
} teletextStatus_t;

typedef struct {
	uint32_t			enabled;
	int32_t				selectedPage;

	uint32_t			exists;
	teletextStatus_t	status;
	int32_t				pageNumber;
	uint8_t				text[1000][25][40];
	uint8_t				subtitle[25][40];
	uint8_t				cyrillic[1000];
	uint32_t			fresh[3];
	uint32_t			freshCounter;
	uint32_t			nextPage[3];
	uint32_t			previousPage;
	uint8_t				time[14];
	int32_t				subtitlePage;
	uint32_t			subtitleFlag;
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
pthread_t teletext_thread = 0;
teletextInfo_t teletextInfo;

int32_t tt_fd = -1;

static unsigned int  PesPacketLength = 0;
static unsigned char PesPacketBuffer[TELETEXT_PES_PACKET_BUFFER_SIZE];

static const unsigned char unhamtab[256] =
{
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

static const unsigned char invtab[256] =
{
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
	0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
	0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
	0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
	0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
	0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
	0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
	0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
	0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};

static const unsigned char vtx2iso8559_1_table[96] =
{
/* English */
	0x20,0x21,0x22,0xa3,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,   // 0x20-0x2f
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,   // 0x30-0x3f
	0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,   // 0x40-0x4f
	0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0xbd,0x5d,0x5e,0x23,   // 0x50-0x5f
	0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,   // 0x60-0x6f
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0xbc,0x7c,0xbe,0xf7,0x7f    // 0x70-0x7f
};

static const unsigned char cyrillic_table[256] =
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xAE, 0x90, 0x91, 0xA6, 0x94, 0x95, 0xA4, 0x93, 0xA5, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E,
	0x9F, 0xAF, 0xA0, 0xA1, 0xA2, 0xA3, 0x96, 0x92, 0xAC, 0xAA, 0x97, 0xA8, 0x00, 0xA9, 0xA7, 0x00,
	0x00, 0xB0, 0xB1, 0x00, 0xB4, 0xB5, 0x84, 0xB3, 0xC5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE,
	0xBF, 0x8F, 0x80, 0x81, 0x82, 0x83, 0x00, 0x00, 0x8C, 0x8A, 0x00, 0x00, 0x8D, 0x00, 0x87, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8D, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
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
	return 0;
}

void teletext_init(void)
{
	int page;

	teletextInfo.status = teletextStatus_disabled;
	teletextInfo.subtitlePage = -1;
	teletextInfo.subtitleFlag = 0;
	teletextInfo.freshCounter = 0;

	memset(teletextInfo.text, 0 ,sizeof(teletextInfo.text));
	for( page=0; page<1000; page++ )
	{
		teletextInfo.text[page][0][0]=
		teletextInfo.text[page][0][1]=
		teletextInfo.text[page][0][2]=
		teletextInfo.text[page][0][3]=32;

		teletextInfo.text[page][0][4]=page/100+48;
		teletextInfo.text[page][0][5]=page/10 -
			(teletextInfo.text[page][0][4]-48)*10 + 48;
		teletextInfo.text[page][0][6]=page-
			(teletextInfo.text[page][0][4]-48)*100 -
			(teletextInfo.text[page][0][5]-48)*10 + 48;
	}
}

static int32_t teletext_nextPageNumber(int32_t pageNumber)
{
	int32_t nextPage = pageNumber;

	do {
		nextPage++;
		if(nextPage >= 1000) {
			nextPage = 0;
		}
	} while((teletextInfo.text[nextPage][0][2] != 'P') && (teletextInfo.subtitlePage != nextPage) && (pageNumber != nextPage));

	return nextPage;
}

static int32_t teletext_previousPageNumber(int32_t pageNumber)
{
	int32_t prevPage = pageNumber;

	do {
		prevPage--;
		if(prevPage < 0) {
			prevPage = 999;
		}
	} while((teletextInfo.text[prevPage][0][2] != 'P') && (teletextInfo.subtitlePage != prevPage) && (pageNumber != prevPage));

	return prevPage;
}

static void teletext_convToCyrillic(unsigned char c, char unsigned *str)
{
	if(c<'q')
		str[0]=0xD0;		//Cyrillic P
	else
		str[0]=0xD1;		//Cyrillic C

	if(c=='h')
	{
		str[0]=0xD1;
		str[1]=0x85;
	}
	else if(c=='f')
	{
		str[0]=0xD1;
		str[1]=0x84;
	}
	else if(c=='c')
	{
		str[0]=0xD1;
		str[1]=0x86;
	}
	else if(c=='v')
	{
		str[0]=0xD0;
		str[1]=0xB6;
	}
	else if(c=='w')
	{
		str[0]=0xD0;
		str[1]=0xB2;
	}
	else if(c=='&')
	{
		str[0]=0xD1;
		str[1]=0x8B;
	}
	else if(c=='z')
	{
		str[0]=0xD0;
		str[1]=0xB7;
	}
	else if(c==189)
	{
		str[0]=0xD0;
		str[1]=0xAD;
	}
	else if(c==96)
	{
		str[0]=0xD1;
		str[1]=0x8E;
	}
	else if(c==188)
	{
		str[0]=0xD1;
		str[1]=0x88;
	}
	else if(c==190)
	{
		str[0]=0xD1;
		str[1]=0x89;
	}
	else
		str[1]=cyrillic_table[c];

	str[2]='\0';
}

//Is PES packet correct?
static int CheckPesPacket(void)
{
	int i = 0;
	int stream_id;
	unsigned int PES_packet_length;

	if ( (PesPacketBuffer[0] != 0) || (PesPacketBuffer[1] != 0) || (PesPacketBuffer[2] != 1))
	{
		// PES ERROR: does not start with 0x000001
		return 0;
	}

	i = 3;

	// stream_id: e0=video, c0=audio, bd=DVB subtitles, AC3 etc
	stream_id = PesPacketBuffer[i++];

	PES_packet_length = (PesPacketBuffer[i] << 8) | PesPacketBuffer[i + 1];
	i += 2;

	if ( (stream_id & 0xe0) == 0xe0)
	{
		// Video stream - PES_packet_length must be equal to zero
		if (PES_packet_length!=0)
		{
			// ERROR IN VIDEO PES STREAM: PES_packet_length=%d (must be zero)\n",PES_packet_length);
			return 0;
		}
	}
	else
	{
		if (PES_packet_length + 6 != PesPacketLength)
		{
			// ERROR IN NON-VIDEO PES: n=%d,PES_packet_length=%d\n",n,PES_packet_length
			return 0;
		}
	}

	return 1;
}

static inline unsigned char unham(unsigned char low, unsigned char high)
{
	return (unhamtab[high] << 4) | (unhamtab[low] & 0x0F);
}

static void SetLine(int line,
			unsigned char* data,
			int mag,
			int last_line)
{
	static int writingEnd=0;
	static unsigned char page[TELETEXT_SYMBOL_LINE_COUNT][TELETEXT_SYMBOL_ROW_COUNT];
	static int row = 0;
	static int m_nPage;
	static int m_Valid = 0;
	static unsigned char timeSec=0;

	unsigned char tmp[TELETEXT_SYMBOL_ROW_COUNT];
	unsigned char teletext_subtitle_line_text[TELETEXT_SYMBOL_ROW_COUNT];
	int column;
	static int outSubtitle = 0;
	int rowCount = TELETEXT_SYMBOL_ROW_COUNT;
	int lineCount = TELETEXT_SYMBOL_LINE_COUNT;

	if (line == 0)
	{
		if(!teletextInfo.status)
			teletextInfo.status=teletextStatus_begin;

		// Using this buffer to start a brand new page.
		m_nPage = (mag << 8) | unham(data[0], data[1]); // The lower two (hex) numbers of page
		m_nPage = (((m_nPage & 0xF00) >> 8 ) * 100) + ((m_nPage & 0xF0) >> 4) * 10 + (m_nPage & 0xF);


		teletextInfo.cyrillic[m_nPage] =	((unham(data[6], data[7]) >> 5) & 0x07);

		m_Valid = 1;
		if(outSubtitle && teletextInfo.subtitleFlag && teletext_isEnable())
			interface_displayMenu(1);
		if(outSubtitle)
			outSubtitle = 0;
	}

	if (m_Valid && (mag == m_nPage / 100))
	{
		int text_index = 0;

		if (line <= 24)
		{
			if (line == 0)
			{
				if((data[8]==32) && (data[9]==32) && (data[10]==32))	//Subtitles
				{
					outSubtitle=1;
					if(teletextInfo.subtitlePage == -1)
					{
						teletextInfo.subtitlePage = (m_nPage/256)*100 +
							((m_nPage-(m_nPage/256)*256)/16)*10 +
							m_nPage-((m_nPage/256)*256 + ((m_nPage-(m_nPage/256)*256)/16)*16);
					}
					memset(teletextInfo.subtitle, ' ', lineCount*rowCount);
					row = lineCount-5;
				}
				else													//Usual page
				{
					tmp[0]=' ';
					tmp[1]=' ';
					tmp[2]='P';
					tmp[3]=' ';
					tmp[4]=data[8];
					tmp[5]=data[9];
					tmp[6]=data[10];
					tmp[7]=' ';
					row = 0;
				}
				memcpy(&tmp[8], data+8, 32);
			}
			else
				memcpy(tmp, data, rowCount);

			for (column = 1; column <= rowCount; column++)
			{
				char ch = tmp[column - 1] & 0x7f;
				if (ch >= ' ')
					teletext_subtitle_line_text[text_index++] = vtx2iso8559_1_table[ch - 0x20];
				else
					teletext_subtitle_line_text[text_index++] = ch;
			}

			if (line == 0)
				memcpy(teletextInfo.time, &teletext_subtitle_line_text[26], 14);

			if(row<lineCount-1)
				memcpy(&page[line][0], teletext_subtitle_line_text, rowCount);

			if(outSubtitle)
			{
				if(row>lineCount-5)
				{
					memcpy(&teletextInfo.subtitle[row][0], teletext_subtitle_line_text, rowCount);
				}
			}
			else if(row == 20)
			{
				teletextInfo.pageNumber = m_nPage;

				if(teletextInfo.status < teletextStatus_finished)
				{
					if(writingEnd==teletextInfo.pageNumber)
                        teletextInfo.status = teletextStatus_finished;

					if(teletextInfo.status==teletextStatus_begin)
					{
						writingEnd = m_nPage;
						teletextInfo.status=teletextStatus_processing;
					}
				}

				memcpy(&teletextInfo.text[teletextInfo.pageNumber][0][0], page, (lineCount-1)*rowCount);

				if((timeSec != page[0][39]) && (teletextInfo.status>=teletextStatus_demand) && (!teletextInfo.subtitleFlag))
				{
					timeSec = page[0][39];
					interface_displayMenu(1);
				}
			}

			row++;
		}
	}
}

static int ProcessPesPacket(void)
{
	int stream_id = PesPacketBuffer[3];

	//if ( (stream_id == 0xbd) && ( (PesPacketBuffer[PesPacketBuffer[8] + 9] >= 0x10) && (PesPacketBuffer[PesPacketBuffer[8] + 9] <= 0x1f) ) )
	{
		//int PTS_DTS_flags = (PesPacketBuffer[7] & 0xb0) >> 6;
		unsigned int k, j;
		int data_unit_id, data_len;

		k = PesPacketBuffer[8] + 10;

		while (k < PesPacketLength)
		{
			data_unit_id = PesPacketBuffer[k++];
			data_len = PesPacketBuffer[k++];
			(void)data_unit_id;

			if (data_len != 0x2c)
				data_len = 0x2c;

			for (j = k; j < k + data_len; j++)
				PesPacketBuffer[j] = invtab[PesPacketBuffer[j]];

			unsigned char mpag = unham(PesPacketBuffer[k + 2], PesPacketBuffer[k + 3]);
			unsigned char mag = mpag & 7;
			unsigned char line = (mpag >> 3) & 0x1f;

			// mag == 0 means page is 8nn
			if (mag == 0)
				mag = 8;

			SetLine(
				line,
				&PesPacketBuffer[k + 4],
				mag,
				23);	//TELETEXT_SYMBOL_LINE_COUNT-2
			k += data_len;
		}

		return 1;
	}
	//else
	{
		// "This is not a private data type 1 stream - are you sure you specified the correct PID?
		return 0;
	}
}

static void teletext_readPESPacket(unsigned char *buf, size_t size)
{
	unsigned char *ts_buf = buf;
	static int tsPacketCounter = -1;
	static int PesPacketDirty = 0;

	while(size >= 188) {
		int offset;
		int continuity_counter;
		int adaption_field_control;
		int discontinuity_indicator;

		continuity_counter = ts_buf[3] & 0x0f;
		adaption_field_control = (ts_buf[3] & 0x30) >> 4;
		discontinuity_indicator = 0;

		if((adaption_field_control == 2) || (adaption_field_control == 3)) {
			// adaption_field
			if(ts_buf[4] > 0) {
				// adaption_field_length
				discontinuity_indicator = (ts_buf[5] & 0x80) >> 7;
			}
		}

		/* Firstly, check the integrity of the stream */
		if(tsPacketCounter == -1) {
			tsPacketCounter = continuity_counter;
		} else {
			if((adaption_field_control != 0) && (adaption_field_control != 2)) {
				tsPacketCounter++;
				tsPacketCounter %= 16;
			}
		}

		if(tsPacketCounter != continuity_counter) {
			if(discontinuity_indicator == 0) {
				PesPacketDirty = 1;
			}
			tsPacketCounter = continuity_counter;
		}

		// Check payload start indicator.
		if(ts_buf[1] & 0x40) {
			if(!PesPacketDirty ) { //&& CheckPesPacket())
				if(!ProcessPesPacket()) {
					size -= 188;
					ts_buf = ts_buf + 188;
					continue;
				}
			}
			PesPacketDirty = 0;
			PesPacketLength = 0;
		}

		if((adaption_field_control == 2) || (adaption_field_control == 3)) {
			// Adaption field!!!
			int adaption_field_length = ts_buf[4];
			if(adaption_field_length > 182)
				adaption_field_length = 182;

			offset = 5 + adaption_field_length;
		} else {
			offset = 4;
		}

		if((adaption_field_control == 1) || (adaption_field_control == 3)) {
			uint32_t dataLength = 188 - offset;
			// Data
			if((PesPacketLength + dataLength) <= sizeof(PesPacketBuffer)) {
				memcpy(PesPacketBuffer + PesPacketLength, ts_buf + offset, dataLength);
				PesPacketLength += dataLength;
			}
		}

		size -= 188;
		ts_buf += 188;
	}
}

void teletext_displayPage(void)
{
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
		symbolHeight	= TELETEXT_SYMBOL_HEIGHT;
		eHeight			= TELETEXT_SYMBOL_EDGE_HEIGHT;
		mHeight			= TELETEXT_SYMBOL_MIDDLE_HEIGHT;
		emHeight		= TELETEXT_SYMBOL_EDGE_HEIGHT + TELETEXT_SYMBOL_MIDDLE_HEIGHT;
		upText			= 2;		//Text lifting
	}
	horIndent		= (interfaceInfo.screenWidth - rowCount*symbolWidth)/2;
	verIndent		= (interfaceInfo.screenHeight - lineCount*symbolHeight)/2 + symbolHeight;

	if(teletextInfo.selectedPage == teletextInfo.subtitlePage) {//If the subtitles appear suddenly
		teletextInfo.subtitleFlag = 1;
	}

	if((teletextInfo.status < teletextStatus_ready) && !teletextInfo.subtitleFlag) {
		if(teletextInfo.status == teletextStatus_finished) {
			teletextInfo.status = teletextStatus_ready;
		}

		if(teletextInfo.pageNumber && (teletextInfo.status < teletextStatus_demand)) {
			gfx_drawRectangle(DRAWING_SURFACE, 0x0, 0x0, 0x0, 0xFF, 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenWidth);
			for(column = 0; column < 25; column++) {
				if((teletextInfo.freshCounter) && (column >= 4) && (column <= 6)) {
					if(column == 4) {
						str[0] = teletextInfo.fresh[0] + 48;
					} else if(column==5) {
						if(teletextInfo.freshCounter == 1) {
							str[0] = ' ';
						} else {
							if(teletextInfo.freshCounter == 2) {
								str[0] = teletextInfo.fresh[1] + 48;
							}
						}
					} else if(column == 6) {
						str[0] = ' ';
					}
				} else {
					str[0]=teletextInfo.text[teletextInfo.pageNumber][0][column];
				}

				if(((str[0]>=64)&&(str[0]<=127))||(str[0]=='#')||(str[0]=='&')||(str[0]==247)||((str[0]>=188)&&(str[0]<=190))) {
					teletext_convToCyrillic(str[0],fu);
					gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 255, 255, 0xFF, column*symbolWidth+horIndent, verIndent-upText, (char*) fu, 0, 0);
				} else {
					gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 255, 255, 0xFF, column*symbolWidth+horIndent, verIndent-upText, (char*) str, 0, 0);
				}
			}
		}
	}

	if((teletextInfo.status >= teletextStatus_demand) || (teletextInfo.subtitleFlag)) {
		uint8_t (*curPageTextBuf)[40] = teletextInfo.text[teletextInfo.selectedPage];

		if(!teletextInfo.subtitleFlag) {
			gfx_drawRectangle(DRAWING_SURFACE, 0x0, 0x0, 0x0, 0xFF, 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenWidth);
			teletextInfo.nextPage[0] = teletext_nextPageNumber(teletextInfo.selectedPage);
			teletextInfo.nextPage[1] = teletext_nextPageNumber(teletextInfo.nextPage[0]);
			teletextInfo.nextPage[2] = teletext_nextPageNumber(teletextInfo.nextPage[1]);
			teletextInfo.previousPage = teletext_previousPageNumber(teletextInfo.selectedPage);

			memset(&curPageTextBuf[lineCount-1][0], ' ', rowCount);

			curPageTextBuf[lineCount-1][3]= 0x01;	//red color
			curPageTextBuf[lineCount-1][4]=
				teletextInfo.nextPage[0]/100+48;
			curPageTextBuf[lineCount-1][5]=
				teletextInfo.nextPage[0]/10 -
				(curPageTextBuf[lineCount-1][4]-48)*10 + 48;
			curPageTextBuf[lineCount-1][6]=
				teletextInfo.nextPage[0] -
				(curPageTextBuf[lineCount-1][4]-48)*100 -
				(curPageTextBuf[lineCount-1][5]-48)*10 + 48;

			curPageTextBuf[lineCount-1][13]= 0x02;	//green color
			curPageTextBuf[lineCount-1][14]=
				teletextInfo.nextPage[1]/100+48;
			curPageTextBuf[lineCount-1][15]=
				teletextInfo.nextPage[1]/10 -
				(curPageTextBuf[lineCount-1][14]-48)*10 + 48;
			curPageTextBuf[lineCount-1][16]=
				teletextInfo.nextPage[1] -
				(curPageTextBuf[lineCount-1][14]-48)*100 -
				(curPageTextBuf[lineCount-1][15]-48)*10 + 48;

			curPageTextBuf[lineCount-1][23]= 0x03;	//yellow color
			curPageTextBuf[lineCount-1][24]=
				teletextInfo.nextPage[2]/100+48;
			curPageTextBuf[lineCount-1][25]=
				teletextInfo.nextPage[2]/10 -
				(curPageTextBuf[lineCount-1][24]-48)*10 + 48;
			curPageTextBuf[lineCount-1][26]=
				teletextInfo.nextPage[2] -
				(curPageTextBuf[lineCount-1][24]-48)*100 -
				(curPageTextBuf[lineCount-1][25]-48)*10 + 48;

			curPageTextBuf[lineCount-1][33]= 0x06;	//cyan color
			curPageTextBuf[lineCount-1][34]=
				teletextInfo.previousPage/100+48;
			curPageTextBuf[lineCount-1][35]=
				teletextInfo.previousPage/10 -
				(curPageTextBuf[lineCount-1][34]-48)*10 + 48;
			curPageTextBuf[lineCount-1][36]=
				teletextInfo.previousPage -
				(curPageTextBuf[lineCount-1][34]-48)*100 -
				(curPageTextBuf[lineCount-1][35]-48)*10 + 48;
		}

		if(!teletextInfo.freshCounter) {
			curPageTextBuf[0][4]=
				teletextInfo.selectedPage/100+48;
			curPageTextBuf[0][5]=
				teletextInfo.selectedPage/10 -
				(curPageTextBuf[0][4]-48)*10 + 48;
			curPageTextBuf[0][6]=
				teletextInfo.selectedPage -
				(curPageTextBuf[0][4]-48)*100 -
				(curPageTextBuf[0][5]-48)*10 + 48;
		}

		memcpy(&curPageTextBuf[0][26], teletextInfo.time, 14);

		for(line = 0; line < lineCount; line++) {
			alpha = 1;
			red = 255;
			green = 255;
			blue = 255;
			Lang = teletextInfo.cyrillic[teletextInfo.selectedPage];
			flagDH=0;
			flagDW=0;
			flagDS=0;
			box=0;

			for(column=0; column < rowCount; column++) {
				if(teletextInfo.selectedPage != teletextInfo.subtitlePage) {
					if((teletextInfo.status == teletextStatus_demand) &&
					   (line == 0) && (column >= 7) && (column <= 10))
					{
						if(column == 7) {
							str[0] = ' ';
						} else {
							str[0] = teletextInfo.text[teletextInfo.pageNumber][0][column];
						}
                    } else {
						str[0] = curPageTextBuf[line][column];
					}
				} else {
					str[0]=teletextInfo.subtitle[line][column];
				}

				if(str[0]<0x20)			//Special simbols
				{
					switch (str[0])
					{
					// Alpha Colour (Set After)
					case 0 :
						alpha = 1;
						red = 0;
						green = 0;
						blue = 0;
						//dprintf("%s:alpha black\n", __FUNCTION__);
						break;
					case 1 :
						alpha = 1;
						red = 0xFF;
						green = 0;
						blue = 0;
						//dprintf("%s:alpha red\n", __FUNCTION__);
						break;
					case 2 :
						alpha = 1;
						red = 0;
						green = 0xFF;
						blue = 0;
						//dprintf("%s:alpha green\n", __FUNCTION__);
						break;
					case 3 :
						alpha = 1;
						red = 0xFF;
						green = 0xFF;
						blue = 0;
						//dprintf("%s:alpha yellow\n", __FUNCTION__);
						break;
					case 4 :
						alpha = 1;
						red = 0;
						green = 0;
						blue = 0xFF;
						//dprintf("%s:alpha blue\n", __FUNCTION__);
						break;
					case 5 :
						alpha = 1;
						red = 0xFF;
						green = 0;
						blue = 0xFF;
						//dprintf("%s:alpha magenta\n", __FUNCTION__);
						break;
					case 6 :
						alpha = 1;
						red = 0;
						green = 0xFF;
						blue = 0xFF;
						//dprintf("%s:alpha cyan\n", __FUNCTION__);
					break;
					case 7 :
						alpha = 1;
						red = 0xFF;
						green = 0xFF;
						blue = 0xFF;
						//dprintf("%s:alpha white\n", __FUNCTION__);
						break;
					case 0x8 :
						// Start Flash (Set After)
						//dprintf("%s:start flash\n", __FUNCTION__);
						break;
					case 0x9 :
						// Steady (Set At)
						//dprintf("%s: <steady>\n", __FUNCTION__);
						break;
					case 0xa :
						// End Box (Set After)
						box--;
						//dprintf("%s: <end box>\n", __FUNCTION__);
						break;
					case 0xb :
						// Start Box (Set After)
						box++;
						//dprintf("%s: <start box>\n", __FUNCTION__);
						break;
					case 0xc :
						// Normal Size (Set At)
						flagDH=0;
						flagDW=0;
						flagDS=0;
						//dprintf("%s: <normal size>\n", __FUNCTION__);
						break;
					case 0xd :
						// Double height (Set After)
						flagDH=1;
						flagDW=0;
						flagDS=0;
						//dprintf("%s: <double height>\n", __FUNCTION__);
						break;
					case 0xe :
						// Double width (Set After)
						flagDH=0;
						flagDW=1;
						flagDS=0;
						//dprintf("%s: <double width>\n", __FUNCTION__);
						break;
					case 0xf :
						// Double size (Set After)
						flagDH=0;
						flagDW=0;
						flagDS=1;
						//dprintf("%s: <double size>\n", __FUNCTION__);
						break;
					// Mosaic colour(Set After)
					case 0x10 :
						alpha = 0;
						red = 0;
						green = 0;
						blue = 0;
						//dprintf("%s: <mosaic black>\n", __FUNCTION__);
						break;
					case 0x11 :
						alpha = 0;
						red = 0xFF;
						green = 0;
						blue = 0;
						//dprintf("%s: <mosaic red>\n", __FUNCTION__);
						break;
					case 0x12 :
						alpha = 0;
						red = 0;
						green = 0xFF;
						blue = 0;
						//dprintf("%s: <mosaic green>\n", __FUNCTION__);
						break;
					case 0x13 :
						alpha = 0;
						red = 0xFF;
						green = 0xFF;
						blue = 0;
						//dprintf("%s: <mosaic yellow>\n", __FUNCTION__);
						break;
					case 0x14 :
						alpha = 0;
						red = 0;
						green = 0;
						blue = 0xFF;
						//dprintf("%s: <mosaic blue>\n", __FUNCTION__);
						break;
					case 0x15 :
						alpha = 0;
						red = 0xFF;
						green = 0;
						blue = 0xFF;
						//dprintf("%s: <mosaic magenta>\n", __FUNCTION__);
						break;
					case 0x16 :
						alpha = 0;
						red = 0;
						green = 0xFF;
						blue = 0xFF;
						//dprintf("%s: <mosaic cyan>\n", __FUNCTION__);
						break;
					case 0x17 :
						alpha = 0;
						red = 0xFF;
						green = 0xFF;
						blue = 0xFF;
						//dprintf("%s: <mosaic white>\n", __FUNCTION__);
						break;
					case 0x18 :
						// Conceal (Set At)
						//dprintf("%s: <conceal>\n", __FUNCTION__);
						break;
					case 0x19 :
						// Contiguous Mosaic Graphics (Set At)
						//dprintf("%s: <contiguous mosaic>\n", __FUNCTION__);
						break;
					case 0x1A :
						// Seperated Mosaic Graphics (Set At)
						//dprintf("%s: <seperated mosaic>\n", __FUNCTION__);
						break;
					case 0x1B :
						// Escape (Set After)
						if(Lang)
							Lang = 0;
						else
							Lang = 1;
						//dprintf("%s: <escape>\n", __FUNCTION__);
						break;
					case 0x1C :
						// Black background (Set At)
						gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0xFF,
										column*symbolWidth+horIndent,
										line*symbolHeight+verIndent-symbolHeight,
										interfaceInfo.screenWidth-2*horIndent - column*symbolWidth,
										symbolHeight);
						//dprintf("%s: <black background>\n", __FUNCTION__);
						break;
					case 0x1D :
						// New background
						// The foreground colour becomes the background colour
						// any new characters until foreground would be invisible.
						gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF,
										column*symbolWidth+horIndent,
										line*symbolHeight+verIndent-symbolHeight,
										interfaceInfo.screenWidth-2*horIndent - column*symbolWidth,
										symbolHeight);
						//dprintf("%s: <new background>\n", __FUNCTION__);
						break;
					case 0x1E :
						// Hold Mosaics (Set At)
						//dprintf("%s: <hold mosaics>\n", __FUNCTION__);
						break;

					case 0x1F :
						// Release Mosaics (Set At)
						//dprintf("%s: <release mosaics>\n", __FUNCTION__);
					break;

					default :
						//dprintf("%s: <default>\n", __FUNCTION__);
						break;
					}
					str[0]=' ';
				}

				if(box)
					gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, symbolHeight);

				if(alpha)	//Text
				{
					if((teletextInfo.cyrillic)&&(Lang)&&(((str[0]>=64)&&(str[0]<=127))||(str[0]=='#')||(str[0]=='&')||(str[0]==247)||((str[0]>=188)&&(str[0]<=190))))
					{
						teletext_convToCyrillic(str[0],fu);
						gfx_drawText(DRAWING_SURFACE, pgfx_font, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-upText, (char*) fu, 0, 0);
					}
					else
						gfx_drawText(DRAWING_SURFACE, pgfx_font, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-upText, (char*) str, 0, 0);
				}
				else		//Pseudographics
				{
					switch (str[0])
					{
						case 33:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							break;
						}
						case 34:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						}
						case 36:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							break;
						}
						case 37:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							break;
						}
						case 38:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						}
						case 39:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						}
						case 40:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						}
						case 41:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						}
						case 42:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						}
						case 43:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						}
						case 44:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, mHeight);
							break;
						}
						case 45:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						}
						case 46:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						}
						case 47:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, emHeight);
							break;
						}
						case 48:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							break;
						}
						case 49:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							break;
						}
						case 50:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 51:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							break;
						}
						case 52:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							break;
						}
						case 53:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							break;
						}
						case 54:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						}
						case 55:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						}
						case 56:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						}
						case 57:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						}
						case 58:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						}
						case 59:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						}
						case 60:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						}
						case 61:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							break;
						}
						case 62:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						}
						case 63:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, emHeight);
							break;
						}
						case 96:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 97:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 98:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 99:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 100:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 101:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 102:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 103:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 104:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						}
						case 105:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						}
						case 106:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						}
						case 107:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						}
						case 108:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 109:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						}
						case 110:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						}
						case 111:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						}
						case 112:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						}
						case 113:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						}
						case 114:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							break;
						}
						case 115:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						}
						case 116:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 117:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 118:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 119:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-eHeight, rWidth, eHeight);
							break;
						}
						case 120:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						}
						case 121:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, mHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, symbolWidth, eHeight);
							break;
						}
						case 122:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						}
						case 124:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, symbolWidth, emHeight);
							break;
						}
						case 127:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, symbolHeight);
							break;
						}
						case 163:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, symbolWidth, eHeight);
							break;
						}
						case 188:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-eHeight, lWidth, eHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						}
						case 190:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-symbolHeight, lWidth, symbolHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-emHeight, rWidth, emHeight);
							break;
						}
						case 247:
						{
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-emHeight, lWidth, emHeight);
							gfx_drawRectangle(DRAWING_SURFACE, red, green, blue, 0xFF, column*symbolWidth+horIndent+lWidth, line*symbolHeight+verIndent-symbolHeight, rWidth, symbolHeight);
							break;
						}
						default:
						{
							gfx_drawText(DRAWING_SURFACE, pgfx_font, red, green, blue, 0xFF, column*symbolWidth+horIndent, line*symbolHeight+verIndent-upText, (char*) str, 0, 0);
							break;
						}
					}
				}

				if(flagDH || flagDW || flagDS)
				{
					gfx_DoubleSize(DRAWING_SURFACE, line, column, flagDH, flagDW, flagDS, symbolWidth, symbolHeight, horIndent, verIndent);
					if((!flagDH)&&(str[0]!=' '))
						column++;
				}
			}
		}
	}
}

static void *teletext_funcThread(void *pArg)
{
	int32_t fd = (int32_t)pArg;
	uint8_t buff[TELETEXT_PACKET_BUFFER_SIZE];
	int32_t len;

	
	struct pollfd pfd[1];

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;

	while(1) {
		pthread_testcancel();

		if(poll(pfd, 1, 100)) {
			if(pfd[0].revents & POLLIN) {

				pthread_testcancel();
//				memset(buff, 0, sizeof(buff));
				len = read(fd, buff, sizeof(buff));
				if(len < 0) {
					eprintf("%s: %d: errno=%d: %s\n", __func__, __LINE__, errno, strerror(errno));
					usleep(1000);
					continue;
				}

				dprintf("len=%d\n", len);
				teletext_readPESPacket(buff, len);
			}
		}
	}

	return NULL;
}


static int32_t teletext_redraw(void)
{
	if(teletextInfo.subtitleFlag == 0) {
		interface_playControlRefresh(1);
	} else {
		interface_playControlHide(1);
	}
	return 0;
}

int32_t teletext_processCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	if(teletextInfo.exists) {
		if(cmd->command == interfaceCommandTeletext) {
			if(teletext_isEnable()) {
				teletext_enable(0);
				teletextInfo.subtitleFlag = 0;
				if(teletextInfo.status == teletextStatus_demand) {
					teletextInfo.status = teletextStatus_processing;
				}
			} else {
				teletext_enable(1);
			}
			teletextInfo.freshCounter = 0;
			teletextInfo.selectedPage = 100;
			teletext_redraw();
			return 0;
		}

		if(teletext_isEnable() && (!teletextInfo.subtitleFlag)) {
			if((cmd->command >= interfaceCommand0) && (cmd->command <= interfaceCommand9)) {
				uint32_t i;
				teletextInfo.fresh[teletextInfo.freshCounter] = cmd->command - interfaceCommand0;
				teletextInfo.freshCounter++;

				for(i = 0; i < teletextInfo.freshCounter; i++) {
					teletextInfo.text[teletextInfo.selectedPage][0][4 + i] =
						teletextInfo.fresh[i] + 48;
				}

				for(i = teletextInfo.freshCounter; i < 3; i++) {
					teletextInfo.text[teletextInfo.selectedPage][0][4 + i] = ' ';
				}

				if(teletextInfo.freshCounter == 3) {
					teletextInfo.selectedPage = teletextInfo.fresh[0] * 100 +
						teletextInfo.fresh[1] * 10 +
						teletextInfo.fresh[2];
					teletextInfo.freshCounter = 0;

					if(teletextInfo.selectedPage == teletextInfo.subtitlePage) {
						teletextInfo.subtitleFlag = 1;
					} else if(teletextInfo.status != teletextStatus_ready) {
						teletextInfo.status = teletextStatus_demand;
					}
				}
				teletext_redraw();
				return 0;
			} else if(teletextInfo.status >= teletextStatus_demand) {
				switch (cmd->command) {
					case interfaceCommandLeft:
					case interfaceCommandBlue:
						teletextInfo.selectedPage = teletextInfo.previousPage;
						break;
					case interfaceCommandRight:
					case interfaceCommandRed:
						teletextInfo.selectedPage = teletextInfo.nextPage[0];
						break;
					case interfaceCommandGreen:
						teletextInfo.selectedPage = teletextInfo.nextPage[1];
						break;
					case interfaceCommandYellow:
						teletextInfo.selectedPage = teletextInfo.nextPage[2];
						break;
					default:
						break;
				}
				if(teletextInfo.selectedPage == teletextInfo.subtitlePage) {
					teletextInfo.subtitleFlag = 1;
				}
				teletext_redraw();
				return 0;
			}
		}
	}

	return -1;
}

int32_t teletext_isTeletextShowing(void)
{
	if( teletextInfo.exists &&
		teletext_isEnable() &&
		!teletextInfo.subtitleFlag )
	{
		return 1;
	}
	return 0;
}

int32_t teletext_isTeletextReady(void)
{
	if(	teletext_isEnable() && 
		(teletextInfo.status >= teletextStatus_demand) &&
		!teletextInfo.subtitleFlag )
	{
		return 1;
	}
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

	if(  result &&
		(result->valuestring != NULL) &&
		(strcmp(result->valuestring, "ok") == 0))
	{
		cJSON_Delete(result);
		cJSON_Delete(params);

		return 1;
	}

	cJSON_Delete(result);
	cJSON_Delete(params);

	return 0;
}
#endif

int32_t teletext_start(DvbParam_t *param)
{
	int32_t ret = 0;
	int32_t hasTeletext = 0;

	if(teletext_thread != 0) {
		return -1;
	}
	tt_fd = -1;

#if (defined STSDK)
	hasTeletext = st_teletext_start();
	tt_fd = open(TELETEXT_pipe_TS, O_RDONLY);
	if(tt_fd < 0) {
		eprintf("Error in opening file %s\n", TELETEXT_pipe_TS);
		return -2;
	}

#else
	if(param->mode != DvbMode_Multi) {
		hasTeletext = dvb_hasTeletext(param->adapter, &tt_fd);
	}
#endif
	if(hasTeletext && (tt_fd >= 0)) {
		int32_t st;
		teletext_init();

		st = pthread_create(&teletext_thread, NULL, teletext_funcThread, (void *)tt_fd);
		if(st != 0) {
			eprintf("%s: ERROR not create thread\n", __func__);
			return 0;
		}
		teletextInfo.exists = 1;
	}

	return ret;
}

int32_t teletext_stop(void)
{
	int32_t ret = 0;

	dprintf("TTX_stop_pthread\n");
	if(teletext_thread) {
		pthread_cancel(teletext_thread);
		pthread_join(teletext_thread, NULL);
		teletext_thread = 0;
	}
	teletextInfo.status = teletextStatus_disabled;
	teletextInfo.exists = 0;
	teletext_enable(0);
#if (defined STSDK)
	close(tt_fd);
#endif

	return ret;
}

#endif //ENABLE_TELETEXT
