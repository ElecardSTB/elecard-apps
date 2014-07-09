/*
 bouquet.c

Copyright (C) 2014  Elecard Devices

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

#include "bouquet.h"

#include "dvbChannel.h"
#include "debug.h"
#include "l10n.h"
#include "analogtv.h"
#include "list.h"
#include "md5.h"

#include "stsdk.h"
#include <cJSON.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define NAME_SPACE                       0xFFFF0000
#define BOUQUET_CONFIG_DIR               CONFIG_DIR "/bouquet"
#define BOUQUET_CONFIG_DIR_ANALOG        CONFIG_DIR "/analog"
#define BOUQUET_ANALOG_MAIN_FILE         "analog.json"
#define BOUQUET_CONFIG_FILE              "bouquet.conf"
#define BOUQUET_CONFIG_FILE_ANALOG       "analog.list"
#define BOUQUET_NAME                     "bouquets"
#define BOUQUET_BLACKLIST                "blacklist"
//#define BOUQUET_STANDARD_NAME            "" //"Elecard playlist"

#define BOUQUET_NAME_SIZE                64
#define CHANNEL_BUFFER_NAME              64
#define MAX_TEXT                         512

#define GARB_DIR                         "/var/etc/garb"
#define GARB_CONFIG                      GARB_DIR "/config"

#define TMP_BATCH_FILE                   "/tmp/cmd_bouquet"

#define PARENT_CONTROL_FILE              CONFIG_DIR "/parentcontrol.hash"

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct {
	uint32_t transport_stream_id;
	uint32_t network_id;
	uint32_t name_space;
} bouquetCommonData_t;

typedef struct {
	bouquetCommonData_t data;
	EIT_media_config_t media;
	struct list_head	transponderList;
} transpounder_t;

typedef struct _lamedb_data {
	uint32_t service_id;
	uint32_t serviceType;
	uint32_t hmm;
	uint16_t audioPID;
	uint16_t videoPID;
	bouquetCommonData_t data;
	char channelsName[64];
	char transponderName[64];
} lamedb_data_t;

typedef struct {
	uint32_t type;
	uint32_t flags;
	uint32_t serviceType;
	uint32_t service_id;
	uint32_t index_8;
	uint32_t index_9;
	uint32_t index_10;
	uint32_t parent_control;
	bouquetCommonData_t data;
	transpounder_t transpounder;
	lamedb_data_t lamedbData;

	struct list_head	channelsList;
} bouquet_element_list_t;

typedef struct {
	uint32_t s_id;                   //Services_ID
	char channel_name[CHANNEL_BUFFER_NAME]; //channels_name
	char provider_name[CHANNEL_BUFFER_NAME]; //provider_name
	uint32_t v_pid;                 //video pid
	uint32_t a_pid;                 //audio pid
	uint32_t t_pid;                 //teletext pid
	uint32_t p_pid;                 //PCR
	uint32_t ac_pid;                //AC3
	uint32_t f;                     //f - ?
} services_t;

typedef struct {
	struct list_head  name_tv;
	struct list_head  name_radio;
	struct list_head  channelsList;
	struct list_head  transponderList;
} bouquetDigital_t;

#ifdef ENABLE_DVB
/******************************************************************
* DATA                                                            *
*******************************************************************/

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static list_element_t *head_ts_list = NULL;
static int bouquets_enable = 0;
static char pName[CHANNEL_BUFFER_NAME];

static bouquetDigital_t digitalBouquet = {
	.name_tv         = LIST_HEAD_INIT(digitalBouquet.name_tv),
	.name_radio      = LIST_HEAD_INIT(digitalBouquet.name_radio),
	.channelsList    = LIST_HEAD_INIT(digitalBouquet.channelsList),
	.transponderList = LIST_HEAD_INIT(digitalBouquet.transponderList),
};

static char *bouqueteCurrentName[eBouquet_all] = { NULL, NULL };
static struct list_head nameListHead[eBouquet_all] = {
	[0] = LIST_HEAD_INIT(nameListHead[0]),
	[1] = LIST_HEAD_INIT(nameListHead[1]),
};

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static void bouquets_addTranspounderData(transpounder_t *tspElement);
static void bouquets_addlamedbData(struct list_head *listHead, lamedb_data_t *lamedbElement);
static void bouquet_loadLamedb( const char *bouquet_file, struct list_head *listHead);
static int bouquet_find_or_AddChannels(const bouquet_element_list_t *element);
static bouquet_element_list_t *digitalList_add(struct list_head *listHead);

static void bouquet_loadNamesFromFile(struct list_head *listHead, char *bouquet_file);
static void bouquet_loadServicesFromFile(struct list_head *listHead, char *bouquet_file);
static void get_bouquets_blacklist(struct list_head *listHead, char *bouquet_file);

static void bouquet_addParentControl(struct list_head *listHead, bouquet_element_list_t *curElement);
static void bouquet_createTransponderList(void);
static void bouquet_saveBouquets(const char *bouquetName, char *typeName);
static void bouquet_saveBouquetsConf(const char *bouquetName);
static void bouquet_saveLamedb(const char *bouquetName);
static int32_t bouquet_saveNameListIntoFile(typeBouquet_t btype);
static int32_t bouquet_parseNameListFile(typeBouquet_t btype, const char *path);

static int32_t bouquet_createDirectory(typeBouquet_t btype, const char *bouquetName);

static int32_t digitalList_release(void);

static int32_t bouquet_uploadDigitalBouquet(const char *bouquetName);
static int32_t bouquet_uploadAnalogBouquet(const char *bouquetName);


/*******************************************************************
* FUNCTION IMPLEMENTATION                                          *
********************************************************************/
static void bouquet_copyToList(typeBouquet_t type, struct list_head *listHead)
{
	switch(type) {
		case eBouquet_digital:
		{
			struct list_head *pos;
			list_for_each(pos, &digitalBouquet.channelsList) {
				bouquet_element_list_t *element = list_entry(pos, bouquet_element_list_t, channelsList);
				bouquet_find_or_AddChannels(element);
			}
		}
			break;

		case eBouquet_analog:
		{
			break;
		}
		default: ;
	}

}

static int bouquet_find_or_AddChannels(const bouquet_element_list_t *element)
{
	struct list_head *pos;
	service_index_t *srvIdx;
	EIT_service_t *el = NULL;

	list_for_each(pos, dvbChannel_getSortList()) {
		srvIdx = list_entry(pos, service_index_t, orderNone);
		if((srvIdx->common.service_id == element->service_id)
			&& (srvIdx->common.transport_stream_id == element->data.transport_stream_id)
			&& (srvIdx->common.media_id == element->data.network_id))
		{
			srvIdx->flag = 1;
			srvIdx->data.parent_control = element->parent_control;

			if(srvIdx->service != NULL) {
				if(srvIdx->service->media.type == serviceMediaDVBC && element->transpounder.media.type == serviceMediaDVBC &&
						srvIdx->service->media.dvb_c.frequency == element->transpounder.media.dvb_c.frequency &&
						srvIdx->service->media.dvb_c.symbol_rate == element->transpounder.media.dvb_c.symbol_rate &&
						srvIdx->service->media.dvb_c.modulation == element->transpounder.media.dvb_c.modulation &&
						srvIdx->service->media.dvb_c.inversion == element->transpounder.media.dvb_c.inversion) {
					return 0;
				}
				if(srvIdx->service->media.type == serviceMediaDVBS && element->transpounder.media.type == serviceMediaDVBS &&
						srvIdx->service->media.dvb_s.frequency == element->transpounder.media.dvb_s.frequency &&
						srvIdx->service->media.dvb_s.symbol_rate == element->transpounder.media.dvb_s.symbol_rate &&
						srvIdx->service->media.dvb_s.polarization == element->transpounder.media.dvb_s.polarization &&
						//element->media.dvb_s.FEC_inner == element_tr->media.dvb_s.FEC_inner &&
						//element->media.dvb_s.orbital_position == element_tr->media.dvb_s.orbital_position &&
						srvIdx->service->media.dvb_s.inversion == element->transpounder.media.dvb_s.inversion) {
					return 0;
				}
				if(srvIdx->service->media.type == serviceMediaDVBT && element->transpounder.media.type == serviceMediaDVBT &&
						srvIdx->service->media.dvb_t.centre_frequency == element->transpounder.media.dvb_t.centre_frequency &&
						srvIdx->service->media.dvb_t.bandwidth == element->transpounder.media.dvb_t.bandwidth &&
						srvIdx->service->media.dvb_t.inversion == element->transpounder.media.dvb_t.inversion &&
						srvIdx->service->media.dvb_t.plp_id == element->transpounder.media.dvb_t.plp_id) {
					return 0;
				}
				memset(srvIdx->service, 0, sizeof(EIT_service_t));
			} else {
				eprintf("ERROR: service not allocated!!!");
				return -1;
			}
			el = srvIdx->service;
			break;
		}
	}

	if(el == NULL) {
		list_element_t *cur_element = NULL;
		if(dvb_services == NULL) {
			dvb_services = cur_element = allocate_element(sizeof(EIT_service_t));
		} else {
			cur_element = append_new_element(dvb_services, sizeof(EIT_service_t));
		}
		if(!cur_element) {
			return -1;
		}
		service_index_data_t data;
		el = (EIT_service_t *)cur_element->data;
		el->common.media_id = element->data.network_id;
		el->common.service_id = element->service_id;
		el->common.transport_stream_id = element->data.transport_stream_id;
		el->original_network_id = element->data.network_id;
		strncpy((char *)el->service_descriptor.service_name, element->lamedbData.channelsName, strlen(element->lamedbData.channelsName));
		data.visible = 1;
		data.parent_control = element->parent_control;
		dvbChannel_addService(el, &data, 1);
	}

	if(element->transpounder.media.type == serviceMediaDVBC) {
		el->media.type = element->transpounder.media.type;
		el->media.dvb_c.frequency = element->transpounder.media.dvb_c.frequency;
		el->media.dvb_c.symbol_rate = element->transpounder.media.dvb_c.symbol_rate;
		el->media.dvb_c.modulation = element->transpounder.media.dvb_c.modulation;
		return 0;
	}
	if(element->transpounder.media.type == serviceMediaDVBS) {
		el->media.type = element->transpounder.media.type;
		el->media.frequency = element->transpounder.media.frequency;
		el->media.dvb_s.frequency = element->transpounder.media.dvb_s.frequency;
		el->media.dvb_s.symbol_rate = element->transpounder.media.dvb_s.symbol_rate;
		el->media.dvb_s.polarization = element->transpounder.media.dvb_s.polarization;
		el->media.dvb_s.FEC_inner = element->transpounder.media.dvb_s.FEC_inner;
		el->media.dvb_s.orbital_position = element->transpounder.media.dvb_s.orbital_position;
		el->media.dvb_s.inversion = element->transpounder.media.dvb_s.inversion;
		return 0;
	}
	if(element->transpounder.media.type == serviceMediaDVBT) {
		el->media.type = element->transpounder.media.type;
		el->media.frequency = element->transpounder.media.frequency;
		el->media.dvb_t.centre_frequency = element->transpounder.media.dvb_t.centre_frequency;
		el->media.dvb_t.bandwidth = element->transpounder.media.dvb_t.bandwidth;
		el->media.dvb_t.code_rate_HP_stream = element->transpounder.media.dvb_t.code_rate_HP_stream;
		el->media.dvb_t.code_rate_LP_stream = element->transpounder.media.dvb_t.code_rate_LP_stream;
		el->media.dvb_t.constellation = element->transpounder.media.dvb_t.constellation;
		el->media.dvb_t.generation = element->transpounder.media.dvb_t.generation;
		el->media.dvb_t.guard_interval = element->transpounder.media.dvb_t.guard_interval;
		el->media.dvb_t.hierarchy_information = element->transpounder.media.dvb_t.hierarchy_information;
		el->media.dvb_t.inversion = element->transpounder.media.dvb_t.inversion;
		el->media.dvb_t.plp_id = element->transpounder.media.dvb_t.plp_id;
		el->media.dvb_t.transmission_mode = element->transpounder.media.dvb_t.transmission_mode;
		el->media.dvb_t.other_frequency_flag = element->transpounder.media.dvb_t.other_frequency_flag;
		return 0;
	}
	return 0;
}

static void bouquet_loadFromFile(typeBouquet_t type, const char *bouquetName)
{
	char fileName[1024];

	switch(type) {
		case eBouquet_digital:
		{
			if(bouquetName == NULL) {
				break;
			}

			digitalList_release();
			dprintf("loading bouquet name: %s\n", bouquetName);

			snprintf(fileName, sizeof(fileName), "%s/%s/bouquets.tv", BOUQUET_CONFIG_DIR, bouquetName);
			bouquet_loadNamesFromFile(&digitalBouquet.name_tv, fileName);
			if(!list_empty(&digitalBouquet.name_tv)) {
				//Use first name only here
				snprintf(fileName, sizeof(fileName), "%s/%s/%s", BOUQUET_CONFIG_DIR, bouquetName, strList_get(&digitalBouquet.name_tv, 0));
				bouquet_loadServicesFromFile(&digitalBouquet.channelsList, fileName/*tv*/);
			}

			snprintf(fileName, sizeof(fileName), "%s/%s/bouquets.radio", BOUQUET_CONFIG_DIR, bouquetName);
			bouquet_loadNamesFromFile(&digitalBouquet.name_radio, fileName);
			if(!list_empty(&digitalBouquet.name_radio)) {
				//Use first name only here
				snprintf(fileName, sizeof(fileName), "%s/%s/%s", BOUQUET_CONFIG_DIR, bouquetName, strList_get(&digitalBouquet.name_radio, 0));
				bouquet_loadServicesFromFile(&digitalBouquet.channelsList, fileName/*radio*/);
			}

			snprintf(fileName, sizeof(fileName), "%s/%s/%s", BOUQUET_CONFIG_DIR, bouquetName, BOUQUET_BLACKLIST);
			get_bouquets_blacklist(&digitalBouquet.channelsList, fileName);

			bouquet_loadLamedb(bouquetName, &digitalBouquet.channelsList);
			break;
		}
		case eBouquet_analog:
		{

			break;
		}
		default: ;
	}

}

static int32_t bouquet_getServerWorkDir(char *buf, size_t bufsize)
{
	getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", buf);
	return 0;
}

static int32_t bouquet_runSftpBatchFile(const char *fileName)
{
	char serverName[16];
	char loginName[32];
	char cmd[1024];
	int32_t ret;

	if(!(helperFileExists(GARB_DIR "/.ssh/id_rsa") && helperFileExists(GARB_DIR "/.ssh/id_rsa.pub"))) {
		eprintf("%s(): No garb private or public key!!!\n", __func__);
		return -1;
	}
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);

	snprintf(cmd, sizeof(cmd), "sftp -b %s -i " GARB_DIR "/.ssh/id_rsa %s@%s", fileName, loginName, serverName);
	ret = dbg_cmdSystem(cmd);

	return WEXITSTATUS(ret);
}

static int32_t bouquet_copyServer(int32_t isFrom, const char *from, const char *to)
{
	char serverName[16];
	char serverDir[256];
	char loginName[32];
	char cmd[1024];
	char *fmt;
	int32_t ret;

	bouquet_getServerWorkDir(serverDir, sizeof(serverDir));
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);

	if(isFrom) {
		fmt = "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/%s %s";
	} else {
		fmt = "scp -i " GARB_DIR "/.ssh/id_rsa -r %s %s@%s:%s/%s";
	}
	snprintf(cmd, sizeof(cmd), fmt, loginName, serverName, serverDir, from, to);
	ret = dbg_cmdSystem(cmd);

	return WEXITSTATUS(ret);
}

int32_t bouquet_copyFromServer(const char *remoteFile, const char *localFile)
{
	return bouquet_copyServer(1, remoteFile, localFile);
}

int32_t bouquet_copyToServer(const char *localFile, const char *dest)
{
	return bouquet_copyServer(0, localFile, dest);
}

static int32_t bouquet_uploadDigitalBouquet(const char *curBouquetName)
{
	char *tmpFile;
	int32_t ret = 0;

	if(curBouquetName == NULL) {
		return -1;
	}

	tmpFile = "/tmp/" BOUQUET_CONFIG_FILE;

	{//Make bouquete list file: bouquet_updateNameList(eBouquet_digital, 1)
		FILE *fd;

		remove(tmpFile);
		bouquet_copyFromServer("../bouquet/" BOUQUET_CONFIG_FILE, tmpFile);

		fd = fopen(tmpFile, "a");
		if(fd == NULL) {
			eprintf("%s: Failed to open '%s'\n", __FUNCTION__, tmpFile);
			return -1;
		}
		fprintf(fd, "#BOUQUETS_NAME=%s\n", curBouquetName);
		fclose(fd);
	}

	{//Make batch file for scp
		FILE *fd;
		char serverDir[256];

		fd = fopen(TMP_BATCH_FILE, "w");
		if(fd == NULL) {
			eprintf("%s: Failed to open %s: %m\n", __FUNCTION__, TMP_BATCH_FILE);
			return 1;
		}

		bouquet_getServerWorkDir(serverDir, sizeof(serverDir));

		fprintf(fd, "-mkdir %s/../bouquet/%s.STB/\n", serverDir, curBouquetName);
		fprintf(fd, "put %s/%s/* %s/../bouquet/%s.STB/\n", BOUQUET_CONFIG_DIR, curBouquetName, serverDir, curBouquetName);
		fprintf(fd, "put %s %s/../bouquet/", tmpFile, serverDir);
		fclose(fd);
	}

	ret = bouquet_runSftpBatchFile(TMP_BATCH_FILE);

	remove(tmpFile);
	remove(TMP_BATCH_FILE);

	return ret;
}


static int32_t bouquet_uploadAnalogBouquet(const char *bouquetName)
{
	int32_t ret = 0;

	if(bouquetName == NULL) {
		eprintf("%s(): No selected bouquet!!!\n", __func__);
		return -1;
	}

	{//Make batch file for scp
		FILE *fd;
		char serverDir[256];

		fd = fopen(TMP_BATCH_FILE, "w");
		if(fd == NULL) {
			eprintf("%s: Failed to open %s: %m\n", __FUNCTION__, TMP_BATCH_FILE);
			return 1;
		}

		bouquet_getServerWorkDir(serverDir, sizeof(serverDir));

		fprintf(fd, "-mkdir %s/../analog\n", serverDir);
		fprintf(fd, "put %s/analog.%s.json %s/../analog/analog.%s.json", BOUQUET_CONFIG_DIR_ANALOG, bouquetName, serverDir, bouquetName);
		fclose(fd);
	}
	ret = bouquet_runSftpBatchFile(TMP_BATCH_FILE);
	remove(TMP_BATCH_FILE);

	return ret;
}

list_element_t *list_getElement(int count, list_element_t **head)
{
	list_element_t *cur_element;
	int cur_count = 0;

	for(cur_element = *head; cur_element != NULL; cur_element = cur_element->next) {
		if(cur_count == count) {
			return cur_element;
		}
		cur_count++;
	}
	return NULL;
}

void bouquet_addScanChannels(void)
{
	list_element_t		*service_element;

	for(service_element = dvb_services; service_element != NULL; service_element = service_element->next) {
		service_index_t *p_srvIdx;
		EIT_service_t *curService = (EIT_service_t *)service_element->data;
		p_srvIdx = dvbChannel_findServiceCommon(&curService->common);
		if(p_srvIdx) {
			p_srvIdx->service = curService;
			if(strlen(p_srvIdx->data.channelsName) == 0) {
				dvbChannel_setName(p_srvIdx, (char *)p_srvIdx->service->service_descriptor.service_name);
			}
		} else {
			service_index_data_t data;
			curService->common.media_id = curService->original_network_id;
			data.visible = 1;
			data.parent_control = 0;
			dvbChannel_addService(curService, &data, 1);
		}
	}

	dvbChannel_changed();
	dvbChannel_save();

}

static void bouquet_loadNamesFromFile(struct list_head *listHead, char *bouquet_file)
{
	char buf[BUFFER_SIZE];
	FILE *fd;

	fd = fopen(bouquet_file, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	// check head file name
	while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		char *ptr;

		if(strncasecmp(buf, "#SERVICE", 8) != 0) {
			continue;
		}

		ptr = strchr(buf, '"');
		if(ptr) {
			char name[256];
			sscanf(ptr + 1, "%s \n", name); //get bouquet_name type: name" (with ")
			name[strlen(name) - 1] = '\0'; //get bouquet_name type: name
			strList_add(listHead, name);

			dprintf("Get bouquet file name: %s\n", name);
		}
	}
	fclose(fd);
}

transpounder_t *found_transpounder(service_index_t *transp)
{	
	struct list_head *pos;
	list_for_each(pos, &digitalBouquet.transponderList) {
		transpounder_t *element = list_entry(pos, transpounder_t, transponderList);
		if(element == NULL) {
			continue;
		}
		if(memcmp(&(transp->service->media), &(element->media.type), sizeof(EIT_media_config_t)) == 0) {
			return element;
		}

	}
	return NULL;
}

static void bouquet_loadServicesFromFile(struct list_head *listHead, char *bouquet_file)
{
	dprintf("%s loading: %s\n",__func__, bouquet_file );


	char buf[BUFFER_SIZE];
	FILE *fd;
	uint32_t type;
	uint32_t flags;
	uint32_t serviceType;
	uint32_t service_id;
	uint32_t transport_stream_id;
	uint32_t network_id;
	uint32_t name_space;
	uint32_t index_8;
	uint32_t index_9;
	uint32_t index_10;

	fd = fopen(bouquet_file, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	if(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		sscanf(buf, "#NAME  %s\n", pName);
	}

	while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		if(sscanf(buf, "#SERVICE %x:%x:%x:%04x:%04x:%x:%x:%x:%x:%x:\n",   &type,
				  &flags,
				  &serviceType,
				  &service_id,
				  &transport_stream_id,
				  &network_id,
				  &name_space,
				  &index_8,
				  &index_9,
				  &index_10) != 10) {
			continue;
		}
		bouquet_element_list_t *element = digitalList_add(listHead);
		if (element) {
			element->type = type;
			element->flags = flags;
			element->serviceType = serviceType;
			element->service_id = service_id;
			element->data.transport_stream_id = transport_stream_id;
			element->data.network_id = network_id;
			element->data.name_space = name_space;
			element->index_8 = index_8;
			element->index_9 = index_9;
			element->index_10 = index_10;
			element->parent_control = 0;
		}
	}
	fclose(fd);
}

static void get_bouquets_blacklist(struct list_head *listHead, char *bouquet_file)
{
	dprintf("%s loading: %s\n",__func__, bouquet_file );

	char buf[BUFFER_SIZE];
	FILE *fd;
	bouquet_element_list_t element;

	fd = fopen(bouquet_file, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}

	while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		if(sscanf(buf, "%x:%x:%x:%04x:%04x:%x:%x:%x:%x:%x:\n",   &element.type,
				  &element.flags,
				  &element.serviceType,
				  &element.service_id,
				  &element.data.transport_stream_id,
				  &element.data.network_id,
				  &element.data.name_space,
				  &element.index_8,
				  &element.index_9,
				  &element.index_10) != 10) {
			continue;
		}
		bouquet_addParentControl(listHead, &element);
	}
	fclose(fd);
}

int32_t bouquet_isEnable(void)
{
	return bouquets_enable;
}

void bouquet_setEnable(int32_t enable)
{
	//TODO: Fix next trouble:
	//this can be called at startup (from app_info.c) when bouquete not initialized yet.

	//TODO: Make with this something more clear
	bouquets_enable = 1;//this need for working bouquet_getCurrentName() in bouquet_open()
	if(enable) {
		bouquet_open(eBouquet_digital, bouquet_getCurrentName(eBouquet_digital), 0);
		bouquet_open(eBouquet_analog, bouquet_getCurrentName(eBouquet_analog), 0);
	} else {
		bouquet_open(eBouquet_digital, NULL, 0);
		bouquet_open(eBouquet_analog, NULL, 0);
	}
	bouquets_enable = enable;

}

static int32_t bouquet_createDirectory(typeBouquet_t btype, const char *bouquetName)
{
	char buffName[256];

	if(bouquetName == NULL) {
		return 0;
	}

	if(btype == eBouquet_digital) {
		sprintf(buffName, BOUQUET_CONFIG_DIR "/%s", bouquetName);
		if(!helperCheckDirectoryExsists(buffName)) {
			return mkdir(buffName, 0777);
		}
	} else if(btype == eBouquet_analog) {
		sprintf(buffName, BOUQUET_CONFIG_DIR_ANALOG "/analog.%s.json", bouquetName);
		if(!helperFileExists(buffName)) {
			return creat(buffName, 0644);
		}
	}

	return 0;
}

void bouquet_saveLamedb(const char *bouquetName)
{
	dprintf("Save services list in lamedb\n");
	char dirName[BUFFER_SIZE];
	char buf[BUFFER_SIZE];
	char bufPID[BUFFER_SIZE];
	uint16_t elPID;
	uint16_t flagPID;
	struct list_head *pos;
	FILE *fd;

	sprintf(dirName, "%s/%s/lamedb", BOUQUET_CONFIG_DIR , bouquetName);
	fd = fopen(dirName, "wb");

	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, dirName);
		return;
	}

	fprintf(fd, "eDVB services /4/\n");
	fprintf(fd, "transponders\n");

	list_for_each(pos, &digitalBouquet.transponderList) {
		transpounder_t *element = list_entry(pos, transpounder_t, transponderList);

		fprintf(fd, "%08x:%04x:%04x\n", element->data.name_space, element->data.transport_stream_id, element->data.network_id);
		if(element->media.type == serviceMediaDVBC) {
			fprintf(fd, "	%c %d:%d:%d:%d:0:0:0\n", 'c', element->media.dvb_c.frequency / 1000,
					element->media.dvb_c.symbol_rate,
					(element->media.dvb_c.inversion == 0 ? 2 : 1),
					element->media.dvb_c.modulation);
		}
		if(element->media.type == serviceMediaDVBS) {
			fprintf(fd, "	%c %d:%d:%d:%d:%d:%d:0\n", 's', element->media.dvb_s.frequency,
					element->media.dvb_s.symbol_rate,
					element->media.dvb_s.polarization,
					element->media.dvb_s.FEC_inner,
					element->media.dvb_s.orbital_position,
					(element->media.dvb_s.inversion == 0 ? 2 : 1));
		}
		if(element->media.type == serviceMediaDVBT) {
			fprintf(fd, "	%c %d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d\n", 't', element->media.dvb_t.centre_frequency,
					element->media.dvb_t.bandwidth,
					element->media.dvb_t.code_rate_HP_stream,
					element->media.dvb_t.code_rate_LP_stream,
					0,
					element->media.dvb_t.transmission_mode,
					element->media.dvb_t.guard_interval,
					element->media.dvb_t.hierarchy_information,
					(element->media.dvb_t.inversion == 0 ? 2 : 1),
					0,
					0,
					element->media.dvb_t.plp_id);
		}
		fprintf(fd, "/\n");
	}
	fprintf(fd, "end\n");

	fprintf(fd, "services\n");
	list_for_each(pos, dvbChannel_getSortList()) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		transpounder_t *element;
		element = found_transpounder(srvIdx);
		fprintf(fd, "%04x:%08x:%04x:%04x:%d:0\n", srvIdx->common.service_id, element->data.name_space, element->data.transport_stream_id, element->data.network_id, srvIdx->service->service_descriptor.service_type);
		fprintf(fd, "%s\n", srvIdx->data.channelsName);
		sprintf(buf, "p:%s,", bouquetName);
		flagPID = 40;
		if(dvb_hasMediaType(srvIdx->service, mediaTypeVideo)) {
			elPID = dvb_getVideoPid(srvIdx->service);
			sprintf(bufPID, "c:00%04x,", elPID);
			strcat(buf,bufPID);
			flagPID = 44;
		}
		if(dvb_hasMediaType(srvIdx->service, mediaTypeAudio)) {
			elPID = dvb_getAudioPid(srvIdx->service, srvIdx->data.audio_track);
			sprintf(bufPID, "c:01%04x,", elPID);
			strcat(buf,bufPID);
			flagPID = 44;
		}
		elPID = dvb_getVideoPid(srvIdx->service);
		sprintf(bufPID, "f:%d", flagPID);
		strcat(buf,bufPID);
		fprintf(fd, "%s\n", buf);
	}
	fprintf(fd, "end\n");
	free_elements(&head_ts_list);
	fclose(fd);
}

void bouquet_createTransponderList(void)
{
	struct list_head *pos;
	int32_t nameSP = NAME_SPACE + 1;

	list_for_each(pos, dvbChannel_getSortList()) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		transpounder_t *element;
		element = found_transpounder(srvIdx);

		if(element != NULL) {
			continue;
		}

		element = malloc(sizeof(transpounder_t));
		if(element == NULL) {
			eprintf("%s()[%d]: Error allocating memory!\n", __func__, __LINE__);
			continue;
		}
		list_add_tail(&(element->transponderList), &digitalBouquet.transponderList);


		if ( srvIdx->service->media.type == serviceMediaDVBS) {
			element->data.name_space = nameSP;
			nameSP++;
		} else {
			element->data.name_space = NAME_SPACE;
		}

		element->data.transport_stream_id = srvIdx->service->common.transport_stream_id;
		element->data.network_id = srvIdx->service->original_network_id;

		memcpy(&element->media, &srvIdx->service->media, sizeof(EIT_media_config_t));
	}
}

void bouquet_saveBouquets(const char *bouquetName, char *typeName)
{
	FILE *fd;
	char bouquet_file[BUFFER_SIZE];

	sprintf(bouquet_file, "%s/%s/bouquets.%s", BOUQUET_CONFIG_DIR , bouquetName, typeName);
	fd = fopen(bouquet_file, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	fprintf(fd, "#NAME Bouquets (%s)\n", typeName);
	fprintf(fd, "#SERVICE 1:7:1:0:0:0:0:0:0:0:FROM BOUQUET \"userbouquet.%s.%s\" ORDER BY bouquet\n", bouquetName, typeName);
	fclose(fd);
}

void bouquet_saveBouquetsConf(const char *bouquetName)
{
	dprintf("Save services list in Bouquets\n");
	FILE *fdTV;
	FILE *fdRadio;
	FILE *fdBlack;
	FILE *fd;
	char bouquet_file[BUFFER_SIZE];
	struct list_head *pos;

	sprintf(bouquet_file, "%s/%s/userbouquet.%s.tv", BOUQUET_CONFIG_DIR, bouquetName, bouquetName);
	fdTV = fopen(bouquet_file, "wb");
	if(fdTV == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	sprintf(bouquet_file, "%s/%s/userbouquet.%s.radio", BOUQUET_CONFIG_DIR, bouquetName, bouquetName);
	fdRadio = fopen(bouquet_file, "wb");
	if(fdRadio == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		fclose(fdTV);
		return;
	}

	sprintf(bouquet_file, "%s/%s/%s", BOUQUET_CONFIG_DIR , bouquetName, BOUQUET_BLACKLIST);
	fdBlack = fopen(bouquet_file, "wb");
	if(fdBlack == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		fclose(fdTV);
		fclose(fdRadio);
		return;
	}

	list_for_each(pos, dvbChannel_getSortList()) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(srvIdx->data.visible) {
			if (srvIdx->service->service_descriptor.service_type  == 0) {
				if (dvb_hasMediaType(srvIdx->service, mediaTypeAudio) && !dvb_hasMediaType(srvIdx->service, mediaTypeVideo)) {
					srvIdx->service->service_descriptor.service_type  = 2;
				} else {
					srvIdx->service->service_descriptor.service_type  = 1;
				}
			}
			if (srvIdx->service->service_descriptor.service_type  == 2) {
				fd = fdRadio;
			} else {
				fd = fdTV;
			}

			transpounder_t *element;
			element = found_transpounder(srvIdx);

			fprintf(fd, "#SERVICE 1:0:%d:%x:%x:%x:%08x:0:0:0:\n",
					srvIdx->service->service_descriptor.service_type,
					srvIdx->common.service_id,
					element->data.transport_stream_id,
					element->data.network_id,
					element->data.name_space);

			if (srvIdx->data.parent_control == 1) {
				fprintf(fdBlack, "1:0:%d:%x:%x:%x:%08x:0:0:0:\n",
						srvIdx->service->service_descriptor.service_type,
						srvIdx->common.service_id,
						element->data.transport_stream_id,
						element->data.network_id,
						element->data.name_space);
			}
		}
	}
	fclose(fdTV);
	fclose(fdRadio);
	fclose(fdBlack);
}

void bouquet_saveAnalogBouquet(void)
{
	char cmd[1024];
	char fname[BUFFER_SIZE];

	snprintf(cmd, sizeof(cmd), "cp %s/../analog.json %s", BOUQUET_CONFIG_DIR_ANALOG, fname);
	dbg_cmdSystem(cmd);

}

static int32_t bouquet_saveNameListIntoFile(typeBouquet_t btype)
{
	FILE *fd;
	char buffName[256];
	const char *name;
	const char *fmt;
	uint32_t num = 0;

	if(btype == eBouquet_analog) {
		snprintf(buffName, sizeof(buffName), "%s/%s", BOUQUET_CONFIG_DIR_ANALOG, BOUQUET_CONFIG_FILE_ANALOG);
		fmt = "#ANALOG_NAME=%s\n";
	} else if(btype == eBouquet_digital) {
		snprintf(buffName, sizeof(buffName), "%s/%s", BOUQUET_CONFIG_DIR, BOUQUET_CONFIG_FILE);
		fmt = "#BOUQUETS_NAME=%s\n";
	} else {
		return -1;
	}
	
	fd = fopen(buffName, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, buffName);
		return -1;
	}

	while((name = strList_get(bouquet_getNameList(btype), num)) != NULL) {
		fprintf(fd, fmt, name);
		num++;
	}
	fclose(fd);
	return 0;
}

static int32_t bouquet_parseNameListFile(typeBouquet_t btype, const char *path)
{
	char buf[BUFFER_SIZE];
	FILE *fd;
	const char *fmt;

	if(btype == eBouquet_analog) {
		fmt = "#ANALOG_NAME=%s";
	} else if(btype == eBouquet_digital) {
		fmt = "#BOUQUETS_NAME=%s";
	} else {
		return -1;
	}
	
	fd = fopen(path, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, path);
		return -2;
	}
	// check head file name
	while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		char name[256];

		if(sscanf(buf, fmt, name) == 1) {
			if(!strList_isExist(bouquet_getNameList(btype), name)) {
				strList_add(bouquet_getNameList(btype), name);
			}
		}
	}
	fclose(fd);
	return 0;
}

int32_t bouquet_updateNameList(typeBouquet_t btype, int32_t download)
{
	char *defaultListFile;
	char *serverPath;

	if(btype == eBouquet_analog) {
		defaultListFile = BOUQUET_CONFIG_DIR_ANALOG "/" BOUQUET_CONFIG_FILE_ANALOG;
		serverPath = "../analog/" BOUQUET_CONFIG_FILE_ANALOG;
	} else if(btype == eBouquet_digital) {
		defaultListFile = BOUQUET_CONFIG_DIR "/" BOUQUET_CONFIG_FILE;
		serverPath = "../bouquet/" BOUQUET_CONFIG_FILE;
	} else {
		return -1;
	}

	strList_release(bouquet_getNameList(btype));
	if(download == 1) {
		char *tmpFile = "/tmp/" BOUQUET_CONFIG_FILE;
		
		remove(tmpFile);
		bouquet_copyFromServer(serverPath, tmpFile);
		bouquet_parseNameListFile(btype, tmpFile);
	}
	bouquet_parseNameListFile(btype, defaultListFile);

	if(download == 1) {
		bouquet_saveNameListIntoFile(btype);
	}

	return 0;
}

void bouquet_loadLamedb(const char *bouquet_file, struct list_head *listHead)
{
	dprintf("Load services list from lamedb\n");
	char path[BOUQUET_NAME_SIZE * 2];
	char buf[BUFFER_SIZE];
	FILE *fd;
	sprintf(path, "%s/%s/lamedb", BOUQUET_CONFIG_DIR, bouquet_file);
	fd = fopen(path, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s': %m\n", __func__, path);
		return;
	}

	do {
		if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
			break;
		}
		//parse head file name
		/*
		 *  not done
		 */
		if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
			break;
		}
		if(strncasecmp(buf, "transponders", 12) != 0) {
			break;
		}
		//------ start parse transponders list -----//
		if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
			break;
		}
		do {
			transpounder_t tspElement;
			if(sscanf(buf, "%x:%x:%x\n",&tspElement.data.name_space, &tspElement.data.transport_stream_id, &tspElement.data.network_id) != 3) {
				break;
			}
			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}
			switch(buf[1]) {
				case 's': {
					uint32_t freq, sym_rate, polarization, FEC_inner, orbital_position, inversion/*0 - auto, 1 - on, 2 - off*/, system/*0 - DVB-C, 1 DVB-C ANNEX C*/, modulation, rolloff, pilot;

					tspElement.media.type = serviceMediaDVBS;
					sscanf(buf + 3, "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d\n",
						   &freq, &sym_rate, &polarization, &FEC_inner, &orbital_position, &inversion, &system, &modulation, &rolloff, &pilot);

					tspElement.media.frequency = freq;
					tspElement.media.dvb_s.frequency = freq;
					tspElement.media.dvb_s.symbol_rate = sym_rate;
					tspElement.media.dvb_s.polarization = polarization;
					tspElement.media.dvb_s.FEC_inner = FEC_inner;
					tspElement.media.dvb_s.orbital_position = orbital_position;
					tspElement.media.dvb_s.inversion = (inversion == 2 ? 0 : 1);
					//system
					//modulation
					//rolloff
					//pilot
					break;
				}
				case 'c': {
					uint32_t freq, sym_rate, inversion, mod, fec_inner, flag, system;

					tspElement.media.type = serviceMediaDVBC;
					sscanf(buf + 3, "%d:%d:%d:%d:%d:%d:%d\n",
						   &freq, &sym_rate, &inversion, &mod, &fec_inner, &flag, &system);

					tspElement.media.type = serviceMediaDVBC;
					tspElement.media.frequency = freq * 1000;
					tspElement.media.dvb_c.frequency = freq * 1000;
					tspElement.media.dvb_c.symbol_rate = sym_rate;
					tspElement.media.dvb_c.modulation = mod;
					tspElement.media.dvb_c.inversion = (inversion == 2 ? 0 : 1);
					break;
				}
				case 't': {
					uint32_t freq, bandwidth, code_rate_HP, code_rate_LP, modulation, transmission_mode, guard_interval, hierarchy, inversion, flags, system, plpid;

					tspElement.media.type = serviceMediaDVBT;
					sscanf(buf + 3, "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
						   &freq, &bandwidth, &code_rate_HP, &code_rate_LP, &modulation, &transmission_mode, &guard_interval, &hierarchy, &inversion, &flags, &system, &plpid);

					tspElement.media.frequency = freq;
					tspElement.media.dvb_t.centre_frequency = freq;
					tspElement.media.dvb_t.bandwidth = bandwidth;
					tspElement.media.dvb_t.code_rate_HP_stream = code_rate_HP;
					tspElement.media.dvb_t.code_rate_LP_stream = code_rate_LP;
					//modulation
					tspElement.media.dvb_t.transmission_mode = transmission_mode;
					tspElement.media.dvb_t.guard_interval = guard_interval;
					tspElement.media.dvb_t.hierarchy_information = hierarchy;
					tspElement.media.dvb_t.inversion = (inversion == 2 ? 0 : 1);
					//flags
					//system
					tspElement.media.dvb_t.plp_id = plpid;
					tspElement.media.dvb_t.generation = 2;
					break;
				}
			}
			bouquets_addTranspounderData(&tspElement);

			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}
			if(strncasecmp(buf, "/", 1) != 0) {
				break;
			}
			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}
		} while(strncasecmp(buf, "end", 3) != 0);

		//------ start parse services list -----//
		if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
			break;
		}
		if(strncasecmp(buf, "services", 8) != 0) {
			break;
		}
		if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
			break;
		}

		do {
			lamedb_data_t lamedb_data;

			if (sscanf(buf, "%x:%x:%x:%x:%x:%x\n",
					   &lamedb_data.service_id,
					   &lamedb_data.data.name_space,
					   &lamedb_data.data.transport_stream_id,
					   &lamedb_data.data.network_id,
					   &lamedb_data.serviceType,
					   &lamedb_data.hmm) != 6){
				break;
			}
			//parse channels name

			char service_name[MAX_TEXT];
			if(fgets(service_name, BUFFER_SIZE, fd) == NULL) {
				break;
			}
			service_name[strlen(service_name) - 1] = '\0';
			sprintf(lamedb_data.channelsName, "%s", service_name);

			//parese bouquet pName
			char bouquetBuf[CHANNEL_BUFFER_NAME];
			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}

			memset(bouquetBuf, 0, strlen(bouquetBuf));
			char *buffer;
			uint32_t videoPID;
			uint32_t audioPID;

			if (strncasecmp(buf, "p:", 2) == 0) {
				buffer = strchr(buf, ',');
				if (buffer != NULL) {
					strncpy(lamedb_data.transponderName, buf + 2, buffer - buf - 2);
					buffer = buf + 3 + strlen(lamedb_data.transponderName);
				}
			}
			if (buffer != NULL && strncasecmp(buffer, "c:00", 4) == 0) {
				sscanf(buffer + 4, "%04x",&videoPID);
				buffer = buffer + 9;
			}
			if (buffer != NULL && strncasecmp(buffer, "c:01", 4) == 0) {
				sscanf(buffer + 4, "%04x",&audioPID);
				buffer = buffer + 9;
			}
			if (buffer != NULL && strncasecmp(buffer, "c:02", 4) == 0) {
				buffer = buffer + 9;
			}
			if (buffer != NULL && strncasecmp(buffer, "c:03", 4) == 0) {
				buffer = buffer + 9;
			}
			if (buffer != NULL && strncasecmp(buffer, "c:04", 4) == 0) {
				buffer = buffer + 9;
			}
			if (buffer != NULL && strncasecmp(buffer, "f:44", 4) == 0) {
				lamedb_data.audioPID = audioPID;
				lamedb_data.videoPID = videoPID;
			}

			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}
			bouquets_addlamedbData(listHead, &lamedb_data);
		} while(strncasecmp(buf, "end", 3) != 0);
	} while(0);

	free_elements(&head_ts_list);
	fclose(fd);
}

static void bouquets_addlamedbData(struct list_head *listHead, lamedb_data_t *lamedbElement)
{
	struct list_head *pos;
	list_for_each(pos, &digitalBouquet.channelsList) {
		bouquet_element_list_t *element = list_entry(pos, bouquet_element_list_t, channelsList);

		if((memcmp(&(element->data), &lamedbElement->data, sizeof(bouquetCommonData_t)) == 0) &&
				(element->service_id == lamedbElement->service_id)) {
			memcpy(&element->lamedbData, lamedbElement, sizeof(lamedb_data_t));
			return ;
		}
	}
}

static void bouquets_addTranspounderData(transpounder_t *tspElement)
{
	struct list_head *pos;
	list_for_each(pos, &digitalBouquet.channelsList) {
		bouquet_element_list_t *element = list_entry(pos, bouquet_element_list_t, channelsList);

		if(memcmp(&(element->data), &tspElement->data, sizeof(bouquetCommonData_t)) == 0) {
			memcpy(&element->transpounder, tspElement, sizeof(transpounder_t));
		}
	}
}

void bouquet_addParentControl(struct list_head *listHead, bouquet_element_list_t *curElement)
{
	struct list_head *pos;
	list_for_each(pos, listHead) {
		bouquet_element_list_t *el = list_entry(pos, bouquet_element_list_t, channelsList);
		if (curElement->service_id == el->service_id &&
				memcmp(&(curElement->data), &(el->data), sizeof(bouquetCommonData_t)) == 0)
		{
			el->parent_control = 1;
			return;
		}
	}
}


void bouquet_setCurrentName(typeBouquet_t btype, const char *name)
{
	switch(btype) {
		case eBouquet_digital:
		case eBouquet_analog:
			if(name == bouqueteCurrentName[btype]) {
				dprintf("%s(): Trying to set name from the same pointer!!!\n", __func__);
				return;
			}
			if(bouqueteCurrentName[btype]) {
				free(bouqueteCurrentName[btype]);
			}
			bouqueteCurrentName[btype] = NULL;
			if(name) {
				bouqueteCurrentName[btype] = strdup(name);
			}
			break;
		default:
			break;
	}
}

const char *bouquet_getCurrentName(typeBouquet_t btype)
{
	if(!bouquet_isEnable()) {
		return NULL;
	}

	switch(btype) {
		case eBouquet_digital:
		case eBouquet_analog:
//			if(digitalBouquet.curName && (strlen(digitalBouquet.curName) > 0)
			return bouqueteCurrentName[btype];
		default:
			break;
	}
	return NULL;
}

struct list_head *bouquet_getNameList(typeBouquet_t btype)
{
	switch(btype) {
		case eBouquet_digital:
		case eBouquet_analog:
			return &nameListHead[btype];
		default:
			break;
	}
	return NULL;
}

static int32_t bouquet_updateLinks(typeBouquet_t btype, const char *name)
{
	char fileName[1024];
	switch(btype) {
		case eBouquet_digital:
			if(helperFileIsSymlink(CHANNEL_FILE_NAME)) {
				unlink(CHANNEL_FILE_NAME);
			} else if(helperFileExists(CHANNEL_FILE_NAME)) {
				rename(CHANNEL_FILE_NAME, CONFIG_DIR "/channels.default.conf");
			}

			if(helperFileIsSymlink(OFFAIR_SERVICES_FILENAME)) {
				unlink(OFFAIR_SERVICES_FILENAME);
			} else if(helperFileExists(OFFAIR_SERVICES_FILENAME)) {
				rename(OFFAIR_SERVICES_FILENAME, CONFIG_DIR "/offair.default.conf");
			}

			if(name) {
				snprintf(fileName, sizeof(fileName), "bouquet/%s/channels.conf", name);
				symlink(fileName, CHANNEL_FILE_NAME);
				snprintf(fileName, sizeof(fileName), "bouquet/%s/offair.conf", name);
				symlink(fileName, OFFAIR_SERVICES_FILENAME);
				
			} else {
				symlink("channels.default.conf", CHANNEL_FILE_NAME);
				symlink("offair.default.conf", OFFAIR_SERVICES_FILENAME);
			}
			break;
		case eBouquet_analog:
			if(helperFileIsSymlink(ANALOG_CHANNEL_FILE_NAME)) {
				unlink(ANALOG_CHANNEL_FILE_NAME);
			} else if(helperFileExists(ANALOG_CHANNEL_FILE_NAME)) {
				rename(ANALOG_CHANNEL_FILE_NAME, CONFIG_DIR "/analog.default.json");
			}

			if(name) {
				snprintf(fileName, sizeof(fileName), "analog/analog.%s.json", name);
				symlink(fileName, ANALOG_CHANNEL_FILE_NAME);
			} else {
				symlink("analog.default.json", ANALOG_CHANNEL_FILE_NAME);
			}

			break;
		default:
			break;
	}
	return 0;
}

int32_t bouquet_open(typeBouquet_t btype, const char *name, int32_t force)
{
	if(force == 0) {
		const char *curName = bouquet_getCurrentName(btype);

		if((curName && name && (strcasecmp(curName, name) == 0))
			|| (!curName && !name))
		{
			return 0;
		}
	}

	//TODO: Check if this need here
	if(name && !bouquet_isDownloaded(btype, name)) {
		eprintf("%s(): Need download first!!! btype=%d, name=%s\n", __func__, btype, name);
		return -1;
	}

	bouquet_updateLinks(btype, name);
	if(btype == eBouquet_digital) {
		dvbChannel_load();
		if(name) {
			bouquet_loadFromFile(eBouquet_digital, name);
			bouquet_copyToList(eBouquet_digital, dvbChannel_getSortList());

			//TODO: here need to remove extra services from dvb_services and save channels.conf
			dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);

#if (defined STSDK)
			elcdRpcType_t type;
			cJSON *result = NULL;
			st_rpcSync(elcmd_dvbclearservices, NULL, &type, &result);
			cJSON_Delete(result);
#endif //#if (defined STSDK)
		}
		dvbChannel_save();
	} else if(btype == eBouquet_analog) {
		analogtv_load();
	} else {
		return -2;
	}

	bouquet_setCurrentName(btype, name);
	saveAppSettings();

	return 0;
}

int32_t bouquet_update(typeBouquet_t btype, const char *name)
{
	if(name == NULL) {
		return -1;
	}

	if(btype == eBouquet_digital) {
		char bouqueteDir[256];
		char bouqueteDir_temp[256];
		char from[256];
		char cmd[1024];

		snprintf(bouqueteDir, sizeof(bouqueteDir), BOUQUET_CONFIG_DIR "/%s", name);

		snprintf(bouqueteDir_temp, sizeof(bouqueteDir_temp), "%s_temp", bouqueteDir);
		rename(bouqueteDir, bouqueteDir_temp);

		snprintf(from, sizeof(from), "../bouquet/%s.STB", name);
		bouquet_copyFromServer(from, bouqueteDir);

		if(!bouquet_isDownloaded(eBouquet_digital, name)) {
			snprintf(from, sizeof(from), "../bouquet/%s", name);
			bouquet_copyFromServer(from, bouqueteDir);
		}

		if(!bouquet_isDownloaded(eBouquet_digital, name)) {
			rename(bouqueteDir_temp, bouqueteDir);
		} else {
			snprintf(cmd, sizeof(cmd), "rm -r %s_temp/", bouqueteDir);
		}
		dbg_cmdSystem(cmd);
	} else if(btype == eBouquet_analog) {
		char from[256];
		char to[256];

		snprintf(from, sizeof(from), "../analog/analog.%s.json", name);
		snprintf(to, sizeof(to), BOUQUET_CONFIG_DIR_ANALOG "/analog.%s.json", name);
		bouquet_copyFromServer(from, to);
	} else {
		return -2;
	}

//	bouquet_open(btype, name, 1);

	return 0;
}

int32_t bouquet_upload(typeBouquet_t btype, const char *name)
{
	if(name == NULL) {
		return -1;
	}
	if(btype == eBouquet_digital) {
		bouquet_uploadDigitalBouquet(name);
	} else if(btype == eBouquet_analog) {
		bouquet_uploadAnalogBouquet(name);
	} else {
		return -2;
	}

	return 0;
}

int32_t bouquet_isDownloaded(typeBouquet_t btype, const char *name)
{
	char buffName[256];
	if(name == NULL) {
		return 0;
	}

	switch(btype) {
		case eBouquet_digital:
			sprintf(buffName, BOUQUET_CONFIG_DIR "/%s", name);
			if(helperCheckDirectoryExsists(buffName)) {
				return 1;
			}
			break;
		case eBouquet_analog:
			sprintf(buffName, BOUQUET_CONFIG_DIR_ANALOG "/analog.%s.json", name);
			if(helperFileExists(buffName)) {
				return 1;
			}
			break;
		default:
			break;
	}
	return 0;
}

int32_t bouquet_create(typeBouquet_t btype, const char *name)
{
	if(name == NULL) {
		return -1;
	}
	int status;
	status = bouquet_createDirectory(btype, name);
	if(status == -1) {
		return -2;
	}
	strList_add(bouquet_getNameList(btype), name);
	bouquet_saveNameListIntoFile(btype);
	return 0;
}

int32_t bouquet_remove(typeBouquet_t btype, const char *bouquetName)
{
	if(bouquetName == NULL) {
		return -1;
	}

	if(btype == eBouquet_digital) {
		//remove files
		char buffName[256];
		//remove dir
		sprintf(buffName, "rm -r %s/%s/", BOUQUET_CONFIG_DIR, bouquetName);
		dbg_cmdSystem(buffName);

	} else if(btype == eBouquet_analog) {
		//TODO
	} else {
		return -3;
	}

	strList_remove(bouquet_getNameList(btype), bouquetName);
	bouquet_saveNameListIntoFile(btype);

	bouquet_open(btype, NULL, 1);

	return 0;
}

int32_t bouquet_save(typeBouquet_t btype, const char *name)
{
	if(name == NULL) {
		return -1;
	}

	if(btype == eBouquet_digital) {
		bouquet_createDirectory(btype, name);

		bouquet_saveBouquets(name, "tv");
		bouquet_saveBouquets(name, "radio");
		bouquet_createTransponderList();
		bouquet_saveBouquetsConf(name);
		bouquet_saveLamedb(name);
	} else if(btype == eBouquet_analog) {
		//TODO
	} else {
		return -2;
	}

	return 0;
}

void bouquet_init(void)
{
	if(!helperCheckDirectoryExsists(BOUQUET_CONFIG_DIR)) {
		mkdir(BOUQUET_CONFIG_DIR, 0777);
	}

	if(!helperCheckDirectoryExsists(BOUQUET_CONFIG_DIR_ANALOG)) {
		mkdir(BOUQUET_CONFIG_DIR_ANALOG, 0777);
	}

	bouquet_updateNameList(eBouquet_digital, 0);
	bouquet_updateNameList(eBouquet_analog, 0);

	bouquet_open(eBouquet_digital, bouquet_getCurrentName(eBouquet_digital), 1);
	bouquet_open(eBouquet_analog, bouquet_getCurrentName(eBouquet_analog), 1);

}

void bouquet_terminate(void)
{
	free_elements(&head_ts_list);
	digitalList_release();

	//TODO: free more obviosly
	bouquet_setCurrentName(eBouquet_digital, NULL);
	bouquet_setCurrentName(eBouquet_analog, NULL);
	
	strList_release(bouquet_getNameList(eBouquet_digital));
	strList_release(bouquet_getNameList(eBouquet_analog));
}

/*************************************************************
****                                                      ****
****              FUNCTIONS FOR CONTROLLING LIST          ****
****                                                      ****
*************************************************************/
bouquet_element_list_t *digitalList_add(struct list_head *listHead)
{
	bouquet_element_list_t *new = malloc(sizeof(bouquet_element_list_t));
	if(new == NULL) {
		eprintf("%s()[%d]: Error allocating memory!\n", __func__, __LINE__);
		return NULL;
	}
	memset(new, 0, sizeof(bouquet_element_list_t));
	list_add_tail(&(new->channelsList), listHead);

	return new;
}

static int32_t digitalList_release(void)
{
	struct list_head *pos;
	dprintf("%s[%d]\n", __func__, __LINE__);
	list_for_each(pos, &digitalBouquet.channelsList) {
		bouquet_element_list_t *el = list_entry(pos, bouquet_element_list_t, channelsList);
		if(!list_empty(&el->channelsList)) {
			list_del(&el->channelsList);
		}
		free(el);
	}
	list_for_each(pos, &digitalBouquet.transponderList) {
		transpounder_t *el = list_entry(pos, transpounder_t, transponderList);
		if(!list_empty(&el->transponderList)) {
			list_del(&el->transponderList);
		}
		free(el);
	}
	strList_release(&digitalBouquet.name_tv);
	strList_release(&digitalBouquet.name_radio);
	return 0;
}

#endif // ENABLE_DVB


int32_t parentControl_savePass(const char *value)
{
	unsigned char out[16];
	char out_hex[32];
	int32_t i;

	if((value == NULL) || (strlen(value) < 4)) {
		eprintf("%s(): Error: wrong argument\n", __func__);
		return -1;
	}

	md5((unsigned char*)value, strlen(value), out);
	for(i = 0; i < 16; i++) {
		sprintf(&out_hex[i*2], "%02hhx", out[i]);
	}
	FILE *pass_file = fopen(PARENT_CONTROL_FILE, "w");
	if(pass_file == NULL) {
		eprintf("%s(): Error while oppening file=%s: %m\n", __func__, PARENT_CONTROL_FILE);
		return -1;
	}
	fwrite(out_hex, 32, 1, pass_file);
	fclose(pass_file);

	return 0;
}

int32_t parentControl_checkPass(const char *value)
{
	int32_t i;
	uint8_t out[16];
	char    out_hex[32];
	char    pass[32];
	FILE *pass_file;

	if(value == NULL) {
		eprintf("%s(): Error: wrong argument\n", __func__);
		return -1;
	}
	md5((uint8_t *)value, strlen(value), out);
	for(i = 0; i < 16; i++) {
		sprintf(&out_hex[i * 2], "%02hhx", out[i]);
	}
	pass_file = fopen(PARENT_CONTROL_FILE, "r");
	if(pass_file == NULL) {
		eprintf("%s(): Error while oppening file=%s: %m\n", __func__, PARENT_CONTROL_FILE);
		return -2;
	}
	fread(pass, 32, 1, pass_file);
	fclose(pass_file);

	if(strncmp(out_hex, pass, 32) == 0) {
		return 0;
	}
	return 1;
}

