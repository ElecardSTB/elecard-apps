/*
 analogtv.h

Copyright (C) 2013  Elecard Devices

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
/** @file analogtv.h Analog TV playback backend
 */

#if !defined(__ANALOGTV_H__)
#define __ANALOGTV_H__
/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "defines.h"

#include "app_info.h"
#include "interface.h"

#include <platform.h>
//#include <sdp.h>
//#include <service.h>

#ifdef ENABLE_ANALOGTV

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define MAX_ANALOG_CHANNELS		128

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef struct {
	uint32_t frequency;
	uint16_t customNumber;
	uint16_t visible;
	uint16_t parent_control;
	char customCaption[256];
	char sysEncode[16];
	char audio[16];
} analog_service_t;

typedef enum {
	TV_SYSTEM_PAL = 0,
	TV_SYSTEM_SECAM,
	TV_SYSTEM_NTSC,
} analogtv_deliverySystem;

typedef enum {
	TV_AUDIO_SIF = 0,
	TV_AUDIO_AM,
	TV_AUDIO_FM1,
	TV_AUDIO_FM2,
} analogtv_audioDemodMode;

/***********************************************
* EXPORTED DATA                                *
************************************************/
extern int analogtv_service_count;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/

/**  @ingroup analogtv
 *   @brief Function used to initialise the Analog TV component
 *
 *   Must be called before initiating offair
 */
void analogtv_init(void);

/**  @ingroup analogtv
 *   @brief Function used to terminate the Analog TV component
 *
 *   Must be called after terminating offair
 */
void analogtv_terminate(void);

/**  @ingroup analogtv
 *   @brief Function used to scan for Analog TV channels
 *
 *   @param[in]  pFunction Callback function
 *
 *   @return 0 on success
 */
int analogtv_serviceScan (interfaceMenu_t *pMenu, void* pArg);

/**  @ingroup analogtv
 *   @brief Function exports current channel list to specified file
 *   @param[in]  filename  Channel file name
 */

void analogtv_stop();
int32_t analogtv_saveConfigFile(void);
int32_t analogtv_parseConfigFile(int visible);

int analogtv_clearServiceList(interfaceMenu_t * pMenu, void *pArg);

int analogtv_changeAnalogLowFreq(interfaceMenu_t * pMenu, void *pArg);
int analogtv_changeAnalogHighFreq(interfaceMenu_t * pMenu, void *pArg);

const char *analogtv_getServiceName(uint32_t index);
int analogtv_getServiceDescription(uint32_t index, char *buf, size_t size);

int analogtv_playControlProcessCommand(pinterfaceCommandEvent_t cmd, void *pArg);

int32_t analogtv_updateName(uint32_t chanIndex, char* str);
void analogtv_removeServiceList(int permanent);

void analogtv_addChannelsToMenu(interfaceMenu_t *pMenu, int startIndex);
int  menu_entryIsAnalogTv(interfaceMenu_t *pMenu, int index);

void analogtv_initMenu(interfaceMenu_t *pParent);
uint32_t analogtv_getChannelCount(int visible);

int analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg);

int32_t analogtv_fillFoundServList(void);
int32_t analogtv_updateFoundServiceFile(void);
int32_t analogtv_findOnFrequency(uint32_t frequency);
void analogtv_swapService(int x, int y);
void analogtv_removeService(int index);


int32_t analogtv_hasTuner(void);
#else /* ENABLE_ANALOGTV */

static inline int32_t analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg) { return 0; }
const char *analogtv_getServiceName(uint32_t index) { return NULL; }
static inline uint32_t analogtv_getChannelCount(void) { return 0; }
static inline void analogtv_addChannelsToMenu(interfaceMenu_t *pMenu, int startIndex) { return ; }
static inline int32_t analogtv_updateName(uint32_t chanIndex, char* str) { return 0; }
static inline int32_t analogtv_getServiceDescription(uint32_t index, char *buf, size_t size) { buf[0] = 0; return -1; }
static inline int32_t menu_entryIsAnalogTv(interfaceMenu_t *pMenu, int index) { return 0; }

static inline int32_t analogtv_hasTuner(void) { return 0; }
#endif /* ENABLE_ANALOGTV */

#endif /* __ANALOGTV_H__      Do not add any thing below this line */
