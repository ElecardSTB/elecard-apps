#if !defined(__OFF_AIR_H)
#define __OFF_AIR_H

/*
 off_air.h

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

#include "dvb.h"
#include "interface.h"
#include "dvbChannel.h"

#include <time.h>
#include <stdint.h>

/*******************
* EXPORTED MACROS  *
********************/

//#define DVB_FAVORITES

#define interfaceMenuEPG (3)

#define CHANNEL_INFO_SET(screen, channel) ((void*)(intptr_t)((screen << 16) | (channel)))
#define CHANNEL_INFO_GET_SCREEN(info)     ((int)(((intptr_t)info >> 16) & 0xFFFF))
#define CHANNEL_INFO_GET_CHANNEL(info)    ((int)( (intptr_t)info        & 0xFFFF))
#define MAX_MEMORIZED_SERVICES MENU_MAX_ENTRIES

/* EPG Record menu layout */

#define ERM_CHANNEL_NAME_LENGTH (150)
/* Current channel is always visible and not counted */
#define ERM_VISIBLE_CHANNELS (5)
#define ERM_DISPLAYING_HOURS (3)
#define ERM_TIMESTAMP_WIDTH  (3)
#define ERM_ICON_SIZE        (34)

/* DVB play control layout */
#define DVBPC_STATUS_ICON_SIZE 22

/* EPG Record menu colors */

#define ERM_TIMELINE_RED   121
#define ERM_TIMELINE_GREEN 121
#define ERM_TIMELINE_BLUE  121
#define ERM_TIMELINE_ALPHA INTERFACE_BACKGROUND_ALPHA

#define ERM_TIMESTAMP_RED   196
#define ERM_TIMESTAMP_GREEN 196
#define ERM_TIMESTAMP_BLUE  196
#define ERM_TIMESTAMP_ALPHA 255

#define ERM_CURRENT_TITLE_RED   121
#define ERM_CURRENT_TITLE_GREEN 121
#define ERM_CURRENT_TITLE_BLUE  121
#define ERM_CURRENT_TITLE_ALPHA INTERFACE_BACKGROUND_ALPHA

#define ERM_TITLE_RED   160
#define ERM_TITLE_GREEN 160
#define ERM_TITLE_BLUE  160
#define ERM_TITLE_ALPHA INTERFACE_BACKGROUND_ALPHA

#define ERM_CELL_START_RED   40
#define ERM_CELL_START_GREEN 40
#define ERM_CELL_START_BLUE  40
#define ERM_CELL_START_ALPHA INTERFACE_BACKGROUND_ALPHA

#define ERM_HIGHLIGHTED_CELL_RED   84
#define ERM_HIGHLIGHTED_CELL_GREEN 239
#define ERM_HIGHLIGHTED_CELL_BLUE  58
#define ERM_HIGHLIGHTED_CELL_ALPHA INTERFACE_BACKGROUND_ALPHA

#define ERM_CELL_RED   69
#define ERM_CELL_GREEN 117
#define ERM_CELL_BLUE  165
#define ERM_CELL_ALPHA INTERFACE_BACKGROUND_ALPHA

#define ERM_RECORD_RED   255
#define ERM_RECORD_GREEN 24
#define ERM_RECORD_BLUE  24
#define ERM_RECORD_ALPHA INTERFACE_BACKGROUND_ALPHA

#define INFO_TIMER_PERIOD (100)

/*********************
* EXPORTED TYPEDEFS  *
**********************/

typedef struct
{
	interfaceMenu_t baseMenu;

	int currentService;
	int highlightedService;
	int serviceOffset;

	time_t curOffset;
	time_t minOffset;
	time_t maxOffset;

	int displayingHours;
	int timelineX;
	int timelineWidth;
	float pps; //pixel per second

	list_element_t *highlightedEvent;
} interfaceEpgMenu_t;

/*******************
* EXPORTED DATA    *
********************/

#ifdef ENABLE_DVB
/* Service index in offair_service used as channel number */
extern interfaceListMenu_t DVBTMenu;
#endif

extern interfaceEpgMenu_t  EPGRecordMenu;
extern interfaceColor_t    genre_colors[];

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

#ifdef ENABLE_DVB
void offair_sortSchedule(void);

char *offair_getChannelNumberPrefix(uint32_t id);

int offair_tunerPresent(void);

/**
*   @brief Function used to build the DVB T menu data structures
*   This function MUST be called once on application start
*/
void offair_buildDVBTMenu(interfaceMenu_t *pParent);

/**
*   @brief Function used to obtain current tuner
*   Any tuner activity is stopped on this function call
*   @retval int Current selected tuner
*/
uint32_t offair_getTuner(void);

/**
*   @brief Function used to perform a channel change
*   @param pArg     I       Channel to change to (void* is cast to int)
*   @retval int Zero on success
*/
int offair_channelChange(interfaceMenu_t* pMenu, void* pArg);

/**
*   @brief Function used to start DVB T output
*   @param[in] which deprecated
*/
void offair_startVideo(int which);

/**
*   @brief Function used to stop DVB T output
*   @param[in] which deprecated
*   @param[in] reset If set resets current frequency to force tuner retune
*/
void offair_stopVideo(int which, int reset);

int  offair_serviceScan(interfaceMenu_t *pMenu, void* pArg);

int  offair_frequencyScan(interfaceMenu_t *pMenu, void* pArg);

int  offair_frequencyMonitor(interfaceMenu_t *pMenu, void* pArg);

int  offair_wizardStart(interfaceMenu_t *pMenu, void* pArg);

void offair_clearServiceList(int permanent);

/*
DVB_URL    ::= dvb://[TS_ID@]FREQUENCY[:SERVICE_ID]/[PARAM]

PARAMS     ::= ?PARAM{&PARAM}
PARAM      ::= vt=VIDEO_TYPE | vp=VIDEO_PID | at=AUDIO_TYPE | ap=AUDIO_PID | pcr=PCR | tp=TELETXT_PID
AUDIO_TYPE ::= MPEG | AAC | AC3
VIDEO_TYPE ::= MPEG2 | MPEGTS | MPEG4 | H264
FREQUENCY  ::= ulong
TS_ID      ::= ushort
PCR        ::= ushort
SERVICE_ID ::= ushort
VIDEO_PID  ::= ushort
AUDIO_PID  ::= ushort
TELETXT_PID::= ushort
*/

#ifdef DVB_FAVORITES
void offair_playURL(char* URL, int which);
#endif

void offair_cleanupMenu(void);

int  offair_enterDVBTMenu(interfaceMenu_t *pMenu, void* pArg);

/* Get offair service index (LCN) by dvb channel index */
int  offair_getIndex(int index);

static inline int offair_getServiceIndex(EIT_service_t *service)
{
	return dvbChannel_getIndex(service);
}

static inline int service_isRadio(const EIT_service_t *service)
{
	return service->service_descriptor.service_type == 2;
}

static inline int service_thumbnail(const EIT_service_t *service)
{
	return service_isRadio(service) ? thumbnail_radio : thumbnail_channels;
}

// Returns -1 if no tuner supporting service media type is found, otherwise set adapter
int32_t offair_findCapableTuner(EIT_service_t *service, uint32_t *adapter);

int  offair_getCurrentServiceIndex(int which);

static inline EIT_service_t* offair_getService(int index)
{
	return dvbChannel_getService(index);
}

static inline const char *offair_getServiceName(int index)
{
	return dvb_getServiceName(offair_getService(index));
}

int32_t offair_updateChannelStatus(void);

void offair_fillDVBTMenu(void);

int  offair_frequencyMonitor(interfaceMenu_t *pMenu, void* pArg);

int  offair_checkForUpdates(void);

int  offair_setChannel(int channel, void* pArg);

// DVB or Analog channel number suitable for offair_setChannel()
int  offair_getCurrentChannel(void);

int  offair_setPreviousChannel(int previousChannel);

int offair_play_callback(interfacePlayControlButton_t button, void *pArg);
void offair_displayPlayControl(void);
#else /* ENABLE_DVB */
#define offair_getChannelNumberPrefix(id)	""
#define offair_fillDVBTMenu()
#endif /* ENABLE_DVB */

int  offair_getLocalEventTime(EIT_event_t *event, struct tm *local_tm, time_t *local_time_t);

time_t offair_getEventDuration(EIT_event_t *event);

int offair_getEventTimes(EIT_event_t *event, time_t *p_start, time_t *p_end);

/** Find schedule event which runs at specified time.
 *  @param[in]  schedule     EPG schedule with list of EIT_event_t elements
 *  @param[in]  now
 *  @param[out] pevent       Pointer to found event will be stored at this address
 *  @param[out] event_start  Value stored will be valid only on successfull call
 *  @param[out] event_length Value stored will be valid only on successfull call
 *  @param[out] start        Can be null
 *  @return 0 if event found, 1 if no such event, *pevent will be set to NULL
 */
int offair_findCurrentEvent(list_element_t *schedule, time_t now, EIT_event_t **pevent, time_t *event_start, time_t *event_length, struct tm *start);

#ifdef ENABLE_PVR
int  offair_initEPGRecordMenu(interfaceMenu_t *pMenu, void *pArg);

int  offair_startPvrVideo(int which);
#endif

void offair_activateChannelMenu(void);

int  offair_getCurrentChannel(void);

int  offair_setPreviousChannel(int previousChannel);

#endif /* __OFF_AIR_H      Do not add any thing below this line */
