

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
} analog_service_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

analog_service_t * analogtv_services = NULL;

static interfaceListMenu_t AnalogTVOutputMenu;
static uint32_t		analogtv_channelFreq[128];
static uint32_t		analogtv_channelCount = 0;

analogtv_freq_range_t analogtv_range = {MIN_FREQUENCY_HZ / 1000, MAX_FREQUENCY_HZ / 1000};

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static pmysem_t analogtv_semaphore;

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int analogtv_clearServiceList(interfaceMenu_t * pMenu, void *pArg)
{
	int permanent = (int)pArg;
	mysem_get(analogtv_semaphore);
	free(analogtv_services);
	analogtv_services = NULL;
	analogtv_channelCount = 0;
	mysem_release(analogtv_semaphore);
	
	if (permanent > 0) remove(appControlInfo.tvInfo.channelConfigFile);
	return 0;
}


static int32_t analogtv_parseConfigFile(void)
{
	FILE *fd = NULL;

	analogtv_channelCount = 0;
	fd = fopen(ANALOGTV_CHANNEL_FILE, "r");
	if(fd == NULL) {
		dprintf("Error opening %s\n", ANALOGTV_CHANNEL_FILE);
		return -1;
	}
	while(!feof(fd)) {
		uint32_t freq;
		if(fscanf(fd, "%u\n", &freq) != 1)
			break;
		analogtv_channelFreq[analogtv_channelCount] = freq;
		analogtv_channelCount++;
	}
	fclose(fd);
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

int analogtv_serviceScan(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STSDK
	//char buf [256];
	//interface_sliderShow(1,1);
	//sprintf(buf, _T("SCAN_COMPLETE_CHANNELS_FOUND"), dvb_getNumberOfServices());
	//interface_showMessageBox(buf, thumbnail_info, 5000);

	uint32_t from_freq, to_freq;

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

	st_rpcSync(elcmd_tvscan, params, &type, &result );
	if(result && result->valuestring != NULL && strcmp (result->valuestring, "ok") == 0) {
		/// TODO
		
		// elcd dumped services to file. read it
		analogtv_parseConfigFile();
	}
	cJSON_Delete(result);
	cJSON_Delete(params);
#endif

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

	free(analogtv_services);
	analogtv_channelCount = 0;

	mysem_destroy(analogtv_semaphore);
}

static int analogtv_activateMenu(interfaceMenu_t *pMenu, void *pArg)
{
	analogtv_fillMenu();
	return 0;
}

static int analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg)
{
	uint32_t freq = *((uint32_t *)pArg);
	char cmd[32];

	snprintf(cmd, sizeof(cmd), "tv://%u", freq);
	printf("%s[%d]: *** cmd=%s\n", __FILE__, __LINE__, cmd);

	return  gfx_startVideoProvider(cmd, 0, 0, NULL);
}

void analogtv_addMenuEntry(interfaceMenu_t *pMenu)
{
	char buf[256];
	char *p_str;
// 	uint32_t count = analogtv_getChannelCount();

// 	if(count > 0) {
// 		snprintf(buf, sizeof(buf), "%s (%d)", _T("ANALOGTV_CHANNELS"), count);
// 		p_str = buf;
// 	} else {
		p_str = _T("ANALOGTV_CHANNELS");
// 	}
// printf("%s[%d]: *** p_str=%s\n", __FILE__, __LINE__, p_str);
	interface_addMenuEntry2(pMenu, p_str, 1, interface_menuActionShowMenu, &AnalogTVOutputMenu, thumbnail_channels);
	return;
}

void analogtv_initMenu(interfaceMenu_t *pParent)
{
	createListMenu(&AnalogTVOutputMenu, _T("ANALOGTV_CHANNELS"), thumbnail_channels, /*offair_icons*/NULL, pParent,
		interfaceListMenuIconThumbnail, analogtv_activateMenu, NULL, NULL);
	return;
}

uint32_t analogtv_getChannelCount(void)
{
	analogtv_parseConfigFile();
	return analogtv_channelCount;
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
		interface_addMenuEntry(channelMenu, channelEntry, analogtv_activateChannel, analogtv_channelFreq + i, thumbnail_channels);
	}
//	interface_setSelectedItem(channelMenu, selectedMenuItem);

	return;
}

#endif /* ENABLE_ANALOGTV */
