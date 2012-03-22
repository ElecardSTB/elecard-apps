#if !defined(__RTP_H)
	#define __RTP_H

/*
 rtp.h

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

#include "defines.h"
#include "interface.h"
#include "off_air.h"

/*******************
* EXPORTED MACROS  *
********************/

#define RTP_EPG_ERROR_TIMEOUT    (3000)

/*******************
* EXPORTED DATA    *
********************/

extern interfaceListMenu_t rtpStreamMenu[screenOutputs];
extern interfaceEpgMenu_t  rtpRecordMenu;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

/** Function used to build the RTP menu data structures
*   @param[in]  pParent Parent menu of rtpMenu
*/
void rtp_buildMenu(interfaceMenu_t *pParent);

/** Function used to init contents of RTP menu
*
*   Called when user enters IPTV stream menu. 
*   Should fill menu with streams, and if it's impossible to do immediately,
*   run thread in background that collects streams from SAP or http playlist.
*   @param[in] pMenu Pointer to rtpMenu
*   @param[in] pArg  Screen (main or pip) to build menu on
*
*   @return 0 on success
*/
int rtp_initStreamMenu(interfaceMenu_t *pMenu, void* pArg);

/** Function used to init contents of RTP EPG menu
*   Calls menuActionShowMenu on success to switch to rtpEpgMenu
*
*   @param[in]  pMenu Pointer to calling menu
*   @param[in]  pArg  Stream number in current playlist
*
*   @return 0 on success
*/
int rtp_initEpgMenu(interfaceMenu_t *pMenu, void* pArg);

void rtp_cleanupMenu();

/** Function used to stop RTP input display
*
*   @param[in] which Screen to stop video on
*/
void rtp_stopVideo(int which);

int rtp_playURL(int which, char *value, char *description, char *thumbnail);

int rtp_startNextChannel(int direction, void* pArg);

int rtp_setChannel(int channel, void* pArg);

void rtp_getPlaylist(int which);

int rtp_showEPG(int which, playControlSetupFunction pSetup);

/** Returns internal channel name of specified URL
 *  @param[in] url Stream url
 *  @return CHANNEL_CUSTOM if stream not found in list
 */
int rtp_getChannelNumber(const char *url);

/** Cleans up previously acquired IPTV EPG
 *  Should be called after changing EPG url
 */
void rtp_cleanupEPG();

/** Cleans IPTV channel list
 *  Should be called after changing IPTV playlist url
 *  @param[in] which Should always be screenMain
 */
void rtp_cleanupPlaylist(int which);

#ifdef ENABLE_PVR
/** Record current or last played RTP stream
 *  @return pvr_record return value
 */
int rtp_recordNow();
#endif

#endif /* __RTP_H      Do not add any thing below this line */
