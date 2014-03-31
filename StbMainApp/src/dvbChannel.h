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
#include "interface.h"
#include "list.h"

#ifdef ENABLE_DVB
/*******************
* EXPORTED DATA    *
********************/


typedef struct {
	struct list_head	orderNoneHead;
	struct list_head	orderSortHead;


    uint32_t viewedCount;
    uint32_t		totalCount;
    serviceSort_t	sortOrderType;

//	uint32_t		initialized;
} dvb_channels_t;

typedef struct {
	EIT_common_t	common;
	EIT_service_t	*service;
	uint16_t		audio_track;
	/* First EPG event which fit to current timeline.
	Updated on each call to offair_initEPGRecordMenu. */
	list_element_t	*first_event;
	uint16_t		visible;

	//lists
	struct list_head	orderNone;
	struct list_head	orderSort;
} service_index_t;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/
service_index_t *dvbChannel_getServiceIndex(uint32_t id);
int32_t dvbChannel_addCommon(EIT_common_t *common, uint16_t audio_track);
service_index_t *dvbChannel_findServiceLimit(EIT_common_t *header, uint32_t searchCount);

struct list_head *dvbChannel_getSortList(void);
int32_t dvbChannel_hasSchedule(uint32_t serviceNumber);
int32_t dvbChannel_writeOrderConfig(void);
int32_t dvbChannel_sort(serviceSort_t sortType);
int32_t dvbChannel_initServices(void);
//int32_t dvbChannel_swapServices(uint32_t first, uint32_t second);

int32_t dvbChannel_hasSchedule( uint32_t channelNumber );

EIT_service_t *dvbChannel_getService(uint32_t id);
int32_t dvbChannel_getServiceId(EIT_common_t *header);
int32_t dvbChannel_getIndex(EIT_service_t *service);
int32_t dvbChannel_getCount(void);
//int32_t dvbChannel_invalidateServicess(void);
int32_t dvbChannel_hasAnyEPG(void);

#endif // ENABLE_DVB
#endif // DVBCHANNEL_H
