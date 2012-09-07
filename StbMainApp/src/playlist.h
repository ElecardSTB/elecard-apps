#ifndef __PLAYLIST_H
#define __PLAYLIST_H

/*
 playlist.h

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

/** @file playlist.h Playlists and Favorites backend
 */
/** @defgroup playlist Playlists and Favorites features
 *  @ingroup StbMainApp
 */

/****************
* INCLUDE FILES *
*****************/

#include "interface.h"
#include "xspf.h"

/*******************
* EXPORTED MACROS  *
********************/

/******************************************************************
* EXPORTED TYPEDEFS                                               *
*******************************************************************/

#define PLAYLIST_ERR_OK         (0)
#define PLAYLIST_ERR_DOWNLOAD   (1)
#define PLAYLIST_ERR_PARSE      (2)

/*******************************************************************
* EXPORTED DATA                                                    *
********************************************************************/

extern interfaceListMenu_t playlistMenu;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

#ifdef __cplusplus
extern "C" {
#endif

void playlist_buildMenu(interfaceMenu_t *pParent);

int playlist_addUrl(char* url, char *description);

int playlist_setLastUrl(char* url);

int playlist_startNextChannel(int direction, void* pArg);

int playlist_setChannel(int channel, void* pArg);

int playlist_streamStart();

char *playlist_getLastURL();

/** @ingroup playlist
 *  Download and parse playlist from specified URL
 *  @param[in]  url            URL of the playlist
 *  @param[in]  pEntryCallback Function to be called for each playlist entry
 *  @param[in]  pArg           User data to be used in callback
 *  @return     0 on success
 *  @sa         playlist_getFromBuffer()
 */
int playlist_getFromURL(const char *url, xspfEntryHandler pEntryCallback, void *pArg);

/** @ingroup playlist
 *  Parse playlist from buffer
 *  @param[in]  data           Buffer with playlist data
 *  @param[in]  size           Size of buffer
 *  @param[in]  pEntryCallback Function to be called for each playlist entry
 *  @param[in]  pArg           User data to be used in callback
 *  @return     0 on success
 *  @sa         playlist_getFromURL()
 */
int playlist_getFromBuffer(const char *data, const size_t size, xspfEntryHandler pEntryCallback, void *pArg);

int playlist_getFromFile(const char *filename, xspfEntryHandler pEntryCallback, void *pArg);

#ifdef __cplusplus
}
#endif

#endif // __PLAYLIST_H
