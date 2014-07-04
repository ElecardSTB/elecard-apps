/*
 off_air.c

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

#include "off_air.h"

#include "bouquet.h"
#include "debug.h"
#include "dvbChannel.h"
#include "app_info.h"
#include "sem.h"
#include "gfx.h"
#include "interface.h"
#include "l10n.h"
#include "menu_app.h"
#include "output.h"
#include "StbMainApp.h"
#include "helper.h"
#include "media.h"
#include "playlist.h"
#include "rtp.h"
#include "rtsp.h"
#include "dlna.h"
#include "list.h"
#ifdef ENABLE_PVR
#include "pvr.h"
#endif
#ifdef ENABLE_STATS
#include "stats.h"
#endif
#include "teletext.h"
#include "stsdk.h"
#include "analogtv.h"

#ifdef STBPNX
#include <phStbRpc_Common.h>
#include <phStbSystemManager.h>
#endif

// NETLib
#include <sdp.h>
#include <tools.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>

//extern char scan_messages[64*1024];

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

interfaceEpgMenu_t EPGRecordMenu;
interfaceColor_t genre_colors[0x10] = {
	{ 151, 200, 142, 0x88 }, // other
	{ 255,  94,  81, 0x88 }, // movie
	{ 175, 175, 175, 0x88 }, // news
	{ 251, 147,  46, 0x88 }, // show
	{  89, 143, 198, 0x88 }, // sports
	{ 245, 231,  47, 0x88 }, // children's
	{ 255, 215, 182, 0x88 }, // music
	{ 165,  81,  13, 0x88 }, // art
	{ 236, 235, 235, 0x88 }, // politics
	{ 160, 151, 198, 0x88 }, // education
	{ 162, 160,   5, 0x88 }, // leisure
	{ 151, 200, 142, 0x88 }, // special
	{ 151, 200, 142, 0x88 }, // 0xC
	{ 151, 200, 142, 0x88 }, // 0xD
	{ 151, 200, 142, 0x88 }, // 0xE
	{ 151, 200, 142, 0x88 }  // 0xF
};

#ifdef ENABLE_DVB
interfaceListMenu_t DVBTMenu;
#endif // ENABLE_DVB


/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define MENU_ITEM_TIMELINE     (0)
#define MENU_ITEM_EVENT        (1)

#ifdef ENABLE_DVB
#define OFFAIR_MULTIVIEW_FILENAME        "/tmp/dvb.ts"
#define OFFAIR_MULTIVIEW_INFOFILE        "/tmp/dvb.multi"

#define EPG_UPDATE_INTERVAL    (1000)

#define PSI_UPDATE_INTERVAL    (1000)

#define WIZARD_UPDATE_INTERVAL (100)
#endif

#define CHANNEL_STATUS_ID 1

#define DEFAULT_FREQUENCY 474000000


/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

#ifdef ENABLE_DVB
typedef enum {
	wizardStateDisabled = 0,
	wizardStateConfirmLocation,
	wizardStateSelectLocation,
	wizardStateConfirmManualScan,
	wizardStateInitialFrequencyScan,
	wizardStateInitialFrequencyMonitor,
	wizardStateInitialServiceScan,
	wizardStateConfirmUpdate,
	wizardStateUpdating,
	wizardStateSelectChannel,
	wizardStateCustomFrequencySelect,
	wizardStateCustomFrequencyMonitor,

	wizardStateCount
} wizardState_t;

typedef struct {
	wizardState_t state;
	int allowExit;
	int locationCount;
	struct dirent **locationFiles;
	unsigned long frequency[64];
	unsigned long frequencyCount;
	unsigned long frequencyIndex;
	interfaceMenu_t *pFallbackMenu;
} wizardSettings_t;

#ifdef ENABLE_MULTI_VIEW
typedef struct {
	int stop;
	int exit;
	pthread_t thread;
} offair_multiviewInstance_t;
#endif

typedef struct {
	int which;
	DvbParam_t pParam;
	int audio_type;
	int video_type;
} offair_confDvbStart_t;

#endif /* ENABLE_DVB */

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

#ifdef ENABLE_DVB
static interfaceListMenu_t EPGMenu;

static struct {
	struct {
		__u32 frequency;
		struct {
			__u32 index;
			__u32 count;
		} nit;
	} scan;
} dvb;

static int  offair_scheduleIndex;   // service number
//static char offair_lcn_buf[4];

static struct {
	int index;
	list_element_t *stream;
	int visible;
} subtitle = {
	.index   = 0,
	.stream  = NULL,
	.visible = 0,
};

static pmysem_t epg_semaphore = 0;
static pmysem_t offair_semaphore = 0;

static interfaceListMenu_t wizardHelperMenu;
static wizardSettings_t   *wizardSettings = NULL;

static int32_t needRefill = 1;

#endif // ENABLE_DVB


/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
#ifdef ENABLE_DVB
static void offair_setStateCheckTimer(int which, int bEnable);
static int  offair_startNextChannel(int direction, void* pArg);
static int  offair_infoTimerEvent(void *pArg);
static int  offair_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static void offair_startDvbVideo(int which, DvbParam_t *param, int audio_type, int video_type);
static void offair_checkParentControl(int which, DvbParam_t *param, int audio_type, int video_type);
static int  offair_fillEPGMenu(interfaceMenu_t *pMenu, void* pArg);
static void offair_sortEvents(list_element_t **event_list);
static int  offair_getUserFrequency(interfaceMenu_t *pMenu, char *value, void* pArg);
//static int  offair_getUserLCN(interfaceMenu_t *pMenu, char *value, void* pArg);
//static int  offair_changeLCN(interfaceMenu_t *pMenu, void* pArg);
static int  offair_showSchedule(interfaceMenu_t *pMenu, int channel);
static int  offair_showScheduleMenu(interfaceMenu_t *pMenu, void* pArg);
static int  offair_updateEPG(void* pArg);
static int  offair_playControlProcessCommand(pinterfaceCommandEvent_t cmd, void* pArg);
static int  offair_audioChanged(void* pArg);
#ifdef ENABLE_DVB_DIAG
static int  offair_updatePSI(void* pArg);
#endif
static int  offair_confirmAutoScan(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);

static void offair_getServiceDescription(EIT_service_t *service, char *desc, char *mode);
#ifdef ENABLE_PVR
static int  offair_confirmStopRecording(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg);
static int  offair_stopRecording(interfaceMenu_t *pMenu, void *pArg);
static void offair_EPGRecordMenuDisplay(interfaceMenu_t *pMenu);
static int  offair_EPGRecordMenuProcessCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd);
static int  offair_EPGMenuKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#endif
#ifdef ENABLE_MULTI_VIEW
static void *offair_multiviewStopThread(void* pArg);
static int  offair_multi_callback(pinterfaceCommandEvent_t cmd, void* pArg);
static int  offair_multiviewNext(int direction, void* pArg);
static int  offair_multiviewPlay( interfaceMenu_t *pMenu, void *pArg);
#endif
#ifdef ENABLE_STATS
static int  offair_updateStats(int which);
static int  offair_updateStatsEvent(void* pArg);
#endif

#ifdef ENABLE_DVB_DIAG
static int  offair_checkSignal(int which, list_element_t **pPSI);
#endif

static int  wizard_init(void);
static int  wizard_show(int allowExit, int displayMenu, interfaceMenu_t *pFallbackMenu, unsigned long monitor_only_frequency);
static void wizard_cleanup(int finished);

static int offair_subtitleShow(uint16_t subtitle_pid);

static inline EIT_service_t *current_service(void)
{
	return dvbChannel_getService(appControlInfo.dvbInfo.channel);
}

static inline int has_video(int channel)
{
	return dvb_hasMediaType(dvbChannel_getService(channel), mediaTypeVideo);
}

static inline int can_play(int channel)
{
	EIT_service_t *service = dvbChannel_getService(channel);
	return (service != NULL) &&
			((appControlInfo.offairInfo.dvbShowScrambled == SCRAMBLED_PLAY) || (dvb_getScrambled(service) == 0));
}

#endif /* ENABLE_DVB */

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

#ifdef ENABLE_DVB
uint32_t offair_getTuner(void)
{
	offair_stopVideo(screenMain, 1);
	return appControlInfo.dvbInfo.adapter;
}

static long offair_getFrequency(void *pArg)
{
    return dvb.scan.frequency;
}

static long offair_getFrequencyIndex(void *pArg)
{
    return dvb.scan.nit.index;
}

void offair_setFrequency(long value, void *pArg)
{

}

static void offair_buildInstallSlider(int numChannels, uint32_t adapter)
{
	char channelEntry[256];
	char installationString[2048];
	char *serviceName;
	__u32 low_freq, high_freq, freq_step;
	int i=0;

	dvbfe_getTuner_freqs(adapter, &low_freq, &high_freq, & freq_step);

	sprintf(installationString, _T("FOUND_CHANNELS"), dvb.scan.frequency, dvbfe_getType(adapter) == SYS_DVBS ? _T("MHZ") : _T("KHZ"), numChannels);
	// in this moment semaphore is busy
	for ( i = numChannels-1; i > numChannels-4; i-- )
	{
		//int channelNumber;
		if ((serviceName = dvb_getTempServiceName(i)) != NULL)
		{
			sprintf(channelEntry, "[%s] ", serviceName);
			strcat(installationString, channelEntry);
		}
	}
	if (numChannels > 0)
	{
		strcat(installationString, "...");
	}

	//dprintf("%s: '%s', min %d, max %d, step %d, freq %d\n", __FUNCTION__,
	//	installationString, low_freq, high_freq, freq_step, dvb.scan.frequency_khz);

	interface_sliderSetText(installationString);
	if (dvb.scan.nit.count == 0)
	{
		interface_sliderSetMinValue(low_freq/1000);
		interface_sliderSetMaxValue(high_freq/1000);
		interface_sliderSetCallbacks(offair_getFrequency, offair_setFrequency, NULL);
	} else
	{
		interface_sliderSetMinValue(0);
		interface_sliderSetMaxValue(dvb.scan.nit.count);
		interface_sliderSetCallbacks(offair_getFrequencyIndex, offair_setFrequency, NULL);
	}
	interface_sliderSetDivisions(100);
	interface_sliderShow(1, 1);
}

static int offair_updateDisplay(uint32_t frequency, int channelCount, uint32_t adapter, int frequencyIndex, int frequencyCount)
{
//	interfaceCommand_t cmd;

	dprintf("%s: in\n", __FUNCTION__);

	//dprintf("%s: freq: %d\n", __FUNCTION__, frequency);
	dvb.scan.frequency = frequency/1000;
	dvb.scan.nit.index = frequencyIndex;
	dvb.scan.nit.count = frequencyCount;
	offair_buildInstallSlider(channelCount, adapter);
	//interface_displayMenu(1);

/*	while ((cmd = helperGetEvent(0)) != interfaceCommandNone)
	{
		//dprintf("%s: got command %d\n", __FUNCTION__, cmd);
		if (cmd != interfaceCommandCount)
		{
			dprintf("%s: exit on command %d\n", __FUNCTION__, cmd);
			// Flush any waiting events
			helperGetEvent(1);
			return -1;
		}
	}*/

	//dprintf("%s: got none %d\n", __FUNCTION__, cmd);

	return keepCommandLoopAlive ? 0 : -1;
}

static void offair_setInfoUpdateTimer(int which, int bEnable)
{
	//dprintf("%s: %s info timer\n", __FUNCTION__, bEnable ? "set" : "unset");

	if(bEnable) {
		offair_infoTimerEvent(SET_NUMBER(which));
	} else {
		//interface_notifyText(NULL, 1);
		interface_customSlider(NULL, NULL, 0, 1);
		interface_removeEvent(offair_infoTimerEvent, SET_NUMBER(which));
	}
}

static int32_t offair_sliderCallback(int32_t id, interfaceCustomSlider_t *info, void *pArg)
{
	uint16_t snr, signal;
	uint32_t ber, uncorrected_blocks;
	fe_status_t status;

	if(id < 0 || info == NULL) {
		return 3;
	}

	//dprintf("%s: get info 0x%08X\n", __FUNCTION__, info);
	status = dvbfe_getSignalInfo(appControlInfo.dvbInfo.adapter, &snr, &signal, &ber, &uncorrected_blocks);
	switch(id) {
	case 0:
		info->min = 0;
		info->max = MAX_SIGNAL;
		info->value = info->max > (signal&SIGNAL_MASK) ? (signal&SIGNAL_MASK) : info->max;
		info->steps = 3;
		sprintf(info->caption, _T("DVB_SIGNAL_INFO"), info->value*100/(info->max-info->min), _T(status == 1 ? "LOCKED" : "NO_LOCK"));
		break;
	case 1:
		info->min = 0;
		info->max = MAX_BER;
		info->value = status == 1 && info->max > (int)ber ? info->max-(int)ber : info->min;
		info->steps = 3;
		sprintf(info->caption, _T("DVB_BER"), info->value*100/(info->max-info->min));
		break;
	case 2:
		info->min = 0;
		info->max = MAX_UNC;
		info->value = status == 1 && info->max > (int)uncorrected_blocks ? info->max-(int)uncorrected_blocks : info->min;
		info->steps = 1;
		sprintf(info->caption, _T("DVB_UNCORRECTED"), info->value*100/(info->max-info->min));
		break;
		/*		case 3:
		info->min = 0;
		info->max = 255;
		info->value = (snr&0xFF) == 0 ? 0xFF : snr&0xFF;
		sprintf(info->caption, _T("DVB_SNR"), info->value*100/(info->max-info->min));
		break;*/
	default:
		return -1;
	}

	//dprintf("%s: done\n", __FUNCTION__);

	return 1;
}

static int offair_infoTimerEvent(void *pArg)
{

	int tuner = GET_NUMBER(pArg);
	/*char buf[BUFFER_SIZE];

	uint16_t snr, signal;
	uint32_t ber, uncorrected_blocks;
	fe_status_t status;*/

	mysem_get(offair_semaphore);

	if (appControlInfo.dvbInfo.active)
	{
		if (appControlInfo.dvbInfo.showInfo)
		{
			/*status = dvbfe_getSignalInfo(appControlInfo.dvbInfo.adapter, &snr, &signal, &ber, &uncorrected_blocks);

			sprintf(buf, _T("DVB_SIGNAL_INFO"),
				signal&0xFF, ber, uncorrected_blocks, _T(status == 1 ? "LOCKED" : "NO_LOCK") );*/
	/*
			sprintf(buf, LANG_TEXT_DVB_SIGNAL_INFO "%s",
				appControlInfo.tunerInfo[tuner].signal_strength,
				appControlInfo.tunerInfo[tuner].snr,
				appControlInfo.tunerInfo[tuner].ber,
				appControlInfo.tunerInfo[tuner].uncorrected_blocks,
				appControlInfo.tunerInfo[tuner].fe_status & FE_HAS_LOCK ? LANG_TEXT_LOCKED : LANG_TEXT_NO_LOCK);
	*/
			//interface_notifyText(buf, 1);
			interface_customSlider(offair_sliderCallback, pArg, 0, 1);
		}

		//offair_setInfoUpdateTimer(tuner, 1);

		interface_addEvent(offair_infoTimerEvent, SET_NUMBER(tuner), INFO_TIMER_PERIOD, 1);
	}

	mysem_release(offair_semaphore);

	return 0;
}

static int offair_stateTimerEvent(void *pArg)
{
	int which = GET_NUMBER(pArg);
	DFBVideoProviderStatus status;

	mysem_get(offair_semaphore);

	if (appControlInfo.dvbInfo.active)
	{
		status = gfx_getVideoProviderStatus(which);
		switch( status )
		{
			case DVSTATE_FINISHED:
			case DVSTATE_STOP:
				//interface_showMenu(1, 0);
				mysem_release(offair_semaphore);
				if(status == DVSTATE_FINISHED)
					interface_showMessageBox(_T("ERR_STREAM_NOT_SUPPORTED"), thumbnail_error, 0);
				offair_stopVideo(which, 1);
				return 0;
			default:
				offair_setStateCheckTimer(which, 1);
		}
	}

	mysem_release(offair_semaphore);

	return 0;
}

static void offair_setStateCheckTimer(int which, int bEnable)
{
	//dprintf("%s: %s state timer\n", __FUNCTION__, bEnable ? "set" : "unset");

	if (bEnable)
	{
		interface_addEvent(offair_stateTimerEvent, SET_NUMBER(which), 1000, 1);
	} else
	{
		interface_removeEvent(offair_stateTimerEvent, SET_NUMBER(which));
	}
}

int offair_wizardStart(interfaceMenu_t *pMenu, void* pArg)
{
	dprintf("%s: in\n", __FUNCTION__);
	if (wizard_show(1, 1, pMenu, (unsigned long)pArg) == 0)
	{
		//interface_showMessageBox(_T("SETTINGS_WIZARD_NO_LOCATIONS"), thumbnail_warning, 5000);
		output_showDVBMenu(pMenu, NULL);
		interface_showMenu(1, 1);
	}

	return 0;
}

static int32_t offair_scanFrequency(interfaceMenu_t *pMenu, uint32_t adapter, uint32_t frequency)
{
	dprintf("%s(): Adapter=%u scan freq=%u\n", __FUNCTION__, adapter, frequency);

	interfaceCommand_t cmd = helperGetEvent(1);
	if (cmd == interfaceCommandRed) {
		return -1;
	}
	
	if(dvb_frequencyScan(adapter, frequency, NULL, offair_updateDisplay, 1, NULL) == 0) {
		interface_refreshMenu(pMenu);
		output_showDVBMenu(pMenu, NULL);
		bouquet_addScanChannels();

#ifdef ENABLE_PVR
		pvr_updateSettings();
#endif
	}

	return 0;
}


int offair_serviceScan(interfaceMenu_t *pMenu, void* pArg)
{	
	uint32_t adapter;
	uint32_t low_freq, high_freq, freq_step, freq_substep, frequency;
	int32_t which = GET_NUMBER(pArg);
	char buf[256];

	adapter = offair_getTuner();
	dvbfe_getTuner_freqs(adapter, &low_freq, &high_freq, &freq_step);
	
	freq_substep = (DEFAULT_FREQUENCY - low_freq) % freq_step;
	if(freq_substep != 0) {
		freq_step = freq_step - freq_substep;
	}

	for(frequency = low_freq; frequency <= high_freq; frequency += freq_step) {
		dprintf( "%s: [ %u < %u < %u ]\n", __FUNCTION__, low_freq, frequency, high_freq);
		if(frequency < low_freq || frequency > high_freq) {
			eprintf("offair: %u is out of frequency range\n", frequency);
			interface_showMessageBox(_T("ERR_FREQUENCY_OUT_OF_RANGE"), thumbnail_error, 0);
			return -1;
		}
		interface_hideMessageBox();
		offair_updateDisplay(frequency, dvb_getNumberOfServices(), which, 0, 1);

		if(offair_scanFrequency(pMenu, adapter, frequency) < 0) {
			interface_hideMessageBox();
			interface_sliderShow(0, 0);
			sprintf(buf, _T("Scan was stopped. Found %d channels"), dvb_getNumberOfServices());
			interface_showMessageBox(buf, thumbnail_info, 5000);
			return -1;
		}
		interface_sliderShow(0, 0);
		sprintf(buf, _T("SCAN_COMPLETE_CHANNELS_FOUND"), dvb_getNumberOfServices());
		interface_showMessageBox(buf, thumbnail_info, 5000);
		if(freq_substep) {
			uint32_t temp_step;
			temp_step = freq_step;
			freq_step = freq_substep;
			freq_substep = temp_step;
		}
	}
	return -1;

/*	uint32_t adapter;

	adapter = offair_getTuner();
	__u32 low_freq, high_freq, freq_step;
	char buf[256];

	dvbfe_getTuner_freqs(adapter, &low_freq, &high_freq, &freq_step);
	offair_updateDisplay(low_freq, 0, adapter, 0, 0);
	dvb_serviceScan(adapter, offair_updateDisplay);*/
	
	/* Re-build any channel related menus */
	/*memset(&DVBTChannelMenu[0], 0, sizeof(interfaceListMenu_t));
	memset(&DVBTChannelMenu[1], 0, sizeof(interfaceListMenu_t));*/
	/*appControlInfo.dvbInfo.channel = 0;//dvb_getChannelNumber(0);
	
	interface_refreshMenu(pMenu);
	output_showDVBMenu(pMenu, NULL);

#ifdef ENABLE_PVR
	pvr_updateSettings();
#endif*/
	//interface_showMessageBox(scan_messages, NULL, 0);
	/*interface_sliderShow(0, 0);
	sprintf(buf, _T("SCAN_COMPLETE_CHANNELS_FOUND"), dvb_getNumberOfServices());
	interface_showMessageBox(buf, thumbnail_info, 5000);	
	
	return  -1;*/
}

static int offair_getUserFrequencyToMonitor(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	unsigned long low_freq = MIN_FREQUENCY_KHZ*KHZ, high_freq = MAX_FREQUENCY_KHZ*KHZ;
	unsigned long freq;
	
	if( value == NULL)
		return 1;

	freq = strtoul(value, NULL, 10)*KHZ;

	if (freq == 0)
	{
		if (dvb_getNumberOfServices() > 0)
		{
			interface_hideMessageBox();
			offair_wizardStart(pMenu, (void*)-1);
		} else
		{
			interface_showMessageBox(_T("ERR_FREUQENCY_LIST_EMPTY"), thumbnail_error, 0);
			return -1;
		}
	} else if (freq >= low_freq && freq <= high_freq)
	{
		interface_hideMessageBox();
		offair_wizardStart(pMenu, (void*)freq);
	} else
	{
		interface_showMessageBox(_T("ERR_FREQUENCY_OUT_OF_RANGE"), thumbnail_error, 0);
		return -1;
	}

	return 0;
}

int offair_frequencyMonitor(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[BUFFER_SIZE];
	__u32 low_freq  = MIN_FREQUENCY_KHZ*KHZ;
	__u32 high_freq = MAX_FREQUENCY_KHZ*KHZ;

	sprintf(buf, "%s [%u;%u] (%s)", _T("ENTER_MONITOR_FREQUENCY"), low_freq / KHZ, high_freq / KHZ,_T("KHZ"));
	interface_getText(pMenu, buf, "\\d{6}", offair_getUserFrequencyToMonitor, NULL, inputModeDirect, pArg);
	return 0;
}

static char* offair_getLastFrequency(int field, void* pArg)
{
	if (field != 0)
		return NULL;
	static char frequency[10];
	snprintf(frequency, sizeof(frequency), "%u", dvb.scan.frequency);
	return frequency;
}

int offair_frequencyScan(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[BUFFER_SIZE];
	__u32 low_freq  =  MIN_FREQUENCY_KHZ*KHZ;
	__u32 high_freq =  MAX_FREQUENCY_KHZ*KHZ;
	__u32 freq_step = FREQUENCY_STEP_KHZ*KHZ;
	uint32_t adapter = offair_getTuner();
	dvbfe_getTuner_freqs(adapter, &low_freq, &high_freq, &freq_step);
	if (dvb.scan.frequency*KHZ < low_freq || dvb.scan.frequency*KHZ > high_freq)
		dvb.scan.frequency = low_freq/KHZ;

	sprintf(buf, "%s [%u;%u] (%s)", _T("ENTER_FREQUENCY"),
		low_freq / KHZ, high_freq / KHZ, dvbfe_getType(adapter) == SYS_DVBS ? _T("MHZ") : _T("KHZ"));
	const char *mask = "\\d{6}";
	if (dvbfe_getType(adapter) == SYS_DVBS)
		mask = appControlInfo.dvbsInfo.band == dvbsBandK ? "\\d{5}" : "\\d{4}";
	interface_getText(pMenu, buf, mask, offair_getUserFrequency, offair_getLastFrequency, inputModeDirect, pArg);
	return 0;
}

static int32_t offair_getUserFrequency(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	uint32_t adapter;
	int32_t which = GET_NUMBER(pArg);
	uint32_t low_freq = MIN_FREQUENCY_KHZ*KHZ, high_freq = MAX_FREQUENCY_KHZ*KHZ, frequency, freq_step;
	char buf[256];

	if(value == NULL) {
		return 1;
	}

	frequency  = strtoul(value, NULL, 10)*KHZ;
	adapter = offair_getTuner();
	dvbfe_getTuner_freqs(adapter, &low_freq, &high_freq, &freq_step);

	dprintf( "%s: [ %u < %u < %u ]\n", __FUNCTION__, low_freq, frequency, high_freq);
	if(frequency < low_freq || frequency > high_freq) {
		eprintf("offair: %u is out of frequency range\n", frequency);
		interface_showMessageBox(_T("ERR_FREQUENCY_OUT_OF_RANGE"), thumbnail_error, 0);
		return -1;
	}
	interface_hideMessageBox();
	offair_updateDisplay(frequency, dvb_getNumberOfServices(), which, 0, 1);

	if(offair_scanFrequency(pMenu, adapter, frequency) < 0) {
		interface_hideMessageBox();
		interface_sliderShow(0, 0);
		sprintf(buf, _T("Scan was stopped. Found %d channels"), dvb_getNumberOfServices());
		interface_showMessageBox(buf, thumbnail_info, 5000);
		return -1;
	}

	interface_sliderShow(0, 0);
	sprintf(buf, _T("SCAN_COMPLETE_CHANNELS_FOUND"), dvb_getNumberOfServices());
	interface_showMessageBox(buf, thumbnail_info, 5000);

	return -1;
}

void offair_stopVideo(int which, int reset)
{
	mysem_get(offair_semaphore);

	if(appControlInfo.dvbInfo.active) {
		interface_playControlSelect(interfacePlayControlStop);

#ifdef ENABLE_DVB_DIAG
		interface_removeEvent(offair_updatePSI, NULL);
#endif
		interface_removeEvent(offair_updateEPG, NULL);
#ifdef ENABLE_STATS
		interface_removeEvent(offair_updateStatsEvent, NULL);
		offair_updateStats(which);
#endif

#ifdef ENABLE_MULTI_VIEW
		offair_multiviewInstance_t multiviewStopInstance;
		int thread_create = -1;
		/* Force stop provider in multiview mode */
		if((appControlInfo.multiviewInfo.count > 0) && (appControlInfo.multiviewInfo.source == streamSourceDVB)) {
			multiviewStopInstance.stop = 0;
			multiviewStopInstance.exit = 0;
			thread_create = pthread_create(&multiviewStopInstance.thread, NULL,  offair_multiviewStopThread,  &multiviewStopInstance);
		}
		/* Force stop provider in multiview mode */
		gfx_stopVideoProvider(which, reset || (appControlInfo.multiviewInfo.count > 0), 1);
		if(!thread_create) {
			appControlInfo.multiviewInfo.count = 0;
			multiviewStopInstance.stop = 1;
			while(!multiviewStopInstance.exit) {
				usleep(100000);
			}
		}
#else
		gfx_stopVideoProvider(which, reset, 1);
#endif
#ifdef ENABLE_PVR
		if(pvr_isRecordingDVB()) {
			pvr_stopPlayback(screenMain);
		}
#endif

		dprintf("%s: Stop video \n", __FUNCTION__);
		teletext_stop();
		dvb_stopDVB(appControlInfo.dvbInfo.adapter, reset);
		appControlInfo.dvbInfo.active = 0;

		offair_setStateCheckTimer(which, 0);
		offair_setInfoUpdateTimer(which, 0);

		interface_disableBackground();
	}

	mysem_release(offair_semaphore);
}

#if !(defined STSDK)
static int32_t offair_audioChange(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	service_index_t	*srvIdx = dvbChannel_getServiceIndex(appControlInfo.dvbInfo.channel);
	EIT_service_t	*service = srvIdx ? srvIdx->service : NULL;
	int32_t	audioCount;
	int32_t	which = CHANNEL_INFO_GET_SCREEN(pArg);
	int32_t	selected = CHANNEL_INFO_GET_CHANNEL(pArg);
	char	buf[MENU_ENTRY_INFO_LENGTH];
	char	str[MENU_ENTRY_INFO_LENGTH];

	if(service == NULL) {
		return 0;//hide message box
	}
	audioCount = dvb_getAudioCount(service);
	if(cmd != NULL) {
		switch(cmd->command) {
		case interfaceCommandExit:
		case interfaceCommandRed:
		case interfaceCommandLeft:
			return 0;
		case interfaceCommandEnter:
		case interfaceCommandOk:
		case interfaceCommandGreen:
			if(srvIdx->data.audio_track != selected) {
				if(dvb_getAudioType(service, selected) !=
					dvb_getAudioType(service, srvIdx->data.audio_track))
				{
					offair_stopVideo(which, 0);
					srvIdx->data.audio_track = selected;
					offair_startVideo(which);
				} else {
					dvb_changeAudioPid(appControlInfo.dvbInfo.adapter, dvb_getAudioPid(service, selected));
					srvIdx->data.audio_track = selected;
				}

				dvbChannel_save();
			}
			return 0;
		case interfaceCommandDown:
			selected++;
			if(selected >= audioCount) {
				selected = audioCount - 1;
			}
			break;
		case interfaceCommandUp:
			selected--;
			if(selected < 0) {
				selected = 0;
			}
			break;
		default:
			  break;
		}
	} else {
		srvIdx = dvbChannel_getServiceIndex(appControlInfo.dvbInfo.channel);
		selected = srvIdx ? srvIdx->data.audio_track : 0;
	}

	buf[0] = 0;
	if(audioCount > 0) {
		for(int i = 0; i < audioCount; i++) {
			if(selected == i) {
				sprintf(str, "> Audio Track %d [%d] <\n", i, audioCount);
			} else {
				sprintf(str, "    Audio Track %d [%d]\n", i, audioCount);
			}
			strcat(buf, str);
		}
		buf[strlen(buf)-1] = 0;

		interface_showConfirmationBox(buf, -1, offair_audioChange, CHANNEL_INFO_SET(which, selected));
	}
	return 1;
}
#endif // !STSDK

int offair_play_callback(interfacePlayControlButton_t button, void *pArg)
{
	int which = GET_NUMBER(pArg);

	dprintf("%s: in %d\n", __FUNCTION__, button);

	if(button == interfacePlayControlPrevious) {
		offair_startNextChannel(1, pArg);
	} else if(button == interfacePlayControlNext) {
		offair_startNextChannel(0, pArg);
	} else if(button == interfacePlayControlPlay) {
		dprintf("%s: play\n", __FUNCTION__);
		if(!appControlInfo.dvbInfo.active) {
			if(appControlInfo.playbackInfo.streamSource == streamSourceDVB) {
				offair_startVideo(which);
			} else if(appControlInfo.playbackInfo.streamSource == streamSourceAnalogTV) {
#ifdef ENABLE_ANALOGTV
				interfaceMenu_t *channelMenu = _M &DVBTMenu;
				analogtv_activateChannel(channelMenu, (void *)appControlInfo.tvInfo.id);
#endif
			}
		}
	} else if(button == interfacePlayControlStop) {
		dprintf("%s: stop\n", __FUNCTION__);
#ifdef ENABLE_PVR
		if(pvr_isPlayingDVB(which)) {
			pvr_stopRecordingDVB(which);
			if(appControlInfo.dvbInfo.channel > 0) {
				char desc[BUFFER_SIZE];
				offair_getServiceDescription(current_service(),desc,_T("DVB_CHANNELS"));
				interface_playControlUpdateDescriptionThumbnail(desc, service_thumbnail(current_service()));
			}
		}
#endif
		offair_stopVideo(which, 1);
	} else if(button == interfacePlayControlInfo) {
#ifdef ENABLE_DVB_DIAG
		/* Make diagnostics... */
		//if (appControlInfo.offairInfo.diagnosticsMode == DIAG_ON)
		{
			offair_checkSignal(which, NULL);
		}
		//offair_setInfoUpdateTimer(which, 1);
		interface_displayMenu(1);
#else
		appControlInfo.dvbInfo.showInfo = !appControlInfo.dvbInfo.showInfo;
		offair_setInfoUpdateTimer(which, appControlInfo.dvbInfo.showInfo);
		interface_displayMenu(1);
#endif
		return 0;
	} else
#if 0 // !STSDK
	if(button == interfacePlayControlAudioTracks) {
		//dprintf("%S: request change tracks\n", __FUNCTION__);
		if(dvb_getAudioCount(current_service()) > 0) {
			//dprintf("%s: display change tracks\n", __FUNCTION__);
			offair_audioChange(interfaceInfo.currentMenu, NULL, CHANNEL_INFO_SET(which, 0));
		}
		interface_displayMenu(1);
		return 0;
	} else
#endif // !STSDK
#ifdef DVB_FAVORITES
	if(button == interfacePlayControlAddToPlaylist) {
		char desc[BUFFER_SIZE];
		dprintf("%s: add to playlist %d\n", __FUNCTION__, appControlInfo.dvbInfo.channel);
		dvb_getServiceURL(current_service(), desc);
		eprintf("offair: Add to Playlist '%s'\n", desc);
		playlist_addUrl(desc, dvb_getServiceName(current_service()));
	} else
#endif // DVB_FAVORITES
#ifdef ENABLE_PVR
	if(button == interfacePlayControlRecord) {
		pvr_toogleRecordingDVB();
	} else
#endif
	{
		// default action
		return 1;
	}

	interface_displayMenu(1);

	dprintf("%s: done\n", __FUNCTION__);

	return 0;
}

#ifdef ENABLE_MULTI_VIEW
static void *offair_multiviewStopThread(void* pArg)
{
	offair_multiviewInstance_t *multiviewStopInstance = (offair_multiviewInstance_t *)pArg;
	char buffer[128];
	int fp ;

	fp = open(OFFAIR_MULTIVIEW_FILENAME, O_WRONLY);
	if(fp>0)
	{
		int writeData = 0;
		memset(buffer,0xFF,128);
		while(!multiviewStopInstance->stop)
		{
			writeData = write(fp,(const void *)buffer,128);
			usleep(1000);
		}
		close(fp);
	}else
	{
		eprintf("offair: Can't open multiview file '%s'\n", OFFAIR_MULTIVIEW_FILENAME);
	}
	multiviewStopInstance->exit = 1;
	pthread_cancel (multiviewStopInstance->thread);
	return 0;
}

static int offair_multiviewPlay(interfaceMenu_t *pMenu, void *pArg)
{
	uint32_t	f1 = 0;
	uint32_t	f;
	uint32_t	adapter;
	int32_t		mvCount, i;
	int32_t		payload[4];
	int32_t		channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
	FILE		*file;
	DvbParam_t	param;
	EIT_service_t	*curService = dvbChannel_getService(channelNumber);

	if(curService == NULL) {
		eprintf("%s: Can't get current channel description %d\n", __FUNCTION__, channelNumber);
		return -1;
	}
	if((dvb_getServiceFrequency(curService, &f1) != 0) || (f1 == 0)) {
		eprintf("%s: Can't determine frequency of channel %d\n", __FUNCTION__, channelNumber);
		return -1;
	}

	if(offair_findCapableTuner(curService, &adapter) != 0) {
		eprintf("%s: Failed to find tuner matching type %d\n", __FUNCTION__, curService->media.type);
		return -1;
	}

	gfx_stopVideoProviders(screenMain);

	param.mode = DvbMode_Multi;
	param.frequency = f1;
	appControlInfo.multiviewInfo.source = streamSourceDVB;
	appControlInfo.multiviewInfo.pArg[0] = CHANNEL_INFO_SET(screenMain, channelNumber);
	dvb_getPIDs(curService, -1, &param.param.multiParam.channels[0], NULL, NULL, NULL);
	payload[0] = dvb_hasPayloadType(curService, payloadTypeH264) ? payloadTypeH264 : payloadTypeMpeg2;
	param.media = &curService->media;
	mvCount = 1;
	for(i = channelNumber + 1; (mvCount < 4) && (i != channelNumber); i = (i + 1) % dvbChannel_getCount()) {
		EIT_service_t	*iService = dvbChannel_getService(i);
		if(can_play(i) && iService &&
			iService->media.type == curService->media.type &&
			dvb_getServiceFrequency(iService, &f) == 0 &&
			f == f1 &&
			has_video(i))
		{
			appControlInfo.multiviewInfo.pArg[mvCount] = CHANNEL_INFO_SET(screenMain, i);
			dvb_getPIDs(iService, -1, &param.param.multiParam.channels[mvCount], NULL, NULL, NULL);
			payload[mvCount] = dvb_hasPayloadType(iService, payloadTypeH264) ? payloadTypeH264 : payloadTypeMpeg2;
			mvCount++;
		}
	}
	for(i = mvCount; i < 4; i++) {
		appControlInfo.multiviewInfo.pArg[i] = NULL;
		param.param.multiParam.channels[i] = 0;
	}
	appControlInfo.multiviewInfo.selected = 0;
	appControlInfo.multiviewInfo.count = mvCount;

	param.adapter = adapter;
	param.directory = NULL;
	file = fopen(OFFAIR_MULTIVIEW_INFOFILE, "w");
	fprintf(file, "%d %d %d %d %d %d %d %d",
		payload[0], param.param.multiParam.channels[0],
		payload[1], param.param.multiParam.channels[1],
		payload[2], param.param.multiParam.channels[2],
		payload[3], param.param.multiParam.channels[3]);
	fclose(file);
	remove(OFFAIR_MULTIVIEW_FILENAME);
	mkfifo(OFFAIR_MULTIVIEW_FILENAME, S_IRUSR | S_IWUSR);

	appControlInfo.dvbInfo.showInfo = 0;
	offair_setInfoUpdateTimer(screenMain, 0);

	offair_checkParentControl(screenMain, &param, 0,payload[0]);
	return 0;
}

static int offair_multiviewNext(int direction, void* pArg)
{
	int indexdiff = direction == 0 ? 1 : -1;
	int channelIndex = CHANNEL_INFO_GET_CHANNEL(pArg);
	int newIndex = channelIndex;
	int i;
	for( i = 0; i < (indexdiff > 0 ? appControlInfo.multiviewInfo.count : 4); i++)
	{
		do
		{
			newIndex = (newIndex + dvbChannel_getCount() + indexdiff) % dvbChannel_getCount();
		} while (newIndex != channelIndex && (!can_play(newIndex) || !has_video(newIndex)));
		if( newIndex == channelIndex )
		{
			return -1;
		}
	}

	return offair_multiviewPlay( interfaceInfo.currentMenu, CHANNEL_INFO_SET(screenMain, newIndex) );
}

static int offair_multi_callback(pinterfaceCommandEvent_t cmd, void* pArg)
{

	dprintf("%s: in\n", __FUNCTION__);

	switch( cmd->command )
	{
		case interfaceCommandStop:
			offair_stopVideo(screenMain, 1);
			interface_showMenu(1, 1);
			break;
		case interfaceCommandOk:
		case interfaceCommandEnter:
			offair_channelChange(interfaceInfo.currentMenu, appControlInfo.multiviewInfo.pArg[appControlInfo.multiviewInfo.selected]);
			break;
		default:
			return interface_multiviewProcessCommand(cmd, pArg);
	}

	interface_displayMenu(1);
	return 0;
}

static void offair_displayMultiviewControl(void)
{
	int i, x, y, w;
	char number[4];
	int number_len;

	if( appControlInfo.multiviewInfo.count <= 0 || interfaceInfo.showMenu )
		return;
#if 0
	w = interfaceInfo.screenWidth / 2;
	switch( appControlInfo.multiviewInfo.selected )
	{
		case 1:  x = w; y = 0; break;
		case 2:  x = 0; y = interfaceInfo.screenHeight / 2; break;
		case 3:  x = w; y = interfaceInfo.screenHeight / 2; break;
		default: x = 0; y = 0;
	}
	if( appControlInfo.multiviewInfo.selected > 1 )
	{
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, x, y, w, interfaceInfo.paddingSize);
	} else
	{
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, x, y - interfaceInfo.paddingSize+interfaceInfo.screenHeight / 2, w, interfaceInfo.paddingSize);
	}
	if( appControlInfo.multiviewInfo.selected % 2 == 1 )
	{
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, x, y, interfaceInfo.paddingSize, interfaceInfo.screenHeight / 2);
	} else
	{
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, x + w - interfaceInfo.paddingSize, y, interfaceInfo.paddingSize, interfaceInfo.screenHeight / 2);
	}
#else
	switch( appControlInfo.multiviewInfo.selected )
	{
		case 1:  x = INTERFACE_ROUND_CORNER_RADIUS + interfaceInfo.screenWidth/2; y = interfaceInfo.marginSize; break;
		case 2:  x = interfaceInfo.marginSize; y = INTERFACE_ROUND_CORNER_RADIUS + interfaceInfo.screenHeight/2; break;
		case 3:  x = INTERFACE_ROUND_CORNER_RADIUS + interfaceInfo.screenWidth/2; y = INTERFACE_ROUND_CORNER_RADIUS + interfaceInfo.screenHeight/2; break;
		default: x = interfaceInfo.marginSize; y = interfaceInfo.marginSize;
	}
	snprintf(number, sizeof(number), "%03d", CHANNEL_INFO_GET_CHANNEL((int)appControlInfo.multiviewInfo.pArg[appControlInfo.multiviewInfo.selected]));
	number[sizeof(number)-1] = 0;
	number_len = strlen(number);
	w = INTERFACE_CLOCK_DIGIT_WIDTH*number_len;

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x+INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, w-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);

	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_ROUND_CORNER_RADIUS/2, w+INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS);

	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x+INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, w-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
	DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w-INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	for( i=0; i<number_len; i++ )
	{
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "digits.png", x, y, INTERFACE_CLOCK_DIGIT_WIDTH, INTERFACE_CLOCK_DIGIT_HEIGHT, 0, number[i]-'0', DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignTop);
		x += INTERFACE_CLOCK_DIGIT_WIDTH;
	}
#endif // 1
}
#endif // ENABLE_MULTI_VIEW

void offair_displayPlayControl(void)
{
	int x, y, w, h, fh, fa, sfh;
	DFBRectangle rect;
	char buffer[MAX_MESSAGE_BOX_LENGTH] = "";

	if( interfaceChannelControl.pSet != NULL && interfaceChannelControl.showingLength > 0 )
	{
		//interface_displayTextBox( interfaceInfo.screenWidth - interfaceInfo.marginSize + interfaceInfo.paddingSize + 22, interfaceInfo.marginSize, interfacePlayControl.channelNumber, NULL, 0, NULL, 0 );

		w = INTERFACE_CLOCK_DIGIT_WIDTH*interfaceChannelControl.showingLength;
		x = interfaceInfo.screenWidth - interfaceInfo.marginSize - INTERFACE_CLOCK_DIGIT_WIDTH*3 - INTERFACE_ROUND_CORNER_RADIUS/2;
		y = interfaceInfo.marginSize - 10;

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x+INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, w-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_ROUND_CORNER_RADIUS/2, w+INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS);

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x+INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, w-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
		DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w-INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

		for(int i=0; i<interfaceChannelControl.showingLength; i++ )
		{
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "digits.png", x, y, INTERFACE_CLOCK_DIGIT_WIDTH, INTERFACE_CLOCK_DIGIT_HEIGHT, 0, interfaceChannelControl.number[i]-'0', DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignTop);
			x += INTERFACE_CLOCK_DIGIT_WIDTH;
		}
	}

	if ( subtitle.visible ) {
		if (subtitle.stream) {
			PID_info_t *info = subtitle.stream->data;
			if (info->ISO_639_language_code[0])
				sprintf(buffer, "%s: %d (%s)", _T("SUBTITLES"), subtitle.index, info->ISO_639_language_code);
			else
				sprintf(buffer, "%s: %d", _T("SUBTITLES"), subtitle.index);
		} else
			sprintf(buffer, "%s: %s", _T("SUBTITLES"), _T("OFF"));
		interface_displayTextBox(
			interfaceInfo.clientX + interfaceInfo.marginSize,
			interfaceInfo.clientY,
			buffer, NULL, 0, NULL, 0);
	}

	if(	!interfaceInfo.showMenu &&
		(	(interfacePlayControl.enabled && interfacePlayControl.visibleFlag) ||
			interfacePlayControl.showState ||
			appControlInfo.dvbInfo.reportedSignalStatus ||
			teletext_isTeletextShowing()
		))
	{
		DFBCHECK( pgfx_font->GetHeight(pgfx_font, &fh) );
		DFBCHECK( pgfx_font->GetAscender(pgfx_font, &fa) );
		DFBCHECK( pgfx_smallfont->GetHeight(pgfx_smallfont, &sfh) );

		w = interfaceInfo.clientWidth;
		h = sfh*4+fh+interfaceInfo.paddingSize*2;

		x = interfaceInfo.clientX;
		y = interfaceInfo.screenHeight-interfaceInfo.marginSize-h;

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
		interface_drawRoundBoxColor(x, y, w, h, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA);

		rect.x = x;
		rect.y = y;
		rect.w = 0;
		rect.h = fh;
		
#ifndef STSDK
		{
		int adv, color;
		float value;
		uint16_t snr, signal;
		uint32_t ber, uncorrected_blocks;
		int stepsize;
		int step;
		int cindex;
		DFBRectangle clip;
		int colors[4][4] =	{
			{0xFF, 0x00, 0x00, 0xFF},
			{0xFF, 0xFF, 0x00, 0xFF},
			{0x00, 0xFF, 0x00, 0xFF},
			{0x00, 0xFF, 0x00, 0xFF},
		};

		dvbfe_getSignalInfo(appControlInfo.dvbInfo.adapter, &snr, &signal, &ber, &uncorrected_blocks);

		signal &= SIGNAL_MASK;

		rect.w = w/4;

		value = (float)(signal)/(float)(MAX_SIGNAL);
		if (value < 1.0f)
		{
			color = 0xb9;
			gfx_drawRectangle(DRAWING_SURFACE, color, color, color, 0xFF, rect.x, rect.y, rect.w, rect.h);
		}

		clip.x = 0;
		clip.y = 0;
		clip.w = rect.w*value;
		clip.h = rect.h;

		/* Leave at least a small part of slider */
		if (clip.w == 0)
		{
			clip.w = rect.w*4/100;
		}

		stepsize = (MAX_SIGNAL)/3;
		step = signal/stepsize;

		cindex = step;

		//interface_drawOuterBorder(DRAWING_SURFACE, 0xFF, 0xFF, 0xFF, 0xFF, rect.x, rect.y, rect.w, rect.h, 2, interfaceBorderSideAll);

		/*interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_LT_RED, INTERFACE_SCROLLBAR_COLOR_LT_GREEN, INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA, rect.x-2, rect.y-2, rect.w+4, rect.h+4, interfaceInfo.borderWidth, interfaceBorderSideBottom|interfaceBorderSideRight);
		interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_DK_RED, INTERFACE_SCROLLBAR_COLOR_DK_GREEN, INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA, rect.x-2, rect.y-2, rect.w+4, rect.h+4, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideLeft);*/

		gfx_drawRectangle(DRAWING_SURFACE, colors[cindex][0], colors[cindex][1], colors[cindex][2], colors[cindex][3], rect.x, rect.y, clip.w, clip.h);

		sprintf(buffer, "% 4d%%", signal*100/MAX_SIGNAL);

		DFBCHECK( pgfx_font->GetStringWidth (pgfx_font, buffer, -1, &adv) );

		color = 0x00; //signal*2/MAX_SIGNAL > 0 ? 0x00 : 0xFF;

		gfx_drawText(DRAWING_SURFACE, pgfx_font, color, color, color, 0xFF, rect.x+rect.w/2-adv/2, rect.y+fa, buffer, 0, 0);
		}
#endif // !STSDK


		//interface_drawOuterBorder(DRAWING_SURFACE, 0xFF, 0xFF, 0xFF, 0xFF, rect.x+rect.w+interfaceInfo.paddingSize, rect.y, w-rect.w-interfaceInfo.paddingSize*2, rect.h, 2, interfaceBorderSideAll);

		/*interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_LT_RED, INTERFACE_SCROLLBAR_COLOR_LT_GREEN, INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA, rect.x+rect.w+interfaceInfo.paddingSize*3-2, rect.y-2, w-rect.w-interfaceInfo.paddingSize*3+4, rect.h+4, interfaceInfo.borderWidth, interfaceBorderSideBottom|interfaceBorderSideRight);
		interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_DK_RED, INTERFACE_SCROLLBAR_COLOR_DK_GREEN, INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA, rect.x+rect.w+interfaceInfo.paddingSize*3-2, rect.y-2, w-rect.w-interfaceInfo.paddingSize*3+4, rect.h+4, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideLeft);*/

		switch(appControlInfo.playbackInfo.streamSource) {
		  case streamSourceDVB:
			strcpy(buffer, dvb_getServiceName(current_service()));
			break;
		  case streamSourceAnalogTV:
#ifdef ENABLE_ANALOGTV
			snprintf(buffer, sizeof(buffer), "%s", analogtv_getServiceName(appControlInfo.tvInfo.id));
#endif
			break;
		  default:
			snprintf(buffer, sizeof(buffer), "unknown");
			break;
		}
		buffer[getMaxStringLengthForFont(pgfx_font, buffer, w-rect.w-DVBPC_STATUS_ICON_SIZE-interfaceInfo.paddingSize*3)] = 0;

#ifdef ENABLE_PVR
		if (pvr_isRecordingDVB())
			interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "icon_record.png", rect.x+rect.w+interfaceInfo.paddingSize, rect.y+fh/2, DVBPC_STATUS_ICON_SIZE, DVBPC_STATUS_ICON_SIZE, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignMiddle, NULL, NULL);
#endif

		gfx_drawText(DRAWING_SURFACE, pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, rect.x+rect.w+DVBPC_STATUS_ICON_SIZE+interfaceInfo.paddingSize*2, rect.y+fa, buffer, 0, 0);
		//interface_drawTextWW(pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, rect.x+rect.w+interfaceInfo.paddingSize*3, rect.y+fh, w-rect.w-interfaceInfo.paddingSize*3, 10, buffer, ALIGN_LEFT);

		if(appControlInfo.playbackInfo.streamSource == streamSourceDVB) {
			rect.x = x;
			rect.w = w;
			rect.y += rect.h+interfaceInfo.paddingSize*2;
			rect.h = h-rect.h-interfaceInfo.paddingSize*2;

			/*interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_LT_RED, INTERFACE_SCROLLBAR_COLOR_LT_GREEN, INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA, rect.x-2, rect.y-2, w+4, rect.h+4, interfaceInfo.borderWidth, interfaceBorderSideBottom|interfaceBorderSideRight);
			interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_DK_RED, INTERFACE_SCROLLBAR_COLOR_DK_GREEN, INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA, rect.x-2, rect.y-2, w+4, rect.h+4, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideLeft);*/

			interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_LT_RED, INTERFACE_SCROLLBAR_COLOR_LT_GREEN, INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA, rect.x-2, rect.y-2, w+4, rect.h+4, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideBottom);

			offair_getServiceDescription(current_service(), buffer, NULL);

			interface_drawTextWW(pgfx_smallfont, 0xFF, 0xFF, 0xFF, 0xFF, rect.x+interfaceInfo.paddingSize, rect.y, rect.w-interfaceInfo.paddingSize, rect.h, buffer, ALIGN_LEFT);
		}

		interface_addEvent((eventActionFunction)interface_displayMenu, (void*)1, 100, 1);
	}

	interface_slideshowControlDisplay();
}

static int offair_toggleSubtitles(void)
{
	subtitle.stream = dvb_getNextSubtitleStream(current_service(), subtitle.stream);
	if (subtitle.stream == NULL && subtitle.index == 0) {
		interface_showMessageBox(_T("NO_SUBTITLES"), thumbnail_warning, 3000);
		return 1;
	}
	if (subtitle.stream)
		subtitle.index++;
	else
		subtitle.index = 0;
	offair_subtitleShow(subtitle.stream ? dvb_getStreamPid(subtitle.stream->data) : 0);
	return 0;
}

static int offair_subtitleControlHide(void *ignored)
{
	subtitle.visible = 0;
	interface_displayMenu(1);
	return 0;
}

int offair_subtitleShow(uint16_t subtitle_pid)
{
#ifdef STSDK
	elcdRpcType_t type;
	cJSON *res = NULL;
	cJSON *params = cJSON_CreateObject();
	if (!params)
		return -1;
	cJSON_AddItemToObject(params, "pid", cJSON_CreateNumber(subtitle_pid));
	dprintf("%s: %04x (%u)\n", __func__, subtitle_pid, subtitle_pid);
	int ret = st_rpcSync (elcmd_subtitle, params, &type, &res);
	cJSON_Delete(params);
	if (ret != 0 || type != elcdRpcResult) {
		eprintf("%s: failed: %s\n", __FUNCTION__, jsonGetString(res, ""));
		cJSON_Delete(res);
		return -1;
	}
	cJSON_Delete(res);
#endif
	subtitle.visible = 1;
	interface_displayMenu(1);
	interface_addEvent(offair_subtitleControlHide, NULL, 2000, 1);
	return 0;
}

static int offair_playControlProcessCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: in 0x%08X\n", __FUNCTION__, cmd);

	if(teletext_processCommand(cmd, pArg) == 0) {
		return 0;
	}

	if(cmd->source == DID_FRONTPANEL) {
		switch(cmd->command) {
			case interfaceCommandRight:
				cmd->command = interfaceCommandVolumeUp;
				interface_soundControlProcessCommand(cmd);
				return 0;
			case interfaceCommandLeft:
				cmd->command = interfaceCommandVolumeDown;
				interface_soundControlProcessCommand(cmd);
				return 0;
			case interfaceCommandDown:  cmd->command = interfaceCommandChannelDown; break;
			case interfaceCommandUp:    cmd->command = interfaceCommandChannelUp; break;
			case interfaceCommandEnter: cmd->command = interfaceCommandBack; break;
			default:;
		}
	}

	if(appControlInfo.playbackInfo.streamSource == streamSourceAnalogTV &&
	   analogtv_playControlProcessCommand(cmd, pArg) == 0) {
		return 0;
	}

	switch (cmd->command)
	{
		case interfaceCommandUp:
		case interfaceCommandDown:
			interface_menuActionShowMenu(interfaceInfo.currentMenu, (void*)&DVBTMenu);
			interface_showMenu(1, 1);
			return 0;
		case interfaceCommand0:
			if (interfaceChannelControl.length)
				return 1;
			if (appControlInfo.offairInfo.previousChannel &&
				appControlInfo.offairInfo.previousChannel != offair_getCurrentChannel()) {
				offair_setChannel(appControlInfo.offairInfo.previousChannel, SET_NUMBER(screenMain));
			}
			return 0;
#ifdef ENABLE_PVR
		case interfaceCommandRecord:
			pvr_toogleRecordingDVB();
			return 0;
#endif
#ifdef ENABLE_MULTI_VIEW
		case interfaceCommandTV:
			if ( appControlInfo.dvbInfo.channel != CHANNEL_CUSTOM &&
#ifdef ENABLE_PVR
			    !pvr_isRecordingDVB() &&
#endif
				 can_play(appControlInfo.dvbInfo.channel) && has_video(appControlInfo.dvbInfo.channel)
			   )
			{
				offair_multiviewPlay(interfaceInfo.currentMenu, CHANNEL_INFO_SET(screenMain, appControlInfo.dvbInfo.channel));
				return 0;
			}
			break;
#endif // ENABLE_MULTI_VIEW
		case interfaceCommandExit:
			if (appControlInfo.dvbInfo.showInfo) {
				appControlInfo.dvbInfo.showInfo = 0;
				offair_setInfoUpdateTimer(screenMain, 0);
				return 0;
			}
			break;
		case interfaceCommandPlay:
		case interfaceCommandStop:
		case interfaceCommandOk:
			// Disable play/stop and playcontrol buttons activation
			offair_stopVideo(screenMain, 1);
			interface_showMenu(1, 1);
			return 0;
		case interfaceCommandSubtitle:
			if (appControlInfo.dvbInfo.channel != CHANNEL_CUSTOM) {
				offair_toggleSubtitles();
				return 0;
			}
			break;
		case interfaceCommandBlue:
		case interfaceCommandEpg:
			offair_showSchedule(interfaceInfo.currentMenu, appControlInfo.dvbInfo.channel);
			return 0;
		default:;
	}
	return 1;
}

static int offair_audioChanged(void* pArg)
{
	int selected = GET_NUMBER(pArg);
	if(appControlInfo.dvbInfo.channel != CHANNEL_CUSTOM) {
		service_index_t *srvIdx = dvbChannel_getServiceIndex(appControlInfo.dvbInfo.channel);
		if(srvIdx == NULL) {
			return -1;
		}
		srvIdx->data.audio_track = selected;

		dvbChannel_save();
	}
	return 0;
}

void offair_startVideo(int which)
{
	service_index_t *srvIdx = dvbChannel_getServiceIndex(appControlInfo.dvbInfo.channel);
	EIT_service_t *service;
	if((srvIdx == NULL) || (srvIdx->service == NULL)) {
		eprintf("%s: Failed to start channel %d: offair service is NULL\n", __FUNCTION__, appControlInfo.dvbInfo.channel);
		return;
	}
	service = srvIdx->service;
	dprintf("start video:\n");
	dprintf(" name: %s\n",service->service_descriptor.service_name);
	dprintf(" media_id: %d\n",service->common.media_id);
	dprintf(" service_id %d:\n",service->common.service_id);
	dprintf(" transport_stream_id: %d\n",service->common.transport_stream_id);
	dprintf(" original_network_id: %d\n",service->original_network_id);
	dprintf(" frequency: %d\n",service->media.frequency);
	if (service->media.type == serviceMediaDVBS) {
		dprintf(" polarization: %d\n",service->media.dvb_s.polarization);
		dprintf(" symbol_rate: %d\n",service->media.dvb_s.symbol_rate);
	}
	if (service->media.type == serviceMediaDVBC) {
		dprintf(" modulation: %d\n",service->media.dvb_c.modulation);
		dprintf(" symbol_rate: %d\n",service->media.dvb_c.symbol_rate);
		dprintf(" inversion: %d\n",service->media.dvb_c.inversion);
	}
	if (service->media.type == serviceMediaDVBT) {
		dprintf(" bandwidth: %d\n",service->media.dvb_t.bandwidth);
		dprintf(" inversion: %d\n",service->media.dvb_t.inversion);
		dprintf(" generation: %d\n",service->media.dvb_t.generation);
		dprintf(" plp_id: %d\n",service->media.dvb_t.plp_id);
	}
	if(offair_findCapableTuner(service, &appControlInfo.dvbInfo.adapter) != 0) {
		eprintf("%s: Failed to find tuner matching type %d\n", __FUNCTION__, service->media.type);
		return;
	}

	DvbParam_t param;
	param.frequency = 0;
	param.mode = DvbMode_Watch;
	param.adapter = appControlInfo.dvbInfo.adapter;
	param.media = &service->media;
	param.param.liveParam.channelIndex = dvb_getServiceIndex(service);
	param.param.liveParam.audioIndex = srvIdx->data.audio_track;
	param.directory = NULL;

	if(!dvb_hasMedia(service)) {
		interface_showMessageBox(_T("DVB_SCANNING_SERVICE"), thumbnail_loading, 0);

//		offair_stopVideo(screenMain, 1);
		dvb_scanForBouquet(param.adapter, service);
		if(!dvb_hasMedia(service)) {//check once again
			eprintf("%s(): Scrambled or media-less service ignored\n", __func__);
			interface_showMessageBox(_T("ERR_NO_STREAMS_IN_CHANNEL"), thumbnail_error, 0);
			return;
		}
#if (defined STSDK)
		elcdRpcType_t type;
		cJSON *result = NULL;
		st_rpcSync(elcmd_dvbclearservices, NULL, &type, &result);
		cJSON_Delete(result);
#endif //#if (defined STSDK)

		interface_hideMessageBox();
    }

	int audio_type = dvb_getAudioType(service, srvIdx->data.audio_track);
	int video_type = dvb_getVideoType(service);

#ifdef ENABLE_DVB_DIAG
	interface_addEvent(offair_updatePSI, SET_NUMBER(which), 1000, 1);
#endif

	interface_addEvent(offair_updateEPG, SET_NUMBER(which), 1000 * 5, 1);
#ifdef ENABLE_STATS
	time(&statsInfo.endTime);
	interface_addEvent(offair_updateStatsEvent, SET_NUMBER(which), STATS_UPDATE_INTERVAL, 1);
#endif

	offair_checkParentControl(which, &param, audio_type, video_type);
}

static int offair_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if(parentControl_checkPass(value) == 0) {
		offair_confDvbStart_t *start = (offair_confDvbStart_t *)pArg;
		offair_startDvbVideo(start->which, &start->pParam, start->audio_type, start->video_type);
		interface_showMenu(0, 1);
	} else {
		interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
		if((appControlInfo.offairInfo.previousChannel) && (appControlInfo.offairInfo.previousChannel != offair_getCurrentChannel())) {
			offair_setChannel(appControlInfo.offairInfo.previousChannel, SET_NUMBER(screenMain));
		}
	}

	return 0;
}

static void offair_checkParentControl(int which, DvbParam_t *pParam, int audio_type, int video_type)
{
	interfaceMenu_t* pMenu = _M &DVBTMenu;
	service_index_t *srvIdx = dvbChannel_getServiceIndex(appControlInfo.dvbInfo.channel);

	if(srvIdx->data.parent_control == 1) {
		const char *mask = "\\d{6}";
		offair_confDvbStart_t start;
		start.which = which;
		start.pParam = *pParam;
		start.audio_type = audio_type;
		start.video_type = video_type;
		interface_getText(pMenu, _T("ENTER_PASSWORD"), mask, offair_checkParentControlPass, NULL, inputModeDirect, &start);
	} else {
		offair_startDvbVideo(which, pParam, audio_type, video_type);
	}
}

static void offair_startDvbVideo(int which, DvbParam_t *pParam, int audio_type, int video_type)
{
	char filename[256];
	char qualifier[64];

	gfx_stopVideoProviders(which);

	dprintf("%s: dvbfe_isLinuxAdapter(%d)=%d\n", __FUNCTION__, pParam->adapter, dvbfe_isLinuxAdapter(pParam->adapter));

	if(dvbfe_isLinuxAdapter(pParam->adapter)) {
#ifdef STBTI
		sprintf(filename, "ln -s /dev/dvb/adapter%d/dvr0 %s", pParam->adapter, OFFAIR_MULTIVIEW_FILENAME);
		system(filename);
		strcpy(filename, OFFAIR_MULTIVIEW_FILENAME);
#else
#ifdef ENABLE_MULTI_VIEW
		if(pParam->mode == DvbMode_Multi) {
			sprintf(filename, OFFAIR_MULTIVIEW_INFOFILE);
		} else
#endif // ENABLE_MULTI_VIEW
		{
			sprintf(filename, "/dev/dvb/adapter%d/demux0", pParam->adapter);
		}
#endif // !STBTI
#ifdef STB6x8x
		sprintf(qualifier, "%s%s%s%s%s",
			"", // (which==screenPip) ? ":SD:NoSpdif:I2S1" : "",
			audio_type == streamTypeAudioAC3 ? ":AC3" : "",
			video_type == streamTypeVideoH264 ? ":H264" : ( video_type == streamTypeVideoMPEG2 ? ":MPEG2" : ""),
			audio_type == streamTypeAudioAAC ? ":AAC" : AUDIO_MPEG,
			(appControlInfo.soundInfo.rcaOutput==1) ? "" : ":I2S0");

		dprintf("%s: Qualifier: %s\n", __FUNCTION__, qualifier);
#endif // STB6x8x

		dprintf("%s: dvb_startDVB\n", __FUNCTION__);
		dvb_startDVB(pParam);
	} else {
		if(dvbfe_setParam(pParam->adapter, 0, pParam->media, NULL) != 0) {
// 			eprintf("%s(): adapter=%d, failed to tine\n", __func__, pParam->adapter);
// 			return;
		}
	}
#ifdef STSDK
	// Always use 0 index for Linux tuners
	sprintf(filename, "dvb://%d@%d", current_service()->common.service_id, pParam->adapter);
	qualifier[0] = 0;
#endif // STSDK

	if(gfx_startVideoProvider(filename, which, 0, qualifier) != 0) {
		eprintf("offair: Failed to start video provider '%s'\n", filename);
		interface_showMessageBox(_T("ERR_VIDEO_PROVIDER"), thumbnail_error, 0);
		dvb_stopDVB(pParam->adapter, 1);
		return;
	}
	offair_updateChannelStatus();
#ifdef STSDK
	service_index_t *srvIdx = dvbChannel_getServiceIndex(appControlInfo.dvbInfo.channel);
	if(srvIdx && srvIdx->data.audio_track) {
		eprintf("%s: set audio %u\n", __func__, srvIdx->data.audio_track);
		gfx_setVideoProviderAudioStream(which, srvIdx->data.audio_track);
	}
#endif

	dprintf("%s: dvb_hasVideo == %d\n", __FUNCTION__,video_type);
	if(video_type != 0) {
		media_slideshowStop(1);
	} else if (appControlInfo.slideshowInfo.state == slideshowDisabled) {
		interface_setBackground(0,0,0,0xFF, INTERFACE_WALLPAPER_IMAGE);
	}

	appControlInfo.dvbInfo.active = 1;
	appControlInfo.dvbInfo.scanPSI = 1;
	appControlInfo.dvbInfo.lastSignalStatus = signalStatusNoStatus;
	appControlInfo.dvbInfo.savedSignalStatus = signalStatusNoStatus;
	appControlInfo.dvbInfo.reportedSignalStatus = 0;

	if(dvbfe_isLinuxAdapter(pParam->adapter)) {
		offair_setStateCheckTimer(which, 1);
		offair_setInfoUpdateTimer(which, 1);
	}

#ifdef ENABLE_MULTI_VIEW
	if(pParam->mode == DvbMode_Multi) {
		dprintf("%s: multiview mode\n", __FUNCTION__);
		interface_playControlSetup(NULL, appControlInfo.multiviewInfo.pArg[0], 0, NULL, thumbnail_channels);
		interface_playControlSetDisplayFunction(offair_displayMultiviewControl);
		interface_playControlSetProcessCommand(offair_multi_callback);
		interface_playControlSetChannelCallbacks(offair_multiviewNext, offair_setChannel);
		interface_playControlRefresh(0);
		interface_showMenu(0, 1);
	}
#endif

	interface_playControlSelect(interfacePlayControlStop);
	interface_playControlSelect(interfacePlayControlPlay);
	interface_displayMenu(1);

	teletext_start(pParam);

	dprintf("%s: done\n", __FUNCTION__);
}

#ifndef HIDE_EXTRA_FUNCTIONS
static int offair_startStopDVB(interfaceMenu_t *pMenu, void* pArg)
{
	if(appControlInfo.dvbInfo.active) {
		offair_stopVideo(GET_NUMBER(pArg), 1);
	} else {
		offair_startVideo(GET_NUMBER(pArg));
	}
	return 0;
}

static int offair_debugToggle(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.offairInfo.tunerDebug = appControlInfo.offairInfo.tunerDebug ? 0 : 1;
	offair_fillDVBTMenu();
	interface_displayMenu(1);
	return 0;
}
#endif

static void offair_getServiceDescription(EIT_service_t *service, char *desc, char *mode)
{
	list_element_t *event_element;
	EIT_event_t *event;
	time_t start_time;
	char *str;

	if( service != NULL )
	{
		if (mode != NULL)
		{
			sprintf(desc, "%s: %s",mode, dvb_getServiceName(service));
		} else
		{
			desc[0] = 0;
		}

		offair_sortEvents(&service->present_following);
		event_element = service->present_following;
		while ( event_element != NULL )
		{
			event = (EIT_event_t*)event_element->data;
			switch(event->running_status)
			{
			case 2:
				//str = _T("STARTS_IN_FEW_SECONDS"); break;
			case 1:
				str = _T("NEXT");
				break;
			case 3:
				//str = _T("PAUSING"); break;
			case 4:
				str = _T("PLAYING");
				break;
			default: str = "";
			}
			sprintf( &desc[strlen(desc)], "%s%s ", desc[0] == 0 ? "" : "\n", str);
			offair_getLocalEventTime( event, NULL, &start_time );
			start_time += offair_getEventDuration( event );
			strftime(&desc[strlen(desc)], 11, "(%H:%M)", localtime( &start_time ));
			sprintf( &desc[strlen(desc)], " %s", event->description.event_name );
			/*if (event->description.text[0] != 0)
			{
				sprintf( &desc[strlen(desc)], ". %s", event->description.text );
			}*/

			event_element = event_element->next;
		}
	} else
	{
		strcpy( desc, _T("RECORDING"));
	}
}

#ifdef ENABLE_PVR
int offair_startPvrVideo( int which )
{
	int ret = 0;
#ifdef STBPNX
	char desc[BUFFER_SIZE];
	int buttons;

	//sprintf(filename, "%s/info.pvr", appControlInfo.pvrInfo.playbackInfo[which].directory);
	if (helperFileExists(STBPVR_PIPE_FILE ".dummy" ))
	{
		gfx_stopVideoProviders(which);

		media_slideshowStop(1);

		dprintf("%s: start provider %s\n", __FUNCTION__, STBPVR_PIPE_FILE ".dummy");

		ret = gfx_startVideoProvider(STBPVR_PIPE_FILE ".dummy", which, 1, NULL);

		dprintf("%s: video provider started with return code %d\n", __FUNCTION__, ret);

		if ( ret != 0 )
		{
			interface_showMessageBox(_T("ERR_VIDEO_PROVIDER"), thumbnail_error, 0);
			eprintf("offair: Failed to start video provider %s\n", STBPVR_PIPE_FILE ".dummy");
		} else
		{
			buttons = interfacePlayControlStop|interfacePlayControlPlay|interfacePlayControlPrevious|interfacePlayControlNext;
			buttons |= appControlInfo.playbackInfo.playlistMode != playlistModeFavorites ? interfacePlayControlAddToPlaylist :  interfacePlayControlMode;

			offair_getServiceDescription(dvb_getService(appControlInfo.pvrInfo.dvb.channel),desc,_T("RECORDING"));

			appControlInfo.dvbInfo.active = 1;
			interface_playControlSetup(offair_play_callback, SET_NUMBER(which), buttons, desc, thumbnail_recording);
			interface_playControlSetDisplayFunction(offair_displayPlayControl);
			interface_playControlSetProcessCommand(offair_playControlProcessCommand);
			interface_playControlSetChannelCallbacks(offair_startNextChannel, offair_setChannel);
			interface_playControlSelect(interfacePlayControlStop);
			interface_playControlSelect(interfacePlayControlPlay);
			interface_playControlRefresh(0);
			interface_showMenu(0, 1);
		}
	} else
	{
		ret = -1;
		eprintf("offair: %s do not exist\n", STBPVR_PIPE_FILE ".dummy");
	}
#endif // STBPNX
	return ret;
}
#endif // ENABLE_PVR

int  offair_setPreviousChannel(int previousChannel)
{
	if (appControlInfo.offairInfo.previousChannel != previousChannel && previousChannel) {
		appControlInfo.offairInfo.previousChannel  = previousChannel;
		return saveAppSettings();
	}
	return 0;
}

int offair_getCurrentChannel(void)
{
#ifdef ENABLE_ANALOGTV
	if (appControlInfo.tvInfo.active)
		return dvbChannel_getCount() + appControlInfo.tvInfo.id;
#endif
	return appControlInfo.dvbInfo.channel;
}

int offair_channelChange(interfaceMenu_t *pMenu, void* pArg)
{
	char desc[BUFFER_SIZE];
	int32_t		channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
	int32_t		buttons;
	int32_t		previousChannel;

	EIT_service_t *service = dvbChannel_getService(channelNumber);

	dprintf("%s: channelNumber = %d\n", __FUNCTION__, channelNumber);

	if((service == NULL) || (dvb_getServiceID(service) == -1)) {
		eprintf("%s: Unknown service for channel %d\n", __FUNCTION__, channelNumber);
		return -1;
	}

#if (defined ENABLE_PVR) && (defined STBPNX)
	if(pvr_isRecordingDVB()) {
		if (offair_getIndex(appControlInfo.pvrInfo.dvb.channel) == channelNumber)
			pvr_startPlaybackDVB(screenMain);
		else
			pvr_showStopPvr(pMenu, (void*)channelNumber);
		return 0;
	}
#endif

	previousChannel = offair_getCurrentChannel();
	appControlInfo.playbackInfo.playlistMode = playlistModeNone;
	appControlInfo.playbackInfo.streamSource = streamSourceDVB;
	appControlInfo.mediaInfo.bHttp = 0;
	appControlInfo.dvbInfo.channel = channelNumber;
	appControlInfo.dvbInfo.scrambled = dvb_getScrambled(service);

	buttons  = interfacePlayControlStop|interfacePlayControlPlay|interfacePlayControlPrevious|interfacePlayControlNext;
	buttons |= appControlInfo.playbackInfo.playlistMode != playlistModeFavorites ?
	           interfacePlayControlAddToPlaylist : interfacePlayControlMode;

	offair_getServiceDescription(service, desc, _T("DVB_CHANNELS"));
	appControlInfo.playbackInfo.channel = channelNumber;

	interface_playControlSetInputFocus(inputFocusPlayControl);
	interface_playControlSetup(offair_play_callback, NULL, buttons, desc, service_thumbnail(service));
	interface_playControlSetDisplayFunction(offair_displayPlayControl);
	interface_playControlSetProcessCommand(offair_playControlProcessCommand);
	interface_playControlSetChannelCallbacks(offair_startNextChannel, offair_setChannel);
	interface_playControlSetAudioCallback(offair_audioChanged);
	interface_channelNumberShow(channelNumber + 1);

	if(appControlInfo.dvbInfo.active) {
		//interface_playControlSelect(interfacePlayControlStop);
		// force showState to NOT be triggered
		interfacePlayControl.activeButton = interfacePlayControlStop;
		offair_stopVideo(screenMain, 0);
	}
	offair_startVideo(screenMain);
	saveAppSettings();

	if(appControlInfo.dvbInfo.active != 0) {
		interface_showMenu(0, 1);
		offair_setPreviousChannel(previousChannel);
	}

	//interface_menuActionShowMenu(pMenu, (void*)&DVBTMenu);

	return 0;
}

char *offair_getChannelNumberPrefix(uint32_t id)
{
	int32_t serviceCount;
	static char numberStr[8];
	const char *format;
	const char *formats[] = {
		"%d",
		"%02d",
		"%03d",
	};

	serviceCount = dvbChannel_getCount() + analogtv_getChannelCount(0);
	if(serviceCount < 10) {
		format = formats[0];
	} else if(serviceCount < 100) {
		format = formats[1];
	} else {
		format = formats[2];
	}
	snprintf(numberStr, sizeof(numberStr), format, id + 1);

	return numberStr;
}

static void offair_addDVBChannelsToMenu()
{
	interfaceMenu_t *channelMenu = _M &DVBTMenu;
	struct list_head *pos;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	int32_t i = 0;

	interface_addMenuEntryDisabled(channelMenu, "DVB", 0);

// 	for(int i = 0; i < dvbChannel_getCount(); ++i) {
// 		EIT_service_t *service = dvbChannel_getService(i);
	list_for_each(pos, dvbChannel_getSortList()) {
		service_index_t *srv = list_entry(pos, service_index_t, orderNone);
		EIT_service_t *service = srv->service;
		interfaceMenuEntry_t *entry;
		char *serviceName;
		int32_t isRadio = 0;

		if(!srv->data.visible) {
			continue;
		}
		if( service_isRadio(service) ||
			(dvb_hasMediaType(service, mediaTypeAudio) && !dvb_hasMediaType(service, mediaTypeVideo)))
		{
			isRadio = 1;
		}
		serviceName = srv->data.channelsName;// dvb_getServiceName(service);

		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), serviceName);
		interface_addMenuEntry(channelMenu, channelEntry, offair_channelChange, CHANNEL_INFO_SET(screenMain, i),
								dvb_getScrambled(service) ? thumbnail_billed : (isRadio ? thumbnail_radio : thumbnail_channels));

		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			interface_setMenuEntryLabel(entry, "DVB");
		}

		if(appControlInfo.dvbInfo.channel == i) {
			interface_setSelectedItem(channelMenu, interface_getMenuEntryCount(channelMenu) - 1);
		}
		i++;
	}
	if(dvbChannel_getCount() == 0) {
		strcpy(channelEntry, _T("NO_CHANNELS"));
		interface_addMenuEntryDisabled(channelMenu, channelEntry, thumbnail_info);
	}
}

int offair_setChannel(int channel, void* pArg)
{
	int which = GET_NUMBER(pArg);
	if(channel <= 0) {
		return 1;
	}
// 	channel--;

	if((channel < dvbChannel_getCount()) && (dvbChannel_getService(channel) != NULL)) {
		printf("%s: offair_channelChange...\n", __FUNCTION__);
		offair_channelChange(interfaceInfo.currentMenu, CHANNEL_INFO_SET(which, channel));
		return 0;
	} else if(channel >= dvbChannel_getCount()) {
		channel = channel - dvbChannel_getCount() - 1;
		analogtv_activateChannel(interfaceInfo.currentMenu, CHANNEL_INFO_SET(which, channel));
		return 0;
	}
	return 1;
}

//static int offair_startNextChannel(int direction, void* pArg)
int offair_startNextChannel(int direction, void* pArg)
{
	int i;
	int which = GET_NUMBER(pArg);

	if (appControlInfo.playbackInfo.playlistMode == playlistModeFavorites)
		return playlist_startNextChannel(direction,(void*)-1);

	dprintf("%s: %d, screen%s\n", __FUNCTION__, direction, which == screenMain ? "Main" : "Pip" );
	direction = direction == 0 ? 1 : -1;
	for(
		i = (appControlInfo.dvbInfo.channel + dvbChannel_getCount() + direction ) % dvbChannel_getCount();
        i != appControlInfo.dvbInfo.channel && (!can_play(i));
		i = (i + direction + dvbChannel_getCount()) % dvbChannel_getCount() );

	dprintf("%s: i = %d, ch = %d, total = %d\n", __FUNCTION__, i, appControlInfo.dvbInfo.channel, dvbChannel_getCount());

	if (i != appControlInfo.dvbInfo.channel)
		offair_channelChange(interfaceInfo.currentMenu, CHANNEL_INFO_SET(which, i));
	return 0;
}

static int offair_confirmAutoScan(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		interface_hideMessageBox();
		offair_serviceScan(pMenu,pArg);
		return 0;
	}

	return 1;
}

static int32_t offair_dvbChannelsChangeCallback(void *pArg)
{
	(void)pArg;
	needRefill = 1;
	return 0;
}

int offair_enterDVBTMenu(interfaceMenu_t *pMenu, void* pArg)
{
	if(needRefill) {
		offair_fillDVBTMenu();
		needRefill = 0;
	}

	if((dvbChannel_getCount() == 0) && (analogtv_getChannelCount(0) == 0)) {
		output_showDVBMenu(pMenu, NULL);
		interface_showConfirmationBox( _T("DVB_NO_CHANNELS"), thumbnail_dvb, offair_confirmAutoScan, NULL);
		return 1;
	}

	/* Auto play */
	if(appControlInfo.playbackInfo.bAutoPlay &&
		(gfx_videoProviderIsActive(screenMain) == 0) &&
		(appControlInfo.slideshowInfo.state == slideshowDisabled) &&
		(dvbChannel_getCount() > 0))
	{
		dprintf("%s: Auto play dvb channel = %d\n", __FUNCTION__,appControlInfo.dvbInfo.channel);
		if((appControlInfo.dvbInfo.channel < 0) || (appControlInfo.dvbInfo.channel > dvbChannel_getCount())) {
			appControlInfo.dvbInfo.channel = 0;
		}
		offair_channelChange(interfaceInfo.currentMenu, CHANNEL_INFO_SET(screenMain, appControlInfo.dvbInfo.channel));
	}

	return 0;
}

int offair_initEPGRecordMenu(interfaceMenu_t *pMenu, void *pArg)
{
	interfaceEpgMenu_t *pEpg = (interfaceEpgMenu_t *)pMenu;
	list_element_t *event_element;

	service_index_t *srvIdx;
	EIT_event_t *event;
	time_t event_start, event_end;
	int i, events_found;

	if(GET_NUMBER(pArg) < 0) {// double call fix
		return 0;
	}

	pEpg->currentService = offair_getCurrentChannel();//GET_NUMBER(pArg);
	pEpg->serviceOffset = 0;

	if(!dvbChannel_hasSchedule(pEpg->serviceOffset)) {
		for(pEpg->serviceOffset = 0; pEpg->serviceOffset < dvbChannel_getCount(); pEpg->serviceOffset++) {
			if(dvbChannel_hasSchedule(pEpg->serviceOffset)) {
				break;
			}
		}
		if((pEpg->serviceOffset == dvbChannel_getCount()) || !dvbChannel_hasSchedule(pEpg->serviceOffset)) {
			interface_showMessageBox(_T("EPG_UNAVAILABLE"), thumbnail_error, 3000);
			return 1;
		}
	}
	if(!dvbChannel_hasSchedule(pEpg->currentService)) {
		pEpg->currentService = pEpg->serviceOffset;
		for(pEpg->serviceOffset++; pEpg->serviceOffset < dvbChannel_getCount(); pEpg->serviceOffset++) {
			if(dvbChannel_hasSchedule(pEpg->serviceOffset)) {
				break;
			}
		}
	}

	dprintf("%s: Found valid service #%d %s\n", __FUNCTION__, pEpg->currentService, dvb_getServiceName(dvbChannel_getService(pEpg->currentService)));

	time( &pEpg->curOffset );
	pEpg->minOffset = pEpg->maxOffset = pEpg->curOffset = 3600 * (pEpg->curOffset / 3600); // round to hours
	events_found = 0;
	for(i = 0; i < dvbChannel_getCount(); i++) {
		srvIdx = dvbChannel_getServiceIndex(i);
		if(srvIdx == NULL) {
			continue;
		}
		srvIdx->first_event = NULL;
		if(srvIdx->service != NULL && srvIdx->service->schedule != NULL && dvb_hasMedia(srvIdx->service) != 0) {
			for(event_element = srvIdx->service->schedule; event_element != NULL; event_element = event_element->next) {
				event = (EIT_event_t *)event_element->data;
				if(offair_getLocalEventTime(event, NULL, &event_start) == 0) {
					event_end = event_start + offair_getEventDuration(event);
					if(srvIdx->first_event == NULL && event_end > pEpg->minOffset) {
						srvIdx->first_event = event_element;
						events_found = 1;
					}
					/* Skip older events
					if(event_start < pEpg->minOffset) {
						pEpg->minOffset = 3600 * (event_start / 3600);
					}*/
					event_end -= 3600 * pEpg->displayingHours;
					if(event_end > pEpg->maxOffset) {
						pEpg->maxOffset = 3600 * (event_end / 3600 + 1);
					}
				}
			}
		}
	}
	if(events_found == 0) {
		interface_showMessageBox(_T("EPG_UNAVAILABLE"), thumbnail_error, 3000);
		return -1;
	}
	if (pEpg->maxOffset - pEpg->minOffset < ERM_DISPLAYING_HOURS * 3600) {
		pEpg->maxOffset = pEpg->minOffset + ERM_DISPLAYING_HOURS * 3600;
	}
	srvIdx = dvbChannel_getServiceIndex(pEpg->currentService);
	if(srvIdx->first_event) {
		pEpg->highlightedEvent = srvIdx->first_event;
	} else {
		struct list_head *pos;

		list_for_each(pos, dvbChannel_getSortList()) {
			service_index_t *srvIdx2 = list_entry(pos, service_index_t, orderNone);

			if(!srvIdx2->data.visible) {
				continue;
			}
			if(srvIdx2 && srvIdx2->first_event) {
				pEpg->highlightedEvent = srvIdx2->first_event;
				break;
			}
		}
	}

	pEpg->highlightedService = pEpg->currentService;

	pEpg->baseMenu.selectedItem = MENU_ITEM_EVENT;
	pEpg->displayingHours = ERM_DISPLAYING_HOURS;
	pEpg->timelineX = interfaceInfo.clientX + 2*interfaceInfo.paddingSize + ERM_CHANNEL_NAME_LENGTH;
	pEpg->timelineWidth = interfaceInfo.clientWidth - 3*interfaceInfo.paddingSize - ERM_CHANNEL_NAME_LENGTH;
	pEpg->pps = (float)pEpg->timelineWidth / (pEpg->displayingHours * 3600);

	pEpg->baseMenu.pArg = (void*)-1; // double call fix

	return 0;
}

#define GLAW_EFFECT \
	gfx_drawRectangle(DRAWING_SURFACE, 255, 255, 255, 0x20, x, y+6, w, 2); \
	gfx_drawRectangle(DRAWING_SURFACE, 255, 255, 255, 0x40, x, y+4, w, 2); \
	gfx_drawRectangle(DRAWING_SURFACE, 255, 255, 255, 0x60, x, y+2, w, 2); \
	gfx_drawRectangle(DRAWING_SURFACE, 255, 255, 255, 0x80, x, y, w, 2);

static void offair_EPGRecordMenuDisplay(interfaceMenu_t *pMenu)
{
	interfaceEpgMenu_t *pEpg = (interfaceEpgMenu_t *)pMenu;
	int fh, x, y, w, l, displayedChannels, i, j, timelineEnd, r,g,b,a;
	int tr, tg, tb, ta;
	interfaceColor_t *color;
	DFBRectangle rect;
	char buf[MAX_TEXT];
	char *str;
	list_element_t *event_element;
	EIT_event_t *event;
	time_t event_tt, event_len, end_tt;
	struct tm event_tm, *t;
	interfaceColor_t sel_color = { ERM_HIGHLIGHTED_CELL_RED, ERM_HIGHLIGHTED_CELL_GREEN, ERM_HIGHLIGHTED_CELL_BLUE, ERM_HIGHLIGHTED_CELL_ALPHA };
#if (defined ENABLE_PVR) && (defined STBPNX)
	pvrJob_t *job;
	int job_channel;
#endif
	service_index_t *srvIdx = dvbChannel_getServiceIndex(pEpg->currentService);
	tr = INTERFACE_BOOKMARK_RED;
	tg = INTERFACE_BOOKMARK_GREEN;
	tb = INTERFACE_BOOKMARK_BLUE;
	ta = INTERFACE_BOOKMARK_ALPHA;

	if(srvIdx == NULL) {
		return;
	}
	interface_displayMenuHeader();

	//dprintf("%s: menu.sel=%d ri.cur=%d ri.hi=%d ri.he=%p\n", __FUNCTION__, pEpg->baseMenu.selectedItem, pEpg->currentService, pEpg->highlightedService, pEpg->highlightedEvent);
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
	DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
	/* Menu background */
	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, interfaceInfo.clientX, interfaceInfo.clientY+INTERFACE_ROUND_CORNER_RADIUS, interfaceInfo.clientWidth, interfaceInfo.clientHeight-2*INTERFACE_ROUND_CORNER_RADIUS);
	// top left corner
	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, interfaceInfo.clientX+INTERFACE_ROUND_CORNER_RADIUS, interfaceInfo.clientY, interfaceInfo.clientWidth-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", interfaceInfo.clientX, interfaceInfo.clientY, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);
	// bottom left corner
	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, interfaceInfo.clientX+INTERFACE_ROUND_CORNER_RADIUS, interfaceInfo.clientY+interfaceInfo.clientHeight-INTERFACE_ROUND_CORNER_RADIUS, interfaceInfo.clientWidth-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", interfaceInfo.clientX, interfaceInfo.clientY+interfaceInfo.clientHeight-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	/* Show menu logo if needed */
	if(interfaceInfo.currentMenu->logo > 0 && interfaceInfo.currentMenu->logoX > 0) {
		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[interfaceInfo.currentMenu->logo],
			interfaceInfo.currentMenu->logoX, interfaceInfo.currentMenu->logoY, interfaceInfo.currentMenu->logoWidth, interfaceInfo.currentMenu->logoHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0);
	}

	interface_displayClock( 0 /* not detached */ );

	pgfx_font->GetHeight(pgfx_font, &fh);

	if(srvIdx->service == NULL || srvIdx->first_event == NULL) {
		eprintf("%s: Can't display '%': service %d event %d\n", __FUNCTION__, pEpg->baseMenu.name, srvIdx->service != NULL, srvIdx->first_event != NULL);
		return;
	}
	/* Timeline */
	x = pEpg->timelineX;
	w = pEpg->timelineWidth;
	y = interfaceInfo.clientY + interfaceInfo.paddingSize;
	end_tt = pEpg->curOffset + 3600 * pEpg->displayingHours;
	timelineEnd =  pEpg->timelineX + pEpg->timelineWidth - interfaceInfo.paddingSize;
	if(pEpg->baseMenu.selectedItem == MENU_ITEM_TIMELINE) {
		r = ERM_HIGHLIGHTED_CELL_RED;//interface_colors[interfaceInfo.highlightColor].R;
		g = ERM_HIGHLIGHTED_CELL_GREEN;//interface_colors[interfaceInfo.highlightColor].G;
		b = ERM_HIGHLIGHTED_CELL_BLUE;//interface_colors[interfaceInfo.highlightColor].B;
		a = ERM_HIGHLIGHTED_CELL_ALPHA;//interface_colors[interfaceInfo.highlightColor].A;*/
	} else {
		r = ERM_TIMELINE_RED;
		g = ERM_TIMELINE_GREEN;
		b = ERM_TIMELINE_BLUE;
		a = ERM_TIMELINE_ALPHA;
	}
	gfx_drawRectangle(DRAWING_SURFACE, r, g, b, a, x, y, w, fh);
	GLAW_EFFECT;
	/* Timeline stamps */
	event_tt = pEpg->curOffset;
	strftime( buf, 25, _T("DATESTAMP"), localtime(&event_tt));
	gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, interfaceInfo.clientX + interfaceInfo.paddingSize, y+fh - interfaceInfo.paddingSize, buf, 0, 0);
	for(i = 0; i < pEpg->displayingHours; i++) {
		strftime( buf, 10, "%H:%M", localtime(&event_tt));
		gfx_drawRectangle(DRAWING_SURFACE, ERM_TIMESTAMP_RED, ERM_TIMESTAMP_GREEN, ERM_TIMESTAMP_BLUE, ERM_TIMESTAMP_ALPHA, x, y, ERM_TIMESTAMP_WIDTH, (1+ERM_VISIBLE_CHANNELS)*interfaceInfo.paddingSize + (2+ERM_VISIBLE_CHANNELS)*fh);
		gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x+ERM_TIMESTAMP_WIDTH, y+fh - interfaceInfo.paddingSize, buf, 0, 0);
		x += (int)(pEpg->pps * 3600);
		event_tt += 3600;
	}
	strftime( buf, 10, "%H:%M", localtime(&event_tt));
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );
	x = timelineEnd - rect.w - ERM_TIMESTAMP_WIDTH;
	gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+fh - interfaceInfo.paddingSize, buf, 0, 0);
	gfx_drawRectangle(DRAWING_SURFACE, ERM_TIMESTAMP_RED, ERM_TIMESTAMP_GREEN, ERM_TIMESTAMP_BLUE, ERM_TIMESTAMP_ALPHA, interfaceInfo.clientX + interfaceInfo.clientWidth - interfaceInfo.paddingSize -  ERM_TIMESTAMP_WIDTH, y, ERM_TIMESTAMP_WIDTH, (1+ERM_VISIBLE_CHANNELS)*interfaceInfo.paddingSize + (2+ERM_VISIBLE_CHANNELS)*fh);

	/* Current service (no vertical scroll) */
	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", dvb_getServiceName(srvIdx->service));
	buf[MENU_ENTRY_INFO_LENGTH-1] = 0;
	l = getMaxStringLength(buf, ERM_CHANNEL_NAME_LENGTH);
	buf[l] = 0;
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );

	x = interfaceInfo.clientX + interfaceInfo.paddingSize;
	y += fh + interfaceInfo.paddingSize;

	gfx_drawRectangle(DRAWING_SURFACE, ERM_CURRENT_TITLE_RED, ERM_CURRENT_TITLE_GREEN, ERM_CURRENT_TITLE_BLUE, ERM_CURRENT_TITLE_ALPHA, x, y, ERM_CHANNEL_NAME_LENGTH, fh);
	if(pEpg->baseMenu.selectedItem == MENU_ITEM_EVENT && pEpg->highlightedService == pEpg->currentService) {
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
		g = 3;
		b = x + ERM_CHANNEL_NAME_LENGTH - g;
		a = ERM_HIGHLIGHTED_CELL_ALPHA;
		for(r = 0; r < 8; r++) {
			gfx_drawRectangle(DRAWING_SURFACE, ERM_HIGHLIGHTED_CELL_RED, ERM_HIGHLIGHTED_CELL_GREEN, ERM_HIGHLIGHTED_CELL_BLUE, a, b, y, g, fh);
			a -= 0x10;
			b -= g;
		}

		//gfx_drawRectangle(DRAWING_SURFACE, ERM_HIGHLIGHTED_CELL_RED, ERM_HIGHLIGHTED_CELL_GREEN, ERM_HIGHLIGHTED_CELL_BLUE, ERM_HIGHLIGHTED_CELL_ALPHA, x + ERM_CHANNEL_NAME_LENGTH - 2*interfaceInfo.paddingSize, y, 2*interfaceInfo.paddingSize, fh);

	}

	gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, buf, 0, 0);
	/* Current service events */
	x += interfaceInfo.paddingSize + ERM_CHANNEL_NAME_LENGTH;
	event_element = srvIdx->first_event;
	while(event_element != NULL) {
		event = (EIT_event_t*)event_element->data;

		if(offair_getLocalEventTime(event, &event_tm, &event_tt) == 0) {
			event_len = offair_getEventDuration(event);
			if(event_tt + event_len > pEpg->curOffset && event_tt < end_tt) {
				if(event_tt >= pEpg->curOffset) {
					x = pEpg->timelineX + (int)(pEpg->pps * (event_tt - pEpg->curOffset));
				}
				w = event_tt >= pEpg->curOffset
					? (int)(pEpg->pps * offair_getEventDuration(event)) - ERM_TIMESTAMP_WIDTH
					: (int)(pEpg->pps * (event_tt + event_len - pEpg->curOffset)) - ERM_TIMESTAMP_WIDTH;
				if(x + w > timelineEnd) {
					w = timelineEnd - x;
				}
				DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
				gfx_drawRectangle(DRAWING_SURFACE, ERM_CELL_START_RED, ERM_CELL_START_GREEN, ERM_CELL_START_BLUE, ERM_CELL_START_ALPHA, x, y, ERM_TIMESTAMP_WIDTH, fh);
				x += ERM_TIMESTAMP_WIDTH;
				color = pEpg->baseMenu.selectedItem == MENU_ITEM_EVENT && event_element == pEpg->highlightedEvent ? &sel_color : &genre_colors[( event->content.content >> 4) & 0x0F];
				gfx_drawRectangle(DRAWING_SURFACE, color->R, color->G, color->B, color->A, x, y, w, fh);
				GLAW_EFFECT;

				snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", event->description.event_name);
				buf[MENU_ENTRY_INFO_LENGTH-1] = 0;
				l = getMaxStringLength(buf, w-ERM_TIMESTAMP_WIDTH);
				buf[l] = 0;
				gfx_drawText(DRAWING_SURFACE, pgfx_font, tr, tg, tb, ta, x, y+fh - interfaceInfo.paddingSize, buf, 0, 0);
			}
		}
		event_element = event_element->next;
	}

	/* Other services (vertically scrollable) */
	displayedChannels = 0;
	for(i = pEpg->serviceOffset; displayedChannels < ERM_VISIBLE_CHANNELS && i < dvbChannel_getCount(); i++) {
		srvIdx = dvbChannel_getServiceIndex(i);
		if(srvIdx == NULL) {
			continue;
		}

		if(i != pEpg->currentService && srvIdx->first_event != NULL && dvb_hasMedia(srvIdx->service)) {
			snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", dvb_getServiceName(srvIdx->service));
			buf[MENU_ENTRY_INFO_LENGTH-1] = 0;
			l = getMaxStringLength(buf, ERM_CHANNEL_NAME_LENGTH);
			buf[l] = 0;
			DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );

			x = interfaceInfo.clientX + interfaceInfo.paddingSize;
			y += rect.h + interfaceInfo.paddingSize;
			gfx_drawRectangle(DRAWING_SURFACE, ERM_TITLE_RED, ERM_TITLE_GREEN, ERM_TITLE_BLUE, ERM_TITLE_ALPHA, x, y, ERM_CHANNEL_NAME_LENGTH, rect.h);
			if(pEpg->baseMenu.selectedItem == MENU_ITEM_EVENT && pEpg->highlightedService == i) {
				DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
				g = 3;
				b = x + ERM_CHANNEL_NAME_LENGTH - g;
				a = ERM_HIGHLIGHTED_CELL_ALPHA;
				for(r = 0; r < 8; r++) {
					gfx_drawRectangle(DRAWING_SURFACE, ERM_HIGHLIGHTED_CELL_RED, ERM_HIGHLIGHTED_CELL_GREEN, ERM_HIGHLIGHTED_CELL_BLUE, a, b, y, g, fh);
					a -= 0x10;
					b -= g;
				}
				//gfx_drawRectangle(DRAWING_SURFACE, ERM_HIGHLIGHTED_CELL_RED, ERM_HIGHLIGHTED_CELL_GREEN, ERM_HIGHLIGHTED_CELL_BLUE, ERM_HIGHLIGHTED_CELL_ALPHA, x + ERM_CHANNEL_NAME_LENGTH - 2*interfaceInfo.paddingSize, y, 2*interfaceInfo.paddingSize, fh);
			}

			gfx_drawText(DRAWING_SURFACE, pgfx_font, tr, tg, tb, ta, x, y+rect.h - interfaceInfo.paddingSize, buf, 0, 0);
			displayedChannels++;

			x += interfaceInfo.paddingSize + ERM_CHANNEL_NAME_LENGTH;
			event_element = srvIdx->first_event;
			while( event_element != NULL )
			{
				event = (EIT_event_t*)event_element->data;

				if(offair_getLocalEventTime(event, &event_tm, &event_tt) == 0)
				{
					event_len = offair_getEventDuration(event);
					if( event_tt + event_len > pEpg->curOffset && event_tt < end_tt )
					{
						if( event_tt >= pEpg->curOffset)
						{
							x = pEpg->timelineX + (int)(pEpg->pps * (event_tt - pEpg->curOffset));
						}
						w = event_tt >= pEpg->curOffset
							? (int)(pEpg->pps * offair_getEventDuration(event)) - ERM_TIMESTAMP_WIDTH
							: (int)(pEpg->pps * (event_tt + event_len - pEpg->curOffset)) - ERM_TIMESTAMP_WIDTH;
						if (x + w > timelineEnd )
						{
							w = timelineEnd - x;
						}
						DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
						gfx_drawRectangle(DRAWING_SURFACE, ERM_CELL_START_RED, ERM_CELL_START_GREEN, ERM_CELL_START_BLUE, ERM_CELL_START_ALPHA, x, y, ERM_TIMESTAMP_WIDTH, fh);
						x += ERM_TIMESTAMP_WIDTH;
						color = pEpg->baseMenu.selectedItem == MENU_ITEM_EVENT && event_element == pEpg->highlightedEvent ? &sel_color : &genre_colors[(event->content.content >> 4) & 0x0F];
						gfx_drawRectangle(DRAWING_SURFACE, color->R, color->G, color->B, color->A, x, y, w, fh);
						GLAW_EFFECT;

						snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", event->description.event_name);
						buf[MENU_ENTRY_INFO_LENGTH-1] = 0;
						l = getMaxStringLength(buf, w-ERM_TIMESTAMP_WIDTH);
						buf[l] = 0;
						gfx_drawText(DRAWING_SURFACE, pgfx_font, tr, tg, tb, ta, x, y+rect.h - interfaceInfo.paddingSize, buf, 0, 0);
					}
				}
				event_element = event_element->next;
			}
		}
	} // end of services loop
	/* Arrows */
	if(displayedChannels == ERM_VISIBLE_CHANNELS) {
		x = interfaceInfo.clientX - interfaceInfo.paddingSize - ERM_ICON_SIZE;
		for(j = i; j < dvbChannel_getCount(); j++) {
			srvIdx = dvbChannel_getServiceIndex(j);

			if(j != pEpg->currentService && srvIdx && srvIdx->first_event != NULL) {
				interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "icon_up.png", x, interfaceInfo.clientY + interfaceInfo.paddingSize + 2*(interfaceInfo.paddingSize + fh), ERM_ICON_SIZE, ERM_ICON_SIZE, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignTop, NULL, NULL);
				interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "icon_down.png", x, interfaceInfo.clientY + (2+displayedChannels)*(interfaceInfo.paddingSize + fh), ERM_ICON_SIZE, ERM_ICON_SIZE, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignBottom, NULL, NULL);
				x = -1;
				break;
			}
		}
		if(x > 0) {
			for(j = 0; j < pEpg->serviceOffset; j++) {
				srvIdx = dvbChannel_getServiceIndex(j);
				if(j != pEpg->currentService && srvIdx && srvIdx->first_event != NULL) {
					interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "icon_up.png", x, interfaceInfo.clientY + interfaceInfo.paddingSize + 2*(interfaceInfo.paddingSize + fh), ERM_ICON_SIZE, ERM_ICON_SIZE, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignTop, NULL, NULL);
					interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "icon_down.png", x, interfaceInfo.clientY + (2+displayedChannels)*(interfaceInfo.paddingSize + fh), ERM_ICON_SIZE, ERM_ICON_SIZE, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignBottom, NULL, NULL);
					break;
				}
			}
		}
	}
	x = interfaceInfo.clientX + interfaceInfo.paddingSize;
	/* Fillers */
	for(; displayedChannels < ERM_VISIBLE_CHANNELS; displayedChannels++) {
		y += fh + interfaceInfo.paddingSize;
		gfx_drawRectangle(DRAWING_SURFACE, ERM_TITLE_RED, ERM_TITLE_GREEN, ERM_TITLE_BLUE, ERM_TITLE_ALPHA, x, y, interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize, fh);
	}
#ifdef ENABLE_PVR
#ifdef STBPNX
	/* Record jobs */
	for(event_element = pvr_jobs; event_element != NULL; event_element = event_element->next) {
		job = (pvrJob_t*)event_element->data;
		if(job->end_time > pEpg->curOffset && job->start_time < end_tt) {
			y = interfaceInfo.clientY + interfaceInfo.paddingSize + fh;
			x = job->start_time >= pEpg->curOffset
				? pEpg->timelineX + (int)(pEpg->pps * (job->start_time - pEpg->curOffset))
				: pEpg->timelineX;
			w = job->start_time >= pEpg->curOffset
				? (int)(pEpg->pps * (job->end_time - job->start_time))
				: (int)(pEpg->pps * (job->end_time - pEpg->curOffset));
			if(x + w > timelineEnd) {
				w = timelineEnd - x;
			}
			gfx_drawRectangle(DRAWING_SURFACE, ERM_RECORD_RED, ERM_RECORD_GREEN, ERM_RECORD_BLUE, ERM_RECORD_ALPHA, x+ERM_TIMESTAMP_WIDTH, y, w, interfaceInfo.paddingSize);
			if(job->type == pvrJobTypeDVB) {
				job_channel = offair_getIndex(job->info.dvb.channel);
				if((job_channel == pEpg->currentService || ( pEpg->serviceOffset <= job_channel && job_channel < i ) )) {
					y += interfaceInfo.paddingSize + fh;
					if(job_channel != pEpg->currentService) {
						displayedChannels = 1;
						for(j = pEpg->serviceOffset; j < job_channel; j++) {
							srvIdx = dvbChannel_getServiceIndex(j);
							if(j != pEpg->currentService && srvIdx && srvIdx->first_event != NULL) {
								displayedChannels++;
							}
						}
						y += displayedChannels * ( fh + interfaceInfo.paddingSize );
					}
					gfx_drawRectangle(DRAWING_SURFACE, ERM_RECORD_RED, ERM_RECORD_GREEN, ERM_RECORD_BLUE, ERM_RECORD_ALPHA, x+ERM_TIMESTAMP_WIDTH, y, w, interfaceInfo.paddingSize);
				}
			}
		}
	}
#endif // STBPNX
#endif // ENABLE_PVR
	/* Current time */
	time(&event_tt);
	if(event_tt > pEpg->curOffset && event_tt < end_tt && (t = localtime(&event_tt)) != NULL)
	{
		gfx_drawRectangle(DRAWING_SURFACE, ERM_TIMESTAMP_RED, ERM_TIMESTAMP_GREEN, ERM_TIMESTAMP_BLUE, ERM_TIMESTAMP_ALPHA, pEpg->timelineX + (int)(pEpg->pps * (event_tt - pEpg->curOffset)), interfaceInfo.clientY + interfaceInfo.paddingSize + fh, ERM_TIMESTAMP_WIDTH, (1+ERM_VISIBLE_CHANNELS)*interfaceInfo.paddingSize + (1+ERM_VISIBLE_CHANNELS)*fh);
	}
	/* Event info */
	x = interfaceInfo.clientX + interfaceInfo.paddingSize;
	y = interfaceInfo.clientY + (3+ERM_VISIBLE_CHANNELS)*interfaceInfo.paddingSize + (2+ERM_VISIBLE_CHANNELS)*fh;
	switch(pEpg->baseMenu.selectedItem)
	{
	case MENU_ITEM_BACK:
		str = _T("BACK");
		gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0);
		break;
	case MENU_ITEM_MAIN:
		str = _T("MAIN_MENU");
		gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0);
		break;
	default:
		if(pEpg->highlightedEvent != NULL)
		{
			event = (EIT_event_t*)pEpg->highlightedEvent->data;
			str = buf;

			// channel name
			snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", dvb_getServiceName(dvbChannel_getService(pEpg->highlightedService)));
			DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );
			gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0); 
			y += rect.h;

			if(offair_getLocalEventTime(event, &event_tm, &event_tt) == 0)
			{
				strftime( buf, 25, _T("DATESTAMP"), &event_tm);
				DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );
				gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0);
// 				x += ERM_CHANNEL_NAME_LENGTH + interfaceInfo.paddingSize;
				x += rect.w + 3*interfaceInfo.paddingSize;
				strftime( buf, 10, "%H:%M:%S", &event_tm);
				DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );
				gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0);
				x += rect.w;
				event_tt += offair_getEventDuration(event);
				strftime( buf, 10, "-%H:%M:%S", localtime(&event_tt));
				gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0);
			}
			/* // channel name
			snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", dvb_getServiceName(dvbChannel_getService(pEpg->highlightedService)));
			buf[MENU_ENTRY_INFO_LENGTH-1] = 0;
			l = getMaxStringLength(buf, interfaceInfo.clientX + interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize - x);
			buf[l] = 0;
			DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );
			gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x+interfaceInfo.paddingSize, y+rect.h - interfaceInfo.paddingSize, str, 0, 0); */

			x = interfaceInfo.clientX + interfaceInfo.paddingSize;
			y += interfaceInfo.paddingSize + fh;
			gfx_drawRectangle(DRAWING_SURFACE, ERM_CELL_START_RED, ERM_CELL_START_GREEN, ERM_CELL_START_BLUE, ERM_CELL_START_ALPHA, x, y, interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize, 2*fh);
			snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", event->description.event_name );
			buf[MENU_ENTRY_INFO_LENGTH-1] = 0;
			interface_drawTextWW(pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y, interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize, 2*fh, str, ALIGN_LEFT);
			y += interfaceInfo.paddingSize + 2*fh;
			snprintf(buf, MAX_TEXT, "%s", event->description.text );
			buf[MAX_TEXT-1] = 0;
			interface_drawTextWW(pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y, interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize, interfaceInfo.clientY + interfaceInfo.clientHeight - y - interfaceInfo.paddingSize, str, ALIGN_LEFT);
			//gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0);
		} else if(pEpg->highlightedService >=0 && pEpg->highlightedService < dvbChannel_getCount()) {
			EIT_service_t *service = dvbChannel_getService(pEpg->highlightedService);
			if(service) {
				snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s", dvb_getServiceName(service));
				str = buf;
				gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+rect.h - interfaceInfo.paddingSize, str, 0, 0);
			}
		}
	}
}

static EIT_event_t * epgMenu_highlightCurrentEvent(interfaceEpgMenu_t *pEpg)
{
	time_t eventStart = 0, eventEnd = 0;
	time_t maxEndTime = pEpg->curOffset + 3600 * pEpg->displayingHours;
	service_index_t *srvIdx = dvbChannel_getServiceIndex(pEpg->highlightedService);

	for(pEpg->highlightedEvent = srvIdx ? srvIdx->first_event : NULL;
		pEpg->highlightedEvent;
		pEpg->highlightedEvent =  pEpg->highlightedEvent->next )
	{
		offair_getEventTimes(pEpg->highlightedEvent->data, &eventStart, &eventEnd);
		if(eventEnd > pEpg->curOffset && eventStart < maxEndTime )
			break;
	}
	return pEpg->highlightedEvent ? pEpg->highlightedEvent->data : NULL;
}

static int offair_EPGRecordMenuProcessCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd)
{
	interfaceEpgMenu_t *pEpg = (interfaceEpgMenu_t *)pMenu;
	interfaceMenu_t *pParent;
	int i, service_index, service_count;
	time_t eventStart = 0;
	service_index_t *srvIdx;

	switch(cmd->command) {
	case interfaceCommandUp:
		switch( pMenu->selectedItem ) {
		case MENU_ITEM_BACK:
		case MENU_ITEM_MAIN:
			pMenu->selectedItem = MENU_ITEM_EVENT;
			service_index = pEpg->currentService;
			for(i = dvbChannel_getCount() - 1; i >= 0; i--) {
				srvIdx = dvbChannel_getServiceIndex(i);
				if(i != pEpg->currentService && srvIdx && srvIdx->first_event != NULL) {
					service_index = i;
					break;
				}
			}
			if (pEpg->highlightedService != service_index) {
				pEpg->highlightedService =  service_index;

				epgMenu_highlightCurrentEvent(pEpg);

				// counting displaying services
				if (pEpg->highlightedService != pEpg->currentService) {
					service_count = 0;
					for(i = pEpg->highlightedService; i >= 0; i--) {
						srvIdx = dvbChannel_getServiceIndex(i);
						if(i != pEpg->currentService && srvIdx && srvIdx->first_event) {
							service_count++;
							pEpg->serviceOffset = i;
							if(service_count == ERM_VISIBLE_CHANNELS) {
								break;
							}
						}
					}
				}
			} else if (!pEpg->highlightedEvent) {
				epgMenu_highlightCurrentEvent(pEpg);
			}
			break;
		case MENU_ITEM_TIMELINE:
			pMenu->selectedItem = MENU_ITEM_BACK;
			break;
		default: // MENU_ITEM_EVENT
			if(pEpg->highlightedService == pEpg->currentService) {
				pMenu->selectedItem = MENU_ITEM_TIMELINE;
			} else {
				/* Get our current position and guess new position */
				service_index = -1;
				for(i = 0; i < pEpg->highlightedService; i++) {
					srvIdx = dvbChannel_getServiceIndex(i);
					if(i != pEpg->currentService && srvIdx && srvIdx->first_event) {
						service_index = i;
					}
				}
				if(service_index < 0) {
					pEpg->highlightedService = pEpg->currentService;
				} else {
					pEpg->highlightedService = service_index;
					if (pEpg->highlightedService < pEpg->serviceOffset) {
						pEpg->serviceOffset = pEpg->highlightedService;
					}
				}
				epgMenu_highlightCurrentEvent(pEpg);
			}
		}
		break;
	case interfaceCommandDown:
		switch( pMenu->selectedItem ) {
		case MENU_ITEM_BACK:
		case MENU_ITEM_MAIN:
			pMenu->selectedItem = MENU_ITEM_TIMELINE;
			break;
		case MENU_ITEM_TIMELINE:
			pMenu->selectedItem = MENU_ITEM_EVENT;
			if (pEpg->highlightedService != pEpg->currentService) {
				pEpg->highlightedService =  pEpg->currentService;

				epgMenu_highlightCurrentEvent(pEpg);

				// counting displaying services
				if (pEpg->highlightedService != pEpg->currentService) {
					service_count = 1; // highlighted
					for(i = pEpg->highlightedService - 1; i >= 0; i-- ) {
						srvIdx = dvbChannel_getServiceIndex(i);
						if(i != pEpg->currentService && srvIdx && srvIdx->first_event) {
							service_count++;
							pEpg->serviceOffset = i;
							if(service_count == ERM_VISIBLE_CHANNELS) {
								break;
							}
						}
					}
				}
			} else if (!pEpg->highlightedEvent) {
				epgMenu_highlightCurrentEvent(pEpg);
			}
			break;
		default: // MENU_ITEM_EVENT
			service_index = -1;
			for( i = pEpg->highlightedService == pEpg->currentService ? 0 : pEpg->highlightedService+1;
				i < dvbChannel_getCount(); i++ ) {
				srvIdx = dvbChannel_getServiceIndex(i);
				if(i != pEpg->currentService && srvIdx && srvIdx->first_event) {
					service_index = i;
					break;
				}
			}
			if(service_index < 0) {
				pMenu->selectedItem = MENU_ITEM_BACK;
				break;
			}
			pEpg->highlightedService = service_index;
			epgMenu_highlightCurrentEvent(pEpg);
			if (pEpg->serviceOffset > service_index) {
				pEpg->serviceOffset = service_index;
			} else {
				service_count = 1;
				for(i = service_index - 1 ; service_count < ERM_VISIBLE_CHANNELS && i >= 0; i--) {
					srvIdx = dvbChannel_getServiceIndex(i);
					if(srvIdx && srvIdx->service && srvIdx->first_event) {
						service_count++;
						service_index = i;
					}
				}
				if(service_count == ERM_VISIBLE_CHANNELS && service_index > pEpg->serviceOffset) {
					pEpg->serviceOffset = service_index;
				}
			}
		}
		break;
	case interfaceCommandLeft:
		switch( pMenu->selectedItem )
		{
		case MENU_ITEM_BACK:
		case MENU_ITEM_MAIN:
			pMenu->selectedItem = 1-(pMenu->selectedItem+2)-2;
			break;
		case MENU_ITEM_TIMELINE:
			if (pEpg->curOffset > pEpg->minOffset)
				pEpg->curOffset -= 3600;
			break;
		default: // MENU_ITEM_EVENT
			srvIdx = dvbChannel_getServiceIndex(pEpg->highlightedService);
			if( pEpg->highlightedService < 0 ||
				pEpg->highlightedService >= dvbChannel_getCount() ||
				!srvIdx)
			{
				return 0;
			}
			if (pEpg->highlightedEvent) {
				time_t eventEnd = 0;
				EIT_event_t *event = (EIT_event_t*)pEpg->highlightedEvent->data;
				offair_getEventTimes(event, &eventStart, &eventEnd);
				if (eventStart <= pEpg->curOffset) {
					if (pEpg->curOffset <= pEpg->minOffset) {
						return 0;
					}
					pEpg->curOffset -= 3600;
					break;
				}

				list_element_t * prev = srvIdx->first_event;
				while (prev && prev->next != pEpg->highlightedEvent) {
					prev = prev->next;
				}
				if (!prev) {
					eprintf("%s:(%s:%s) unable to find previous programme!\n", __FUNCTION__,
							dvb_getServiceName(srvIdx->service), event->description.event_name);
					pEpg->highlightedEvent = NULL;
				} else {
					pEpg->highlightedEvent = prev;
					offair_getEventTimes(prev->data, &eventStart, &eventEnd);
					if (eventEnd <= pEpg->curOffset) {
						pEpg->highlightedEvent = NULL;
					}
				}
			} else {
				// If there was no highlighted event, move timeline one step back and find rightmost event
				if (pEpg->curOffset <= pEpg->minOffset) {
					return 0;
				}
				pEpg->curOffset -= 3600;

				epgMenu_highlightCurrentEvent(pEpg);
				if (!pEpg->highlightedEvent) {
					break;
				}
				time_t maxStartTime = pEpg->curOffset + 3600*ERM_DISPLAYING_HOURS;
				for (list_element_t *next = pEpg->highlightedEvent->next; next; next = next->next) {
					offair_getLocalEventTime(next->data, NULL, &eventStart);
					if (eventStart >= maxStartTime) {
						break;
					}
					pEpg->highlightedEvent = next;
				}
			}
			break;
		}
		break;
	case interfaceCommandRight:
		switch( pMenu->selectedItem )
		{
		case MENU_ITEM_BACK:
		case MENU_ITEM_MAIN:
			pMenu->selectedItem = 1-(pMenu->selectedItem+2)-2;
			break;
		case MENU_ITEM_TIMELINE:
			if (pEpg->curOffset < pEpg->maxOffset)
				pEpg->curOffset += 3600;
			break;
		default: // MENU_ITEM_EVENT
			if (pEpg->highlightedEvent == NULL) {
				if (pEpg->curOffset >= pEpg->maxOffset) {
					break;
				}
				pEpg->curOffset += 3600;
				epgMenu_highlightCurrentEvent(pEpg);
				break;
			}

			time_t maxStartTime = pEpg->curOffset + 3600*ERM_DISPLAYING_HOURS;
			time_t eventEnd = 0;
			offair_getEventTimes(pEpg->highlightedEvent->data, &eventStart, &eventEnd);
			if (eventEnd >= maxStartTime) {
				pEpg->curOffset += 3600;
				if (pEpg->maxOffset < pEpg->curOffset) {
					pEpg->maxOffset = pEpg->curOffset;
				}
				if (eventEnd <= pEpg->curOffset) {
					epgMenu_highlightCurrentEvent(pEpg);
				}
				break;
			}
			if (pEpg->highlightedEvent->next == NULL) {
				return 0;
			}

			pEpg->highlightedEvent = pEpg->highlightedEvent->next;
			offair_getLocalEventTime(pEpg->highlightedEvent->data, NULL, &eventStart);
			if (eventStart >= maxStartTime) {
				pEpg->curOffset += 3600;
				if (pEpg->maxOffset < pEpg->curOffset) {
					pEpg->maxOffset = pEpg->curOffset;
				}
				epgMenu_highlightCurrentEvent(pEpg);
			}
		}
		break;
	case interfaceCommandPageUp:
		service_index = -1;
		for(i = pEpg->serviceOffset - 1; i >= 0; i--) {
			srvIdx = dvbChannel_getServiceIndex(i);
			if(i != pEpg->currentService && srvIdx && srvIdx->first_event) {
				service_index = i;
				break;
			}
		}
		if(service_index < 0) {
			return 0;
		}
		pEpg->serviceOffset = service_index;
		break;
	case interfaceCommandPageDown:
		service_index = -1;
		for(i = pEpg->serviceOffset + 1; i < dvbChannel_getCount(); i++) {
			srvIdx = dvbChannel_getServiceIndex(i);
			if(i != pEpg->currentService && srvIdx && srvIdx->first_event) {
				service_index = i;
				break;
			}
		}
		if(service_index < 0) {
			return 0;
		}
		service_count = 1;
		for(i = service_index + 1; service_count < ERM_VISIBLE_CHANNELS && i < dvbChannel_getCount(); i++) {
			srvIdx = dvbChannel_getServiceIndex(i);
			if(i != pEpg->currentService && srvIdx && srvIdx->first_event) {
				service_count++;
			}
		}
		if(service_count == ERM_VISIBLE_CHANNELS) {
			pEpg->serviceOffset = service_index;
		} else {
			return 0;
		}
		break;
#ifdef ENABLE_PVR
#ifdef STBPNX
	case interfaceCommandRed:
		switch( pMenu->selectedItem )
		{
		case MENU_ITEM_EVENT:
			{
				list_element_t *job_element = NULL;
				pvrJob_t job;
				EIT_service_t *service = dvbChannel_getService(pEpg->highlightedService);

				event = (EIT_event_t*)pEpg->highlightedEvent->data;
				offair_getLocalEventTime(event,&t,&eventOffset);
				job.type         = pvrJobTypeDVB;
				job.start_time   = eventOffset;
				job.end_time     = eventOffset + offair_getEventDuration(event);
				job.info.dvb.channel  = service ? dvb_getServiceIndex(service) : -1;
				job.info.dvb.service  = service;
				job.info.dvb.event_id = event->event_id;
				switch( pvr_findOrInsertJob( &job, &job_element) )
				{
				case PVR_RES_MATCH:
					if( pvr_deleteJob(job_element) != 0 )
						interface_showMessageBox( _T("ERROR_SAVE_RECORD_LIST") , thumbnail_error, 0 );
					break;
				case PVR_RES_COLLISION:
					interface_showMessageBox( _T("RECORD_JOB_COLLISION") , thumbnail_info, 0 );
					return 0;
				case PVR_RES_ADDED:
					if( pvr_exportJobList() != 0 )
					{
						interface_showMessageBox( _T("ERROR_SAVE_RECORD_LIST") , thumbnail_error, 0 );
					}
					break;
				default:
					eprintf("%s: pvr_findOrInsertJob failed\n", __FUNCTION__);
					//case 1: interface_showMessageBox( _T("RECORD_JOB_INVALID_SERVICE") , thumbnail_info, 0 );
					return 0;
				}
			}
			break;
			//case MENU_ITEM_JOBS:
			//	break;
		default: ;
		}
		break;
#endif // STBPNX
#endif // ENABLE_PVR
	case interfaceCommandGreen:
		if( pMenu->selectedItem == MENU_ITEM_EVENT )
		{
			pMenu->selectedItem = MENU_ITEM_TIMELINE;
		}
		else if( pMenu->selectedItem == MENU_ITEM_TIMELINE )
		{
			pMenu->selectedItem = MENU_ITEM_EVENT;
		}
		break;
	case interfaceCommandExit:
		/*interface_showMenu(!interfaceInfo.showMenu, 1);
		break;*/
	case interfaceCommandBack:
		if ( pMenu->pParentMenu != NULL )
		{
			//dprintf("%s: go back\n", __FUNCTION__);
			interface_menuActionShowMenu(pMenu, pMenu->pParentMenu);
			//interfaceInfo.currentMenu = pMenu->pParentMenu;
		}
		break;
	case interfaceCommandMainMenu:
		pParent = pMenu;
		while ( pParent->pParentMenu != NULL )
		{
			pParent = pParent->pParentMenu;
		}
		interface_menuActionShowMenu(pMenu, pParent);
		//interfaceInfo.currentMenu = pParent;
		break;
	case interfaceCommandEnter:
	case interfaceCommandOk:
		switch( pMenu->selectedItem )
		{
		case MENU_ITEM_BACK:
			interface_menuActionShowMenu(pMenu, pMenu->pParentMenu);
			return 0;
		case MENU_ITEM_MAIN:
			pParent = pMenu;
			while ( pParent->pParentMenu != NULL )
			{
				pParent = pParent->pParentMenu;
			}
			interface_menuActionShowMenu(pMenu, pParent);
			return 0;
		default: ;
		}
		break;
	default: ;
	}
	interface_displayMenu(1);
	return 0;
}

static int offair_EPGMenuKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int channelNumber;

	if(pMenu->selectedItem < 0)
	{
		return 1;
	}

	channelNumber = GET_NUMBER(pArg);

	if (channelNumber >= 0)
	{
		if (cmd->command == interfaceCommandRed)
		{
			offair_initEPGRecordMenu(pMenu, SET_NUMBER(channelNumber));
			return 0;
		}
	}

	return 1;
}

#ifdef ENABLE_PVR
static int  offair_confirmStopRecording(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch( cmd->command )
	{
	case interfaceCommandExit:
	case interfaceCommandLeft:
	case interfaceCommandRed:
		return 0;
		break;
	case interfaceCommandGreen:
	case interfaceCommandOk:
	case interfaceCommandEnter:
		pvr_stopRecordingDVB(screenMain);
		return 0;
		break;
	default: return 1;
	}
	return 0;
}

static int  offair_stopRecording(interfaceMenu_t *pMenu, void *pArg)
{
	interface_showConfirmationBox( _T("CONFIRM_STOP_PVR"), thumbnail_question, offair_confirmStopRecording, NULL );
	return 0;
}
#endif

int32_t offair_updateChannelStatus(void)
{
	char  buf[MENU_ENTRY_INFO_LENGTH];

	interfaceMenu_t *dvbtMenu = _M &DVBTMenu;

	int hasChannel = 0;
//	int selectedMenuItem = MENU_ITEM_BACK; 
	interfaceMenuEntry_t *dvbtEntry = interface_getMenuEntry(dvbtMenu, CHANNEL_STATUS_ID);
	if(dvbtEntry == NULL) {
		return -1;
	}
#ifdef ENABLE_PVR
	if(pvr_isRecordingDVB()) {
		char *str = dvb_getTempServiceName(appControlInfo.pvrInfo.dvb.channel);
		if (str == NULL)
			str = _T("NOT_AVAILABLE_SHORT");
		snprintf(buf, sizeof(buf), "%s: %s", _T("RECORDING"), str);
		interface_addMenuEntry(dvbtMenu, buf, offair_stopRecording, NULL, thumbnail_recording);
		interface_changeMenuEntryInfo(dvbtEntry, buf, sizeof(buf));
		interface_changeMenuEntryArgs(dvbtEntry, NULL);
		interface_changeMenuEntryFunc(dvbtEntry, offair_stopRecording);
		interface_changeMenuEntryThumbnail(dvbtEntry, thumbnail_recording);
		interface_changeMenuEntrySelectable(dvbtEntry, 1);
	} else
#endif
	{
		switch(appControlInfo.playbackInfo.streamSource) {
			case streamSourceDVB:
				hasChannel = 	(appControlInfo.dvbInfo.channel >= 0) &&
								(appControlInfo.dvbInfo.channel != CHANNEL_CUSTOM) &&
								(appControlInfo.dvbInfo.channel < dvbChannel_getCount()) &&
								(current_service() != NULL);

				if(hasChannel) {
					snprintf(buf, sizeof(buf), "%s: %s", _T("SELECTED_CHANNEL"), dvb_getServiceName(current_service()));

					interface_changeMenuEntryInfo(dvbtEntry, buf, sizeof(buf));
					interface_changeMenuEntryArgs(dvbtEntry, CHANNEL_INFO_SET(screenMain, appControlInfo.dvbInfo.channel));
					interface_changeMenuEntryFunc(dvbtEntry, offair_channelChange);
					interface_changeMenuEntryThumbnail(dvbtEntry, thumbnail_selected);
					interface_changeMenuEntrySelectable(dvbtEntry, 1);
	//				selectedMenuItem = appControlInfo.dvbInfo.channel + 1;//shift on 1 becouse there are 1 disabled entry (DVB)
				}
				break;
			case streamSourceAnalogTV:
				hasChannel = 1;
#ifdef ENABLE_ANALOGTV
				snprintf(buf, sizeof(buf), "%s: %s", _T("SELECTED_CHANNEL"), analogtv_getServiceName(appControlInfo.tvInfo.id));

				interface_changeMenuEntryInfo(dvbtEntry, buf, sizeof(buf));
				interface_changeMenuEntryArgs(dvbtEntry, (void *)appControlInfo.tvInfo.id);
				interface_changeMenuEntryFunc(dvbtEntry, analogtv_activateChannel);
				interface_changeMenuEntryThumbnail(dvbtEntry, thumbnail_selected);
				interface_changeMenuEntrySelectable(dvbtEntry, 1);
#endif
//				selectedMenuItem = dvbChannel_getCount() + appControlInfo.tvInfo.id + 3; //shift on 3 becouse there are 3 disabled entry(DVB, ANALOGTV and start if f)
				break;
			default:
				break;
		}
		if(!hasChannel) {

			snprintf(buf, sizeof(buf), "%s: %s", _T("SELECTED_CHANNEL"), _T("NONE"));

			interface_changeMenuEntryInfo(dvbtEntry, buf, sizeof(buf));
			interface_changeMenuEntryThumbnail(dvbtEntry, thumbnail_not_selected);
			interface_changeMenuEntrySelectable(dvbtEntry, 0);
		}
	}
//	interface_setSelectedItem(dvbtMenu, selectedMenuItem);
	return 0;
}

void offair_fillDVBTMenu(void)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	interfaceMenu_t *dvbtMenu = _M &DVBTMenu;

	interface_clearMenuEntries(dvbtMenu);

	snprintf(buf, sizeof(buf), "%s: %s", _T("SELECTED_CHANNEL"), _T("NONE"));
	interface_addMenuEntryDisabled(dvbtMenu, buf, thumbnail_not_selected);

	interfaceMenuEntry_t *entry = menu_getLastEntry(dvbtMenu);
	if(entry) {
		interface_setMenuEntryId(entry, CHANNEL_STATUS_ID);
		offair_updateChannelStatus();
	} else {
		eprintf("%s()[%d]: ERROR: Cant get Channel status menu item!\n", __func__, __LINE__);
	}

#ifndef HIDE_EXTRA_FUNCTIONS
	switch ( dvbfe_getType(0) )  // Assumes same type FEs
	{
	case SYS_DVBT:
	case SYS_DVBT2:
		sprintf(buf,"%s: DVB-T", _T("DVB_MODE") );
		break;
	case SYS_DVBC_ANNEX_AC:
		sprintf(buf,"%s: DVB-C", _T("DVB_MODE") );
		break;
	case SYS_DVBS:
		sprintf(buf,"%s: DVB-S", _T("DVB_MODE") );
		break;
	//case SYS_ATSC:
	//case SYS_DVBC_ANNEX_B:
	default:
		eprintf("offair: unsupported FE type %d\n", dvbfe_getType(0));
		sprintf(buf,"%s: %s", _T("DVB_MODE"), _T("NOT_AVAILABLE_SHORT") );
		break;
	}

	interface_addMenuEntryDisabled(dvbtMenu, buf, thumbnail_channels);
#endif

	if(dvbChannel_getCount() > 0) {
		offair_addDVBChannelsToMenu();
	}

	if(analogtv_getChannelCount(0) > 0) {
		analogtv_addChannelsToMenu(dvbtMenu, dvbChannel_getCount());
	}

	if(dvbChannel_hasAnyEPG()) {
		interface_addMenuEntry2(dvbtMenu, _T("EPG_MENU"), dvb_services != NULL, interface_menuActionShowMenu, &EPGMenu, thumbnail_epg);
	} /*else {
		interface_addMenuEntryDisabled(dvbtMenu, _T("EPG_UNAVAILABLE"), thumbnail_not_selected);
	}*/
#ifdef ENABLE_PVR
	interface_addMenuEntry(dvbtMenu, _T("RECORDING"), pvr_initPvrMenu, SET_NUMBER(pvrJobTypeDVB), thumbnail_recording);
#endif

#ifndef HIDE_EXTRA_FUNCTIONS
	interface_addMenuEntry(dvbtMenu, _T(appControlInfo.offairInfo.tunerDebug ? "DVB_DEBUG_ENABLE" : "DVB_DEBUG_DISABLE"),
		offair_debugToggle, NULL, appControlInfo.offairInfo.tunerDebug ? thumbnail_yes : thumbnail_no);
#endif
}

void offair_activateChannelMenu(void)
{
	interface_menuActionShowMenu((interfaceMenu_t *)&DVBTMenu, (interfaceMenu_t *)&DVBTMenu);
}

#define MAX_SEARCH_STACK 32

typedef int (*list_comp_func)(list_element_t *, list_element_t *);

static int event_time_cmp(list_element_t *elem1, list_element_t *elem2)
{
	int result;
	EIT_event_t *event1,*event2;
	event1 = (EIT_event_t *)elem1->data;
	event2 = (EIT_event_t *)elem2->data;
	result = event1->start_year - event2->start_year;
	if(result != 0) {
		return result;
	}
	result = event1->start_month - event2->start_month;
	if(result != 0) {
		return result;
	}
	result = event1->start_day - event2->start_day;
	if(result != 0) {
		return result;
	}
	return event1->start_time - event2->start_time;
}

static list_element_t *intersect_sorted(list_element_t *list1, list_element_t *list2, list_comp_func cmp_func)
{
	list_element_t *current, *p1, *p2, *result;
	p1 = list1;
	p2 = list2;
	if (cmp_func(p1,p2) <= 0)
	{
		current = p1;
		p1 = p1->next;
	} else
	{
		current = p2;
		p2 = p2->next;
	}
	result = current;
	while ((p1 != NULL) && (p2 != NULL))
	{
		if (cmp_func(p1,p2) <= 0)
		{
			current->next = p1;
			current = p1;
			p1 = p1->next;
		} else
		{
			current->next = p2;
			current = p2;
			p2 = p2->next;
		}
	}
	if( p1 != NULL)
	{
		current->next = p1;
	}	else
	{
		current->next = p2;
	}
	return result;
}

void offair_sortEvents(list_element_t **event_list)
{
	struct list_stack_t
	{
		int level;
		list_element_t *list;
	} stack[MAX_SEARCH_STACK];
	int stack_pos;
	list_element_t *event_element;
	if(event_list == NULL)
	{
		return;
	}
	stack_pos = 0;
	event_element = *event_list;
	while( event_element != NULL )
	{
		stack[stack_pos].level = 1;
		stack[stack_pos].list = event_element;
		event_element = event_element->next;
		stack[stack_pos].list->next = NULL;
		stack_pos++;
		while( (stack_pos > 1) && (stack[stack_pos - 1].level == stack[stack_pos - 2].level))
		{
			stack[stack_pos - 2].list = intersect_sorted(stack[stack_pos - 2].list, stack[stack_pos - 1].list, event_time_cmp);
			stack[stack_pos - 2].level++;
			stack_pos--;
		}
	}
	while( (stack_pos > 1) && (stack[stack_pos - 1].level == stack[stack_pos - 2].level))
	{
		stack[stack_pos - 2].list = intersect_sorted(stack[stack_pos - 2].list, stack[stack_pos - 1].list, event_time_cmp);
		stack[stack_pos - 2].level++;
		stack_pos--;
	}
	if(stack_pos > 0)
	{
		*event_list = stack[0].list;
	}
}

void offair_sortSchedule(void)
{
	for(list_element_t *s = dvb_services; s != NULL; s = s->next ) {
		EIT_service_t * service = s->data;
		offair_sortEvents(&service->schedule);
	}
}

void offair_clearServiceList(int permanent)
{
#ifdef ENABLE_PVR
	pvr_stopRecordingDVB(screenMain);
#endif
	offair_stopVideo(screenMain, 1);

	appControlInfo.dvbInfo.channel = 0;
	appControlInfo.offairInfo.previousChannel = 0;
	dvb_clearServiceList(permanent);
#if (defined ENABLE_PVR) && (defined STBPNX)
	pvr_purgeDVBRecords();
#endif
	dvbChannel_load();
	dvbChannel_save();
}

static int offair_showSchedule(interfaceMenu_t *pMenu, int channel)
{
	if(dvbChannel_hasSchedule(channel)) {
		return offair_showScheduleMenu(pMenu, SET_NUMBER(channel));
	}

	interface_showMessageBox( _T("EPG_UNAVAILABLE"), thumbnail_epg ,3000);
	return 1;
}

static int offair_showScheduleMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buffer[MAX_MESSAGE_BOX_LENGTH], *str;
	int buf_length = 0, event_length;
	list_element_t *event_element;
	EIT_event_t *event;
	int old_mday = -1;
	struct tm event_tm;
	time_t event_start;
	EIT_service_t *service;

	offair_scheduleIndex = GET_NUMBER(pArg);

	service = dvbChannel_getService(offair_scheduleIndex);
	if(service == NULL) {
		return -1;
	}

	buffer[0] = 0;
	str = buffer;
	for(event_element = service->schedule; event_element != NULL; event_element = event_element->next) {
		event = (EIT_event_t *)event_element->data;
		event_length = strlen( (char*)event->description.event_name );
		/* 11 = strlen(timestamp + ". " + "\n"); */
		if( (buf_length + 11 + event_length) > MAX_MESSAGE_BOX_LENGTH )
		{
			break;
		}
		if(offair_getLocalEventTime( event, &event_tm, &event_start ) == 0)
		{
			if ( event_tm.tm_mday != old_mday )
			{
				/* 64 - approximate max datestamp length in most languages */
				if( (buf_length + 64 + 11 + event_length) > MAX_MESSAGE_BOX_LENGTH )
					break;
				strftime(str, 64, _T("SCHEDULE_TIME_FORMAT"), &event_tm);
				buf_length += strlen(str);
				str = &buffer[buf_length];
				*str = '\n';
				buf_length++;
				str++;
				old_mday = event_tm.tm_mday;
			}
			strftime( str, 9, "%T", &event_tm );
		} else
		{
			strcpy(str, "--:--:--");
		}
		buf_length += 8;
		str = &buffer[buf_length];
		sprintf(str, ". %s\n", (char*)event->description.event_name);
		buf_length += event_length + 3;
		str = &buffer[buf_length];
	}
	interface_showScrollingBox(buffer, thumbnail_epg, NULL, NULL );
	return 0;
}

static int offair_fillEPGMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int i;
	char buf[MENU_ENTRY_INFO_LENGTH], *str;

	interface_clearMenuEntries((interfaceMenu_t*)&EPGMenu);

	if(dvb_services == NULL) {
		str = _T("NONE");
		interface_addMenuEntryDisabled((interfaceMenu_t*)&EPGMenu, str, thumbnail_not_selected);
		return 0;
	}

#ifndef ENABLE_PVR
	str = _T("SCHEDULE");
	interface_addMenuEntry((interfaceMenu_t*)&EPGMenu, str, interface_menuActionShowMenu, (void*)&EPGRecordMenu, thumbnail_epg);
#endif

	for(i = 0; i < dvbChannel_getCount(); ++i) {
		EIT_service_t *service = dvbChannel_getService(i);
		if(dvbChannel_hasSchedule(i)) {
			snprintf(buf, sizeof(buf), "%s. %s", offair_getChannelNumberPrefix(i), service->service_descriptor.service_name);
			interface_addMenuEntry((interfaceMenu_t*)&EPGMenu, buf, offair_showScheduleMenu, SET_NUMBER(i), thumbnail_channels);
		} else {
			if(service) {
				dprintf("%s: Service %s has no EPG\n", __FUNCTION__, dvb_getServiceName(service));
			} else {
				dprintf("%s: Service %d is not available\n", __FUNCTION__, i);
			}
		}
	}

	//interface_menuActionShowMenu(pMenu, (void*)&EPGMenu);

	return 0;
}

#ifdef ENABLE_DVB_DIAG
static int offair_diagnisticsInfoCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int32_t ret = 1;
	switch(cmd->command) {
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
			appControlInfo.dvbInfo.reportedSignalStatus = 0;
			ret = 0;
			break;
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			dprintf("%s: start info wizard\n", __FUNCTION__);
			interfaceInfo.showMessageBox = 0;
			offair_wizardStart((interfaceMenu_t *)&DVBTMenu, pArg);
			appControlInfo.dvbInfo.reportedSignalStatus = 0;
			ret = 0;
			break;
		default:
	}

	return ret;
}

static int offair_diagnisticsCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch(cmd->command) {
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
			if(appControlInfo.offairInfo.diagnosticsMode == DIAG_ON) {
				interface_showMessageBox(_T("DIAGNOSTICS_DISABLED"), thumbnail_info, 5000);
				appControlInfo.offairInfo.diagnosticsMode = DIAG_OFF;
				ret = 1;
			} else {
				ret = 0;
			}
			break;
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			dprintf("%s: start diag wizard\n", __FUNCTION__);
			interfaceInfo.showMessageBox = 0;
			offair_wizardStart((interfaceMenu_t *)&DVBTMenu, pArg);
			appControlInfo.dvbInfo.reportedSignalStatus = 0;
			ret = 0;
			break;
		default:
	}

	return 1;
}

static int offair_checkSignal(int which, list_element_t **pPSI)
{
	uint16_t snr, signal;
	uint32_t ber, uncorrected_blocks;
	fe_status_t status;
	list_element_t *running_program_map = NULL;
	int res = 1;
	int ccerrors;
	stb810_signalStatus lastSignalStatus = signalStatusNoStatus;

	if(pPSI != NULL) {
		running_program_map = *pPSI;
	}

	dprintf("%s: Check signal %d!\n", __FUNCTION__, appControlInfo.dvbInfo.reportedSignalStatus);

	status = dvbfe_getSignalInfo(appControlInfo.dvbInfo.adapter, &snr, &signal, &ber, &uncorrected_blocks);

	ccerrors = phStbSystemManager_GetErrorsStatistics(phStbRpc_MainTMErrorId_DemuxerContinuityCounterMismatch, 1);

	dprintf("%s: Status: %d, ccerrors: %d\n", __FUNCTION__, status, ccerrors);

	if(status == 0) {
		lastSignalStatus = signalStatusNoSignal;
	} else if(signal < BAD_SIGNAL && (uncorrected_blocks > BAD_UNC || ber > BAD_BER)) {
		lastSignalStatus = signalStatusBadSignal;
	} else if(running_program_map != NULL) {
		int has_new_channels = 0;
		int missing_old_channels = 0;
		list_element_t *element, *found;

		/* find new */
		element = running_program_map;
		while(element != NULL) {
			EIT_service_t *service = (EIT_service_t *)element->data;
			if(service != NULL && (service->flags & (serviceFlagHasPAT|serviceFlagHasPMT)) == (serviceFlagHasPAT|serviceFlagHasPMT)) {
				found = find_element_by_header(dvb_services, (unsigned char*)&service->common, sizeof(EIT_common_t), NULL);
				if(found == NULL || (((EIT_service_t *)found->data)->flags & (serviceFlagHasPAT|serviceFlagHasPMT)) != (serviceFlagHasPAT|serviceFlagHasPMT)) {
					dprintf("%s: Found new channel!\n", __FUNCTION__);
					has_new_channels = 1;
					break;
				}
			}
			element = element->next;
		}

		/* find missing */
		element = dvb_services;
		while(element != NULL) {
			EIT_service_t *service = (EIT_service_t *)element->data;
			if(service != NULL && (service->flags & (serviceFlagHasPAT|serviceFlagHasPMT)) == (serviceFlagHasPAT|serviceFlagHasPMT)) {
				found = find_element_by_header(running_program_map, (unsigned char*)&service->common, sizeof(EIT_common_t), NULL);
				if (found == NULL || (((EIT_service_t *)found->data)->flags & (serviceFlagHasPAT|serviceFlagHasPMT)) != (serviceFlagHasPAT|serviceFlagHasPMT))
				{
					dprintf("%s: Some channels are missing!\n", __FUNCTION__);
					missing_old_channels = 1;
					break;
				}
			}
			element = element->next;
		}

		if (has_new_channels || missing_old_channels)
		{
			lastSignalStatus = signalStatusNewServices;
		}
	} else if (running_program_map == NULL && pPSI != NULL && uncorrected_blocks == 0)
	{
		lastSignalStatus = signalStatusNoPSI;
	}

	// When we're called by 'info' button or we're already showing info
	if (lastSignalStatus == signalStatusNoStatus && (pPSI == NULL || appControlInfo.dvbInfo.reportedSignalStatus))
	{
		if (signal > AVG_SIGNAL && (uncorrected_blocks > BAD_UNC || ber > BAD_BER))
		{
			lastSignalStatus = signalStatusBadQuality;
		} else if (signal > AVG_SIGNAL && uncorrected_blocks == 0 && ber < BAD_BER && ccerrors > BAD_CC)
		{
			lastSignalStatus = signalStatusBadCC;
		} else if (signal < BAD_SIGNAL && uncorrected_blocks == 0 && ber < BAD_BER)
		{
			lastSignalStatus = signalStatusLowSignal;
		} else
		{
			lastSignalStatus = signalStatusNoProblems;
		}
	}

	if (lastSignalStatus != signalStatusNoStatus && (pPSI == NULL || lastSignalStatus == appControlInfo.dvbInfo.lastSignalStatus))
	{
		char *str = "";
		int confirm = 1;
		int delay = 10000;
		void *param = (void*)current_service()->media.frequency;

		dprintf("%s: Signal status changed!\n", __FUNCTION__);

		res = 0;

		switch (lastSignalStatus)
		{
		case signalStatusNoSignal:
			str = _T("DIAGNOSTICS_NO_SIGNAL");
			break;
		case signalStatusBadSignal:
			str = _T("DIAGNOSTICS_BAD_SIGNAL");
			break;
		case signalStatusNewServices:
			str = _T("DIAGNOSTICS_NEW_SERVICES");
			param = NULL;
			break;
		case signalStatusNoPSI:
			str = _T("DIAGNOSTICS_NO_PSI");
			confirm = 0;
			delay = 0;
			res = 1;
			break;
		case signalStatusBadQuality:
			str = _T("DIAGNOSTICS_BAD_QUALITY");
			break;
		case signalStatusBadCC:
			str = _T("DIAGNOSTICS_BAD_CC");
			confirm = 0;
			res = 1;
			break;
		case signalStatusLowSignal:
			str = _T("DIAGNOSTICS_LOW_SIGNAL");
			break;
		case signalStatusNoProblems:
		default:
			str = _T("DIAGNOSTICS_NO_PROBLEMS");
			confirm = 0;
			res = 1;
			break;
		}

		/* Don't show same error many times */
		if (appControlInfo.dvbInfo.savedSignalStatus != lastSignalStatus || pPSI == NULL)
		{
			dprintf("%s: Show message!\n", __FUNCTION__);
			/* Make sure play control is visible with it's signal levels */
			interface_playControlRefresh(0);
			if (confirm)
			{
				appControlInfo.dvbInfo.reportedSignalStatus = 1;
				interface_showConfirmationBox(str, thumbnail_question, pPSI == NULL ? offair_diagnisticsInfoCallback : offair_diagnisticsCallback, param);
			} else
			{
				appControlInfo.dvbInfo.reportedSignalStatus = 0;
				interface_showMessageBox(str, thumbnail_info, delay);
			}
		}
		appControlInfo.dvbInfo.savedSignalStatus = lastSignalStatus;
	}

	dprintf("%s: Signal id: %d/%d\n", __FUNCTION__, lastSignalStatus, appControlInfo.dvbInfo.lastSignalStatus);

	appControlInfo.dvbInfo.lastSignalStatus = lastSignalStatus;

	return res;
}

static int  offair_updatePSI(void* pArg)
{
	int which = GET_NUMBER(pArg);
	list_element_t *running_program_map = NULL;
	int my_channel = appControlInfo.dvbInfo.channel;

	dprintf("%s: in\n", __FUNCTION__);

	if( current_service() == NULL )
	{
		eprintf("offair: Can't update PSI: service %d is null\n",appControlInfo.dvbInfo.channel);
		return -1;
	}

	//if (appControlInfo.dvbInfo.scanPSI)
	{
		dprintf("%s: *** updating PSI [%s]***\n", __FUNCTION__, dvb_getServiceName(current_service()));
		dvb_scanForPSI( appControlInfo.dvbInfo.adapter, current_service()->media.frequency, &running_program_map );
		dprintf("%s: *** PSI updated ***\n", __FUNCTION__ );

		dprintf("%s: active %d, channel %d/%d\n", __FUNCTION__, appControlInfo.dvbInfo.active, my_channel, appControlInfo.dvbInfo.channel);

		if( appControlInfo.dvbInfo.active && my_channel == appControlInfo.dvbInfo.channel ) // can be 0 if we switched from DVB when already updating
		{
			/* Make diagnostics... */
			if (appControlInfo.offairInfo.diagnosticsMode == DIAG_ON)
			{
				appControlInfo.dvbInfo.scanPSI = offair_checkSignal(which, &running_program_map);
			}
		}
	}

	if (appControlInfo.dvbInfo.active)
	{
		interface_addEvent(offair_updatePSI, pArg, PSI_UPDATE_INTERVAL, 1);
	}

	free_services(&running_program_map);

	dprintf("%s: out\n");

	return 0;
}
#endif

static int offair_updateEPG(void* pArg)
{
	char desc[BUFFER_SIZE];
	int my_channel = appControlInfo.dvbInfo.channel;

	dprintf("%s: in\n", __FUNCTION__);

	if(current_service() == NULL) {
		dprintf("offair: Can't update EPG: service %d is null\n",appControlInfo.dvbInfo.channel);
		return -1;
	}

	mysem_get(epg_semaphore);
/*	dprintf("%s: Check PSI: %d, diag mode %d\n", __FUNCTION__, appControlInfo.dvbInfo.scanPSI, appControlInfo.offairInfo.diagnosticsMode);
	if(appControlInfo.dvbInfo.scanPSI) {
		offair_updatePSI(pArg);
	}*/

	if(appControlInfo.dvbInfo.active && my_channel == appControlInfo.dvbInfo.channel) {// can be 0 if we switched from DVB when already updating
		dprintf("%s: scan for epg\n", __FUNCTION__);

		dprintf("%s: *** updating EPG [%s]***\n", __FUNCTION__, dvb_getServiceName(current_service()));
		dvb_scanForEPG( appControlInfo.dvbInfo.adapter, current_service()->media.frequency );
		dprintf("%s: *** EPG updated ***\n", __FUNCTION__ );

		dprintf("%s: if active\n", __FUNCTION__);

		if(appControlInfo.dvbInfo.active && my_channel == appControlInfo.dvbInfo.channel ) {// can be 0 if we switched from DVB when already updating
			dprintf("%s: refresh event\n", __FUNCTION__);

			if(appControlInfo.dvbInfo.active) {
				offair_getServiceDescription(current_service(),desc,_T("DVB_CHANNELS"));
				interface_playControlUpdateDescription(desc);
				interface_addEvent(offair_updateEPG, pArg, EPG_UPDATE_INTERVAL, 1);
			}
		}
	}

	dprintf("%s: update epg out\n", __FUNCTION__);
	mysem_release(epg_semaphore);

	return 0;
}

int  offair_tunerPresent(void)
{
#if (defined STSDK)
	if(st_getBoardId() == eSTB850) {
		return 1; //we always have analog tuner for stb850.
	}
#endif
	return dvbfe_hasTuner(0) || dvbfe_hasTuner(1);
}

void offair_buildDVBTMenu(interfaceMenu_t *pParent)
{
	int offair_icons[4] = { 0, 0, 0, statusbar_f4_rename};
	interfaceMenu_t *scheduleMenu;

	mysem_create(&epg_semaphore);
	mysem_create(&offair_semaphore);

	//Register callback on dvbChannels chanched
	dvbChannel_registerCallbackOnChange(offair_dvbChannelsChangeCallback, NULL);
	createListMenu(&DVBTMenu, _T("DVB_CHANNELS"), thumbnail_dvb, NULL, pParent,
		interfaceListMenuIconThumbnail, offair_enterDVBTMenu, NULL, NULL);

	interface_setCustomKeysCallback(_M &DVBTMenu, offair_keyCallback);

#ifdef ENABLE_PVR
	offair_icons[0] = statusbar_f1_record;
	scheduleMenu = _M &PvrMenu;
#else
	scheduleMenu = _M &EPGMenu;
#endif
	createBasicMenu(&EPGRecordMenu.baseMenu, interfaceMenuEPG, _T("SCHEDULE"), thumbnail_epg, offair_icons, scheduleMenu,
					offair_EPGRecordMenuProcessCommand, offair_EPGRecordMenuDisplay, NULL, offair_initEPGRecordMenu, NULL, NULL);

	createListMenu(&EPGMenu, _T("EPG_MENU"), thumbnail_epg, NULL, (interfaceMenu_t *)&DVBTMenu,
					interfaceListMenuIconThumbnail, offair_fillEPGMenu, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&EPGMenu, offair_EPGMenuKeyCallback);

#ifdef ENABLE_ANALOGTV
	analogtv_initMenu((interfaceMenu_t *)&DVBTMenu);
#endif

	wizard_init();
#ifdef ENABLE_STATS
	stats_init();
#endif
}

void offair_cleanupMenu()
{
	if(epg_semaphore != 0) {
		mysem_destroy(epg_semaphore);
		epg_semaphore = 0;
	}
	if(offair_semaphore != 0) {
		mysem_destroy(offair_semaphore);
		offair_semaphore = 0;
	}
}

#ifdef DVB_FAVORITES
void offair_playURL(char* URL, int which)
{
	url_desc_t ud;
	int i, j;
	unsigned short srv_id = 0, ts_id = 0, pcr = 0, apid = 0, tpid = 0, vpid = 0, at = 0, vt = 0;
	__u32 frequency;
	char c;
	DvbParam_t param;

	dprintf("%s: '%s'\n", __FUNCTION__, URL);

	i = parseURL(URL,&ud);
	if (i != 0) {
		eprintf("offair: Error %d parsing '%s'\n",i,URL);
		return;
	}
#if (defined ENABLE_PVR) && (defined STBPNX)
	if (pvr_isRecordingDVB()) {
		pvr_showStopPvr( interfaceInfo.currentMenu, (void*)-1 );
		return;
	}
#endif

	frequency = atol(ud.address);
	ts_id = atoi(ud.source);
	srv_id = ud.port;
	// FIXME: Not used in current implementation
	(void)ts_id;
	(void)srv_id;

	if(ud.stream[0] == '?')
	{
		i = 1;
		while(ud.stream[i])
		{
			switch(ud.stream[i])
			{
			case '&': ++i; break;
			case 'p': /* pcr=PCR */
				if(strncmp(&ud.stream[i],"pcr=",4) == 0)
				{
					for( j = i + 4; ud.stream[j]>= '0' && ud.stream[j] <= '9'; ++j);
					c = ud.stream[j];
					ud.stream[j] = 0;
					pcr = atoi(&ud.stream[i+4]);
					ud.stream[j] = c;
					i = j;
				}
				else
				{
					dprintf("%s: Error parsing dvb url at %d: 'pcr=' expected\n", __FUNCTION__,i);
					goto parsing_done;
				}
				break;
			case 'v': /* vt | vp */
				switch(ud.stream[i+1])
				{
				case 't': /* vt=VIDEO_TYPE */
					if( ud.stream[i+2] != '=' )
					{
						dprintf("%s: Error parsing dvb url at %d: '=' expected but '%c' found\n", __FUNCTION__,i,ud.stream[i+2]);
						goto parsing_done;
					}
					i += 3;
					if( strncasecmp(&ud.stream[i],"MPEG2",5)==0 )
					{
						vt = streamTypeVideoMPEG2;
						i += 5;
					}
					else if ( strncasecmp(&ud.stream[i],"H264",4)==0 )
					{
						vt = streamTypeVideoH264;
						i += 4;
					}
					else
					{
						dprintf("%s: Error parsing dvb url at %d: video type expected\n", __FUNCTION__,i,&ud.stream[i]);
						goto parsing_done;
					}
					break;
				case 'p': /* vp=VIDEO_PID */
					if( ud.stream[i+2] != '=' )
					{
						dprintf("%s: Error parsing dvb url at %d: '=' expected but '%c' found\n", __FUNCTION__,i,ud.stream[i+2]);
						goto parsing_done;
					}
					i += 3;
					for( j = i; ud.stream[j]>= '0' && ud.stream[j] <= '9'; ++j);
					c = ud.stream[j];
					ud.stream[j] = 0;
					vpid = atoi(&ud.stream[i]);
					ud.stream[j] = c;
					i = j;
					break;
				default:
					dprintf("%s: Error parsing dvb url at %d: vt or vp expected but v'%c' found\n", __FUNCTION__,i,ud.stream[i+1]);
					goto parsing_done;
				}
				break;  /* vt | vp */
			case 't':
				if( ud.stream[i+1] == 'p' && ud.stream[i+2] == '=' )
				{
					i += 3;
					for( j = i; ud.stream[j]>= '0' && ud.stream[j] <= '9'; ++j);
					c = ud.stream[j];
					ud.stream[j] = 0;
					tpid = atoi(&ud.stream[i]);
					ud.stream[j] = c;
					i = j;
				} else
				{
					dprintf("%s: Error parsing dvb url at %d: tp expected but t'%c' found\n", __FUNCTION__,i, ud.stream[i+1]);
					goto parsing_done;
				}
				break;
			case 'a': /* at | ap */
				switch(ud.stream[i+1])
				{
				case 't': /* at=AUDIO_TYPE */
					if( ud.stream[i+2] != '=' )
					{
						dprintf("%s: Error parsing dvb url at %d: '=' expected but '%c' found\n", __FUNCTION__,i,ud.stream[i+2]);
						goto parsing_done;
					}
					i += 3;
					if( strncasecmp(&ud.stream[i],"MP3",3)==0 )
					{
						at = streamTypeAudioMPEG1;
						i += 3;
					}
					else if ( strncasecmp(&ud.stream[i],"AAC",3)==0 )
					{
						at = streamTypeAudioAAC;
						i += 3;
					}
					else if ( strncasecmp(&ud.stream[i],"AC3",3)==0 )
					{
						at = streamTypeAudioAC3;
						i += 3;
					}
					else
					{
						dprintf("%s: Error parsing dvb url at %d: video type expected\n", __FUNCTION__,i);
						goto parsing_done;
					}
					break;
				case 'p': /* ap=AUDIO_PID */
					if( ud.stream[i+2] != '=' )
					{
						dprintf("%s: Error parsing dvb url at %d: '=' expected but '%c' found\n", __FUNCTION__,i,ud.stream[i+2]);
						goto parsing_done;
					}
					i += 3;
					for( j = i; ud.stream[j]>= '0' && ud.stream[j] <= '9'; ++j);
					c = ud.stream[j];
					ud.stream[j] = 0;
					apid = atoi(&ud.stream[i]);
					ud.stream[j] = c;
					i = j;
					break;
				default:
					dprintf("%s: Error parsing dvb url at %d: at or ap expected but a'%c' found\n", __FUNCTION__,i,ud.stream[i+1]);
					goto parsing_done;
				}
				break; /* at | ap */
			default:
				dprintf("%s: Error parsing dvb url at %d: unexpected symbol '%c' found\n", __FUNCTION__,i,ud.stream[i]);
				goto parsing_done;
			}
		}
		/* while(ud.stream[i]) */
	}
parsing_done:
	dprintf("%s: dvb url parsing done: freq=%ld srv_id=%d ts_id=%d vpid=%d apid=%d vtype=%d atype=%d pcr=%d\n", __FUNCTION__,
		frequency,srv_id,ts_id,vpid,apid,vt,at,pcr);

	if ( appControlInfo.dvbInfo.active != 0 )
	{
		// force showState to NOT be triggered
		interfacePlayControl.activeButton = interfacePlayControlStop;
		offair_stopVideo(which, 0);
	}

	param.frequency = frequency;
	param.mode = DvbMode_Watch;
	param.adapter = offair_getTuner();
	param.param.playParam.audioPid = apid;
	param.param.playParam.videoPid = vpid;
	param.param.playParam.textPid  = tpid;
	param.param.playParam.pcrPid = pcr;
	param.directory = NULL;
	param.media = NULL;

	interface_playControlSetInputFocus(inputFocusPlayControl);

	interface_playControlSetup(offair_play_callback, SET_NUMBER(which), interfacePlayControlStop|interfacePlayControlPlay|interfacePlayControlPrevious|interfacePlayControlNext, URL, vpid != 0 ? thumbnail_channels : thumbnail_radio);
	interface_playControlSetDisplayFunction(offair_displayPlayControl);
	interface_playControlSetProcessCommand(offair_playControlProcessCommand);
	interface_playControlSetChannelCallbacks(offair_startNextChannel, appControlInfo.playbackInfo.playlistMode == playlistModeFavorites ? playlist_setChannel : offair_setChannel);

	offair_checkParentControl(which, &param, at, vt);

	playlist_setLastUrl(URL);

	if ( appControlInfo.dvbInfo.active != 0 )
	{
		interface_showMenu(0, 1);
	}
}
#endif // DVB_FAVORITES

#if 0
static char* offair_getLCNText(int field, void *pArg)
{
	return field == 0 ? offair_lcn_buf : NULL;
}

int offair_changeLCN(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_LCN"), MAX_MEMORIZED_SERVICES <= 100 ? "\\d{2}" : "\\d{3}" , offair_getUserLCN, offair_getLCNText, inputModeDirect, pArg);
	return 0;
}

static int offair_getUserLCN(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int serviceNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
	int screen = CHANNEL_INFO_GET_SCREEN(pArg);
	int lcn;

	if(value == NULL) {
		return 1;
	}

	lcn = strtol(value,NULL,10);
	if(serviceNumber == lcn || lcn < 0 || lcn >= dvbChannel_getCount()) {
		return 0;
	}

	dvbChannel_swapServices(lcn, serviceNumber);
	dvbChannel_save();

	return 0;
}
#endif

static int offair_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	EIT_service_t *service;
	int channelNumber;
	char URL[MAX_URL];

	dprintf("%s: in\n", __FUNCTION__);

	if(cmd->command == interfaceCommandRed) {
		appControlInfo.offairInfo.sorting = (appControlInfo.offairInfo.sorting + 1) % serviceSortCount;
		saveAppSettings();
		dvbChannel_sort(appControlInfo.offairInfo.sorting);

		interface_displayMenu(1);
		return 0;
	}

	if((pMenu->selectedItem < 0) || (GET_NUMBER(pArg) < 0)) {
		return 1;
	}

	channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
	service = dvbChannel_getService(channelNumber);

	switch(cmd->command) {
#ifdef DVB_FAVORITES
		case interfaceCommandYellow:
			dvb_getServiceURL(service, URL);
			eprintf("offair: Add to Playlist '%s'\n",URL);
			playlist_addUrl(URL, dvb_getServiceName(service));
			return 0;
#endif // DVB_FAVORITES
		case interfaceCommandInfo:
		case interfaceCommandGreen:
			if(menu_entryIsAnalogTv(pMenu, pMenu->selectedItem)) {
				analogtv_getServiceDescription(channelNumber, URL, sizeof(URL));
			} else {
				dvb_getServiceDescription(service, URL);
			}
			eprintf("offair: Channel %d info:\n%s\n", channelNumber, URL);
			interface_showMessageBox(URL, thumbnail_info, 0);
			return 0;
		case interfaceCommandEpg:
			offair_showSchedule(pMenu, channelNumber);
			return 0;
#ifdef ENABLE_MULTI_VIEW
		case interfaceCommandTV:
			if (
#ifdef ENABLE_PVR
				!pvr_isRecordingDVB() &&
#endif
				 can_play(channelNumber) && has_video(channelNumber)
			   )
				offair_multiviewPlay(pMenu, pArg);
			return 0;
#endif // ENABLE_MULTI_VIEW
		default: ;
	}

	return 1;
}

int offair_getIndex(int index)
{
	EIT_service_t* service = dvb_getService(index);
	return dvbChannel_getIndex(service);
}

int offair_getCurrentServiceIndex(int which)
{
	if(appControlInfo.dvbInfo.active == 0) {
		return -1;
	}
	return dvb_getServiceIndex(current_service());
}


#ifdef ENABLE_STATS
static int offair_updateStats(int which)
{
	time_t now = time(NULL) - statsInfo.today;
	if( now >= 0 && now < 24*3600 )
	{
		now /= STATS_RESOLUTION;
		statsInfo.watched[now] = appControlInfo.dvbInfo.channel;
		stats_save();
	} else /* Date changed */
	{
		stats_init();
		offair_updateStats(which);
	}
	return 0;
}

static int offair_updateStatsEvent(void *pArg)
{
	int which = GET_NUMBER(pArg);
	if( appControlInfo.dvbInfo.active == 0 )
		return 1;
	offair_updateStats(which);
	if (appControlInfo.dvbInfo.active)
	{
		interface_addEvent(offair_updateStatsEvent, pArg, STATS_UPDATE_INTERVAL, 1);
	}
	return 0;
}
#endif // ENABLE_STATS

int32_t offair_findCapableTuner(EIT_service_t *service, uint32_t *adapter)
{
	fe_delivery_system_t type = 0;
	uint32_t adap;
	switch (service->media.type) {
		case serviceMediaDVBT: type = SYS_DVBT; break;
		case serviceMediaDVBC: type = SYS_DVBC_ANNEX_AC; break;
		case serviceMediaDVBS: type = SYS_DVBS; break;
		case serviceMediaATSC: type = SYS_ATSC; break;
		default: return -1;
	}
	for(adap = 0; adap < MAX_ADAPTER_SUPPORTED; adap++) {
		if(dvbfe_checkDelSysSupport(adap, type)) {
			if(adapter) {
				*adapter = adap;
			}
			return 0;
		}
	}
	return -1;
}

int offair_checkForUpdates()
{
	return 0;
}

/****************************** WIZARD *******************************/

static int wizard_infoTimerEvent(void *pArg)
{
	//dprintf("%s: update display\n", __FUNCTION__);
	if (wizardSettings != NULL)
	{
		interface_displayMenu(1);
		interface_addEvent(wizard_infoTimerEvent, NULL, WIZARD_UPDATE_INTERVAL, 1);
	}

	return 0;
}

static int wizard_checkAbort(uint32_t frequency, int channelCount, uint32_t adapter, int frequencyNumber, int frequencyMax)
{
	interfaceCommand_t cmd;

	dprintf("%s: in\n", __FUNCTION__);

	while ((cmd = helperGetEvent(0)) != interfaceCommandNone)
	{
		//dprintf("%s: got command %d\n", __FUNCTION__, cmd);
		if (cmd == interfaceCommandExit && wizardSettings->allowExit != 0)
		{
			wizardSettings->state = wizardStateDisabled;
			return -1;
		}
		if (wizardSettings->state == wizardStateInitialFrequencyScan)
		{
			if (cmd == interfaceCommandGreen || cmd == interfaceCommandEnter || cmd == interfaceCommandOk || cmd == interfaceCommandRed || cmd == interfaceCommandBack)
			{
				if (cmd == interfaceCommandRed || cmd == interfaceCommandBack)
				{
					wizardSettings->state = wizardStateConfirmLocation;
				}
				goto aborted;
			}
		} else if (wizardSettings->state == wizardStateInitialServiceScan)
		{
			if (cmd == interfaceCommandRed || cmd == interfaceCommandBack)
			{
				wizardSettings->state = wizardStateConfirmLocation;
				goto aborted;
			} else if (cmd == interfaceCommandGreen || cmd == interfaceCommandEnter || cmd == interfaceCommandOk)
			{
				goto aborted;
			}
		} else if (wizardSettings->state == wizardStateInitialFrequencyMonitor)
		{
			if (cmd == interfaceCommandGreen || cmd == interfaceCommandEnter || cmd == interfaceCommandOk || cmd == interfaceCommandRed || cmd == interfaceCommandBack)
			{
				if (cmd == interfaceCommandRed || cmd == interfaceCommandBack)
				{
					wizardSettings->state = wizardStateConfirmLocation;
				} else
				{
					wizardSettings->state = wizardStateInitialServiceScan;
				}
				goto aborted;
			}
		} else if (wizardSettings->state == wizardStateCustomFrequencyMonitor)
		{
			if (cmd == interfaceCommandGreen || cmd == interfaceCommandEnter || cmd == interfaceCommandOk || cmd == interfaceCommandRed || cmd == interfaceCommandBack)
			{
				goto aborted;
			}
		} else if (wizardSettings->state == wizardStateConfirmUpdate)
		{
			if (cmd == interfaceCommandGreen || cmd == interfaceCommandEnter || cmd == interfaceCommandOk || cmd == interfaceCommandRed || cmd == interfaceCommandBack)
			{
				if (cmd == interfaceCommandGreen || cmd == interfaceCommandEnter || cmd == interfaceCommandOk)
				{
					wizardSettings->state = wizardStateUpdating;
				}
				goto aborted;
			}
		}
	}

	return keepCommandLoopAlive ? 0 : -1;
aborted:
	//dprintf("%s: exit on command %d\n", __FUNCTION__, cmd);
	/* Flush any waiting events */
	helperGetEvent(1);
	return -1;
}

static int wizard_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{

	dprintf("%s: in\n", __FUNCTION__);

	if (cmd->command == interfaceCommandExit && wizardSettings->allowExit != 0)
	{
		wizardSettings->state = wizardStateDisabled;
		wizard_cleanup(0);
		return 1;
	} else if (wizardSettings->state == wizardStateCustomFrequencySelect)
	{
		if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
		{
			wizardSettings->state = wizardStateCustomFrequencyMonitor;

			//dprintf("%s: selected %d = %lu\n", __FUNCTION__, pMenu->selectedItem, (unsigned long)pMenu->menuEntry[pMenu->selectedItem].pArg);

			wizardSettings->frequencyIndex = pMenu->selectedItem;

			wizard_infoTimerEvent(NULL);
			dvb_frequencyScan(appControlInfo.dvbInfo.adapter, (unsigned long)pMenu->menuEntry[pMenu->selectedItem].pArg, NULL,
								wizard_checkAbort, -1, (dvbfe_cancelFunctionDef*)wizard_checkAbort);
			wizard_cleanup(-1);
		} else if (cmd->command == interfaceCommandUp ||
			cmd->command == interfaceCommandDown)
		{
			interface_listMenuProcessCommand(pMenu, cmd);
		} else if (cmd->command == interfaceCommandBack ||
			cmd->command == interfaceCommandRed)
		{
			wizardSettings->state = wizardStateDisabled;
			wizard_cleanup(-1);
		}

		return 1;
	} else if (wizardSettings->state == wizardStateSelectLocation)
	{
		if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
		{
			int i;

			if (pMenu->selectedItem < pMenu->menuEntryCount)
			{
				strcpy(appControlInfo.offairInfo.profileLocation, pMenu->menuEntry[pMenu->selectedItem].pArg);
			}

			wizardSettings->state = wizardStateConfirmLocation;
			if (wizardSettings->locationFiles != NULL)
			{
				for (i=0; i<wizardSettings->locationCount; i++)
				{
					//dprintf("%s: free %s\n", __FUNCTION__, wizardSettings->locationFiles[i]->d_name);
					free(wizardSettings->locationFiles[i]);
				}
				free(wizardSettings->locationFiles);
				wizardSettings->locationFiles = NULL;
			}
		} else if (cmd->command == interfaceCommandUp ||
			cmd->command == interfaceCommandDown)
		{
			interface_listMenuProcessCommand(pMenu, cmd);
		} else if (cmd->command == interfaceCommandBack ||
			cmd->command == interfaceCommandRed)
		{
			wizardSettings->state = wizardStateConfirmLocation;
		}
	} else if (wizardSettings->state == wizardStateConfirmManualScan)
	{
		if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
		{
			output_showDVBMenu(pMenu, NULL);
			wizard_cleanup(1);
		} else if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandBack)
		{
			wizardSettings->state = wizardStateConfirmLocation;
		}
	} else if (wizardSettings->state == wizardStateConfirmLocation)
	{
		if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
		{
			char *str;
			char buffer[2048];
			int res;

			//dprintf("%s: goto scan\n", __FUNCTION__);

			sprintf(buffer, PROFILE_LOCATIONS_PATH "/%s", appControlInfo.offairInfo.profileLocation);
			if (getParam(buffer, "FREQUENCIES", NULL, buffer))
			{
				unsigned long minfreq, minindex;

				//dprintf("%s: do scan '%s'\n", __FUNCTION__, buffer);

				wizardSettings->frequencyCount = wizardSettings->frequencyIndex = 0;

				/* Get list of frequencies */
				str = strtok(buffer, ",");
				while (str != NULL && wizardSettings->frequencyCount < 64)
				{
					unsigned long freqval = strtoul(str, NULL, 10);
					if (freqval < 1000)
					{
						freqval *= 1000000;
					} else if (freqval < 1000000)
					{
						freqval *= 1000;
					}
					//dprintf("%s: got freq %lu\n", __FUNCTION__, freqval);
					wizardSettings->frequency[wizardSettings->frequencyCount++] = freqval;
					str = strtok(NULL, ",");
				}

				wizardSettings->state = wizardStateInitialFrequencyScan;

				wizard_infoTimerEvent(NULL);

				//dprintf("%s: find any working freq\n", __FUNCTION__);

				/* Try to tune to any of specified frequencies. If none succeeded - use lowest */
				minfreq = wizardSettings->frequency[0];
				minindex = 0;
				for (wizardSettings->frequencyIndex=0; wizardSettings->frequencyIndex < wizardSettings->frequencyCount; wizardSettings->frequencyIndex++)
				{
					//dprintf("%s: tune to %lu\n", __FUNCTION__, wizardSettings->frequency[wizardSettings->frequencyIndex]);
					interface_displayMenu(1);
					if (minfreq > wizardSettings->frequency[wizardSettings->frequencyIndex])
					{
						minfreq = wizardSettings->frequency[wizardSettings->frequencyIndex];
						minindex = wizardSettings->frequencyIndex;
					}
					if((res = dvb_frequencyScan(appControlInfo.dvbInfo.adapter, wizardSettings->frequency[wizardSettings->frequencyIndex], NULL, NULL, -2, (dvbfe_cancelFunctionDef*)wizard_checkAbort)) == 1)
					{
						//dprintf("%s: found smth on %lu\n", __FUNCTION__, wizardSettings->frequency[wizardSettings->frequencyIndex]);
						minfreq = wizardSettings->frequency[wizardSettings->frequencyIndex];
						minindex = wizardSettings->frequencyIndex;
						break;
					}
					if (res == -1)
					{
						//dprintf("%s: scan abort\n", __FUNCTION__);
						break;
					}
					if (wizard_checkAbort(wizardSettings->frequency[wizardSettings->frequencyIndex], 0, appControlInfo.dvbInfo.adapter, wizardSettings->frequencyIndex, wizardSettings->frequencyCount) == -1)
					{
						//dprintf("%s: user abort\n", __FUNCTION__);
						break;
					}
				}

				/* If user did not abort our scan - proceed to monitoring and antenna adjustment */
				if (wizardSettings->state == wizardStateInitialFrequencyScan)
				{
					int foundAll = 1;

					//dprintf("%s: monitor\n", __FUNCTION__);

					wizardSettings->frequencyIndex = minindex;

					wizardSettings->state = wizardStateInitialFrequencyMonitor;

					interface_displayMenu(1);

					/* Stay in infinite loop until user takes action */
					res = dvb_frequencyScan(appControlInfo.dvbInfo.adapter, wizardSettings->frequency[wizardSettings->frequencyIndex], NULL, wizard_checkAbort, -1, (dvbfe_cancelFunctionDef*)wizard_checkAbort);

					//dprintf("%s: done monitoring!\n", __FUNCTION__);

					if (wizardSettings->state == wizardStateInitialServiceScan)
					{
						int lastServiceCount, currentServiceCount;
						//dprintf("%s: scan services\n", __FUNCTION__);

						offair_clearServiceList(0);

						lastServiceCount = dvb_getNumberOfServices();

						/* Go through all frequencies and scan for services */
						for (wizardSettings->frequencyIndex=0; wizardSettings->frequencyIndex < wizardSettings->frequencyCount; wizardSettings->frequencyIndex++)
						{
							//dprintf("%s: scan %lu\n", __FUNCTION__, wizardSettings->frequency[wizardSettings->frequencyIndex]);
							interface_displayMenu(1);
							res = dvb_frequencyScan(appControlInfo.dvbInfo.adapter, wizardSettings->frequency[wizardSettings->frequencyIndex], NULL, NULL, 0, (dvbfe_cancelFunctionDef*)wizard_checkAbort);
							if(res == -1) {
								//dprintf("%s: scan abort\n", __FUNCTION__);
								break;
							}
							if (wizard_checkAbort(wizardSettings->frequency[wizardSettings->frequencyIndex], dvb_getNumberOfServices(), appControlInfo.dvbInfo.adapter, wizardSettings->frequencyIndex, wizardSettings->frequencyCount) == -1)
							{
								//dprintf("%s: user abort service scan\n", __FUNCTION__);
								break;
							}

							if (offair_checkForUpdates())
							{
								wizardSettings->state = wizardStateConfirmUpdate;

								interface_displayMenu(1);

								while (wizard_checkAbort(0,0,0,0,0) != -1)
								{
									sleepMilli(500);
								}

								if (wizardSettings->state == wizardStateUpdating)
								{
									// TODO: update code here, save location and reboot
									sleep(3);
									system("reboot");
									break;
								}

								wizardSettings->state = wizardStateInitialServiceScan;
								interface_displayMenu(1);
							}

							currentServiceCount = dvb_getNumberOfServices();

							if (currentServiceCount <= lastServiceCount)
							{
								foundAll = 0;
							}
							lastServiceCount = currentServiceCount;
						}

						//dprintf("%s: done scanning %d!\n", __FUNCTION__, dvb_getNumberOfServices());

						/* If user didn't cancel our operation, we now can display channel list */
						if (wizardSettings->state == wizardStateInitialServiceScan)
						{
							int tmp;

							interface_removeEvent(wizard_infoTimerEvent, NULL);

							wizard_cleanup(1);

							tmp = appControlInfo.playbackInfo.bAutoPlay;
							appControlInfo.playbackInfo.bAutoPlay = 0;
							interface_menuActionShowMenu(pMenu, (void*)&DVBTMenu);
							appControlInfo.playbackInfo.bAutoPlay = tmp;

							if (foundAll == 0)
							{
								interface_showMessageBox(_T("SETTINGS_WIZARD_NOT_ALL_CHANNELS_FOUND"), thumbnail_info, 10000);
							}

							return 1;
						}
					}
				}

				interface_removeEvent(wizard_infoTimerEvent, NULL);

				//dprintf("%s: done with scanning\n", __FUNCTION__);

				if (wizardSettings->state == wizardStateDisabled)
				{
					wizard_cleanup(0);
				}
			} else
			{
				wizardSettings->state = wizardStateConfirmManualScan;
			}

		} else if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandBack)
		{
			char buffer[2048];
			int res, i, found;

			//dprintf("%s: goto location list\n", __FUNCTION__);

			found = 0;

			interface_clearMenuEntries((interfaceMenu_t*)&wizardHelperMenu);

			/* Fill menu with locations */
			res = scandir(PROFILE_LOCATIONS_PATH, &wizardSettings->locationFiles, NULL, alphasort);

			if (res > 0)
			{
				for (i=0;i<res; i++)
				{
					/* Skip if we have found suitable file or file is a directory */
					if ((wizardSettings->locationFiles[i]->d_type & DT_DIR))
					{
						continue;
					}

					//dprintf("Test location %s\n", wizardSettings->locationFiles[i]->d_name);

					sprintf(buffer, PROFILE_LOCATIONS_PATH "/%s", wizardSettings->locationFiles[i]->d_name);
					if (getParam(buffer, "LOCATION_NAME", NULL, buffer))
					{
						interface_addMenuEntry((interfaceMenu_t*)&wizardHelperMenu, buffer, NULL, wizardSettings->locationFiles[i]->d_name, thumbnail_info);
						found = 1;
					}
				}

				if (found)
				{
					wizardSettings->locationCount = res;
					wizardSettings->state = wizardStateSelectLocation;
				} else
				{
					if (wizardSettings->locationFiles != NULL)
					{
						for (i=0; i<res; i++)
						{
							//dprintf("free %s\n", wizardSettings->locationFiles[i]->d_name);
							free(wizardSettings->locationFiles[i]);
						}
						free(wizardSettings->locationFiles);
						wizardSettings->locationFiles = NULL;
					}
				}
			} else
			{
				// show error
			}
		}
	}
	interface_displayMenu(1);
	return 1;
}

static int wizard_sliderCallback(int id, interfaceCustomSlider_t *info, void *pArg)
{
	if (id < 0 || info == NULL)
	{
		return 1;
	}

	//dprintf("%s: get info 0x%08X\n", __FUNCTION__, info);

	switch (id)
	{
	case 0:
		sprintf(info->caption, _T("SETTINGS_WIZARD_SCANNING_SERVICES_SLIDER"), dvb_getNumberOfServices());
		info->min = 0;
		info->max = wizardSettings->frequencyCount;
		info->value = wizardSettings->frequencyIndex;
		break;
	default:
		return -1;
	}

	return 1;
}

static void wizard_displayCallback(interfaceMenu_t *pMenu)
{
	char *str, *helpstr, *infostr;
	char buffer[MAX_MESSAGE_BOX_LENGTH];
	char buffer2[MAX_MESSAGE_BOX_LENGTH];
	char cmd[1024];
	int  icons[4] = { statusbar_f1_cancel, statusbar_f2_ok, 0, 0 };
	int fh, fa;
	int x,y,w,h,pos, centered;

	//dprintf("%s: in\n", __FUNCTION__);

	/*sprintf(buffer, "%d", wizardSettings->state);
	gfx_drawText(DRAWING_SURFACE, pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, interfaceInfo.clientX+interfaceInfo.paddingSize*2, interfaceInfo.clientY-interfaceInfo.paddingSize*2, buffer, 0, 0);*/

	x = interfaceInfo.clientX;
	y = interfaceInfo.clientY;
	w = interfaceInfo.clientWidth;
	h = interfaceInfo.clientHeight;

	if (wizardSettings->state != wizardStateSelectLocation &&
		wizardSettings->state != wizardStateCustomFrequencySelect &&
		wizardSettings->state != wizardStateSelectChannel)
	{
		interface_drawRoundBox(x, y, w, h);
	}

	pgfx_font->GetHeight(pgfx_font, &fh);
	pgfx_font->GetHeight(pgfx_font, &fa);

	switch (wizardSettings->state)
	{
	case wizardStateSelectLocation:
	case wizardStateCustomFrequencySelect:
	case wizardStateSelectChannel:
		{
			interface_listMenuDisplay(pMenu);
		} break;
	default:
		{
			customSliderFunction showSlider = offair_sliderCallback;
			infostr = NULL;
			centered = ALIGN_LEFT;

			if (wizardSettings->state == wizardStateInitialFrequencyScan)
			{
				helpstr = _T("SETTINGS_WIZARD_SCANNING_FREQUENCY_HINT");

				str = buffer;
				sprintf(buffer, _T("SETTINGS_WIZARD_SCANNING_FREQUENCY"), wizardSettings->frequency[wizardSettings->frequencyIndex]/MHZ);
			} else if (wizardSettings->state == wizardStateInitialFrequencyMonitor ||
				wizardSettings->state == wizardStateCustomFrequencyMonitor)
			{
				static time_t lastUpdate = 0;
				static int rating = 5;
				char tindex[] = "SETTINGS_WIZARD_DIAG_SIGNAL_0";

				if (time(NULL)-lastUpdate > 1)
				{
					uint16_t snr, signal;
					uint32_t ber, uncorrected_blocks;
					fe_status_t status;

					rating = 5;
					lastUpdate = time(NULL);

					status = dvbfe_getSignalInfo(appControlInfo.dvbInfo.adapter, &snr, &signal, &ber, &uncorrected_blocks);

					if (status == 0)
					{
						rating = min(rating, 1);
					}

					if (uncorrected_blocks > BAD_UNC)
					{
						rating = min(rating, 2);
					}

					if (ber > BAD_BER)
					{
						rating = min(rating, 3);
					}

					if (signal < AVG_SIGNAL)
					{
						rating = min(rating, 4);
					}

					if (signal < BAD_SIGNAL)
					{
						rating = min(rating, 3);
					}
				}

				tindex[strlen(tindex)-1] += rating;

				sprintf(cmd, PROFILE_LOCATIONS_PATH "/%s", appControlInfo.offairInfo.profileLocation);
				getParam(cmd, "TRANSPONDER_LOCATION", _T("SETTINGS_WIZARD_NO_TRANSPONDER_LOCATION"), cmd);

				helpstr = buffer2;
				sprintf(buffer2, _T(tindex), cmd);

				if (wizardSettings->state == wizardStateCustomFrequencyMonitor)
				{
					icons[0] = 0;
					icons[1] = 0;
				} else if (rating >= 3)
				{
					strcat(buffer2, " ");
					strcat(buffer2, _T("SETTINGS_WIZARD_DIAG_SIGNAL_DO_SCAN"));
				}

				str = buffer;
				sprintf(buffer, _T("SETTINGS_WIZARD_MONITORING_FREQUENCY"), wizardSettings->frequency[wizardSettings->frequencyIndex]/MHZ);
			} else if (wizardSettings->state == wizardStateInitialServiceScan)
			{
				helpstr = _T("SETTINGS_WIZARD_SCANNING_SERVICES_HINT");

				showSlider = wizard_sliderCallback;

				str = buffer;
				sprintf(buffer, _T("SETTINGS_WIZARD_SCANNING_SERVICES"), wizardSettings->frequency[wizardSettings->frequencyIndex]/MHZ);
			} else if (wizardSettings->state == wizardStateConfirmUpdate)
			{
				showSlider = NULL;

				helpstr = _T("SETTINGS_WIZARD_CONFIRM_UPDATE_HINT");

				str = _T("SETTINGS_WIZARD_CONFIRM_UPDATE");
			} else if (wizardSettings->state == wizardStateConfirmManualScan)
			{
				showSlider = NULL;

				helpstr = _T("SETTINGS_WIZARD_NO_FREQUENCIES_HINT");

				// TODO: add version display, etc

				str = _T("SETTINGS_WIZARD_NO_FREQUENCIES");
			} else
			{
				showSlider = NULL;

				helpstr = _T("SETTINGS_WIZARD_CONFIRM_LOCATION_HINT");

				sprintf(cmd, PROFILE_LOCATIONS_PATH "/%s", appControlInfo.offairInfo.profileLocation);
				if (getParam(cmd, "LOCATION_NAME", NULL, buffer))
				{
					infostr = buffer;
				} else
				{
					infostr = _T("SETTINGS_WIZARD_NO_LOCATIONS");
				}
				//interface_displayCustomTextBoxColor(interfaceInfo.clientX+interfaceInfo.clientWidth/2, interfaceInfo.clientY+interfaceInfo.clientHeight/2-fh*2, str, NULL, 0, &rect, 0, NULL, 0,0,0,0, 0xFF,0xFF,0xFF,0xFF, pgfx_font);

				str = _T("SETTINGS_WIZARD_CONFIRM_LOCATION");
				centered = ALIGN_CENTER;
			}

			pos = y;
			pos += interface_drawTextWW(pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, x, pos, w, h, str, centered)+interfaceInfo.paddingSize;
			if (showSlider != NULL)
			{
				if (pos < y+fh*3+interfaceInfo.paddingSize)
				{
					pos = y+fh*3+interfaceInfo.paddingSize;
				}
				pos += interface_displayCustomSlider(showSlider, NULL, 1, interfaceInfo.screenWidth/2-(interfaceInfo.clientWidth)/2, pos, interfaceInfo.clientWidth, pgfx_smallfont)+interfaceInfo.paddingSize;
			} else if (infostr != NULL)
			{
				if (pos < y+fh*4+interfaceInfo.paddingSize)
				{
					pos = y+fh*4+interfaceInfo.paddingSize;
				}
				pos += interface_drawTextWW(pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, x, pos, w, h, infostr, ALIGN_CENTER)+interfaceInfo.paddingSize;
			}
			if (pos < y+fh*10+interfaceInfo.paddingSize)
			{
				pos = y+fh*10+interfaceInfo.paddingSize;
			}
			pos += interface_drawTextWW(pgfx_smallfont, 0xFF, 0xFF, 0xFF, 0xFF, x, pos, w, h, helpstr, ALIGN_LEFT)+interfaceInfo.paddingSize;
			if (pos < y+h-INTERFACE_STATUSBAR_ICON_HEIGHT)
			{
				int i, n, icons_w;

				n = 0;
				for( i = 0; i < 4; i++)
				{
					if( icons[i] > 0)
					{
						n++;
					}
				}
				icons_w = n * INTERFACE_STATUSBAR_ICON_WIDTH + (n-1)*3*interfaceInfo.paddingSize;

				x = x+(w - icons_w) / 2;
				y = y+h-INTERFACE_STATUSBAR_ICON_HEIGHT;
				for( i = 0; i < 4; i++)
				{
					if( icons[i] > 0)
					{
						interface_drawImage(DRAWING_SURFACE, resource_thumbnails[icons[i]], x, y, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignTop, 0, 0);
						x += INTERFACE_STATUSBAR_ICON_WIDTH + interfaceInfo.paddingSize * 3;
					}
				}
			}
			//interface_displayCustomTextBoxColor(interfaceInfo.clientX+interfaceInfo.clientWidth/2, interfaceInfo.clientY+fh*2+fh/2, str, NULL, 0, &rect, 0, NULL, 0,0,0,0, 0xFF,0xFF,0xFF,0xFF, pgfx_font);
			//interface_displayCustomTextBoxColor(interfaceInfo.clientX+interfaceInfo.clientWidth/2, interfaceInfo.clientY+interfaceInfo.clientHeight-fh*6+fh/2, helpstr, NULL, 0, &rect, 0, icons, 0,0,0,0, 0xFF,0xFF,0xFF,0xFF, pgfx_smallfont);



		} break;
	}
}

static int wizard_show(int allowExit, int displayMenu, interfaceMenu_t *pFallbackMenu, unsigned long monitor_only_frequency)
{
	char cmd[1024];

	dprintf("%s: %lu\n", __FUNCTION__, monitor_only_frequency);

	sprintf(cmd, PROFILE_LOCATIONS_PATH "/%s", appControlInfo.offairInfo.profileLocation);
	if (monitor_only_frequency == 0 && !getParam(cmd, "LOCATION_NAME", NULL, NULL))
	{
		return 0;
	}

	gfx_stopVideoProviders(screenMain);

	interfaceInfo.enableClock = 0;
	appControlInfo.playbackInfo.streamSource = streamSourceNone;
	appControlInfo.dvbInfo.adapter = offair_getTuner();
	wizardHelperMenu.baseMenu.selectedItem = 0;

	wizardSettings = dmalloc(sizeof(wizardSettings_t));
	memset(wizardSettings, 0, sizeof(wizardSettings_t));

	wizardSettings->pFallbackMenu = pFallbackMenu;
	wizardSettings->allowExit = allowExit;
	wizardSettings->state = monitor_only_frequency > 0 ? (monitor_only_frequency == (unsigned long)-1 ? wizardStateCustomFrequencySelect : wizardStateCustomFrequencyMonitor) : wizardStateConfirmLocation;
	wizardSettings->frequency[wizardSettings->frequencyIndex] = monitor_only_frequency;

	if (wizardSettings->state == wizardStateCustomFrequencySelect)
	{
		list_element_t *cur;
		EIT_service_t *service;
		unsigned long freq, i, found;

		interface_clearMenuEntries((interfaceMenu_t*)&wizardHelperMenu);

		wizardSettings->frequencyCount = 0;

		cur = dvb_services;
		while (cur != NULL)
		{
			service = (EIT_service_t*)cur->data;
			freq = service->media.frequency;

			found = 0;
			for (i=0; i<wizardSettings->frequencyCount; i++)
			{
				if (freq == wizardSettings->frequency[i])
				{
					found = 1;
					break;
				}
				if (freq < wizardSettings->frequency[i])
				{
					break;
				}
			}

			if (!found)
			{
				wizardSettings->frequencyCount++;
				for (; i<wizardSettings->frequencyCount; i++)
				{
					found = wizardSettings->frequency[i];
					wizardSettings->frequency[i] = freq;
					freq = found;
				}
			}

			cur = cur->next;
		}

		for (i=0; i<wizardSettings->frequencyCount; i++)
		{
			sprintf(cmd, "%lu %s", wizardSettings->frequency[i]/MHZ, _T("MHZ"));
			interface_addMenuEntry((interfaceMenu_t*)&wizardHelperMenu, cmd, NULL, (void*)wizardSettings->frequency[i], thumbnail_info);
		}
	}

	interfaceInfo.currentMenu = (interfaceMenu_t*)&wizardHelperMenu;

	interface_showMenu(1, displayMenu);
	interfaceInfo.lockMenu = 1;

	if((monitor_only_frequency > 0) && (monitor_only_frequency != (unsigned long)-1)) {
		wizard_infoTimerEvent(NULL);
		dvb_frequencyScan(appControlInfo.dvbInfo.adapter,
			monitor_only_frequency, NULL, wizard_checkAbort, -1, (dvbfe_cancelFunctionDef*)wizard_checkAbort);
		wizard_cleanup(-1);
	}

	return 1;
}

static void wizard_cleanup(int finished)
{
	int i;

	interface_removeEvent(wizard_infoTimerEvent, NULL);

	interfaceInfo.lockMenu = 0;
	interfaceInfo.enableClock = 1;

	if(finished == 1) {
		dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
		appControlInfo.offairInfo.wizardFinished = 1;
		saveAppSettings();
	} else if(finished == 0) {
		offair_clearServiceList(0);
	}

	if(finished != -1) {
		offair_fillDVBTMenu();
	}

	if(wizardSettings->locationFiles != NULL) {
		for(i=0; i<wizardSettings->locationCount; i++) {
			//dprintf("%s: free %s\n", __FUNCTION__, wizardSettings->locationFiles[i]->d_name);
			free(wizardSettings->locationFiles[i]);
		}
		free(wizardSettings->locationFiles);
		wizardSettings->locationFiles = NULL;
	}

	if (wizardSettings->pFallbackMenu != NULL)
	{
		interface_menuActionShowMenu((interfaceMenu_t*)&wizardHelperMenu, (void*)wizardSettings->pFallbackMenu);
	}

	dfree(wizardSettings);
	wizardSettings = NULL;
}

int wizard_init()
{
	createListMenu(&wizardHelperMenu, NULL, thumbnail_logo, NULL, NULL,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&wizardHelperMenu, wizard_keyCallback);
	interface_setCustomDisplayCallback((interfaceMenu_t*)&wizardHelperMenu, wizard_displayCallback);

	if (appControlInfo.offairInfo.wizardFinished == 0 || appControlInfo.offairInfo.profileLocation[0] == 0) // wizard
	{
		dprintf("%s: start init wizard\n", __FUNCTION__);
		if (wizard_show(0, 0, NULL, 0) == 0 && dvb_getNumberOfServices() == 0)
		{
			//interface_showMessageBox(_T("SETTINGS_WIZARD_NO_LOCATIONS"), thumbnail_warning, 5000);

#ifdef ENABLE_DVB_DIAG
			output_showDVBMenu(interfaceInfo.currentMenu, NULL);
#endif
		}
	}

	return 0;
}

#endif /* ENABLE_DVB */

int offair_getLocalEventTime(EIT_event_t *event, struct tm *p_tm, time_t *p_time)
{
	if (event == NULL)
		return -2;
	if (event->start_time == TIME_UNDEFINED)
		return 1;

	time_t local_time = event_start_time( event );
	if (p_time)
		*p_time = local_time;
	if (p_tm)
		localtime_r( &local_time, p_tm );
	return 0;
}

time_t offair_getEventDuration(EIT_event_t *event)
{
	if(event == NULL || event->duration == TIME_UNDEFINED)
		return 0;

	return decode_bcd_time(event->duration);
}

int offair_getEventTimes(EIT_event_t *event, time_t *p_start, time_t *p_end)
{
	if (offair_getLocalEventTime(event, NULL, p_start))
		return -1;
	*p_end = *p_start + offair_getEventDuration(event);
	return 0;
}

int offair_findCurrentEvent(list_element_t *schedule, time_t now,
                            EIT_event_t **pevent, time_t *event_start, time_t *event_length, struct tm *start)
{
	/* Find current programme */
	EIT_event_t *event = NULL;

	*pevent = NULL;
	for (list_element_t *element = schedule; element != NULL; element = element->next)
	{
		event = element->data;
		offair_getLocalEventTime( event, start, event_start );
		if ( *event_start > now )
			return 1;
		*event_length = offair_getEventDuration( event );
		if ( *event_start+*event_length >= now ) {
			*pevent = event;
			return 0;
		}
	}
	return 1;
}
