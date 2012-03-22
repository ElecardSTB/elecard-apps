
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

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "teletext.h"

#ifdef ENABLE_TELETEXT

#include "gfx.h"
#include "interface.h"

#include <stdio.h>
#include <string.h>

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

static int teletext_nextPageNumber(int pageNumber);
static int teletext_previousPageNumber(int pageNumber);

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

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

void teletext_init()
{
	int page;

	appControlInfo.teletextInfo.status = teletextStatus_disabled;
	appControlInfo.teletextInfo.subtitlePage = -1;
	appControlInfo.teletextInfo.subtitleFlag = 0;
	appControlInfo.teletextInfo.freshCounter = 0;

	memset(appControlInfo.teletextInfo.text, 0 ,sizeof(appControlInfo.teletextInfo.text));
	for( page=0; page<1000; page++ )
	{
		appControlInfo.teletextInfo.text[page][0][0]=
		appControlInfo.teletextInfo.text[page][0][1]=
		appControlInfo.teletextInfo.text[page][0][2]=
		appControlInfo.teletextInfo.text[page][0][3]=32;

		appControlInfo.teletextInfo.text[page][0][4]=page/100+48;
		appControlInfo.teletextInfo.text[page][0][5]=page/10 -
			(appControlInfo.teletextInfo.text[page][0][4]-48)*10 + 48;
		appControlInfo.teletextInfo.text[page][0][6]=page-
			(appControlInfo.teletextInfo.text[page][0][4]-48)*100 -
			(appControlInfo.teletextInfo.text[page][0][5]-48)*10 + 48;
	}
}

static int teletext_nextPageNumber(int pageNumber)
{
	int teletext_nextPageNumber;

	teletext_nextPageNumber = pageNumber;
	do
	{
		if(teletext_nextPageNumber==999)
			teletext_nextPageNumber=0;
		else
			teletext_nextPageNumber++;
	}
	while (((appControlInfo.teletextInfo.text[teletext_nextPageNumber][0][2] !='P' ) && (appControlInfo.teletextInfo.subtitlePage != teletext_nextPageNumber)) && (pageNumber != teletext_nextPageNumber));

	return teletext_nextPageNumber;
}

static int teletext_previousPageNumber(int pageNumber)
{
	int previoustPageNumber;

	previoustPageNumber = pageNumber;
	do
	{
		if(previoustPageNumber==0)
			previoustPageNumber=999;
		else
			previoustPageNumber--;
	}
	while (((appControlInfo.teletextInfo.text[previoustPageNumber][0][2] !='P' ) && (appControlInfo.teletextInfo.subtitlePage != previoustPageNumber)) && (pageNumber != previoustPageNumber));

	return previoustPageNumber;
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
		if(!appControlInfo.teletextInfo.status)
			appControlInfo.teletextInfo.status=teletextStatus_begin;

		// Using this buffer to start a brand new page.
		m_nPage = (mag << 8) | unham(data[0], data[1]); // The lower two (hex) numbers of page

		appControlInfo.teletextInfo.cyrillic[(m_nPage/256)*100 + ((m_nPage-(m_nPage/256)*256)/16)*10 + m_nPage-((m_nPage/256)*256 + ((m_nPage-(m_nPage/256)*256)/16)*16)] =
			((unham(data[6], data[7]) >> 5) & 0x07);

		m_Valid = 1;
		if(outSubtitle && appControlInfo.teletextInfo.subtitleFlag && interfaceInfo.teletext.show)
			interface_displayMenu(1);
		if(outSubtitle)
			outSubtitle = 0;
	}

	if (m_Valid)
	{
		int text_index = 0;

		if (line <= last_line)
		{
			if (line == 0)
			{
				if((data[8]==32) && (data[9]==32) && (data[10]==32))	//Subtitles
				{
					outSubtitle=1;
					if(appControlInfo.teletextInfo.subtitlePage == -1)
					{
						appControlInfo.teletextInfo.subtitlePage = (m_nPage/256)*100 +
							((m_nPage-(m_nPage/256)*256)/16)*10 +
							m_nPage-((m_nPage/256)*256 + ((m_nPage-(m_nPage/256)*256)/16)*16);
					}
					memset(appControlInfo.teletextInfo.subtitle, ' ', lineCount*rowCount);
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
				memcpy(appControlInfo.teletextInfo.time, &teletext_subtitle_line_text[26], 14);

			if(row<lineCount-1)
				memcpy(&page[row][0], teletext_subtitle_line_text, rowCount);

			if(outSubtitle)
			{
				if(row>lineCount-5)
				{
					memcpy(&appControlInfo.teletextInfo.subtitle[row][0], teletext_subtitle_line_text, rowCount);
				}
			}
			else if(row==lineCount-2)
			{
				appControlInfo.teletextInfo.pageNumber = (page[0][4]-48)*100+(page[0][5]-48)*10+page[0][6]-48;

				if(appControlInfo.teletextInfo.status < teletextStatus_finished)
				{
					if(writingEnd==appControlInfo.teletextInfo.pageNumber)
                        appControlInfo.teletextInfo.status = teletextStatus_finished;

					if(appControlInfo.teletextInfo.status==teletextStatus_begin)
					{
						writingEnd=(page[0][4]-48)*100+(page[0][5]-48)*10+page[0][6]-48;
						appControlInfo.teletextInfo.status=teletextStatus_processing;
					}
				}

				memcpy(&appControlInfo.teletextInfo.text[appControlInfo.teletextInfo.pageNumber][0][0], page, (lineCount-1)*rowCount);

				if((timeSec != page[0][39]) && (appControlInfo.teletextInfo.status>=teletextStatus_demand) && (!appControlInfo.teletextInfo.subtitleFlag))
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

	if ( (stream_id == 0xbd) && ( (PesPacketBuffer[PesPacketBuffer[8] + 9] >= 0x10) && (PesPacketBuffer[PesPacketBuffer[8] + 9] <= 0x1f) ) )
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
	else
	{
		// "This is not a private data type 1 stream - are you sure you specified the correct PID?
		return 0;
	}
}

void teletext_readPESPacket(unsigned char *buf, size_t size)
{
	unsigned char *ts_buf = buf;
	int continuity_counter;
	int adaption_field_control;
	int discontinuity_indicator;
	static int tsPacketCounter = -1;
	static int PesPacketDirty = 0;

	while(size>=188)
	{
		continuity_counter = ts_buf[3] & 0x0f;
		adaption_field_control = (ts_buf[3] & 0x30) >> 4;
		discontinuity_indicator = 0;

		if ((adaption_field_control == 2) || (adaption_field_control == 3))
		{
			// adaption_field
			if (ts_buf[4] > 0)
			{
				// adaption_field_length
				discontinuity_indicator = (ts_buf[5] & 0x80) >> 7;
			}
		}

		/* Firstly, check the integrity of the stream */
		if (tsPacketCounter == -1)
			tsPacketCounter = continuity_counter;
		else
		{
			if ( (adaption_field_control != 0) && (adaption_field_control != 2) )
			{
				tsPacketCounter++;
				tsPacketCounter %= 16;
			}
		}

		if (tsPacketCounter != continuity_counter)
		{
			if (discontinuity_indicator == 0)
				PesPacketDirty = 1;
			tsPacketCounter = continuity_counter;
		}

		// Check payload start indicator.
		if (ts_buf[1] & 0x40)
		{
			if (!PesPacketDirty && CheckPesPacket())
			{
				if (!ProcessPesPacket())
				{
					size-=188;
					ts_buf=&ts_buf[188];
					continue;
				}
			}
			PesPacketDirty = 0;
			PesPacketLength = 0;
		}

		int i = 4;
		if ( (adaption_field_control == 2) || (adaption_field_control == 3) )
		{
				// Adaption field!!!
			int adaption_field_length = ts_buf[i++];
			if (adaption_field_length > 182)
				adaption_field_length = 182;

			i += adaption_field_length;
		}

		if ( (adaption_field_control == 1) || (adaption_field_control == 3) )
		{
			// Data
			if ( (PesPacketLength + (188 - i)) <= sizeof(PesPacketBuffer) )
			{
				memcpy(&PesPacketBuffer[PesPacketLength], &ts_buf[i],188-i);
				PesPacketLength += (188 - i);
			}
		}

		size-=188;
		ts_buf=&ts_buf[188];
	}
}

void teletext_displayTeletext()
{
	int line, column;
	int red, green, blue, alpha, Lang;
	int symbolWidth, symbolHeight, horIndent, verIndent, lWidth, rWidth, eHeight, mHeight, emHeight, upText;
	unsigned char str[2], fu[3];
	int oldPageNumber=0;
	int flagDH, flagDW, flagDS;
	int box;
	int rowCount = TELETEXT_SYMBOL_ROW_COUNT;
	int lineCount = TELETEXT_SYMBOL_LINE_COUNT;

	str[1]='\0';
	fu[2]='\0';

	symbolWidth		= TELETEXT_SYMBOL_WIDTH;
    lWidth			= TELETEXT_SYMBOL_RIGHR_WIDTH;
	rWidth			= TELETEXT_SYMBOL_LEFT_WIDTH;
	if(interfaceInfo.screenHeight == 480)
	{
		symbolHeight	= TELETEXT_SYMBOL_HEIGHT_480I;
		eHeight			= TELETEXT_SYMBOL_EDGE_HEIGHT_480I;
		mHeight			= TELETEXT_SYMBOL_MIDDLE_HEIGHT_480I;
		emHeight		= TELETEXT_SYMBOL_EDGE_HEIGHT_480I + TELETEXT_SYMBOL_MIDDLE_HEIGHT_480I;
		upText			= 1;		//Text lifting
	}
	else
	{
		symbolHeight	= TELETEXT_SYMBOL_HEIGHT;
		eHeight			= TELETEXT_SYMBOL_EDGE_HEIGHT;
		mHeight			= TELETEXT_SYMBOL_MIDDLE_HEIGHT;
		emHeight		= TELETEXT_SYMBOL_EDGE_HEIGHT + TELETEXT_SYMBOL_MIDDLE_HEIGHT;
		upText			= 2;		//Text lifting
	}
	horIndent		= (interfaceInfo.screenWidth - rowCount*symbolWidth)/2;
	verIndent		= (interfaceInfo.screenHeight - lineCount*symbolHeight)/2 + symbolHeight;

	if(interfaceInfo.teletext.pageNumber == appControlInfo.teletextInfo.subtitlePage)		//If the subtitles appear suddenly
		appControlInfo.teletextInfo.subtitleFlag = 1;

	if((appControlInfo.teletextInfo.status < teletextStatus_ready)&&(!appControlInfo.teletextInfo.subtitleFlag))
	{
		if(appControlInfo.teletextInfo.status==teletextStatus_finished)
		{
			appControlInfo.teletextInfo.status = teletextStatus_ready;
		}

		if((appControlInfo.teletextInfo.pageNumber != oldPageNumber) && (appControlInfo.teletextInfo.status < teletextStatus_demand))
		{
			oldPageNumber = appControlInfo.teletextInfo.pageNumber;
			gfx_drawRectangle(DRAWING_SURFACE, 0x0, 0x0, 0x0, 0xFF, 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenWidth);
			for(column=0;column<25;column++)
			{
				if((appControlInfo.teletextInfo.freshCounter) && (column>=4) && (column<=6))
				{
					if(column==4)
					{
						str[0]=appControlInfo.teletextInfo.fresh[0]+48;
					}
					else if(column==5)
					{
						if(appControlInfo.teletextInfo.freshCounter == 1)
							str[0]=' ';
						else
						{
							if(appControlInfo.teletextInfo.freshCounter == 2)
								str[0]=appControlInfo.teletextInfo.fresh[1]+48;
						}
					}
					else if(column==6)
						str[0]=' ';
				}
				else
					str[0]=appControlInfo.teletextInfo.text[appControlInfo.teletextInfo.pageNumber][0][column];

				if(((str[0]>=64)&&(str[0]<=127))||(str[0]=='#')||(str[0]=='&')||(str[0]==247)||((str[0]>=188)&&(str[0]<=190)))
				{
					teletext_convToCyrillic(str[0],fu);
					gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 255, 255, 0xFF, column*symbolWidth+horIndent, verIndent-upText, (char*) fu, 0, 0);
				}
				else
					gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 255, 255, 0xFF, column*symbolWidth+horIndent, verIndent-upText, (char*) str, 0, 0);
			}
		}
	}

	if((appControlInfo.teletextInfo.status >= teletextStatus_demand) || (appControlInfo.teletextInfo.subtitleFlag))
	{
		if(!appControlInfo.teletextInfo.subtitleFlag)
		{
			gfx_drawRectangle(DRAWING_SURFACE, 0x0, 0x0, 0x0, 0xFF, 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenWidth);
			appControlInfo.teletextInfo.nextPage[0] = teletext_nextPageNumber(interfaceInfo.teletext.pageNumber);
			appControlInfo.teletextInfo.nextPage[1] = teletext_nextPageNumber(appControlInfo.teletextInfo.nextPage[0]);
			appControlInfo.teletextInfo.nextPage[2] = teletext_nextPageNumber(appControlInfo.teletextInfo.nextPage[1]);
			appControlInfo.teletextInfo.previousPage = teletext_previousPageNumber(interfaceInfo.teletext.pageNumber);

			memset(&appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][0], ' ', rowCount);

			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][3]= 0x01;	//red color
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][4]=
				appControlInfo.teletextInfo.nextPage[0]/100+48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][5]=
				appControlInfo.teletextInfo.nextPage[0]/10 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][4]-48)*10 + 48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][6]=
				appControlInfo.teletextInfo.nextPage[0] -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][4]-48)*100 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][5]-48)*10 + 48;

			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][13]= 0x02;	//green color
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][14]=
				appControlInfo.teletextInfo.nextPage[1]/100+48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][15]=
				appControlInfo.teletextInfo.nextPage[1]/10 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][14]-48)*10 + 48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][16]=
				appControlInfo.teletextInfo.nextPage[1] -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][14]-48)*100 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][15]-48)*10 + 48;

			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][23]= 0x03;	//yellow color
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][24]=
				appControlInfo.teletextInfo.nextPage[2]/100+48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][25]=
				appControlInfo.teletextInfo.nextPage[2]/10 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][24]-48)*10 + 48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][26]=
				appControlInfo.teletextInfo.nextPage[2] -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][24]-48)*100 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][25]-48)*10 + 48;

			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][33]= 0x06;	//cyan color
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][34]=
				appControlInfo.teletextInfo.previousPage/100+48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][35]=
				appControlInfo.teletextInfo.previousPage/10 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][34]-48)*10 + 48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][36]=
				appControlInfo.teletextInfo.previousPage -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][34]-48)*100 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][lineCount-1][35]-48)*10 + 48;
		}

		if(!appControlInfo.teletextInfo.freshCounter)
		{
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][0][4]=
				interfaceInfo.teletext.pageNumber/100+48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][0][5]=
				interfaceInfo.teletext.pageNumber/10 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][0][4]-48)*10 + 48;
			appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][0][6]=
				interfaceInfo.teletext.pageNumber -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][0][4]-48)*100 -
				(appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][0][5]-48)*10 + 48;
		}

		memcpy(&appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][0][26], appControlInfo.teletextInfo.time, 14);

		for(line=0;line<lineCount;line++)
		{
			alpha = 1;
			red = 255;
			green = 255;
			blue = 255;
			Lang = appControlInfo.teletextInfo.cyrillic[interfaceInfo.teletext.pageNumber];
			flagDH=0;
			flagDW=0;
			flagDS=0;
			box=0;

			for(column=0;column<rowCount;column++)
			{
				if(interfaceInfo.teletext.pageNumber!=appControlInfo.teletextInfo.subtitlePage)
				{
					if((appControlInfo.teletextInfo.status == teletextStatus_demand) &&
					   (line==0) && (column>=7) && (column<=10))
					{
						if(column==7)
							str[0]=' ';
						else
							str[0]=appControlInfo.teletextInfo.text[appControlInfo.teletextInfo.pageNumber][0][column];
                    }
					else
						str[0]=appControlInfo.teletextInfo.text[interfaceInfo.teletext.pageNumber][line][column];
				}
				else
					str[0]=appControlInfo.teletextInfo.subtitle[line][column];

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
					if((appControlInfo.teletextInfo.cyrillic)&&(Lang)&&(((str[0]>=64)&&(str[0]<=127))||(str[0]=='#')||(str[0]=='&')||(str[0]==247)||((str[0]>=188)&&(str[0]<=190))))
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

#endif /* ENABLE_TELETEXT */
