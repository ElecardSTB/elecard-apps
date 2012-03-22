#ifndef __M3U_H
#define __M3U_H

/*
 m3u.h

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


/****************
* INCLUDE FILES *
*****************/

#include "defines.h"
#include "app_info.h"

/*******************
* EXPORTED MACROS  *
********************/

#define EXTM3U  "#EXTM3U"
#define EXTINF  "#EXTINF"

/******************************************************************
* EXPORTED TYPEDEFS                                               *
*******************************************************************/

typedef struct __m3uEntry_t
{
	char title[MENU_ENTRY_INFO_LENGTH];
	char url[MAX_URL];
} m3uEntry_t;

/*******************************************************************
* EXPORTED DATA                                                    *
********************************************************************/

extern char m3u_description[MENU_ENTRY_INFO_LENGTH];
extern char m3u_url[MAX_URL];

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

#ifdef __cplusplus
extern "C" {
#endif

int m3u_readEntry(FILE* f);
FILE*  m3u_initFile(const char *m3u_filename, const char* mode);
int m3u_addEntry(const char *m3u_filename, const char *url, const char *description);
int m3u_getEntry(const char *m3u_filename, int selected);
int m3u_findUrl(const char *m3u_filename, const char *url);
int m3u_createFile(const char *m3u_filename);
int m3u_deleteEntryByIndex(const char *m3u_filename, int index);
int m3u_deleteEntryByUrl(const char *m3u_filename, const char *url);
int m3u_replaceEntryByIndex(const char *m3u_filename, int index, const char *url, const char *description);
int m3u_readEntryFromBuffer(char **pData, int *length);

#ifdef __cplusplus
}
#endif

#endif //__M3U_H
