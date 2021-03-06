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
#include "helper.h"

#include <platform.h>
//#include <sdp.h>
//#include <service.h>

/***********************************************
* EXPORTED MACROS                              *
************************************************/
#define MAX_ANALOG_CHANNELS 128

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/
typedef struct {
	uint32_t frequency;
	uint16_t customNumber;
	uint16_t visible;
	uint16_t parent_control;
	char customCaption[MENU_ENTRY_INFO_LENGTH];
	char sysEncode[16];
	char audio[16];
	struct list_head	channelList;
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


/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/
#ifdef ENABLE_ANALOGTV

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
int32_t analogtv_serviceScan (interfaceMenu_t *pMenu, void* pArg);

/**  @ingroup analogtv
 *   @brief Function exports current channel list to specified file
 *   @param[in]  filename  Channel file name
 */

int32_t analogtv_hasTuner(void);

void analogtv_stop(void);
void analogtv_load(void);

int32_t analogtv_setChannelsData(char *, int32_t, int32_t);
int32_t analogtv_applyUpdates(void);
int32_t analogtv_findOnFrequency(uint32_t frequency);
int32_t analogtv_swapService(int32_t, int32_t);
struct list_head *analogtv_getChannelList(void);
const char *analogtv_getServiceName(uint32_t index);
uint32_t analogtv_getChannelCount(int32_t visible);

int32_t analogtv_clearServiceList(interfaceMenu_t * pMenu, void *pArg);
int32_t analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg);
int32_t analogtv_changeAnalogLowFreq(interfaceMenu_t * pMenu, void *pArg);
int32_t analogtv_changeAnalogHighFreq(interfaceMenu_t * pMenu, void *pArg);
int32_t analogtv_getServiceDescription(uint32_t index, char *buf, size_t size);
int32_t  menu_entryIsAnalogTv(interfaceMenu_t *pMenu, int32_t index);
void analogtv_addChannelsToMenu(interfaceMenu_t *pMenu, int32_t startIndex);

int32_t analogtv_registerCallbackOnChange(changeCallback_t *pCallback, void *pArg);

//Inputs API (in common this not relate to analogtv, but now it here)
int32_t     extInput_set(const char *name, char *descr);
int32_t     extInput_getSelectedId(void);
int32_t     extInput_disble(void);
listHead_t *extInput_getList(void);

int32_t     extInput_init(void);
int32_t     extInput_release(void);

int32_t     analogNames_isExist(void);
int32_t     analogNames_download(void);
int32_t     analogNames_load(void);
listHead_t *analogNames_getList(void);

#else /* ENABLE_ANALOGTV */

static inline void analogtv_load(void) {}
static inline int32_t analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg) { return 0; }
static inline const char *analogtv_getServiceName(uint32_t index) { return NULL; }
static inline uint32_t analogtv_getChannelCount(int32_t visible) { return 0; }
static inline void analogtv_addChannelsToMenu(interfaceMenu_t *pMenu, int32_t startIndex) { return ; }
static inline int32_t analogtv_getServiceDescription(uint32_t index, char *buf, size_t size) { buf[0] = 0; return -1; }
static inline int32_t analogtv_registerCallbackOnChange(changeCallback_t *pCallback, void *pArg) { return 0; }
static inline struct list_head *analogtv_getChannelList(void) { return NULL; }
static inline int32_t analogtv_findOnFrequency(uint32_t frequency) { return -1; }
static inline int32_t analogtv_setChannelsData(char *Name, int32_t visible,  int32_t index) { return -1; }
static inline int32_t analogtv_swapService(int32_t first, int32_t second) { return -1; }
static inline int32_t analogtv_applyUpdates(void) { return -1; }
static inline int32_t analogtv_hasTuner(void) { return 0; }
static inline int32_t analogNames_isExist(void) { return 0; }
static inline int32_t analogNames_download(void) { return -1; }
static inline int32_t analogNames_load(void) { return -1; }
static inline listHead_t *analogNames_getList(void) { return NULL; }

static inline int32_t menu_entryIsAnalogTv(interfaceMenu_t *pMenu, int32_t index) { return 0; }

static inline int32_t extInput_set(const char *name, char *descr) { return 0; }
static inline int32_t extInput_getSelectedId(void) { return -1; }
static inline int32_t extInput_disble(void) { return 0; }
static inline listHead_t *extInput_getList(void) { return NULL; }
#endif /* ENABLE_ANALOGTV */

#endif /* __ANALOGTV_H__      Do not add any thing below this line */
