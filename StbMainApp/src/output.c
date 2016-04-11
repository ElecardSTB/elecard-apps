
/*
 output.c

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

#include "output.h"

#include "output_network.h"
#include "debug.h"
#include "dvbChannel.h"
#include "l10n.h"
#include "StbMainApp.h"
#include "helper.h"
#include "sem.h"
#include "gfx.h"
#include "backend.h"
#include "menu_app.h"
#include "off_air.h"
#include "sound.h"
#include "stats.h"
#include "stsdk.h"
#include "media.h"
#include "playlist_editor.h"

#include "dvb.h"
#include "analogtv.h"
#include "pvr.h"
#include "md5.h"
#include "voip.h"
#include "messages.h"
#include "Stb225.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#include <fcntl.h>

#include <directfb.h>
#include <directfb_strings.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/
#define COLOR_STEP_COUNT            (64)
#define COLOR_STEP_SIZE             (0x10000/COLOR_STEP_COUNT)

#ifdef STSDK
# define TIMEZONE_FILE               "/var/etc/localtime"
# define NTP_CONFIG_FILE             "/var/etc/ntpd"
#endif //STSDK

#define FORMAT_CHANGE_TIMEOUT       (15)

#define DVB_MIN_SYMBOLRATE               100
#define DVB_MAX_SYMBOLRATE             60000
#define DVB_MIN_FREQUENCY_C            47000
#define DVB_MAX_FREQUENCY_C         10020000
#define DVB_MIN_FREQUENCY_T           174000
#define DVB_MAX_FREQUENCY_T           862000
#define DVB_MIN_FREQUENCY_C_BAND        3400
#define DVB_MAX_FREQUENCY_C_BAND        4800
#define DVB_MIN_FREQUENCY_K_BAND       11000
#define DVB_MAX_FREQUENCY_K_BAND       18000
#define DVB_MIN_FREQUENCY_STEP          1000
#define DVB_MAX_FREQUENCY_STEP          8000

#define ATV_MIN_FREQUENCY		43000
#define ATV_MAX_FREQUENCY		960000

//fake curentmeter functions:
#define currentmeter_isExist()					0
#define currentmeter_getCalibrateHighValue()	0
#define currentmeter_getCalibrateLowValue()		0
#define currentmeter_getValue(...)				0
#define currentmeter_setCalibrateHighValue(...)
#define currentmeter_setCalibrateLowValue(...)

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
typedef enum
{
	optionLowFreq    = 0,
	optionHighFreq,
	optionFreqStep,
	optionSymbolRate,
} outputDvbOption;

typedef struct {
	char inputName[20];
	int deviceSelect;
} inputMode_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

// Please follow this rules when naming and implementing functions:
// 1. Menus
//   output_enter<Name>Menu - Should be a pActivatedAction, specified in createListMenu(<Name>Menu), clears and re-fills menu with entries.
//                            This functions implementation implies that pMenu is always <Name>Menu, and not any other menu.
//                            Menu created with this callback can be refilled using output_refillMenu(pMenu) from entry handlers, so
//                            this function shouldn't be called manually.
//                            Use this callback for menus with dynamical content, which should be updated each time user visits this menu.
//   output_leave<Name>Menu - Should be a pDeactivatedAction, specified in createListMenu(<Name>Menu). pMenu is assumed to be &<Name>Menu.
//   output_fill<Name>Menu  - Clears and refills <Name>Menu. Should be called manually.
//                            Use it for menus which seldom updates it's contents.
// 2. Entries
//   output_show<Name>Menu  - Should be pAction, specified in addMenuEntry, and call menuActionShowMenu(pMenu, &<Name>Menu).
//                            Use to change value using submenu with possible values.
//   output_toggle<Option>  - Change <Option> value in-place and redraw menu. Use for boolean and enumeration values.
//   output_change<Option>  - Change <Option> value using interface_getText. Use for string and numeric values with this callbacks:
//      output_set<Option>  - Text box input callback function
//      output_get<Option>  - Text box field callback function
//   output_confirm<Action> - Use for confirmation message box callbacks

static void output_fillOutputMenu(void);
static int output_enterVideoMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterGraphicsModeMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_enterTimeMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterInterfaceMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterPlaybackMenu(interfaceMenu_t *pMenu, void* notused);

#ifdef ENABLE_ANALOGTV
static int output_enterAnalogTvMenu(interfaceMenu_t *pMenu, void* notused);
#endif

#ifdef STB82
static int output_toggleInterfaceAnimation(interfaceMenu_t* pMenu, void* pArg);
#endif
static int output_toggleResumeAfterStart(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleAutoPlay(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleFileSorting(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleHighlightColor(interfaceMenu_t* pMenu, void* pArg);
static int output_togglePlayControlTimeout(interfaceMenu_t* pMenu, void* pArg);
static int output_toggleAutoStopTimeout(interfaceMenu_t *pMenu, void* pArg);
static int output_togglePlaybackMode(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleVolumeFadein(interfaceMenu_t *pMenu, void* pArg);
static int output_togglePlayControlShowOnStart(interfaceMenu_t* pMenu, void* pArg);
#ifdef ENABLE_VOIP
static int output_toggleVoipIndication(interfaceMenu_t* pMenu, void* pArg);
static int output_toggleVoipBuzzer(interfaceMenu_t* pMenu, void* pArg);
#endif
#ifdef ENABLE_DVB
static int output_enterDVBMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterDiseqcMenu(interfaceMenu_t *pMenu, void* notused);
static int output_toggleDvbShowScrambled(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbBandwidth(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbPolarization(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbType(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbTuner(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDiseqcSwitch(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDiseqcPort(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDiseqcUncommited(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleSortOrder(interfaceMenu_t *pMenu, void* pArg);
static int output_clearDvbSettings(interfaceMenu_t *pMenu, void* pArg);
static int output_confirmClearDvb(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#ifdef ENABLE_DVB_START_CHANNEL
static int output_enterDvbStartMenu(interfaceMenu_t *startMenu, void *notused);
static int output_toggleDvbStartChannel(interfaceMenu_t *pMenu, void *pChannelNumber);
#endif // ENABLE_DVB_START_CHANNEL
#endif // ENABLE_DVB

#ifdef ENABLE_3D
static interfaceListMenu_t Video3DSubMenu;
static int output_enter3DMenu(interfaceMenu_t *pMenu, void* pArg);
#endif // #ifdef STB225

static void output_colorSliderUpdate(void *pArg);

#ifdef ENABLE_PASSWORD
static int output_askPassword(interfaceMenu_t *pMenu, void* pArg);
static int output_enterPassword(interfaceMenu_t *pMenu, char *value, void* pArg);
static int output_showNetworkMenu(interfaceMenu_t *pMenu, void* pArg);
#endif
static void output_fillTimeZoneMenu(void);
#if (defined STB6x8x) || (defined STB225)
static void output_fillStandardMenu(void);
#endif
#ifdef STB82
static void output_fillFormatMenu(void);
static void output_fillBlankingMenu(void);
#endif

static int output_enterCalibrateMenu(interfaceMenu_t *pMenu, void * pArg);
static int output_calibrateCurrentMeter(interfaceMenu_t *pMenu, void* pArg);

#ifdef STSDK

static int output_enterUpdateMenu(interfaceMenu_t *pMenu, void* notused);
static int output_toggleAdvancedVideoOutput(interfaceMenu_t *pMenu, void* pArg);

#endif


/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

#ifdef ENABLE_DVB
static interfaceListMenu_t DVBSubMenu;
static interfaceListMenu_t DiSEqCMenu;
#ifdef ENABLE_DVB_START_CHANNEL
static interfaceListMenu_t DvbStartMenu;
#endif // ENABLE_DVB_START_CHANNEL

static const char *diseqc_switch_names[] = DISEQC_SWITCH_NAMES;
#endif
#ifndef STSDK
static interfaceListMenu_t StandardMenu;
static interfaceListMenu_t FormatMenu;
static interfaceListMenu_t BlankingMenu;
#endif
static interfaceListMenu_t GraphicsModeMenu;
static interfaceListMenu_t TimeZoneMenu;
static interfaceListMenu_t InterfaceMenu;
static interfaceListMenu_t PlaybackMenu;
static interfaceListMenu_t InputsSubMenu;
#ifdef ENABLE_ANALOGTV
static interfaceListMenu_t AnalogTvSubMenu;
#endif

static interfaceListMenu_t CurrentmeterSubMenu;
static interfaceListMenu_t VideoSubMenu;
static interfaceListMenu_t TimeSubMenu;

static interfaceEditEntry_t TimeEntry;
static interfaceEditEntry_t DateEntry;

static long info_progress;

#ifdef STSDK
static interfaceListMenu_t UpdateMenu;

videoOutput_t	*p_mainVideoOutput = NULL;
static char		previousFormat[64]; //this needs for cancel switching video output format
static uint32_t	isToggleContinues = 0; //this indicates that "togle vfmt" button pressed several times without apply/cancaling format

#endif

#define GRAPHICS_MODE_COUNT 4
static struct {
	const char *name;
	const char *mode;
} output_graphicsModes[GRAPHICS_MODE_COUNT] = {
	{ "AUTOMATIC", "" },
	{ "LARGE",  "720x480" },
	{ "MEDIUM", "1280x720" },
	{ "SMALL",  "1920x1080" },
};

static table_IntStr_t inputDeviceNames[] = {
	{1, "DVD"},
	{2, "VCR"},
	{3, "GAME"},
	{4, "PC"},
	{5, "OTHER"},
	TABLE_INT_STR_END_VALUE
};

/**
 * @brief Useful DFB macros to have Strings and Values in an array.
 */
static const DirectFBScreenEncoderTVStandardsNames(tv_standards);
static const DirectFBScreenOutputSignalsNames(signals);
static const DirectFBScreenOutputResolutionNames(resolutions);

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

interfaceListMenu_t OutputMenu;

stbTimeZoneDesc_t timezones[] = {
	{"Russia/Kaliningrad", "(MSK-1) Калининградское время"},
	{"Russia/Moscow", "(MSK) Московское время"},
	{"Russia/Yekaterinburg", "(MSK+2) Екатеринбургское время"},
	{"Russia/Omsk", "(MSK+3) Омское время"},
	{"Russia/Krasnoyarsk", "(MSK+4) Красноярское время"},
	{"Russia/Irkutsk", "(MSK+5) Иркутское время"},
	{"Russia/Yakutsk", "(MSK+6) Якутское время"},
	{"Russia/Vladivostok", "(MSK+7) Владивостокское время"},
	{"Russia/Magadan", "(MSK+8) Магаданское время"},
	{"Etc/GMT+12", "(GMT -12:00) Eniwetok, Kwajalein"},
	{"Etc/GMT+11", "(GMT -11:00) Midway Island, Samoa"},
	{"Etc/GMT+10", "(GMT -10:00) Hawaii"},
	{"Etc/GMT+9", "(GMT -9:00) Alaska"},
	{"Etc/GMT+8", "(GMT -8:00) Pacific Time (US &amp; Canada)"},
	{"Etc/GMT+7", "(GMT -7:00) Mountain Time (US &amp; Canada)"},
	{"Etc/GMT+6", "(GMT -6:00) Central Time (US &amp; Canada), Mexico City"},
	{"Etc/GMT+5", "(GMT -5:00) Eastern Time (US &amp; Canada), Bogota, Lima"},
	{"Etc/GMT+4", "(GMT -4:00) Atlantic Time (Canada), La Paz, Santiago"},
	{"Etc/GMT+3", "(GMT -3:00) Brazil, Buenos Aires, Georgetown"},
	{"Etc/GMT+2", "(GMT -2:00) Mid-Atlantic"},
	{"Etc/GMT+1", "(GMT -1:00) Azores, Cape Verde Islands"},
	{"Etc/GMT-0", "(GMT) Western Europe Time, London, Lisbon, Casablanca"},
	{"Etc/GMT-1", "(GMT +1:00) Brussels, Copenhagen, Madrid, Paris"},
	{"Etc/GMT-2", "(GMT +2:00) Kaliningrad, South Africa"},
	{"Europe/Sofia", "(GMT +2:00) Bulgaria, Sofia"},
	{"Etc/GMT-3", "(GMT +3:00) Baghdad, Riyadh, Moscow, St. Petersburg"},
	{"Etc/GMT-4", "(GMT +4:00) Abu Dhabi, Muscat, Baku, Tbilisi"},
	{"Etc/GMT-5", "(GMT +5:00) Ekaterinburg, Islamabad, Karachi, Tashkent"},
	{"Etc/GMT-6", "(GMT +6:00) Almaty, Dhaka, Colombo"},
	{"Etc/GMT-7", "(GMT +7:00) Bangkok, Hanoi, Krasnoyarsk"},
	{"Etc/GMT-8", "(GMT +8:00) Beijing, Perth, Singapore, Hong Kong"},
	{"Etc/GMT-9", "(GMT +9:00) Tokyo, Seoul, Osaka, Sapporo, Yakutsk"},
	{"Etc/GMT-10", "(GMT +10:00) Eastern Australia, Guam, Vladivostok"},
	{"Etc/GMT-11", "(GMT +11:00) Magadan, Solomon Islands, New Caledonia"},
	{"Etc/GMT-12", "(GMT +12:00) Auckland, Wellington, Fiji, Kamchatka"}
};

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/
void output_redrawMenu(interfaceMenu_t *pMenu)
{
    output_refillMenu(pMenu);
    interface_displayMenu(1);
}

int output_warnIfFailed(int failed)
{
    static int bDisplayedWarning = 0;
    if (failed && !bDisplayedWarning)
    {
        bDisplayedWarning = 1;
        interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
        return failed;
    }
    return 0;
}

int output_saveAndRedraw(int saveFailed, interfaceMenu_t *pMenu)
{
    output_refillMenu(pMenu);
    if (output_warnIfFailed(saveFailed) == 0)
        interface_displayMenu(1);
    return 0;
}

/* -------------------------- OUTPUT SETTING --------------------------- */

#if (defined STB6x8x) || (defined STB225)
static int output_setStandard(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STB6x8x
	DFBScreenEncoderTVStandards tv_standard;
	tv_standard = GET_NUMBER(pArg);

	/* Check to see if the standard actually needs changing */
	/*if (tv_standard==appControlInfo.outputInfo.encConfig[0].tv_standard)
	{
		return 0;
	}*/

	appControlInfo.outputInfo.encConfig[0].tv_standard = tv_standard;
	appControlInfo.outputInfo.encConfig[0].flags = DSECONF_TV_STANDARD;

	/* What if we have 2 encoders like PNX8510 + TDA9982 */
	if(appControlInfo.outputInfo.numberOfEncoders == 2)
	{
		appControlInfo.outputInfo.encConfig[1].tv_standard = tv_standard;
		appControlInfo.outputInfo.encConfig[1].flags = DSECONF_TV_STANDARD;
	}

	switch(tv_standard)
	{
		case DSETV_NTSC:
#ifdef STBPNX
		case DSETV_NTSC_M_JPN:
#endif
			interface_setSelectedItem(pMenu, 0);
			system("/config.templates/scripts/dispmode ntsc");
			break;

		case DSETV_SECAM:
			interface_setSelectedItem(pMenu, 1);
			system("/config.templates/scripts/dispmode secam");
			break;

		case DSETV_PAL:
#ifdef STBPNX
		case DSETV_PAL_BG:
		case DSETV_PAL_I:
		case DSETV_PAL_N:
		case DSETV_PAL_NC:
#endif
			interface_setSelectedItem(pMenu, 2);
			system("/config.templates/scripts/dispmode pal");
			break;

#ifdef STB82
		case DSETV_PAL_60:
#endif //#ifdef STB82
#ifdef STBPNX
		case DSETV_PAL_M:
			interface_setSelectedItem(pMenu, 3);
			system("/config.templates/scripts/dispmode pal60");
			break;
		case DSETV_DIGITAL:
			appControlInfo.outputInfo.encConfig[0].out_signals = DSOS_YCBCR;
			appControlInfo.outputInfo.encConfig[0].out_connectors = DSOC_COMPONENT;
			appControlInfo.outputInfo.encConfig[0].flags = (DSECONF_OUT_SIGNALS | DSECONF_CONNECTORS );
			interface_setSelectedItem(pMenu, 0);
			break;
#endif
		default:
			break;
	}
	appControlInfo.outputInfo.standart = tv_standard;
	output_warnIfFailed(saveAppSettings());
#endif //#ifdef STB6x8x
#ifdef STB225
//	system("mount -o rw,remount /");
//	setParam("/etc/init.d/S35pnxcore.sh", "resolution", GET_NUMBER(pArg) == 720 ? "1280x720x60p" : "1920x1080x60i");
//	system("mount -o ro,remount /");
#endif // STB225

	system("sync");
	system("reboot");

	output_fillStandardMenu();
	interface_displayMenu(1);
#ifdef ENABLE_DVB
	/* Re-start DVB - if possible */
	if (offair_tunerPresent() && dvb_getNumberOfServices() > 0)
		offair_startVideo(screenMain);
#endif
	return 0;
}
#endif // STB6x8x || STB225

#ifdef STSDK
static void output_applyFormatMessage(void)
{
	if (st_needRestart()) {
		interface_showMessageBox(_T("RESTARTING"), thumbnail_warning, 0);
		raise(SIGQUIT);
	}
}

//this function MUST NOT be called from keyThread, because it cantain call of st_changeOutputMode() that can reinit DirectFB
static int output_cancelFormat(void *pArg)
{
	videoOutput_t	*p_videoOutput = (videoOutput_t *)pArg;

	interface_hideMessageBox();
	st_changeOutputMode(p_videoOutput, previousFormat);

	output_refillMenu(&(p_videoOutput->menu.baseMenu));
	output_applyFormatMessage();
	interface_displayMenu(1);
	return 0;
}

static int output_confirmFormat(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if ((cmd->command == interfaceCommandGreen) ||
		(cmd->command == interfaceCommandEnter) ||
		(cmd->command == interfaceCommandOk))
	{
//		videoOutput_t	*p_videoOutput = (videoOutput_t *)pArg;

		interface_hideMessageBox();
		interface_removeEvent(output_cancelFormat, pArg);
		output_applyFormatMessage();
	} else {
		// output_cancelFormat MUST NOT be called from keyThread, because it tries to cancel it
		interface_addEvent(output_cancelFormat, pArg, 0, 1);
	}
	isToggleContinues = 0;
	return 0;
}

static const char *output_getSelectedFormatName(interfaceMenu_t *pMenu)
{
	int32_t		itemMenu = pMenu->selectedItem;

	if(itemMenu < 0)
		return NULL;

	if(pMenu->menuEntry[itemMenu].pArg)
		return (char *)pMenu->menuEntry[itemMenu].pArg;

	return pMenu->menuEntry[itemMenu].info;
}

//this function MUST NOT be called from keyThread, because it cantain call of st_changeOutputMode() that can reinit DirectFB
static int output_tryNewVideoMode_Event(void* pArg)
{
	videoOutput_t	*p_videoOutput = (videoOutput_t *)pArg;
	const char		*newVideoMode = output_getSelectedFormatName(&(p_videoOutput->menu.baseMenu));

	if(strcmp(p_videoOutput->currentFormat, newVideoMode) == 0) {
		return 1;
	}
	interface_showMessageBox(_T("PLEASE_WAIT"), thumbnail_info, 0);

	st_changeOutputMode(p_videoOutput, newVideoMode);

	interface_addEvent(output_cancelFormat, pArg, FORMAT_CHANGE_TIMEOUT * 1000, 1);

	interface_showConfirmationBox(_T("CONFIRM_FORMAT_CHANGE"), thumbnail_warning, output_confirmFormat, pArg);
	return 1;
}

/**
 * This function switch into "Output format" menu and toggle output video modes.
 */
int output_toggleOutputModes(void)
{
	interfaceMenu_t	*pMenu;

	//we shold enable VideoSubMenu, because p_mainVideoOutput initialize there
	interface_switchMenu(interfaceInfo.currentMenu, &(VideoSubMenu.baseMenu));
	if(p_mainVideoOutput == NULL) {
		return -1;
	}
	pMenu = &(p_mainVideoOutput->menu.baseMenu);
	interface_menuActionShowMenu(interfaceInfo.currentMenu, pMenu);
	if(p_mainVideoOutput->formatCount) {
		uint32_t	next = 0;
		next = pMenu->selectedItem + 1;
		if(next > (p_mainVideoOutput->formatCount - 1))
			next = 0;
		pMenu->selectedItem = next;
	} else {
		return -1;
	}

	if(isToggleContinues == 0) {
		strcpy(previousFormat, p_mainVideoOutput->currentFormat);
		isToggleContinues = 1;
	}
	interface_addEvent(output_tryNewVideoMode_Event, (void *)p_mainVideoOutput, 0, 1);
	return 0;
}

static int output_setVideoOutput(interfaceMenu_t *pMenu, void* pArg)
{
	videoOutput_t	*p_videoOutput = (videoOutput_t *)pMenu;

	isToggleContinues = 1;
	strcpy(previousFormat, p_videoOutput->currentFormat);
	interface_addEvent(output_tryNewVideoMode_Event, (void *)p_videoOutput, 0, 1);

	return 0;
}

static void output_fillVideoOutputMenu(videoOutput_t *p_videoOutput)
{
	int32_t			selected = MENU_ITEM_BACK;
	interfaceMenu_t	*formatMenu = &(p_videoOutput->menu.baseMenu);
	elcdRpcType_t	type;
	cJSON			*list;
	cJSON			*param;
	int32_t			ret;

	param = cJSON_CreateString(p_videoOutput->name);
	interface_clearMenuEntries(formatMenu);
	ret = st_rpcSync(elcmd_listvmode, param, &type, &list);
	if(ret == 0 && type == elcdRpcResult && list && list->type == cJSON_Array) {
		cJSON *mode;
		char *name;

		struct {
			char	*displayName;
			char	*groupName;
			int		groupNameLen;
			cJSON	*mode;
			char	*modeName;
		} humanReadableOutputModes[] = {
//			{"1080p",	"1080p",	sizeof("1080p") - 1,	NULL, NULL},
			{"1080i",	"1080i",	sizeof("1080i") - 1,	NULL, NULL},
			{"720p",	"720p",		sizeof("720p") - 1,		NULL, NULL},
			{"PAL",		"720x576",	sizeof("720x576") - 1,	NULL, NULL},
			{"NTSC",	"720x480",	sizeof("720x480") - 1,	NULL, NULL},
		};
		uint32_t	i;
		int32_t		icon;
		int32_t		hasSupportedModes = 0;

		//check if supported any mode
		for(i = 0; (mode = cJSON_GetArrayItem(list, i)) != NULL; i++) {
			name = objGetString(mode, "name", NULL);
			if(!name)
				continue;

			if(objCheckIfTrue(mode, "current")) {
				int len = sizeof(p_videoOutput->currentFormat);
				strncpy(p_videoOutput->currentFormat, name, len);
				p_videoOutput->currentFormat[len - 1] = 0;
			}
			if(!p_videoOutput->hasFeedback || objCheckIfTrue(mode, "supported")) {
				uint32_t	j;
				//fill mediate humanReadableOutputModes massive
				for(j = 0; j < ARRAY_SIZE(humanReadableOutputModes); j++) {
					//chek group name
					if(strncmp(humanReadableOutputModes[j].groupName, name, humanReadableOutputModes[j].groupNameLen) != 0)
						continue;
					//choose the best mode (with highest fps)
					//for example, next inequalitys are true:
					//	"1080p60" > "1080p30" > "1080i50"
					//	"720x576p50 (4:3)" > "720x576p50" > "720x576i50 (4:3)" > "720x576i50"
					if(humanReadableOutputModes[j].modeName && (strcmp(humanReadableOutputModes[j].modeName, name) > 0))
						continue;
					// skip resolutions with 4:3 aspect
					if(strstr(name, "(4:3)"))
						continue;

					humanReadableOutputModes[j].modeName = name;
					humanReadableOutputModes[j].mode = mode;

					hasSupportedModes = 1;
					break;
				}
			}
			if(p_videoOutput->showAdvanced && hasSupportedModes)
				break;
		}

		if(!hasSupportedModes || p_videoOutput->showAdvanced) {
			uint32_t formatId = 0;
			//fill advanced settings fill menu here
			for(i = 0; (mode = cJSON_GetArrayItem(list, i)) != NULL; i++) {
				name = objGetString(mode, "name", NULL);
				if(!name)
					continue;

				if(objCheckIfTrue(mode, "current")) {
					selected = formatId;
				}

				icon = thumbnail_channels;
				if(p_videoOutput->hasFeedback) {
					if(objCheckIfTrue(mode, "native")) {
						icon = thumbnail_tvstandard;
					} else if(!objCheckIfTrue(mode, "supported")) {
						icon = thumbnail_not_selected;
					}
				}

				interface_addMenuEntry(formatMenu, name, output_setVideoOutput, NULL, icon);
				formatId++;
			}
			p_videoOutput->formatCount = formatId;
		}

		if(hasSupportedModes) {
			if(!p_videoOutput->showAdvanced) {
				uint32_t formatId = 0;
				//This static array should be changed on something clearer
				static char	correctModeNames[ARRAY_SIZE(humanReadableOutputModes)][64];

				for(i = 0; i < ARRAY_SIZE(humanReadableOutputModes); i++) {
					if(humanReadableOutputModes[i].mode) {
						mode = humanReadableOutputModes[i].mode;

						icon = objCheckIfTrue(mode, "native") ? thumbnail_tvstandard : thumbnail_channels;
						if(objCheckIfTrue(mode, "current"))
							selected = formatId;

						strncpy(correctModeNames[i], humanReadableOutputModes[i].modeName, sizeof(correctModeNames[i]) - 1);
						correctModeNames[i][sizeof(correctModeNames[i]) - 1] = 0;
// printf("%s[%d]: humanReadableOutputModes[%d].displayName=%s, correctModeNames[%d]=%s\n",
// 		__FILE__, __LINE__, i, humanReadableOutputModes[i].displayName, i, correctModeNames[i]);

						interface_addMenuEntry(formatMenu, humanReadableOutputModes[i].displayName, output_setVideoOutput, correctModeNames[i], icon);
						formatId++;
					}
				}
				p_videoOutput->formatCount = formatId;
				interface_addMenuEntry(formatMenu, _T("SHOW_ADVANCED"), output_toggleAdvancedVideoOutput, (void *)p_videoOutput, thumbnail_configure);
			} else {
				interface_addMenuEntry(formatMenu, _T("HIDE_ADVANCED"), output_toggleAdvancedVideoOutput, (void *)p_videoOutput, thumbnail_configure);
			}
		}
	} else if( type == elcdRpcError && list && list->type == cJSON_String )
		eprintf("%s: failed to get video mode list: %s\n", list->valuestring);
	cJSON_Delete(param);
	cJSON_Delete(list);

	interface_setSelectedItem(formatMenu, selected);
}

static int output_toggleAdvancedVideoOutput(interfaceMenu_t *pMenu, void* pArg)
{
	videoOutput_t	*p_videoOutput = (videoOutput_t *)pArg;

	p_videoOutput->showAdvanced = !p_videoOutput->showAdvanced;
	output_fillVideoOutputMenu(p_videoOutput);
//	if(p_videoOutput->showAdvanced == 0)
//		interface_setSelectedItem(pMenu, interface_getMenuEntryCount(pMenu) - 1);
	interface_displayMenu(1);

	return 0;
}

static int output_enterVideoOutputMenu(interfaceMenu_t *pMenu, void *pArg)
{
	videoOutput_t	*p_videoOutput = (videoOutput_t *)pArg;

	output_fillVideoOutputMenu(p_videoOutput);
	return 0;
}
#endif // STSDK

static int32_t output_inputFilmTypeCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	inputMode_t *input = (inputMode_t *)pArg;
	int32_t selected;

	switch(cmd->command) {
		case interfaceCommand1:
		case interfaceCommand2:
		case interfaceCommand5:
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand6: {
			char text[60];
			sprintf(text, "%s:%d%d", table_IntStrLookup(inputDeviceNames, input->deviceSelect, "-"), input->deviceSelect, cmd->command - interfaceCommand1 + 1);
			extInput_set(input->inputName, text);

			break;
		}
		case DIKS_HOME:
		case interfaceCommandExit:
		case interfaceCommandGreen:
		case interfaceCommandRed:
		default:
			break;
	}
	selected = extInput_getSelectedId();
	interface_setSelectedItem(&InputsSubMenu.baseMenu, (selected >= 0) ? selected + 1 : 0);
	return 0;
}

static int32_t output_inputTypeCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	int32_t selected;
	int32_t deviceSelect = cmd->command - interfaceCommand1 + 1;

	switch(cmd->command) {
		case interfaceCommand1:
		case interfaceCommand2: {
			char menuText[] = "Choose type of film:\n\n"
				"1. Thriller\n"
				"2. Drama\n"
				"3. Romance\n"
				"4. Comedy\n"
				"5. Sports\n"
				"6. Documentary\n";
			static inputMode_t input;

			input.deviceSelect = deviceSelect;
			strcpy(input.inputName, (char*)pArg);
			interface_showConfirmationBox(menuText, thumbnail_account_active, output_inputFilmTypeCallback, (void*)&input);
			return 1;
		}
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand5: {
			char text[60];

			sprintf(text, "%s:%d%d", table_IntStrLookup(inputDeviceNames, deviceSelect, "-"), deviceSelect, 0);
			extInput_set(pArg, text);
			break;
		}
		case interfaceCommandMainMenu:
		case interfaceCommandExit:
		case interfaceCommandGreen:
		case interfaceCommandRed:
		default:
			break;
	}
	selected = extInput_getSelectedId();
	interface_setSelectedItem(&InputsSubMenu.baseMenu, (selected >= 0) ? selected + 1 : 0);

	return 0;
}

static int32_t output_disableInput(interfaceMenu_t *pMenu, void *pArg)
{
	(void)pMenu;
	(void)pArg;
	extInput_disble();

	return 0;
}

static int32_t output_setInput(interfaceMenu_t *pMenu, void *pArg)
{
	if(!pArg) {
		eprintf ("%s: Error setting input.\n", __FUNCTION__);
		return 0;
	}

	interface_showConfirmationBox("Choose using device:\n"
						"\n1. DVD"
						"\n2. VCR"
						"\n3. Video Game"
						"\n4. PC"
						"\n5. Other\n", thumbnail_account_active, output_inputTypeCallback, pArg);
	return 0;
}

static void output_fillInputsMenu(interfaceListMenu_t *pMenu, void *pArg)
{
	interfaceMenu_t	*inputsMenu = &(pMenu->baseMenu);
	listHead_t *inputNamesList;
	int32_t icon = thumbnail_channels;
	int32_t i;
	int32_t selected;
	const char *name;

	(void)pArg;
	interface_clearMenuEntries(inputsMenu);

	interface_addMenuEntry(inputsMenu, _T("OFF"), output_disableInput, NULL, icon);
	inputNamesList = extInput_getList();
	i = 0;
	while((name = strList_get(inputNamesList, i)) != NULL) {
		//in next line force cast name to (void *) for avoiding compile warning
		interface_addMenuEntry(inputsMenu, name, output_setInput, (void *)name, icon);
		i++;
	}
	selected = extInput_getSelectedId();
	interface_setSelectedItem(&InputsSubMenu.baseMenu, (selected >= 0) ? selected + 1 : 0);
}

int32_t output_toggleInputs(void)
{
	listHead_t *inputNamesList;
	int32_t inputCurId;
	int32_t inputNextId;
	interfaceMenu_t *inputsMenu = &InputsSubMenu.baseMenu; 

	inputCurId = extInput_getSelectedId();
	if(inputCurId >= 0) {
		inputNextId = inputCurId + 1;
	} else {
		inputNextId = 0;
	}
	extInput_disble();

	interface_menuActionShowMenu(interfaceInfo.currentMenu, inputsMenu);
	inputNamesList = extInput_getList();
	if(inputNextId >= strList_count(inputNamesList)) {
		//we reach end of list, just disable
		interface_setSelectedItem(inputsMenu, 0);
		interface_displayMenu(1);
		return 0;
	}
	interface_setSelectedItem(inputsMenu, inputNextId + 1);
	interface_displayMenu(1);

	output_setInput(inputsMenu, (void *)strList_get(inputNamesList, inputNextId));

	return 0;
}



#ifdef STB82
/**
 * This function now uses the Encoder API to set the slow blanking instead of the Output API.
 */
static int output_setBlanking(interfaceMenu_t *pMenu, void* pArg)
{
	int blanking = GET_NUMBER(pArg);

	appControlInfo.outputInfo.encConfig[0].slow_blanking = blanking;
	appControlInfo.outputInfo.encConfig[0].flags = DSECONF_SLOW_BLANKING;
	switch(blanking)
	{
		case(DSOSB_4x3) : interface_setSelectedItem(pMenu, 0); break;
		case(DSOSB_16x9): interface_setSelectedItem(pMenu, 1); break;
		case(DSOSB_OFF) : interface_setSelectedItem(pMenu, 2); break;
		default: break;
	}
	gfx_setOutputFormat(0);
	interface_displayMenu(1);
	return 0;
}

static void output_fillBlankingMenu(void)
{
	interfaceMenu_t *blankingMenu = _M &BlankingMenu;
	interface_clearMenuEntries(blankingMenu);
	interface_addMenuEntry(blankingMenu, "4 x 3",  output_setBlanking, (void*)DSOSB_4x3,  thumbnail_configure);
	interface_addMenuEntry(blankingMenu, "16 x 9", output_setBlanking, (void*)DSOSB_16x9, thumbnail_configure);
	interface_addMenuEntry(blankingMenu, "None",   output_setBlanking, (void*)DSOSB_OFF,  thumbnail_configure);
}

static int output_toggleInterfaceAnimation(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceInfo.animation = (interfaceInfo.animation + 1) % interfaceAnimationCount;
	if (interfaceInfo.animation > 0) {
		interfaceInfo.currentMenu = _M &interfaceMainMenu; // toggles animation
		saveAppSettings();
	}
	return interface_menuActionShowMenu(interfaceInfo.currentMenu, pMenu);
}
#endif // STB82

#ifndef HIDE_EXTRA_FUNCTIONS
static int output_toggleAudio(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.soundInfo.rcaOutput = !appControlInfo.soundInfo.rcaOutput;
	// FIXME: Just force rcaOutput for now
	appControlInfo.soundInfo.rcaOutput = 1;
	sound_restart();
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_togglePCR(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.bProcessPCR = !appControlInfo.bProcessPCR;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleRSync(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.bRendererDisableSync = !appControlInfo.bRendererDisableSync;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleBufferTracking(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.bUseBufferModel = !appControlInfo.bUseBufferModel;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}
#endif // HIDE_EXTRA_FUNCTIONS

static int output_toggleHighlightColor(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceInfo.highlightColor++;
	if (interface_colors[interfaceInfo.highlightColor].A==0)
		interfaceInfo.highlightColor = 0;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleResumeAfterStart(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.playbackInfo.bResumeAfterStart = !appControlInfo.playbackInfo.bResumeAfterStart;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleAutoPlay(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.playbackInfo.bAutoPlay = !appControlInfo.playbackInfo.bAutoPlay;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

int output_toggleFileSorting(interfaceMenu_t* pMenu, void* pArg)
{
	appControlInfo.mediaInfo.fileSorting = appControlInfo.mediaInfo.fileSorting == naturalsort ? alphasort : naturalsort;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_togglePlayControlTimeout(interfaceMenu_t *pMenu, void* pArg)
{
	interfacePlayControl.showTimeout = interfacePlayControl.showTimeout % PLAYCONTROL_TIMEOUT_MAX + 1;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_togglePlayControlShowOnStart(interfaceMenu_t *pMenu, void* pArg)
{
	interfacePlayControl.showOnStart = !interfacePlayControl.showOnStart;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

#ifdef ENABLE_VOIP
static int output_toggleVoipIndication(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceInfo.enableVoipIndication = !interfaceInfo.enableVoipIndication;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleVoipBuzzer(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.voipInfo.buzzer = !appControlInfo.voipInfo.buzzer;
	voip_setBuzzer();
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}
#endif



static int output_confirmReset(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
#if (defined __i386__) || (defined __x86_64__)
		system("rm -R " CONFIG_DIR "/*");
#else
#ifdef STB225
		system("mount -o rw,remount /");
#endif
		system("rm -R " SYSTEM_CONFIG_DIR "/*");
#ifdef STB225
		system("mount -o ro,remount /");
#endif
		system("sync");
		system("reboot");
#endif
		return 0;
	}

	return 1;
}

static int output_statusReport(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	system(CREATE_REPORT_FILE);
	interface_showMessageBox(_T("STATUS_REPORT_COMPLETE"), thumbnail_configure, 3000);

	return 0;
}

static int output_enterCalibrateMenu(interfaceMenu_t *pMenu, void * pArg)
{
	int32_t selected = MENU_ITEM_BACK;
	interfaceMenu_t * calibrMenu = &CurrentmeterSubMenu.baseMenu;
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries(calibrMenu);

	sprintf(buf, "%s: %u %s", _T("CURRENTMETER_CALIBRATE_HIGH_VALUE"), currentmeter_getCalibrateHighValue(), _T("WATT"));
	interface_addMenuEntry(calibrMenu, buf, output_calibrateCurrentMeter, SET_NUMBER(1), thumbnail_configure);

	sprintf(buf, "%s: %u %s", _T("CURRENTMETER_CALIBRATE_LOW_VALUE"), currentmeter_getCalibrateLowValue(), _T("WATT"));
	interface_addMenuEntry(calibrMenu, buf, output_calibrateCurrentMeter, SET_NUMBER(0), thumbnail_configure);

	interface_setSelectedItem(calibrMenu, selected);

	return 0;
}

static int output_calibrateCurrentMeter(interfaceMenu_t *pMenu, void* pArg)
{
	int isHigh = GET_NUMBER(pArg);
	uint32_t cur_val = 0;

	if(currentmeter_getValue(&cur_val) == 0) {
		char info[MENU_ENTRY_INFO_LENGTH];

		if(isHigh == 1) {
			currentmeter_setCalibrateHighValue(cur_val);
		} else {
			currentmeter_setCalibrateLowValue(cur_val);
		}
		snprintf(info, sizeof(info), _T("CURRENTMETER_CALIBRATE_SUCCESS"), cur_val);
		interface_showMessageBox(info, thumbnail_yes, 0);
	}
	else {
		interface_showMessageBox(_T("CURRENTMETER_CALIBRATE_ERROR"), thumbnail_error, 0);
	}
	output_redrawMenu(pMenu);

	return 1;
}

static int output_resetSettings(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showConfirmationBox(_T("RESET_SETTINGS_CONFIRM"), thumbnail_question, output_confirmReset, pArg);
	return 1; // when called from askPassword, should return non-0 to leave getText message box opened
}

#ifdef ENABLE_DVB
static int output_clearDvbSettings(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
#if (defined ENABLE_PVR) && (defined STBPNX)
	if( pvr_hasDVBRecords() )
		str = _T("DVB_CLEAR_SERVICES_CONFIRM_CANCEL_PVR");
	else
#endif
	str = _T("DVB_CLEAR_SERVICES_CONFIRM");
	interface_showConfirmationBox(str, thumbnail_question, output_confirmClearDvb, pArg);
	return 1;
}

static int output_confirmClearDvb(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch(cmd->command) {
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
			return 0;
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			offair_clearServiceList(1);
			output_redrawMenu(pMenu);
			return 0;
		default:
			break;
	}

	return 1;
}

#ifdef ENABLE_DVB_DIAG
static int output_toggleDvbDiagnosticsOnStart(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.offairInfo.diagnosticsMode = appControlInfo.offairInfo.diagnosticsMode != DIAG_ON ? DIAG_ON : DIAG_FORCED_OFF;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}
#endif

static int output_toggleDvbType(interfaceMenu_t *pMenu, void* pArg)
{
	return output_saveAndRedraw(dvbfe_toggleType(offair_getTuner()), pMenu);
}

static int output_toggleDvbTuner(interfaceMenu_t *pMenu, void* pArg)
{
	offair_stopVideo(screenMain, 1);
	appControlInfo.dvbInfo.adapter = 1 - appControlInfo.dvbInfo.adapter;
	return output_saveAndRedraw(0, pMenu);
}

static int output_toggleDvbShowScrambled(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.offairInfo.dvbShowScrambled = (appControlInfo.offairInfo.dvbShowScrambled + 1) % 3;
	dvbChannel_changed();

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleDvbNetworkSearch(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbCommonInfo.networkScan = !appControlInfo.dvbCommonInfo.networkScan;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

#ifdef STBPNX
static int output_toggleDvbInversion(interfaceMenu_t *pMenu, void* pArg)
{
	stb810_dvbfeInfo *fe;

	switch (dvbfe_getType(appControlInfo.dvbInfo.adapter))
	{
		case SYS_DVBT:
		case SYS_DVBT2:
			fe = &appControlInfo.dvbtInfo.fe;
			break;
		case SYS_DVBC_ANNEX_AC:
			fe = &appControlInfo.dvbcInfo.fe;
			break;
		default:
			return 0;
	}
	fe->inversion = fe->inversion == 0 ? 1 : 0;

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}
#endif

static int output_toggleDvbBandwidth(interfaceMenu_t *pMenu, void* pArg)
{
	switch (appControlInfo.dvbtInfo.bandwidth)
	{
		case BANDWIDTH_8_MHZ: appControlInfo.dvbtInfo.bandwidth = BANDWIDTH_7_MHZ; break;
		/*
		case BANDWIDTH_7_MHZ: appControlInfo.dvbtInfo.bandwidth = BANDWIDTH_6_MHZ; break;
		case BANDWIDTH_6_MHZ: appControlInfo.dvbtInfo.bandwidth = BANDWIDTH_AUTO; break;*/
		default:
			appControlInfo.dvbtInfo.bandwidth = BANDWIDTH_8_MHZ;
	}

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleDvbPolarization(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbsInfo.polarization = !appControlInfo.dvbsInfo.polarization;
	return output_saveAndRedraw(0, pMenu);
}

static int output_toggleDvbSpeed(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbCommonInfo.adapterSpeed = (appControlInfo.dvbCommonInfo.adapterSpeed+1) % 11;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleDvbExtScan(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbCommonInfo.extendedScan = !appControlInfo.dvbCommonInfo.extendedScan;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleDiseqcSwitch(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbsInfo.diseqc.type = (appControlInfo.dvbsInfo.diseqc.type+1) % diseqcSwitchTypeCount;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleDiseqcPort(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbsInfo.diseqc.port = (appControlInfo.dvbsInfo.diseqc.port+1) % 4;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleDiseqcUncommited(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbsInfo.diseqc.uncommited = (appControlInfo.dvbsInfo.diseqc.uncommited+1) % 17;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static void getDvbLimits(uint32_t adapter, uint32_t *min, uint32_t *max)
{
	switch (dvbfe_getType(adapter)) {
		case SYS_DVBS:
			if (appControlInfo.dvbsInfo.band == dvbsBandC) {
				*min = DVB_MIN_FREQUENCY_C_BAND;
				*max = DVB_MAX_FREQUENCY_C_BAND;
			} else {
				*min = DVB_MIN_FREQUENCY_K_BAND;
				*max = DVB_MAX_FREQUENCY_K_BAND;
			}
			break;
		case SYS_DVBC_ANNEX_AC:
			*min = DVB_MIN_FREQUENCY_C;
			*max = DVB_MAX_FREQUENCY_C;
			break;
		default: // DVBT, ATSC
			*min = DVB_MIN_FREQUENCY_T;
			*max = DVB_MAX_FREQUENCY_T;
	}
}

static stb810_dvbfeInfo* getDvbRange(uint32_t adapter)
{
	stb810_dvbfeInfo *fe = NULL;

	switch(dvbfe_getType(adapter)) {
		case SYS_DVBT:
		case SYS_DVBT2:
			fe = &appControlInfo.dvbtInfo.fe;
			break;
		case SYS_DVBC_ANNEX_AC:
			fe = &appControlInfo.dvbcInfo.fe;
			break;
		case SYS_DVBS:
			if(appControlInfo.dvbsInfo.band == dvbsBandC) {
				fe = &appControlInfo.dvbsInfo.c_band;
			} else {
				fe = &appControlInfo.dvbsInfo.k_band;
			}
			break;
		case SYS_ATSC:
		case SYS_DVBC_ANNEX_B:
			fe = &appControlInfo.atscInfo.fe;
			break;
		default:;
	}
	return fe;
}

static uint32_t* getDvbSymbolRate(void)
{
	switch (dvbfe_getType(appControlInfo.dvbInfo.adapter)) {
		case SYS_DVBC_ANNEX_AC: return &appControlInfo.dvbcInfo.symbolRate;
		case SYS_DVBS: return &appControlInfo.dvbsInfo.symbolRate;
		default:   break;
	}
	return NULL;
}

static char *output_getDvbRange(int field, void* pArg)
{
	if (field == 0) {
		static char buffer[32];
		int id = GET_NUMBER(pArg);
		buffer[0] = 0;
		stb810_dvbfeInfo *fe = getDvbRange(appControlInfo.dvbInfo.adapter);
		if (!fe)
			return buffer;
		switch (id)
		{
			case 0: sprintf(buffer, "%u", fe->lowFrequency); break;
			case 1: sprintf(buffer, "%u", fe->highFrequency); break;
			case 2: sprintf(buffer, "%u", fe->frequencyStep); break;
			case 3: {
				uint32_t *symbolRate = getDvbSymbolRate();
				if (!symbolRate)
					return buffer;
				sprintf(buffer, "%u", *symbolRate);
				break;
			}
		}
		return buffer;
	} else
		return NULL;
}

static int output_setDvbRange(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	outputDvbOption option = GET_NUMBER(pArg);
	uint32_t val;
	stb810_dvbfeInfo *fe;

	if( value == NULL )
		return 1;

	val = strtoul(value, NULL, 10);

	if(option == optionSymbolRate) {
		if (val < DVB_MIN_SYMBOLRATE || val > DVB_MAX_SYMBOLRATE) {
			eprintf("%s: invalid frequency step %u\n", __FUNCTION__, val);
			interface_showMessageBox(_T("ERR_INCORRECT_FREQUENCY"), thumbnail_error, 0);
			return -1;
		}
		uint32_t *symbolRate = getDvbSymbolRate();
		if (!symbolRate) {
			eprintf("%s: failed to get symbolRate for tuner %d %s\n", __FUNCTION__,
				appControlInfo.dvbInfo.adapter, dvbfe_getTunerTypeName(appControlInfo.dvbInfo.adapter));
			goto set_range_failed;
		}
		*symbolRate = val;
		return output_saveAndRedraw(saveAppSettings(), pMenu);
	}

	fe = getDvbRange(appControlInfo.dvbInfo.adapter);
	if (!fe) {
		eprintf("%s: failed to get freuquency range for tuner %d %s\n", __FUNCTION__,
			appControlInfo.dvbInfo.adapter, dvbfe_getTunerTypeName(appControlInfo.dvbInfo.adapter));
		goto set_range_failed;
	}

	if (option == optionFreqStep) {
		if (val < DVB_MIN_FREQUENCY_STEP || val > DVB_MAX_FREQUENCY_STEP) {
			eprintf("%s: invalid frequency step %u\n", __FUNCTION__, val);
			interface_showMessageBox(_T("ERR_INCORRECT_FREQUENCY"), thumbnail_warning, 0);
			return -1;
		}
	} else {
		uint32_t min,max;
		getDvbLimits(appControlInfo.dvbInfo.adapter, &min, &max);
		if (val < min || val > max) {
			eprintf("%s: invalid frequency %u: must be %u-%u for tuner %d type %s\n", __FUNCTION__,
				val, min, max, appControlInfo.dvbInfo.adapter, dvbfe_getTunerTypeName(appControlInfo.dvbInfo.adapter));
			interface_showMessageBox(_T("ERR_INCORRECT_FREQUENCY"), thumbnail_warning, 0);
			return -1;
		}
	}
	switch (option)
	{
		case optionLowFreq:     fe->lowFrequency  = val; break;
		case optionHighFreq:    fe->highFrequency = val; break;
		case optionFreqStep:    fe->frequencyStep = val; break;
		default:
			eprintf("%s: wrong option %d\n", __FUNCTION__, option);
			goto set_range_failed;
	}
	return output_saveAndRedraw(saveAppSettings(), pMenu);

set_range_failed:
	interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	return -1;
}

static int output_toggleDvbModulation(interfaceMenu_t *pMenu, void* pArg)
{
	switch (appControlInfo.dvbcInfo.modulation) {
		case QAM_16: appControlInfo.dvbcInfo.modulation = QAM_32; break;
		case QAM_32: appControlInfo.dvbcInfo.modulation = QAM_64; break;
		case QAM_64: appControlInfo.dvbcInfo.modulation = QAM_128; break;
		case QAM_128: appControlInfo.dvbcInfo.modulation = QAM_256; break;
		default: appControlInfo.dvbcInfo.modulation = QAM_16;
	}
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleAtscModulation(interfaceMenu_t *pMenu, void* pArg)
{
	table_IntInt_t mod_to_delSys[] = {
		{VSB_8,		SYS_ATSC},
//		{VSB_16,	SYS_ATSC},
		{QAM_64,	SYS_DVBC_ANNEX_B},
		{QAM_256,	SYS_DVBC_ANNEX_B},
		TABLE_INT_INT_END_VALUE
	};
	static uint32_t curAtscModulation = 0;

	curAtscModulation++;
	if(curAtscModulation >= ARRAY_SIZE(mod_to_delSys)) {
		curAtscModulation = 0;
	}
	appControlInfo.atscInfo.modulation = mod_to_delSys[curAtscModulation].key;

	dvbfe_setFrontendType(appControlInfo.dvbInfo.adapter, mod_to_delSys[curAtscModulation].value);

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleSortOrder(interfaceMenu_t *pMenu, void* pArg)
{
	do {
		appControlInfo.offairInfo.sorting = (appControlInfo.offairInfo.sorting + 1) % serviceSortCount;
	} while(appControlInfo.offairInfo.sorting == serviceSortType);
	dvbChannel_sort(appControlInfo.offairInfo.sorting);

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static inline char *get_HZprefix(fe_delivery_system_t delSys)
{
	return _T(delSys == SYS_DVBS ? "MHZ" : "KHZ");
}

static int output_changeDvbRange(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	int id = GET_NUMBER(pArg);
	fe_delivery_system_t delSys = dvbfe_getType(appControlInfo.dvbInfo.adapter);
	switch (id)
	{
		case optionLowFreq:    sprintf(buf, "%s, %s: ", _T("DVB_LOW_FREQ"),  get_HZprefix(delSys)); break;
		case optionHighFreq:   sprintf(buf, "%s, %s: ", _T("DVB_HIGH_FREQ"), get_HZprefix(delSys)); break;
		case optionFreqStep:   sprintf(buf, "%s, %s: ", _T("DVB_STEP_FREQ"),   _T("KHZ")); break;
		case optionSymbolRate: sprintf(buf, "%s, %s: ", _T("DVB_SYMBOL_RATE"), _T("KHZ")); break;
		default: return -1;
	}
	return interface_getText(pMenu, buf, "\\d+", output_setDvbRange, output_getDvbRange, inputModeDirect, pArg);
}

static int output_dvbSignalMonitor(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMenu(0, 0);
	offair_dvbInfo(appControlInfo.dvbInfo.adapter);
	return 0;
}

int output_showDVBMenu(interfaceMenu_t *pMenu, void* notused)
{
	return interface_menuActionShowMenu(pMenu, &DVBSubMenu);
}

int output_enterDVBMenu(interfaceMenu_t *dvbMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	const char *str;
	uint32_t adapter = appControlInfo.dvbInfo.adapter;
	stb810_dvbfeInfo *fe = getDvbRange(adapter);
	fe_delivery_system_t delSys = dvbfe_getType(adapter);

	dprintf("%s(): tuner %d - %s\n", __func__, adapter, dvbfe_getTunerTypeName(adapter));

	// assert (dvbMenu == _M &DVBSubMenu);
	interface_clearMenuEntries(dvbMenu);

	if (fe == NULL)
		return 1;

	if(dvbfe_hasTuner(0) && dvbfe_hasTuner(1)) {
		sprintf(buf, "DVB #%d", adapter + 1);
		interface_addMenuEntry(dvbMenu, buf, output_toggleDvbTuner, NULL, thumbnail_scan);
	}

	if(dvbfe_isLinuxAdapter(adapter)) {
		sprintf(buf, PROFILE_LOCATIONS_PATH "/%s", appControlInfo.offairInfo.profileLocation);
		if(getParam(buf, "LOCATION_NAME", NULL, NULL)) {
			str = _T("SETTINGS_WIZARD");
			interface_addMenuEntry(dvbMenu, str, offair_wizardStart, NULL, thumbnail_scan);
		}

		str = _T("DVB_MONITOR");
		interface_addMenuEntry(dvbMenu, str, offair_frequencyMonitor, NULL, thumbnail_info);
	}

	str = _T("DVB_INSTALL");
	interface_addMenuEntry(dvbMenu, str, offair_serviceScan, NULL, thumbnail_scan);

	str = _T("DVB_SCAN_FREQUENCY");
	interface_addMenuEntry(dvbMenu, str, offair_frequencyScan, screenMain, thumbnail_scan);

	sprintf(buf, "%s (%d)", _T("DVB_CLEAR_SERVICES"), dvb_getNumberOfServices());
	interface_addMenuEntry(dvbMenu, buf, output_clearDvbSettings, screenMain, thumbnail_scan);

	str = _T(table_IntStrLookup(g_serviceSortNames, appControlInfo.offairInfo.sorting, "NONE"));
	sprintf(buf, "%s: %s", _T("SORT_ORDER_MENU_ITEM"), str);
	interface_addMenuEntry(dvbMenu, buf, output_toggleSortOrder, screenMain, thumbnail_select);

#ifdef ENABLE_DVB_DIAG
	sprintf(buf, "%s: %s", _T("DVB_START_WITH_DIAGNOSTICS"), _T( appControlInfo.offairInfo.diagnosticsMode == DIAG_FORCED_OFF ? "OFF" : "ON" ) );
	interface_addMenuEntry(dvbMenu, buf, output_toggleDvbDiagnosticsOnStart, NULL, thumbnail_configure);
#endif

	if(dvbfe_getDelSysCount(adapter) > 1) {
		snprintf(buf, sizeof(buf), "%s: %s", _T("DVB_MODE"), dvbfe_getTunerTypeName(adapter));
		interface_addMenuEntry(dvbMenu, buf, output_toggleDvbType, NULL, thumbnail_scan);
	}

	switch(appControlInfo.offairInfo.dvbShowScrambled) {
		case SCRAMBLED_SHOW: str = _T("ON"); break;
		case SCRAMBLED_PLAY: str = _T("PLAY"); break;
		default:             str = _T("OFF");
	}
	sprintf(buf, "%s: %s", _T("DVB_SHOW_SCRAMBLED"), str);
	interface_addMenuEntry(dvbMenu, buf, output_toggleDvbShowScrambled, NULL, thumbnail_configure);

	sprintf(buf, "%s: %s", _T("DVB_NETWORK_SEARCH"), _T( appControlInfo.dvbCommonInfo.networkScan ? "ON" : "OFF" ) );
	interface_addMenuEntry(dvbMenu, buf, output_toggleDvbNetworkSearch, NULL, thumbnail_configure);

#ifdef STBPNX
	sprintf(buf, "%s: %s", _T("DVB_INVERSION"), _T( fe->inversion ? "ON" : "OFF" ) );
	interface_addMenuEntry(dvbMenu, buf, output_toggleDvbInversion, NULL, thumbnail_configure);
#endif
	if(delSys == SYS_DVBC_ANNEX_AC || delSys == SYS_DVBS) {
		sprintf(buf, "%s: %u %s", _T("DVB_SYMBOL_RATE"), getDvbSymbolRate()?*(getDvbSymbolRate()):0, _T("KHZ"));
		interface_addMenuEntry(dvbMenu, buf, output_changeDvbRange, SET_NUMBER(optionSymbolRate), thumbnail_configure);
	}
	switch(delSys) {
	case SYS_DVBT:
	case SYS_DVBT2:
		switch(appControlInfo.dvbtInfo.bandwidth) {
			case BANDWIDTH_8_MHZ: sprintf(buf, "%s: 8 %s", _T("DVB_BANDWIDTH"), _T( "MHZ" ) ); break;
			case BANDWIDTH_7_MHZ: sprintf(buf, "%s: 7 %s", _T("DVB_BANDWIDTH"), _T( "MHZ" ) ); break;
			case BANDWIDTH_6_MHZ: sprintf(buf, "%s: 6 %s", _T("DVB_BANDWIDTH"), _T( "MHZ" ) ); break;
			default:
				sprintf(buf, "%s: Auto", _T("DVB_BANDWIDTH") );
		}
		interface_addMenuEntry(dvbMenu, buf, output_toggleDvbBandwidth, NULL, thumbnail_configure);
		break;
	case SYS_DVBC_ANNEX_AC:
		str = table_IntStrLookup(fe_modulationName, appControlInfo.dvbcInfo.modulation, _T("NOT_AVAILABLE_SHORT"));
		sprintf(buf, "%s: %s", _T("DVB_QAM_MODULATION"), str);
		interface_addMenuEntry(dvbMenu, buf, output_toggleDvbModulation, NULL, thumbnail_configure);
		break;
	case SYS_ATSC:
	case SYS_DVBC_ANNEX_B:
		str = table_IntStrLookup(fe_modulationName, appControlInfo.atscInfo.modulation, _T("NOT_AVAILABLE_SHORT"));
		sprintf(buf, "%s: %s", _T("DVB_MODULATION"), str);
		interface_addMenuEntry(dvbMenu, buf, output_toggleAtscModulation, NULL, thumbnail_configure);
		break;
	case SYS_DVBS:
		sprintf(buf, "%s: %c", _T("DVB_POLARIZATION"), appControlInfo.dvbsInfo.polarization ? 'V' : 'H');
		interface_addMenuEntry(dvbMenu, buf, output_toggleDvbPolarization, NULL, thumbnail_configure);
		break;
	default:
		break;
	}

	sprintf(buf, "%s: %u %s", _T("DVB_LOW_FREQ"), fe->lowFrequency, get_HZprefix(delSys));
	interface_addMenuEntry(dvbMenu, buf, output_changeDvbRange, SET_NUMBER(optionLowFreq), thumbnail_configure);

	sprintf(buf, "%s: %u %s", _T("DVB_HIGH_FREQ"),fe->highFrequency, get_HZprefix(delSys));
	interface_addMenuEntry(dvbMenu, buf, output_changeDvbRange, SET_NUMBER(optionHighFreq), thumbnail_configure);

	if(dvbfe_isLinuxAdapter(appControlInfo.dvbInfo.adapter)) {
		if(appControlInfo.dvbCommonInfo.adapterSpeed > 0) {
			sprintf(buf, "%s: %d%%", _T("DVB_SPEED"), 100-100*appControlInfo.dvbCommonInfo.adapterSpeed/10);
		} else {
			sprintf(buf, "%s: 1", _T("DVB_SPEED"));
		}
		interface_addMenuEntry(dvbMenu, buf, output_toggleDvbSpeed, NULL, thumbnail_configure);

		sprintf(buf,"%s: %s", _T("DVB_EXT_SCAN") , _T( appControlInfo.dvbCommonInfo.extendedScan == 0 ? "OFF" : "ON" ));
		interface_addMenuEntry(dvbMenu, buf, output_toggleDvbExtScan, NULL, thumbnail_configure);

		sprintf(buf, "%s: %u %s", _T("DVB_STEP_FREQ"), fe->frequencyStep, _T("KHZ"));
		interface_addMenuEntry(dvbMenu, buf, output_changeDvbRange, (void*)2, thumbnail_configure);
	}

	if(delSys == SYS_DVBS) {
		interface_addMenuEntry(dvbMenu, "DiSEqC", interface_menuActionShowMenu, &DiSEqCMenu, thumbnail_scan);
	}

	interface_addMenuEntry(dvbMenu, _T("DVB_SIGNAL_MONITOR"), output_dvbSignalMonitor, NULL, thumbnail_info);

	return 0;
}

int output_enterDiseqcMenu(interfaceMenu_t *diseqcMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(diseqcMenu);
	if (appControlInfo.dvbsInfo.diseqc.uncommited)
		snprintf(buf, sizeof(buf), "DiSEqC 1.1 Switch: %u", appControlInfo.dvbsInfo.diseqc.uncommited);
	else
		snprintf(buf, sizeof(buf), "DiSEqC 1.1 Switch: %s", _T("NONE"));
	interface_addMenuEntry(diseqcMenu, buf, output_toggleDiseqcUncommited, NULL, thumbnail_configure);
	snprintf(buf, sizeof(buf), "DiSEqC 1.0 Switch: %s", _T( diseqc_switch_names[appControlInfo.dvbsInfo.diseqc.type] ));
	interface_addMenuEntry(diseqcMenu, buf, output_toggleDiseqcSwitch, NULL, thumbnail_configure);
	if (appControlInfo.dvbsInfo.diseqc.type) {
		if (appControlInfo.dvbsInfo.diseqc.type == diseqcSwitchMulti)
			snprintf(buf, sizeof(buf), "Multiswitch Port: %c", 'A' + (appControlInfo.dvbsInfo.diseqc.port & 1));
		else
			snprintf(buf, sizeof(buf), "Switch LNB: %u", appControlInfo.dvbsInfo.diseqc.port + 1);
		interface_addMenuEntry(diseqcMenu, buf, output_toggleDiseqcPort, NULL, thumbnail_configure);
	}
	return 0;
}

#ifdef ENABLE_DVB_START_CHANNEL
int output_enterDvbStartMenu(interfaceMenu_t* startMenu, void* notused)
{
	interface_clearMenuEntries(startMenu);
	interface_addMenuEntry(startMenu, _T("LAST_WATCHED"), output_toggleDvbStartChannel, SET_NUMBER(0), thumbnail_channels);
	interface_setSelectedItem(startMenu, 0);

	for (int i = 1; i < dvbChannel_getCount(); i++) {
		EIT_service_t * service = offair_getService(i);
		if (!service)
			continue;
		if (appControlInfo.offairInfo.startChannel == i)
			interface_setSelectedItem(startMenu, interface_getMenuEntryCount(startMenu));
		interface_addMenuEntry(startMenu, dvb_getServiceName(service),
		                       output_toggleDvbStartChannel, SET_NUMBER(i),
		                       service_thumbnail(service));
		interface_setMenuEntryLabel(menu_getLastEntry(startMenu), "DVB");
	}
#ifdef ENABLE_ANALOGTV
	for (int i = dvbChannel_getCount();
			 i < dvbChannel_getCount() + (int)analogtv_getChannelCount(0);
	         i++)
	{
		if (appControlInfo.offairInfo.startChannel == i)
			interface_setSelectedItem(startMenu, interface_getMenuEntryCount(startMenu));
		interface_addMenuEntry(startMenu, analogtv_getServiceName(i-dvbChannel_getCount()),
		                       output_toggleDvbStartChannel, SET_NUMBER(i),
		                       thumbnail_tvstandard);
		interface_setMenuEntryLabel(menu_getLastEntry(startMenu), "ANALOG");
	}
#endif
	return 0;
}

int output_toggleDvbStartChannel(interfaceMenu_t* pMenu, void* pStartChannel)
{
	appControlInfo.offairInfo.startChannel = GET_NUMBER(pStartChannel);
	int ret = output_saveAndRedraw(saveAppSettings(), pMenu);
	if (ret)
		return ret;
	return interface_menuActionShowMenu(pMenu, pMenu->pParentMenu);
}
#endif // ENABLE_DVB_START_CHANNEL

#endif /* ENABLE_DVB */


#ifdef STB82
static int output_toggleAspectRatio(interfaceMenu_t *pMenu, void* pArg)
{
	if (appControlInfo.outputInfo.aspectRatio == aspectRatio_4x3)
		appControlInfo.outputInfo.aspectRatio = aspectRatio_16x9;
	else
		appControlInfo.outputInfo.aspectRatio = aspectRatio_4x3;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleAutoScale(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.outputInfo.autoScale = appControlInfo.outputInfo.autoScale == videoMode_scale ? videoMode_stretch : videoMode_scale;

#ifdef STB225
	gfxUseScaleParams = 0;
    (void)event_send(gfxDimensionsEvent);
#endif
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleScreenFiltration(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.outputInfo.bScreenFiltration = !appControlInfo.outputInfo.bScreenFiltration;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}
#endif // STB82

/* -------------------------- MENU DEFINITION --------------------------- */

#if (defined STB6x8x) || (defined STB225)
static void output_fillStandardMenu(void)
{
	int selected = MENU_ITEM_BACK;
	char *str;

	interfaceMenu_t *standardMenu = _M &StandardMenu;
	interface_clearMenuEntries(standardMenu);
#ifdef STB6x8x
	if (appControlInfo.outputInfo.encDesc[0].caps & DSECAPS_TV_STANDARDS)
	{
		int position = 0;
		for (int n=0; tv_standards[n].standard; n++) {
			/* the following will only work if the supported resolutions is only one value when you have a DIGITAL (HD) output.*/
			if (appControlInfo.outputInfo.encDesc[0].tv_standards & tv_standards[n].standard)
			{
				if (tv_standards[n].standard == appControlInfo.outputInfo.encConfig[0].tv_standard)
					selected = position;
#ifdef STBPNX
				str = (tv_standards[n].standard == DSETV_DIGITAL) ? (char*)resolutions[0].name : (char*) tv_standards[n].name;
#else
				str = (char*) tv_standards[n].name;
#endif
				interface_addMenuEntry(standardMenu, str, output_setStandard, (void*) tv_standards[n].standard,
				                       tv_standards[n].standard == appControlInfo.outputInfo.encConfig[0].tv_standard ? thumbnail_selected : thumbnail_tvstandard);
				position++;
			}
		}
	}
#endif // STB6x8x
#ifdef STB225
	{
		char buf[128];
		getParam("/etc/init.d/S35pnxcore.sh", "resolution", "1280x720x60p", buf);

		str = "1280x720x60p";
		interface_addMenuEntry(standardMenu, str, output_setStandard, (void*) 720, strstr(buf, str) != 0 ? thumbnail_selected : thumbnail_tvstandard);
		str = "1920x1080x60i";
		interface_addMenuEntry(standardMenu, str, output_setStandard, (void*) 1080, strstr(buf, str) != 0 ? thumbnail_selected : thumbnail_tvstandard);
		selected = strstr(buf, str) != 0;
	}
#endif // STB225
	interface_setSelectedItem(standardMenu, selected);
}
#endif // STB6x8x || STB225

static int output_setDate(interfaceMenu_t *pMenu, void* pArg)
{
	struct tm newtime;
	struct tm *lt;
	time_t t;
	char temp[5];
	if ( pArg != &DateEntry ) {
		eprintf("%s: wrong edit entry %p (should be %p)\n", __FUNCTION__, pArg, &DateEntry);
		return 1;
	}

	time(&t);
	lt = localtime(&t);
	memcpy(&newtime, lt, sizeof(struct tm));

	temp[2] = 0;
	temp[0] = DateEntry.info.date.value[0];
	temp[1] = DateEntry.info.date.value[1];
	newtime.tm_mday = atoi(temp);
	temp[0] = DateEntry.info.date.value[2];
	temp[1] = DateEntry.info.date.value[3];
	newtime.tm_mon = atoi(temp) - 1;
	temp[4] = 0;
	memcpy(temp, &DateEntry.info.date.value[4], 4);
	newtime.tm_year = atoi(temp) - 1900;
	if ( newtime.tm_year < 0 ||
	     newtime.tm_mon  < 0 || newtime.tm_mon  > 11 ||
	     newtime.tm_mday < 0 || newtime.tm_mday > 31 )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_DATE"), thumbnail_error, 0);
		return 2;
	}

	if ( lt->tm_mday != newtime.tm_mday ||
	     lt->tm_mon  != newtime.tm_mon  ||
	     lt->tm_year != newtime.tm_year )
	{
		struct timeval tv;

		tv.tv_usec = 0;
		tv.tv_sec = mktime(&newtime);

		settimeofday(&tv, NULL);
		system("hwclock -w -u");

		output_redrawMenu(pMenu);
	}
	return 0;
}

static int output_setTime(interfaceMenu_t *pMenu, void* pArg)
{
	char temp[3];
	struct tm newtime;
	struct tm *lt;
	time_t t;

	time(&t);
	lt = localtime(&t);

	memcpy(&newtime, lt, sizeof(struct tm));

	temp[2] = 0;
	temp[0] = TimeEntry.info.time.value[0];
	temp[1] = TimeEntry.info.time.value[1];
	newtime.tm_hour = atoi(temp);
	temp[0] = TimeEntry.info.time.value[2];
	temp[1] = TimeEntry.info.time.value[3];
	newtime.tm_min = atoi(temp);
	if( newtime.tm_hour < 0 || newtime.tm_hour > 23 ||
	    newtime.tm_min  < 0 || newtime.tm_min  > 59 )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_TIME"), thumbnail_error, 0);
		return 2;
	}
	if( lt->tm_hour != newtime.tm_hour ||
	    lt->tm_min  != newtime.tm_min)
	{
		struct timeval tv;

		tv.tv_usec = 0;
		tv.tv_sec = mktime(&newtime);
		settimeofday(&tv, NULL);
		system("hwclock -w -u");

		output_redrawMenu(pMenu);
	}
	return 0;
}

static int output_setTimeZone(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STBxx
	FILE *f = fopen("/config/timezone", "w");
	if (f != NULL) {
		fprintf(f, "/usr/share/zoneinfo/%s", (char*)pArg);
		fclose(f);

		system("/etc/init.d/S18timezone restart");

	} else
#endif
#ifdef STSDK
	char buf[MENU_ENTRY_INFO_LENGTH];

	unlink(TIMEZONE_FILE);
	snprintf(buf, sizeof(buf), "/usr/share/zoneinfo/%s", (char*)pArg);

	if (symlink(buf, TIMEZONE_FILE) < 0)
#endif
	{
		output_warnIfFailed(1);
	}
	tzset();
	interface_menuActionShowMenu(pMenu, &TimeSubMenu);
	return 0;
}

static char* output_getNTP(int field, void* pArg)
{
	if ( field == 0 ) {
		static char ntp[MENU_ENTRY_INFO_LENGTH] = {0};
#ifdef STBxx
		getParam(STB_CONFIG_FILE, "NTPSERVER", "", ntp);
#endif
#ifdef STSDK
		getParam(NTP_CONFIG_FILE, "NTPSERVERS", "", ntp);
#endif
		return ntp;
	} else
		return NULL;
}

static int output_setNTP(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if ( value == NULL )
		return 1;

#ifdef STBxx
	if ( setParam(STB_CONFIG_FILE, "NTPSERVER", value) != 0 ) {
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	} else {
		system("/usr/sbin/ntpupdater");
		output_refillMenu(pMenu);
	}
#endif
#ifdef STSDK
	if ( setParam(NTP_CONFIG_FILE, "NTPSERVERS", value) != 0 ) {
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	} else {
		interface_showLoading();
		interface_hideMessageBox();

		if (*value) {
			setParam(NTP_CONFIG_FILE, "NTPD",    "yes");
			setParam(NTP_CONFIG_FILE, "NTPDATE", "yes");
		} else {
			setParam(NTP_CONFIG_FILE, "NTPD",    "no");
			setParam(NTP_CONFIG_FILE, "NTPDATE", "no");
		}
		system("/etc/init.d/S85ntp stop");
		system("/etc/init.d/S85ntp start");

		
		interface_hideLoading();
		output_redrawMenu(pMenu);
	}
#endif

	return 0;
}

static int output_changeNTP(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ENTER_NTP_ADDRESS"), "\\w+", output_setNTP, output_getNTP, inputModeABC, pArg);
}

static void output_fillTimeZoneMenu(void)
{
	char *str;
	char buf[BUFFER_SIZE];
	int selected = 21; // GMT+0

	interfaceMenu_t *tzMenu = _M &TimeZoneMenu;
	interface_clearMenuEntries(tzMenu);

#ifdef STBxx
	if (!helperParseLine(INFO_TEMP_FILE, "cat /config/timezone",    "zoneinfo/", buf, 0))
#endif
#ifdef STSDK
	if (!helperParseLine(INFO_TEMP_FILE, "readlink " TIMEZONE_FILE, "zoneinfo/", buf, 0))
#endif
	{
		buf[0] = 0;
	}

	for (size_t i=0; i<(sizeof(timezones)/sizeof(timezones[0])); i++) {
		str = timezones[i].desc;
		interface_addMenuEntry(tzMenu, str, output_setTimeZone, (void*)timezones[i].file, thumbnail_log);
		if (strcmp(timezones[i].file, buf) == 0)
			selected = i;
	}

	interface_setSelectedItem(tzMenu, selected);
}

#ifdef STB82
/**
 * This function now uses the Encoder API to set the output format instead of the Output API.
 */
static int output_setFormat(interfaceMenu_t *pMenu, void* pArg)
{
	int format = GET_NUMBER(pArg);

	gfx_changeOutputFormat(format);
	appControlInfo.outputInfo.format = format;
	output_warnIfFailed(saveAppSettings());
    output_fillFormatMenu();
	interface_displayMenu(1);

    return 0;
}

/**
 * This function now uses the Encoder description to get the supported outout formats instead of the Output API.
 */
static void output_fillFormatMenu(void)
{
	int selected = MENU_ITEM_BACK;
	interfaceMenu_t *formatMenu = _M &FormatMenu;
	interface_clearMenuEntries(formatMenu);
	int n = 0;
	int position = 0;
	char *signalName;
	int signalEnable;

	/*Add menu items automatically*/
	if (appControlInfo.outputInfo.encDesc[0].caps & DSECAPS_OUT_SIGNALS) {
		for (n=0; signals[n].signal; n++) {
			if (appControlInfo.outputInfo.encDesc[0].out_signals & signals[n].signal)
			{
				switch( signals[n].signal )
				{
					case DSOS_YC:
						signalName = "S-Video";
						signalEnable = 1;
						break;
					case DSOS_RGB:
#ifndef HIDE_EXTRA_FUNCTIONS
						signalName = "SCART-RGB";
						signalEnable = 0;
						break;
#else
						continue;
#endif
					case DSOS_YCBCR:
#ifndef HIDE_EXTRA_FUNCTIONS
						signalName = "SCART-YUYV";
						signalEnable = 0;
						break;
#else
						continue;
#endif
					case DSOS_CVBS:
						signalName = "CVBS";
						signalEnable = 1;
						break;
					default:
						signalName = (char*)signals[n].name;
						signalEnable = 0;
				}
				if (signals[n].signal == appControlInfo.outputInfo.encConfig[0].out_signals)
				{
					selected = position;
				}
				interface_addMenuEntry2(_M &FormatMenu, signalName, signalEnable,
				                             output_setFormat, SET_NUMBER(signals[n].signal),
				                             signals[n].signal == appControlInfo.outputInfo.encConfig[0].out_signals ? thumbnail_selected : thumbnail_channels);
				position++;
			}
		}
	}

	interface_setSelectedItem(formatMenu, selected);
}
#endif // STB82

static int output_applyGraphicsMode(void *notused)
{
	gfx_stopEventThread();
	gfx_terminate();
#ifdef STSDK
	st_reinitFb(p_mainVideoOutput ? p_mainVideoOutput->currentFormat : "");
#endif
	interface_hideLoading();
	gfx_init(0, NULL);
	interface_resize();
	gfx_startEventThread();
	interface_displayMenu(1);
	return 0;
}

static int output_setGraphicsMode(interfaceMenu_t *pMenu, void *pModeStr)
{
	strncpy(appControlInfo.outputInfo.graphicsMode, pModeStr, sizeof(appControlInfo.outputInfo.graphicsMode)-1);
	appControlInfo.outputInfo.graphicsMode[sizeof(appControlInfo.outputInfo.graphicsMode)-1] = 0;
	saveAppSettings();
	output_refillMenu(pMenu);

	int width = 720, height = 576;
	if (sscanf(pModeStr, "%dx%d", &width, &height) != 2) {
#ifdef STSDK
		st_getFormatResolution(p_mainVideoOutput ? p_mainVideoOutput->currentFormat : "", &width, &height);
#endif
	}
	if (width  == interfaceInfo.screenWidth &&
		height == interfaceInfo.screenHeight) {
		interface_displayMenu(1);
		return 0;
	}
	interface_showLoading();
	interface_displayMenu(1);
	interface_addEvent(output_applyGraphicsMode, NULL, 0, 1);
	return 0;
}

static int output_enterGraphicsModeMenu(interfaceMenu_t *graphicsMenu, void *pArg)
{
#ifdef STSDK
	int new_height, height;
	st_getFormatResolution(p_mainVideoOutput ? p_mainVideoOutput->currentFormat : "", &new_height, &height);
#endif
	interface_clearMenuEntries(graphicsMenu);
	for (int i = 0; i < GRAPHICS_MODE_COUNT; i++) {
		int selected = !strcmp(appControlInfo.outputInfo.graphicsMode, output_graphicsModes[i].mode);
#ifdef STSDK
		new_height = 0;
		// setting 1080p mode on SD resolutions cause application to crash
		sscanf(output_graphicsModes[i].mode, "%*dx%d", &new_height);
		if (height < 720 && new_height > 720)
			continue;
#endif
		interface_addMenuEntry(graphicsMenu, _T(output_graphicsModes[i].name), output_setGraphicsMode, (void*)output_graphicsModes[i].mode, selected ? radiobtn_filled : radiobtn_empty);
		if (selected)
			interface_setSelectedItem(graphicsMenu, i);
	}
	return 0;
}

int output_setZoom(zoomPreset_t preset)
{
	appControlInfo.playbackInfo.zoom = preset;
#ifdef STSDK
	return st_applyZoom(appControlInfo.playbackInfo.zoom);
#endif
	return 0;
}

int output_toggleZoom()
{
	return output_setZoom((appControlInfo.playbackInfo.zoom+1)%zoomPresetsCount);
}

static long get_info_progress()
{
	return info_progress;
}

int show_info(interfaceMenu_t* pMenu, void* pArg)
{
	char buf[256];
	char temp[256];
	char info_text[4096];

	info_text[0] = 0;
	info_progress = 0;

	eprintf("output: Start collecting info...\n");
	interface_sliderSetText( _T("COLLECTING_INFO"));
	interface_sliderSetMinValue(0);
	interface_sliderSetMaxValue(9);
	interface_sliderSetCallbacks(get_info_progress, NULL, NULL);
	interface_sliderSetDivisions(100);
	interface_sliderShow(1, 1);
#ifdef STBxx
	char *vendor;
	fd = open("/proc/nxp/drivers/pnx8550/video/renderer0/output_resolution", O_RDONLY);
	if (fd > 0) {
		vendor = "nxp";
		close(fd);
	} else
		vendor = "philips";
#ifdef STB82
	systemId_t sysid;
	systemSerial_t serial;
	unsigned long stmfw;

	if (helperParseLine(INFO_TEMP_FILE, "cat /dev/sysid", "SERNO: ", buf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
		serial.SerialFull = strtoul(buf, NULL, 16);
	else
		serial.SerialFull = 0;

	if (helperParseLine(INFO_TEMP_FILE, NULL, "SYSID: ", buf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
		sysid.IDFull = strtoul(buf, NULL, 16);
	else
		sysid.IDFull = 0;

	if (helperParseLine(INFO_TEMP_FILE, NULL, "VER: ", buf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
		stmfw = strtoul(buf, NULL, 16);
	else
		stmfw = 0;

	get_composite_serial(sysid, serial, temp);
	sprintf(info_text,"%s: %s\n",_T("SERIAL_NUMBER"),temp);
	if (stmfw > 0x0106 && helperParseLine(INFO_TEMP_FILE, "stmclient 7", "MAC: ", buf, ' ')) // MAC: 02:EC:D0:00:00:39
	{
		char mac[6];
		sscanf(buf, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		sprintf(temp, "%s 1: %02hhX%02hhX%02hhX%02hhX%02hhX%02hhX\n", _T("MAC_ADDRESS"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else
		sprintf(temp, "%s 1: %s\n", _T("MAC_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
	strcat(info_text, temp);

	if (stmfw > 0x0106 && helperParseLine(INFO_TEMP_FILE, "stmclient 8", "MAC: ", buf, ' ')) // MAC: 02:EC:D0:00:00:39
	{
		char mac[6];
		sscanf(buf, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		sprintf(temp, "%s 2: %02hhX%02hhX%02hhX%02hhX%02hhX%02hhX\n", _T("MAC_ADDRESS"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else
		sprintf(temp, "%s 2: %s\n", _T("MAC_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
	strcat(info_text, temp);

	strcat(info_text,_T("STM_VERSION"));
	strcat(info_text,": ");
	if (stmfw != 0) {
		sprintf(temp, "%lu.%lu", (stmfw >> 8)&0xFF, (stmfw)&0xFF);
		strcat(info_text, temp);
	} else
		strcat(info_text, _T("NOT_AVAILABLE_SHORT"));
	strcat(info_text, "\n");

	strcat(info_text, _T("SERIAL_NUMBER_OLD"));
	strcat(info_text,": ");
	if (serial.SerialFull != 0)
		sprintf(temp, "%u\n", serial.SerialFull);
	else
		sprintf(temp, "%s\n", _T("NOT_AVAILABLE_SHORT"));
	strcat(info_text, temp);

	strcat(info_text, _T("SYSID"));
	strcat(info_text,": ");
	if (sysid.IDFull != 0) {
		get_system_id(sysid, temp);
		strcat(temp,"\n");
	} else
		sprintf(temp, "%s\n", _T("NOT_AVAILABLE_SHORT"));

	strcat(info_text, temp);
	info_progress++;
	interface_displayMenu(1);
#endif // STB82
	sprintf(temp, "%s: %s\n%s: %s\n%s: %s\n", _T("APP_RELEASE"), RELEASE_TYPE, _T("APP_VERSION"), REVISION, _T("COMPILE_TIME"), COMPILE_TIME);
#endif // STBxx
#ifdef STSDK
	sprintf(temp, "%s: %s\n%s: %s\n", _T("APP_RELEASE"), RELEASE_TYPE, _T("COMPILE_TIME"), COMPILE_TIME);
#endif
	strcat(info_text,temp);

#ifdef STB82
	sprintf(buf, "%s: %d MB\n", _T("MEMORY_SIZE"), appControlInfo.memSize);
	strcat(info_text, buf);
#endif
	if (helperParseLine(INFO_TEMP_FILE, "date -R", "", buf, '\r'))
		sprintf(temp, "%s: %s\n\n",_T("CURRENT_TIME"), buf);
	else
		sprintf(temp, "%s: %s\n\n",_T("CURRENT_TIME"), _T("NOT_AVAILABLE_SHORT"));
	strcat(info_text, temp);

	info_progress++;
	interface_displayMenu(1);

    outputNetwork_showNetwork(info_text);

	info_progress++;
	interface_displayMenu(1);

#if 0
	strcat(info_text, "\n");

	/* prefetch messages for speedup */
	system("cat /var/log/messages | grep -e \"ate wi\" -e \"TM . n\" > /tmp/tmmsg");
	system("mkdir -p /tmp/dp");
	sprintf(buf, "cat /proc/%s/dp0 >> /tmp/dp/dp0", vendor);
	system(buf);
	sprintf(buf, "cat /proc/%s/dp1 >> /tmp/dp/dp1", vendor);
	system(buf);
	system("cat /tmp/dp/dp0 | grep -e \"\\$Build: \" -e \"CPU RSE\" > /tmp/tm0dp");
	system("cat /tmp/dp/dp1 | grep -e \"\\$Build: \" -e \"CPU RSE\" > /tmp/tm1dp");

	info_progress++;
	interface_displayMenu(1);

	if (helperParseLine(INFO_TEMP_FILE, "cat /tmp/dp/dp0init | grep \"\\$Build: \"", "$Build: ", buf, '$'))
	{
		strcat(info_text, LANG_TM0_IMAGE ": ");
		strcat(info_text, buf);
		strcat(info_text, "\n");
	} else
		strcat(info_text, LANG_TM0_IMAGE ": " LANG_NOT_AVAILABLE_SHORT "\n");

	info_progress++;
	interface_displayMenu(1);

	if (helperParseLine(INFO_TEMP_FILE, "cat /tmp/tm0dp | grep \"CPU RSE\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"unable to communicate with TriMedia 0\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"TM 0 not ready\"", NULL, NULL, ' '))
	{
		strcat(info_text, LANG_TM0_STATUS ": " LANG_FAIL "\n");
	} else
		strcat(info_text, LANG_TM0_STATUS ": " LANG_OK "\n");

	info_progress++;
	interface_displayMenu(1);

	if (helperParseLine(INFO_TEMP_FILE, "cat /tmp/dp/dp1init | grep \"\\$Build: \"", "$Build: ", buf, '$'))
	{
		strcat(info_text, LANG_TM1_IMAGE ": ");
		strcat(info_text, buf);
		strcat(info_text, "\n");
	} else
		strcat(info_text, LANG_TM1_IMAGE ": " LANG_NOT_AVAILABLE_SHORT "\n");

	info_progress++;
	interface_displayMenu(1);

	if (helperParseLine(INFO_TEMP_FILE, "cat /tmp/tm1dp | grep \"CPU RSE\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"unable to communicate with TriMedia 1\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"TM 1 not ready\"", NULL, NULL, ' '))
	{
		strcat(info_text, LANG_TM1_STATUS ": " LANG_FAIL "\n");
	} else
		strcat(info_text, LANG_TM1_STATUS ": " LANG_OK "\n");

	info_progress++;
	interface_displayMenu(1);

	sprintf(buf, "cat /proc/%s/drivers/pnx8550/video/renderer0/output_resolution", vendor);
	if (helperParseLine(INFO_TEMP_FILE, buf, NULL, buf, ' '))
	{
		strcat(info_text, LANG_RESOLUTION ": ");
		strcat(info_text, buf);
	} else
		strcat(info_text, LANG_RESOLUTION ": " LANG_NOT_AVAILABLE_SHORT);
#endif // #if 0
	info_progress++;
	interface_displayMenu(1);
	interface_sliderShow(0, 0);
	eprintf("output: Done collecting info.\n---------------------------------------------------\n%s\n---------------------------------------------------\n", info_text);
	helperFlushEvents();
	interface_showScrollingBox( info_text, thumbnail_info, NULL, NULL );
	return 0;
}

long output_getColorValue(void *pArg)
{
	int iarg = GET_NUMBER(pArg);
	DFBColorAdjustment adj;
	IDirectFBDisplayLayer *layer = gfx_getLayer(gfx_getMainVideoLayer());

	adj.flags = DCAF_ALL;
	layer->GetColorAdjustment(layer, &adj);

	switch (iarg)
	{
		case colorSettingContrast:   return adj.contrast;
		case colorSettingBrightness: return adj.brightness;
		case colorSettingHue:        return adj.hue;
		default:                     return adj.saturation;
	}
	return 0;
}


void output_setColorValue(long value, void *pArg)
{
	int iarg = GET_NUMBER(pArg);
	DFBColorAdjustment adj;
	IDirectFBDisplayLayer *layer = gfx_getLayer(gfx_getMainVideoLayer());
	switch (iarg)
	{
		case colorSettingContrast:
			adj.flags = DCAF_CONTRAST;
			adj.contrast = value;
			appControlInfo.pictureInfo.contrast = value;
			break;
		case colorSettingBrightness:
			adj.flags = DCAF_BRIGHTNESS;
			adj.brightness = value;
			appControlInfo.pictureInfo.brightness = value;
			break;
		case colorSettingHue:
			adj.flags = DCAF_HUE;
			adj.hue = value;
			break;
		default:
			adj.flags = DCAF_SATURATION;
			adj.saturation = value;
			appControlInfo.pictureInfo.saturation = value;
	}
	layer->SetColorAdjustment(layer, &adj);
}

int output_showColorSlider(interfaceMenu_t *pMenu, void* pArg)
{
	if (interfacePlayControl.activeButton == interfacePlayControlStop &&
	    appControlInfo.slideshowInfo.state == slideshowDisabled &&
	    appControlInfo.slideshowInfo.showingCover == 0)
	{
		gfx_decode_and_render_Image_to_layer(IMAGE_DIR "wallpaper_test.png", screenMain);
		gfx_showImage(screenMain);
	}
	interface_playControlDisable(0);

	interface_showMenu(0, 0);
	output_colorSliderUpdate(pArg);

	return 0;
}

int output_colorCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int iarg = GET_NUMBER(pArg);

	switch( cmd->command )
	{
		case interfaceCommandOk:
		case interfaceCommandEnter:
		case interfaceCommandUp:
		case interfaceCommandYellow:
		case interfaceCommandBlue:
			iarg = (iarg + 1) % colorSettingCount;
			output_colorSliderUpdate(SET_NUMBER(iarg));
			return 0;
		case interfaceCommandDown:
			iarg = (iarg + colorSettingCount - 1) % colorSettingCount;
			output_colorSliderUpdate(SET_NUMBER(iarg));
			return 0;
		case interfaceCommandBack:
		case interfaceCommandRed:
		case interfaceCommandGreen:
			cmd->command = interfaceCommandExit;
			interface_processCommand(cmd);
			return 1;
		case interfaceCommandExit:
			interfacePlayControl.enabled = 1;
			output_warnIfFailed(saveAppSettings());
			//dprintf("%s: b=%d h=%d s=%d\n", __FUNCTION__, appCon);
			return 0;
		default:
			return 0;
	}
}

static void output_colorSliderUpdate(void *pArg)
{
	int iarg = GET_NUMBER(pArg);
	char *param;
	char installationString[2048];

	switch (iarg)
	{
		case colorSettingContrast:
			param = _T("CONTRAST");
			break;
		case colorSettingBrightness:
			param = _T("BRIGHTNESS");
			break;
		case colorSettingHue:
			param = _T("HUE");
			break;
		default:
			iarg = colorSettingSaturation;
			param = _T("SATURATION");
	}

	sprintf(installationString, "%s", param);

	interface_sliderSetText(installationString);
	interface_sliderSetMinValue(0);
	interface_sliderSetMaxValue(0xFFFF);
	interface_sliderSetCallbacks(output_getColorValue, output_setColorValue, pArg);
	interface_sliderSetDivisions(COLOR_STEP_COUNT);
	interface_sliderSetHideDelay(10);
	interface_sliderSetKeyCallback(output_colorCallback);
	interface_sliderShow(2, 1);
}

#ifdef ENABLE_PASSWORD
static int output_askPassword(interfaceMenu_t *pMenu, void* pArg)
{
	char passwd[MAX_PASSWORD_LENGTH];
	FILE *file = NULL;
	menuActionFunction pAction = (menuActionFunction)pArg;

	file = popen("hwconfigManager a -1 PASSWORD 2>/dev/null | grep \"VALUE:\" | sed 's/VALUE: \\(.*\\)/\\1/'","r");
	if( file != NULL )
	{
		if ( fgets(passwd, MAX_PASSWORD_LENGTH, file) != NULL && passwd[0] != 0 && passwd[0] != '\n' )
		{
			fclose(file);
			return interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\w+", output_enterPassword, NULL, inputModeDirect, pArg);
		}
	}

	return pAction(pMenu, NULL);
}

static int output_enterPassword(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if ( value == NULL || pArg == NULL )
		return 1;

	menuActionFunction pAction = (menuActionFunction)pArg;
	char passwd[MAX_PASSWORD_LENGTH];

	FILE *file = popen("hwconfigManager a -1 PASSWORD 2>/dev/null | grep \"VALUE:\" | sed 's/VALUE: \\(.*\\)/\\1/'","r");
	if ( file != NULL && fgets(passwd, MAX_PASSWORD_LENGTH, file) != NULL && passwd[0] != 0 && passwd[0] != '\n' )
	{
		char inpasswd[MAX_PASSWORD_LENGTH];
		unsigned char passwdsum[16];

		fclose(file);
		if( passwd[strlen(passwd)-1] == '\n')
			passwd[strlen(passwd)-1] = 0;
		/* Get MD5 sum of input and convert it to hex string */
		md5((unsigned char*)value, strlen(value), passwdsum);
		for (int i=0;i<16;i++)
			sprintf(&inpasswd[i*2], "%02hhx", passwdsum[i]);
		dprintf("%s: Passwd #1: %s/%s\n", __FUNCTION__,passwd, inpasswd);
		if (strcmp( passwd, inpasswd ) != 0) {
			interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
			return 1;
		}
	} else
	{
		if (file != NULL)
			fclose(file);

		file = popen("cat /dev/sysid | sed 's/.*SERNO: \\(.*\\), .*/\\1/'","r");
		if ( file != NULL && fgets(passwd, MAX_PASSWORD_LENGTH, file) != NULL && passwd[0] != 0 && passwd[0] != '\n' )
		{
			unsigned long id, intpwd;

			fclose(file);
			if( passwd[strlen(passwd)-1] == '\n')
				passwd[strlen(passwd)-1] = 0;

			id = strtoul(passwd, NULL, 16);
			intpwd = strtoul(value, NULL, 10);
			dprintf("%s: Passwd #2: %lu/%lu\n", __FUNCTION__, id, intpwd);

			if( intpwd != id ) {
				interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
				return 1;
			}
		} else {
			if (file != NULL)
				fclose(file);

			dprintf("%s: Warning: can't determine system password!\n", __FUNCTION__);
		}
	}
	return pAction(pMenu, NULL);
}

int output_showNetworkMenu(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_menuActionShowMenu(pMenu, outputNetwork_getMenu());
}
#endif // ENABLE_PASSWORD


#ifdef ENABLE_ANALOGTV
int analogtv_setRange(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int option = GET_NUMBER(pArg);
	uint32_t val;
	if (!value) return 1;

	val = strtoul (value, NULL, 10);
	if (option == 0) { // low
		if (val != appControlInfo.tvInfo.lowFrequency){
			if (val < ATV_MIN_FREQUENCY) {
				appControlInfo.tvInfo.lowFrequency = ATV_MIN_FREQUENCY;
			}
			else {
				appControlInfo.tvInfo.lowFrequency = val;
			}
			eprintf ("%s: from_freq = %d KHz\n", __FUNCTION__, appControlInfo.tvInfo.lowFrequency);
		}
	}
	else {
		if (val != appControlInfo.tvInfo.highFrequency){
			if (val > ATV_MAX_FREQUENCY) {
				appControlInfo.tvInfo.highFrequency = ATV_MAX_FREQUENCY;
			}
			else {
				appControlInfo.tvInfo.highFrequency = val;
			}
			eprintf ("%s: to_freq = %d KHz\n", __FUNCTION__, appControlInfo.tvInfo.highFrequency);
		}
	}
	interface_displayMenu(1);
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static char* analogtv_getRange (int index, void* pArg)
{
	if (index == 0){
		static char buffer[32];
		int id = GET_NUMBER(pArg);
		if (id == 0) sprintf(buffer, "%u", appControlInfo.tvInfo.lowFrequency);
		else sprintf (buffer, "%u", appControlInfo.tvInfo.highFrequency);
		return buffer;
	}
	return NULL;
}

int analogtv_changeAnalogLowFreq(interfaceMenu_t * pMenu, void *pArg)
{
	if (!pArg) return -1;
	appControlInfo.tvInfo.lowFrequency = *((uint32_t *)pArg);

	char buf[MENU_ENTRY_INFO_LENGTH];
	sprintf(buf, "%s, kHz: ", _T("ANALOGTV_LOW_FREQ"));

	return interface_getText(pMenu, buf, "\\d+", analogtv_setRange, analogtv_getRange, 0, 0);
}

int analogtv_changeAnalogHighFreq(interfaceMenu_t * pMenu, void *pArg)
{
	if (!pArg) return -1;
	appControlInfo.tvInfo.highFrequency = *((uint32_t *)pArg);

	char buf[MENU_ENTRY_INFO_LENGTH];
	sprintf(buf, "%s, kHz: ", _T("ANALOGTV_HIGH_FREQ"));

	return interface_getText(pMenu, buf, "\\d+", analogtv_setRange, analogtv_getRange, 0, (void *)1);
}

char *analogtv_delSysName[] = {
	[TV_SYSTEM_PAL]		= "PAL",
	[TV_SYSTEM_SECAM]	= "SECAM",
	[TV_SYSTEM_NTSC]	= "NTSC",
};

static int analogtv_changeAnalogDelSys(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.tvInfo.delSys++;
	if(appControlInfo.tvInfo.delSys > TV_SYSTEM_NTSC) {
		appControlInfo.tvInfo.delSys = TV_SYSTEM_PAL;
	}

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

char *analogtv_audioName[] = {
	[TV_AUDIO_SIF]	= "sif",
	[TV_AUDIO_AM]	= "am",
	[TV_AUDIO_FM1]	= "fm1",
	[TV_AUDIO_FM2]	= "fm2",
};

static int analogtv_changeAnalogAudio(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.tvInfo.audioMode++;
	if(appControlInfo.tvInfo.audioMode > TV_AUDIO_FM2) {
		appControlInfo.tvInfo.audioMode = TV_AUDIO_SIF;
	}

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_enterAnalogTvMenu(interfaceMenu_t *pMenu, void* notused)
{
	interfaceMenu_t * tvMenu = &AnalogTvSubMenu.baseMenu;
	char buf[MENU_ENTRY_INFO_LENGTH];
	char * str;
	
	interface_clearMenuEntries(tvMenu);

	str = _T("ANALOGTV_SCAN_ALL");
	interface_addMenuEntry(tvMenu, str, analogtv_serviceScan, NULL, thumbnail_scan);

	str = _T("ANALOGTV_SCAN_RANGE");
	interface_addMenuEntry(tvMenu, str, analogtv_serviceScan, NULL, thumbnail_scan);
	
	sprintf(buf, "%s: %u kHz", _T("ANALOGTV_LOW_FREQ"), appControlInfo.tvInfo.lowFrequency);
	interface_addMenuEntry(tvMenu, buf, analogtv_changeAnalogLowFreq, &(appControlInfo.tvInfo.lowFrequency), thumbnail_configure);  // SET_NUMBER(optionLowFreq)

	sprintf(buf, "%s: %u kHz", _T("ANALOGTV_HIGH_FREQ"), appControlInfo.tvInfo.highFrequency);
	interface_addMenuEntry(tvMenu, buf, analogtv_changeAnalogHighFreq, &(appControlInfo.tvInfo.highFrequency), thumbnail_configure); // SET_NUMBER(optionHighFreq)

	sprintf(buf, "%s: %s", _T("ANALOGTV_DELSYS"), analogtv_delSysName[appControlInfo.tvInfo.delSys]);
	interface_addMenuEntry(tvMenu, buf, analogtv_changeAnalogDelSys, NULL, thumbnail_configure); // SET_NUMBER(optionHighFreq

	sprintf(buf, "%s: %s", _T("ANALOGTV_AUDIO_MODE"), analogtv_audioName[appControlInfo.tvInfo.audioMode]);
	interface_addMenuEntry(tvMenu, buf, analogtv_changeAnalogAudio, NULL, thumbnail_configure);

	sprintf(buf, "%s (%d)", _T("ANALOGTV_CLEAR"), analogtv_getChannelCount(0)); //analogtv_service_count
	interface_addMenuEntry(tvMenu, buf, analogtv_clearServiceList, (void *)1, thumbnail_scan);
	
	return 0;
}
#endif //#ifdef ENABLE_ANALOGTV


int output_enterVideoMenu(interfaceMenu_t *videoMenu, void* notused)
{
#ifdef STB82
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str;

	// assert (videoMenu == (interfaceMenu_t*)&VideoSubMenu);
	interface_clearMenuEntries(videoMenu);

	interface_addMenuEntry(videoMenu, _T("TV_STANDARD"), interface_menuActionShowMenu, &StandardMenu, thumbnail_tvstandard);
	/* We only enable this menu when we are outputting SD and we do not only have the HD denc. (HDMI is not denc[0])*/
	if(!(gfx_isHDoutput()) && !(appControlInfo.outputInfo.encDesc[0].all_connectors & DSOC_HDMI)) {
		interface_addMenuEntry(videoMenu, _T("TV_FORMAT"), interface_menuActionShowMenu, &FormatMenu, thumbnail_channels);

		/*Only add slow blanking if we have the capability*/
		if (appControlInfo.outputInfo.encDesc[0].caps & DSOCAPS_SLOW_BLANKING)
			interface_addMenuEntry(videoMenu, _T("TV_BLANKING"), interface_menuActionShowMenu, &BlankingMenu, thumbnail_configure);
	}
	interface_addMenuEntry(videoMenu, _T("INTERFACE_SIZE"), interface_menuActionShowMenu, &GraphicsModeMenu, settings_interface);

	str = appControlInfo.outputInfo.aspectRatio == aspectRatio_16x9 ? "16:9" : "4:3";
	sprintf(buf, "%s: %s", _T("ASPECT_RATIO"), str);
	interface_addMenuEntry(videoMenu, buf, output_toggleAspectRatio, NULL, thumbnail_channels);

	str = appControlInfo.outputInfo.autoScale == videoMode_scale ? _T("AUTO_SCALE_ENABLED") : _T("AUTO_STRETCH_ENABLED");
	sprintf(buf, "%s: %s", _T("SCALE_MODE"), str);
	interface_addMenuEntry(videoMenu, buf, output_toggleAutoScale, NULL, settings_size);
#ifndef HIDE_EXTRA_FUNCTIONS
	sprintf(buf, "%s: %s", _T("AUDIO_OUTPUT"), appControlInfo.soundInfo.rcaOutput == 0 ? "SCART" : "RCA");
	interface_addMenuEntry(videoMenu, buf, output_toggleAudio, NULL, thumbnail_sound);
#endif
	sprintf(buf, "%s: %s", _T("SCREEN_FILTRATION"), _T( appControlInfo.outputInfo.bScreenFiltration ? "ON" : "OFF" ));
	interface_addMenuEntry(videoMenu, buf, output_toggleScreenFiltration, NULL, thumbnail_channels);

	str = _T("COLOR_SETTINGS");
	interface_addMenuEntry(videoMenu, str, output_showColorSlider, NULL, thumbnail_tvstandard);
#endif // STB82
#ifdef STSDK
	static uint8_t	initialized = 0;

	if(!initialized) {
		elcdRpcType_t	type;
		cJSON			*list = NULL;
		int32_t			ret;

		interface_clearMenuEntries(videoMenu);

		ret = st_rpcSync(elcmd_listvoutput, NULL, &type, &list);
		if(ret == 0 && type == elcdRpcResult && list && list->type == cJSON_Array) {
			uint32_t	i;
//			uint32_t	j = 0;
			cJSON		*output;

			for(i = 0; (output = cJSON_GetArrayItem(list, i)) != NULL; i++) {
				videoOutput_t	*p_videoOutput;
				char			*name;
				char			*str;
				char			buf[MENU_ENTRY_INFO_LENGTH];

				name = objGetString(output, "name", NULL);
				if(!name)
					continue;

				//TODO: dont forget to free it somewhere
				p_videoOutput = malloc(sizeof(videoOutput_t));
				if(p_videoOutput == NULL) {
					eprintf("%s: ERROR: Cant allocate memory\n", __FUNCTION__);
					continue;
				}
				memset(p_videoOutput, 0 , sizeof(videoOutput_t));

				strncpy(p_videoOutput->name, name, sizeof(p_videoOutput->name));
				p_videoOutput->name[sizeof(p_videoOutput->name) - 1] = 0;

				if(objCheckIfTrue(output, "major")) {
					p_mainVideoOutput = p_videoOutput;
					p_videoOutput->isMajor = 1;
				}
				if(objCheckIfTrue(output, "feedback")) {
					p_videoOutput->hasFeedback = 1;
				}

				snprintf(buf, sizeof(buf), "TV_FORMAT_%s", name);
				str = _T(buf);
				createListMenu(&(p_videoOutput->menu), str, settings_video, NULL, videoMenu,
								interfaceListMenuIconThumbnail, output_enterVideoOutputMenu, NULL, p_videoOutput);
				interface_addMenuEntry(videoMenu, str, interface_menuActionShowMenu, &(p_videoOutput->menu), thumbnail_channels);
			}
		} else {
			eprintf("%s: failed to get video outputs\n", __FUNCTION__);
		}
		cJSON_Delete(list);

		interface_addMenuEntry(videoMenu, _T("INTERFACE_SIZE"), interface_menuActionShowMenu, &GraphicsModeMenu, settings_interface);

		initialized = 1;
	}
#endif
	return 0;
}

#ifdef ENABLE_3D
static int output_toggle3DMonitor(interfaceMenu_t *pMenu, void* pArg) {
	interfaceInfo.enable3d = !interfaceInfo.enable3d;

#if defined(STB225)
	if(interfaceInfo.mode3D==0 || interfaceInfo.enable3d==0) {
		Stb225ChangeDestRect("/dev/fb0", 0, 0, 1920, 1080);
	} else	{
		Stb225ChangeDestRect("/dev/fb0", 0, 0, 960, 1080);
	}
#endif
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggle3DContent(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.content3d = (appControlInfo.outputInfo.content3d + 1) % 3;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggle3DFormat(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.format3d = (appControlInfo.outputInfo.format3d + 1) % 3;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleUseFactor(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.use_factor = !appControlInfo.outputInfo.use_factor;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_toggleUseOffset(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.use_offset = !appControlInfo.outputInfo.use_offset;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

char *output_get3DFactor(int index, void* pArg) {
	if( index == 0 ) {
		static char temp[8];
		sprintf(temp, "%03d", appControlInfo.outputInfo.factor);
		return temp;
	} else return NULL;
}

static int output_set3DFactor(interfaceMenu_t *pMenu, char *value, void* pArg) {
	if( value != NULL && value[0] != 0)  {
		int ivalue = atoi(value);
		if (ivalue < 0 || ivalue > 255) ivalue = 255;

		appControlInfo.outputInfo.factor = ivalue;
	}
	output_refillMenu(pMenu);
	return output_warnIfFailed(saveAppSettings());
}

char *output_get3DOffset(int index, void* pArg) {
	if( index == 0 ) {
		static char temp[8];
		sprintf(temp, "%03d", appControlInfo.outputInfo.offset);
		return temp;
	} else	return NULL;
}

static int output_set3DOffset(interfaceMenu_t *pMenu, char *value, void* pArg) {
	if( value != NULL && value[0] != 0)  {
		int ivalue = atoi(value);
		if (ivalue < 0 || ivalue > 255) ivalue = 255;

		appControlInfo.outputInfo.offset = ivalue;
	}
	output_refillMenu(pMenu);
	return output_warnIfFailed(saveAppSettings());
}

static int output_change3DFactor(interfaceMenu_t *pMenu, void* pArg) {
	return interface_getText(pMenu, _T("3D_FACTOR"), "\\d{3}", output_set3DFactor, output_get3DFactor, inputModeDirect, pArg);
}
static int output_change3DOffset(interfaceMenu_t *pMenu, void* pArg) {
	return interface_getText(pMenu, _T("3D_OFFSET"), "\\d{3}", output_set3DOffset, output_get3DOffset, inputModeDirect, pArg);
}

int output_enter3DMenu(interfaceMenu_t *video3dMenu, void* pArg)
{
	char *str;
	char buf[MENU_ENTRY_INFO_LENGTH];
	
	char *chContent[] = {_T("VIDEO"), _T("3D_SIGNAGE"), _T("3D_STILLS")};
	char *chFormat [] = {_T("3D_2dZ"), _T("3D_DECLIPSE_RD"), _T("3D_DECLIPSE_FULL")};

	interface_clearMenuEntries(video3dMenu);

	sprintf(buf, "%s: %s", _T("3D_MONITOR"), _T( interfaceInfo.enable3d ? "ON" : "OFF" ));
	interface_addMenuEntry(video3dMenu, buf, output_toggle3DMonitor, NULL, thumbnail_channels);

	str = chContent[appControlInfo.outputInfo.content3d];
	sprintf(buf, "%s: %s", _T("3D_CONTENT"), str);
	interface_addMenuEntry(video3dMenu, buf, output_toggle3DContent, NULL, thumbnail_channels);

	str = chFormat[appControlInfo.outputInfo.format3d];
	sprintf(buf, "%s: %s", _T("3D_FORMAT"), str);
	interface_addMenuEntry(video3dMenu, buf, output_toggle3DFormat, NULL, thumbnail_channels);

	sprintf(buf, "%s: %s", _T("3D_FACTOR_FLAG"), _T( appControlInfo.outputInfo.use_factor ? "ON" : "OFF" ));
	interface_addMenuEntry(video3dMenu, buf, output_toggleUseFactor, NULL, thumbnail_channels);

	sprintf(buf, "%s: %s", _T("3D_OFFSET_FLAG"), _T( appControlInfo.outputInfo.use_offset ? "ON" : "OFF" ));
	interface_addMenuEntry(video3dMenu, buf, output_toggleUseOffset, NULL, thumbnail_channels);

	sprintf(buf, "%s: %d", _T("3D_FACTOR"), appControlInfo.outputInfo.factor);
	interface_addMenuEntry(video3dMenu, buf, output_change3DFactor, NULL, thumbnail_channels);

	sprintf(buf, "%s: %d", _T("3D_OFFSET"), appControlInfo.outputInfo.offset);
	interface_addMenuEntry(video3dMenu, buf, output_change3DOffset, NULL, thumbnail_channels);

	return 0;
}
#endif // ENABLE_3D

#ifdef STSDK
void output_onUpdate(int found)
{
	if (interfaceInfo.currentMenu == _M &UpdateMenu) {
		output_refillMenu(_M &UpdateMenu);
		if (interfaceInfo.messageBox.type == interfaceMessageBoxNone)
			interface_showMessageBox(_T(found ? "UPDATE_FOUND" : "UPDATE_NOT_FOUND"), thumbnail_info, 3000);
	}
}

static int output_updateCheck(interfaceMenu_t *pMenu, void* notused)
{
	system("killall -HUP updaterDaemon");
	return 0;
}

static int output_updateCheckNetwork(interfaceMenu_t *pMenu, void* on)
{
	int res;
	if (on)
		res = system("hwconfigManager s -1 UPNET 1 &>/dev/null");
	else
		res = system("hwconfigManager f -1 UPNET   &>/dev/null");

	if (WIFEXITED(res) != 1 || WEXITSTATUS(res) != 0)
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	else
		output_redrawMenu(pMenu);
	return 0;
}

static int output_rebootAndUpdate(interfaceMenu_t *pMenu, void* notused)
{
	system("hwconfigManager s -1 UPFOUND 1 2>/dev/null");
	system("reboot");
	return 0;
}

int output_enterUpdateMenu(interfaceMenu_t *pMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	pMenu = _M &UpdateMenu;
	interface_clearMenuEntries(pMenu);

	FILE *p;
	int found = 0;
	int network_check = 0;
	p = popen("hwconfigManager h -1 UPFOUND 2>/dev/null | head -1 | cut -c8-", "r");
	if (p) {
		buf[0] = 0;
		fgets(buf, sizeof(buf), p);
		pclose(p);
		found = atoi(buf);
	}
	p = popen("hwconfigManager h -1 UPNET   2>/dev/null | head -1 | cut -c8-", "r");
	if (p) {
		buf[0] = 0;
		fgets(buf, sizeof(buf), p);
		pclose(p);
		network_check = atoi(buf);
	}

	interface_addMenuEntry(pMenu, _T("UPDATE_CHECK"), output_updateCheck, NULL, settings_update_now);

	snprintf(buf, sizeof(buf), "%s: %s", _T("UPDATE_CHECK_NETWORK"), _T(network_check ? "ON" : "OFF"));
	interface_addMenuEntry(pMenu, buf, output_updateCheckNetwork, SET_NUMBER(!network_check), settings_updates);

	interface_addMenuEntry(pMenu, _T("UPDATE_ON_REBOOT"), output_rebootAndUpdate, NULL, settings_update_on_boot);

	interface_addMenuEntryDisabled(pMenu, _T(found ? "UPDATE_FOUND" : "UPDATE_NOT_FOUND"), thumbnail_info);

	return 0;
}
#endif // STSDK

static int output_resetTimeEdit(interfaceMenu_t *pMenu, void* pArg)
{
	return output_refillMenu(pMenu);
}

int output_enterTimeMenu(interfaceMenu_t *timeMenu, void* notused)
{
	struct tm *t;
	time_t now;
	char   buf[BUFFER_SIZE], *str;

	// assert (timeMenu == (interfaceMenu_t*)&TimeSubMenu);
	interface_clearMenuEntries(timeMenu);

	time(&now);
	t = localtime(&now);
	strftime( TimeEntry.info.time.value, sizeof(TimeEntry.info.time.value), "%H%M", t);
	interface_addEditEntryTime(timeMenu, _T("SET_TIME"), output_setTime, output_resetTimeEdit, NULL, thumbnail_log, &TimeEntry);
	strftime( DateEntry.info.date.value, sizeof(DateEntry.info.date.value), "%d%m%Y", t);
	interface_addEditEntryDate(timeMenu, _T("SET_DATE"), output_setDate, output_resetTimeEdit, NULL, thumbnail_log, &DateEntry);

	interface_addMenuEntry(timeMenu, _T("SET_TIME_ZONE"), interface_menuActionShowMenu, &TimeZoneMenu, thumbnail_log);

	sprintf(buf, "%s: ", _T("NTP_SERVER"));
	str = &buf[strlen(buf)];
	strcpy( str, output_getNTP( 0, NULL ) );
	if ( *str == 0 )
		strcpy(str, _T("NOT_AVAILABLE_SHORT") );
	interface_addMenuEntry(timeMenu, buf, output_changeNTP, NULL, thumbnail_enterurl);

	return 0;
}

int output_toggleAutoStopTimeout(interfaceMenu_t *pMenu, void* pArg)
{
	static const time_t timeouts[] = { 0, 10, 15, 30, 45, 60 };
	static const int    timeouts_count = sizeof(timeouts)/sizeof(timeouts[0]);
	int i;
	for ( i = 0; i < timeouts_count; i++ )
		if ( timeouts[i] >= appControlInfo.playbackInfo.autoStop )
			break;
	if ( i >= timeouts_count )
		appControlInfo.playbackInfo.autoStop = timeouts[0];
	else
		appControlInfo.playbackInfo.autoStop = timeouts[ (i+1)%timeouts_count ];

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

int output_togglePlaybackMode(interfaceMenu_t *pMenu, void* pArg)
{
	return output_saveAndRedraw(media_setNextPlaybackMode(), pMenu);
}

int output_toggleVolumeFadein(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.soundInfo.fadeinVolume = !appControlInfo.soundInfo.fadeinVolume;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

int output_enterInterfaceMenu(interfaceMenu_t *interfaceMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	// assert (interfaceMenu == _M &InterfaceMenu);
	interface_clearMenuEntries(interfaceMenu);

	sprintf( buf, "%s: %s", _T("FILE_SORTING"), _T( appControlInfo.mediaInfo.fileSorting == naturalsort ? "SORT_NATURAL" : "SORT_ALPHA" ));
	interface_addMenuEntry(interfaceMenu, buf, output_toggleFileSorting, NULL, settings_interface);

#ifdef STB82
	char *str;
	switch( interfaceInfo.animation )
	{
		case interfaceAnimationVerticalCinema:     str = _T("VERTICAL_CINEMA");     break;
		case interfaceAnimationVerticalPanorama:   str = _T("VERTICAL_PANORAMA");   break;
		case interfaceAnimationHorizontalPanorama: str = _T("HORIZONTAL_PANORAMA"); break;
		case interfaceAnimationHorizontalSlide:    str = _T("HORIZONTAL_SLIDE");    break;
		case interfaceAnimationHorizontalStripes:  str = _T("HORIZONTAL_STRIPES");  break;
		default:                                   str = _T("NONE");
	}
	snprintf(buf, sizeof(buf), "%s: %s", _T("MENU_ANIMATION"), str);
	interface_addMenuEntry(interfaceMenu, buf, output_toggleInterfaceAnimation, NULL, settings_interface);
#endif
	interface_addMenuEntry(interfaceMenu, _T("CHANGE_HIGHLIGHT_COLOR"), output_toggleHighlightColor, NULL, settings_interface);
	snprintf(buf, sizeof(buf), "%s: %d %s", _T("PLAYCONTROL_SHOW_TIMEOUT"), interfacePlayControl.showTimeout, _T("SECOND_SHORT"));
	interface_addMenuEntry(interfaceMenu, buf, output_togglePlayControlTimeout, NULL, settings_interface);
	snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYCONTROL_SHOW_ON_START"), interfacePlayControl.showOnStart ? _T("ON") : _T("OFF") );
	interface_addMenuEntry(interfaceMenu, buf, output_togglePlayControlShowOnStart, NULL, settings_interface);
#ifdef ENABLE_VOIP
	snprintf(buf, sizeof(buf), "%s: %s", _T("VOIP_INDICATION"), _T( interfaceInfo.enableVoipIndication ? "ON" : "OFF" ));
	interface_addMenuEntry(interfaceMenu, buf, output_toggleVoipIndication, NULL, settings_interface);
	snprintf(buf, sizeof(buf), "%s: %s", _T("VOIP_BUZZER"), _T( appControlInfo.voipInfo.buzzer ? "ON" : "OFF" ));
	interface_addMenuEntry(interfaceMenu, buf, output_toggleVoipBuzzer, NULL, settings_interface);
#endif
	return 0;
}

int output_enterPlaybackMenu(interfaceMenu_t *pMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	const char *str = NULL;

	interface_clearMenuEntries(pMenu);

#ifdef ENABLE_DVB_START_CHANNEL
	if (appControlInfo.offairInfo.startChannel <= 0)
		str = _T("LAST_WATCHED");
	else if (appControlInfo.offairInfo.startChannel < dvbChannel_getCount())
		str = offair_getServiceName(appControlInfo.offairInfo.startChannel);
	else
#ifdef ENABLE_ANALOGTV
		str = analogtv_getServiceName(appControlInfo.offairInfo.startChannel - dvbChannel_getCount());
#else
		str = _T("NOT_AVAILABLE_SHORT");
#endif
	snprintf(buf, sizeof(buf), "%s: %s", _T("DVB_START_CHANNEL"), str);
	interface_addMenuEntry(pMenu, buf, interface_menuActionShowMenu, &DvbStartMenu, DvbStartMenu.baseMenu.logo);
#endif // ENABLE_DVB_START_CHANNEL

	sprintf( buf, "%s: %s", _T("RESUME_AFTER_START"), _T( appControlInfo.playbackInfo.bResumeAfterStart ? "ON" : "OFF" ) );
	interface_addMenuEntry(pMenu, buf, output_toggleResumeAfterStart, NULL, settings_interface);

	sprintf( buf, "%s: %s", _T("AUTOPLAY"), _T( appControlInfo.playbackInfo.bAutoPlay ? "ON" : "OFF" ) );
	interface_addMenuEntry(pMenu, buf, output_toggleAutoPlay, NULL, settings_interface);

	snprintf(buf, sizeof(buf), "%s: %d %s", _T("PLAYBACK_STOP_TIMEOUT"), appControlInfo.playbackInfo.autoStop, _T("MINUTE_SHORT"));
	interface_addMenuEntry(pMenu, buf, output_toggleAutoStopTimeout, NULL, settings_interface);

	snprintf(buf, sizeof(buf), "%s: %s", _T("FADEIN_VOLUME"), _T( appControlInfo.soundInfo.fadeinVolume ? "ON" : "OFF" ));
	interface_addMenuEntry(pMenu, buf, output_toggleVolumeFadein, NULL, settings_interface);

	switch(appControlInfo.mediaInfo.playbackMode) {
		case playback_looped:     str = _T("LOOPED");    break;
		case playback_sequential: str = _T("SEQUENTAL"); break;
		case playback_random:     str = _T("RANDOM");    break;
		default:                  str = _T("SINGLE");
	}
	sprintf(buf, "%s: %s", _T("PLAYBACK_MODE"), str);
	interface_addMenuEntry(pMenu, buf, output_togglePlaybackMode, NULL, thumbnail_turnaround);

	return 0;
}

void output_fillOutputMenu(void)
{
	char *str;
	interfaceMenu_t *outputMenu = _M &OutputMenu;

	interface_clearMenuEntries(outputMenu);

	str = _T("INFO");
	interface_addMenuEntry(outputMenu, str, show_info, NULL, thumbnail_info);

#ifdef ENABLE_STATS
	str = _T("PROFILE");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &StatsMenu, thumbnail_recorded);
#endif
	str = _T("LANGUAGE");
	interface_addMenuEntry(outputMenu, str, l10n_initLanguageMenu, NULL, settings_language);

#ifdef ENABLE_MESSAGES
	str = _T("MESSAGES");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &MessagesMenu, thumbnail_messages);
#endif

	str = _T("TIME_DATE_CONFIG");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &TimeSubMenu, settings_datetime);

#if (defined STBxx) || (defined STSDK)
	str = _T("VIDEO_CONFIG");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &VideoSubMenu, settings_video);
#endif

	if(strList_count(extInput_getList()) > 0) {
		str = _T("INPUTS_CONFIG");
		interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &InputsSubMenu, settings_video);
	}

#ifdef ENABLE_ANALOGTV
	if(analogtv_hasTuner()) {
		str = _T("ANALOGTV_CONFIG");
		interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &AnalogTvSubMenu, settings_dvb);
	}
#endif

#ifdef ENABLE_DVB
#ifdef HIDE_EXTRA_FUNCTIONS
	if(dvbfe_hasTuner(0) || dvbfe_hasTuner(1))
#endif
	{
		str = _T("DVB_CONFIG");
#ifdef ENABLE_PASSWORD
        interface_addMenuEntry(outputMenu, str, output_askPassword, (void*)output_showDVBMenu, settings_dvb);
#else
        interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &DVBSubMenu, settings_dvb);
#endif
	}
#endif // #ifdef ENABLE_DVB

	str = _T("NETWORK_CONFIG");
#ifdef ENABLE_PASSWORD
    interface_addMenuEntry(outputMenu, str, output_askPassword, (void*)output_showNetworkMenu, settings_network);
#else
    interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, outputNetwork_getMenu(), settings_network);
#endif

	str = _T("INTERFACE");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &InterfaceMenu, settings_interface);

#ifdef ENABLE_DVB
	str = _T("PLAYLIST_MAIN");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &InterfacePlaylistMain, settings_interface);
#endif

	str = _T("PLAYBACK");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &PlaybackMenu, thumbnail_loading);

#ifdef ENABLE_3D
	str = _T("3D_SETTINGS");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &Video3DSubMenu, thumbnail_channels);
#endif

#ifdef STSDK
	if(currentmeter_isExist()) {
		str = _T("CURRENTMETER_CALIBRATE");
		interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &CurrentmeterSubMenu, thumbnail_configure);
	}

	str = _T("UPDATES");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &UpdateMenu, settings_updates);
#endif /* STSDK */

	str = _T("STATUS_REPORT");
	interface_addMenuEntry(outputMenu, str, (void*)output_statusReport, NULL, thumbnail_configure);

	str = _T("RESET_SETTINGS");
#ifdef ENABLE_PASSWORD
    interface_addMenuEntry(outputMenu, str, output_askPassword, (void*)output_resetSettings, thumbnail_warning);
#else
    interface_addMenuEntry(outputMenu, str, output_resetSettings, NULL, thumbnail_warning);
#endif

}

void output_buildMenu(interfaceMenu_t *pParent)
{
	createListMenu(&OutputMenu, _T("SETTINGS"), thumbnail_configure, NULL, pParent,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&VideoSubMenu, _T("VIDEO_CONFIG"), settings_video, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterVideoMenu, NULL, NULL);

	if(strList_count(extInput_getList()) > 0) {
		createListMenu(&InputsSubMenu, _T("INPUTS_CONFIG"), settings_video, NULL, _M &OutputMenu,
			interfaceListMenuIconThumbnail, NULL, NULL, NULL);
		output_fillInputsMenu(&InputsSubMenu, NULL);
	}

#ifdef ENABLE_ANALOGTV
	if(analogtv_hasTuner()) {
		createListMenu(&AnalogTvSubMenu, _T("ANALOGTV_CONFIG"), settings_dvb, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterAnalogTvMenu, NULL, NULL);
	}
#endif

	if(currentmeter_isExist()) {
		createListMenu(&CurrentmeterSubMenu, _T("CURRENTMETER_CALIBRATE"), settings_dvb, NULL, _M &OutputMenu,
			interfaceListMenuIconThumbnail, output_enterCalibrateMenu, NULL, NULL);
	}

#ifdef ENABLE_3D
	createListMenu(&Video3DSubMenu, _T("3D_SETTINGS"), settings_video, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enter3DMenu, NULL, NULL);
#endif // #ifdef STB225

	createListMenu(&TimeSubMenu, _T("TIME_DATE_CONFIG"), settings_datetime, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterTimeMenu, NULL, NULL);
#ifdef ENABLE_DVB
	createListMenu(&DVBSubMenu, _T("DVB_CONFIG"), settings_dvb, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterDVBMenu, NULL, NULL);
	createListMenu(&DiSEqCMenu, "DiSEqC", settings_dvb, NULL, _M &DVBSubMenu,
		interfaceListMenuIconThumbnail, output_enterDiseqcMenu, NULL, NULL);
#ifdef ENABLE_DVB_START_CHANNEL
	createListMenu(&DvbStartMenu, _T("DVB_START_CHANNEL"), settings_interface, NULL, _M &PlaybackMenu,
		interfaceListMenuIconThumbnail, output_enterDvbStartMenu, NULL, NULL);
#endif // ENABLE_DVB_START_CHANNEL
	playlistEditor_init();
#endif // ENABLE_DVB

    outputNetwork_buildMenu(_M &OutputMenu);

#ifndef STSDK
	createListMenu(&StandardMenu, _T("TV_STANDARD"), settings_video, NULL, _M &VideoSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&FormatMenu, _T("TV_FORMAT"), settings_video, NULL, _M &VideoSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&BlankingMenu, _T("TV_BLANKING"), settings_video, NULL, _M &VideoSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);
#endif

	createListMenu(&GraphicsModeMenu, _T("INTERFACE_SIZE"), settings_video, NULL, _M &VideoSubMenu,
		interfaceListMenuIconThumbnail, output_enterGraphicsModeMenu, NULL, NULL);

	createListMenu(&TimeZoneMenu, _T("TIME_ZONE"), settings_datetime, NULL, _M &TimeSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&InterfaceMenu, _T("INTERFACE"), settings_interface, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterInterfaceMenu, NULL, NULL);

	createListMenu(&PlaybackMenu, _T("PLAYBACK"), thumbnail_loading, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterPlaybackMenu, NULL, NULL);


#ifdef STSDK
	createListMenu(&UpdateMenu, _T("UPDATES"), settings_updates, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterUpdateMenu, NULL, NULL);
#endif

#ifdef ENABLE_MESSAGES
	messages_buildMenu(_M &OutputMenu);
#endif

	TimeEntry.info.time.type    = interfaceEditTime24;

#if (defined STB6x8x) || (defined STB225)
	output_fillStandardMenu();
#endif
#if (defined STB82)
	output_fillBlankingMenu();
	output_fillFormatMenu();
#endif
	output_fillOutputMenu();
	output_fillTimeZoneMenu();

#ifdef ENABLE_STATS
	stats_buildMenu(_M &OutputMenu);
#endif
}

void output_cleanupMenu(void)
{
#ifdef ENABLE_DVB
	playlistEditor_terminate();
#endif
    outputNetwork_cleanupMenu();
}


