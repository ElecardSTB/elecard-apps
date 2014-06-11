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
#include "list.h"
#include "debug.h"
#include "off_air.h"
#include "l10n.h"
#include "analogtv.h"
#include "playlist_editor.h"
#include "list.h"

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
//#define BOUQUET_STANDARD_NAME            "" //"Elecard playlist"
#define BOUQUET_SERVICES_FILENAME_TV     BOUQUET_CONFIG_DIR "/" BOUQUET_NAME ".tv"
#define BOUQUET_SERVICES_FILENAME_RADIO  BOUQUET_CONFIG_DIR "/" BOUQUET_NAME ".radio"
#define BOUQUET_LAMEDB_FILENAME          BOUQUET_CONFIG_DIR "/" "lamedb"

#define BOUQUET_NAME_SIZE                64
#define CHANNEL_BUFFER_NAME              64
#define MAX_TEXT                         512

#define GARB_DIR                         "/var/etc/garb"
#define GARB_CONFIG                      GARB_DIR "/config"

#define TMP_BATCH_FILE                   "/tmp/cmd_bouquet"

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct {
	bouquet_data_t bouquet_data;
	EIT_common_t	common;
} bouquet_t;

typedef struct {
	uint32_t ts_id;         //Transpounder_ID
	uint32_t n_id;          //Network_ID
	uint32_t name_space;    //Namespace(media_ID -?)
} transponder_id_t;

typedef struct {
	transponder_id_t transpounder_id;
	EIT_media_config_t media;
} transponder_t;

typedef struct {
	uint32_t s_id;                   //Services_ID
	transponder_id_t transpounder_id;
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
	char               *str;
	struct list_head    list;
} strList_t;

#ifdef ENABLE_DVB
/******************************************************************
* DATA                                                            *
*******************************************************************/
//list_element_t *bouquetNameDigitalList = NULL;
//static list_element_t *bouquetNameAnalogList = NULL;
LIST_HEAD(bouquetNameDigitalList);
LIST_HEAD(bouquetNameAnalogList);


/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static list_element_t *head_ts_list = NULL;
static list_element_t *bouquets_list = NULL;
static int bouquets_coun_list = 0;
static int bouquets_enable = 0;
static list_element_t *bouquet_name_tv = NULL;
static list_element_t *bouquet_name_radio = NULL;
static char bouquetDigitalName[CHANNEL_BUFFER_NAME];
static char bouquetAnalogName[CHANNEL_BUFFER_NAME];
static char pName[CHANNEL_BUFFER_NAME];

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static void bouquet_saveBouquets(char *fileName, char *typeName);
static void bouquet_saveBouquetsConf(char *fileName, char *typeName);
static void bouquet_saveLamedb(char *fileName);
static int32_t bouquet_saveBouquetsList(struct list_head *listHead);
static int32_t bouquet_downloadDigitalConfigList(void);
static int32_t bouquet_downloadAnalogConfigList(void);
static void bouquet_parseBouquetsList(struct list_head *listHead, const char *path);

static void bouquet_getAnalogJsonName(char *fname, const char *name);


/*******************************************************************
* FUNCTION IMPLEMENTATION                                          *
********************************************************************/

void bouquets_setNumberPlaylist(int num)
{
	bouquets_coun_list = num;
}

int bouquets_getNumberPlaylist(void)
{
	return bouquets_coun_list;
}

static void bouquet_saveAllBouquet(void)
{
	char *bouquetName;
	char buffName[128];
	struct stat sb;

	bouquetName = bouquet_getDigitalBouquetName();
	sprintf(buffName, "%s/%s", BOUQUET_CONFIG_DIR, bouquetName);
	if(!(stat(buffName, &sb) == 0 && S_ISDIR(sb.st_mode))) {
		mkdir(buffName, 0777);
	}

	bouquet_saveBouquets(bouquetName, "tv");
	bouquet_saveBouquets(bouquetName, "radio");
	bouquet_saveBouquetsConf(bouquetName, "tv");
	bouquet_saveLamedb(bouquetName);
}

static int bouquet_sendBouquet(void)
{
	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	if(!(helperFileExists(GARB_DIR "/.ssh/id_rsa") && helperFileExists(GARB_DIR "/.ssh/id_rsa.pub"))) {
		eprintf("%s(): No garb private or public key!!!\n", __func__);
		return -1;
	}

	char serverName[16];
	char loginName[32];
	char serverDir[256];
	char cmd[1024];
	int32_t ret = 0;

	getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);

	FILE *fd;
	fd = fopen(TMP_BATCH_FILE, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open %s: %m\n", __FUNCTION__, TMP_BATCH_FILE);
		return 1;
	}
	fprintf(fd, "-mkdir %s/../bouquet/%s.STB/\n -put %s/%s/* %s/../bouquet/%s.STB/\n -put %s/%s %s/../bouquet/",
			serverDir,    bouquetDigitalName,      BOUQUET_CONFIG_DIR, bouquetDigitalName, serverDir, bouquetDigitalName,  BOUQUET_CONFIG_DIR, BOUQUET_CONFIG_FILE, serverDir);
	fclose(fd);

	snprintf(cmd, sizeof(cmd), "sftp -b %s -i /var/etc/garb/.ssh/id_rsa %s@%s", TMP_BATCH_FILE, loginName, serverName);
	ret = dbg_cmdSystem(cmd);
	interface_hideMessageBox();
	return WEXITSTATUS(ret);
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
	//fixed radio type
	list_element_t		*service_element;

	for(service_element = dvb_services; service_element != NULL; service_element = service_element->next) {
		EIT_service_t *curService = (EIT_service_t *)service_element->data;
		if(!(dvbChannel_findServiceCommon(&curService->common))) {
			curService->common.media_id = curService->original_network_id;
			dvbChannel_addService(curService, 1);
		}
	}
	dvbChannel_writeOrderConfig();
	if(bouquet_enable()) {
		bouquet_saveAllBouquet();
	}
}

int bouquets_setAnalogBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	(void)pMenu;
	char *name;
	int number;
	number = CHANNEL_INFO_GET_CHANNEL(pArg);
	name = bouquet_getAnalogBouquetName();
	if((name != NULL) &&
			(strcasecmp(name, strList_get(&bouquetNameAnalogList, number)) == 0)) {
		return 0;
	}
	bouquet_setAnalogBouquetName(strList_get(&bouquetNameAnalogList, number));
	saveAppSettings();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 0;
}

int bouquets_setDigitalBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	(void)pMenu;
	char *name;
	int number;
	number = CHANNEL_INFO_GET_CHANNEL(pArg);
	name = bouquet_getDigitalBouquetName();
	if((name != NULL) &&
			(strcasecmp(name, strList_get(&bouquetNameDigitalList, number)) == 0)) {
		return 0;
	}
	dvbChannel_terminate();
	free_services(&dvb_services);
	bouquet_setDigitalBouquetName(strList_get(&bouquetNameDigitalList, number));
	saveAppSettings();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 0;
}

void bouquet_setDigitalBouquetName(const char *name)
{
	sprintf(bouquetDigitalName, "%s", name);
}

void bouquet_setAnalogBouquetName(const char *name)
{
	bouquet_loadBouquet(eBouquet_analog, name);
	sprintf(bouquetAnalogName, "%s", name);
}

void bouquet_setNewBouquetName(char *name)
{
	char buffName[64];
	char cmd[1024];

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	sprintf(buffName, "%s/%s", BOUQUET_CONFIG_DIR, name);
	sprintf(bouquetDigitalName, "%s", name);
	if(bouquet_getDigitalBouquetName() == NULL) {
		strList_add(&bouquetNameDigitalList, name);

		snprintf(cmd, sizeof(cmd), "mkdir -p %s", buffName);
		dbg_cmdSystem(cmd);
		bouquet_saveBouquetsList(&bouquetNameDigitalList);
	}
	interface_hideMessageBox();
}

char *bouquet_getDigitalBouquetName(void)
{
	if(bouquet_enable()) {
		return bouquetDigitalName;
	}
	return NULL;
}

char *bouquet_getAnalogBouquetName(void)
{
	if(bouquet_enable()) {
		return bouquetAnalogName;
	}
	return NULL;
}


void bouquet_getDigitalName(char *dir, char *fname, char *name)
{
	sprintf(fname, "%s/offair.%s%s", dir, (name == NULL ? "" : name), (name == NULL ? "conf" : ".conf"));
}

static void bouquet_getAnalogJsonName(char *fname, const char *name)
{
	if(name == NULL) {
		sprintf(fname, "analog.json");
	} else {
		sprintf(fname, "analog.%s.json", name);
	}
}

void get_bouquets_file_name(list_element_t **bouquet_name, char *bouquet_file)
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
		list_element_t *cur_element;
		char *element_data;

		if(strncasecmp(buf, "#SERVICE", 8) != 0) {
			continue;
		}

		if(*bouquet_name == NULL) {
			cur_element = *bouquet_name = allocate_element(BOUQUET_NAME_SIZE);
		} else {
			cur_element = append_new_element(*bouquet_name, BOUQUET_NAME_SIZE);
		}
		if(!cur_element) {
			break;
		}

		element_data = (char *)cur_element->data;

		char *ptr;
		ptr = strchr(buf, '"');
		if(ptr) {
			sscanf(ptr + 1, "%s \n", element_data); //get bouquet_name type: name" (with ")
			element_data[strlen(element_data) - 1] = '\0'; //get bouquet_name type: name
			dprintf("Get bouquet file name: %s\n", element_data);
		}
	}
	fclose(fd);
}

transponder_t *found_transpounder(EIT_common_t	*common, uint32_t name_space)
{
	list_element_t *cur_tr_element;
	transponder_t *element_tr;

	for(cur_tr_element = head_ts_list; cur_tr_element != NULL; cur_tr_element = cur_tr_element->next) {
		element_tr = (transponder_t *)cur_tr_element->data;

		if(element_tr == NULL) {
			continue;
		}
		if(name_space == element_tr->transpounder_id.name_space &&
				common->transport_stream_id == element_tr->transpounder_id.ts_id &&
				common->media_id == element_tr->transpounder_id.n_id) {
			return element_tr;
		}
	}
	return NULL;
}

int bouquets_found(EIT_common_t *common)
{
	list_element_t *found;
	for(found = bouquets_list; found != NULL; found = found->next) {
		bouquet_t *curService = (bouquet_t *)found->data;
		if(common->service_id ==  curService->common.service_id &&
				common->transport_stream_id ==  curService->common.transport_stream_id) {
			//if(memcmp((common), &(curService->common), sizeof(EIT_common_t)) == 0) {
			return true;
		}
	}
	return false;
}
void filter_bouquets_list(char *bouquet_file)
{
	struct list_head *pos;
	extern dvb_channels_t g_dvb_channels;
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);

		if(!(bouquets_found(&(srv->common)))) {
			dvbChannel_remove(srv);
		}
	}
	free_elements(&bouquets_list);
}

void get_bouquets_list(char *bouquet_file)
{
	list_element_t *cur_element;
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
		if(service_id == 0 && transport_stream_id == 0 && network_id == 0) {
			continue;
		}

		bouquet_t *element;

		if(bouquets_list == NULL) {
			cur_element  = bouquets_list = allocate_element(sizeof(bouquet_t));
		} else {
			cur_element = append_new_element(bouquets_list, sizeof(bouquet_t));
		}
		if(cur_element == NULL) {
			break;
		}
		element = (bouquet_t *)cur_element->data;
		element->bouquet_data.serviceType = serviceType;
		element->bouquet_data.network_id = network_id;
		element->common.service_id = service_id;
		element->common.transport_stream_id = transport_stream_id;
		element->common.media_id = network_id;

		service_index_t *p_srvIdx;
		p_srvIdx = dvbChannel_findServiceCommon(&element->common);
		if(p_srvIdx == NULL) {
			dvbChannel_addBouquetData(&element->common, &element->bouquet_data, /*visible*/ true);
		}
	}
	fclose(fd);
}

int bouquets_compare(list_element_t **services)
{
	list_element_t	*service_element;

	if(dvbChannel_getCount() != dvb_getCountOfServices()) {
		return 0;
	}

	for(service_element = *services; service_element != NULL; service_element = service_element->next) {
		EIT_service_t *curService = (EIT_service_t *)service_element->data;
		if(dvbChannel_findServiceCommon(&curService->common) == NULL) {
			return 0;
		}
	}
	return 1;
}

void get_addStandardPlaylist(list_element_t **bouquet_name, char *bouquet_file)
{
	list_element_t *cur_element;
	char *element_data;
	free_elements(&*bouquet_name);

	if(*bouquet_name == NULL) {
		cur_element = *bouquet_name = allocate_element(BOUQUET_NAME_SIZE);
	} else {
		cur_element = append_new_element(*bouquet_name, BOUQUET_NAME_SIZE);
	}
	if(!cur_element) {
		return;
	}

	element_data = (char *)cur_element->data;
	sprintf(element_data, "%s", bouquet_file);
}

int bouquet_enable(void)
{
	return bouquets_enable;
}

void bouquet_setEnable(int i)
{
	bouquets_enable = i;
}

int bouquet_getFile(char *fileName)
{
	//bouquet_downloadFileWithServices(CONFIG_DIR);
	struct stat sb;
	if(stat(fileName, &sb) == 0) {
		if(S_ISDIR(sb.st_mode)) {
			return false;
		}
		return true;
	}
	return false;
}

int bouquet_getFolder(char *bouquetsFile)
{
	char buffName[256];
	sprintf(buffName, "%s/%s", BOUQUET_CONFIG_DIR, bouquetsFile);
	struct stat sb;

	if(stat(buffName, &sb) == 0 && S_ISDIR(sb.st_mode)) {
		return 1;
	}
	return 0;
}

void bouquet_saveLamedb(char *fileName)
{
	dprintf("Save services list in lamedb\n");
	char dirName[BUFFER_SIZE];
	extern dvb_channels_t g_dvb_channels;
	struct list_head *pos;
	list_element_t *cur_element;
	transponder_t *element;
	FILE *fd;

	sprintf(dirName, "%s/%s/lamedb", BOUQUET_CONFIG_DIR , fileName);
	fd = fopen(dirName, "wb");

	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, dirName);
		return;
	}

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

		element = found_transpounder(&(srvIdx->common), NAME_SPACE);

		if(element != NULL) {
			continue;
		}

		if(head_ts_list == NULL) {
			cur_element = head_ts_list = allocate_element(sizeof(transponder_t));
		} else {
			cur_element = append_new_element(head_ts_list, sizeof(transponder_t));
		}
		if(cur_element == NULL) {
			break;
		}

		element = (transponder_t *)cur_element->data;

		element->transpounder_id.name_space = NAME_SPACE;
		element->transpounder_id.ts_id = srvIdx->service->common.transport_stream_id;
		element->transpounder_id.n_id = srvIdx->service->original_network_id;

		memcpy(&element->media, &srvIdx->service->media, sizeof(EIT_media_config_t));
	}

	fprintf(fd, "eDVB services /4/\n");
	fprintf(fd, "transponders\n");
	for(cur_element = head_ts_list; cur_element != NULL; cur_element = cur_element->next) {
		element = (transponder_t *)cur_element->data;
		fprintf(fd, "%08x:%04x:%04x\n", element->transpounder_id.name_space, element->transpounder_id.ts_id, element->transpounder_id.n_id);
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
	free_elements(&head_ts_list);

	fprintf(fd, "services\n");
	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		fprintf(fd, "%04x:%08x:%04x:%04x:%d:0\n", srvIdx->common.service_id, NAME_SPACE, srvIdx->service->common.transport_stream_id, srvIdx->service->original_network_id, srvIdx->service->service_descriptor.service_type);
		fprintf(fd, "%s\n", srvIdx->data.channelsName);
		fprintf(fd, "p:%s,f:40\n", fileName);
	}
	fprintf(fd, "end\n");
	fclose(fd);
}

void bouquet_saveBouquets(char *fileName, char *typeName)
{
	FILE *fd;
	char bouquet_file[BUFFER_SIZE];

	sprintf(bouquet_file, "%s/%s/bouquets.%s", BOUQUET_CONFIG_DIR , fileName, typeName);
	fd = fopen(bouquet_file, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	fprintf(fd, "#NAME Bouquets (%s)\n", typeName);
	fprintf(fd, "#SERVICE 1:7:1:0:0:0:0:0:0:0:FROM BOUQUET \"userbouquet.%s.%s\" ORDER BY bouquet\n", fileName, typeName);
	fclose(fd);
}

void bouquet_saveBouquetsConf(char *fileName, char *typeName)
{
	dprintf("Save services list in Bouquets\n");

	FILE *fd;
	char bouquet_file[BUFFER_SIZE];
	extern dvb_channels_t g_dvb_channels;
	struct list_head *pos;

	sprintf(bouquet_file, "%s/%s/userbouquet.%s.%s", BOUQUET_CONFIG_DIR, fileName, fileName, typeName);
	fd = fopen(bouquet_file, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	fprintf(fd, "#NAME %s\n", fileName);

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if(srvIdx->data.visible)
			fprintf(fd, "#SERVICE 1:0:%d:%x:%x:%x:%08x:0:0:0:\n",
					srvIdx->service->service_descriptor.service_type,
					srvIdx->common.service_id,
					srvIdx->common.transport_stream_id,
					srvIdx->service->original_network_id,
					NAME_SPACE);

	}
	fclose(fd);
}
void bouquet_stashBouquet(typeBouquet_t index, const char *name)
{
	char cmd[256];
	char fname[256];
	if(index == eBouquet_analog) {
		bouquet_getAnalogJsonName(fname , name);
		snprintf(cmd, sizeof(cmd), "cp %s/../%s %s/%s", BOUQUET_CONFIG_DIR_ANALOG, BOUQUET_ANALOG_MAIN_FILE,
				 BOUQUET_CONFIG_DIR_ANALOG, fname);
	}
	dbg_cmdSystem(cmd);
}

void bouquet_loadBouquet(typeBouquet_t index, const char *name)
{
	char cmd[256];
	char fname[256];
	if(index == eBouquet_analog) {
		bouquet_getAnalogJsonName(fname , name);
		analogtv_removeServiceList(0);
		snprintf(cmd, sizeof(cmd), "cp %s/%s %s/../%s", BOUQUET_CONFIG_DIR_ANALOG, fname,
				 BOUQUET_CONFIG_DIR_ANALOG, BOUQUET_ANALOG_MAIN_FILE);
	}
	dbg_cmdSystem(cmd);
}

int bouquet_enableControl(interfaceMenu_t *pMenu, void *pArg)
{
	if(bouquet_enable()) {
		bouquet_setEnable(0);
		bouquet_loadBouquet(eBouquet_analog, NULL);
	} else {
		bouquet_setEnable(1);
		bouquet_stashBouquet(eBouquet_analog, NULL);
	}
	saveAppSettings();
	dvbChannel_terminate();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 1;
}

int bouquet_createNewBouquet(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	if(value == NULL) {
		return 0;
	}
	dvbChannel_terminate();
	free_services(&dvb_services);
	bouquet_setNewBouquetName(value);
	saveAppSettings();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 0;
}

int bouquet_removeBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	char *bouquetName;
	bouquetName = bouquet_getDigitalBouquetName();
	dprintf("%s\n", bouquetName);

	if(bouquetName != NULL) {
		interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "rm -r %s/%s/", BOUQUET_CONFIG_DIR, bouquetName);
		dbg_cmdSystem(cmd);
		interface_hideMessageBox();
		strList_remove(&bouquetNameDigitalList, bouquetName);
		bouquet_loadDigitalBouquetsList(1);
	}
	free_services(&dvb_services);
	dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
#if (defined STSDK)
	elcdRpcType_t type;
	cJSON *result = NULL;
	st_rpcSync(elcmd_dvbclearservices, NULL, &type, &result);
	cJSON_Delete(result);
#endif //#if (defined STSDK)

	dvbChannel_terminate();
	dvbChannel_writeOrderConfig();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 0;
}

int bouquet_updateAnalogBouquetList(interfaceMenu_t *pMenu, void *pArg)
{
	bouquet_loadAnalogBouquetsList(1);
	return 0;
}

int bouquet_updateAnalogBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	char *bouquetName;
	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	bouquetName = bouquet_getAnalogBouquetName();
	if(bouquetName != NULL) {
		char serverName[16];
		char serverDir[256];
		//			char filename[256];
		char loginName[32];
		char cmd[1024];

		getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
		getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
		getParam(GARB_CONFIG, "SERVER_IP", "", serverName);
		snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/../analog/analog.%s.json %s/analog.%s.json", loginName, serverName, serverDir, bouquetName, BOUQUET_CONFIG_DIR_ANALOG, bouquetName);
		dbg_cmdSystem(cmd);
	}
	char *name = bouquet_getAnalogBouquetName();
	if(name != NULL) {
		bouquet_setAnalogBouquetName(name);
	}
	interface_hideMessageBox();
	offair_fillDVBTMenu();
	return 0;
}

int bouquet_updateDigitalBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	char *bouquetName;
	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);

	bouquet_loadDigitalBouquetsList(1);
	bouquetName = bouquet_getDigitalBouquetName();
	if(bouquetName != NULL) {
		char serverName[16];
		char serverDir[256];
		char filename[256];
		char loginName[32];
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "mv %s/%s/ %s/%s_temp", BOUQUET_CONFIG_DIR, bouquetName, BOUQUET_CONFIG_DIR, bouquetName);
		dbg_cmdSystem(cmd);
		getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
		getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
		getParam(GARB_CONFIG, "SERVER_IP", "", serverName);

		snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/../bouquet/%s.%s %s/%s", loginName, serverName, serverDir, bouquetName, "STB", BOUQUET_CONFIG_DIR, bouquetName);
		dbg_cmdSystem(cmd);

		sprintf(filename , "%s/%s", BOUQUET_CONFIG_DIR, bouquetName);
		struct stat sb;
		if(!(stat(filename, &sb) == 0 || S_ISDIR(sb.st_mode))) {
			snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/../bouquet/%s %s/%s", loginName, serverName, serverDir, bouquetName, BOUQUET_CONFIG_DIR, bouquetName);
			dbg_cmdSystem(cmd);
		}

		if(!(stat(filename, &sb) == 0  || S_ISDIR(sb.st_mode))) {
			snprintf(cmd, sizeof(cmd), "mv %s/%s_temp/ %s/%s", BOUQUET_CONFIG_DIR, bouquetName, BOUQUET_CONFIG_DIR, bouquetName);
		} else {
			snprintf(cmd, sizeof(cmd), "rm -r %s/%s_temp/", BOUQUET_CONFIG_DIR, bouquetName);
		}
		dbg_cmdSystem(cmd);
	}
	interface_hideMessageBox();
	offair_fillDVBTMenu();
	return 0;
}

int bouquet_saveAnalogMenuBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	char serverName[16];
	char loginName[32];
	char serverDir[256];
	char cmd[1024];
	int32_t ret = 0;

	char *bouquetName = bouquet_getAnalogBouquetName();
	if(bouquetName == NULL) {
		eprintf("%s(): Noе selected bouquet!!!\n", __func__);
		return -1;
	}

	if(!(helperFileExists(GARB_DIR "/.ssh/id_rsa") && helperFileExists(GARB_DIR "/.ssh/id_rsa.pub"))) {
		eprintf("%s(): No garb private or public key!!!\n", __func__);
		return -1;
	}

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	bouquet_stashBouquet(eBouquet_analog, bouquetName);

	getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);

	FILE *fd;

	fd = fopen(TMP_BATCH_FILE, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open %s: %m \n", __FUNCTION__, TMP_BATCH_FILE);
		return 1;
	}
	fprintf(fd, "-put %s/analog.%s.json %s/../analog/analog.%s.json",
			BOUQUET_CONFIG_DIR_ANALOG, bouquetName, serverDir, bouquetName);
	fclose(fd);

	snprintf(cmd, sizeof(cmd), "sftp -b %s -i /var/etc/garb/.ssh/id_rsa %s@%s", TMP_BATCH_FILE, loginName, serverName);
	ret = dbg_cmdSystem(cmd);
	interface_hideMessageBox();
	return WEXITSTATUS(ret);
}

void bouquet_saveAnalogBouquet(void)
{
	char cmd[1024];
	char fname[BUFFER_SIZE];

	bouquet_stashBouquet(eBouquet_analog, bouquet_getAnalogBouquetName());
	snprintf(cmd, sizeof(cmd), "cp %s/../analog.json %s", BOUQUET_CONFIG_DIR_ANALOG, fname);
	dbg_cmdSystem(cmd);

	offair_fillDVBTMenu();
}

int bouquet_saveDigitalBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	bouquet_loadDigitalBouquetsList(1);
	bouquet_saveBouquetsList(&bouquetNameDigitalList);
	bouquet_saveAllBouquet();
	bouquet_sendBouquet();
	offair_fillDVBTMenu();
	return 0;
}

void bouquet_loadBouquets(list_element_t **services)
{
	char *bouquetName;
	bouquetName = bouquet_getDigitalBouquetName();
	if(bouquetName != NULL && bouquet_getFolder(bouquetName)) {
		char fileName[1024];
		free_elements(&bouquet_name_tv);
		free_elements(&bouquet_name_radio);
		snprintf(fileName, sizeof(fileName), "%s/%s/%s.%s", BOUQUET_CONFIG_DIR, bouquetName, BOUQUET_NAME, "tv");
		get_bouquets_file_name(&bouquet_name_tv, fileName);
		snprintf(fileName, sizeof(fileName), "%s/%s/%s.%s", BOUQUET_CONFIG_DIR, bouquetName, BOUQUET_NAME, "radio");
		get_bouquets_file_name(&bouquet_name_radio, fileName);
		if(bouquet_name_tv != NULL) {
			snprintf(fileName, sizeof(fileName), "%s/%s/%s", BOUQUET_CONFIG_DIR, bouquetName, (char *)bouquet_name_tv->data);
			get_bouquets_list(fileName/*tv*/);
		}
		if(bouquet_name_radio != NULL) {
			snprintf(fileName, sizeof(fileName), "%s/%s/%s", BOUQUET_CONFIG_DIR, bouquetName, (char *)bouquet_name_radio->data);
			get_bouquets_list(fileName/*radio*/);
		}
		filter_bouquets_list(bouquetName);
		bouquet_loadLamedb(bouquetName, services);

	}
}

int32_t bouquet_downloadAnalogConfigList(void)
{
	char serverName[16];
	char serverDir[256];
	char loginName[32];
	char cmd[1024];

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);

	getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);

	snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa %s@%s:%s/../analog/%s %s", loginName, serverName, serverDir, BOUQUET_CONFIG_FILE_ANALOG, BOUQUET_CONFIG_DIR_ANALOG);
	dbg_cmdSystem(cmd);
	interface_hideMessageBox();
	return 0;
}

int32_t bouquet_downloadDigitalConfigList(void)
{
	char serverName[16];
	char serverDir[256];
	char filename[256];
	char filename_tmp[256];
	char loginName[32];
	char cmd[1024];

	snprintf(filename, sizeof(filename), "%s/%s", BOUQUET_CONFIG_DIR, BOUQUET_CONFIG_FILE);
	snprintf(filename_tmp, sizeof(filename_tmp), "%s_temp", filename);

	if(rename(filename, filename_tmp) != 0) {
		eprintf("%s(): Cant rename file %s into %s: %m\n", __func__, filename, filename_tmp);
		return -1;
	}

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);

	snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa %s@%s:%s/../bouquet/%s %s", loginName, serverName, serverDir, BOUQUET_CONFIG_FILE, BOUQUET_CONFIG_DIR);
	dbg_cmdSystem(cmd);

	if(!helperFileExists(filename)) {
		snprintf(cmd, sizeof(cmd), "mv %s_temp %s", filename, filename);
		dbg_cmdSystem(cmd);
		if(rename(filename_tmp, filename) != 0) {
			eprintf("%s(): Cant rename file %s into %s: %m\n", __func__, filename_tmp, filename);
			interface_hideMessageBox();
			return -2;
		}
	}
	interface_hideMessageBox();
	return 0;
}

void bouquet_init(void)
{
	struct stat sb;
	if(!(stat(BOUQUET_CONFIG_DIR, &sb) == 0 && S_ISDIR(sb.st_mode))) {
		mkdir(BOUQUET_CONFIG_DIR, 0777);
	}

	if(!(stat(BOUQUET_CONFIG_DIR_ANALOG, &sb) == 0 && S_ISDIR(sb.st_mode))) {
		mkdir(BOUQUET_CONFIG_DIR_ANALOG, 0777);
	}
}

static int32_t bouquet_saveBouquetsList(struct list_head *listHead)
{
	FILE *fd;
	char buffName[256];
	const char *name;
	uint32_t num = 0;

	snprintf(buffName, sizeof(buffName), "%s/%s", BOUQUET_CONFIG_DIR, BOUQUET_CONFIG_FILE);
	fd = fopen(buffName, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, buffName);
		return -1;
	}

/*	struct list_head *pos;
	list_for_each(pos, listHead) {
		strList_t *el = list_entry(pos, strList_t, list);
		fprintf(fd, "#BOUQUETS_NAME=%s\n", el->str);
	}*/
	while((name = strList_get(listHead, num)) != NULL) {
		fprintf(fd, "#BOUQUETS_NAME=%s\n", name);
		num++;
	}

	fclose(fd);
	return 0;
}

void bouquet_parseBouquetsList(struct list_head *listHead, const char *path)
{
	char buf[BUFFER_SIZE];
	FILE *fd;

	fd = fopen(path, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, path);
		return;
	}
	// check head file name
	while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		char name[256];
		if((sscanf(buf, "#BOUQUETS_NAME=%s", name) == 1) || (sscanf(buf, "#ANALOG_NAME=%s", name) == 1)) {
			if(!strList_isExist(listHead, name)) {
				strList_add(listHead, name);
			}
		}
	}
	fclose(fd);
}

void bouquet_loadDigitalBouquetsList(int force)
{
	if(force == 1) {
		bouquet_downloadDigitalConfigList();
	}

	char buffName[256];
	sprintf(buffName, "%s/%s", BOUQUET_CONFIG_DIR, BOUQUET_CONFIG_FILE);
	bouquet_parseBouquetsList(&bouquetNameDigitalList, buffName);
	sprintf(buffName, "%s/%s_temp", BOUQUET_CONFIG_DIR, BOUQUET_CONFIG_FILE);
	bouquet_parseBouquetsList(&bouquetNameDigitalList, buffName);
}

void bouquet_loadAnalogBouquetsList(int force)
{
	char buffName[256];
	if(force == 1) {
		bouquet_downloadAnalogConfigList();
	}

	sprintf(buffName, "%s/%s", BOUQUET_CONFIG_DIR_ANALOG, BOUQUET_CONFIG_FILE_ANALOG);
	bouquet_parseBouquetsList(&bouquetNameAnalogList, buffName);
}


void bouquet_loadChannelsFile(void)
{
	get_bouquets_file_name(&bouquet_name_tv, BOUQUET_SERVICES_FILENAME_TV);
	get_bouquets_file_name(&bouquet_name_radio, BOUQUET_SERVICES_FILENAME_RADIO);
}

list_element_t *found_list(EIT_common_t *common, list_element_t **services)
{

	list_element_t	*service_element;
	for(service_element = *services; service_element != NULL; service_element = service_element->next) {
		EIT_service_t *element = (EIT_service_t *)service_element->data ;
		if(memcmp(&(element->common), common, sizeof(EIT_common_t)) == 0) {
			return service_element;
		}
	}
	return NULL;
}

void bouquet_loadLamedb(char *bouquet_file, list_element_t **services)
{
	dprintf("Load services list from lamedb\n");
	char path[BOUQUET_NAME_SIZE * 2];
	char buf[BUFFER_SIZE];
	FILE *fd;
	sprintf(path, "%s/%s/lamedb", BOUQUET_CONFIG_DIR, bouquet_file);
	fd = fopen(path, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, BOUQUET_LAMEDB_FILENAME);
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
			uint32_t name_space;
			uint32_t ts_id;
			uint32_t n_id;
			if(sscanf(buf, "%x:%x:%x\n", &name_space, &ts_id, &n_id) != 3) {
				break;
			}

			list_element_t *cur_element;
			transponder_t *element;

			if(head_ts_list == NULL) {
				cur_element = head_ts_list = allocate_element(sizeof(transponder_t));
			} else {
				cur_element = append_new_element(head_ts_list, sizeof(transponder_t));
			}
			if(cur_element == NULL) {
				break;
			}

			element = (transponder_t *)cur_element->data;
			element->transpounder_id.name_space = name_space;
			element->transpounder_id.ts_id = ts_id;
			element->transpounder_id.n_id = n_id;

			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}

			switch(buf[1]) {
				case 's': {
					uint32_t freq, sym_rate, polarization, FEC_inner, orbital_position, inversion/*0 - auto, 1 - on, 2 - off*/, system/*0 - DVB-C, 1 DVB-C ANNEX C*/, modulation, rolloff, pilot;

					element->media.type = serviceMediaDVBS;
					sscanf(buf + 3, "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d\n",
						   &freq, &sym_rate, &polarization, &FEC_inner, &orbital_position, &inversion, &system, &modulation, &rolloff, &pilot);

					element->media.frequency = freq;
					element->media.dvb_s.frequency = freq;
					element->media.dvb_s.symbol_rate = sym_rate;
					element->media.dvb_s.polarization = polarization;
					element->media.dvb_s.FEC_inner = FEC_inner;
					element->media.dvb_s.orbital_position = orbital_position;
					element->media.dvb_s.inversion = (inversion == 2 ? 0 : 1);
					//system
					//modulation
					//rolloff
					//pilot
					break;
				}
				case 'c': {
					uint32_t freq, sym_rate, inversion, mod, fec_inner, flag, system;

					element->media.type = serviceMediaDVBC;
					sscanf(buf + 3, "%d:%d:%d:%d:%d:%d:%d\n",
						   &freq, &sym_rate, &inversion, &mod, &fec_inner, &flag, &system);

					element->media.type = serviceMediaDVBC;
					element->media.frequency = freq * 1000;
					element->media.dvb_c.frequency = freq * 1000;
					element->media.dvb_c.symbol_rate = sym_rate;
					element->media.dvb_c.modulation = mod;
					element->media.dvb_c.inversion = (inversion == 2 ? 0 : 1);
					break;
				}
				case 't': {
					uint32_t freq, bandwidth, code_rate_HP, code_rate_LP, modulation, transmission_mode, guard_interval, hierarchy, inversion, flags, system, plpid;

					element->media.type = serviceMediaDVBT;
					sscanf(buf + 3, "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
						   &freq, &bandwidth, &code_rate_HP, &code_rate_LP, &modulation, &transmission_mode, &guard_interval, &hierarchy, &inversion, &flags, &system, &plpid);

					element->media.frequency = freq;
					element->media.dvb_t.centre_frequency = freq;
					element->media.dvb_t.bandwidth = bandwidth;
					element->media.dvb_t.code_rate_HP_stream = code_rate_HP;
					element->media.dvb_t.code_rate_LP_stream = code_rate_LP;
					//modulation
					element->media.dvb_t.transmission_mode = transmission_mode;
					element->media.dvb_t.guard_interval = guard_interval;
					element->media.dvb_t.hierarchy_information = hierarchy;
					element->media.dvb_t.inversion = (inversion == 2 ? 0 : 1);
					//flags
					//system
					element->media.dvb_t.plp_id = plpid;
					break;
				}

			}

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
			uint32_t service_id;
			uint32_t name_space;
			uint32_t transport_stream_id;
			uint32_t original_network_id;
			uint32_t serviceType;
			uint32_t hmm;

			if(sscanf(buf, "%x:%x:%x:%x:%x:%x\n", &service_id, &name_space, &transport_stream_id, &original_network_id, &serviceType, &hmm) != 6) {
				break;
			}
			//parse channels name
			char service_name[MAX_TEXT];
			if(fgets(service_name, BUFFER_SIZE, fd) == NULL) {
				break;
			}

			service_name[strlen(service_name) - 1] = '\0';
			//parese bouquet pName
			char bouqName[CHANNEL_BUFFER_NAME];
			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}
			memset(bouqName, 0, strlen(bouqName));
			sscanf(buf, "p:%s\n", bouqName);

			if(fgets(buf, BUFFER_SIZE, fd) == NULL) {
				break;
			}

			if((strlen(bouqName) < 1) && (strncasecmp(pName, bouqName, strlen((pName))) != 0)) {
				continue;
			}

			EIT_common_t	common;
			common.service_id = service_id;
			common.transport_stream_id = transport_stream_id;
			common.media_id  = original_network_id;

			transponder_t *element_tr;
			element_tr = found_transpounder(&common, name_space);
			if(element_tr == NULL) {
				continue;
			}

			EIT_service_t *element;
			list_element_t *service_element;
			service_element = found_list(&common, &*services);

			if(service_element != NULL) {
				element = (EIT_service_t *)service_element->data;
				if(element->media.type == serviceMediaDVBC && element_tr->media.type == serviceMediaDVBC &&
						element->media.dvb_c.frequency == element_tr->media.dvb_c.frequency &&
						element->media.dvb_c.symbol_rate == element_tr->media.dvb_c.symbol_rate &&
						element->media.dvb_c.modulation == element_tr->media.dvb_c.modulation &&
						element->media.dvb_c.inversion == element_tr->media.dvb_c.inversion) {
					continue;
				}
				if(element->media.type == serviceMediaDVBS && element_tr->media.type == serviceMediaDVBS &&
						element->media.dvb_s.frequency == element_tr->media.dvb_s.frequency &&
						element->media.dvb_s.symbol_rate == element_tr->media.dvb_s.symbol_rate &&
						element->media.dvb_s.polarization == element_tr->media.dvb_s.polarization &&
						//element->media.dvb_s.FEC_inner == element_tr->media.dvb_s.FEC_inner &&
						//element->media.dvb_s.orbital_position == element_tr->media.dvb_s.orbital_position &&
						element->media.dvb_s.inversion == element_tr->media.dvb_s.inversion) {
					continue;
				}
				if(element->media.type == serviceMediaDVBT && element_tr->media.type == serviceMediaDVBT &&
						element->media.dvb_t.centre_frequency == element_tr->media.dvb_t.centre_frequency &&
						element->media.dvb_t.bandwidth == element_tr->media.dvb_t.bandwidth &&
						element->media.dvb_t.inversion == element_tr->media.dvb_t.inversion) {
					continue;
				}
				remove_element(&*services, service_element);
			}
			list_element_t *cur_element = NULL;
			if(*services == NULL) {
				*services = cur_element = allocate_element(sizeof(EIT_service_t));
			} else {
				cur_element = append_new_element(*services, sizeof(EIT_service_t));
			}

			if(!cur_element) {
				break;
			}
			element = (EIT_service_t *)cur_element->data;

			element->common.service_id = common.service_id;
			element->common.transport_stream_id = common.transport_stream_id;
			element->common.media_id  = common.media_id;
			element->original_network_id = common.media_id;
			element->service_descriptor.service_type = serviceType;

			memcpy(&element->service_descriptor.service_name, &service_name, strlen(service_name));

			if(element_tr->media.type == serviceMediaDVBC) {
				element->media.type = element_tr->media.type;
				element->media.dvb_c.frequency = element_tr->media.dvb_c.frequency;
				element->media.dvb_c.symbol_rate = element_tr->media.dvb_c.symbol_rate;
				element->media.dvb_c.modulation = element_tr->media.dvb_c.modulation;
			}
			if(element_tr->media.type == serviceMediaDVBS) {
				element->media.type = element_tr->media.type;
				element->media.frequency = element_tr->media.frequency;
				element->media.dvb_s.frequency = element_tr->media.dvb_s.frequency;
				element->media.dvb_s.symbol_rate = element_tr->media.dvb_s.symbol_rate;
				element->media.dvb_s.polarization = element_tr->media.dvb_s.polarization;
				element->media.dvb_s.FEC_inner = element_tr->media.dvb_s.FEC_inner;
				element->media.dvb_s.orbital_position = element_tr->media.dvb_s.orbital_position;
				element->media.dvb_s.inversion = element_tr->media.dvb_s.inversion;
			}
			if(element_tr->media.type == serviceMediaDVBT) {
				element->media.type = element_tr->media.type;
				element->media.frequency = element_tr->media.frequency;
				element->media.dvb_t.centre_frequency = element_tr->media.dvb_t.centre_frequency;
				element->media.dvb_t.bandwidth = element_tr->media.dvb_t.bandwidth;
				element->media.dvb_t.code_rate_HP_stream = element_tr->media.dvb_t.code_rate_HP_stream;
				element->media.dvb_t.code_rate_LP_stream = element_tr->media.dvb_t.code_rate_LP_stream;
				element->media.dvb_t.constellation = element_tr->media.dvb_t.constellation;
				element->media.dvb_t.generation = element_tr->media.dvb_t.generation;
				element->media.dvb_t.guard_interval = element_tr->media.dvb_t.guard_interval;
				element->media.dvb_t.hierarchy_information = element_tr->media.dvb_t.hierarchy_information;
				element->media.dvb_t.inversion = element_tr->media.dvb_t.inversion;
				element->media.dvb_t.plp_id = element_tr->media.dvb_t.plp_id;
				element->media.dvb_t.transmission_mode = element_tr->media.dvb_t.transmission_mode;
				element->media.dvb_t.other_frequency_flag = element_tr->media.dvb_t.other_frequency_flag;
			}
		} while(strncasecmp(buf, "end", 3) != 0);
	} while(0);

	free_elements(&head_ts_list);
	fclose(fd);
}

int bouquet_file()
{
	struct stat sb;
	if(stat(BOUQUET_SERVICES_FILENAME_TV, &sb) == 0 || stat(BOUQUET_SERVICES_FILENAME_RADIO, &sb) == 0) {
		return true;
	}
	return false;
}

void bouquet_downloadFileFromServer(char *shortname, char *fullname)
{
	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	char serverName[16];
	char serverDir[256];
	char loginName[32];
	char cmd[1024];
	getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);
	snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/../channels/%s %s", loginName, serverName, serverDir, shortname, fullname);
	dbg_cmdSystem(cmd);
	interface_hideMessageBox();
}

void bouquet_downloadFileWithServices(char *filename)
{
	printf("%s[%d]\n", __func__, __LINE__);
	//    struct stat sb;
	//  if(stat(filename, &sb) != 0) {
	//      printf("%s[%d]\n", __func__, __LINE__);
	bouquet_downloadFileFromServer("bouquet", filename);
	//  }
}

#endif // ENABLE_DVB

int32_t strList_add(struct list_head *listHead, const char *str)
{
	strList_t *newName;
	if(!listHead || !str) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -1;
	}

	newName = malloc(sizeof(strList_t));
	if(!newName) {
		eprintf("%s(): Allocation error!\n", __func__);
		return -2;
	}
	newName->str = strdup(str);
	if(!newName->str) {
		eprintf("%s(): Cat duplicate str=%s!\n", __func__, str);
		free(newName);
		return -3;
	}
	list_add_tail(&newName->list, listHead);

	return 0;
}

int32_t strList_remove(struct list_head *listHead, const char *str)
{
	struct list_head *pos;
	if(!listHead || !str) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -1;
	}
	list_for_each(pos, listHead) {
		strList_t *el = list_entry(pos, strList_t, list);
		if(strcasecmp(el->str, str) == 0) {
			dprintf("%s: %s\n", __func__, str);
			list_del(pos);
			if(el->str) {
				free(el->str);
			} else {
				eprintf("%s(): Something wrong, element has no str!\n", __func__);
			}
			free(el);
			return 1;
		}
	}

	return 0;
}

int32_t strList_isExist(struct list_head *listHead, const char *str)
{
	struct list_head *pos;
	if(!listHead || !str) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return 0;
	}
	list_for_each(pos, listHead) {
		strList_t *el = list_entry(pos, strList_t, list);
		//CHECK: is there need to ignore case???
		if(strcasecmp(el->str, str) == 0) {
			dprintf("%s: %s\n", __func__, str);
			return 1;
		}
	}

	return 0;
}

const char *strList_get(struct list_head *listHead, uint32_t number)
{
	struct list_head *pos;
	uint32_t id = 0;
	if(!listHead) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return NULL;
	}
	list_for_each(pos, listHead) {
		if(id == number) {
			strList_t *el = list_entry(pos, strList_t, list);
			return el->str;
		}
		id++;
	}
	return NULL;
}
