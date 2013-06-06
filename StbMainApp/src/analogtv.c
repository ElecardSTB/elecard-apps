

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
int analogtv_service_count = 0;

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

void analogtv_clearServiceList(int permanent)
{
	mysem_get(analogtv_semaphore);
	free(analogtv_services);
	analogtv_services = NULL;
	analogtv_service_count = 0;
	mysem_release(analogtv_semaphore);
	
	if (permanent) remove(appControlInfo.tvInfo.channelConfigFile);
	return;
}

int analogtv_readServicesFromFile ()
{
	if (!helperFileExists(appControlInfo.tvInfo.channelConfigFile)) return -1;

	int res = 0;
	analogtv_clearServiceList(0);

	/// TODO : read from XML file and set analogtv_service_count

	eprintf("%s: loaded %d services\n", __FUNCTION__, analogtv_service_count);
	
	return res;
}

int analogtv_serviceScan (interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STSDK

	uint32_t from_freq, to_freq;

	if (pArg != NULL){
		from_freq = ((analogtv_freq_range_t*)pArg)->from_freq;
		to_freq = ((analogtv_freq_range_t*)pArg)->to_freq;
		
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
	int res = -1;
	
	if (!params)
	{
		eprintf("%s: out of memory\n", __FUNCTION__);
		return -1;
	}

	cJSON_AddItemToObject(params, "from_freq", cJSON_CreateNumber(from_freq));
	cJSON_AddItemToObject(params, "to_freq", cJSON_CreateNumber(to_freq));

	res = st_rpcSync(elcmd_tvscan, params, &type, &result );
	if (result && result->valuestring != NULL && strcmp (result->valuestring, "ok") == 0)
	{
		/// TODO
		
		// elcd dumped services to file. read it
		analogtv_readServicesFromFile();
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
	analogtv_readServicesFromFile();
}

void analogtv_terminate(void)
{
	/// TODO: additional cleanup
	
	analogtv_stop();

	free(analogtv_services);
	analogtv_service_count = 0;

	mysem_destroy(analogtv_semaphore);
}

#endif /* ENABLE_ANALOGTV */
