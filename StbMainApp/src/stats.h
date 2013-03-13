#ifndef __STATS_H
#define __STATS_H

/*
 stats.h

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

#ifdef ENABLE_STATS

#include <time.h>
#include "interface.h"

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define STATS_FILE     CONFIG_DIR "/stats.txt"
#define STATS_TMP_FILE CONFIG_DIR "/stats_today.txt"
#define STATS_RESOLUTION      (60)
#define STATS_SAMPLE_COUNT    (24*60*60 / STATS_RESOLUTION)
#define STATS_UPDATE_INTERVAL (1000*STATS_RESOLUTION)

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef struct __statsInfo_t
{
	time_t startTime;
	time_t endTime;
	time_t today;
	int watched[STATS_SAMPLE_COUNT];
} statsInfo_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

extern statsInfo_t statsInfo;
extern interfaceListMenu_t StatsMenu;
extern time_t stats_lastTime;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

int stats_buildMenu(interfaceMenu_t* pParent);
int stats_init(void);
int stats_save(void);
int stats_load(void);

#endif //ENABLE_STATS
#endif //__STATS_H
