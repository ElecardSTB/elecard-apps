/*
 analogtv.c

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

#include "analogtv.h"

#ifdef ENABLE_ANALOGTV

#include "debug.h"
#include "app_info.h"
#include "StbMainApp.h"
#include "interface.h"
#include "l10n.h"
#include "stsdk.h"
#include "gfx.h"
#include "off_air.h"
#include "helper.h"
#include "server.h"

#include <cJSON.h>
#include <sys/stat.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define fatal   eprintf
#define info    dprintf
#define verbose(...)
#define debug(...)

#define PERROR(fmt, ...)        eprintf(fmt " (%s)\n", ##__VA_ARGS__, strerror(errno))

#define FILE_PERMS              (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

#define ANALOGTV_CONFIG_DIR     CONFIG_DIR "/analog"
#define ANALOGTV_UNDEF          "UNDEF"

#define MAX_SERVICE_COUNT 2048

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct _short_chinfo {
	char name[50];
	uint32_t id;
} short_chinfo;

typedef struct _full_chinfo {
	char name[50];
	uint32_t id;
	uint32_t freq;
} full_chinfo;

typedef struct {
	uint32_t channelCount;
	uint32_t channelCountVisible;
	struct list_head	channelList;
} bouquetAnalog_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static void analogChannelList_release(void);
static analog_service_t *analogChannelList_getElement(int index);
static analog_service_t *analogChannelList_getElementVisible(int index);
static int32_t analogtv_parseConfigFile(void);
static analog_service_t *analogList_add(struct list_head *listHead);
static int32_t analogtv_saveConfigFile(void);
static int32_t analogtv_changed(void);

static int32_t analogNames_release(void);

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static registeredChangeCallback_t changeCallbacks[2] = {
	{NULL, NULL},
	{NULL, NULL},
};

bouquetAnalog_t analogBouquet = {
	.channelCount         = 0,
	.channelCountVisible  = 0,
	.channelList          = LIST_HEAD_INIT(analogBouquet.channelList),
};
static struct list_head analogChannelsNamesHead = LIST_HEAD_INIT(analogChannelsNamesHead);

/******************************************************************
* FUNCTION DECLARATION                     <Module>[_<Word>+]  *
*******************************************************************/

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/
static void analogtv_cleanList(void)
{
	analogBouquet.channelCount = 0;
	analogBouquet.channelCountVisible = 0;
	analogChannelList_release();
	analogtv_changed();
}

void analogtv_init(void)
{
	if(!helperCheckDirectoryExsists(ANALOGTV_CONFIG_DIR)) {
		mkdir(ANALOGTV_CONFIG_DIR, 0777);
	}

	analogtv_cleanList();
	analogtv_parseConfigFile();
}

void analogtv_terminate(void)
{
	analogtv_stop();
	analogtv_cleanList();

	analogNames_release();
}

void analogtv_load(void)
{
	analogtv_stop();
	analogtv_cleanList();
	analogtv_parseConfigFile();
}

struct list_head *analogtv_getChannelList(void)
{
	return &analogBouquet.channelList;
}

static int32_t analogtv_parseConfigFile(void)
{
	FILE *fd = NULL;
	cJSON *root;
	cJSON *format;
	char *data;
	long len;
	analogBouquet.channelCount = 0;
	analogBouquet.channelCountVisible = 0;

	fd = fopen(ANALOG_CHANNEL_FILE_NAME, "r");
	if(fd == NULL) {
		dprintf("Error opening %s\n", ANALOG_CHANNEL_FILE_NAME);
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
		printf("Error before: [%s]\n", cJSON_GetErrorPtr());
		return -2;
	}
	format = cJSON_GetObjectItem(root, "Analog TV channels");
	if(format) {
		uint32_t i;
		analogBouquet.channelCount = cJSON_GetArraySize(format);
		for(i = 0 ; i < analogBouquet.channelCount; i++) {
			cJSON *subitem = cJSON_GetArrayItem(format, i);
			if(subitem) {
				//++++++
				//TODO: remove duplicates when scanning
				//++++++
				analog_service_t *el = analogList_add(&analogBouquet.channelList);

				el->visible = objGetInt(subitem, "visible", 1);
				el->frequency = objGetInt(subitem, "frequency", 0);
				sprintf(el->customCaption, "%s", objGetString(subitem, "name", ""));
				sprintf(el->sysEncode, "%s", objGetString(subitem, "system encode", ANALOGTV_UNDEF));
				sprintf(el->audio, "%s", objGetString(subitem, "audio demod mode", ANALOGTV_UNDEF));

				if (el->customCaption[0] == 0) {
					sprintf(el->customCaption, "TV Program %02d", analogBouquet.channelCountVisible + 1);
				}
				if (el->visible == 1) {
					analogBouquet.channelCountVisible++;
				}
			}
		}
	}
	cJSON_Delete(root);
	return 0;
}

int32_t analogtv_saveConfigFile(void)
{
	cJSON* root;
	cJSON* format;
	char *rendered;
	uint32_t i = 0;

	root = cJSON_CreateObject();
	if(!root) {
		dprintf("Memory error!\n");
		return -1;
	}
	format = cJSON_CreateArray();
	if(!format) {
		dprintf("Memory error!\n");
		cJSON_Delete(root);
		return -1;
	}
	cJSON_AddItemToObject(root, "Analog TV channels", format);

	struct list_head *pos;
	list_for_each(pos, &analogBouquet.channelList) {
		analog_service_t *element = list_entry(pos, analog_service_t, channelList);
		if(element == NULL) {
			continue;
		}
		cJSON* fld;
		fld = cJSON_CreateObject();
		if(fld) {
			cJSON_AddNumberToObject(fld, "id", i++);
			cJSON_AddNumberToObject(fld, "frequency", element->frequency);
			cJSON_AddNumberToObject(fld, "visible", element->visible);
			cJSON_AddStringToObject(fld, "name", element->customCaption);
			cJSON_AddStringToObject(fld, "system encode", element->sysEncode);
			cJSON_AddStringToObject(fld, "audio demod mode", element->audio);

			cJSON_AddItemToArray(format, fld);
		} else {
			dprintf("Memory error!\n");
		}
	}

	rendered = cJSON_Print(root);
	cJSON_Delete(root);

	if(rendered) {
		FILE *fd = NULL;
		fd = fopen(ANALOG_CHANNEL_FILE_NAME, "w");
		if(fd) {
			fwrite(rendered, strlen(rendered), 1, fd);
			fclose(fd);
		}
		free(rendered);
	}
	return 0;
}

int32_t analogtv_findOnFrequency(uint32_t frequency)
{
	uint32_t i = 0;
	struct list_head *pos;
	list_for_each(pos, &analogBouquet.channelList) {
		analog_service_t *element = list_entry(pos, analog_service_t, channelList);
		if(element == NULL ) {
			continue;
		}
		if(element->frequency == frequency) {
			return i;
		}
		i++;
	}
	return -1;
}

#define RPC_ANALOG_SCAN_TIMEOUT      (180)
int analogtv_serviceScan(interfaceMenu_t *pMenu, void* pArg)
{
	dprintf("%s[%d]\n", __func__, __LINE__);
#ifdef STSDK
	char buf[256];
	uint32_t from_freq, to_freq;

	offair_stopVideo(screenMain, 1);

	sprintf(buf, "%s", _T("SCANNING_ANALOG_CHANNELS"));
	interface_showMessageBox(buf, thumbnail_info, 0);
	
	from_freq = appControlInfo.tvInfo.lowFrequency * KHZ;
	to_freq = appControlInfo.tvInfo.highFrequency * KHZ;

	cJSON *params = cJSON_CreateObject();
	cJSON *result = NULL;
	elcdRpcType_t type = elcdRpcInvalid;

	if(!params) {
		eprintf("%s: out of memory\n", __FUNCTION__);
		return -1;
	}

	cJSON_AddItemToObject(params, "from_freq", cJSON_CreateNumber(from_freq));
	cJSON_AddItemToObject(params, "visible", cJSON_CreateNumber(1));
	cJSON_AddItemToObject(params, "to_freq", cJSON_CreateNumber(to_freq));

	char *analogtv_delSysName[] = {
		[TV_SYSTEM_PAL]		= "pal",
		[TV_SYSTEM_SECAM]	= "secam",
		[TV_SYSTEM_NTSC]	= "ntsc",
	};
	cJSON_AddItemToObject(params, "delsys", cJSON_CreateString(analogtv_delSysName[appControlInfo.tvInfo.delSys]));

	char *analogtv_audioName[] = {
		[TV_AUDIO_SIF]	= "sif",
		[TV_AUDIO_AM]	= "am",
		[TV_AUDIO_FM1]	= "fm1",
		[TV_AUDIO_FM2]	= "fm2",
	};
	cJSON_AddItemToObject(params, "audio", cJSON_CreateString(analogtv_audioName[appControlInfo.tvInfo.audioMode]));

	int res = st_rpcSyncTimeout(elcmd_tvscan, params, RPC_ANALOG_SCAN_TIMEOUT, &type, &result );
	(void)res;
	if(result && result->valuestring != NULL && strcmp (result->valuestring, "ok") == 0) {
		/// TODO

		// elcd dumped services to file. read it
		analogtv_parseConfigFile();
	}
	cJSON_Delete(result);
	cJSON_Delete(params);
#endif

	interface_hideMessageBox();
	pMenu->pActivatedAction(pMenu, pMenu->pArg);
	analogtv_changed();
	interface_displayMenu(1);
	return 0;
}

void analogtv_stop(void)
{
	if(appControlInfo.playbackInfo.streamSource == streamSourceAnalogTV) {
		offair_stopVideo(screenMain, 1);
		appControlInfo.tvInfo.active = 0;
	}
}

int analogtv_clearServiceList(interfaceMenu_t * pMenu, void *pArg)
{
	(void)pArg;

	analogtv_stop();
	analogtv_cleanList();
	analogtv_saveConfigFile();
	pMenu->pActivatedAction(pMenu, pMenu->pArg);
	interface_displayMenu(1);
	analogtv_changed();
	return 0;
}

void analogtv_sortRecheck(void)
{
	analogBouquet.channelCount = 0;
	analogBouquet.channelCountVisible = 0;
	struct list_head *pos;
	list_for_each(pos, &analogBouquet.channelList) {
		analog_service_t *element = list_entry(pos, analog_service_t, channelList);
		if(element == NULL ) {
			continue;
		}
		if (element->visible)
			analogBouquet.channelCountVisible++;

		analogBouquet.channelCount++;
	}
}

int32_t analogtv_setChannelsData(char *Name, int visible,  int index)
{
	analog_service_t *element = analogChannelList_getElement(index);
	if (element == NULL)
		return -1;

	snprintf(element->customCaption, MENU_ENTRY_INFO_LENGTH, "%s", Name);
	element->visible = visible;
	return 0;
}

int32_t analogtv_applyUpdates(void)
{
	analogtv_sortRecheck();
	analogtv_changed();

	analogtv_saveConfigFile();

	//TODO: is it need here?
	dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);

	return 0;
}

int32_t analogtv_swapService(int first, int second)
{
	if (first == second)
		return 0;
	analog_service_t *srvIdx_first;
	analog_service_t *srvIdx_second;
	struct list_head *srvIdx_beforeFirst;
	struct list_head *srvIdx_beforeSecond;

	srvIdx_first = analogChannelList_getElement(first);
	srvIdx_second = analogChannelList_getElement(second);

	if(!srvIdx_first || !srvIdx_second) {
		return -1;
	}
	srvIdx_beforeFirst = srvIdx_first->channelList.prev;
	srvIdx_beforeSecond = srvIdx_second->channelList.prev;

	if(&srvIdx_first->channelList != srvIdx_beforeSecond) {
		list_del(&srvIdx_first->channelList);
		list_add(&srvIdx_first->channelList, srvIdx_beforeSecond);
	}
	list_del(&srvIdx_second->channelList);
	list_add(&srvIdx_second->channelList, srvIdx_beforeFirst);

	analogtv_changed();
	return 0;
}

int32_t analogtv_registerCallbackOnChange(changeCallback_t *pCallback, void *pArg)
{
	uint32_t i;
	for(i = 0; i < ARRAY_SIZE(changeCallbacks); i++) {
		if(changeCallbacks[i].pCallback == NULL) {
			changeCallbacks[i].pCallback = pCallback;
			changeCallbacks[i].pArg = pArg;
			return 0;
		}
	}
	return -1;
}

static int32_t analogtv_changed(void)
{
	uint32_t i = 0;

	while(changeCallbacks[i].pCallback) {
		printf("%s[%d]\n",__func__, __LINE__);
		changeCallbacks[i].pCallback(changeCallbacks[i].pArg);
		i++;
		if((i >= ARRAY_SIZE(changeCallbacks))) {
			return 0;
		}
	}

	return 0;
}

//----------------------SET NEXT AUDIO MODE ----  button F3--------------
int32_t analogtv_setNextAudioMode()
{
	char *analogtv_audioName[] = {
		[TV_AUDIO_SIF]	= "sif",
		[TV_AUDIO_AM]	= "am",
		[TV_AUDIO_FM1]	= "fm1",
		[TV_AUDIO_FM2]	= "fm2",
	};
	uint32_t id = appControlInfo.tvInfo.id;
	uint32_t i = 0;
	analog_service_t *element = analogChannelList_getElementVisible(id);

	for (i = 0; i <= TV_AUDIO_FM2; i++) {
		if ( !strcmp(element->audio, analogtv_audioName[i]) )
			break;
	}
	i++;
	if(i > TV_AUDIO_FM2)
		i = 0;
	
	strncpy(element->audio, analogtv_audioName[i], sizeof(analogtv_audioName[i]));

	analogtv_activateChannel(interfaceInfo.currentMenu, (void *)id);
	return analogtv_saveConfigFile();
}

int analogtv_playControlProcessCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommandUp:
		case interfaceCommandDown:
			interface_menuActionShowMenu(interfaceInfo.currentMenu, &DVBTMenu);
			interface_showMenu(1, 1);
			return 0;
		case interfaceCommand0:
			if (interfaceChannelControl.length)
				return 1;
			if (appControlInfo.offairInfo.previousChannel &&
				appControlInfo.offairInfo.previousChannel != offair_getCurrentChannel())
			{
				offair_setChannel(appControlInfo.offairInfo.previousChannel, SET_NUMBER(screenMain));
			}
			return 0;
/*		case interfaceCommandGreen:
			if(services_edit_able) {
				analogtv_fillFullServList();
				if(full_service_count > 0) {
					analogtv_menuServicesShow();
				}
			}
			return 0;*/
		case interfaceCommandYellow:
			analogtv_setNextAudioMode();
			return 0;
		default:;
	}
	return 1;
}

//-----------------------------------------------------------------------
int32_t analogtv_startNextChannel(int32_t direction, void* pArg)
{
	if (analogBouquet.channelCountVisible <= 1)
		return 0;

	int32_t id = appControlInfo.tvInfo.id;
	id += direction ? -1 : 1;

	if ((int)id >= (int)analogBouquet.channelCountVisible) {
		id = 0;
	}
	if ((int)id < 0) {
		id = analogBouquet.channelCountVisible - 1;
	}
	analogtv_activateChannel(interfaceInfo.currentMenu, (void *)id);
	return 0;
}
//-----------------------------------------------------------------------

int analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg)
{
	uint32_t id = (uint32_t)pArg;
	uint32_t freq;
	char cmd[32];
	int32_t buttons;
	int32_t previousChannel;
	int result = 0;

	dprintf("%s[%d] id = %d\n",__func__, __LINE__, id);
	analog_service_t *element = analogChannelList_getElementVisible(id);
	if (element == NULL)
		return -1;

	freq = element->frequency;

	previousChannel = offair_getCurrentChannel();
	if(appControlInfo.tvInfo.active != 0) {
		//interface_playControlSelect(interfacePlayControlStop);
		// force showState to NOT be triggered
		interfacePlayControl.activeButton = interfacePlayControlStop;
	}

	appControlInfo.playbackInfo.playlistMode = playlistModeNone;
	appControlInfo.playbackInfo.streamSource = streamSourceAnalogTV;
	appControlInfo.tvInfo.active = 1;
	appControlInfo.mediaInfo.bHttp = 0;
	offair_stopVideo(screenMain, 1);
//	offair_startVideo(screenMain);
// 	offair_fillDVBTMenu();

//	saveAppSettings();

	snprintf(cmd, sizeof(cmd), URL_ANALOGTV_MEDIA "%u@%s:%s", freq, element->sysEncode, element->audio);
	gfx_startVideoProvider(cmd, 0, 0, NULL);


	if(appControlInfo.tvInfo.active != 0 && result == 0) {
		appControlInfo.playbackInfo.channel = id + dvbChannel_getCount();
		appControlInfo.tvInfo.id = id;

		buttons  = interfacePlayControlStop|interfacePlayControlPlay|interfacePlayControlPrevious|interfacePlayControlNext;
		buttons |= appControlInfo.playbackInfo.playlistMode != playlistModeFavorites ?
				   interfacePlayControlAddToPlaylist : interfacePlayControlMode;

		interface_playControlSetInputFocus(inputFocusPlayControl);
		interface_playControlSetup(offair_play_callback, NULL, buttons, element->customCaption, thumbnail_tvstandard);
		interface_playControlSetDisplayFunction(offair_displayPlayControl);
		interface_playControlSetProcessCommand(analogtv_playControlProcessCommand);
		interface_playControlSetChannelCallbacks(analogtv_startNextChannel, offair_setChannel);
	//	interface_playControlSetAudioCallback(offair_audioChanged);
		interface_channelNumberShow(appControlInfo.playbackInfo.channel + 1);

			offair_updateChannelStatus();

		interface_showMenu(0, 1);
		offair_setPreviousChannel(previousChannel);
	}
	//interface_menuActionShowMenu(pMenu, (void*)&DVBTMenu);

	return 0;
}

uint32_t analogtv_getChannelCount(int visible)
{
	if (visible == 1) {
		return analogBouquet.channelCountVisible;
	}
	return analogBouquet.channelCount;
}

void analogtv_addChannelsToMenu(interfaceMenu_t *pMenu, int startIndex)
{
	dprintf("%s[%d]\n", __func__, __LINE__);
	uint32_t i = 0;

	if(analogBouquet.channelCountVisible == 0) {
		interface_addMenuEntryDisabled(pMenu, _T("NO_CHANNELS"), thumbnail_info);
		return;
	}

	interface_addMenuEntryDisabled(pMenu, "AnalogTV", 0);
	struct list_head *pos;
	list_for_each(pos, &analogBouquet.channelList) {
		analog_service_t *element = list_entry(pos, analog_service_t, channelList);
		if(element == NULL || !(element->visible)) {
			continue;
		}
		char channelEntry[32];

		sprintf(channelEntry, "%s. %s", offair_getChannelNumberPrefix(startIndex + i), element->customCaption);
		interface_addMenuEntry(pMenu, channelEntry, analogtv_activateChannel, (void *)i, thumbnail_tvstandard);
		interface_setMenuEntryLabel(&pMenu->menuEntry[pMenu->menuEntryCount-1], "ANALOG");
		if( (appControlInfo.playbackInfo.streamSource == streamSourceAnalogTV) &&
			(appControlInfo.tvInfo.id == i) ) {
			interface_setSelectedItem(pMenu, pMenu->menuEntryCount - 1);
		}
		i++;
	}
}

int menu_entryIsAnalogTv(interfaceMenu_t *pMenu, int index)
{
	return pMenu->menuEntry[pMenu->selectedItem].pAction == analogtv_activateChannel;
}

int analogtv_getServiceDescription(uint32_t index, char *buf)
{
	if (index >= analogBouquet.channelCountVisible) {
		buf[0] = 0;
		return -1;
	}
	analog_service_t *element = analogChannelList_getElementVisible(index);
	sprintf(buf,"\"%s\"\n", element->customCaption);
	buf += strlen(buf);

	sprintf(buf, "   %s: %u MHz\n", _T("DVB_FREQUENCY"), element->frequency / 1000000);
	buf += strlen(buf);

	sprintf(buf, "   %s\n", element->sysEncode);
	buf += strlen(buf);

	sprintf(buf, "   audio:%s", element->audio);
	buf += strlen(buf);
	return 0;
}

const char *analogtv_getServiceName(uint32_t index)
{
	if (index < analogBouquet.channelCountVisible) {
		analog_service_t *element = analogChannelList_getElementVisible(index);
		if (element != NULL) {
			return element->customCaption;
		}
	}
	return "";
}

int32_t analogtv_hasTuner(void)
{
#ifdef STSDK
	if(st_getBoardId() == eSTB850) {
		return 1;
	}
#endif
	return 0;
}

/*************************************************************
****                                                      ****
****              FUNCTIONS FOR CONTROLLING LIST          ****
****                                                      ****
*************************************************************/

void analogChannelList_release(void)
{
	struct list_head *pos;
	list_for_each(pos, &analogBouquet.channelList) {
		analog_service_t *element = list_entry(pos, analog_service_t, channelList);
		if(!list_empty(&element->channelList)) {
			list_del(&element->channelList);
		}
		free(element);
	}
}

analog_service_t *analogChannelList_getElement(int index)
{
	int i = 0;
	struct list_head *pos;
	list_for_each(pos, &analogBouquet.channelList) {
		if (i == index) {
			return list_entry(pos, analog_service_t, channelList);
		}
		i++;
	}
	return NULL;
}

analog_service_t *analogChannelList_getElementVisible(int index)
{
	int i = 0;
	struct list_head *pos;
	list_for_each(pos, &analogBouquet.channelList) {
		analog_service_t *element = list_entry(pos, analog_service_t, channelList);
		if (element->visible != 1) {
			continue;
		}
		if (i == index) {
			return element;
		}
		i++;
	}
	return NULL;
}

analog_service_t *analogList_add(struct list_head *listHead)
{
	analog_service_t *new_element = malloc(sizeof(analog_service_t));
	if(new_element == NULL) {
		eprintf("%s()[%d]: Error allocating memory!\n", __func__, __LINE__);
		return NULL;
	}
	memset(new_element, 0, sizeof(analog_service_t));
	list_add_tail(&(new_element->channelList), listHead);
	return new_element;
}

int32_t analogNames_isExist(void)
{
	if(!list_empty(&analogChannelsNamesHead)) {
		return 1;
	}
	return helperFileExists(ANALOGTV_CONFIG_DIR "/tvchannels.txt");
}

int32_t analogNames_download(void)
{
	if(server_get("../channels/tvchannels.txt", ANALOGTV_CONFIG_DIR "/tvchannels.txt") != 0) {
		if(server_get("../analog/tvchannels.txt", ANALOGTV_CONFIG_DIR "/tvchannels.txt") != 0) {
			return -1;
		}
	}
	return 0;
}

int32_t analogNames_load(void)
{
	if(!list_empty(&analogChannelsNamesHead)) {
		return 0;
	}
	FILE *fd = fopen(ANALOGTV_CONFIG_DIR "/tvchannels.txt", "r");
	if(fd != NULL) {
		int i = 0;
		while(!feof(fd)) {
			char buf[256];
			int32_t id;
 			if(fgets(buf, sizeof(buf), fd) != NULL) {
				 char *name;
				 id = atoi(buf);
				 name = strchr(buf, '\t');
				 if(name) {
					name++;
					stripEnterInStr(name);
				 } else {
					name = "-";
				 }
// dbg_printf("id=%d, name=%s\n", id, name);
				strList_add(&analogChannelsNamesHead, name);
				i++;
			}
		}
		dbg_printf("i=%d\n", i);
		fclose(fd);
	} else {
		eprintf("%s(): Cant open file " ANALOGTV_CONFIG_DIR "/tvchannels.txt: %m\n", __func__);
	}
	return 0;
}

int32_t analogNames_release(void)
{
	strList_release(&analogChannelsNamesHead);
	return 0;
}

struct list_head *analogNames_getList(void)
{
	return &analogChannelsNamesHead;
}


#endif /* ENABLE_ANALOGTV */
