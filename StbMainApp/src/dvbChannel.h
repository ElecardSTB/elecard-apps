#ifndef DVBCHANNEL_H
#define DVBCHANNEL_H
/*
 dvbChannel.h

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
#include "list.h"
#include "app_info.h"

/******************************************************************
* EXPORTED MACROS                              [for headers only] *
*******************************************************************/
#define dvbChannel_getServiceIndex(id) dvbChannel_getServiceIndexVisible(id, 1)

/******************************************************************
* EXPORTED TYPEDEFS                            [for headers only] *
*******************************************************************/
typedef struct {
    uint8_t  serviceType;
	uint16_t network_id;
} bouquet_data_t;

typedef struct {
	uint16_t audio_track;
	uint16_t visible;
	uint16_t parent_control;
	char     channelsName[MENU_ENTRY_INFO_LENGTH];
} service_index_data_t;

typedef struct {
	EIT_common_t         common;
	bouquet_data_t       bouquet_data;
	uint8_t              flag;
	service_index_data_t data;
	EIT_service_t       *service;

	/* First EPG event which fit to current timeline.
	Updated on each call to offair_initEPGRecordMenu. */
	list_element_t      *first_event;

	//lists
	struct list_head     orderNone;
} service_index_t;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef ENABLE_DVB
int32_t dvbChannel_addService(EIT_service_t *service, service_index_data_t *data, uint8_t flag);
int32_t dvbChannel_remove(service_index_t *srvIdx);
service_index_t *dvbChannel_getServiceIndexVisible(uint32_t id, uint32_t visible);
service_index_t *dvbChannel_findServiceCommon(EIT_common_t *header);
int dvbChannel_findNumberService(service_index_t *srv_id);
int32_t dvbChannel_setName(service_index_t *srvIdx, const char *name);

struct list_head *dvbChannel_getSortList(void);
int32_t dvbChannel_hasSchedule(uint32_t serviceNumber);
int32_t dvbChannel_sort(serviceSort_t sortType);
int32_t dvbChannel_swapServices(uint32_t first, uint32_t second);

int32_t dvbChannel_hasSchedule(uint32_t channelNumber);

EIT_service_t *dvbChannel_getService(uint32_t id);
int32_t dvbChannel_getServiceId(EIT_common_t *header);
int32_t dvbChannel_getIndex(EIT_service_t *service);
int32_t dvbChannel_getCount(void);
//int32_t dvbChannel_invalidateServicess(void);
int32_t dvbChannel_hasAnyEPG(void);
int32_t dvbChannel_applyUpdates(void);


int32_t dvbChannel_registerCallbackOnChange(changeCallback_t *pCallback, void *pArg);
int32_t dvbChannel_changed(void);

int32_t dvbChannel_load(void);
int32_t dvbChannel_save(void);

void dvbChannel_init(void);
void dvbChannel_terminate(void);
#endif // ENABLE_DVB
#endif // DVBCHANNEL_H
