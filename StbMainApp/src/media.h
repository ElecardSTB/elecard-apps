#if !defined(__MEDIA_H)
#define __MEDIA_H

/*
 media.h

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

#include <dirent.h>

/*******************
* EXPORTED MACROS  *
********************/

/*********************
* EXPORTED TYPEDEFS  *
**********************/

/*******************
* EXPORTED DATA    *
********************/

extern const char usbRoot[];
extern char       currentPath[];
extern interfaceListMenu_t MediaMenu;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

#ifdef __cplusplus
extern "C" {
#endif

int media_startPlayback();

/**
*   @brief Function used to initialize Media play control and start playback of appControlInfo.mediaInfo.filename
*
*   @retval void
*/
int media_streamStart(void);

/**
*   @brief Function used to stop Media playback
*
*   @retval void
*/
void media_stopPlayback(void);

/**
*   @brief Function used to build the media menu data structures
*
*   @retval void
*/
void media_buildMediaMenu(interfaceMenu_t *pParent);

void media_cleanupMenu(void);

/**
*   @brief Function used to build the media progress slider data structure
*
*   @retval void
*/
void media_buildSlider(void);

/**
*   @brief Function used to set the current media playback position
*
*   @param  value		IN		Location within media file (percentage)
*
*   @retval void
*/
void media_setPosition(long value);

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
int  media_playURL(int which, const char* URL, const char *description, const char* thumbnail);

int  media_slideshowStart(void);

int  media_slideshowStop(int disable);

int  media_slideshowNext(int direction);

int  media_slideshowSetMode(int mode);

int  media_slideshowSetTimeout(int timeout);

int  media_initUSBBrowserMenu(interfaceMenu_t *pMenu, void* pArg);

int  media_initSambaBrowserMenu(interfaceMenu_t *pMenu, void* pArg);

int  media_setMode(interfaceMenu_t *pMenu, void *pArg);

/**
*   @brief Function used to determine count of USB flash or hard disk drives (not CD drives)
*
*   @retval int Storage count
*/
int  media_scanStorages(void);

void media_storagesChanged(void);

/**
*   @brief Function used to determine media type from file extension
*
*   @return mediaAll if extension is unknown
*/
mediaType media_getMediaType(const char *filename);

/** Callback for scandir function, used to select mounted USB storages
 */
int media_select_usb(const struct dirent * de);

/* like strcmp but compare sequences of digits numerically */
int strnaturalcmp(const char *s1, const char *s2);

int naturalsort (const struct dirent **e1,
                 const struct dirent **e2);

#ifdef __cplusplus
}
#endif

#endif /* __MEDIA_H      Do not add any thing below this line */
