
/*
 interface.c

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
#include "dvbChannel.h"
#include "playlist_editor.h"

#include "debug.h"
#include "off_air.h"
#include "bouquet.h"

/***********************************************
* LOCAL MACROS                                 *
************************************************/

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
#ifdef ENABLE_DVB
dvb_channels_t g_dvb_channels = {
    .orderNoneHead	= LIST_HEAD_INIT(g_dvb_channels.orderNoneHead),

    .viewedCount	= 0,
    .totalCount		= 0,
    .sortOrderType	= serviceSortNone,
//	.initialized	= 0,
};
#endif // ENABLE_DVB

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/
#ifdef ENABLE_DVB
static int32_t dvbChannel_isServiceEnabled(EIT_service_t *service)
{
	if(service == NULL) {
		return 0;
	}
	if((appControlInfo.offairInfo.dvbShowScrambled != 0) || !dvb_getScrambled(service)) {
		return 1;
	}
	return 0;
}

service_index_t *dvbChannel_findServiceLimit(EIT_common_t *header, uint32_t searchCount)
{
	struct list_head *pos;
	uint32_t i = 0;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		if(i >= searchCount) {
			break;
		}
		if(memcmp(&(srv->common), header, sizeof(EIT_common_t)) == 0) {
			return srv;
		}
		i++;
	}
	return NULL;
}

int dvbChannel_findNumberService(service_index_t *srv_id)
{
	struct list_head *pos;
	uint32_t i = 0;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		if (srv == NULL)
			break;
		if ( srv == srv_id ){
			return i;
		}
		i++;
	}
	return -1;
}

int32_t dvbChannel_getServiceId(EIT_common_t *header)
{
	struct list_head *pos;
	int32_t i = 0;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		if(!srv->visible) {
			continue;
		}
		if(memcmp(&(srv->common), header, sizeof(EIT_common_t)) == 0) {
			return i;
		}
		i++;
	}
	return -1;
}

int32_t dvbChannel_getIndex(EIT_service_t *service)
{
	struct list_head *pos;
	int32_t i = 0;

	if(service == NULL) {
		return -2;
	}

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		if(!srv->visible) {
			continue;
		}
		if(srv && (srv->service == service)) {
			return i;
		}
		i++;
	}

	return -1;
}

service_index_t *dvbChannel_getServiceIndex(uint32_t id)
{
	struct list_head *pos;
	uint32_t i = 0;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		if(!srv->visible) {
			continue;
		}
		if(i == id) {
			return srv;
		}
		i++;
	}
	return NULL;
}

service_index_t *dvbChannel_getServiceIndexnoVisible(uint32_t id)
{
	struct list_head *pos;
	uint32_t i = 0;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		
		if(i == id) {
			return srv;
		}
		i++;
	}
	return NULL;
}

EIT_service_t *dvbChannel_getService(uint32_t id)
{
	service_index_t *srv = dvbChannel_getServiceIndex(id);
	return srv ? srv->service : NULL;
}

int32_t dvbChannel_hasSchedule(uint32_t serviceNumber)
{
	EIT_service_t *service = dvbChannel_getService(serviceNumber);
	if(service == NULL) {
		return 0;
	}
	return ((service->schedule != NULL) && dvb_hasMedia(service));
}

int32_t dvbChannel_swapServices(uint32_t first, uint32_t second)
{
	if (first == second)
		return 0;
	service_index_t *srvIdx_first;
	service_index_t *srvIdx_second;
	struct list_head *srvIdx_beforeFirst;
	struct list_head *srvIdx_beforeSecond;

	srvIdx_first = dvbChannel_getServiceIndexnoVisible(first);
	srvIdx_second = dvbChannel_getServiceIndexnoVisible(second);

	if(!srvIdx_first || !srvIdx_second) {
		return -1;
	}
	srvIdx_beforeFirst = srvIdx_first->orderNone.prev;
	srvIdx_beforeSecond = srvIdx_second->orderNone.prev;

	if(&srvIdx_first->orderNone != srvIdx_beforeSecond) {
		list_del(&srvIdx_first->orderNone);
		list_add(&srvIdx_first->orderNone, srvIdx_beforeSecond);
	}
	list_del(&srvIdx_second->orderNone);
	list_add(&srvIdx_second->orderNone, srvIdx_beforeFirst);

	return 0;
}

int32_t dvbChannel_hasAnyEPG(void)
{
	struct list_head *pos;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if((srvIdx == NULL) || (srvIdx->service == NULL) || !srvIdx->visible) {
			continue;
		}
		if((srvIdx->service->schedule != NULL) && dvb_hasMedia(srvIdx->service)) {
			return 1;
		}
	}

	return 0;
}

//dvb_getNumberOfServices
int32_t dvbChannel_getCount(void)
{
	return g_dvb_channels.viewedCount;
}

service_index_t *dvbChannel_add(void)
{
	service_index_t *new = malloc(sizeof(service_index_t));
	if(new == NULL) {
		eprintf("%s()[%d]: Error allocating memory!\n", __func__, __LINE__);
		return NULL;
	}
	memset(new, 0, sizeof(service_index_t));
	list_add_tail(&(new->orderNone), &g_dvb_channels.orderNoneHead);
	g_dvb_channels.totalCount++;
	g_dvb_channels.viewedCount++;

	return new;
}

int32_t dvbChannel_addCommon(EIT_common_t *common, uint16_t audio_track, uint16_t visible)
{
	service_index_t *new = dvbChannel_add();
	if(new) {
		new->common = *common;
		new->audio_track = audio_track;
		new->visible = visible;
	} else {
		eprintf("%s()[%d]: Cant add channel with common!\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

int32_t dvbChannel_addBouquetData(EIT_common_t *common, bouquet_data_t *bouquet_data, uint16_t visible)
{
    service_index_t *new = dvbChannel_add();
    if(new) {
        new->common = *common;
        new->bouquet_data = *bouquet_data;
		new->visible = visible;
    } else {
        eprintf("%s()[%d]: Cant add channel with common!\n", __func__, __LINE__);
        return -1;
    }
    return 0;
}

static int32_t dvbChannel_addService(EIT_service_t *service, uint16_t visible)
{
	service_index_t *new = dvbChannel_add();
	if(new) {
		new->service = service;
		new->common = service->common;
		new->visible = visible;

	} else {
		eprintf("%s()[%d]: Cant add channel with common!\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int32_t dvbChannel_remove(service_index_t *srvIdx)
{
	list_del(&srvIdx->orderNone);
	g_dvb_channels.totalCount--;
//	if(!list_empty(&srvIdx->orderSort)) {
//		list_del(&srvIdx->orderSort);
		g_dvb_channels.viewedCount--;
//	}
	free(srvIdx);


	return 0;
}

static int32_t dvbChannel_readOrderConfig(void)
{
	char buf[BUFFER_SIZE];
	FILE* fd;

	fd = fopen(OFFAIR_SERVICES_FILENAME, "r");
	if(fd == NULL) {
		dprintf("%s: Failed to open '%s'\n", __FUNCTION__, OFFAIR_SERVICES_FILENAME);
		return -1;
	}
	while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		uint32_t media_id = 0;
		uint16_t service_id = 0;
		uint16_t transport_stream_id = 0;
		uint16_t audio_track = 0;
		uint16_t visible = 0;
		int i;

		if ( sscanf(buf, "service %d media_id %u service_id %hu transport_stream_id %hu audio_track %hu visible %hu\n",
			&i, &media_id, &service_id, &transport_stream_id, &audio_track, &visible) >= 5)
		{
			EIT_common_t common;

			if(i < 0) {
				continue;
			}

			common.media_id = media_id;
			common.service_id = service_id;
			common.transport_stream_id = transport_stream_id;

			dvbChannel_addCommon(&common, audio_track, visible);
		}
	}
	fclose(fd);
	dprintf("%s: imported %d services\n", __FUNCTION__, dvbChannel_getCount());

	return 0;
}

int32_t dvbChannel_writeOrderConfig(void)
{
	struct list_head *pos;
	FILE *f;
	uint32_t i = 0;

	if(list_empty(&g_dvb_channels.orderNoneHead)) {
		return -1;
	}

	f = fopen(OFFAIR_SERVICES_FILENAME, "w");

	if(f == NULL) {
		eprintf("%s: Failed to open '%s': %m\n", __FUNCTION__, OFFAIR_SERVICES_FILENAME);
		return -1;
	}

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(srvIdx->common.media_id || srvIdx->common.service_id || srvIdx->common.transport_stream_id) {
			fprintf(f, "service %d media_id %u service_id %hu transport_stream_id %hu audio_track %hu visible %hu\n", i,
				srvIdx->common.media_id,
				srvIdx->common.service_id,
				srvIdx->common.transport_stream_id,
				srvIdx->audio_track,
				srvIdx->visible);
			i++;
		}
	}

	fclose(f);

	return 0;
}

static int32_t dvbChannel_invalidateServices(void)
{
	struct list_head *pos;
	//invalidate service pointers
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		srvIdx->service = NULL;
	}
	return 0;
}

static void dvbChannel_sortOrderEmpty(void)
{
	struct list_head	*pos;
	struct list_head	*n;

	//empty orderSort list
	list_for_each_safe(pos, n, &g_dvb_channels.orderNoneHead) {
		list_del_init(pos);
		g_dvb_channels.viewedCount--;
	}
	if(!list_empty(&g_dvb_channels.orderNoneHead) || (g_dvb_channels.viewedCount != 0)) {
		eprintf("%s()[%d]: Something wrong viewedCount=%d!!!!!!\n", __func__, __LINE__, g_dvb_channels.viewedCount);
		INIT_LIST_HEAD(&g_dvb_channels.orderNoneHead);
		g_dvb_channels.viewedCount = 0;
	}
}

static void dvbChannel_sortOrderRecheck(void)
{
	struct list_head	*pos;
	g_dvb_channels.viewedCount = 0;
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(dvbChannel_isServiceEnabled(srvIdx->service)) {
			srvIdx->visible = 1;
			g_dvb_channels.viewedCount++;
		} else {
			srvIdx->visible = 0;
		}
	}
}

static void dvbChannel_sortOrderRecheck_2(void)
{
	struct list_head	*pos;
	g_dvb_channels.viewedCount = 0;
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(dvbChannel_isServiceEnabled(srvIdx->service)) {
			if (srvIdx->visible == 1)
			g_dvb_channels.viewedCount++;
		} else {
			srvIdx->visible = 0;
		}
	}
}

static int32_t dvbChannel_update(void)
{
	if (push_playlist()) {
		dvbChannel_sortOrderRecheck_2();
		dvbChannel_writeOrderConfig();
		return 0;
	}
	playlist_editor_cleanup();
	uint32_t old_count;
	list_element_t		*service_element;
    struct list_head    *pos;
    struct list_head    *n;
	bouquet_downloadFileWithServices(BOUQUET_FULL_LIST);

	if (bouquet_file()) {
		load_bouquets(); // in g_dvb_channels
        // bouquets list compare with dvb_services
		if ( !bouquets_compare(&dvb_services) ){
			free_services(&dvb_services);
			load_lamedb(&dvb_services);
		}
    } else {
		if(list_empty(&g_dvb_channels.orderNoneHead)) {
			dvbChannel_readOrderConfig();
		} else {
			dvbChannel_invalidateServices();
		}
    }
    //  dvbChannel_getAudioTrack(); // from offair
	old_count = g_dvb_channels.totalCount;
	service_element = dvb_services;
    while (service_element != NULL) {
		service_index_t *p_srvIdx;
		EIT_service_t *curService = (EIT_service_t *)service_element->data;
/*        if((curService == NULL)) {
			continue;
        }*/
		p_srvIdx = dvbChannel_findServiceLimit(&curService->common, old_count);
		if(p_srvIdx) {
			p_srvIdx->service = curService;
            p_srvIdx->service->original_network_id = p_srvIdx->bouquet_data.network_id;
            p_srvIdx->service->service_descriptor.service_type = p_srvIdx->bouquet_data.serviceType;
/*            p_srvIdx->service->lcn.visible_service_flag = 1;
            p_srvIdx->service->lcn.logical_channel_number = p_srvIdx->bouquet_data.channel_number;
            p_srvIdx->service->flags |= serviceFlagHasLCN;
*/		} else {
            if (bouquet_file()) {
                service_element = remove_element(&dvb_services, service_element);
                continue;
            } else {
                dvbChannel_addService(curService, 1);
            }
        }
        service_element = service_element->next;
    }
    //remove elements without service pointer
    list_for_each_safe(pos, n, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(srvIdx->service == NULL) {
			dvbChannel_remove(srvIdx);
		}
	}
	dvbChannel_sortOrderRecheck_2();
	dvbChannel_writeOrderConfig();
	return 0;
}

//int32_t serviceIdx_cmp(const void *e1, const void *e2, void *arg)
int32_t serviceIdx_cmp(const void *e1, const void *e2)
{
//	serviceSort_t sortOrderType = *((serviceSort_t *)arg);
	serviceSort_t sortOrderType = g_dvb_channels.sortOrderType;
	service_index_t *srvIndx1 = *(service_index_t **)e1;
	service_index_t *srvIndx2 = *(service_index_t **)e2;
	EIT_service_t *s1;
	EIT_service_t *s2;
	int32_t result = 0;

	if(!srvIndx1 || !srvIndx2) {
		eprintf("%s:%s()[%d]: ERROR!!!\n", __FILE__, __func__, __LINE__);
		return 0;
	}
	s1 = srvIndx1->service;
	s2 = srvIndx2->service;
	if(!s1 || !s2) {
		eprintf("%s:%s()[%d]: ERROR!!!\n", __FILE__, __func__, __LINE__);
		return 0;
	}
	if(sortOrderType == serviceSortType) {
		result = dvb_hasMediaType(s2, mediaTypeVideo) - dvb_hasMediaType(s1, mediaTypeVideo);
	} else if(sortOrderType == serviceSortFreq) {
		__u32 f1,f2;
		dvb_getServiceFrequency(s1, &f1);
		dvb_getServiceFrequency(s2, &f2);
		if(f1 > f2) {
			result = 1;
		} else if(f2 > f1) {
			result = -1;
		}
	}

	if(result == 0) {
		char *n1 = dvb_getServiceName(s1);
		char *n2 = dvb_getServiceName(s2);
		return strcasecmp(n1, n2);
	}
	return result;
}

int32_t dvbChannel_sort(serviceSort_t sortType)
{
#define MAX_DVB_CHANNELS_SORT	512

	if(sortType == g_dvb_channels.sortOrderType) {
		return 0;
	}

	dvbChannel_sortOrderEmpty();
	g_dvb_channels.sortOrderType = sortType;
	if(sortType == serviceSortNone) {
		struct list_head *pos;

		list_for_each(pos, &g_dvb_channels.orderNoneHead) {
			service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

			list_add_tail(&(srvIdx->orderNone), &g_dvb_channels.orderNoneHead);
		}
	} else {
		service_index_t *sortingBuf[MAX_DVB_CHANNELS_SORT];
		struct list_head *pos;
		uint32_t i = 0;
		uint32_t count;

		if(g_dvb_channels.totalCount > ARRAY_SIZE(sortingBuf)) {
			eprintf("%s()[%d]: Too much channels %d, sort only first %d!\n",
					__func__, __LINE__, g_dvb_channels.totalCount, ARRAY_SIZE(sortingBuf));
		}
		list_for_each(pos, &g_dvb_channels.orderNoneHead) {
			if(i >= ARRAY_SIZE(sortingBuf)) {
				break;
			}

			sortingBuf[i] = list_entry(pos, service_index_t, orderNone);
			i++;
		}
		count = i;

//		qsort_r(sortingBuf, count, sizeof(sortingBuf[0]), serviceIdx_cmp, &g_dvb_channels.sortOrderType);
		qsort(sortingBuf, count, sizeof(sortingBuf[0]), serviceIdx_cmp);

		for(i = 0; i < count; i++) {
			list_add_tail(&(sortingBuf[i]->orderNone), &g_dvb_channels.orderNoneHead);
		}
	}
	dvbChannel_sortOrderRecheck();

	return 0;
}

int32_t dvbChannel_initServices(void)
{
	dvbChannel_update();
	offair_sortSchedule();

	//return dvbChannel_getCount();
#ifdef ENABLE_STATS
	stats_load();
#endif

	return 0;
}

struct list_head *dvbChannel_getSortList(void)
{
	return &g_dvb_channels.orderNoneHead;
}

#endif // ENABLE_DVB
