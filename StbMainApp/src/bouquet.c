/*
 bouquet.c

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

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "bouquet.h"

#include "dvbChannel.h"
#include "list.h"
#include "debug.h"
#include "off_air.h"
#include "l10n.h"

#include "stsdk.h"
#include <cJSON.h>

#ifdef ENABLE_DVB

/***********************************************
* LOCAL MACROS                                 *
************************************************/
#define DEBUG 1
#define debug(fmt, ...) \
		do { if (DEBUG) fprintf(stderr, "%s():%d: " fmt, __func__, \
								__LINE__, __VA_ARGS__); } while (0)

#define NAME_SPACE                      0xFFFF0000
#define BOUQUET_FULL_LIST	            "/var/etc/elecard/StbMainApp/"
#define BOUGET_CONFIG_DIR               "/var/etc/elecard/StbMainApp/bouquet"
#define BOUGET_CONFIG_FILE               "bouquet.conf"
#define BOUGET_NAME                     "bouquets"
#define BOUGET_STANDARD_NAME            "" //"Elecard playlist"
#define BOUGET_SERVICES_FILENAME_TV     BOUGET_CONFIG_DIR "/" BOUGET_NAME ".tv"
#define BOUGET_SERVICES_FILENAME_RADIO  BOUGET_CONFIG_DIR "/" BOUGET_NAME ".radio"
#define BOUGET_LAMEDB_FILENAME          BOUGET_CONFIG_DIR "/" "lamedb"

#define BOUGET_NAME_SIZE                64
#define CHANNEL_BUFFER_NAME             64
#define MAX_TEXT	512

#define GARB_DIR					"/var/etc/garb"
#define GARB_CONFIG			GARB_DIR "/config"

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct {
	bouquet_data_t bouquet_data;
	EIT_common_t	common;
} bouquet_t;

typedef struct _transpounder_id_t
{
	uint32_t ts_id;         //Transpounder_ID
	uint32_t n_id;          //Network_ID
	uint32_t name_space;    //Namespace(media_ID -?)
} transpounder_id_t;

typedef struct _transpounder_t
{
	transpounder_id_t transpounder_id;
	EIT_media_config_t media;
} transpounder_t;

typedef struct _services_t
{
	uint32_t s_id;                   //Services_ID
	transpounder_id_t transpounder_id;
	char channel_name[CHANNEL_BUFFER_NAME]; //channels_name
	char provider_name[CHANNEL_BUFFER_NAME]; //provider_name
	uint32_t v_pid;                 //video pid
	uint32_t a_pid;                 //audio pid
	uint32_t t_pid;                 //teletext pid
	uint32_t p_pid;                 //PCR
	uint32_t ac_pid;                //AC3
	uint32_t f;                     //f - ?

} services_t;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

list_element_t *head_ts_list = NULL;
list_element_t *bouquets_list = NULL;
static int bouquets_coun_list = 0;
static int bouquets_enable = 0;
list_element_t *bouquetNameList = NULL;
list_element_t *bouquet_name_tv = NULL;
list_element_t *bouquet_name_radio = NULL;
char bouquetName[CHANNEL_BUFFER_NAME];
char pName[CHANNEL_BUFFER_NAME];

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

int bouquet_foundBouquetName(char *name);
void bouquet_saveBouquets(char *fileName, char *typeName);
void bouquet_saveBouquetsConf(char *fileName, char *typeName);
void bouquet_saveLamedb(char *fileName);
void bouquet_saveBouquetsList(list_element_t **bouquet_name);
int bouquet_downloadBouquetsList();
void bouquet_parseBouquetsList(list_element_t **bouquet_name, char *path);


/*******************************************************************
* FUNCTION IMPLEMENTATION                                          *
********************************************************************/

void bouquets_setNumberPlaylist(int num)
{
	bouquets_coun_list = num;
}

int bouquets_getNumberPlaylist()
{
	return bouquets_coun_list;
}

void bouquet_saveAllBouquet()
{
	char *bouquetName;
	bouquetName = bouquet_getBouquetName();
	bouquet_saveBouquets(bouquetName, "tv");
	bouquet_saveBouquets(bouquetName, "radio");
	bouquet_saveBouquetsConf(bouquetName, "tv");
	bouquet_saveLamedb(bouquetName);
}

int bouquet_sendBouquet()
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

	FILE* fd;
	char bouquet_file[BUFFER_SIZE];

	sprintf(bouquet_file, "-mkdir %s/../bouquet/%s.STB/\n -put %s/%s/* %s/../bouquet/%s.STB/\n -put %s/%s %s/../bouquet/",
							 serverDir,    bouquetName,      BOUGET_CONFIG_DIR, bouquetName, serverDir, bouquetName,  BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE, serverDir );
	fd = fopen("/tmp/cmd_bouquet", "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open /tmp/cmd_bouquet \n", __FUNCTION__);
		return 1;
	}
	fprintf(fd, "%s\n",bouquet_file);
	fclose(fd);

	snprintf(cmd, sizeof(cmd), "sftp -b /tmp/cmd_bouquet -i /var/etc/garb/.ssh/id_rsa %s@%s", loginName, serverName);
	printf("cmd: %s\n",cmd);
	ret = system(cmd);
	interface_hideMessageBox();
	return WEXITSTATUS(ret);
}

list_element_t *list_getElement(int count, list_element_t **head)
{
	list_element_t *cur_element;
	int cur_count = 0;

	for(cur_element = *head; cur_element != NULL; cur_element = cur_element->next) {
		if (cur_count == count)
			return cur_element;
		cur_count++;
	}
	return NULL;
}

void bouquet_addScanChannels()
{
	list_element_t		*service_element;

	for(service_element = dvb_services; service_element != NULL; service_element = service_element->next) {
		EIT_service_t *curService = (EIT_service_t *)service_element->data;
		if(!(dvbChannel_findServiceCommon(&curService->common))){
			dvbChannel_addService(curService, 1);
		}
	}
	dvbChannel_writeOrderConfig();
	if (bouquet_enable())
		bouquet_saveAllBouquet();
}

int bouquets_setBouquet(interfaceMenu_t *pMenu, void* pArg)
{
	(void)pMenu;
	char *name;
	int number;

	number = CHANNEL_INFO_GET_CHANNEL(pArg);
	name = bouquet_getBouquetName();
	if (name != NULL) {
		if (strncasecmp(name, bouquet_getNameBouquetList(number), strlen(name) ) == 0) {
			return 0;
		}
	}
	dvbChannel_terminate();
	free_services(&dvb_services);
	bouquet_setBouquetName(bouquet_getNameBouquetList(number));
	saveAppSettings();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 0;
}

void bouquet_setBouquetName(char *name)
{
	sprintf(bouquetName,"%s", name);
}

void bouquet_removeBouquetName(list_element_t **bouquet_name, char *name)
{
	list_element_t *cur_element;
	for(cur_element = bouquetNameList; cur_element != NULL; cur_element = cur_element->next) {
		if (cur_element != NULL){
			if (strncasecmp((char *)cur_element->data, name, strlen(name)) == 0) {
				remove_element(&*bouquet_name, cur_element);
				return;
			}
		}
	}
}

void bouquet_addNewBouquetName(list_element_t **bouquet_name, char *name)
{
	list_element_t *cur_element;
	char *element_data;

	if (*bouquet_name == NULL) {
		cur_element = *bouquet_name = allocate_element(BOUGET_NAME_SIZE);
	} else {
		cur_element = append_new_element(*bouquet_name, BOUGET_NAME_SIZE);
	}
	if (!cur_element)
		return;
	element_data = (char *)cur_element->data;
	strncpy(element_data, name, BOUGET_NAME_SIZE);

}

void bouquet_setNewBouquetName(char *name)
{
	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	char buffName[64];
	char cmd[1024];
	sprintf(buffName, "%s/%s", BOUGET_CONFIG_DIR, name);
	sprintf(bouquetName,"%s", name);
	if (bouquet_getBouquetName() == NULL) {
		bouquet_addNewBouquetName(&bouquetNameList, name);

		snprintf(cmd, sizeof(cmd), "mkdir -p %s", buffName);
		printf("cmd = %s\n",cmd);
		system(cmd);
		bouquet_saveBouquetsList(&bouquetNameList);
	}
	interface_hideMessageBox();
}

char *bouquet_getBouquetName()
{
	if (bouquet_enable()) {
		list_element_t *cur_element;
		for(cur_element = bouquetNameList; cur_element != NULL; cur_element = cur_element->next) {
			if (cur_element != NULL){
				if (strncasecmp((char *)cur_element->data, bouquetName, strlen(bouquetName)) == 0) {
					dprintf("%s: %s\n",__func__, bouquetName);
					return bouquetName;
				}
			}
		}
	}
	return NULL;
}


int bouquet_foundBouquetName(char *name)
{
	if (!bouquet_enable() || name == NULL)
		return -1;
	list_element_t *cur_element;
	for(cur_element = bouquetNameList; cur_element != NULL; cur_element = cur_element->next) {
		if (cur_element != NULL){
			if (strcasecmp((char *)cur_element->data, name) == 0) {
				dprintf("%s: %s\n",__func__, bouquetName);
				return 0;
			}
		}
	}
	return -1;
}

char *bouquet_getNameBouquetList(int number)
{
	list_element_t *NameElement;
	NameElement = list_getElement(number, &bouquetNameList);
	if (NameElement != NULL){
		return (char *)NameElement->data;
	}
	return NULL;
}

void bouquet_getOffairName(char *fname, char *name)
{
	sprintf(fname,"%s/offair.%s%s", CONFIG_DIR, (name == NULL ? "" : name), (name == NULL ? "conf" : ".conf"));
}

void get_bouquets_file_name(list_element_t **bouquet_name, char *bouquet_file)
{
	char buf[BUFFER_SIZE];
	FILE* fd;
	list_element_t *cur_element;
	char *element_data;

	fd = fopen(bouquet_file, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	// check head file name
	while ( fgets(buf, BUFFER_SIZE, fd) != NULL ) {
		if ( strncasecmp(buf, "#SERVICE", 8) !=0 )
			continue;

		if (*bouquet_name == NULL) {
			cur_element = *bouquet_name = allocate_element(BOUGET_NAME_SIZE);
		} else {
			cur_element = append_new_element(*bouquet_name, BOUGET_NAME_SIZE);
		}
		if (!cur_element)
			break;

		element_data = (char *)cur_element->data;

		char * ptr;
		int    ch = '"';
		ptr = strchr( buf, ch ) + 1;
		sscanf(ptr,"%s \n",element_data); //get bouquet_name type: name" (with ")
		element_data[strlen(element_data) - 1] = '\0'; //get bouquet_name type: name
		dprintf("Get bouquet file name: %s\n", element_data);
	}
	fclose(fd);
}

transpounder_t *found_transpounder(EIT_common_t	*common, uint32_t name_space)
{
	list_element_t *cur_tr_element;
	transpounder_t *element_tr;

	for(cur_tr_element = head_ts_list; cur_tr_element != NULL; cur_tr_element = cur_tr_element->next) {
		element_tr = (transpounder_t*)cur_tr_element->data;

		if(element_tr == NULL) {
			continue;
		}
		if (name_space == element_tr->transpounder_id.name_space &&
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
		if (common->service_id ==  curService->common.service_id &&
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

		if (!(bouquets_found(&(srv->common)))) {
			dvbChannel_remove(srv);
		}
	}
	free_elements(&bouquets_list);
}

void get_bouquets_list(char *bouquet_file)
{
	list_element_t *cur_element;
	char buf[BUFFER_SIZE];
	FILE* fd;
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
	if (fgets(buf, BUFFER_SIZE, fd) != NULL) {
		sscanf(buf, "#NAME  %s\n", pName);
	}
	while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
		if ( sscanf(buf, "#SERVICE %x:%x:%x:%04x:%04x:%x:%x:%x:%x:%x:\n",   &type,
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
		if (service_id == 0 && transport_stream_id == 0 && network_id == 0)
			continue;

		bouquet_t *element;

		if (bouquets_list == NULL) {
			cur_element  = bouquets_list = allocate_element(sizeof(bouquet_t));
		} else {
			cur_element = append_new_element(bouquets_list, sizeof(bouquet_t));
		}
		if (cur_element == NULL)
			break;
		element = (bouquet_t *)cur_element->data;
		element->bouquet_data.serviceType = serviceType;
		element->bouquet_data.network_id = network_id;
		element->common.service_id = service_id;
		element->common.transport_stream_id = transport_stream_id;
		element->common.media_id = network_id;

		service_index_t *p_srvIdx;
		p_srvIdx = dvbChannel_findServiceCommon(&element->common);
		if (p_srvIdx == NULL) {
			dvbChannel_addBouquetData(&element->common, &element->bouquet_data, /*visible*/ true);
		}
	}
	fclose(fd);
}

int bouquets_compare(list_element_t **services)
{
	list_element_t	*service_element;

	if (dvbChannel_getCount() != dvb_getCountOfServices())
		return 0;

	for(service_element = *services; service_element != NULL; service_element = service_element->next) {
		EIT_service_t *curService = (EIT_service_t *)service_element->data;
		if (dvbChannel_findServiceCommon(&curService->common) == NULL)
			return 0;
	}
	return 1;
}

void get_addStandardPlaylist(list_element_t **bouquet_name,char *bouquet_file)
{
	list_element_t *cur_element;
	char *element_data;
	free_elements(&*bouquet_name);

	if (*bouquet_name == NULL) {
		cur_element = *bouquet_name = allocate_element(BOUGET_NAME_SIZE);
	} else {
		cur_element = append_new_element(*bouquet_name, BOUGET_NAME_SIZE);
	}
	if (!cur_element)
		return;

	element_data = (char *)cur_element->data;
	sprintf(element_data, "%s", bouquet_file);
}

int bouquet_enable()
{
	return bouquets_enable;
}

void bouquet_setEnable(int i)
{
	bouquets_enable = i;
}

int bouquet_getFile(char *fileName)
{
	//bouquet_downloadFileWithServices(BOUQUET_FULL_LIST);
	struct stat sb;
	if(stat(fileName, &sb) == 0) {
		if ( S_ISDIR( sb.st_mode))
			return false;
		return true;
	}
	return false;
}

int bouquet_getFolder(char *bouquetsFile)
{
	char buffName[256];
	sprintf(buffName, "%s/%s", BOUGET_CONFIG_DIR, bouquetsFile);

	struct stat sb;
	if(stat(buffName, &sb) == 0) {
		if ( S_ISDIR( sb.st_mode)) {
			return true;
		}
	}
	return false;
}

void bouquet_saveLamedb(char *fileName)
{
	dprintf("Save services list in lamedb\n");
	char dirName[BUFFER_SIZE];
	extern dvb_channels_t g_dvb_channels;
	struct list_head *pos;
	list_element_t *cur_element;
	transpounder_t *element;
	FILE* fd;

	sprintf(dirName, "%s/%s/lamedb",BOUGET_CONFIG_DIR ,fileName);
	fd = fopen(dirName, "wb");



	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, dirName);
		return;
	}


	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

		element = found_transpounder(&(srvIdx->common), NAME_SPACE);

		if (element != NULL)
			continue;

		if (head_ts_list == NULL) {
			cur_element = head_ts_list = allocate_element(sizeof(transpounder_t));
		} else {
			cur_element = append_new_element(head_ts_list, sizeof(transpounder_t));
		}
		if (cur_element == NULL)
			break;

		element = (transpounder_t *)cur_element->data;

		element->transpounder_id.name_space = NAME_SPACE;
		element->transpounder_id.ts_id = srvIdx->service->common.transport_stream_id;
		element->transpounder_id.n_id = srvIdx->service->original_network_id;

		memcpy(&element->media,&srvIdx->service->media,sizeof(EIT_media_config_t));
	}

	fprintf(fd, "eDVB services /4/\n");
	fprintf(fd, "transponders\n");
	for(cur_element = head_ts_list; cur_element != NULL; cur_element = cur_element->next) {
		element = (transpounder_t*)cur_element->data;
		fprintf(fd, "%08x:%04x:%04x\n", element->transpounder_id.name_space, element->transpounder_id.ts_id, element->transpounder_id.n_id);
		if (element->media.type == serviceMediaDVBC) {
			fprintf(fd, "	%c %d:%d:%d:%d:0:0:0\n", 'c', element->media.dvb_c.frequency / 1000,
														  element->media.dvb_c.symbol_rate,
														  (element->media.dvb_c.inversion == 0 ? 2 : 1),
														  element->media.dvb_c.modulation);
		}
		if (element->media.type == serviceMediaDVBS) {
			fprintf(fd, "	%c %d:%d:%d:%d:%d:%d:0\n", 's', element->media.dvb_s.frequency,
														  element->media.dvb_s.symbol_rate,
														  element->media.dvb_s.polarization,
														  element->media.dvb_s.FEC_inner,
														  element->media.dvb_s.orbital_position,
														  (element->media.dvb_s.inversion == 0 ? 2 : 1));
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
	FILE* fd;
	char bouquet_file[BUFFER_SIZE];

	sprintf(bouquet_file, "%s/%s/bouquets.%s",BOUGET_CONFIG_DIR , fileName, typeName);
	fd = fopen(bouquet_file, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	fprintf(fd, "#NAME Bouquets (%s)\n",typeName);
	fprintf(fd, "#SERVICE 1:7:1:0:0:0:0:0:0:0:FROM BOUQUET \"userbouquet.%s.%s\" ORDER BY bouquet\n", fileName, typeName);
	fclose(fd);
}

void bouquet_saveBouquetsConf(char *fileName, char *typeName)
{
	dprintf("Save services list in Bouquets\n");

	FILE* fd;
	char bouquet_file[BUFFER_SIZE];
	extern dvb_channels_t g_dvb_channels;
	struct list_head *pos;

	sprintf(bouquet_file, "%s/%s/userbouquet.%s.%s",BOUGET_CONFIG_DIR, fileName, fileName, typeName);
	fd = fopen(bouquet_file, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
		return;
	}
	fprintf(fd, "#NAME %s\n", fileName);

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);
		if ( srvIdx->data.visible )
			fprintf(fd, "#SERVICE 1:0:%d:%x:%x:%x:%08x:0:0:0:\n",
					srvIdx->service->service_descriptor.service_type,
					srvIdx->common.service_id,
					srvIdx->common.transport_stream_id,
					srvIdx->service->original_network_id,
					NAME_SPACE );

	}
	fclose(fd);
}

int bouquet_enableControl(interfaceMenu_t* pMenu, void* pArg)
{
	if (bouquet_enable()) {
		bouquet_setEnable(0);
	} else {
		bouquet_setEnable(1);
	}
	saveAppSettings();
	dvbChannel_terminate();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 1;
}

int bouquet_createNewBouquet(interfaceMenu_t *pMenu, char *value, void* pArg)
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
int bouquet_removeBouquet(interfaceMenu_t* pMenu, void* pArg)
{
	char *bouquetName;
	bouquetName = bouquet_getBouquetName();
	debug("%s\n",bouquetName);

	if (bouquetName != NULL) {
		interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "rm -r %s/%s/", BOUGET_CONFIG_DIR, bouquetName);
		printf("cmd: %s\n",cmd);
		system(cmd);
		interface_hideMessageBox();
		bouquet_removeBouquetName(&bouquetNameList, bouquetName);
		bouquet_loadBouquetsList(1);
	} else {
		free_services(&dvb_services);
	}
	dvbChannel_terminate();
	dvbChannel_writeOrderConfig();
	offair_fillDVBTMenu();
	output_redrawMenu(pMenu);
	return 0;
}

int bouquet_updateBouquet(interfaceMenu_t* pMenu, void* pArg)
{
	char *bouquetName;
	bouquetName = bouquet_getBouquetName();
	bouquet_loadBouquetsList(1);

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	if (bouquetName != NULL) {
		char serverName[16];
		char serverDir[256];
		char filename[256];
		char loginName[32];
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "mv %s/%s/ %s/%s_temp", BOUGET_CONFIG_DIR, bouquetName, BOUGET_CONFIG_DIR, bouquetName);
		printf("cmd: %s\n",cmd);
		system(cmd);
		getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
		getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
		getParam(GARB_CONFIG, "SERVER_IP", "", serverName);
		snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/../bouquet/%s.%s %s/%s", loginName, serverName, serverDir, bouquetName, "STB", BOUGET_CONFIG_DIR, bouquetName);
		printf("cmd: %s\n",cmd);
		system(cmd);

		sprintf(filename , "%s/%s", BOUGET_CONFIG_DIR, bouquetName);
		struct stat sb;
		if(!(stat(filename, &sb) == 0  || S_ISDIR( sb.st_mode))) {
			snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/../bouquet/%s %s/%s", loginName, serverName, serverDir, bouquetName, BOUGET_CONFIG_DIR, bouquetName);
			printf("cmd: %s\n",cmd);
			system(cmd);
		}

		if(!(stat(filename, &sb) == 0  || S_ISDIR( sb.st_mode))) {
			snprintf(cmd, sizeof(cmd), "mv %s/%s_temp/ %s/%s", BOUGET_CONFIG_DIR, bouquetName, BOUGET_CONFIG_DIR, bouquetName);
		} else {
			snprintf(cmd, sizeof(cmd), "rm -r %s/%s_temp/", BOUGET_CONFIG_DIR, bouquetName);
		}
		printf("cmd: %s\n",cmd);
		system(cmd);
	}
	interface_hideMessageBox();
	offair_fillDVBTMenu();
	return 0;
}


int bouquet_saveBouquet(interfaceMenu_t* pMenu, void* pArg)
{
	bouquet_loadBouquetsList(1);
	bouquet_saveBouquetsList(&bouquetNameList);
	bouquet_saveAllBouquet();
	bouquet_sendBouquet();
	offair_fillDVBTMenu();
	return 0;
}

void bouquet_loadBouquets(list_element_t **services)
{
	char *bouquetName;
	bouquetName = bouquet_getBouquetName();
	if (bouquetName != NULL && bouquet_getFolder(bouquet_getBouquetName(bouquetName))) {
		char fileName[1024];
		free_elements(&bouquet_name_tv);
		free_elements(&bouquet_name_radio);
		snprintf(fileName, sizeof(fileName), "%s/%s/%s.%s", BOUGET_CONFIG_DIR, bouquetName, BOUGET_NAME, "tv");
		get_bouquets_file_name(&bouquet_name_tv, fileName);
		snprintf(fileName, sizeof(fileName), "%s/%s/%s.%s", BOUGET_CONFIG_DIR, bouquetName, BOUGET_NAME, "radio");
		get_bouquets_file_name(&bouquet_name_radio, fileName);

		if (bouquet_name_tv != NULL) {
			snprintf(fileName, sizeof(fileName), "%s/%s/%s", BOUGET_CONFIG_DIR, bouquetName, (char *)bouquet_name_tv->data);
			get_bouquets_list(fileName/*tv*/);
		}
		if (bouquet_name_radio != NULL) {
			snprintf(fileName, sizeof(fileName), "%s/%s/%s", BOUGET_CONFIG_DIR, bouquetName, (char *)bouquet_name_radio->data);
			get_bouquets_list(fileName/*radio*/);
		}
		filter_bouquets_list(bouquetName);
		bouquet_loadLamedb(bouquetName, &*services);
	}
}

int bouquet_downloadBouquetsList()
{
	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	char serverName[16];
	char serverDir[256];
	char filename[256];
	char loginName[32];
	char cmd[1024];
	int val;
	snprintf(cmd, sizeof(cmd), "mv %s/%s %s/%s_temp", BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE, BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE);
	printf("cmd: %s\n",cmd);
	system(cmd);

	getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
	getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
	getParam(GARB_CONFIG, "SERVER_IP", "", serverName);
	snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa %s@%s:%s/../bouquet/%s %s", loginName, serverName, serverDir, BOUGET_CONFIG_FILE, BOUGET_CONFIG_DIR);
	printf("cmd: %s\n",cmd);
	system(cmd);


	sprintf(filename , "%s/%s", BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE);
	struct stat sb;
	if(stat(filename, &sb) == 0) {
		val = 0;
	} else {
		snprintf(cmd, sizeof(cmd), "mv %s/%s_temp %s/%s", BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE, BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE);
		printf("cmd: %s\n",cmd);
		system(cmd);
		val = -1;
	}
	interface_hideMessageBox();
	return val;
}

void bouquet_init()
{
	struct stat sb;
	if(stat(BOUGET_CONFIG_DIR, &sb) == 0 && S_ISDIR( sb.st_mode))
		return;
	mkdir(BOUGET_CONFIG_DIR, 0777);
}

void bouquet_saveBouquetsList(list_element_t **bouquet_name)
{
	FILE* fd;
	char buffName[256];
	sprintf(buffName, "%s/%s", BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE);

	fd = fopen(buffName, "wb");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, buffName);
		return;
	}

	list_element_t *cur_element;
	for(cur_element = *bouquet_name; cur_element != NULL; cur_element = cur_element->next) {
		if (cur_element != NULL){
			fprintf(fd, "#BOUQUETS_NAME=%s\n", (char *)cur_element->data);
		}
	}
	fclose(fd);
}

void bouquet_parseBouquetsList(list_element_t **bouquet_name, char *path)
{
	char buf[BUFFER_SIZE];
	FILE* fd;

	fd = fopen(path, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, path);
		return;
	}
	// check head file name
	while ( fgets(buf, BUFFER_SIZE, fd) != NULL ) {
		if (sscanf(buf, "#BOUQUETS_NAME=%s", path) == 1) {
			if (bouquet_foundBouquetName(path) != 0)
				bouquet_addNewBouquetName(&*bouquet_name, path);
		}
	}
	fclose(fd);
}

void bouquet_loadBouquetsList(int force)
{
	if (force == 1)
		bouquet_downloadBouquetsList();

	char buffName[256];
	sprintf(buffName, "%s/%s", BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE);
	bouquet_parseBouquetsList(&bouquetNameList, buffName);
	sprintf(buffName, "%s/%s_temp", BOUGET_CONFIG_DIR, BOUGET_CONFIG_FILE);
	bouquet_parseBouquetsList(&bouquetNameList, buffName);
}


void bouquet_loadChannelsFile()
{
	get_bouquets_file_name(&bouquet_name_tv, BOUGET_SERVICES_FILENAME_TV);
	get_bouquets_file_name(&bouquet_name_radio, BOUGET_SERVICES_FILENAME_RADIO);
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
	char path[BOUGET_NAME_SIZE * 2];
	char buf[BUFFER_SIZE];
	FILE* fd;
	sprintf(path, "%s/%s/lamedb",BOUGET_CONFIG_DIR, bouquet_file);
	fd = fopen(path, "r");
	if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, BOUGET_LAMEDB_FILENAME);
		return;
	}
	do {
		if (fgets(buf, BUFFER_SIZE, fd) == NULL)
			break;
		//parse head file name
		/*
		 *  not done
		 */
		if (fgets(buf, BUFFER_SIZE, fd) == NULL)
			break;
		if (strncasecmp(buf, "transponders", 12) != 0)
			break;
		//------ start parse transponders list -----//
		if (fgets(buf, BUFFER_SIZE, fd) == NULL)
			break;
		do {
			uint32_t name_space;
			uint32_t ts_id;
			uint32_t n_id;
			if (sscanf(buf, "%x:%x:%x\n",&name_space, &ts_id, &n_id) != 3)
				break;

			list_element_t *cur_element;
			transpounder_t *element;

			if (head_ts_list == NULL) {
				cur_element = head_ts_list = allocate_element(sizeof(transpounder_t));
			} else {
				cur_element = append_new_element(head_ts_list, sizeof(transpounder_t));
			}
			if (cur_element == NULL)
				break;

			element = (transpounder_t *)cur_element->data;
			element->transpounder_id.name_space = name_space;
			element->transpounder_id.ts_id = ts_id;
			element->transpounder_id.n_id = n_id;

			char type;
			uint32_t freq;
			uint32_t sym_rate;
			uint32_t inversion; //0 - auto, 1 - on, 2 - off
			uint32_t mod;
			uint32_t fec_inner;
			uint32_t flag;
			uint32_t system; // 0 - DVB-C, 1 DVB-C ANNEX C

			if (fgets(buf, BUFFER_SIZE, fd) == NULL)
				break;
			if (sscanf(buf, " %c %d:%d:%d:%d:%d:%d:%d\n",&type, &freq, &sym_rate, &inversion, &mod, &fec_inner, &flag, &system) != 8)
				break;
			if( type == 'c') {
				element->media.type = serviceMediaDVBC;
				element->media.frequency = freq * 1000;
				element->media.dvb_c.frequency = freq * 1000;
				element->media.dvb_c.symbol_rate = sym_rate;
				element->media.dvb_c.modulation = mod;
				element->media.dvb_c.inversion = (inversion == 2 ? 0 : 1);
			}
			if( type == 's') {
				element->media.type = serviceMediaDVBS;
				element->media.frequency = freq;
				element->media.dvb_s.frequency = freq;
				element->media.dvb_s.symbol_rate = sym_rate;
				element->media.dvb_s.polarization = inversion;
				element->media.dvb_s.FEC_inner = mod;
				element->media.dvb_s.orbital_position = fec_inner;
				element->media.dvb_s.inversion = (flag == 2 ? 0 : 1);
			}

			if (fgets(buf, BUFFER_SIZE, fd) == NULL)
				break;
			if (strncasecmp(buf, "/", 1) != 0 )
				break;
			if (fgets(buf, BUFFER_SIZE, fd) == NULL)
				break;
		} while (strncasecmp(buf, "end", 3) != 0);
		//------ start parse services list -----//
		if (fgets(buf, BUFFER_SIZE, fd) == NULL)
			break;
		if (strncasecmp(buf, "services", 8) != 0)
			break;
		if (fgets(buf, BUFFER_SIZE, fd) == NULL)
			break;
		do {
			uint32_t service_id;
			uint32_t name_space;
			uint32_t transport_stream_id;
			uint32_t original_network_id;
			uint32_t serviceType;
			uint32_t hmm;

			if (sscanf(buf, "%x:%x:%x:%x:%x:%x\n",&service_id, &name_space, &transport_stream_id, &original_network_id, &serviceType, &hmm) != 6)
				break;
			//parse channels name
			char service_name[MAX_TEXT];			
			if (fgets(service_name, BUFFER_SIZE, fd) == NULL)
				break;

			service_name[strlen(service_name) - 1] = '\0';
			//parese bouquet pName
			char bouqName[CHANNEL_BUFFER_NAME];
			if (fgets(buf, BUFFER_SIZE, fd) == NULL)
				break;
			memset(bouqName, 0, strlen(bouqName));
			sscanf(buf, "p:%s\n",bouqName);

			if (fgets(buf, BUFFER_SIZE, fd) == NULL)
				break;

			if ((strlen(bouqName) < 1) && (strncasecmp(pName, bouqName, strlen((pName))) != 0)) {
				continue;
			}

			EIT_common_t	common;
			common.service_id = service_id;
			common.transport_stream_id = transport_stream_id;
			common.media_id  = original_network_id;

			transpounder_t *element_tr;
			element_tr = found_transpounder(&common, name_space);
			if (element_tr == NULL)
				continue;

			EIT_service_t *element;
			list_element_t *service_element;
			service_element = found_list(&common, &*services);

			if (service_element != NULL) {
				element = (EIT_service_t *)service_element->data;
				if (element->media.type == serviceMediaDVBC && element_tr->media.type == serviceMediaDVBC &&
						element->media.dvb_c.frequency == element_tr->media.dvb_c.frequency &&
						element->media.dvb_c.symbol_rate == element_tr->media.dvb_c.symbol_rate &&
						element->media.dvb_c.modulation == element_tr->media.dvb_c.modulation &&
						element->media.dvb_c.inversion == element_tr->media.dvb_c.inversion)
					continue;
				if (element->media.type == serviceMediaDVBS && element_tr->media.type == serviceMediaDVBS &&
						element->media.dvb_s.frequency == element_tr->media.dvb_s.frequency &&
						element->media.dvb_s.symbol_rate == element_tr->media.dvb_s.symbol_rate &&
						element->media.dvb_s.polarization == element_tr->media.dvb_s.polarization &&
						//element->media.dvb_s.FEC_inner == element_tr->media.dvb_s.FEC_inner &&
						//element->media.dvb_s.orbital_position == element_tr->media.dvb_s.orbital_position &&
						element->media.dvb_s.inversion == element_tr->media.dvb_s.inversion)
					continue;
				remove_element(&*services, service_element);
			}
			list_element_t *cur_element = NULL;
			if (*services == NULL) {
				*services = cur_element = allocate_element(sizeof(EIT_service_t));
			} else {
				cur_element = append_new_element(*services, sizeof(EIT_service_t));
			}

			if (!cur_element)
				break;
			element = (EIT_service_t *)cur_element->data;

			element->common.service_id = common.service_id;
			element->common.transport_stream_id = common.transport_stream_id;
			element->common.media_id  = common.media_id;
			element->original_network_id = common.media_id;
			element->service_descriptor.service_type = serviceType;

			memcpy(&element->service_descriptor.service_name, &service_name, strlen(service_name));

			if (element_tr->media.type == serviceMediaDVBC) {
				element->media.type = element_tr->media.type;
				element->media.dvb_c.frequency = element_tr->media.dvb_c.frequency;
				element->media.dvb_c.symbol_rate = element_tr->media.dvb_c.symbol_rate;
				element->media.dvb_c.modulation = element_tr->media.dvb_c.modulation;
			}
			if (element_tr->media.type == serviceMediaDVBS) {
				element->media.type = element_tr->media.type;
				element->media.frequency = element_tr->media.frequency;
				element->media.dvb_s.frequency = element_tr->media.dvb_s.frequency;
				element->media.dvb_s.symbol_rate = element_tr->media.dvb_s.symbol_rate;
				element->media.dvb_s.polarization = element_tr->media.dvb_s.polarization;
				element->media.dvb_s.FEC_inner = element_tr->media.dvb_s.FEC_inner;
				element->media.dvb_s.orbital_position = element_tr->media.dvb_s.orbital_position;
				element->media.dvb_s.inversion = element_tr->media.dvb_s.inversion;
			}
		} while (strncasecmp(buf, "end", 3) != 0);
	} while(0);

	free_elements(&head_ts_list);
	fclose(fd);
}

int32_t getParam(const char *path, const char *param, const char *defaultValue, char *output)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	FILE *fd;
	int32_t found = 0;
	int32_t plen, vlen;

	fd = fopen(path, "r");
	if(fd != NULL) {
		while(fgets(buf, sizeof(buf), fd) != NULL) {
			plen = strlen(param);
			vlen = strlen(buf)-1;
			if(strncmp(buf, param, plen) == 0 && buf[plen] == '=') {
				while(buf[vlen] == '\r' || buf[vlen] == '\n' || buf[vlen] == ' ') {
					buf[vlen] = 0;
					vlen--;
				}
				if(vlen-plen > 0) {
					if(output != NULL) {
						strcpy(output, &buf[plen+1]);
					}
					found = 1;
				}
				break;
			}
		}
		fclose(fd);
	}

	if(!found && defaultValue && output) {
		strcpy(output, defaultValue);
	}
	return found;
}

int bouquet_file()
{
	struct stat sb;
	if(stat(BOUGET_SERVICES_FILENAME_TV, &sb) == 0 || stat(BOUGET_SERVICES_FILENAME_RADIO, &sb) == 0)
		return true;
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
	printf("cmd: %s\n",cmd);
	system(cmd);
	interface_hideMessageBox();
}

void bouquet_downloadFileWithServices(char *filename){
	printf("%s[%d]\n", __func__, __LINE__);
	//    struct stat sb;
	//  if(stat(filename, &sb) != 0) {
	//      printf("%s[%d]\n", __func__, __LINE__);
	bouquet_downloadFileFromServer("bouquet", filename);
	//  }
}

#endif // ENABLE_DVB
