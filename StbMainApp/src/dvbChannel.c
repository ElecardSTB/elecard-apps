
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

#include "debug.h"
#include "off_air.h"

#include <cJSON.h>
#include <elcd-rpc.h>

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

static registeredChangeCallback_t changeCallbacks[2] = {
	{NULL, NULL},
	{NULL, NULL},
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

service_index_t *dvbChannel_findServiceCommon(EIT_common_t *header)
{
	struct list_head *pos;
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		if(memcmp(&(srv->common), header, sizeof(EIT_common_t)) == 0) {
			return srv;
		}
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
		if(!srv->data.visible) {
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
		if(!srv->data.visible) {
			continue;
		}
		if(srv && (srv->service == service)) {
			return i;
		}
		i++;
	}

	return -1;
}

service_index_t *dvbChannel_getServiceIndexVisible(uint32_t id, uint32_t visible)
{
	struct list_head *pos;
	uint32_t i = 0;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		if(visible && !srv->data.visible) {
			continue;
		}
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

	srvIdx_first = dvbChannel_getServiceIndexVisible(first, 0);
	srvIdx_second = dvbChannel_getServiceIndexVisible(second, 0);

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

	dvbChannel_changed();

	return 0;
}

int32_t dvbChannel_hasAnyEPG(void)
{
	struct list_head *pos;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if((srvIdx == NULL) || (srvIdx->service == NULL) || !srvIdx->data.visible) {
			continue;
		}
		if((srvIdx->service->schedule != NULL) && dvb_hasMedia(srvIdx->service)) {
			return 1;
		}
	}

	return 0;
}

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

int32_t dvbChannel_addServiceIndexData(EIT_common_t *common, service_index_data_t *data, uint8_t flag)
{
	service_index_t *new = dvbChannel_add();
	if(new) {
		memcpy(&new->common, common, sizeof(EIT_common_t));
		memcpy(&new->data, data, sizeof(service_index_data_t));
		new->flag = flag;
	} else {
		eprintf("%s()[%d]: Cant add channel with common!\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

int32_t dvbChannel_setName(service_index_t *srvIdx, const char *name)
{
	if(!srvIdx || !name) {
		return -1;
	}
	strncpy(srvIdx->data.channelsName, name, sizeof(srvIdx->data.channelsName));
	srvIdx->data.channelsName[sizeof(srvIdx->data.channelsName) - 1] = 0;

	return 0;
}

int32_t dvbChannel_addService(EIT_service_t *service, service_index_data_t *data, uint8_t flag)
{
	service_index_t *new = dvbChannel_add();
	if(new) {
		new->service = service;
		memcpy(&new->common, &service->common, sizeof(EIT_common_t));
		memcpy(&new->data, data, sizeof(service_index_data_t));
		new->flag = flag;
		dvbChannel_setName(new, (char *)service->service_descriptor.service_name);
	} else {
		eprintf("%s()[%d]: Cant add channel with common!\n", __func__, __LINE__);
		return -1;
	}
	return 0;
}

int32_t dvbChannel_remove(service_index_t *srvIdx)
{
	list_del(&srvIdx->orderNone);
	g_dvb_channels.totalCount--;
	if(srvIdx->data.visible) {
		g_dvb_channels.viewedCount--;
	}

	free(srvIdx);
	return 0;
}

static int32_t dvbChannel_readOrderConfig()
{
	FILE *fd = NULL;
	cJSON *root;
	cJSON *format;
	char *data;
	long len;

	fd = fopen(OFFAIR_SERVICES_FILENAME, "r");
	if(fd == NULL) {
		eprintf("%s(): Error opening %s: %m\n", __func__, OFFAIR_SERVICES_FILENAME);
		//Is this need still
		return -1;
	}
	fseek(fd, 0, SEEK_END);
	len = ftell(fd);
	fseek(fd, 0, SEEK_SET);
	data = malloc(len + 1);
	fread(data, 1, len, fd);
	fclose(fd);

	root = cJSON_Parse(data);
	free(data);
	if(!root) {
		eprintf("Error before: [%s]\n", cJSON_GetErrorPtr());
		return -1;
	}

	format = cJSON_GetObjectItem(root, "digital TV channels");
	if(format) {
		uint32_t i = 0;
		uint32_t channelCount = cJSON_GetArraySize(format);
		for (i = 0 ; i < channelCount; i++) {
			cJSON *subitem = cJSON_GetArrayItem(format, i);
			if(subitem) {
				EIT_common_t common;
				service_index_data_t data;

				// common data
				common.media_id = (uint32_t) objGetInt(subitem, "media_id", 0);
				common.service_id = objGetInt(subitem, "service_id", 0);
				common.transport_stream_id = objGetInt(subitem, "transport_stream_id", 0);
				// data
				memset(&data, 0, sizeof(data));
				strncpy(data.channelsName, objGetString(subitem, "channels_name", ""), sizeof(data.channelsName));
				data.audio_track = objGetInt(subitem, "audio_track", 0);
				data.visible = objGetInt(subitem, "visible", 1);
				data.parent_control = objGetInt(subitem, "parent_control", 0);
				dvbChannel_addServiceIndexData(&common, &data, 0);
			}
		}
	}
	cJSON_Delete(root);
	dprintf("%s imported services: %s\n", __func__, OFFAIR_SERVICES_FILENAME);
	return 0;
}

static int32_t dvbChannel_writeOrderConfig(void)
{
	cJSON* format;
	cJSON* root;
	char *render;
	struct list_head *pos;
	uint32_t i = 0;

	format = cJSON_CreateArray();
	if(!format) {
		eprintf("%s(): Memory error!\n", __func__);
		return -1;
	}
	root = cJSON_CreateObject();
	if(!root) {
		cJSON_Delete(format);
		eprintf("%s(): Memory error!\n", __func__);
		return -1;
	}
	cJSON_AddItemToObject(root, "digital TV channels", format);
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(srvIdx->common.media_id || srvIdx->common.service_id || srvIdx->common.transport_stream_id) {
			cJSON* fld;

			fld = cJSON_CreateObject();
			if (fld) {
				cJSON_AddNumberToObject(fld, "service", i);
				cJSON_AddNumberToObject(fld, "media_id", (int)srvIdx->common.media_id);
				cJSON_AddNumberToObject(fld, "service_id", srvIdx->common.service_id);
				cJSON_AddNumberToObject(fld, "transport_stream_id", srvIdx->common.transport_stream_id);
				cJSON_AddStringToObject(fld, "channels_name", srvIdx->data.channelsName);
				cJSON_AddNumberToObject(fld, "audio_track", srvIdx->data.audio_track);
				cJSON_AddNumberToObject(fld, "visible", srvIdx->data.visible);

				cJSON_AddNumberToObject(fld, "parent_control", srvIdx->data.parent_control);
				cJSON_AddItemToArray(format, fld);
				i++;
			}
		}
	}
	render = cJSON_Print(root);
	cJSON_Delete(root);

	if(render) {
		FILE *fd = NULL;

		fd = fopen(OFFAIR_SERVICES_FILENAME, "w");
		if (fd) {
			fwrite(render, strlen(render), 1, fd);
			fclose(fd);
		} else {
			eprintf("%s(): Error opening %s: %m\n", __func__, OFFAIR_SERVICES_FILENAME);
		}
		free(render);
	}
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
			if(srvIdx->data.visible == 1) {
				g_dvb_channels.viewedCount++;
			}
		} else {
			srvIdx->data.visible = 0;
		}
	}
	dvbChannel_changed();
}

int32_t dvbChannel_applyUpdates(void)
{
	dvbChannel_sortOrderRecheck();
	dvbChannel_save();

	//TODO: is it need here?
	dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
	
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

struct list_head *dvbChannel_getSortList(void)
{
	return &g_dvb_channels.orderNoneHead;
}

static int32_t dvbChannel_update(void)
{
	list_element_t   *service_element;
	struct list_head *pos;
	struct list_head *n;

	if(list_empty(&g_dvb_channels.orderNoneHead)) {
		dvbChannel_readOrderConfig();
	} else {
		dvbChannel_invalidateServices();
	}

	for(service_element = dvb_services; service_element != NULL; service_element = service_element->next) {
		service_index_t *p_srvIdx;
		EIT_service_t *curService = (EIT_service_t *)service_element->data;
		p_srvIdx = dvbChannel_findServiceCommon(&curService->common);
		if(p_srvIdx) {
			p_srvIdx->service = curService;
			if (strlen(p_srvIdx->data.channelsName) == 0) {
				strncpy(p_srvIdx->data.channelsName, (char *)p_srvIdx->service->service_descriptor.service_name, strlen((char *)p_srvIdx->service->service_descriptor.service_name));
			}
		} else {
			service_index_data_t data;

			curService->common.media_id = curService->original_network_id;
			memset(&data, 0, sizeof(data));
			data.visible = 1;
			data.parent_control = 0;

			dvbChannel_addService(curService, &data, 0);
		}
	}

	//remove elements without service pointer
	list_for_each_safe(pos, n, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(srvIdx->service == NULL/* || srvIdx->flag == 0*/) {
			dvbChannel_remove(srvIdx);
		}
	}

	return 0;
}

static int32_t dvbChannel_initServices(void)
{
	dvbChannel_update();
	offair_sortSchedule();

	//return dvbChannel_getCount();
#ifdef ENABLE_STATS
	stats_load();
#endif

	return 0;
}

int32_t dvbChannel_registerCallbackOnChange(changeCallback_t *pCallback, void *pArg)
{
	uint32_t i = 0;
	
	while(changeCallbacks[i].pCallback) {
		i++;
		if((i >= ARRAY_SIZE(changeCallbacks))) {
			return -1;
		}
	}
	changeCallbacks[i].pCallback = pCallback;
	changeCallbacks[i].pArg = pArg;

	return 0;
}

int32_t dvbChannel_changed(void)
{
	uint32_t i = 0;
	
	while(changeCallbacks[i].pCallback) {
		changeCallbacks[i].pCallback(changeCallbacks[i].pArg);
		i++;
		if((i >= ARRAY_SIZE(changeCallbacks))) {
			return 0;
		}
	}

	return 0;
}

int32_t dvbChannel_load(void)
{
	dvbChannel_clear();

	if(helperFileExists(appControlInfo.dvbCommonInfo.channelConfigFile)) {
		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
		dprintf("%s(): loaded %d services\n", __func__, dvb_getNumberOfServices());
	} else {
		dvb_clearServiceList(0);
	}
	dvbChannel_initServices();
	return 0;
}

int32_t dvbChannel_save(void)
{
	dvbChannel_writeOrderConfig();

	return 0;
}

int32_t dvbChannel_clear(void)
{
	struct list_head *pos;
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);dbg_printf("\n");
		dvbChannel_remove(srvIdx);
	}
	dvbChannel_changed();
	return 0;
}

void dvbChannel_init(void)
{
// 	dvbChannel_initServices();
}

void dvbChannel_terminate(void)
{
	dvbChannel_clear();
}


#endif // ENABLE_DVB
