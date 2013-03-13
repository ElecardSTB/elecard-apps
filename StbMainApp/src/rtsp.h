#if !defined(__RTSP_H)
#define __RTSP_H

/*
 rtsp.h

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

/*******************
* INCLUDE FILES    *
********************/

#include "interface.h"

/*********************
* EXPORTED TYPEDEFS  *
**********************/

typedef struct
{
	char streamname[MENU_ENTRY_INFO_LENGTH];
	int pida;           //Audio Pid
	int pidv;           //Video Pid
	int pidp;           //PCR pid
	int vformat;        //the stream type assignment for the selected video pid
	int aformat;        //the stream type assignment for the selected audio pid
	int device;         //which /dev/dvb/adapter to use (2 or 3)
	unsigned int port;  //what RTSP port to use
	char ip[32];           //which ip address to connect to

	int custom_url;		// it this is not 0, then this stream is treated as custom url
} rtsp_stream_info;

/*******************
* EXPORTED DATA    *
********************/

extern interfaceListMenu_t rtspMenu;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

/**
*   @brief Function used to build the RTSP menu data structures
*
*   @retval void
*/
void rtsp_buildMenu(interfaceMenu_t *pParent);

int rtsp_fillStreamMenu(interfaceMenu_t *pMenu, void* pArg);

void rtsp_cleanupMenu(void);

/**
*   @brief Function used to stop RTSP input display
*
*   @param which    I       Screen to stop video on

*   @retval void
*/
void rtsp_stopVideo(int which);

/**
*   @brief Function used to play custom path or URL
*
*   @param  which		IN		Screen number
*   @param  URL			IN		Should be not null
*   @param  description	IN		If not null, description of playing stream to be set to
*   @param  thumbnail	IN		If not null, custom thumbnail to be set to
*
*   @retval void
*/
int rtsp_playURL(int which, const char *URL, const char* description, const char* thumbnail);

#endif /* __RTSP_H      Do not add any thing below this line */
