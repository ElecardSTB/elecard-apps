#if !defined(__YOUTUBE_H)
#define __YOUTUBE_H

/*
 youtube.h

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

#include "defines.h"

#ifdef ENABLE_YOUTUBE

#include "interface.h"

/***********************************************
* EXPORTED DATA                                *
************************************************/

extern interfaceListMenu_t YoutubeMenu;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif
	void youtube_buildMenu(interfaceMenu_t* pParent);

	/** Prev/next Youtube channel
	 *  @param[in] pArg Ignored
	 *  @return 0 if successfully changed
	 */
	int  youtube_startNextChannel(int direction, void* pArg);

	/** Set Youtube channel using currently downloaded video list
	 *  @param[in] pArg Ignored
	 *  @return 0 if successfully changed
	 */
	int  youtube_setChannel(int channel, void* pArg);

	/** Get permanent link to currently playing YouTube stream
	 * @return Statically allocated string, empty if YouTube is not active
	 */
	char *youtube_getCurrentURL();

	/** Play YouTube stream, predefined in appControlInfo.mediaInfo.lastFile
	 * @return 0 on success
	 */
	int youtube_streamStart();
#ifdef __cplusplus
}
#endif

#endif // ENABLE_YOUTUBE

#endif /* __YOUTUBE_H      Do not add any thing below this line */
