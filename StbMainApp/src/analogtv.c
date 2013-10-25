

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
#include "list.h"
#include "app_info.h"
#include "StbMainApp.h"
#include "interface.h"
#include "l10n.h"
#include "playlist.h"
#include "sem.h"
#include "stsdk.h"
#include "gfx.h"
#include "off_air.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>
#include <libgen.h>
#include <signal.h>
#include <pthread.h>

#include <poll.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define fatal   eprintf
#define info    dprintf
#define verbose(...)
#define debug(...)

#define PERROR(fmt, ...) eprintf(fmt " (%s)\n", ##__VA_ARGS__, strerror(errno))

#define FILE_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/
typedef struct {
	uint32_t frequency;
	uint16_t customNumber;
	char customCaption[256];
	char sysEncode[5];
} analog_service_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

analog_service_t 	analogtv_channelParam[128];

static interfaceListMenu_t AnalogTVOutputMenu;
static uint32_t		analogtv_channelCount = 0;

analogtv_freq_range_t analogtv_range = {MIN_FREQUENCY_HZ / 1000, MAX_FREQUENCY_HZ / 1000};

analogtv_deliverySystem analogtv_delSys = TV_SYSTEM_PAL;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static pmysem_t analogtv_semaphore;

/******************************************************************
* FUNCTION DECLARATION                     <Module>[_<Word>+]  *
*******************************************************************/

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int analogtv_clearServiceList(interfaceMenu_t * pMenu, void *pArg)
{
	int permanent = (int)pArg;
	mysem_get(analogtv_semaphore);
	analogtv_channelCount = 0;
	mysem_release(analogtv_semaphore);

	remove(ANALOGTV_CONFIG_JSON);
	if (permanent > 0) remove(appControlInfo.tvInfo.channelConfigFile);

	pMenu->pActivatedAction(pMenu, pMenu->pArg);
	interface_displayMenu(1);

	offair_fillDVBTMenu();
	offair_fillDVBTOutputMenu(screenMain);

	return 0;
}


static int32_t analogtv_parseConfigFile(void)
{
	FILE *fd = NULL;
	fd = fopen(ANALOGTV_CONFIG_JSON, "r");
	if(fd == NULL) {
		dprintf("Error opening %s\n", ANALOGTV_CONFIG_JSON);

		int i = 1;
		analogtv_channelCount = 0;
		fd = fopen(ANALOGTV_CHANNEL_FILE, "r");
		if(fd == NULL) {
			dprintf("Error opening %s\n", ANALOGTV_CHANNEL_FILE);
			return -1;
		}
		while(!feof(fd)) {
			uint32_t freq;
			char buf[256];
			const char *name;

			fgets(buf, sizeof(buf), fd);
			freq = strtoul(buf, NULL, 10);

			name = strchr(buf, ';');
			if(name) {
				name++;
			} else {
				snprintf(buf, sizeof(buf), "TV Program %02d", i);
				name = buf;
			}
			i++;

			analogtv_channelParam[analogtv_channelCount].frequency = freq;
			strcpy(analogtv_channelParam[analogtv_channelCount].customCaption, name);
			strcpy(analogtv_channelParam[analogtv_channelCount].sysEncode, "UNDEF");
			analogtv_channelCount++;
		}
		fclose(fd);
		return 0;
	}
	fseek(fd, 0, SEEK_END);
	long len = ftell(fd);
	fseek(fd, 0, SEEK_SET);
	char *data = malloc(len + 1);
	fread(data, 1, len, fd);
	fclose(fd);

	cJSON *root;

	root = cJSON_Parse(data);
	free(data);
	if(!root) {
		printf("Error before: [%s]\n", cJSON_GetErrorPtr());
		return -1;
	}

	if(cJSON_GetObjectItem(root, "Analog TV channels")) {
		cJSON * format = cJSON_GetObjectItem(root, "Analog TV channels");
		if (format) {
			for (analogtv_channelCount = 0 ; (int32_t)analogtv_channelCount < cJSON_GetArraySize(format) ; analogtv_channelCount++)
			{
				cJSON * subitem = cJSON_GetArrayItem(format, analogtv_channelCount);
				if(subitem) {
					cJSON *obj;
					obj = cJSON_GetObjectItem(subitem,"frequency");
					if(obj) {
						analogtv_channelParam[analogtv_channelCount].frequency = obj->valueint;
					}
					obj = cJSON_GetObjectItem(subitem,"name");
					if(obj) {
						strcpy(analogtv_channelParam[analogtv_channelCount].customCaption, obj->valuestring);
					} else {
						sprintf(analogtv_channelParam[analogtv_channelCount].customCaption, "TV Program %02d", analogtv_channelCount + 1);
					}
					obj = cJSON_GetObjectItem(subitem,"systems encode");
					if(obj) {
						strcpy(analogtv_channelParam[analogtv_channelCount].sysEncode, obj->valuestring);
					}
				}
			}
		}
	}
	cJSON_Delete(root);

	return 0;
}

int analogtv_saveConfigFile(int32_t chanIndex, char* str)
{
	FILE *fd = NULL;
	fd = fopen(ANALOGTV_CONFIG_JSON, "r");
	if(fd == NULL) {
		dprintf("Error opening %s\n", ANALOGTV_CONFIG_JSON);

		fd = fopen(ANALOGTV_CHANNEL_FILE, "w");
		if(fd == NULL) {
			dprintf("Error opening %s\n", ANALOGTV_CHANNEL_FILE);
			return -1;
		}

		for (int index = 0; index < (int32_t)analogtv_channelCount; index++) {
			fprintf(fd, "%u;%s\n", analogtv_channelParam[index].frequency, (chanIndex == index) ? str:analogtv_channelParam[index].customCaption);
		}

		fclose(fd);
		return 0;
	}
	fseek(fd, 0, SEEK_END);
	long len = ftell(fd);
	fseek(fd, 0, SEEK_SET);
	char *data = malloc(len + 1);
	fread(data, 1, len, fd);
	fclose(fd);

	cJSON *root;
	root = cJSON_Parse(data);
	free(data);
	if (!root)
	{
		printf("Error before: [%s]\n", cJSON_GetErrorPtr());
		return 1;
	}

	cJSON * format = cJSON_GetObjectItem(root, "Analog TV channels");

	if(format) {
		cJSON * subitem = cJSON_GetArrayItem(format, chanIndex);
		if(subitem)	{
			cJSON *obj;
			obj = cJSON_GetObjectItem(subitem,"name");
			if(obj) {
				strcpy(obj->valuestring, str);
			} else {
				cJSON_AddStringToObject(subitem, "name", str);
			}
			char * rendered = cJSON_Print(root);
			fd = fopen(ANALOGTV_CONFIG_JSON, "w");
			fwrite(rendered, strlen(rendered), 1, fd);
			free(rendered);
			fclose(fd);
		}
	}
	cJSON_Delete(root);

	return 0;
}

// int analogtv_readServicesFromFile ()
// {
// 	if (!helperFileExists(appControlInfo.tvInfo.channelConfigFile)) return -1;
//
// 	int res = 0;
// 	analogtv_clearServiceList(NULL, 0);
//
// 	/// TODO : read from XML file and set analogtv_channelCount
//
// 	eprintf("%s: loaded %d services\n", __FUNCTION__, analogtv_channelCount);
//
// 	return res;
// }

#define RPC_ANALOG_SCAN_TIMEOUT      (180)

int analogtv_serviceScan(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STSDK
	char buf [256];
	uint32_t from_freq, to_freq;

	sprintf(buf, "%s", _T("SCANNING_ANALOG_CHANNELS"));
	interface_showMessageBox(buf, thumbnail_info, 0);

	if (pArg != NULL){
		from_freq = ((analogtv_freq_range_t*)pArg)->from_freqKHz * KHZ;
		to_freq = ((analogtv_freq_range_t*)pArg)->to_freqKHz * KHZ;

		if (from_freq > to_freq){
			uint32_t tmp = from_freq;
			from_freq = to_freq;
			to_freq = tmp;
		}
		if (from_freq < MIN_FREQUENCY_HZ) from_freq = MIN_FREQUENCY_HZ;
		if (to_freq > MAX_FREQUENCY_HZ) to_freq = MAX_FREQUENCY_HZ;
	} else {
		from_freq = MIN_FREQUENCY_HZ;
		to_freq = MAX_FREQUENCY_HZ;
	}

	cJSON *params = cJSON_CreateObject();
	cJSON *result = NULL;
	elcdRpcType_t type = elcdRpcInvalid;

	if(!params) {
		eprintf("%s: out of memory\n", __FUNCTION__);
		return -1;
	}

	cJSON_AddItemToObject(params, "from_freq", cJSON_CreateNumber(from_freq));
	cJSON_AddItemToObject(params, "to_freq", cJSON_CreateNumber(to_freq));

	char *analogtv_delSysName[] = {
		[TV_SYSTEM_PAL]		= "pal",
		[TV_SYSTEM_SECAM]	= "secam",
		[TV_SYSTEM_NTSC]	= "ntsc",
	};
	cJSON_AddItemToObject(params, "delsys", cJSON_CreateString(analogtv_delSysName[analogtv_delSys]));

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
	interface_displayMenu(1);

	offair_fillDVBTMenu();
	offair_fillDVBTOutputMenu(screenMain);

	return 0;
}

void analogtv_stopScan ()
{
	/// TODO
}

int analogtv_start()
{
	/// TODO
	return 0;
}

void analogtv_stop()
{
	/// TODO
	gfx_stopVideoProvider(0, 0, 0);
	appControlInfo.tvInfo.active = 0;
}

void analogtv_init(void)
{
	/// TODO: additional setup

	mysem_create(&analogtv_semaphore);
	analogtv_parseConfigFile();
}

void analogtv_terminate(void)
{
	/// TODO: additional cleanup

	analogtv_stop();

	analogtv_channelCount = 0;

	mysem_destroy(analogtv_semaphore);
}

static int analogtv_activateMenu(interfaceMenu_t *pMenu, void *pArg)
{
	analogtv_fillMenu();
	return 0;
}



static int analogtv_playControlProcessCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		default:;
	}
	return -1;
}

extern int offair_play_callback(interfacePlayControlButton_t button, void *pArg);
extern void offair_displayPlayControl(void);

//-----------------------------------------------------------------------
int32_t analogtv_startNextChannel(int32_t direction, void* pArg)
{
	uint32_t i;
	int32_t which = GET_NUMBER(pArg);

	for(i = (appControlInfo.tvInfo.id + analogtv_channelCount + direction) % analogtv_channelCount;
		i != appControlInfo.tvInfo.id;
		i = (i + direction + analogtv_channelCount) % analogtv_channelCount)
	{
		;
	}
	//printf("%s: i = %d, ch = %d, total = %d\n", __FUNCTION__, i, appControlInfo.tvInfo.id, analogtv_channelCount);
	analogtv_activateChannel(interfaceInfo.currentMenu, CHANNEL_INFO_SET(which, i));
	return 0;
}
//-----------------------------------------------------------------------

extern int offair_setChannel(int channel, void* pArg);

int analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg)
{
	uint32_t id = (uint32_t)pArg;
	uint32_t freq = analogtv_channelParam[id].frequency;
	char cmd[32];
	int buttons;

	dprintf("%s: in %d\n", __FUNCTION__, channelNumber);

	if(appControlInfo.tvInfo.active != 0) {
		//interface_playControlSelect(interfacePlayControlStop);
		// force showState to NOT be triggered
		interfacePlayControl.activeButton = interfacePlayControlStop;
	}

	appControlInfo.playbackInfo.playlistMode = playlistModeNone;
	appControlInfo.playbackInfo.streamSource = streamSourceAnalogTV;
	appControlInfo.playbackInfo.channel = id + offair_serviceCount;
	appControlInfo.mediaInfo.bHttp = 0;
	appControlInfo.tvInfo.active = 1;
	appControlInfo.tvInfo.id = id;

	buttons  = interfacePlayControlStop|interfacePlayControlPlay|interfacePlayControlPrevious|interfacePlayControlNext;
	buttons |= appControlInfo.playbackInfo.playlistMode != playlistModeFavorites ?
	           interfacePlayControlAddToPlaylist : interfacePlayControlMode;

	interface_playControlSetInputFocus(inputFocusPlayControl);
	interface_playControlSetup(offair_play_callback, NULL, buttons, analogtv_channelParam[id].customCaption, thumbnail_tvstandard);
	interface_playControlSetDisplayFunction(offair_displayPlayControl);
	interface_playControlSetProcessCommand(analogtv_playControlProcessCommand);
	interface_playControlSetChannelCallbacks(analogtv_startNextChannel, offair_setChannel);
//	interface_playControlSetAudioCallback(offair_audioChanged);
	interface_channelNumberShow(appControlInfo.playbackInfo.channel);


//	offair_startVideo(screenMain);
	offair_fillDVBTMenu();
	offair_fillDVBTOutputMenu(screenMain);
//	saveAppSettings();

	snprintf(cmd, sizeof(cmd), URL_ANALOGTV_MEDIA "%u@%s", freq, analogtv_channelParam[id].sysEncode);

	gfx_startVideoProvider(cmd, 0, 0, NULL);

	if(appControlInfo.tvInfo.active != 0) {
		interface_showMenu(0, 1);
	}

	//interface_menuActionShowMenu(pMenu, (void*)&DVBTMenu);

	return 0;
}

void analogtv_addMenuEntry(interfaceMenu_t *pMenu)
{
	interface_addMenuEntry2(pMenu, _T("ANALOGTV_CHANNELS"), 1, interface_menuActionShowMenu, &AnalogTVOutputMenu, thumbnail_tvstandard);
	return;
}

void analogtv_initMenu(interfaceMenu_t *pParent)
{
	createListMenu(&AnalogTVOutputMenu, _T("ANALOGTV_CHANNELS"), thumbnail_tvstandard, /*offair_icons*/NULL, pParent,
		interfaceListMenuIconThumbnail, analogtv_activateMenu, NULL, NULL);
	return;
}

uint32_t analogtv_getChannelCount(void)
{
	analogtv_parseConfigFile();
	return analogtv_channelCount;
}

void analogtv_addChannelsToMenu(interfaceMenu_t *pMenu, int startIndex)
{
	uint32_t i;

	analogtv_parseConfigFile();

	if(analogtv_channelCount == 0) {
		interface_addMenuEntryDisabled(pMenu, _T("NO_CHANNELS"), thumbnail_info);
		return;
	}

	interface_addMenuEntryDisabled(pMenu, "AnalogTV", 0);
	for(i = 0; i < analogtv_channelCount; i++) {
		char channelEntry[32];
		sprintf(channelEntry, "%02d. %s", startIndex + i + 1, analogtv_channelParam[i].customCaption);
		interface_addMenuEntry(pMenu, channelEntry, analogtv_activateChannel, (void *)i, thumbnail_tvstandard);
		interface_setMenuEntryLabel(&pMenu->menuEntry[pMenu->menuEntryCount-1], "ANALOG");

		if( (appControlInfo.playbackInfo.streamSource == streamSourceAnalogTV) &&
			(appControlInfo.tvInfo.id == i) )
		{
			interface_setSelectedItem(pMenu, pMenu->menuEntryCount - 1);
		}
	}
//	interface_setSelectedItem(channelMenu, selectedMenuItem);

	return;
}

void analogtv_fillMenu(void)
{
	interfaceMenu_t *channelMenu = _M &AnalogTVOutputMenu;
	uint32_t i;

	analogtv_parseConfigFile();
	interface_clearMenuEntries(channelMenu);

	if(analogtv_channelCount == 0) {
		interface_addMenuEntryDisabled(channelMenu, _T("NO_CHANNELS"), thumbnail_info);
	}
	for(i = 0; i < analogtv_channelCount; i++) {
		char channelEntry[32];

		sprintf(channelEntry, "TV Program %02d", i + 1);
		interface_addMenuEntry(channelMenu, channelEntry, analogtv_activateChannel, (void*)(analogtv_channelParam + i)->frequency, thumbnail_tvstandard);
	}
//	interface_setSelectedItem(channelMenu, selectedMenuItem);

	return;
}

#endif /* ENABLE_ANALOGTV */
