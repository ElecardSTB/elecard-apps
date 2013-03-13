#if !defined(__RUTUBE_H)
#define __RUTUBE_H

/*
 rutube.h

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

#ifdef ENABLE_RUTUBE

#include "interface.h"

/*******************
* EXPORTED MACROS  *
********************/

#define RUTUBE_XML "/tmp/rutube_export.txt"

/*********************
* EXPORTED TYPEDEFS  *
**********************/
/** The structure of an element of asset list. There is the list of movie categories,
* keeping in RutubeCategories. Each category element has pArg pointer to the first asset element in
* current category. Each asset element has pointer to the next asset element - *nextInCategory.
*/
typedef struct __rutube_asset_t
{
	char *title;
	char *url;
	char *thumbnail;
	char *description;

	struct __rutube_asset_t *nextInCategory; /**< Next asset in it's category */
} rutube_asset_t;

/*******************
* EXPORTED DATA    *
********************/

extern interfaceListMenu_t RutubeCategories;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

#ifdef __cplusplus
extern "C" {
#endif
	void rutube_buildMenu(interfaceMenu_t* pParent);

	/** Prev/next Rutube movie
	  *  @param[in] pArg Ignored
	  *  @return 0 if successfully changed
	  *  @return 1 if there is no prev/next movie in menu
	 */
	int  rutube_startNextChannel(int direction, void* pArg);

	/** Set Rutube movie using currently downloaded video list
	 *  @param[in] pArg Ignored
	 *  @return 0 if successfully changed
	 */
	int  rutube_setChannel(int channel, void* pArg);

	/** Get permanent link to currently playing Rutube stream
	 * @return Statically allocated string, empty if Rutube is not active
	 */
	char *rutube_getCurrentURL(void);

	void rutube_cleanupMenu(void);
#ifdef __cplusplus
}
#endif

#endif // ENABLE_RUTUBE

#endif /* __RUTUBE_H      Do not add any thing below this line */
