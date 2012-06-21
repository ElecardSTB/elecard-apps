
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

#include "debug.h"
#include "l10n.h"
#include "StbMainApp.h"
#include "sem.h"
#include "gfx.h"
#include "backend.h"
#include "menu_app.h"
#include "off_air.h"
#include "sound.h"
#include "stats.h"
#include "stsdk.h"
#include "media.h"

#include "dvb.h"
#include "rtp.h"
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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>

#include <directfb.h>
#include <directfb_strings.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define DATA_BUFF_SIZE              (32*1024)

#define MAX_CONFIG_PATH             (64)

#define COLOR_STEP_COUNT            (64)
#define COLOR_STEP_SIZE             (0x10000/COLOR_STEP_COUNT)

#ifdef STB82
#define PPP_CHAP_SECRETS_FILE       "/etc/ppp/chap-secrets"
#endif
#ifdef STSDK
#define PPP_CHAP_SECRETS_FILE       "/var/etc/ppp/chap-secrets"
#define TIMEZONE_FILE               "/var/etc/localtime"
#define NTP_CONFIG_FILE             "/var/etc/ntpd"
#endif

#define TEMP_CONFIG_FILE            "/var/tmp/cfg.tmp"

#define OUTPUT_INFO_SET(type,index) (void*)(intptr_t)(((int)type << 16) | (index))
#define OUTPUT_INFO_GET_TYPE(info)  ((int)(intptr_t)info >> 16)
#define OUTPUT_INFO_GET_INDEX(info) ((int)(intptr_t)info & 0xFFFF)

#define FORMAT_CHANGE_TIMEOUT       (15)

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

typedef enum
{
	gatewayModeOff = 0,
	gatewayModeBridge,
	gatewayModeNAT,
	gatewayModeFull,
	gatewayModeCount
} gatewayMode_t;

typedef enum
{
	optionRtpEpg,
	optionRtpPlaylist,
	optionVodPlaylist
} outputUrlOption;

typedef enum
{
	optionIP   = 0x01,
	optionGW   = 0x02,
	optionMask = 0x04,
	optionDNS  = 0x08,
	optionMode = 0x10
} outputIPOption;

#ifdef ENABLE_PPP
typedef struct
{
	char login[MENU_ENTRY_INFO_LENGTH];
	char password[MENU_ENTRY_INFO_LENGTH];
	pthread_t check_thread;
} pppInfo_t;
#endif

#ifdef ENABLE_WIFI

//#define USE_WPA_SUPPLICANT

#include <iwlib.h>

typedef struct
{
	int wanMode;
#ifdef STSDK
	int wanChanged;
#endif
#ifdef STBPNX
	int dhcp;
#endif
	int channelCount;
	int currentChannel;
	outputWifiMode_t       mode;
	outputWifiAuth_t       auth;
	outputWifiEncryption_t encryption;
	char essid[IW_ESSID_MAX_SIZE];
	char key[IW_ENCODING_TOKEN_MAX+1];
} outputWifiInfo_t;
#endif

#ifdef STSDK

#define USE_WPA_SUPPLICANT

static char output_currentFormat[64] = "1080i50";

typedef struct
{
	struct in_addr ip;
	struct in_addr mask;
	struct in_addr gw;
} outputNfaceInfo_t;

typedef enum {
	lanStatic = 0,
	lanDhcpServer,
	lanDhcpClient,
	lanBridge,
	lanModeCount
} lanMode_t;

typedef struct
{
	outputNfaceInfo_t wan;
	int               wanDhcp;
	struct in_addr    dns;
	outputNfaceInfo_t lan;
	lanMode_t         lanMode;
} outputNetworkInfo_t;
#endif // STSDK

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static void output_fillOutputMenu(void);
static int output_fillNetworkMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_fillVideoMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_fillTimeMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_fillInterfaceMenu(interfaceMenu_t *pMenu, void* pArg);

static int output_fillWANMenu (interfaceMenu_t *pMenu, void* pArg);
#ifdef ENABLE_PPP
static int output_fillPPPMenu (interfaceMenu_t *pMenu, void* pArg);
static int output_leavePPPMenu (interfaceMenu_t *pMenu, void* pArg);
#endif
#ifdef ENABLE_LAN
static int output_fillLANMenu (interfaceMenu_t *pMenu, void* pArg);
static int output_fillGatewayMenu(interfaceMenu_t *pMenu, void* pArg);
#endif
#ifdef ENABLE_WIFI
static int output_fillWifiMenu (interfaceMenu_t *pMenu, void* pArg);
static int output_wifiKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#ifdef USE_WPA_SUPPLICANT
static int output_readWpaSupplicantConf(const char *filename);
static int output_writeWpaSupplicantConf(const char *filename);
#endif
#endif
#ifdef ENABLE_IPTV
static int output_fillIPTVMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleIPTVPlaylist(interfaceMenu_t *pMenu, void* pArg);

#ifdef ENABLE_PROVIDER_PROFILES
static int output_enterProfileMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_leaveProfileMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_setProfile(interfaceMenu_t *pMenu, void* pArg);
#endif

#endif // ENABLE_IPTV
#ifdef ENABLE_VOD
static int output_fillVODMenu (interfaceMenu_t *pMenu, void* pArg);
static int output_toggleVODPlaylist(interfaceMenu_t *pMenu, void* pArg);
#endif
static int output_fillWebMenu (interfaceMenu_t *pMenu, void* pArg);

#ifdef STB82
static int output_toggleInterfaceAnimation(interfaceMenu_t* pMenu, void* pArg);
#endif
static int output_toggleResumeAfterStart(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleAutoPlay(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleFileSorting(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleHighlightColor(interfaceMenu_t* pMenu, void* pArg);
static int output_togglePlayControlTimeout(interfaceMenu_t* pMenu, void* pArg);
static int output_togglePlayControlShowOnStart(interfaceMenu_t* pMenu, void* pArg);
#ifdef ENABLE_VOIP
static int output_toggleVoipIndication(interfaceMenu_t* pMenu, void* pArg);
static int output_toggleVoipBuzzer(interfaceMenu_t* pMenu, void* pArg);
#endif
#ifdef ENABLE_DVB
static int output_toggleDvbShowScrambled(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbBandwidth(interfaceMenu_t *pMenu, void* pArg);
static int output_clearDvbSettings(interfaceMenu_t *pMenu, void* pArg);
static int output_clearOffairSettings(interfaceMenu_t *pMenu, void* pArg);
static int output_confirmClearDvb(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int output_confirmClearOffair(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#endif
#ifdef ENABLE_LAN
static int output_confirmGatewayMode(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int output_toggleGatewayMode(interfaceMenu_t *pMenu, void* pArg);
#endif

// 3D
#if (defined(STB225) || defined(STSDK))
static interfaceListMenu_t Video3DSubMenu;
static int output_fill3DMenu(interfaceMenu_t *pMenu, void* pArg);
#endif // #ifdef STB225

static void output_colorSliderUpdate(void *pArg);

static char* output_getOption(outputUrlOption option);
static char* output_getURL(int index, void* pArg);
static int output_changeURL(interfaceMenu_t *pMenu, char *value, void* pArg);
static int output_toggleURL(interfaceMenu_t *pMenu, void* pArg);

#ifdef ENABLE_PASSWORD
static int output_askPassword(interfaceMenu_t *pMenu, void* pArg);
static int output_enterPassword(interfaceMenu_t *pMenu, char *value, void* pArg);
#endif
static void output_fillStandardMenu(void);
static void output_fillFormatMenu(void);
static void output_fillBlankingMenu(void);

static void output_fillTimeZoneMenu(void);

#ifdef STSDK
static int output_writeInterfacesFile(void);
static int output_writeDhcpConfig(void);

static int output_setVideoFormat(const char *name);
static int output_enterFormatMenu(interfaceMenu_t *pMenu, void *pArg);
#endif

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static interfaceListMenu_t StandardMenu;
static interfaceListMenu_t FormatMenu;
static interfaceListMenu_t BlankingMenu;
static interfaceListMenu_t TimeZoneMenu;
static interfaceListMenu_t InterfaceMenu;

static interfaceListMenu_t VideoSubMenu;
static interfaceListMenu_t TimeSubMenu;
static interfaceListMenu_t NetworkSubMenu;

static interfaceListMenu_t WANSubMenu;
#ifdef ENABLE_PPP
static interfaceListMenu_t PPPSubMenu;
#endif
#ifdef ENABLE_LAN
static interfaceListMenu_t LANSubMenu;
static interfaceListMenu_t GatewaySubMenu;
#endif
#ifdef ENABLE_WIFI
interfaceListMenu_t WifiSubMenu;
#endif
#ifdef ENABLE_IPTV
static interfaceListMenu_t IPTVSubMenu;

#ifdef ENABLE_PROVIDER_PROFILES
#define MAX_PROFILE_PATH 64
static struct dirent **output_profiles = NULL;
static int             output_profiles_count = 0;
static char            output_profile[MAX_PROFILE_PATH] = {0};
static interfaceListMenu_t ProfileMenu;
#endif

#endif // ENABLE_IPTV

#ifdef ENABLE_VOD
static interfaceListMenu_t VODSubMenu;
#endif
static interfaceListMenu_t WebSubMenu;

#ifdef ENABLE_WIFI
static outputWifiInfo_t wifiInfo;
#endif

static interfaceEditEntry_t TimeEntry;
static interfaceEditEntry_t DateEntry;

#ifdef ENABLE_LAN
#ifdef STBPNX
static gatewayMode_t output_gatewayMode = gatewayModeOff;
#endif
#endif

static int bDisplayedWarning = 0;

static long info_progress;

static char output_ip[4*4];

#ifdef ENABLE_PPP
static pppInfo_t pppInfo;
#endif

#ifdef STSDK
static outputNetworkInfo_t networkInfo;
#endif

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
#ifdef ENABLE_DVB
interfaceListMenu_t DVBSubMenu;
#endif

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

char* inet_addr_prepare( char *value)
{
	char *str1, *str2;
	str1 = value;
	while( str1[0] == '0' && !(str1[1] == 0 || str1[1] == '.'))
	{
		str2 = str1;
		while( (*str2++ = str2[1]) );
	}
	for( str1 = value; *str1; str1++)
	{
		while( str1[0] == '.' && str1[1] == '0' && !(str1[2] == 0 || str1[2] == '.'))
		{
			str2 = &str1[1];
			while( (*str2++ = str2[1]) );
		}
	}
	return value;
}

/* -------------------------- OUTPUT SETTING --------------------------- */

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
			interface_setSelectedItem((interfaceMenu_t*)&StandardMenu, 0);
			system("/config.templates/scripts/dispmode ntsc");
            break;

        case DSETV_SECAM:
			interface_setSelectedItem((interfaceMenu_t*)&StandardMenu, 1);
			system("/config.templates/scripts/dispmode secam");
            break;

        case DSETV_PAL:
#ifdef STBPNX
        case DSETV_PAL_BG:
        case DSETV_PAL_I:
        case DSETV_PAL_N:
        case DSETV_PAL_NC:
#endif
			interface_setSelectedItem((interfaceMenu_t*)&StandardMenu, 2);
			system("/config.templates/scripts/dispmode pal");
            break;

#ifdef STB82
        case DSETV_PAL_60:
#endif //#ifdef STB82
#ifdef STBPNX
        case DSETV_PAL_M:
			interface_setSelectedItem((interfaceMenu_t*)&StandardMenu, 3);
			system("/config.templates/scripts/dispmode pal60");
            break;
        case DSETV_DIGITAL:
            appControlInfo.outputInfo.encConfig[0].out_signals = DSOS_YCBCR;
            appControlInfo.outputInfo.encConfig[0].out_connectors = DSOC_COMPONENT;
            appControlInfo.outputInfo.encConfig[0].flags = (DSECONF_OUT_SIGNALS | DSECONF_CONNECTORS );
			interface_setSelectedItem((interfaceMenu_t*)&StandardMenu, 0);
            break;
#endif
        default:
            break;
    }

    //gfx_setOutputFormat(1);

	appControlInfo.outputInfo.standart = tv_standard;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#else //#ifdef STB6x8x
//	system("mount -o rw,remount /");
//	setParam("/etc/init.d/S35pnxcore.sh", "resolution", GET_NUMBER(pArg) == 720 ? "1280x720x60p" : "1920x1080x60i");
//	system("mount -o ro,remount /");
#endif //#ifdef STB6x8x

	system("sync");
	system("reboot");

    output_fillStandardMenu();
	interface_displayMenu(1);

#ifdef ENABLE_DVB
    /* Re-start DVB - if possible */
    if ( offair_tunerPresent() && dvb_getNumberOfServices() > 0 )
    {
        offair_startVideo(screenMain);
    }
#endif

    return 0;
}

#ifdef STSDK
static int output_setVideoFormat(const char *name)
{
	elcdRpcType_t type;
	cJSON *res  = NULL;
	cJSON *mode = cJSON_CreateString(name);
	int    ret = 1;

	ret = st_rpcSync( elcmd_setvmode, mode, &type, &res );
	if( ret == 0 && type == elcdRpcResult && res && res->type == cJSON_String )
	{
		if( strcmp(res->valuestring, "ok") )
		{
			eprintf("%s: failed: %s\n", __FUNCTION__, res->valuestring);
			ret = 1;
		}
	} else if ( type == elcdRpcError && res && res->type == cJSON_String )
	{
		eprintf("%s: error: %s\n", __FUNCTION__, res->valuestring);
		ret = 1;
	}
	cJSON_Delete(res);
	cJSON_Delete(mode);
	return ret;
}

static int output_cancelFormat(void* pArg)
{
	int ret;
	interface_hideMessageBox();
	ret = output_setVideoFormat(output_currentFormat);
	interface_displayMenu(1);
	return ret;
}

static int output_getFormatHeight(const char *format)
{
	const char *s = strchr(format, 'x');
	if (!s) s = format;
	return strtol(s, NULL, 10);
}

static int output_confirmFormat(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	interface_removeEvent(output_cancelFormat, NULL);

	if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		int old_height = output_getFormatHeight(output_currentFormat);
		int new_height = output_getFormatHeight(pMenu->menuEntry[pMenu->selectedItem].info);
		if (old_height != new_height)
		{
			// Command should be sent after framebuffer device is closed
			helperStartApp("StbCommandClient -f /tmp/elcd.sock '{\"method\":\"initfb\",\"params\":[],\"id\": 1}'");
		} else
		output_fillFormatMenu();
		return 0;
	}

	output_cancelFormat(NULL);
	return 0;
}
#endif

/**
 * This function now uses the Encoder API to set the output format instead of the Output API.
 */
static int output_setFormat(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STB82
	int format = GET_NUMBER(pArg);

	gfx_changeOutputFormat(format);

	appControlInfo.outputInfo.format = format;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

    /*switch(format)
    {
        case(DSOS_YCBCR) :
			interface_setSelectedItem((interfaceMenu_t*)&FormatMenu, 3);
            break;
        case(DSOS_YC) :
			interface_setSelectedItem((interfaceMenu_t*)&FormatMenu, 0);
            break;
        case(DSOS_CVBS) :
			interface_setSelectedItem((interfaceMenu_t*)&FormatMenu, 1);
            break;
        case(DSOS_RGB) :
			interface_setSelectedItem((interfaceMenu_t*)&FormatMenu, 2);
            break;
        default:
            break;
    }*/
    output_fillFormatMenu();
	interface_displayMenu(1);
#endif // STB82
#ifdef STSDK
	if( strcmp( output_currentFormat, pMenu->menuEntry[pMenu->selectedItem].info ) )
	{
		output_setVideoFormat( pMenu->menuEntry[pMenu->selectedItem].info );
		interface_addEvent(output_cancelFormat, NULL, FORMAT_CHANGE_TIMEOUT*1000, 1);
		interface_showConfirmationBox( _T("CONFIRM_FORMAT_CHANGE"), thumbnail_warning, output_confirmFormat, NULL );
	}
#endif
    return 0;
}

/**
 * This function now uses the Encoder API to set the slow blanking instead of the Output API.
 */
static int output_setBlanking(interfaceMenu_t *pMenu, void* pArg)
{
    int blanking;

    blanking = GET_NUMBER(pArg);

    appControlInfo.outputInfo.encConfig[0].slow_blanking = blanking;
    appControlInfo.outputInfo.encConfig[0].flags = DSECONF_SLOW_BLANKING;

    switch(blanking)
    {
        case(DSOSB_4x3) :
			interface_setSelectedItem((interfaceMenu_t*)&BlankingMenu, 0);
            break;
        case(DSOSB_16x9) :
			interface_setSelectedItem((interfaceMenu_t*)&BlankingMenu, 1);
            break;
        case(DSOSB_OFF) :
			interface_setSelectedItem((interfaceMenu_t*)&BlankingMenu, 2);
            break;
        default:
            break;
    }

    gfx_setOutputFormat(0);
    //output_fillBlankingMenu();
	interface_displayMenu(1);

    return 0;
}

#ifndef HIDE_EXTRA_FUNCTIONS
static int output_toggleAudio(interfaceMenu_t *pMenu, void* pArg)
{
	if (appControlInfo.soundInfo.rcaOutput == 1)
	{
		appControlInfo.soundInfo.rcaOutput = 0;
	} else
	{
		appControlInfo.soundInfo.rcaOutput = 1;
	}

	/* Just force rcaOutput for now */
	appControlInfo.soundInfo.rcaOutput = 1;

	sound_restart();
	output_fillVideoMenu(pMenu, NULL);

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	interface_displayMenu(1);
	return 0;
}

static int output_togglePCR(interfaceMenu_t *pMenu, void* pArg)
{
	if (appControlInfo.bProcessPCR == 1)
	{
		appControlInfo.bProcessPCR = 0;
	} else
	{
		appControlInfo.bProcessPCR = 1;
	}

	output_fillVideoMenu(pMenu, pArg);

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	interface_displayMenu(1);

	return 0;
}

static int output_toggleRSync(interfaceMenu_t *pMenu, void* pArg)
{
	if (appControlInfo.bRendererDisableSync == 1)
	{
		appControlInfo.bRendererDisableSync = 0;
	} else
	{
		appControlInfo.bRendererDisableSync = 1;
	}

	output_fillVideoMenu(pMenu, pArg);

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	interface_displayMenu(1);

	return 0;
}


static int output_toggleBufferTracking(interfaceMenu_t *pMenu, void* pArg)
{
	if (appControlInfo.bUseBufferModel == 1)
	{
		appControlInfo.bUseBufferModel = 0;
	} else
	{
		appControlInfo.bUseBufferModel = 1;
	}

	output_fillVideoMenu(pMenu, pArg);

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	interface_displayMenu(1);

	return 0;
}
#endif

#ifdef STB82
static int output_toggleInterfaceAnimation(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceInfo.animation = (interfaceInfo.animation + 1) % interfaceAnimationCount;
	if( interfaceInfo.animation > 0 )
	{
		interfaceInfo.currentMenu = (interfaceMenu_t*)&interfaceMainMenu; // toggles animation
		interface_displayMenu(1);
	}

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}
#endif

static int output_toggleHighlightColor(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceInfo.highlightColor++;
	if (interface_colors[interfaceInfo.highlightColor].A==0)
		interfaceInfo.highlightColor = 0;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}

static int output_toggleResumeAfterStart(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.playbackInfo.bResumeAfterStart = (appControlInfo.playbackInfo.bResumeAfterStart + 1) % 2;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}

static int output_toggleAutoPlay(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.playbackInfo.bAutoPlay = (appControlInfo.playbackInfo.bAutoPlay + 1) % 2;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}

int output_toggleFileSorting(interfaceMenu_t* pMenu, void* pArg)
{
	appControlInfo.mediaInfo.fileSorting = appControlInfo.mediaInfo.fileSorting == naturalsort ? alphasort : naturalsort;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}

static int output_togglePlayControlTimeout(interfaceMenu_t *pMenu, void* pArg)
{
	interfacePlayControl.showTimeout = interfacePlayControl.showTimeout % PLAYCONTROL_TIMEOUT_MAX + 1;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}

static int output_togglePlayControlShowOnStart(interfaceMenu_t *pMenu, void* pArg)
{
	interfacePlayControl.showOnStart = !interfacePlayControl.showOnStart;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}

#ifdef ENABLE_VOIP
static int output_toggleVoipIndication(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceInfo.enableVoipIndication = (interfaceInfo.enableVoipIndication + 1) % 2;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}

static int output_toggleVoipBuzzer(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.voipInfo.buzzer = (appControlInfo.voipInfo.buzzer + 1) % 2;
	voip_setBuzzer();
	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	return output_fillInterfaceMenu(pMenu, pArg);
}
#endif

int getParam(const char *path, const char *param, const char *defaultValue, char *output)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	FILE *fd;
	int found = 0;
	int plen, vlen;

	fd = fopen(path, "r");
	if (fd != NULL)
	{
		while (fgets(buf, MENU_ENTRY_INFO_LENGTH, fd) != NULL)
		{
			plen = strlen(param);
			vlen = strlen(buf)-1;
			if (strncmp(buf, param, plen) == 0 && buf[plen] == '=')
			{
				while (buf[vlen] == '\r' || buf[vlen] == '\n' || buf[vlen] == ' ')
				{
					buf[vlen] = 0;
					vlen--;
				}
				if (vlen-plen > 0)
				{
					if (output != NULL)
					{
						strcpy(output, &buf[plen+1]);
					}
					found = 1;
				}
				break;
			}
		}
		fclose(fd);
	}

	if (!found && defaultValue != NULL && output != NULL)
	{
		strcpy(output, defaultValue);
	}
	return found;
}

int setParam(const char *path, const char *param, const char *value)
{
	FILE *fdi, *fdo;
	char buf[MENU_ENTRY_INFO_LENGTH];
	int found = 0;

	//dprintf("%s: %s: %s -> %s\n", __FUNCTION__, path, param, value);

#ifdef STB225
//	system("mount -o rw,remount /");
#endif

	fdo = fopen(TEMP_CONFIG_FILE, "w");

	if (fdo == NULL)
	{
		eprintf("output: Failed to open out file '%s'\n", TEMP_CONFIG_FILE);
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
#ifdef STB225
//		system("mount -o ro,remount /");
#endif
		return -1;
	}

	fdi = fopen(path, "r");

	if (fdi != NULL)
	{
		while (fgets(buf, MENU_ENTRY_INFO_LENGTH, fdi) != NULL)
		{
			//dprintf("%s: line %s\n", __FUNCTION__, buf);
			if (strncasecmp(param, buf, strlen(param)) == 0 ||
				(buf[0] == '#' && strncasecmp(param, &buf[1], strlen(param)) == 0))
			{
				//dprintf("%s: line matched param %s\n", __FUNCTION__, param);
				if (value != NULL)
				{
					fprintf(fdo, "%s=%s\n", param, value);
				} else
				{
					fprintf(fdo, "#%s=\n", param);
				}
				found = 1;
			} else
			{
				fwrite(buf, strlen(buf), 1, fdo);
			}
		}
		fclose(fdi);
	}

	if (found == 0)
	{
		if (value != NULL)
		{
			fprintf(fdo, "%s=%s\n", param, value);
		} else
		{
			fprintf(fdo, "#%s=\n", param);
		}
	}

	fclose(fdo);

	//dprintf("%s: replace!\n", __FUNCTION__);
	sprintf(buf, "mv -f '%s' '%s'", TEMP_CONFIG_FILE, path);
	system(buf);

#ifdef STB225
//	system("mount -o ro,remount /");
#endif

	return 0;
}

#ifdef ENABLE_WIFI
int output_changeESSID(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;

	size_t essid_len = strlen(value);
	if (essid_len < 1 || essid_len >= sizeof(wifiInfo.essid))
		return 0;

	memcpy( wifiInfo.essid, value, essid_len+1 );
#ifdef STBPNX
	int i = OUTPUT_INFO_GET_INDEX(pArg);
	char path[MAX_CONFIG_PATH];

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

	if (setParam(path, "ESSID", value) != 0	&& bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif // STBPNX
#ifdef STSDK
	if (wifiInfo.wanMode)
		output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
	else
	if (setParam(STB_HOSTAPD_CONF, "ssid", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif // STSDK

	output_fillWifiMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

char* output_getESSID(int index, void* pArg)
{
	return index == 0 ? wifiInfo.essid : NULL;
}

static int output_toggleESSID(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_ESSID"), "\\w+", output_changeESSID, output_getESSID, inputModeABC, pArg );

	return 0;
}

static int output_changeWifiChannel(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;

	int channel = strtol( value, NULL, 10 );
	if (channel < 1 || channel > wifiInfo.channelCount)
		return 0;

	wifiInfo.currentChannel = channel;
#ifdef STBPNX
	int i = OUTPUT_INFO_GET_INDEX(pArg);
	char path[MAX_CONFIG_PATH];

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));
	if( setParam(path, "CHANNEL", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif // STBPNX
#ifdef STSDK
	if( setParam(STB_HOSTAPD_CONF, "channel", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif // STSDK

	output_fillWifiMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

char* output_getWifiChannel(int index, void* pArg)
{
	if( index == 0 )
	{
		static char temp[8];
		sprintf(temp, "%02d", wifiInfo.currentChannel);
		return temp;
	} else
		return NULL;
}

static int output_toggleWifiChannel(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_WIFI_CHANNEL"), "\\d{2}", output_changeWifiChannel, output_getWifiChannel, inputModeDirect, pArg );

	return 0;
}

static int output_toggleAuthMode(interfaceMenu_t *pMenu, void* pArg)
{
	outputWifiAuth_t maxAuth = wifiInfo.mode == wifiModeAdHoc ? wifiAuthWEP+1 : wifiAuthCount;

	return output_changeAuthMode(pMenu, (void*)((wifiInfo.auth+1)%maxAuth));
}

int output_changeAuthMode(interfaceMenu_t *pMenu, void* pArg)
{
	wifiInfo.auth = (outputWifiAuth_t)pArg;

#ifdef STBPNX
	char path[MAX_CONFIG_PATH];
	int i = ifaceWireless;
	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

	char *value = NULL;
	switch(wifiInfo.auth)
	{
		case wifiAuthOpen:   value = "OPEN";
			setParam(path, "ENCRYPTION", "NONE");
			break;
		case wifiAuthWEP:    value = "SHARED";
			setParam(path, "ENCRYPTION", "WEP");
			break;
		case wifiAuthWPAPSK: value = "WPAPSK";
			setParam(path, "ENCRYPTION", "TKIP");
			break;
		case wifiAuthWPA2PSK:value = "WPA2PSK";
			setParam(path, "ENCRYPTION", "TKIP");
			break;
		default:
			return 1;
	}

	if( setParam(path, "AUTH", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif
#ifdef STSDK
	if( output_writeInterfacesFile() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif

	output_fillWifiMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

static int output_toggleWifiEncryption(interfaceMenu_t *pMenu, void* pArg)
{
	return output_changeWifiEncryption(pMenu, (void*)((wifiInfo.encryption+1)%wifiEncCount));
}

int output_changeWifiEncryption(interfaceMenu_t *pMenu, void* pArg)
{
	wifiInfo.encryption = (outputWifiEncryption_t)pArg;

#ifdef STBPNX
	char path[MAX_CONFIG_PATH];
	int i = ifaceWireless;

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

	char *value = NULL;
	switch(wifiInfo.encryption)
	{
		case wifiEncAES:  value = "AES";  break;
		case wifiEncTKIP: value = "TKIP"; break;
		default: return 1;
	}

	if( setParam(path, "ENCRYPTION", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif
#ifdef STSDK
	if( output_writeInterfacesFile() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif

	output_fillWifiMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

static int output_changeWifiKey(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	menuActionFunction pAction = pArg;

	if ( value == NULL )
		return 1;

	size_t key_len = strlen(value);

	if ( wifiInfo.auth == wifiAuthWEP )
	{
		if ( key_len != 10 )
		{
			interface_showMessageBox(_T("WIRELESS_PASSWORD_INCORRECT"), thumbnail_error, 0);
			return 1;
		} else
		{
			int j;
			for ( j = 0; j < 10; j++ )
				if ( value[j] < '0' || value[j] > '9' )
				{
					interface_showMessageBox(_T("WIRELESS_PASSWORD_INCORRECT"), thumbnail_error, 0);
					return 1;
				}
		}
	} else if ( key_len < 8 )
	{
		interface_showMessageBox(_T("WIRELESS_PASSWORD_TOO_SHORT"), thumbnail_error, 0);
		return 1;
	} else if ( key_len >= sizeof( wifiInfo.key ) )
	{
		interface_showMessageBox(_T("WIRELESS_PASSWORD_INCORRECT"), thumbnail_error, 0);
		return 1;
	}

	memcpy( wifiInfo.key, value, key_len+1 );

#ifdef STBPNX
	char path[MAX_CONFIG_PATH];
	int i = ifaceWireless;

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

	if (setParam(path, "KEY", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif // STBPNX
#ifdef STSDK
	if (output_writeInterfacesFile() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif

	if( pAction != NULL )
	{
		return pAction(pMenu, SET_NUMBER(ifaceWireless));
	}

	output_fillWifiMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

char* output_getWifiKey(int index, void* pArg)
{
	return index == 0 ? wifiInfo.key : NULL;
}

/**
 * @param[in] pArg Pointer to menuActionFunction, which will be called after successfull password change.
 * Callback will be called as pArg(pMenu, SET_NUMBER(ifaceWireless)). Can be NULL.
 */
int output_toggleWifiKey(interfaceMenu_t *pMenu, void* pArg)
{
	if( wifiInfo.auth == wifiAuthWEP )
		interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\d{10}", output_changeWifiKey, output_getWifiKey, inputModeDirect, pArg );
	else
		interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\w+", output_changeWifiKey, output_getWifiKey, inputModeABC, pArg );

	return 0;
}

static int output_toggleWifiWAN(interfaceMenu_t *pMenu, void* pArg)
{
	wifiInfo.wanMode = (wifiInfo.wanMode+1)%2;

#ifdef STBPNX
	int i = OUTPUT_INFO_GET_INDEX(pArg);
	char path[MAX_CONFIG_PATH];
	char value[16];

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));
	sprintf(value,"%d",wifiInfo.wanMode);

	if( !wifiInfo.wanMode )
	{
		setParam(path, "BOOTPROTO",      "static");
		setParam(path, "MODE",           "ad-hoc");
		if( wifiInfo.auth > wifiAuthWEP )
		{
			setParam(path, "AUTH",       "SHARED");
			setParam(path, "ENCRYPTION", "WEP");
			setParam(path, "KEY",        "0102030405");
		}
	}
	if( setParam(path, "WAN_MODE", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif
#ifdef STSDK
	if( wifiInfo.wanMode )
	{
		wifiInfo.mode = wifiModeManaged;
	} else {
		wifiInfo.mode = wifiModeMaster;
		wifiInfo.auth = wifiAuthWPA2PSK;
		wifiInfo.encryption = wifiEncAES;
	}
	wifiInfo.wanChanged = 1;

	if( output_writeInterfacesFile() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif

	output_fillWifiMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

#ifdef STBPNX
static int output_toggleWifiMode(interfaceMenu_t *pMenu, void* pArg)
{
	return output_changeWifiMode( pMenu, (void*)((wifiInfo.mode+1)%wifiModeCount));
}
#endif

int output_changeWifiMode(interfaceMenu_t *pMenu, void* pArg)
{

	wifiInfo.mode = (outputWifiMode_t)pArg;

#ifdef STBPNX
	char path[MAX_CONFIG_PATH];
	char *value = NULL;
	int i = ifaceWireless;

	switch(wifiInfo.mode)
	{
		case wifiModeCount:
		case wifiModeAdHoc:   value = "ad-hoc"; break;
		case wifiModeMaster:  value = "master"; break;
		case wifiModeManaged: value = "managed"; break;
	}

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

	if( setParam(path, "MODE", value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif
#ifdef STSDK
	if( output_writeInterfacesFile() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif

	output_fillWifiMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}
#endif

static void output_parseIP(char *value)
{
	int i;
	int ip_index, j;
	ip_index = 0;
	j = 0;
	memset( &output_ip, 0, sizeof(output_ip) );
	for( i = 0; ip_index < 4 && j < 4 && value[i]; i++ )
	{
		if( value[i] >= '0' && value[i] <= '9' )
			output_ip[(ip_index*4) + j++] = value[i];
		else if( value[i] == '.' )
		{
			ip_index++;
			j = 0;
		}
	}
}

static void output_getIP(void* pArg)
{
	char value[MENU_ENTRY_INFO_LENGTH];
	int i = OUTPUT_INFO_GET_INDEX(pArg);
	outputIPOption type = (outputIPOption)OUTPUT_INFO_GET_TYPE(pArg);

#ifdef STBPNX
	char path[MAX_CONFIG_PATH];
	char *key = "";
	switch(type)
	{
		case optionIP:   key = "IPADDR";          break;
		case optionGW:   key = "DEFAULT_GATEWAY"; break;
		case optionMask: key = "NETMASK";         break;
		case optionDNS:  key = "NAMESERVERS";     break;
		default:
			return;
	}

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

	if( getParam(path, key, "0.0.0.0", value) )
#endif
#ifdef STSDK
	outputNfaceInfo_t *nface = NULL;
	struct in_addr ip;

	switch( i )
	{
		case ifaceWAN: nface = &networkInfo.wan; break;
		case ifaceLAN: nface = &networkInfo.lan; break;
		default:
			return;
	}
	switch(type)
	{
		case optionIP:   ip = nface->ip;   break;
		case optionGW:   ip = nface->gw;   break;
		case optionMask: ip = nface->mask; break;
		case optionDNS:  ip = networkInfo.dns; break;
		default:
			return;
	}
	strcpy( value, inet_ntoa(ip) );
#endif
	{
		output_parseIP(value);
	}
}

static char* output_getIPfield(int field, void* pArg)
{
	return field < 4 ? &output_ip[field*4] : NULL;
}

static int output_changeIP(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	in_addr_t ip;
	int i = OUTPUT_INFO_GET_INDEX(pArg);
	outputIPOption type = (outputIPOption)OUTPUT_INFO_GET_TYPE(pArg);

	if( value == NULL )
		return 1;

	if (type != optionMode)
	{
		ip = inet_addr( inet_addr_prepare(value) );
		if(ip == INADDR_NONE || ip == INADDR_ANY)
		{
			interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
			return -1;
		}
	}
#ifdef STBPNX
	char path[MAX_CONFIG_PATH];
	char *key = "";

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

	switch(type)
	{
		case optionIP:   key = "IPADDR";          break;
		case optionGW:   key = "DEFAULT_GATEWAY"; break;
		case optionMask: key = "NETMASK";         break;
		case optionDNS:  key = "NAMESERVERS";     break;
		case optionMode: key = "BOOTPROTO";       break;
	}

	if( setParam(path, key, value) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif
#ifdef STSDK
	outputNfaceInfo_t *nface = NULL;
	switch( i )
	{
		case ifaceWAN: nface = &networkInfo.wan; break;
		case ifaceLAN: nface = &networkInfo.lan; break;
		default:
			eprintf("%s: unsupported iface %d\n", __FUNCTION__, i );
			return -1;
	}
	switch(type)
	{
		case optionIP:   nface->ip.s_addr   = ip; break;
		case optionGW:   nface->gw.s_addr   = ip; break;
		case optionMask: nface->mask.s_addr = ip; break;
		case optionDNS:
		{
			networkInfo.dns.s_addr = ip;
			FILE *f = fopen(STB_RESOLV_CONF, "w");
			if(!f)
			{
				interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
				break;
			}
			fprintf(f, "nameserver %s\n", value);
			fclose(f);
			f = fopen("/etc/resolv.conf", "w");
			if(f)
			{
				fprintf(f, "nameserver %s\n", value);
				fclose(f);
			}
			break;
		}
		//case optionMode: TODO
		default:
			eprintf("%s: unsupported type %d\n", __FUNCTION__, type );
			return -1;
	}
	if( i == ifaceLAN && networkInfo.lanMode == lanDhcpServer )
		output_writeDhcpConfig();
	if( output_writeInterfacesFile() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
#endif

	switch( i )
	{
		case ifaceWAN: output_fillWANMenu(pMenu, SET_NUMBER(type)); break;
#ifdef ENABLE_LAN
		case ifaceLAN: output_fillLANMenu(pMenu, SET_NUMBER(type)); break;
#endif
#ifdef ENABLE_WIFI
		case ifaceWireless: output_fillWifiMenu(pMenu, SET_NUMBER(type)); break;
#endif
	}
	interface_displayMenu(1);

	return 0;
}

static int output_toggleIP(interfaceMenu_t *pMenu, void* pArg)
{

	output_getIP(OUTPUT_INFO_SET(optionIP,GET_NUMBER(pArg)));
	interface_getText(pMenu, _T("ENTER_DEFAULT_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionIP,GET_NUMBER(pArg)) );

	return 0;
}

static int output_toggleGw(interfaceMenu_t *pMenu, void* pArg)
{

	output_getIP(OUTPUT_INFO_SET(optionGW,GET_NUMBER(pArg)));
	interface_getText(pMenu, _T("ENTER_GATEWAY"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionGW,GET_NUMBER(pArg)) );

	return 0;
}

static int output_toggleDNSIP(interfaceMenu_t *pMenu, void* pArg)
{

	output_getIP(OUTPUT_INFO_SET(optionDNS,GET_NUMBER(pArg)));
	interface_getText(pMenu, _T("ENTER_DNS_ADDRESS"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionDNS,GET_NUMBER(pArg)) );

	return 0;
}

static int output_toggleNetmask(interfaceMenu_t *pMenu, void* pArg)
{

	output_getIP(OUTPUT_INFO_SET(optionMask,GET_NUMBER(pArg)));
	interface_getText(pMenu, _T("ENTER_NETMASK"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionMask,GET_NUMBER(pArg)) );

	return 0;
}

#ifdef ENABLE_LAN
char *output_BWField(int field, void* pArg)
{
	if( field == 0 )
	{
		static char BWValue[MENU_ENTRY_INFO_LENGTH];
		getParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", "0", BWValue);
		return BWValue;
	} else
		return NULL;
}

char *output_MACField(int field, void* pArg)
{
	int i;
	char *ptr;
	static char MACValue[MENU_ENTRY_INFO_LENGTH];
	getParam(STB_CONFIG_FILE, "CONFIG_GATEWAY_CLIENT_MAC", "", MACValue);

	ptr = MACValue;
	for (i=0;i<field;i++)
	{
		ptr = strchr(ptr, ':');
		if (ptr == NULL)
		{
			return NULL;
		}
		ptr++;
	}

	ptr[2] = 0;

	return ptr;
}

#ifdef STBPNX
static int output_changeBW(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ivalue;
	char buf[32];

	if( value == NULL )
		return 1;

	if (helperFileExists(STB_CONFIG_OVERRIDE_FILE))
	{
		return 0;
	}

	interface_showMessageBox(_T("GATEWAY_IN_PROGRESS"), settings_renew, 0);

	ivalue = atoi(value);

	if (value[0] == 0 || ivalue <= 0)
	{
		strcpy(buf, "");
	} else
	{
		sprintf(buf, "%d", ivalue);
	}

	// Stop network interfaces
	system("/usr/local/etc/init.d/S90dhcpd stop");
	// Update settings
	setParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", buf);
	// Start network interfaces
	system("/usr/local/etc/init.d/S90dhcpd start");

	output_fillLANMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

static int output_toggleGatewayBW(interfaceMenu_t *pMenu, void* pArg)
{

	interface_getText(pMenu, _T("GATEWAY_BANDWIDTH_INPUT"), "\\d*", output_changeBW, output_BWField, inputModeDirect, pArg);

	return 0;
}
#endif // STBPNX

static int output_confirmGatewayMode(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
#ifdef STBPNX
	gatewayMode_t mode = (gatewayMode_t)pArg;
#endif

	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
#ifdef STBPNX
		if (helperFileExists(STB_CONFIG_OVERRIDE_FILE))
		{
			return 0;
		}
		if (mode >= gatewayModeCount )
		{
			return 0;
		}
#endif
#ifdef STSDK
		if( (lanMode_t)pArg >= lanModeCount )
		{
			return 0;
		}
#endif
		interface_showMessageBox(_T("GATEWAY_IN_PROGRESS"), settings_renew, 0);
#ifdef STBPNX
		char *str = "";
		switch( mode ) {
			case gatewayModeBridge: str = "BRIDGE"; break;
			case gatewayModeNAT:    str = "NAT"; break;
			case gatewayModeFull:   str = "FULL"; break;
			default:                str = "OFF"; break;
		}

		output_gatewayMode = mode;
#endif
#ifdef STSDK
		networkInfo.lanMode = (lanMode_t)pArg;
#endif

#ifdef STBPNX
		// Stop network interfaces
#ifdef ENABLE_WIFI
		system("/usr/local/etc/init.d/S80wifi stop");
#endif
		system("/usr/local/etc/init.d/S90dhcpd stop");
		system("/etc/init.d/S70servers stop");
#ifdef ENABLE_PPP
		system("/etc/init.d/S65ppp stop");
#endif
		system("/etc/init.d/S19network stop");
		// Update settings
		setParam(STB_CONFIG_FILE, "CONFIG_GATEWAY_MODE", str);
		// Start network interfaces
		system("/etc/init.d/S19network start");
#ifdef ENABLE_PPP
		system("/etc/init.d/S65ppp start");
#endif
		system("/etc/init.d/S70servers start");
		system("/usr/local/etc/init.d/S90dhcpd start");
#ifdef ENABLE_WIFI
		system("/usr/local/etc/init.d/S80wifi start");
#endif
#endif // STBPNX
#ifdef STSDK
		system("/etc/init.d/S40network stop");
		output_writeInterfacesFile();
		if( networkInfo.lanMode == lanDhcpServer )
			output_writeDhcpConfig();
		else
			unlink(STB_DHCPD_CONF);
		system("/etc/init.d/S40network start");
#endif

		output_fillGatewayMenu(pMenu, (void*)0);

		interface_hideMessageBox();

		return 0;
	}
	return 1;
}

static int output_toggleGatewayMode(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showConfirmationBox(_T("GATEWAY_MODE_CONFIRM"), thumbnail_question, output_confirmGatewayMode, pArg);
	return 0;
}
#endif // ENABLE_LAN

static int output_toggleReset(interfaceMenu_t *pMenu, void* pArg)
{
    interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 0);

#ifdef STBPNX
	char buf[PATH_MAX];
	int i = GET_NUMBER(pArg);
	switch(i)
	{
		case ifaceWAN:
#if !(defined STB225)
			sprintf(buf, "/usr/sbin/ifdown %s", helperEthDevice(i));
			system(buf);

			sleep(1);

			sprintf(buf, "/usr/sbin/ifup %s", helperEthDevice(i));
#else
			strcpy(buf, "/etc/init.d/additional/dhcp.sh");
#endif
			system(buf);
			output_fillWANMenu( (interfaceMenu_t*)&WANSubMenu, NULL );
			break;
#ifdef ENABLE_LAN
		case ifaceLAN:
			sprintf(buf, "/usr/local/etc/init.d/S90dhcpd stop");
			system(buf);

			sleep(1);

			sprintf(buf, "/usr/local/etc/init.d/S90dhcpd start");
			system(buf);
			break;
#endif
#ifdef ENABLE_WIFI
		case ifaceWireless:
			gfx_stopVideoProviders(screenMain);
#ifdef USE_WPA_SUPPLICANT
			if (wifiInfo.wanMode)
				output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
#endif
#if (defined STB225)
			sprintf(buf, "/etc/init.d/additional/wifi.sh stop");
#else
			sprintf(buf, "/usr/local/etc/init.d/S80wifi stop");
#endif
			system(buf);

			sleep(1);

#if (defined STB225)
			sprintf(buf, "/etc/init.d/additional/wifi.sh start");
#else
			sprintf(buf, "/usr/local/etc/init.d/S80wifi start");
#endif
			system(buf);
			break;
#endif
		default:
			eprintf("%s: unknown iface %d\n", __FUNCTION__, i);
	}
#endif // STBPNX
#ifdef STSDK
#ifdef ENABLE_WIFI
	if( ifaceWireless == GET_NUMBER(pArg) && networkInfo.lanMode != lanBridge && wifiInfo.wanChanged == 0 )
	{
		system("ifdown wlan0");
		system("ifup wlan0");
	} else
#endif
	{
	system("/etc/init.d/S40network stop");
	sleep(1);
	system("/etc/init.d/S40network start");
#ifdef ENABLE_WIFI
	wifiInfo.wanChanged = 0;
#endif
	}
#endif // STSDK

    interface_hideMessageBox();

	return 0;
}

static int output_changeProxyAddr(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	char *ptr1, *ptr2;
	char buf[MENU_ENTRY_INFO_LENGTH];
	int ret = 0;
	int port, i;

	if( value == NULL )
		return 1;

	strcpy(buf, value);

	ptr2 = buf;
	ptr1 = strchr(buf, ':');
	*ptr1 = 0;
	ptr1++;

	if (strlen(ptr1) == 0 && strlen(ptr2) == 3 /* dots */)
	{
		appControlInfo.networkInfo.proxy[0] = 0;
#ifdef STBPNX
		ret = setParam(BROWSER_CONFIG_FILE, "HTTPProxyServer", NULL);
#endif
#ifdef STSDK
		unlink(ELCD_PROXY_CONFIG_FILE);
		ret = 0;
#endif
		unsetenv("http_proxy");
	}
	else
	{
		ptr2 = inet_addr_prepare(ptr2);
		if (inet_addr(ptr2) == INADDR_NONE || inet_addr(ptr2) == INADDR_ANY)
		{
			interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
			return -1;
		}

		if ( (port = atoi(ptr1)) <= 0)
		{
			interface_showMessageBox(_T("ERR_INCORRECT_PORT"), thumbnail_error, 0);
			return -1;
		}

		ptr2[strlen(ptr2) + 7] = 0;
		for ( i = strlen(ptr2) - 1 + 7; i > 6; --i)
		{
			ptr2[i] = ptr2[i-7];
		}
		strncpy(ptr2,"http://",7);
		sprintf(&ptr2[strlen(ptr2)], ":%d", port);
		dprintf("%s: HTTPProxyServer=%s\n", __FUNCTION__,ptr2);
		strcpy( appControlInfo.networkInfo.proxy, ptr2+7 );
#ifdef STBPNX
		ret = setParam(BROWSER_CONFIG_FILE, "HTTPProxyServer", ptr2);
#endif
#ifdef STSDK
		if( (ret = saveProxySettings()) != 0 )
			interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
#endif
		ret |= setenv("http_proxy", ptr2, 1);
	}
#ifdef STSDK
	system( "killall -SIGHUP '" ELCD_BASENAME "'" );
#endif

	if (ret == 0)
	{
		output_fillWebMenu(pMenu, 0);
		interface_displayMenu(1);
	}
	return ret;
}

static char* output_getOption(outputUrlOption option)
{
	switch(option)
	{
#ifdef ENABLE_IPTV
		case optionRtpEpg:
			return appControlInfo.rtpMenuInfo.epg;
		case optionRtpPlaylist:
			return appControlInfo.rtpMenuInfo.playlist;
#endif
#ifdef ENABLE_VOD
		case optionVodPlaylist:
			return appControlInfo.rtspInfo.streamInfoUrl;
#endif
		default:;
	}
	eprintf("%s: unknown option %d\n", __FUNCTION__, option);
	return NULL;
}

static int output_changeURL(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret;
	char *dest;

	if( value == NULL )
	{
		if( (outputUrlOption)pArg == optionRtpPlaylist && appControlInfo.rtpMenuInfo.playlist[0] == 0 )
		{
			appControlInfo.rtpMenuInfo.usePlaylistURL = 0;
		}
		return 1;
	}

	dest = output_getOption((outputUrlOption)pArg);
	if( dest == NULL )
		return 1;

	if( value[0] == 0 )
	{
		dest[0] = 0;
		if( (outputUrlOption)pArg == optionRtpPlaylist )
		{
			appControlInfo.rtpMenuInfo.usePlaylistURL = 0;
		}
	} else
	{
		if( strlen( value ) < 8 )
		{
			interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_warning, 3000);
			return 1;
		}
		if( strncasecmp( value, "http://", 7 ) == 0 || strncasecmp( value, "https://", 8 ) == 0 )
		{
			strcpy(dest, value);
		} else
		{
			sprintf(dest, "http://%s",value);
		}
	}
	if ((ret = saveAppSettings() != 0) && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
	if (ret == 0)
	{
		switch((outputUrlOption)pArg)
		{
#ifdef ENABLE_IPTV
			case optionRtpEpg:
				rtp_cleanupEPG();
				output_fillIPTVMenu(pMenu, NULL);
				break;
			case optionRtpPlaylist:
				rtp_cleanupPlaylist(screenMain);
				output_fillIPTVMenu(pMenu, NULL);
				break;
#endif
#ifdef ENABLE_VOD
			case optionVodPlaylist:
				output_fillVODMenu(pMenu, NULL);
#endif
			default:;
		}
		interface_displayMenu(1);
	}

	return ret;
}

static int output_changeProxyLogin(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret = 0;

	if( value == NULL )
		return 1;

	strncpy(appControlInfo.networkInfo.login, value, sizeof(appControlInfo.networkInfo.login));
	appControlInfo.networkInfo.login[sizeof(appControlInfo.networkInfo.login)-1]=0;
#ifdef STBPNX
	ret = setParam(BROWSER_CONFIG_FILE, "HTTPProxyLogin", value);
#endif
#ifdef STSDK
	ret = saveProxySettings();
#endif
	if (ret == 0)
	{
		setenv("http_proxy_login", value, 1);
		output_fillWebMenu(pMenu, 0);
		interface_displayMenu(1);
	}

	return ret;
}

static int output_changeProxyPasswd(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret = 0;

	if( value == NULL )
		return 1;

	strncpy(appControlInfo.networkInfo.passwd, value, sizeof(appControlInfo.networkInfo.passwd));
	appControlInfo.networkInfo.passwd[sizeof(appControlInfo.networkInfo.passwd)-1]=0;
#ifdef STBPNX
	ret = setParam(BROWSER_CONFIG_FILE, "HTTPProxyPasswd", value);
#endif
#ifdef STSDK
	ret = saveProxySettings();
#endif
	if (ret == 0)
	{
		setenv("http_proxy_passwd", value, 1);
		output_fillWebMenu(pMenu, 0);
		interface_displayMenu(1);
	}

	return ret;
}

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

static int output_resetSettings(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showConfirmationBox(_T("RESET_SETTINGS_CONFIRM"), thumbnail_question, output_confirmReset, pArg);
	return 1; // when called from askPassword, should return non-0 to leave getText message box opened
}

static char *output_getURL(int index, void* pArg)
{
	return index == 0 ? output_getOption((outputUrlOption)pArg) : NULL;
}

static int output_toggleURL(interfaceMenu_t *pMenu, void* pArg)
{
	char *str = "";
	switch( (outputUrlOption)pArg )
	{
		case optionRtpEpg:      str = _T("ENTER_IPTV_EPG_ADDRESS");  break;
		case optionRtpPlaylist: str = _T("ENTER_IPTV_LIST_ADDRESS"); break;
		case optionVodPlaylist: str = _T("ENTER_VOD_LIST_ADDRESS");  break;
	}
	interface_getText(pMenu, str, "\\w+", output_changeURL, output_getURL, inputModeABC, pArg);
	return 0;
}

#ifdef ENABLE_IPTV
static int output_toggleIPTVPlaylist(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.rtpMenuInfo.usePlaylistURL = (appControlInfo.rtpMenuInfo.usePlaylistURL+1)%2;
	if( appControlInfo.rtpMenuInfo.usePlaylistURL && appControlInfo.rtpMenuInfo.playlist[0] == 0 )
	{
		output_toggleURL( pMenu, (void*)optionRtpPlaylist );
	} else
	{
		if (saveAppSettings() != 0 && bDisplayedWarning == 0)
		{
			bDisplayedWarning = 1;
			interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
		}
		output_fillIPTVMenu( pMenu, pArg );
		interface_displayMenu(1);
	}
	return 0;
}
#endif

#ifdef ENABLE_VOD
static int output_toggleVODPlaylist(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.rtspInfo.usePlaylistURL = (appControlInfo.rtspInfo.usePlaylistURL+1)%2;
	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
	output_fillVODMenu( pMenu, pArg );
	interface_displayMenu(1);
	return 0;
}
#endif

static int output_toggleProxyAddr(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_PROXY_ADDR"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}:\\d+", output_changeProxyAddr, NULL, inputModeDirect, pArg);
	return 0;
}

static int output_toggleProxyLogin(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_PROXY_LOGIN"), "\\w+", output_changeProxyLogin, NULL, inputModeABC, pArg);
	return 0;
}

static int output_toggleProxyPasswd(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_PROXY_PASSWD"), "\\w+", output_changeProxyPasswd, NULL, inputModeABC, pArg);
	return 0;
}

#ifdef ENABLE_BROWSER
char *output_getMWAddr(int field, void* pArg)
{
	if( field == 0 )
	{
#ifdef STBPNX
		static char MWaddr[MENU_ENTRY_INFO_LENGTH];
		getParam(BROWSER_CONFIG_FILE, "HomeURL", "", MWaddr);
		return MWaddr;
#endif
	}
	return NULL;
}

static int output_changeMWAddr(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret = 0;

	if( value == NULL )
		return 1;

#ifdef STBPNX
	char buf[MENU_ENTRY_INFO_LENGTH];

	strcpy(buf, value);

	if(strncmp(value,"http",4) != 0)
	{
		sprintf(buf, "http://%s/", value);
	} else
	{
		strcpy(buf,value);
	}
	ret = setParam(BROWSER_CONFIG_FILE, "HomeURL", buf);

	if (ret == 0)
	{
		output_fillWebMenu(pMenu, 0);
		interface_displayMenu(1);
	}
#endif
	return ret;
}

static int output_toggleMWAddr(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_MW_ADDR"), "\\w+", output_changeMWAddr, output_getMWAddr, inputModeABC, pArg);
	return 0;
}

static int output_toggleMWAutoLoading(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STBPNX
	char *str;
	char temp[256];

	getParam(BROWSER_CONFIG_FILE, "AutoLoadingMW", "OFF", temp);
	dprintf("%s: %s \n", __FUNCTION__,temp);
	if (strcmp(temp,"OFF"))
	{
		dprintf("%s: Set OFF \n", __FUNCTION__);
		str = "OFF";
	} else
	{
		dprintf("%s: Set ON  \n", __FUNCTION__);
		str = "ON";
	}
	setParam(BROWSER_CONFIG_FILE, "AutoLoadingMW",str);
	output_fillWebMenu(pMenu, 0);
	interface_displayMenu(1);
#endif
	return 0;
}
#endif // ENABLE_BROWSER

static int output_toggleMode(interfaceMenu_t *pMenu, void* pArg)
{
	int i = GET_NUMBER(pArg);

#ifdef STBPNX
	char value[MENU_ENTRY_INFO_LENGTH];
	char path[MAX_CONFIG_PATH];

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));
	getParam(path, "BOOTPROTO", "static", value);

	if (strcmp("dhcp+dns", value) == 0)
	{
		strcpy(value, "static");
	} else
	{
		strcpy(value, "dhcp+dns");
	}

	output_changeIP(pMenu, value, OUTPUT_INFO_SET( optionMode,i));
#endif
#ifdef STSDK
	switch( i )
	{
		case ifaceWAN:
			networkInfo.wanDhcp = !networkInfo.wanDhcp;
			break;
		case ifaceLAN:
			networkInfo.lanMode = (networkInfo.lanMode+1)%lanModeCount;
			if( networkInfo.lanMode != lanDhcpServer )
				unlink(STB_DHCPD_CONF);
			else
				output_writeDhcpConfig();
			break;
		default:
			return 0;
	}

	if( 0 != output_writeInterfacesFile() )
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
#endif


	switch( i )
	{
		case ifaceWAN: output_fillWANMenu(pMenu, SET_NUMBER(i)); break;
#ifdef ENABLE_LAN
		case ifaceLAN: output_fillLANMenu(pMenu, SET_NUMBER(i)); break;
#endif
#ifdef ENABLE_WIFI
		case ifaceWireless: output_fillWifiMenu(pMenu, SET_NUMBER(i)); break;
#endif
	}
	interface_displayMenu(1);

	return 0;
}

#ifdef ENABLE_VOD
static int output_changeVODIP(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if( value == NULL )
		return 1;

	value = inet_addr_prepare(value);
	if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
		return -1;
	}

	strcpy(appControlInfo.rtspInfo.streamIP, value);

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillVODMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

static int output_changeVODINFOIP(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if( value == NULL )
		return 1;

	value = inet_addr_prepare(value);
	if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
		return -1;
	}

	strcpy(appControlInfo.rtspInfo.streamInfoIP, value);

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillVODMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

static int output_toggleVODIP(interfaceMenu_t *pMenu, void* pArg)
{
	output_parseIP( appControlInfo.rtspInfo.streamIP );
	interface_getText(pMenu, _T("ENTER_VOD_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeVODIP, output_getIPfield, inputModeDirect, NULL);

	return 0;
}

static int output_toggleVODINFOIP(interfaceMenu_t *pMenu, void* pArg)
{
	output_parseIP( appControlInfo.rtspInfo.streamInfoIP );
	interface_getText(pMenu, _T("ENTER_VOD_INFO_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeVODINFOIP, output_getIPfield, inputModeDirect, NULL);

	return 0;
}
#endif /* ENABLE_VOD */

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

	return 0;
}

static int output_clearOffairSettings(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showConfirmationBox(_T("DVB_CLEAR_CHANNELS_CONFIRM"), thumbnail_question, output_confirmClearOffair, pArg);

	return 0;
}

static int output_confirmClearDvb(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		offair_clearServiceList(1);
		output_fillDVBMenu(pMenu, pArg);
		return 0;
	}

	return 1;
}

static int output_confirmClearOffair(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		offair_clearServices();
		offair_initServices();
		offair_fillDVBTOutputMenu(screenMain);
		return 0;
	}

	return 1;
}

#ifdef ENABLE_DVB_DIAG
static int output_toggleDvbDiagnosticsOnStart(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.offairInfo.diagnosticsMode = appControlInfo.offairInfo.diagnosticsMode != DIAG_ON ? DIAG_ON : DIAG_FORCED_OFF;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}
#endif

static int output_toggleDvbShowScrambled(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.offairInfo.dvbShowScrambled = (appControlInfo.offairInfo.dvbShowScrambled + 1) % 3;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillDVBMenu(pMenu, pArg);
	offair_initServices();
	offair_fillDVBTMenu();
	offair_fillDVBTOutputMenu(screenMain);
	interface_displayMenu(1);
	return 0;
}

static int output_toggleDvbNetworkSearch(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbCommonInfo.networkScan = !appControlInfo.dvbCommonInfo.networkScan;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}

#ifdef STBPNX
static int output_toggleDvbInversion(interfaceMenu_t *pMenu, void* pArg)
{
	stb810_dvbfeInfo *fe;

	switch( dvb_getType(0) )
	{
		case FE_OFDM:
			fe = &appControlInfo.dvbtInfo.fe;
			break;
		case FE_QAM:
			fe = &appControlInfo.dvbcInfo.fe;
			break;
		default:
			return 0;
	}
	fe->inversion = fe->inversion == 0 ? 1 : 0;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
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

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}

static int output_toggleDvbSpeed(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbCommonInfo.adapterSpeed = (appControlInfo.dvbCommonInfo.adapterSpeed+1) % 11;

    if (saveAppSettings() != 0 && bDisplayedWarning == 0)
    {
    	bDisplayedWarning = 1;
    	interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
    }

    output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);
    return 0;
}

static int output_toggleDvbExtScan(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.dvbCommonInfo.extendedScan ^= 1;

    if (saveAppSettings() != 0 && bDisplayedWarning == 0)
    {
    	bDisplayedWarning = 1;
    	interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
    }

    output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);
    return 0;
}

static stb810_dvbfeInfo* getDvbRange(void)
{
	stb810_dvbfeInfo *fe = NULL;

	switch (dvb_getType(0))
	{
		case FE_OFDM:
			fe = &appControlInfo.dvbtInfo.fe;
			break;
		case FE_QAM:
			fe = &appControlInfo.dvbcInfo.fe;
			break;
		case FE_QPSK:
			if (appControlInfo.dvbsInfo.band == dvbsBandC)
				fe = &appControlInfo.dvbsInfo.c_band;
			else
				fe = &appControlInfo.dvbsInfo.k_band;
			break;
	}
	return fe;
}

static char *output_getDvbRange(int field, void* pArg)
{
	if( field == 0 )
	{
		static char buffer[32];
		int id = GET_NUMBER(pArg);
		buffer[0] = 0;
		stb810_dvbfeInfo *fe = getDvbRange();
		if (fe == NULL)
			return buffer;
		switch (id)
		{
			case 0: sprintf(buffer, "%ld", fe->lowFrequency); break;
			case 1: sprintf(buffer, "%ld", fe->highFrequency); break;
			case 2: sprintf(buffer, "%ld", fe->frequencyStep); break;
			case 3: sprintf(buffer, "%u",  appControlInfo.dvbcInfo.symbolRate); break;
		}

		return buffer;
	} else
		return NULL;
}

static int output_changeDvbRange(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int id = GET_NUMBER(pArg);
	long val;
	stb810_dvbfeInfo *fe;

	if( value == NULL )
		return 1;

	val = strtol(value, NULL, 10);
	fe = getDvbRange();
	if (fe == NULL)
		return 0;

	if ( (id < 3 && (val < 1000 || val > 860000)) ||
		 (id == 3 && (val < 1 || val > 50000)) )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_FREQUENCY"), thumbnail_error, 0);
		return -1;
	}

	switch (id)
	{
		case 0: fe->lowFrequency = val; break;
		case 1: fe->highFrequency = val; break;
		case 2: fe->frequencyStep = val; break;
		case 3: appControlInfo.dvbcInfo.symbolRate = val; break;
		default: return 0;
	}

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);

	return 0;
}

static int output_toggleDvbModulation(interfaceMenu_t *pMenu, void* pArg)
{
	switch (appControlInfo.dvbcInfo.modulation)
	{
		case QAM_16: appControlInfo.dvbcInfo.modulation = QAM_32; break;
		case QAM_32: appControlInfo.dvbcInfo.modulation = QAM_64; break;
		case QAM_64: appControlInfo.dvbcInfo.modulation = QAM_128; break;
		case QAM_128: appControlInfo.dvbcInfo.modulation = QAM_256; break;
		default: appControlInfo.dvbcInfo.modulation = QAM_16;
	}

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillDVBMenu(pMenu, pArg);
	interface_displayMenu(1);

	return 0;
}

static char *get_HZprefix(tunerFormat tuner)
{
	if(dvb_getType(tuner) == FE_QPSK) //if dvb-s, use MHz
		return _T("MHZ");
	else
		return _T("KHZ");
}

static int output_toggleDvbRange(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	int id = GET_NUMBER(pArg);
	char *HZ_prefix = get_HZprefix(offair_getTuner());

	switch (id)
	{
		case 0: sprintf(buf, "%s, %s: ", _T("DVB_LOW_FREQ"), HZ_prefix); break;
		case 1: sprintf(buf, "%s, %s: ", _T("DVB_HIGH_FREQ"), HZ_prefix); break;
		case 2: sprintf(buf, "%s, %s: ", _T("DVB_STEP_FREQ"), _T("KHZ")); break;
		case 3: sprintf(buf, "%s, %s: ", _T("DVB_SYMBOL_RATE"), _T("KHZ")); break;
		default: return -1;
	}

	interface_getText(pMenu, buf, "\\d+", output_changeDvbRange, output_getDvbRange, inputModeDirect, pArg);

	return 0;
}

int output_fillDVBMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str;
	stb810_dvbfeInfo	*fe;
	tunerFormat			tuner;
	char				*HZ_prefix;

	tuner = offair_getTuner();
	HZ_prefix = get_HZprefix(tuner);

	interface_clearMenuEntries((interfaceMenu_t*)&DVBSubMenu);

	if ((fe = getDvbRange()) == NULL)
		return interface_menuActionShowMenu(pMenu, (void*)&DVBSubMenu);

#ifdef STSDK
	if( tuner < VMSP_COUNT ) {
#endif

	sprintf(buf, PROFILE_LOCATIONS_PATH "/%s", appControlInfo.offairInfo.profileLocation);
	if (getParam(buf, "LOCATION_NAME", NULL, NULL))
	{
		str = _T("SETTINGS_WIZARD");
		interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, str, offair_wizardStart, NULL, thumbnail_scan);
	}

	str = _T("DVB_MONITOR");
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, str, offair_frequencyMonitor, NULL, thumbnail_info);

#ifdef STSDK
	}
#endif

	str = _T("DVB_INSTALL");
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, str, offair_serviceScan, NULL, thumbnail_scan);

	str = _T("DVB_SCAN_FREQUENCY");
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, str, offair_frequencyScan, screenMain, thumbnail_scan);

	sprintf(buf, "%s (%d)", _T("DVB_CLEAR_SERVICES"), dvb_getNumberOfServices());
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_clearDvbSettings, screenMain, thumbnail_scan);

	str = _T("DVB_CLEAR_CHANNELS");
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, str, output_clearOffairSettings, screenMain, thumbnail_scan);

#ifdef ENABLE_DVB_DIAG
	sprintf(buf, "%s: %s", _T("DVB_START_WITH_DIAGNOSTICS"), _T( appControlInfo.offairInfo.diagnosticsMode == DIAG_FORCED_OFF ? "OFF" : "ON" ) );
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbDiagnosticsOnStart, NULL, thumbnail_configure);
#endif

	sprintf(buf, "%s: %s", _T("DVB_SHOW_SCRAMBLED"), _T( appControlInfo.offairInfo.dvbShowScrambled == SCRAMBLED_PLAY ? "PLAY" : (appControlInfo.offairInfo.dvbShowScrambled ? "ON" : "OFF") ) );
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbShowScrambled, NULL, thumbnail_configure);

#ifdef STSDK
	if( offair_getTuner() < VMSP_COUNT ) {
#endif
	sprintf(buf, "%s: %s", _T("DVB_NETWORK_SEARCH"), _T( appControlInfo.dvbCommonInfo.networkScan ? "ON" : "OFF" ) );
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbNetworkSearch, NULL, thumbnail_configure);
#ifdef STSDK
	}
#endif
#ifdef STBPNX
	sprintf(buf, "%s: %s", _T("DVB_INVERSION"), _T( fe->inversion ? "ON" : "OFF" ) );
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbInversion, NULL, thumbnail_configure);
#endif
	if (dvb_getType(0) == FE_OFDM)
	{
		switch (appControlInfo.dvbtInfo.bandwidth)
		{
			case BANDWIDTH_8_MHZ: sprintf(buf, "%s: 8 %s", _T("DVB_BANDWIDTH"), _T( "MHZ" ) ); break;
			case BANDWIDTH_7_MHZ: sprintf(buf, "%s: 7 %s", _T("DVB_BANDWIDTH"), _T( "MHZ" ) ); break;
			case BANDWIDTH_6_MHZ: sprintf(buf, "%s: 6 %s", _T("DVB_BANDWIDTH"), _T( "MHZ" ) ); break;
			default:
				sprintf(buf, "%s: Auto", _T("DVB_BANDWIDTH") );
		}
		interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbBandwidth, NULL, thumbnail_configure);
	} else if (dvb_getType(0) == FE_QAM)
	{
		char *mod;

		sprintf(buf, "%s: %u %s", _T("DVB_SYMBOL_RATE"),appControlInfo.dvbcInfo.symbolRate, _T("KHZ"));
		interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbRange, (void*)3, thumbnail_configure);

		switch (appControlInfo.dvbcInfo.modulation)
		{
			case QAM_16: mod = "QAM16"; break;
			case QAM_32: mod = "QAM32"; break;
			case QAM_64: mod = "QAM64"; break;
			case QAM_128: mod = "QAM128"; break;
			case QAM_256: mod = "QAM256"; break;
			default: mod = _T("NOT_AVAILABLE_SHORT");
		}

		sprintf(buf, "%s: %s", _T("DVB_QAM_MODULATION"), mod);
		interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbModulation, NULL, thumbnail_configure);
	}

	sprintf(buf, "%s: %ld %s", _T("DVB_LOW_FREQ"), fe->lowFrequency, HZ_prefix);
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbRange, (void*)0, thumbnail_configure);

	sprintf(buf, "%s: %ld %s", _T("DVB_HIGH_FREQ"),fe->highFrequency, HZ_prefix);
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbRange, (void*)1, thumbnail_configure);

#ifdef STSDK
	if( offair_getTuner() < VMSP_COUNT ) {
#endif
	if (appControlInfo.dvbCommonInfo.adapterSpeed > 0)
	{
		sprintf(buf, "%s: %d%%", _T("DVB_SPEED"), 100-100*appControlInfo.dvbCommonInfo.adapterSpeed/10);
	} else
	{
		sprintf(buf, "%s: 1", _T("DVB_SPEED"));
	}
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbSpeed, NULL, thumbnail_configure);

	sprintf(buf,"%s: %s", _T("DVB_EXT_SCAN") , _T( appControlInfo.dvbCommonInfo.extendedScan == 0 ? "OFF" : "ON" ));
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbExtScan, NULL, thumbnail_configure);

	sprintf(buf, "%s: %ld %s", _T("DVB_STEP_FREQ"), fe->frequencyStep, _T("KHZ"));
	interface_addMenuEntry((interfaceMenu_t*)&DVBSubMenu, buf, output_toggleDvbRange, (void*)2, thumbnail_configure);
#ifdef STSDK
	}
#endif

	interface_menuActionShowMenu(pMenu, (void*)&DVBSubMenu);

	return 0;
}
#endif /* ENABLE_DVB */

#ifdef ENABLE_VERIMATRIX
static int output_toggleVMEnable(interfaceMenu_t *pMenu, void* pArg)
{
    if (appControlInfo.useVerimatrix == 0)
    {
    	appControlInfo.useVerimatrix = 1;
    }
    else
    {
    	appControlInfo.useVerimatrix = 0;
    }

    if (saveAppSettings() != 0 && bDisplayedWarning == 0)
    {
    	bDisplayedWarning = 1;
    	interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
    }

    output_fillNetworkMenu(pMenu, 0);
	interface_displayMenu(1);
    return 0;
}

static int output_changeVMAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret;

	if( value == NULL )
		return 1;

	value = inet_addr_prepare(value);
	if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
		return -1;
	}

	ret = setParam(VERIMATRIX_INI_FILE, "SERVERADDRESS", value);
	if (ret == 0)
	{
		char buf[64];

		sprintf(buf, "%s/%d", value, VERIMATRIX_VKS_PORT);
		ret = setParam(VERIMATRIX_INI_FILE, "PREFERRED_VKS", buf);
	}

	if (ret == 0)
	{
		output_fillNetworkMenu(pMenu, 0);
		interface_displayMenu(1);
	}

	return ret;
}

static size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp)
{
	int len = strlen((char*)userp);

	if (len+size*nmemb >= DATA_BUFF_SIZE)
	{
		size = 1;
		nmemb = DATA_BUFF_SIZE-len-1;
	}

	memcpy(&((char*)userp)[len], buffer, size*nmemb);
	((char*)userp)[len+size*nmemb] = 0;
	return size*nmemb;
}

static int output_getVMRootCert(interfaceMenu_t *pMenu, void* pArg)
{
	char info_url[MAX_URL];
	int fd, code;
	char rootcert[DATA_BUFF_SIZE];
	char errbuff[CURL_ERROR_SIZE];
	CURLcode ret;
	CURL *hnd;

	hnd = curl_easy_init();

	memset(rootcert, 0, sizeof(rootcert));

	sprintf(info_url, "http://%s/%s", appControlInfo.rtspInfo.streamInfoIP, "rootcert.pem");

	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, rootcert);
	curl_easy_setopt(hnd, CURLOPT_URL, info_url);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, errbuff);
	curl_easy_setopt(hnd, CURLOPT_CONNECTTIMEOUT, 5);
	curl_easy_setopt(hnd, CURLOPT_TIMEOUT, 15);

	ret = curl_easy_perform(hnd);

	if (ret == 0)
	{
		curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &code);
		if (code != 200)
		{
			ret = -1;
		} else
		{
			dprintf("%s: rootcert data %d:\n%s\n", __FUNCTION__, strlen(rootcert), rootcert);
		}
	}

	curl_easy_cleanup(hnd);

	if (ret == 0)
	{
		getParam(VERIMATRIX_INI_FILE, "ROOTCERT", VERIMATRIX_ROOTCERT_FILE, errbuff);
		dprintf("%s: Will write to: %s\n", __FUNCTION__, errbuff);
		fd = open(errbuff, O_CREAT|O_WRONLY|O_TRUNC);
		if (fd > 0)
		{
			write(fd, rootcert, strlen(rootcert));
			close(fd);
		} else
		{
			ret = -1;
		}
	}

	if (ret == 0)
	{
		interface_showMessageBox(_T("GOT_ROOTCERT"), thumbnail_yes, 0);
		return ret;
	} else
	{
		interface_showMessageBox(_T("ERR_FAILED_TO_GET_ROOTCERT"), thumbnail_error, 0);
		return ret;
	}

	return ret;
}

static int output_changeVMCompany(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret;

	if( value == NULL )
		return 1;

	ret = setParam(VERIMATRIX_INI_FILE, "COMPANY", value);

	if (ret == 0)
	{
		output_fillNetworkMenu(pMenu, 0);
		interface_displayMenu(1);
	}

	return ret;
}

static int output_toggleVMAddress(interfaceMenu_t *pMenu, void* pArg)
{

	interface_getText(pMenu, _T("VERIMATRIX_ADDRESS"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeVMAddress, NULL, inputModeDirect, pArg);

	return 0;
}

static int output_toggleVMCompany(interfaceMenu_t *pMenu, void* pArg)
{

	interface_getText(pMenu, _T("VERIMATRIX_COMPANY"), "\\w+", output_changeVMCompany, NULL, inputModeABC, pArg);

	return 0;
}
#endif // #ifdef ENABLE_VERIMATRIX

#ifdef ENABLE_SECUREMEDIA
static int output_toggleSMEnable(interfaceMenu_t *pMenu, void* pArg)
{
    if (appControlInfo.useSecureMedia == 0)
    {
    	appControlInfo.useSecureMedia = 1;
    }
    else
    {
    	appControlInfo.useSecureMedia = 0;
    }

    if (saveAppSettings() != 0 && bDisplayedWarning == 0)
    {
    	bDisplayedWarning = 1;
    	interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
    }

    output_fillNetworkMenu(pMenu, 0);
	interface_displayMenu(1);
    return 0;
}

static int output_changeSMAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int type = GET_NUMBER(pArg);
	int ret;

	if( value == NULL )
		return 1;

	value = inet_addr_prepare(value);
	if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
		return -1;
	}

	if (type == 1)
	{
		ret = setParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_ESAM_HOST", value);
	} else
	{
		ret = setParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_RANDOM_HOST", value);
	}

	if (ret == 0)
	{
		/* Restart smdaemon */
		system("killall smdaemon");
		output_fillNetworkMenu(pMenu, 0);
		interface_displayMenu(1);
	}

	return ret;
}

static int output_toggleSMAddress(interfaceMenu_t *pMenu, void* pArg)
{
	int type = GET_NUMBER(pArg);

	if (type == 1)
	{
		interface_getText(pMenu, _T("SECUREMEDIA_ESAM_HOST"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeSMAddress, NULL, inputModeDirect, pArg);
	} else
	{
		interface_getText(pMenu, _T("SECUREMEDIA_RANDOM_HOST"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_changeSMAddress, NULL, inputModeDirect, pArg);
	}

	return 0;
}
#endif // #ifdef ENABLE_SECUREMEDIA

#ifdef STB82
static int output_toggleAspectRatio(interfaceMenu_t *pMenu, void* pArg)
{
	if (appControlInfo.outputInfo.aspectRatio == aspectRatio_4x3)
	{
		appControlInfo.outputInfo.aspectRatio = aspectRatio_16x9;
	} else
	{
		appControlInfo.outputInfo.aspectRatio = aspectRatio_4x3;
	}

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

    output_fillVideoMenu(pMenu, pArg);
	interface_displayMenu(1);
    return 0;
}

static int output_toggleAutoScale(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.outputInfo.autoScale = appControlInfo.outputInfo.autoScale == videoMode_scale ? videoMode_stretch : videoMode_scale;

#ifdef STB225
	gfxUseScaleParams = 0;
    (void)event_send(gfxDimensionsEvent);
#endif

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

    output_fillVideoMenu(pMenu, pArg);
	interface_displayMenu(1);
    return 0;
}

static int output_toggleScreenFiltration(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.outputInfo.bScreenFiltration = (appControlInfo.outputInfo.bScreenFiltration + 1) % 2;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillVideoMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}
#endif // STB82

/* -------------------------- MENU DEFINITION --------------------------- */


static void output_fillStandardMenu(void)
{
    int selected = MENU_ITEM_BACK;
	char *str;

    //StandardMenu.prev = &OutputMenu;
	interface_clearMenuEntries((interfaceMenu_t*)&StandardMenu);
#ifdef STB6x8x
    /*Add menu items automatically*/
    if (appControlInfo.outputInfo.encDesc[0].caps & DSECAPS_TV_STANDARDS)
    {
        int n;
        int position = 0;
        for (n=0; tv_standards[n].standard; n++)
        {
            /* the following will only work if the supported resolutions is only one value when you have a DIGITAL (HD) output.*/
            if (appControlInfo.outputInfo.encDesc[0].tv_standards & tv_standards[n].standard)
            {
                if (tv_standards[n].standard == appControlInfo.outputInfo.encConfig[0].tv_standard)
                {
                    selected = position;
                }
#ifdef STBPNX
				str = (tv_standards[n].standard == DSETV_DIGITAL) ? (char*)resolutions[0].name : (char*) tv_standards[n].name;
#else
				str = (char*) tv_standards[n].name;
#endif
				interface_addMenuEntry((interfaceMenu_t*)&StandardMenu, str, output_setStandard, (void*) tv_standards[n].standard,
				                       tv_standards[n].standard == appControlInfo.outputInfo.encConfig[0].tv_standard ? thumbnail_selected : thumbnail_tvstandard);
				position++;
            }
        }
    }
#else
	{
		char buf[128];
		getParam("/etc/init.d/S35pnxcore.sh", "resolution", "1280x720x60p", buf);

		str = "1280x720x60p";
		interface_addMenuEntry((interfaceMenu_t*)&StandardMenu, str, output_setStandard, (void*) 720, strstr(buf, str) != 0 ? thumbnail_selected : thumbnail_tvstandard);
		str = "1920x1080x60i";
		interface_addMenuEntry((interfaceMenu_t*)&StandardMenu, str, output_setStandard, (void*) 1080, strstr(buf, str) != 0 ? thumbnail_selected : thumbnail_tvstandard);
		selected = strstr(buf, str) != 0;
	}
#endif
    /* Ensure that the correct entry is selected */
	interface_setSelectedItem((interfaceMenu_t*)&StandardMenu, selected);
}

static int output_setDate(interfaceMenu_t *pMenu, void* pArg)
{
	struct tm newtime;
	struct tm *lt;
	time_t t;
	char temp[5];
	if( pArg != &DateEntry )
	{
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
	if( newtime.tm_year < 0 || newtime.tm_mon < 0 || newtime.tm_mon > 11 || newtime.tm_mday < 0 || newtime.tm_mday > 31 )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_DATE"), thumbnail_error, 0);
		return 2;
	}

	if( lt->tm_mday != newtime.tm_mday ||
	    lt->tm_mon  != newtime.tm_mon  ||
	    lt->tm_year != newtime.tm_year )
	{
		struct timeval tv;
		interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t *)pArg;

		tv.tv_usec = 0;
		tv.tv_sec = mktime(&newtime);

		settimeofday(&tv, NULL);
		system("hwclock -w -u");

		output_fillTimeMenu(pMenu, pEditEntry->pArg);
		interface_displayMenu(1);
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
	if( newtime.tm_hour < 0 || newtime.tm_hour > 23 || newtime.tm_min  < 0 || newtime.tm_min  > 59 )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_TIME"), thumbnail_error, 0);
		return 2;
	}
	if( lt->tm_hour != newtime.tm_hour ||
	    lt->tm_min  != newtime.tm_min)
	{
		struct timeval tv;
		interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t *)pArg;

		tv.tv_usec = 0;
		tv.tv_sec = mktime(&newtime);

		settimeofday(&tv, NULL);

		system("hwclock -w -u");

		output_fillTimeMenu(pMenu, pEditEntry->pArg);
		interface_displayMenu(1);
	}
	return 0;
}

static int output_setTimeZone(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STBxx
	FILE *f = fopen("/config/timezone", "w");
	if (f != NULL)
	{
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
		if (bDisplayedWarning == 0)
		{
			bDisplayedWarning = 1;
			interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
		}
	}

	tzset();

	output_fillTimeMenu(pMenu, pArg);

	return 0;
}

static char* output_getNTP(int field, void* pArg)
{
	if( field == 0 )
	{
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

static int output_changeNTPValue(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if( value == NULL )
		return 1;

#ifdef STBxx
	if ( setParam(STB_CONFIG_FILE, "NTPSERVER", value) != 0 )
	{
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	} else
	{
		system("/usr/sbin/ntpupdater");
		output_fillTimeMenu(pMenu, pArg);
	}
#endif
#ifdef STSDK
	if ( setParam(NTP_CONFIG_FILE, "NTPSERVERS", value) != 0 )
	{
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	} else
	{
		interface_showLoading();
		interface_hideMessageBox();

		if (*value)
		{
			setParam(NTP_CONFIG_FILE, "NTPD",    "yes");
			setParam(NTP_CONFIG_FILE, "NTPDATE", "yes");
		} else
		{
			setParam(NTP_CONFIG_FILE, "NTPD",    "no");
			setParam(NTP_CONFIG_FILE, "NTPDATE", "no");
		}
		system("/etc/init.d/S85ntp stop");
		system("/etc/init.d/S85ntp start");

		interface_hideLoading();
		output_fillTimeMenu(pMenu, NULL);
		interface_displayMenu(1);
	}
#endif

	return 0;
}

static int output_setNTP(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_NTP_ADDRESS"), "\\w+", output_changeNTPValue, output_getNTP, inputModeABC, pArg);
	return 0;
}

static void output_fillTimeZoneMenu(void)
{
	int i;
	char *str;
	char buf[BUFFER_SIZE];
	int found = 12;

	interface_clearMenuEntries((interfaceMenu_t*)&TimeZoneMenu);

#ifdef STBxx
	if (!helperParseLine(INFO_TEMP_FILE, "cat /config/timezone",    "zoneinfo/", buf, 0))
#endif
#ifdef STSDK
	if (!helperParseLine(INFO_TEMP_FILE, "readlink " TIMEZONE_FILE, "zoneinfo/", buf, 0))
#endif
	{
		buf[0] = 0;
	}

	for (i=0; i<(int)(sizeof(timezones)/sizeof(timezones[0])); i++)
	{
		str = timezones[i].desc;
		interface_addMenuEntry((interfaceMenu_t*)&TimeZoneMenu, str, output_setTimeZone, (void*)timezones[i].file, thumbnail_log);
		if (strcmp(timezones[i].file, buf) == 0)
		{
			found = i;
		}
	}

	interface_setSelectedItem((interfaceMenu_t*)&TimeZoneMenu, found);
}

/**
 * This function now uses the Encoder description to get the supported outout formats instead of the Output API.
 */
static void output_fillFormatMenu(void)
{
	int selected = MENU_ITEM_BACK;

	interface_clearMenuEntries((interfaceMenu_t*)&FormatMenu);

#ifdef STB82
	int n = 0;
	int position = 0;
	char *signalName;
	int signalEnable;

	/*Add menu items automatically*/
	if (appControlInfo.outputInfo.encDesc[0].caps & DSECAPS_OUT_SIGNALS)
	{
		for (n=0; signals[n].signal; n++)
		{
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
				interface_addMenuEntryCustom((interfaceMenu_t*)&FormatMenu, interfaceMenuEntryText, signalName, strlen(signalName)+1,
				                             signalEnable, output_setFormat, NULL, NULL, NULL, (void*) signals[n].signal,
				                             signals[n].signal == appControlInfo.outputInfo.encConfig[0].out_signals ? thumbnail_selected : thumbnail_channels);
				position++;
			}
		}
	}
#endif // STB82
#ifdef STSDK
	int n = 0;
	int icon = thumbnail_channels;

	elcdRpcType_t type;
	cJSON *list = NULL;
	int    ret;

	ret = st_rpcSync( elcmd_listvmode, NULL, &type, &list );
	if( ret == 0 && type == elcdRpcResult && list && list->type == cJSON_Array )
	{
		cJSON *mode;
		int i;
		for( i = 0; (mode = cJSON_GetArrayItem(list, i)) != NULL; i++ )
		{
			if( mode->type != cJSON_Object )
				continue;
			cJSON *name = cJSON_GetObjectItem( mode, "name" );
			if( !name || name->type != cJSON_String )
				continue;
			cJSON *supported = cJSON_GetObjectItem( mode, "supported" );
			if( !supported || supported->type > cJSON_True )
				continue;
			cJSON *native  = cJSON_GetObjectItem( mode, "native" );
			cJSON *current = cJSON_GetObjectItem( mode, "current" );

			if( native && native->type == cJSON_True )
				icon = thumbnail_tvstandard;
			else
				icon = supported->type ? thumbnail_channels : thumbnail_not_selected;
			if( current && current->type == cJSON_True )
			{
				selected = n;
				strncpy(output_currentFormat, name->valuestring, sizeof(output_currentFormat));
				output_currentFormat[sizeof(output_currentFormat)-1]=0;
			}
			interface_addMenuEntry((interfaceMenu_t*)&FormatMenu, name->valuestring, output_setFormat, NULL, icon);
			n++;
		}
	} else if( type == elcdRpcError && list && list->type == cJSON_String )
		eprintf("%s: failed to get video mode list: %s\n", list->valuestring);
	cJSON_Delete(list);
#endif
	interface_setSelectedItem((interfaceMenu_t*)&FormatMenu, selected);
}

#ifdef STSDK
int output_enterFormatMenu(interfaceMenu_t *pMenu, void *pArg)
{
	output_fillFormatMenu();
	return 0;
}
#endif

static void output_fillBlankingMenu(void)
{
	//int position = 0;
	char *str;
	interface_clearMenuEntries((interfaceMenu_t*)&BlankingMenu);
	//BlankingMenu.prev = &OutputMenu;
	str = "4 x 3";
	interface_addMenuEntry((interfaceMenu_t*)&BlankingMenu, str, output_setBlanking, (void*)DSOSB_4x3, thumbnail_configure);
	str = "16 x 9";
	interface_addMenuEntry((interfaceMenu_t*)&BlankingMenu, str, output_setBlanking, (void*)DSOSB_16x9, thumbnail_configure);
	str = "None";
	interface_addMenuEntry((interfaceMenu_t*)&BlankingMenu, str, output_setBlanking, (void*)DSOSB_OFF, thumbnail_configure);
}

long get_info_progress()
{
	return info_progress;
}

int show_info(interfaceMenu_t* pMenu, void* pArg)
{
	char buf[256];
	char temp[256];
	char info_text[4096];
	char *iface_name;
	int i;
	int fd;

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
	if (fd > 0)
	{
		vendor = "nxp";
		close(fd);
	} else
	{
		vendor = "philips";
	}

#ifdef STB82
	systemId_t sysid;
	systemSerial_t serial;
	unsigned long stmfw;

	if (helperParseLine(INFO_TEMP_FILE, "cat /dev/sysid", "SERNO: ", buf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
	{
		serial.SerialFull = strtoul(buf, NULL, 16);
	} else {
		serial.SerialFull = 0;
	}

	if (helperParseLine(INFO_TEMP_FILE, NULL, "SYSID: ", buf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
	{
		sysid.IDFull = strtoul(buf, NULL, 16);
	} else {
		sysid.IDFull = 0;
	}

	if (helperParseLine(INFO_TEMP_FILE, NULL, "VER: ", buf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
	{
		stmfw = strtoul(buf, NULL, 16);
	} else {
		stmfw = 0;
	}

	get_composite_serial(sysid, serial, temp);
	sprintf(info_text,"%s: %s\n",_T("SERIAL_NUMBER"),temp);
	if (stmfw > 0x0106 && helperParseLine(INFO_TEMP_FILE, "stmclient 7", "MAC: ", buf, ' ')) // MAC: 02:EC:D0:00:00:39
	{
		char mac[6];
		sscanf(buf, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		sprintf(temp, "%s 1: %02hhX%02hhX%02hhX%02hhX%02hhX%02hhX\n", _T("MAC_ADDRESS"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		sprintf(temp, "%s 1: %s\n", _T("MAC_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
	}
	strcat(info_text, temp);

	if (stmfw > 0x0106 && helperParseLine(INFO_TEMP_FILE, "stmclient 8", "MAC: ", buf, ' ')) // MAC: 02:EC:D0:00:00:39
	{
		char mac[6];
		sscanf(buf, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		sprintf(temp, "%s 2: %02hhX%02hhX%02hhX%02hhX%02hhX%02hhX\n", _T("MAC_ADDRESS"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		sprintf(temp, "%s 2: %s\n", _T("MAC_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
	}
	strcat(info_text, temp);

	strcat(info_text,_T("STM_VERSION"));
	strcat(info_text,": ");
	if (stmfw != 0)
	{
		sprintf(temp, "%lu.%lu", (stmfw >> 8)&0xFF, (stmfw)&0xFF);
		strcat(info_text, temp);
	} else
	{
		strcat(info_text, _T("NOT_AVAILABLE_SHORT"));
	}
	strcat(info_text, "\n");

	strcat(info_text, _T("SERIAL_NUMBER_OLD"));
	strcat(info_text,": ");
	if (serial.SerialFull != 0)
	{
		sprintf(temp, "%u\n", serial.SerialFull);
	} else
	{
		sprintf(temp, "%s\n", _T("NOT_AVAILABLE_SHORT"));
	}
	strcat(info_text, temp);

	strcat(info_text, _T("SYSID"));
	strcat(info_text,": ");
	if (sysid.IDFull != 0)
	{
		get_system_id(sysid, temp);
		strcat(temp,"\n");
	} else
	{
		sprintf(temp, "%s\n", _T("NOT_AVAILABLE_SHORT"));
	}
	strcat(info_text, temp);
	info_progress++;
	interface_displayMenu(1);
#endif // STB82

#endif // STBxx
	sprintf(temp, "%s: %s\n%s: %s\n%s: %s\n", _T("APP_RELEASE"), RELEASE_TYPE, _T("APP_VERSION"), REVISION, _T("COMPILE_TIME"), COMPILE_TIME);
	strcat(info_text,temp);

#ifdef STB82
	sprintf(buf, "%s: %d MB\n", _T("MEMORY_SIZE"), appControlInfo.memSize);
	strcat(info_text, buf);
#endif

	if (helperParseLine(INFO_TEMP_FILE, "date -R", "", buf, '\r'))
	{
  	sprintf(temp, "%s: %s\n",_T("CURRENT_TIME"), buf);
	} else
	{
		sprintf(temp, "%s: %s\n",_T("CURRENT_TIME"), _T("NOT_AVAILABLE_SHORT"));
	}
	strcat(info_text, temp);

	info_progress++;
	interface_displayMenu(1);

	for (i=0;;i++)
	{
		eprintf("%s: checking %s\n", __FUNCTION__, helperEthDevice(i));
		sprintf(temp, "/sys/class/net/%s", helperEthDevice(i));
		if (helperCheckDirectoryExsists(temp))
		{
			switch( i )
			{
				case ifaceWAN:
#if (defined STSDK) && (defined ENABLE_WIFI)
					if( wifiInfo.wanMode && networkInfo.lanMode != lanBridge )
						continue;
#endif
					iface_name = output_isBridge() ? _T("GATEWAY_BRIDGE") : "WAN";
					break;
#ifdef ENABLE_LAN
				case ifaceLAN:
#ifdef STSDK
					if( output_isBridge() )
						continue;
#endif
					iface_name = "LAN"; break;
#endif
#ifdef ENABLE_PPP
				case ifacePPP: iface_name = "PPP"; break;
#endif
#ifdef ENABLE_WIFI
				case ifaceWireless: iface_name = _T("WIRELESS");
					strcat(info_text, "\n");
					sprintf(temp, "%s ESSID: %s\n", iface_name, wifiInfo.essid);
					strcat(info_text, temp);
					sprintf(temp, "%s %s: %s\n", iface_name, _T("MODE"), wireless_mode_print( wifiInfo.mode ));
					strcat(info_text, temp);
					if( !wifiInfo.wanMode )
					{
						sprintf(temp, "%s %s: %d\n", iface_name, _T("CHANNEL_NUMBER"), wifiInfo.currentChannel );
						strcat(info_text, temp);
						sprintf(temp, "%s %s: %s\n", iface_name, _T("AUTHENTICATION"), wireless_auth_print( wifiInfo.auth ));
						strcat(info_text, temp);
						sprintf(temp, "%s %s: %s\n", iface_name, _T("ENCRYPTION"), wireless_encr_print( wifiInfo.encryption ));
						strcat(info_text, temp);
					}
#ifdef STSDK
					if( wifiInfo.wanMode == 0 || networkInfo.lanMode == lanBridge ) // wifi is in bridge with eth1
						continue;
#endif
					break;
#endif
				default:
					eprintf("%s: unknown network interface %s\n", temp);
					iface_name = "";
			}
			strcat(info_text, "\n");
			sprintf(temp, "ifconfig %s | grep HWaddr", helperEthDevice(i));
			if (helperParseLine(INFO_TEMP_FILE, temp, "HWaddr ", buf, ' '))			 // eth0      Link encap:Ethernet  HWaddr 76:60:37:02:24:02
			{
				sprintf(temp, "%s %s: ", iface_name, _T("MAC_ADDRESS"));
				strcat(info_text, temp);
				strcat(info_text, buf);
				strcat(info_text, "\n");
			} else
			{
				sprintf(temp, "%s %s: %s\n", iface_name, _T("MAC_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
				strcat(info_text, temp);
			}

			sprintf(temp, "ifconfig %s | grep \"inet addr\"", helperEthDevice(i));
			if (helperParseLine(INFO_TEMP_FILE, temp, "inet addr:", buf, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			{
				sprintf(temp, "%s %s: ", iface_name, _T("IP_ADDRESS"));
				strcat(info_text, temp);
				strcat(info_text, buf);
				strcat(info_text, "\n");
			} else {
				sprintf(temp, "%s %s: %s\n", iface_name, _T("IP_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
				strcat(info_text, temp);
			}

			sprintf(temp, "ifconfig %s | grep \"Mask:\"", helperEthDevice(i));
			if (helperParseLine(INFO_TEMP_FILE, temp, "Mask:", buf, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			{
				sprintf(temp, "%s %s: ", iface_name, _T("NETMASK"));
				strcat(info_text, temp);
				strcat(info_text, buf);
				strcat(info_text, "\n");
			} else {
				sprintf(temp, "%s %s: %s\n", iface_name, _T("NETMASK"), _T("NOT_AVAILABLE_SHORT"));
				strcat(info_text, temp);
			}

			sprintf(temp, "route -n | grep -e \"0\\.0\\.0\\.0 .* 0\\.0\\.0\\.0 *UG .* %s\"", helperEthDevice(i));
			if (helperParseLine(INFO_TEMP_FILE, temp, "0.0.0.0", buf, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			{
				sprintf(temp, "%s %s: ", iface_name, _T("GATEWAY"));
				strcat(info_text, temp);
				strcat(info_text, buf);
				strcat(info_text, "\n");
			}
			/* else {
				sprintf(temp, "%s %s: %s\n", iface_name, _T("GATEWAY"), _T("NOT_AVAILABLE_SHORT"));
				strcat(info_text, temp);
			} */
		} else
		{
			break;
		}
	}
	strcat(info_text, "\n");
	i = -1;
	fd = open( "/etc/resolv.conf", O_RDONLY );
	if( fd > 0 )
	{
		char *ptr;
		while( helperReadLine( fd, buf ) == 0 && buf[0] )
		{
			if( (ptr = strstr( buf, "nameserver " )) != NULL )
			{
				ptr += 11;
				++i;
				sprintf(temp, "%s %d: %s\n", _T("DNS_SERVER"), i+1, ptr);
				strcat(info_text, temp);
			}
		}
		close(fd);
	}
	if( i < 0 )
	{
		sprintf(temp, "%s: %s\n", _T("DNS_SERVER"), _T("NOT_AVAILABLE_SHORT"));
		strcat(info_text, temp);
	}

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
	{
		strcat(info_text, LANG_TM0_IMAGE ": " LANG_NOT_AVAILABLE_SHORT "\n");
	}

	info_progress++;
	interface_displayMenu(1);

	if (helperParseLine(INFO_TEMP_FILE, "cat /tmp/tm0dp | grep \"CPU RSE\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"unable to communicate with TriMedia 0\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"TM 0 not ready\"", NULL, NULL, ' '))
	{
		strcat(info_text, LANG_TM0_STATUS ": " LANG_FAIL "\n");
	} else
	{
		strcat(info_text, LANG_TM0_STATUS ": " LANG_OK "\n");
	}

	info_progress++;
	interface_displayMenu(1);

	if (helperParseLine(INFO_TEMP_FILE, "cat /tmp/dp/dp1init | grep \"\\$Build: \"", "$Build: ", buf, '$'))
	{
		strcat(info_text, LANG_TM1_IMAGE ": ");
		strcat(info_text, buf);
		strcat(info_text, "\n");
	} else
	{
		strcat(info_text, LANG_TM1_IMAGE ": " LANG_NOT_AVAILABLE_SHORT "\n");
	}

	info_progress++;
	interface_displayMenu(1);

	if (helperParseLine(INFO_TEMP_FILE, "cat /tmp/tm1dp | grep \"CPU RSE\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"unable to communicate with TriMedia 1\"", NULL, NULL, ' ') ||
		helperParseLine(INFO_TEMP_FILE, "cat /tmp/tmmsg | grep \"TM 1 not ready\"", NULL, NULL, ' '))
	{
		strcat(info_text, LANG_TM1_STATUS ": " LANG_FAIL "\n");
	} else
	{
		strcat(info_text, LANG_TM1_STATUS ": " LANG_OK "\n");
	}

	info_progress++;
	interface_displayMenu(1);

	sprintf(buf, "cat /proc/%s/drivers/pnx8550/video/renderer0/output_resolution", vendor);
	if (helperParseLine(INFO_TEMP_FILE, buf, NULL, buf, ' '))
	{
		strcat(info_text, LANG_RESOLUTION ": ");
		strcat(info_text, buf);
	} else
	{
		strcat(info_text, LANG_RESOLUTION ": " LANG_NOT_AVAILABLE_SHORT);
	}
#endif // #if 0

	info_progress++;
	interface_displayMenu(1);

	interface_sliderShow(0, 0);

	eprintf("output: Done collecting info.\n---------------------------------------------------\n%s\n---------------------------------------------------\n", info_text);

	helperFlushEvents();

	interface_showScrollingBox( info_text, thumbnail_info, NULL, NULL );
	//interface_showMessageBox(info_text, thumbnail_info, 0);

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
		case colorSettingContrast:
			return adj.contrast;
		case colorSettingBrightness:
			return adj.brightness;
		case colorSettingHue:
			return adj.hue;
		default:
			return adj.saturation;
	}

	return 0;
}


void output_setColorValue(long value, void *pArg)
{
	int iarg = GET_NUMBER(pArg);
	DFBColorAdjustment adj;
	IDirectFBDisplayLayer *layer = gfx_getLayer(gfx_getMainVideoLayer());

	/*adj.flags = DCAF_ALL;
	layer->GetColorAdjustment(layer, &adj);*/

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
			if (saveAppSettings() != 0 && bDisplayedWarning == 0)
			{
				bDisplayedWarning = 1;
				interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
			}
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
			interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\w+", output_enterPassword, NULL, inputModeDirect, pArg);
			return 0;
		}
	}

	return pAction(pMenu, NULL);
}

static int output_enterPassword(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	char passwd[MAX_PASSWORD_LENGTH];
	unsigned char passwdsum[16];
	char inpasswd[MAX_PASSWORD_LENGTH];
	FILE *file;
	unsigned long id, i, intpwd;
	menuActionFunction pAction = (menuActionFunction)pArg;

	if( value == NULL )
		return 1;

	if( pArg != NULL)
	{
		file = popen("hwconfigManager a -1 PASSWORD 2>/dev/null | grep \"VALUE:\" | sed 's/VALUE: \\(.*\\)/\\1/'","r");
		if( file != NULL && fgets(passwd, MAX_PASSWORD_LENGTH, file) != NULL && passwd[0] != 0 && passwd[0] != '\n' )
		{
			fclose(file);
			if( passwd[strlen(passwd)-1] == '\n')
			{
				passwd[strlen(passwd)-1] = 0;
			}
			/* Get MD5 sum of input and convert it to hex string */
			md5((unsigned char*)value, strlen(value), passwdsum);
			for (i=0;i<16;i++)
			{
				sprintf(&inpasswd[i*2], "%02hhx", passwdsum[i]);
			}
			dprintf("%s: Passwd #1: %s/%s\n", __FUNCTION__,passwd, inpasswd);
			if(strcmp( passwd, inpasswd ) != 0)
			{
				interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
				return 1;
			}
		} else
		{
			if( file != NULL)
			{
				fclose(file);
			}
			file = popen("cat /dev/sysid | sed 's/.*SERNO: \\(.*\\), .*/\\1/'","r");
			if( file != NULL && fgets(passwd, MAX_PASSWORD_LENGTH, file) != NULL && passwd[0] != 0 && passwd[0] != '\n' )
			{
				fclose(file);
				if( passwd[strlen(passwd)-1] == '\n')
				{
					passwd[strlen(passwd)-1] = 0;
				}
				id = strtoul(passwd, NULL, 16);
				intpwd = strtoul(value, NULL, 10);
				dprintf("%s: Passwd #2: %lu/%lu\n", __FUNCTION__, id, intpwd);

				if( intpwd != id )
				{
					interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
					return 1;
				}
			} else
			{
				if( file != NULL)
				{
					fclose(file);
				}
				dprintf("%s: Warning: can't determine system password!\n", __FUNCTION__);
			}
		}
		return pAction(pMenu, NULL);
	}
	return 1;
}
#endif

int output_fillVideoMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;

	interface_clearMenuEntries((interfaceMenu_t*)&VideoSubMenu);

#ifdef STB82
    {
		str = _T("TV_STANDARD");
		interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&StandardMenu, thumbnail_tvstandard);
    }

    /* We only enable this menu when we are outputting SD and we do not only have the HD denc. (HDMI is not denc[0])*/
    if(!(gfx_isHDoutput()) && !(appControlInfo.outputInfo.encDesc[0].all_connectors & DSOC_HDMI))
#endif
	{
		str = _T("TV_FORMAT");
		interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&FormatMenu, thumbnail_channels);
#ifdef STB82
        /*Only add slow blanking if we have the capability*/
        if(appControlInfo.outputInfo.encDesc[0].caps & DSOCAPS_SLOW_BLANKING)
        {
			str = _T("TV_BLANKING");
			interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&BlankingMenu, thumbnail_configure);
            output_fillBlankingMenu();
        }
#endif
	}

#ifdef STB82
	char buf[MENU_ENTRY_INFO_LENGTH];

	str = appControlInfo.outputInfo.aspectRatio == aspectRatio_16x9 ? "16:9" : "4:3";
	sprintf(buf, "%s: %s", _T("ASPECT_RATIO"), str);
	interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, buf, output_toggleAspectRatio, NULL, thumbnail_channels);

	str = appControlInfo.outputInfo.autoScale == videoMode_scale ? _T("AUTO_SCALE_ENABLED") :
		_T("AUTO_STRETCH_ENABLED");
	sprintf(buf, "%s: %s", _T("SCALE_MODE"), str);
	interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, buf, output_toggleAutoScale, NULL, settings_size);
#ifndef HIDE_EXTRA_FUNCTIONS
	sprintf(buf, "%s: %s", _T("AUDIO_OUTPUT"), appControlInfo.soundInfo.rcaOutput == 0 ? "SCART" : "RCA");
	interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, buf, output_toggleAudio, NULL, thumbnail_sound);
#endif
	sprintf(buf, "%s: %s", _T("SCREEN_FILTRATION"), _T( appControlInfo.outputInfo.bScreenFiltration ? "ON" : "OFF" ));
	interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, buf, output_toggleScreenFiltration, NULL, thumbnail_channels);

	str = _T("COLOR_SETTINGS");
	interface_addMenuEntry((interfaceMenu_t*)&VideoSubMenu, str, output_showColorSlider, NULL, thumbnail_tvstandard);
#endif // STB82
	interface_menuActionShowMenu(pMenu, (void*)&VideoSubMenu);

	return 0;
}



#if (defined(STB225) || defined(STSDK))
static int output_toggle3DMonitor(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.has_3D_TV = (appControlInfo.outputInfo.has_3D_TV+1) & 1;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

#if defined(STB225)
	if(interfaceInfo.mode3D==0 || appControlInfo.outputInfo.has_3D_TV==0) {
		Stb225ChangeDestRect("/dev/fb0", 0, 0, 1920, 1080);
	} else	{
		Stb225ChangeDestRect("/dev/fb0", 0, 0, 960, 1080);
	}
#endif
	output_fill3DMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}
static int output_toggle3DContent(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.content3d = (appControlInfo.outputInfo.content3d + 1) % 3;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
	output_fill3DMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}
static int output_toggle3DFormat(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.format3d = (appControlInfo.outputInfo.format3d + 1) % 3;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
	output_fill3DMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}
static int output_toggleUseFactor(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.use_factor = (appControlInfo.outputInfo.use_factor + 1) % 2;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
	output_fill3DMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}
static int output_toggleUseOffset(interfaceMenu_t *pMenu, void* pArg) {
	appControlInfo.outputInfo.use_offset= (appControlInfo.outputInfo.use_offset + 1) % 2;

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
	output_fill3DMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}

char *output_get3DFactor(int index, void* pArg) {
	if( index == 0 ) {
		static char temp[8];
		sprintf(temp, "%03d", appControlInfo.outputInfo.factor);
		return temp;
	} else	return NULL;
}
static int output_change3DFactor(interfaceMenu_t *pMenu, char *value, void* pArg) {
	if( value != NULL && value[0] != 0)  {
		int ivalue = atoi(value);
		if (ivalue < 0 || ivalue > 255) ivalue = 255;

		appControlInfo.outputInfo.factor = ivalue;
	}
	output_fill3DMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}
char *output_get3DOffset(int index, void* pArg) {
	if( index == 0 ) {
		static char temp[8];
		sprintf(temp, "%03d", appControlInfo.outputInfo.offset);
		return temp;
	} else	return NULL;
}
static int output_change3DOffset(interfaceMenu_t *pMenu, char *value, void* pArg) {
	if( value != NULL && value[0] != 0)  {
		int ivalue = atoi(value);
		if (ivalue < 0 || ivalue > 255) ivalue = 255;

		appControlInfo.outputInfo.offset = ivalue;
	}
	output_fill3DMenu(pMenu, 0);
	interface_displayMenu(1);

	return 0;
}

static int output_toggle3DFactor(interfaceMenu_t *pMenu, void* pArg) {
	return interface_getText(pMenu, _T("3D_FACTOR"), "\\d{3}", output_change3DFactor, output_get3DFactor, inputModeDirect, pArg);
}
static int output_toggle3DOffset(interfaceMenu_t *pMenu, void* pArg) {
	return interface_getText(pMenu, _T("3D_OFFSET"), "\\d{3}", output_change3DOffset, output_get3DOffset, inputModeDirect, pArg);
}



int output_fill3DMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	char buf[MENU_ENTRY_INFO_LENGTH];
	
	char *chContent[] = {_T("VIDEO"), _T("3D_SIGNAGE"), _T("3D_STILLS")};
	char *chFormat [] = {_T("3D_2dZ"), _T("3D_DECLIPSE_RD"), _T("3D_DECLIPSE_FULL")};

//OutputMenu
	interface_clearMenuEntries((interfaceMenu_t*)&Video3DSubMenu);

	sprintf(buf, "%s: %s", _T("3D_MONITOR"), _T( appControlInfo.outputInfo.has_3D_TV ? "ON" : "OFF" ));
	interface_addMenuEntry((interfaceMenu_t*)&Video3DSubMenu, buf, output_toggle3DMonitor, NULL, thumbnail_channels);

	str = chContent[appControlInfo.outputInfo.content3d];
	sprintf(buf, "%s: %s", _T("3D_CONTENT"), str);
	interface_addMenuEntry((interfaceMenu_t*)&Video3DSubMenu, buf, output_toggle3DContent, NULL, thumbnail_channels);

	str = chFormat[appControlInfo.outputInfo.format3d];
	sprintf(buf, "%s: %s", _T("3D_FORMAT"), str);
	interface_addMenuEntry((interfaceMenu_t*)&Video3DSubMenu, buf, output_toggle3DFormat, NULL, thumbnail_channels);

	sprintf(buf, "%s: %s", _T("3D_FACTOR_FLAG"), _T( appControlInfo.outputInfo.use_factor ? "ON" : "OFF" ));
	interface_addMenuEntry((interfaceMenu_t*)&Video3DSubMenu, buf, output_toggleUseFactor, NULL, thumbnail_channels);

	sprintf(buf, "%s: %s", _T("3D_OFFSET_FLAG"), _T( appControlInfo.outputInfo.use_offset ? "ON" : "OFF" ));
	interface_addMenuEntry((interfaceMenu_t*)&Video3DSubMenu, buf, output_toggleUseOffset, NULL, thumbnail_channels);

	sprintf(buf, "%s: %d", _T("3D_FACTOR"), appControlInfo.outputInfo.factor);
	interface_addMenuEntry((interfaceMenu_t*)&Video3DSubMenu, buf, output_toggle3DFactor, NULL, thumbnail_channels);

	sprintf(buf, "%s: %d", _T("3D_OFFSET"), appControlInfo.outputInfo.offset);
	interface_addMenuEntry((interfaceMenu_t*)&Video3DSubMenu, buf, output_toggle3DOffset, NULL, thumbnail_channels);

	interface_menuActionShowMenu(pMenu, (void*)&Video3DSubMenu);

	return 0;
}
#endif // #ifdef STB225

static int output_resetTimeEdit(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t *)pArg;
	return output_fillTimeMenu( pMenu, pEditEntry->pArg );
}

int output_fillTimeMenu(interfaceMenu_t *pMenu, void* pArg)
{
	struct tm *t;
	time_t now;
	char   buf[BUFFER_SIZE], *str;

	interface_clearMenuEntries((interfaceMenu_t*)&TimeSubMenu);

	time(&now);
	t = localtime(&now);
	strftime( TimeEntry.info.time.value, sizeof(TimeEntry.info.time.value), "%H%M", t);
	interface_addEditEntryTime((interfaceMenu_t *)&TimeSubMenu, _T("SET_TIME"), output_setTime, output_resetTimeEdit, pArg, thumbnail_log, &TimeEntry);
	strftime( DateEntry.info.date.value, sizeof(DateEntry.info.date.value), "%d%m%Y", t);
	interface_addEditEntryDate((interfaceMenu_t *)&TimeSubMenu, _T("SET_DATE"), output_setDate, output_resetTimeEdit, pArg, thumbnail_log, &DateEntry);

	//interface_addMenuEntry((interfaceMenu_t*)&TimeSubMenu, _T("SET_TIME"), output_changeTime, NULL, thumbnail_log);
	interface_addMenuEntry((interfaceMenu_t*)&TimeSubMenu, _T("SET_TIME_ZONE"), (menuActionFunction)menuDefaultActionShowMenu, (void*)&TimeZoneMenu, thumbnail_log);
	//interface_addMenuEntry((interfaceMenu_t*)&TimeSubMenu, _T("SET_DATE"), output_changeDate, NULL, thumbnail_log);

	sprintf(buf, "%s: ", _T("NTP_SERVER"));
	str = &buf[strlen(buf)];
	strcpy( str, output_getNTP( 0, NULL ) );
	if ( *str == 0 )
		strcpy(str, _T("NOT_AVAILABLE_SHORT") );
	interface_addMenuEntry((interfaceMenu_t*)&TimeSubMenu, buf, output_setNTP, NULL, thumbnail_enterurl);

	interface_menuActionShowMenu(pMenu, (void*)&TimeSubMenu);

	return 0;
}

int output_fillNetworkMenu(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STBPNX
	char path[MAX_CONFIG_PATH];
#endif
	char temp[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries((interfaceMenu_t*)&NetworkSubMenu);
	sprintf(temp, "/sys/class/net/%s", helperEthDevice(ifaceWAN));
	if( helperCheckDirectoryExsists(temp) )
	{
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, "WAN", (menuActionFunction)menuDefaultActionShowMenu, (void*)&WANSubMenu, settings_network);
#ifdef STBPNX
		sprintf(path, "/config/ifcfg-%s", helperEthDevice(ifaceWAN));
#ifdef ENABLE_LAN
		if (!helperFileExists(STB_CONFIG_OVERRIDE_FILE))
		{
			getParam(STB_CONFIG_FILE, "CONFIG_GATEWAY_MODE", "OFF", temp);
			if (strcmp("NAT", temp) == 0)
			{
				output_gatewayMode = gatewayModeNAT;
			} else if (strcmp("FULL", temp) == 0)
			{
				output_gatewayMode = gatewayModeFull;
			} else if (strcmp("BRIDGE", temp) == 0)
			{
				output_gatewayMode = gatewayModeBridge;
			} else
			{
				output_gatewayMode = gatewayModeOff;
			}
		}
#endif
#endif // STBPNX
	} else
	{
		interface_addMenuEntryDisabled((interfaceMenu_t*)&NetworkSubMenu, "WAN", settings_network);
	}
#ifdef ENABLE_PPP
	interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, _T("PPP"), (menuActionFunction)menuDefaultActionShowMenu, (void*)&PPPSubMenu, settings_network);
#endif
#ifdef ENABLE_LAN
#ifndef ENABLE_WIFI
	sprintf(temp, "/sys/class/net/%s", helperEthDevice(ifaceLAN));
	if( helperCheckDirectoryExsists(temp) )
#endif
	{
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, "LAN", (menuActionFunction)menuDefaultActionShowMenu, (void*)&LANSubMenu, settings_network);
	}
#endif
#ifdef ENABLE_WIFI
#if !(defined STB225)
	sprintf(temp, "/sys/class/net/%s", helperEthDevice(ifaceWireless));
	if( wifiInfo.wanMode || helperCheckDirectoryExsists(temp) )
	{
#endif
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, _T("WIRELESS"), (menuActionFunction)menuDefaultActionShowMenu, (void*)&WifiSubMenu, settings_network);
#if !(defined STB225)
	} else
	{
		interface_addMenuEntryDisabled((interfaceMenu_t*)&NetworkSubMenu, _T("WIRELESS"), settings_network);
	}
#endif
#endif // ENABLE_WIFI

#ifdef ENABLE_IPTV
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, _T("TV_CHANNELS"), (menuActionFunction)menuDefaultActionShowMenu, (void*)&IPTVSubMenu, thumbnail_multicast);
#endif
#ifdef ENABLE_VOD
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, _T("MOVIES"), (menuActionFunction)menuDefaultActionShowMenu, (void*)&VODSubMenu, thumbnail_vod);
#endif

#ifdef STBPNX
	if (helperFileExists(BROWSER_CONFIG_FILE))
#endif
	{
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, _T("INTERNET_BROWSING"), (menuActionFunction)menuDefaultActionShowMenu, (void*)&WebSubMenu, thumbnail_internet);
	}
#ifdef ENABLE_VERIMATRIX
	if (helperFileExists(VERIMATRIX_INI_FILE))
	{
		sprintf(path,"%s: %s", _T("VERIMATRIX_ENABLE"), appControlInfo.useVerimatrix == 0 ? _T("OFF") : _T("ON"));
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, path, output_toggleVMEnable, NULL, thumbnail_configure);
		if (appControlInfo.useVerimatrix != 0)
		{
			getParam(VERIMATRIX_INI_FILE, "COMPANY", "", temp);
			if (temp[0] != 0)
			{
				sprintf(path, "%s: %s", _T("VERIMATRIX_COMPANY"), temp);
				interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, path, output_toggleVMCompany, pArg, thumbnail_enterurl);
			}
			getParam(VERIMATRIX_INI_FILE, "SERVERADDRESS", "", temp);
			if (temp[0] != 0)
			{
				sprintf(path, "%s: %s", _T("VERIMATRIX_ADDRESS"), temp);
				interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, path, output_toggleVMAddress, pArg, thumbnail_enterurl);
			}
			interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, _T("VERIMATRIX_GET_ROOTCERT"), output_getVMRootCert, NULL, thumbnail_turnaround);
		}
	}
#endif
#ifdef ENABLE_SECUREMEDIA
	if (helperFileExists(SECUREMEDIA_CONFIG_FILE))
	{
		sprintf(path,"%s: %s", _T("SECUREMEDIA_ENABLE"), appControlInfo.useSecureMedia == 0 ? _T("OFF") : _T("ON"));
		interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, path, output_toggleSMEnable, NULL, thumbnail_configure);
		if (appControlInfo.useSecureMedia != 0)
		{
			getParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_ESAM_HOST", "", temp);
			if (temp[0] != 0)
			{
				sprintf(path, "%s: %s", _T("SECUREMEDIA_ESAM_HOST"), temp);
				interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, path, output_toggleSMAddress, (void*)1, thumbnail_enterurl);
			}
			getParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_RANDOM_HOST", "", temp);
			if (temp[0] != 0)
			{
				sprintf(path, "%s: %s", _T("SECUREMEDIA_RANDOM_HOST"), temp);
				interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, path, output_toggleSMAddress, (void*)2, thumbnail_enterurl);
			}
		}
	}
#endif
//HTTPProxyServer=http://192.168.1.2:3128
//http://192.168.1.57:8080/media/stb/home.html
#ifndef HIDE_EXTRA_FUNCTIONS
	char *str;
	str = _T("PROCESS_PCR");
	interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, str, output_togglePCR, NULL, appControlInfo.bProcessPCR ? thumbnail_yes : thumbnail_no);
	str = _T( appControlInfo.bUseBufferModel ? "BUFFER_TRACKING" : "PCR_TRACKING");
	interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, str, output_toggleBufferTracking, NULL, thumbnail_configure);
	str = _T("RENDERER_SYNC");
	interface_addMenuEntry((interfaceMenu_t*)&NetworkSubMenu, str, output_toggleRSync, NULL, appControlInfo.bRendererDisableSync ? thumbnail_no : thumbnail_yes);
#endif

	interface_menuActionShowMenu(pMenu, (void*)&NetworkSubMenu);

	return 0;
}

static int output_fillWANMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char path[MAX_CONFIG_PATH];
	char buf[MENU_ENTRY_INFO_LENGTH];
	char temp[MENU_ENTRY_INFO_LENGTH];
	int dhcp = 0;

	interface_clearMenuEntries((interfaceMenu_t*)&WANSubMenu);

	const int i = ifaceWAN;
	sprintf(temp, "/sys/class/net/%s", helperEthDevice(i));
	if( helperCheckDirectoryExsists(temp) )
	{
#ifdef STBPNX
		sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

		getParam(path, "BOOTPROTO", _T("NOT_AVAILABLE_SHORT"), temp);
		if (strcmp("dhcp+dns", temp) == 0)
		{
			strcpy(temp, _T("ADDR_MODE_DHCP"));
			dhcp = 1;
		} else
		{
			strcpy(temp, _T("ADDR_MODE_STATIC"));
			dhcp = 0;
		}
#endif // STBPNX
#ifdef STSDK
		dhcp = networkInfo.wanDhcp;
		strcpy(temp, _T( dhcp ? "ADDR_MODE_DHCP" : "ADDR_MODE_STATIC" ));
#endif // STSDK
		sprintf(buf, "%s: %s", _T("ADDR_MODE"), temp);
		interface_addMenuEntry((interfaceMenu_t*)&WANSubMenu, buf, output_toggleMode, SET_NUMBER(i), thumbnail_configure);

		if (dhcp == 0)
		{
#ifdef STBPNX
			getParam(path, "IPADDR", _T("NOT_AVAILABLE_SHORT"), temp);
#endif
#ifdef STSDK
			strcpy(temp, networkInfo.wan.ip.s_addr != 0 ? inet_ntoa(networkInfo.wan.ip) : _T("NOT_AVAILABLE_SHORT") );
#endif
			sprintf(buf, "%s: %s",  _T("IP_ADDRESS"), temp);
			interface_addMenuEntry((interfaceMenu_t*)&WANSubMenu, buf, output_toggleIP, SET_NUMBER(i), thumbnail_configure);

#ifdef STBPNX
			getParam(path, "NETMASK", _T("NOT_AVAILABLE_SHORT"), temp);
#endif
#ifdef STSDK
			strcpy(temp, networkInfo.wan.mask.s_addr != 0 ? inet_ntoa(networkInfo.wan.mask) : _T("NOT_AVAILABLE_SHORT") );
#endif
			sprintf(buf, "%s: %s", _T("NETMASK"), temp);
			interface_addMenuEntry((interfaceMenu_t*)&WANSubMenu, buf, output_toggleNetmask, SET_NUMBER(i), thumbnail_configure);

#ifdef STBPNX
			getParam(path, "DEFAULT_GATEWAY", _T("NOT_AVAILABLE_SHORT"), temp);
#endif
#ifdef STSDK
			strcpy(temp, networkInfo.wan.gw.s_addr != 0 ? inet_ntoa(networkInfo.wan.gw) : _T("NOT_AVAILABLE_SHORT") );
#endif
			sprintf(buf, "%s: %s", _T("GATEWAY"), temp);
			interface_addMenuEntry((interfaceMenu_t*)&WANSubMenu, buf, output_toggleGw, SET_NUMBER(i), thumbnail_configure);

#ifdef STBPNX
			getParam(path, "NAMESERVERS", _T("NOT_AVAILABLE_SHORT"), temp);
#endif
#ifdef STSDK
			strcpy(temp, networkInfo.dns.s_addr != 0 ? inet_ntoa(networkInfo.dns) : _T("NOT_AVAILABLE_SHORT"));
#endif
			sprintf(buf, "%s: %s", _T("DNS_SERVER"), temp);
			interface_addMenuEntry((interfaceMenu_t*)&WANSubMenu, buf, output_toggleDNSIP, SET_NUMBER(i), thumbnail_configure);
		} else
		{
			sprintf(path, "ifconfig %s | grep \"inet addr\"", helperEthDevice(i));
			if (!helperParseLine(INFO_TEMP_FILE, path, "inet addr:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			{
				strcpy(temp, _T("NOT_AVAILABLE_SHORT"));
			}
			sprintf(buf, "%s: %s", _T("IP_ADDRESS"), temp);
			interface_addMenuEntryDisabled((interfaceMenu_t*)&WANSubMenu, buf, thumbnail_configure);

			sprintf(path, "ifconfig %s | grep \"Mask:\"", helperEthDevice(i));
			if (!helperParseLine(INFO_TEMP_FILE, path, "Mask:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			{
				strcpy(temp, _T("NOT_AVAILABLE_SHORT"));
			}
			sprintf(buf, "%s: %s", _T("NETMASK"), temp);
			interface_addMenuEntryDisabled((interfaceMenu_t*)&WANSubMenu, buf, thumbnail_configure);

			sprintf(path, "route -n | grep -e \"0\\.0\\.0\\.0 .* 0\\.0\\.0\\.0 *UG .* %s\"", helperEthDevice(i));
			if (!helperParseLine(INFO_TEMP_FILE, path, "0.0.0.0", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			{
				strcpy(temp, _T("NOT_AVAILABLE_SHORT"));
			}
			sprintf(buf, "%s: %s", _T("GATEWAY"), temp);
			interface_addMenuEntryDisabled((interfaceMenu_t*)&WANSubMenu, buf, thumbnail_configure);

			int found = -1;
			int fd = open( "/etc/resolv.conf", O_RDONLY );
			if( fd > 0 )
			{
				char *ptr;
				while( helperReadLine( fd, temp ) == 0 && temp[0] )
				{
					if( (ptr = strstr( temp, "nameserver " )) != NULL )
					{
						ptr += 11;
						found++;
						sprintf(buf, "%s %d: %s", _T("DNS_SERVER"), found+1, ptr);
						interface_addMenuEntryDisabled((interfaceMenu_t*)&WANSubMenu, buf, thumbnail_configure);
					}
				}
				close(fd);
			}
			if( found < 0 )
			{
				sprintf(buf, "%s: %s", _T("DNS_SERVER"), _T("NOT_AVAILABLE_SHORT"));
				interface_addMenuEntryDisabled((interfaceMenu_t*)&WANSubMenu, buf, thumbnail_configure);
			}
		}
		/*sprintf(path, "ifconfig %s | grep HWaddr", helperEthDevice(i));
		if (!helperParseLine(INFO_TEMP_FILE, path, "HWaddr ", temp, ' '))			 // eth0      Link encap:Ethernet  HWaddr 76:60:37:02:24:02
		{
			strcpy(temp, _T("NOT_AVAILABLE_SHORT"));
		}
		sprintf(buf, "%s: %s",  _T("MAC_ADDRESS"), temp);
		interface_addMenuEntryDisabled((interfaceMenu_t*)&WANSubMenu, buf, thumbnail_configure);
		*/

		sprintf(buf, "%s", _T("NET_RESET"));
		interface_addMenuEntry((interfaceMenu_t*)&WANSubMenu, buf, output_toggleReset, SET_NUMBER(i), settings_renew);

		return 0;
	}

	return 1;
}

#ifdef ENABLE_PPP
static char* output_getPPPPassword(int field, void* pArg)
{
	if( field == 0 )
		return pppInfo.password;

	return NULL;
}

static char* output_getPPPLogin(int field, void* pArg)
{
	if( field == 0 )
		return pppInfo.login;

	return NULL;
}

static int output_setPPPPassword(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL )
		return 1;

	FILE *f;

	strcpy( pppInfo.password, value );

	f = fopen(PPP_CHAP_SECRETS_FILE, "w");
	if( f != NULL )
	{
		fprintf( f, "%s pppoe %s *\n", pppInfo.login, pppInfo.password );
		fclose(f);
	} else
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
		return 0;
	}

	output_fillPPPMenu(pMenu, pArg);

	return 0;
}

static int output_togglePPPPassword(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText( pMenu, _T("PASSWORD"), "\\w+", output_setPPPPassword, output_getPPPPassword, inputModeABC, NULL );

	return 0;
}

static int output_setPPPLogin(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL )
		return 1;

	if( value[0] == 0 )
	{
		system("rm -f " PPP_CHAP_SECRETS_FILE);
		output_fillPPPMenu(pMenu, pArg);
		return 0;
	}

	strcpy(pppInfo.login, value);

	output_togglePPPPassword(pMenu, pArg);

	return 1;
}

static int output_togglePPPLogin(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText( pMenu, _T("LOGIN"), "\\w+", output_setPPPLogin, output_getPPPLogin, inputModeABC, NULL );
	return 0;
}

static void* output_checkPPPThread(void *pArg)
{
	for(;;)
	{
		sleep(2);
		output_fillPPPMenu(interfaceInfo.currentMenu, pArg);
		interface_displayMenu(1);
	}
	pthread_exit(NULL);
}

static int output_togglePPP(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 0);

	system("/etc/init.d/S65ppp stop");
	system("/etc/init.d/S65ppp start");

	output_fillPPPMenu( pMenu, pArg );

	interface_hideMessageBox();

	if( pppInfo.check_thread == 0 )
		pthread_create( &pppInfo.check_thread, NULL, output_checkPPPThread, NULL );

	return 0;
}

static int output_leavePPPMenu(interfaceMenu_t *pMenu, void* pArg)
{
	if( pppInfo.check_thread != 0 )
	{
		pthread_cancel( pppInfo.check_thread );
		pthread_join( pppInfo.check_thread, NULL );
		pppInfo.check_thread = 0;
	}
	return 0;
}

static int output_fillPPPMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str;
	pMenu = (interfaceMenu_t*)&PPPSubMenu;

	interface_clearMenuEntries(pMenu);

	FILE *f;
	f = fopen("/etc/ppp/chap-secrets", "r");
	if( f != NULL )
	{
		fgets( buf, sizeof(buf), f );
		if( sscanf( buf, "%s pppoe %s *", pppInfo.login, pppInfo.password ) != 2 )
		{
			pppInfo.login[0] = 0;
			pppInfo.password[0] = 0;
		}
		fclose(f);
	} else
	{
		pppInfo.login[0] = 0;
		pppInfo.password[0] = 0;
	}

	snprintf(buf, sizeof(buf), "%s: %s", _T("PPP_TYPE"), "PPPoE");
	interface_addMenuEntryDisabled(pMenu, buf, thumbnail_configure);

	snprintf(buf, sizeof(buf), "%s: %s", _T("LOGIN"), pppInfo.login[0] ? pppInfo.login : _T("OFF") );
	interface_addMenuEntry(pMenu, buf, output_togglePPPLogin, NULL, thumbnail_enterurl);

	if( pppInfo.login[0] != 0 )
	{
		snprintf(buf, sizeof(buf), "%s: ***", _T("PASSWORD"));
		interface_addMenuEntry(pMenu, buf, output_togglePPPPassword, NULL, thumbnail_enterurl);
	}

	interface_addMenuEntry(pMenu, _T("NET_RESET"), output_togglePPP, NULL, settings_renew);

	if( helperCheckDirectoryExsists( "/sys/class/net/ppp0" ) )
	{
		str = _T("ON");
	} else
	{
		int res;
		res = system("killall -0 pppd 2> /dev/null");
		if( WIFEXITED(res) == 1 && WEXITSTATUS(res) == 0 )
		{
			str = _T("CONNECTING");
			if( pppInfo.check_thread == 0 )
				pthread_create( &pppInfo.check_thread, NULL, output_checkPPPThread, NULL );
		}
		else
			str = _T("OFF");
	}
	snprintf(buf, sizeof(buf), "%s: %s", _T("PPP"), str);
	interface_addMenuEntryDisabled(pMenu, buf, thumbnail_info);

	return 0;
}
#endif // ENABLE_PPP

#ifdef ENABLE_LAN
static int output_fillLANMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char path[MAX_CONFIG_PATH];
	char buf[MENU_ENTRY_INFO_LENGTH];
	char temp[MENU_ENTRY_INFO_LENGTH];
	char *str;

	const int i = ifaceLAN;
	sprintf(path, "/sys/class/net/%s", helperEthDevice(i));
	if( helperCheckDirectoryExsists(path) )
	{
		interface_clearMenuEntries((interfaceMenu_t*)&LANSubMenu);
		if( !output_isBridge() )
		{
#ifdef STBPNX
			sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));

			getParam(path, "IPADDR", _T("NOT_AVAILABLE_SHORT"), temp);
#endif
#ifdef STSDK
			strcpy(temp, networkInfo.lan.ip.s_addr != 0 ? inet_ntoa(networkInfo.lan.ip) : _T("NOT_AVAILABLE_SHORT") );
#endif
			sprintf(buf, "%s: %s", _T("IP_ADDRESS"), temp);
			interface_addMenuEntry((interfaceMenu_t*)&LANSubMenu, buf, output_toggleIP, SET_NUMBER(i), thumbnail_configure);
		} else
		{
			sprintf(buf, "%s: %s", _T("IP_ADDRESS"), _T("GATEWAY_BRIDGE"));
			interface_addMenuEntryDisabled((interfaceMenu_t*)&LANSubMenu, buf, thumbnail_configure);
		}

		sprintf(buf, "%s", _T("NET_RESET"));
		interface_addMenuEntry((interfaceMenu_t*)&LANSubMenu, buf, output_toggleReset, SET_NUMBER(i), settings_renew);

#ifdef STBPNX
		if (!helperFileExists(STB_CONFIG_OVERRIDE_FILE))
		{
			switch( output_gatewayMode )
			{
				case gatewayModeNAT:    str = _T("GATEWAY_NAT_ONLY"); break;
				case gatewayModeFull:   str = _T("GATEWAY_FULL"); break;
				case gatewayModeBridge: str = _T("GATEWAY_BRIDGE"); break;
				default:                str = _T("OFF");
			}
			sprintf(buf,"%s: %s", _T("GATEWAY_MODE"), str);
			interface_addMenuEntry((interfaceMenu_t*)&LANSubMenu, buf, output_fillGatewayMenu, (void*)0, thumbnail_configure);
			if (output_gatewayMode != gatewayModeOff)
			{
				getParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", "0", temp);
				sprintf(buf,"%s: %s %s", _T("GATEWAY_BANDWIDTH"), atoi(temp) <= 0 ? _T("NONE") : temp, atoi(temp) <= 0 ? "" : _T("KBPS"));
				interface_addMenuEntry((interfaceMenu_t*)&LANSubMenu, buf, output_toggleGatewayBW, (void*)0, thumbnail_configure);
			}
		}
#endif
#ifdef STSDK
		switch( networkInfo.lanMode )
		{
			case lanBridge:     str = _T("GATEWAY_BRIDGE"); break;
			case lanStatic:     str = _T("ADDR_MODE_STATIC"); break;
			case lanDhcpServer: str = _T("GATEWAY_FULL"); break;
			case lanDhcpClient: str = _T("ADDR_MODE_DHCP"); break;
			default:            str = _T("OFF"); break;
		}
		sprintf(buf,"%s: %s", _T("GATEWAY_MODE"), str);
		interface_addMenuEntry((interfaceMenu_t*)&LANSubMenu, buf, output_fillGatewayMenu, (void*)0, thumbnail_configure);
#endif
		return 0;
	}

	return 1;
}
#endif // ENABLE_LAN

#ifdef ENABLE_WIFI
static int output_fillWifiMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	char temp[MENU_ENTRY_INFO_LENGTH];
	char *iface_name;
	int exists;
	char *str;

	const int i = ifaceWireless;
	iface_name  = _T("WIRELESS");
#if !(defined STB225)
	sprintf(temp, "/sys/class/net/%s", helperEthDevice(i));
	exists = helperCheckDirectoryExsists(temp);
#else
	exists = 1; // no check
#endif
	interface_clearMenuEntries((interfaceMenu_t*)&WifiSubMenu);
#ifdef STBPNX
	char path[MAX_CONFIG_PATH];

	sprintf(path, "/config/ifcfg-%s", helperEthDevice(i));
	getParam(path, "WAN_MODE", "0", temp);
	wifiInfo.wanMode = strtol( temp, NULL, 10 );
#endif
	if (wifiInfo.wanMode || exists)
	{
		sprintf(buf, "%s: %s", iface_name, wifiInfo.wanMode ? "WAN" : "LAN" );
		interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleWifiWAN, SET_NUMBER(i), thumbnail_configure);
#ifdef USE_WPA_SUPPLICANT
		if (wifiInfo.wanMode)
			output_readWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
#endif
	} else
	{
		sprintf(buf, "%s: %s", iface_name, _T("OFF") );
		interface_addMenuEntryDisabled((interfaceMenu_t*)&WifiSubMenu, buf, thumbnail_no);
		if (WifiSubMenu.baseMenu.selectedItem >= 0)
			WifiSubMenu.baseMenu.selectedItem = MENU_ITEM_BACK;
	}

	if( exists )
	{
#ifdef STBPNX
		getParam(path, "MODE", "ad-hoc", temp);
		if( strcmp(temp, "managed") == 0 )
		{
			wifiInfo.mode = wifiModeManaged;
			str = "Managed";
		}
		/*else if( strcmp(temp, "master") == 0 )
		{
			wifiInfo.mode = wifiModeMaster;
			str = "AP";
		}*/
		else
		{
			wifiInfo.mode = wifiModeAdHoc;
			str = "Ad-Hoc";
		}
		sprintf(buf, "%s %s: %s", iface_name, _T("MODE"), str);
		interface_addMenuEntryCustom((interfaceMenu_t*)&WifiSubMenu, interfaceMenuEntryText, buf, strlen(buf)+1,
		                             wifiInfo.wanMode, output_toggleWifiMode, NULL, NULL, NULL, SET_NUMBER(i), thumbnail_configure);

		getParam(path, "BOOTPROTO", "static", temp);
		if (strcmp("dhcp+dns", temp) == 0)
		{
			strcpy(temp, _T("ADDR_MODE_DHCP"));
			wifiInfo.dhcp = 1;
		} else
		{
			strcpy(temp, _T("ADDR_MODE_STATIC"));
			wifiInfo.dhcp = 0;
		}
		sprintf(buf, "%s %s: %s", iface_name, _T("ADDR_MODE"), temp);
		interface_addMenuEntryCustom((interfaceMenu_t*)&WifiSubMenu, interfaceMenuEntryText, buf, strlen(buf)+1,
		                             wifiInfo.wanMode, output_toggleMode, NULL, NULL, NULL, SET_NUMBER(i), thumbnail_configure);

		if( wifiInfo.dhcp == 0 || wifiInfo.wanMode == 0 )
		{
			getParam(path, "IPADDR", _T("NOT_AVAILABLE_SHORT"), temp);
			sprintf(buf, "%s %s: %s", iface_name, _T("IP_ADDRESS"), temp);
			interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleIP, SET_NUMBER(i), thumbnail_configure);

			getParam(path, "NETMASK", _T("NOT_AVAILABLE_SHORT"), temp);
			sprintf(buf, "%s %s: %s", iface_name, _T("NETMASK"), temp);
			interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleNetmask, SET_NUMBER(i), thumbnail_configure);
			
			if( wifiInfo.wanMode )
			{
				getParam(path, "DEFAULT_GATEWAY", _T("NOT_AVAILABLE_SHORT"), temp);
				sprintf(buf, "%s %s: %s", iface_name, _T("GATEWAY"), temp);
				interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleGw, SET_NUMBER(i), thumbnail_configure);

				getParam(path, "NAMESERVERS", _T("NOT_AVAILABLE_SHORT"), temp);
				sprintf(buf, "%s %s: %s", iface_name, _T("DNS_SERVER"), temp);
				interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleDNSIP, SET_NUMBER(i), thumbnail_configure);
			}
		}
		getParam(path, "ESSID", _T("NOT_AVAILABLE_SHORT"), temp);
		strncpy(wifiInfo.essid, temp, sizeof(wifiInfo.essid));
		wifiInfo.essid[sizeof(wifiInfo.essid)-1]=0;
#endif // STBPNX
#ifdef STSDK
		sprintf(buf, "%s %s", iface_name, _T("IP_ADDRESS"));
		interface_addMenuEntry(_M &WifiSubMenu, buf, (menuActionFunction)menuDefaultActionShowMenu,
			(void*)((wifiInfo.wanMode || networkInfo.lanMode == lanBridge) ? &WANSubMenu : &LANSubMenu), settings_network);
#endif // STSDK
		sprintf(buf, "%s %s: %s", iface_name, _T("ESSID"), wifiInfo.essid);
		interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleESSID, SET_NUMBER(i), thumbnail_enterurl);

#ifdef STBPNX
		getParam(path, "CHANNEL", "1", temp);
		wifiInfo.currentChannel = strtol( temp, NULL, 10 );
#endif

#ifdef STBPNX
		wifiInfo.auth       = wifiAuthOpen;
		wifiInfo.encryption = wifiEncTKIP;

		getParam(path, "AUTH", "SHARED", temp);
		if( strcasecmp( temp, "WPAPSK" ) == 0 )
			wifiInfo.auth = wifiAuthWPAPSK;
		else if( strcasecmp( temp, "WPA2PSK" ) == 0 )
			wifiInfo.auth = wifiAuthWPA2PSK;

		getParam(path, "ENCRYPTION", "WEP", temp);
		if( strcasecmp( temp, "WEP" ) == 0 )
			wifiInfo.auth = wifiAuthWEP;
		else if ( strcasecmp( temp, "AES" ) == 0 )
			wifiInfo.encryption = wifiEncAES;

		getParam(path, "KEY", "", temp);
		memcpy( wifiInfo.key, temp, sizeof(wifiInfo.key)-1 );
		wifiInfo.key[sizeof(wifiInfo.key)-1] = 0;
#endif // STBPNX

		sprintf(buf, "iwlist %s channel > %s", helperEthDevice(i), INFO_TEMP_FILE);
		system(buf);
		FILE* f = fopen( INFO_TEMP_FILE, "r" );
		if( f )
		{
			char *ptr;
			while( fgets(buf, sizeof(buf), f ) != NULL )
			{
				if( strncmp( buf, helperEthDevice(i), 5 ) == 0 )
				{
					// sample: 'wlan0     14 channels in total; available frequencies :'
					str = index(buf, ' ');
					while( str && *str == ' ' )
						str++;
					if( str )
					{
						ptr = index(str+1, ' ');
						if( ptr )
							*ptr++ = 0;

						wifiInfo.channelCount = strtol( str, NULL, 10 );
						if( wifiInfo.channelCount > 12 )
						{
							wifiInfo.channelCount = 12; // 13 and 14 are disallowed in Russia
						}
					}
				}
				/*else if( strstr( buf, "Current Frequency:" ) != NULL )
				{
					// sample: 'Current Frequency:2.412 GHz (Channel 1)'
					str = index(buf, '(');
					if( str )
					{
						str += 9;
						ptr = index(str, ')');
						if(ptr)
							*ptr = 0;
						wifiInfo.currentChannel = strtol( str, NULL, 10 );
					}
				}*/
			}
			fclose(f);
		} else
			eprintf("%s: failed to open %s\n", __FUNCTION__, INFO_TEMP_FILE);
		if( !wifiInfo.wanMode && wifiInfo.channelCount > 0 )
		{
			sprintf(buf, "%s %s: %d", iface_name, _T("CHANNEL_NUMBER"), wifiInfo.currentChannel);
			interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleWifiChannel, SET_NUMBER(i), thumbnail_configure);
		}

		sprintf(buf, "%s %s: %s", iface_name, _T("AUTHENTICATION"), wireless_auth_print( wifiInfo.auth ));
		interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleAuthMode, SET_NUMBER(i), thumbnail_configure);
		if( wifiInfo.auth == wifiAuthWPAPSK || wifiInfo.auth == wifiAuthWPA2PSK )
		{
			sprintf(buf, "%s %s: %s", iface_name, _T("ENCRYPTION"), wireless_encr_print( wifiInfo.encryption ));
			interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleWifiEncryption, SET_NUMBER(i), thumbnail_configure);
		}
		if( wifiInfo.auth != wifiAuthOpen && wifiInfo.auth < wifiAuthCount )
		{
			sprintf(buf, "%s %s: %s", iface_name, _T("PASSWORD"), wifiInfo.key );
			interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleWifiKey, NULL, thumbnail_enterurl);
		}

		sprintf(buf, "%s: %s", iface_name, _T("NET_RESET"));
		interface_addMenuEntry((interfaceMenu_t*)&WifiSubMenu, buf, output_toggleReset, SET_NUMBER(i), settings_renew);
	}

	return 0;
}

static int output_wifiKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch( cmd->command )
	{
		case interfaceCommandBlue:
			if( wifiInfo.wanMode )
			{
				interface_menuActionShowMenu( pMenu, &WirelessMenu );
				return 0;
			}
			// fall through
		default:
			break;
	}
	return 1;
}
#endif

#if (defined ENABLE_IPTV) && (defined ENABLE_XWORKS)
static int output_togglexWorks(interfaceMenu_t *pMenu, void* pArg)
{
	if ( setParam( STB_CONFIG_FILE, "XWORKS", pArg ? "ON" : "OFF" ) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
		return 1;
	}
	output_fillIPTVMenu( pMenu, NULL );
	interface_displayMenu(1);
	return 0;
}

static int output_togglexWorksProto(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	media_proto proto = (media_proto)pArg;

	switch(proto) // choose next proto
	{
		case mediaProtoHTTP: proto = mediaProtoRTSP; break;
		default:             proto = mediaProtoHTTP;
	}

	switch(proto) // choose right name for setting
	{
		case mediaProtoRTSP: str = "rtsp"; break;
		default:             str = "http";
	}

	if ( setParam( STB_CONFIG_FILE, "XWORKS_PROTO", str ) != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
		return 1;
	}
	system("/usr/local/etc/init.d/S94xworks config");
	output_fillIPTVMenu( pMenu, NULL );
	interface_displayMenu(1);
	return 0;
}

static int output_restartxWorks(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox(_T("LOADING"), settings_renew, 0);

	system("/usr/local/etc/init.d/S94xworks restart");

	output_fillIPTVMenu( pMenu, NULL );
	interface_hideMessageBox();
	return 0;
}
#endif // ENABLE_XWORKS

#ifdef ENABLE_IPTV
int output_toggleIPTVtimeout(interfaceMenu_t *pMenu, void* pArg)
{
	static const time_t timeouts[] = { 3, 5, 7, 10, 15, 30 };
	static const int    timeouts_count = sizeof(timeouts)/sizeof(timeouts[0]);
	int i;
	for( i = 0; i < timeouts_count; i++ )
		if( timeouts[i] >= appControlInfo.rtpMenuInfo.pidTimeout )
			break;
	if( i >= timeouts_count )
		appControlInfo.rtpMenuInfo.pidTimeout = timeouts[0];
	else
		appControlInfo.rtpMenuInfo.pidTimeout = timeouts[ (i+1)%timeouts_count ];

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	output_fillIPTVMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}

static int output_fillIPTVMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries((interfaceMenu_t*)&IPTVSubMenu);

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("IPTV_PLAYLIST"), appControlInfo.rtpMenuInfo.usePlaylistURL ? "URL" : "SAP");
	interface_addMenuEntry((interfaceMenu_t*)&IPTVSubMenu, buf, output_toggleIPTVPlaylist, NULL, thumbnail_configure);

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("IPTV_PLAYLIST"), appControlInfo.rtpMenuInfo.playlist[0] != 0 ? appControlInfo.rtpMenuInfo.playlist : _T("NONE"));
	interface_addMenuEntryCustom((interfaceMenu_t*)&IPTVSubMenu, interfaceMenuEntryText, buf, strlen(buf)+1,
	                             appControlInfo.rtpMenuInfo.usePlaylistURL, output_toggleURL, NULL, NULL, NULL, (void*)optionRtpPlaylist, thumbnail_enterurl);

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("IPTV_EPG"), appControlInfo.rtpMenuInfo.epg[0] != 0 ? appControlInfo.rtpMenuInfo.epg : _T("NONE"));
	interface_addMenuEntryCustom((interfaceMenu_t*)&IPTVSubMenu, interfaceMenuEntryText, buf, strlen(buf)+1,
	                             appControlInfo.rtpMenuInfo.usePlaylistURL, output_toggleURL, NULL, NULL, NULL, (void*)optionRtpEpg, thumbnail_enterurl);

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %ld", _T("IPTV_WAIT_TIMEOUT"), appControlInfo.rtpMenuInfo.pidTimeout);
	interface_addMenuEntry((interfaceMenu_t*)&IPTVSubMenu, buf, output_toggleIPTVtimeout, pArg, thumbnail_configure);

#ifdef ENABLE_XWORKS
	int xworks_enabled;
	media_proto proto;
	char *str;

	getParam( STB_CONFIG_FILE, "XWORKS", "OFF", buf );
	xworks_enabled = strcasecmp( "ON", buf ) == 0;
	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("XWORKS"), xworks_enabled ? _T("ON") : _T("OFF"));
	interface_addMenuEntry((interfaceMenu_t*)&IPTVSubMenu, buf, output_togglexWorks, SET_NUMBER(!xworks_enabled), thumbnail_configure);

	getParam( STB_CONFIG_FILE, "XWORKS_PROTO", "http", buf );
	if( strcasecmp( buf, "rtsp" ) == 0 )
	{
		proto = mediaProtoRTSP;
		str = _T("MOVIES");
	} else
	{
		proto = mediaProtoHTTP;
		str = "HTTP";
	}

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("USE_PROTOCOL"), str);
	if( xworks_enabled )
		interface_addMenuEntry((interfaceMenu_t*)&IPTVSubMenu, buf, output_togglexWorksProto, (void*)proto, thumbnail_configure);
	else
		interface_addMenuEntryDisabled((interfaceMenu_t*)&IPTVSubMenu, buf, thumbnail_configure);

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("XWORKS"), _T("RESTART"));
	interface_addMenuEntry((interfaceMenu_t*)&IPTVSubMenu, buf, output_restartxWorks, NULL, settings_renew);

	if( xworks_enabled && appControlInfo.rtpMenuInfo.usePlaylistURL )
	{
		char temp[256];
		sprintf(buf, "ifconfig %s | grep \"inet addr\"", helperEthDevice(ifaceWAN));
		if (helperParseLine(INFO_TEMP_FILE, buf, "inet addr:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
		{
			sprintf(buf, "http://%s:1080/xworks.xspf", temp );
			interface_addMenuEntryDisabled((interfaceMenu_t*)&IPTVSubMenu, buf, thumbnail_enterurl);
		}
	}

	if( interface_getSelectedItem( (interfaceMenu_t*)&IPTVSubMenu ) >= interface_getMenuEntryCount( (interfaceMenu_t*)&IPTVSubMenu ) )
	{
		interface_setSelectedItem( (interfaceMenu_t*)&IPTVSubMenu, 0 );
	}
#endif
#ifdef ENABLE_PROVIDER_PROFILES
	interface_addMenuEntry( (interfaceMenu_t*)&IPTVSubMenu, _T("PROFILE"), (menuActionFunction)menuDefaultActionShowMenu, (void*)&ProfileMenu, thumbnail_account );
#endif

	return 0;
}
#endif

#ifdef ENABLE_VOD
static int output_fillVODMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries((interfaceMenu_t*)&VODSubMenu);

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("VOD_PLAYLIST"), appControlInfo.rtspInfo.usePlaylistURL ? "URL" : _T("IP_ADDRESS") );
	interface_addMenuEntry((interfaceMenu_t*)&VODSubMenu, buf, output_toggleVODPlaylist, NULL, thumbnail_configure);

	if( appControlInfo.rtspInfo.usePlaylistURL )
	{
		snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("VOD_PLAYLIST"),
			appControlInfo.rtspInfo.streamInfoUrl != 0 ? appControlInfo.rtspInfo.streamInfoUrl : _T("NONE"));
		interface_addMenuEntry((interfaceMenu_t*)&VODSubMenu, buf, output_toggleURL, (void*)optionVodPlaylist, thumbnail_enterurl);
	}
	else
	{
		snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("VOD_INFO_IP_ADDRESS"), appControlInfo.rtspInfo.streamInfoIP);
		interface_addMenuEntry((interfaceMenu_t*)&VODSubMenu, buf, output_toggleVODINFOIP, NULL, thumbnail_enterurl);
	}

	snprintf(buf, MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("VOD_IP_ADDRESS"), appControlInfo.rtspInfo.streamIP);
	interface_addMenuEntry((interfaceMenu_t*)&VODSubMenu, buf, output_toggleVODIP, NULL, thumbnail_enterurl);

	return 0;
}
#endif

static int output_fillWebMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries((interfaceMenu_t*)&WebSubMenu);
	sprintf(buf, "%s: %s", _T("PROXY_ADDR"), appControlInfo.networkInfo.proxy[0] != 0 ? appControlInfo.networkInfo.proxy : _T("NONE"));
	interface_addMenuEntry((interfaceMenu_t*)&WebSubMenu, buf, output_toggleProxyAddr, pArg, thumbnail_enterurl);

	sprintf(buf, "%s: %s", _T("PROXY_LOGIN"), appControlInfo.networkInfo.login[0] != 0 ? appControlInfo.networkInfo.login : _T("NONE"));
	interface_addMenuEntry((interfaceMenu_t*)&WebSubMenu, buf, output_toggleProxyLogin, pArg, thumbnail_enterurl);

	sprintf(buf, "%s: ***", _T("PROXY_PASSWD"));
	interface_addMenuEntry((interfaceMenu_t*)&WebSubMenu, buf, output_toggleProxyPasswd, pArg, thumbnail_enterurl);

#ifdef ENABLE_BROWSER
#ifdef STBPNX
	char temp[MENU_ENTRY_INFO_LENGTH];

		getParam(BROWSER_CONFIG_FILE, "HomeURL", "", temp);
		sprintf(buf, "%s: %s", _T("MW_ADDR"), temp);
		interface_addMenuEntry((interfaceMenu_t*)&WebSubMenu, buf, output_toggleMWAddr, pArg, thumbnail_enterurl);

		getParam(BROWSER_CONFIG_FILE, "AutoLoadingMW", "", temp);
		if (temp[0] != 0)
		{
			sprintf(buf, "%s: %s", _T("MW_AUTO_MODE"), strcmp(temp,"ON")==0 ? _T("ON") : _T("OFF"));
			interface_addMenuEntry((interfaceMenu_t*)&WebSubMenu, buf, output_toggleMWAutoLoading, pArg, thumbnail_configure);
		}else
		{
			setParam(BROWSER_CONFIG_FILE, "AutoLoadingMW","OFF");
			sprintf(buf, "%s: %s", _T("MW_AUTO_MODE"), _T("OFF"));
			interface_addMenuEntry((interfaceMenu_t*)&WebSubMenu, buf, output_toggleMWAutoLoading, pArg, thumbnail_configure);
		}
#endif
#endif
	return 0;
}

#ifdef ENABLE_LAN
int output_fillGatewayMenu(interfaceMenu_t *pMenu, void* pArg)
{
	interface_clearMenuEntries((interfaceMenu_t*)&GatewaySubMenu);

#ifdef STBPNX
	gatewayMode_t mode;
	for( mode = gatewayModeOff; mode < gatewayModeCount; mode++ )
	{
		char *str = NULL;
		switch( mode ) {
			case gatewayModeBridge: str = _T("GATEWAY_BRIDGE"); break;
			case gatewayModeNAT:    str = _T("GATEWAY_NAT_ONLY"); break;
			case gatewayModeFull:   str = _T("GATEWAY_FULL"); break;
			default:                str = _T("OFF"); break;
		}
		interface_addMenuEntry((interfaceMenu_t*)&GatewaySubMenu, str, mode == output_gatewayMode ? NULL : output_toggleGatewayMode,
		                       (void*)mode,  mode == output_gatewayMode ? radiobtn_filled : radiobtn_empty);
	}
#endif
#ifdef STSDK
	lanMode_t mode;
	for( mode = 0; mode < lanModeCount; mode++)
	{
		char *str = NULL;
		switch( mode )
		{
			case lanBridge:     str = _T("GATEWAY_BRIDGE"); break;
			//case lanStatic:     str = _T("ADDR_MODE_STATIC"); break; /// TODO
			case lanDhcpServer: str = _T("GATEWAY_FULL"); break;
			//case lanDhcpClient: str = _T("ADDR_MODE_DHCP"); break; /// TODO
			default:
				continue;
		}
		interface_addMenuEntry((interfaceMenu_t*)&GatewaySubMenu, str, mode == networkInfo.lanMode ? NULL : output_toggleGatewayMode,
		                       (void*)mode,  mode == networkInfo.lanMode ? radiobtn_filled : radiobtn_empty);
	}
#endif

	interface_menuActionShowMenu(pMenu, (void*)&GatewaySubMenu);

	return 0;
}
#endif

int output_fillInterfaceMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries((interfaceMenu_t*)&InterfaceMenu);

	sprintf( buf, "%s: %s", _T("RESUME_AFTER_START"), _T( appControlInfo.playbackInfo.bResumeAfterStart ? "ON" : "OFF" ) );
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_toggleResumeAfterStart, NULL, settings_interface);

	sprintf( buf, "%s: %s", _T("AUTOPLAY"), _T( appControlInfo.playbackInfo.bAutoPlay ? "ON" : "OFF" ) );
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_toggleAutoPlay, NULL, settings_interface);

	sprintf( buf, "%s: %s", _T("FILE_SORTING"), _T( appControlInfo.mediaInfo.fileSorting == naturalsort ? "SORT_NATURAL" : "SORT_ALPHA" ));
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_toggleFileSorting, NULL, settings_interface);

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
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_toggleInterfaceAnimation, NULL, settings_interface);
#endif
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, _T("CHANGE_HIGHLIGHT_COLOR"), output_toggleHighlightColor, NULL, settings_interface);
	snprintf(buf, sizeof(buf), "%s: %d %s", _T("PLAYCONTROL_SHOW_TIMEOUT"), interfacePlayControl.showTimeout, _T("SECOND_SHORT"));
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_togglePlayControlTimeout, NULL, settings_interface);
	snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYCONTROL_SHOW_ON_START"), interfacePlayControl.showOnStart ? _T("ON") : _T("OFF") );
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_togglePlayControlShowOnStart, NULL, settings_interface);
#ifdef ENABLE_VOIP
	snprintf(buf, sizeof(buf), "%s: %s", _T("VOIP_INDICATION"), _T( interfaceInfo.enableVoipIndication ? "ON" : "OFF" ));
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_toggleVoipIndication, NULL, settings_interface);
	snprintf(buf, sizeof(buf), "%s: %s", _T("VOIP_BUZZER"), _T( appControlInfo.voipInfo.buzzer ? "ON" : "OFF" ));
	interface_addMenuEntry((interfaceMenu_t*)&InterfaceMenu, buf, output_toggleVoipBuzzer, NULL, settings_interface);
#endif

	interface_menuActionShowMenu(pMenu, (void*)&InterfaceMenu);
	interface_displayMenu(1);

	return 0;
}

void output_fillOutputMenu(void)
{
	char *str;
	interface_clearMenuEntries((interfaceMenu_t*)&OutputMenu);

//#ifdef STB6x8x
	str = _T("INFO");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, show_info, NULL, thumbnail_info);
//#endif
#ifdef ENABLE_STATS
	str = _T("PROFILE");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&StatsMenu, thumbnail_recorded);
#endif
	str = _T("LANGUAGE");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, l10n_initLanguageMenu, NULL, settings_language);

#ifdef ENABLE_MESSAGES
	str = _T("MESSAGES");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&MessagesMenu, thumbnail_messages);
#endif

	str = _T("TIME_DATE_CONFIG");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_fillTimeMenu, NULL, settings_datetime);

#if (defined STBxx) || (defined STSDK)
	str = _T("VIDEO_CONFIG");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_fillVideoMenu, NULL, settings_video);
#endif

#ifdef ENABLE_DVB
#ifdef HIDE_EXTRA_FUNCTIONS
	if ( offair_tunerPresent() )
#endif
	{
		str = _T("DVB_CONFIG");
#ifndef ENABLE_PASSWORD
		interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_fillDVBMenu, NULL, settings_dvb);
#else
		interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_askPassword, (void*)output_fillDVBMenu, settings_dvb);
#endif
	}
#endif // #ifdef ENABLE_DVB

	str = _T("NETWORK_CONFIG");
#ifndef ENABLE_PASSWORD
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_fillNetworkMenu, NULL, settings_network);
#else
		interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_askPassword, (void*)output_fillNetworkMenu, settings_network);
#endif

	str = _T("INTERFACE");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_fillInterfaceMenu, NULL, settings_interface);

#if (defined(STB225) || defined(STSDK))
	str = _T("3D_SETTINGS");
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_fill3DMenu, NULL, thumbnail_channels);
#endif

	str = _T("RESET_SETTINGS");
#ifndef ENABLE_PASSWORD
	interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_resetSettings, NULL, thumbnail_warning);
#else
		interface_addMenuEntry((interfaceMenu_t*)&OutputMenu, str, output_askPassword, (void*)output_resetSettings, thumbnail_warning);
#endif

	output_fillStandardMenu();
	output_fillFormatMenu();
	output_fillTimeZoneMenu();
#if 0
    if(!(gfx_isHDoutput()))
    {
        /* Ensure that the correct format entry is selected */
		interface_setSelectedItem((interfaceMenu_t*)&FormatMenu, 1);
    }
#endif
}


void output_buildMenu(interfaceMenu_t *pParent)
{
#ifdef ENABLE_WIFI
	wifiInfo.channelCount   = 0;
	wifiInfo.currentChannel = 1;
#endif

#ifdef STSDK
	memset(&networkInfo, 0, sizeof(networkInfo));
	output_readInterfacesFile();
#endif
	createListMenu(&OutputMenu, _T("SETTINGS"), thumbnail_configure, NULL, pParent,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&VideoSubMenu, _T("VIDEO_CONFIG"), settings_video, NULL, (interfaceMenu_t*)&OutputMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

#if (defined(STB225) || defined(STSDK))
	createListMenu(&Video3DSubMenu, _T("3D_SETTINGS"), settings_video, NULL, (interfaceMenu_t*)&OutputMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);
#endif // #ifdef STB225

	createListMenu(&TimeSubMenu, _T("TIME_DATE_CONFIG"), settings_datetime, NULL, (interfaceMenu_t*)&OutputMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);
#ifdef ENABLE_DVB
	createListMenu(&DVBSubMenu, _T("DVB_CONFIG"), settings_dvb, NULL, (interfaceMenu_t*)&OutputMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);
#endif
	createListMenu(&NetworkSubMenu, _T("NETWORK_CONFIG"), settings_network, NULL, (interfaceMenu_t*)&OutputMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&StandardMenu, _T("TV_STANDARD"), settings_video, NULL, (interfaceMenu_t*)&VideoSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&FormatMenu, _T("TV_FORMAT"), settings_video, NULL, (interfaceMenu_t*)&VideoSubMenu,
		interfaceListMenuIconThumbnail,
#ifdef STSDK
		output_enterFormatMenu, NULL, NULL);
#else
		NULL, NULL, NULL);
#endif

	createListMenu(&BlankingMenu, _T("TV_BLANKING"), settings_video, NULL, (interfaceMenu_t*)&VideoSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&TimeZoneMenu, _T("TIME_ZONE"), settings_datetime, NULL, (interfaceMenu_t*)&TimeSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&InterfaceMenu, _T("INTERFACE"), settings_interface, NULL, (interfaceMenu_t*)&OutputMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&WANSubMenu, "WAN", settings_network, NULL, (interfaceMenu_t*)&NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_fillWANMenu, NULL, SET_NUMBER(ifaceWAN));

#ifdef ENABLE_PPP
	createListMenu(&PPPSubMenu, _T("PPP"), settings_network, NULL, (interfaceMenu_t*)&NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_fillPPPMenu, output_leavePPPMenu, SET_NUMBER(ifaceWAN));
#endif
#ifdef ENABLE_LAN
	createListMenu(&LANSubMenu, "LAN", settings_network, NULL, (interfaceMenu_t*)&NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_fillLANMenu, NULL, SET_NUMBER(ifaceLAN));

	createListMenu(&GatewaySubMenu, _T("GATEWAY_MODE"), settings_network, NULL, (interfaceMenu_t*)&LANSubMenu,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);
#endif
#ifdef ENABLE_WIFI
	int wifi_icons[4] = { 0, 0, 0, statusbar_f4_enterurl };
	createListMenu(&WifiSubMenu, _T("WIRELESS"), settings_network, wifi_icons, (interfaceMenu_t*)&NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_fillWifiMenu, NULL, SET_NUMBER(ifaceWireless));
	interface_setCustomKeysCallback(_M &WifiSubMenu, output_wifiKeyCallback);

	wireless_buildMenu(_M &WifiSubMenu);
#endif
#ifdef ENABLE_IPTV
	createListMenu(&IPTVSubMenu, _T("TV_CHANNELS"), thumbnail_multicast, NULL, (interfaceMenu_t*)&NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_fillIPTVMenu, NULL, NULL);
#ifdef ENABLE_PROVIDER_PROFILES
	createListMenu(&ProfileMenu, _T("PROFILE"), thumbnail_account, NULL, (interfaceMenu_t*)&IPTVSubMenu,
		interfaceListMenuIconThumbnail, output_enterProfileMenu, output_leaveProfileMenu, NULL);
#endif
#endif
#ifdef ENABLE_VOD
	createListMenu(&VODSubMenu, _T("MOVIES"), thumbnail_vod, NULL, (interfaceMenu_t*)&NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_fillVODMenu, NULL, NULL);
#endif
	createListMenu(&WebSubMenu, _T("INTERNET_BROWSING"), thumbnail_internet, NULL, (interfaceMenu_t*)&NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_fillWebMenu, NULL, NULL);

#ifdef ENABLE_MESSAGES
	messages_buildMenu((interfaceMenu_t*)&OutputMenu);
#endif

	TimeEntry.info.time.type    = interfaceEditTime24;

	output_fillStandardMenu();
	output_fillOutputMenu();
	output_fillFormatMenu();
	output_fillBlankingMenu();
	output_fillTimeZoneMenu();
}

void output_cleanupMenu(void)
{
#ifdef ENABLE_WIFI
	wireless_cleanupMenu();
#endif
}

int output_isBridge(void)
{
#ifdef STBPNX
	char temp[64];
	getParam(STB_CONFIG_FILE, "CONFIG_GATEWAY_MODE", "OFF", temp);
	return strcmp(temp, "BRIDGE") == 0;
#endif
#ifdef STSDK
	return networkInfo.lanMode == lanBridge;
#endif
	return 0;
}

#if (defined ENABLE_IPTV) && (defined ENABLE_PROVIDER_PROFILES)
static int output_select_profile(const struct dirent * de)
{
	if( de->d_name[0] == '.' )
		return 0;

	char full_path[MAX_PROFILE_PATH];
	char name[MENU_ENTRY_INFO_LENGTH];
	name[0] = 0;
	snprintf(full_path, sizeof(full_path), "%s%s",STB_PROVIDER_PROFILES_DIR,de->d_name);
	getParam(full_path, "NAME", "", name);
	return name[0] != 0;
}

static int output_enterProfileMenu(interfaceMenu_t *pMenu, void* pArg)
{
	if( output_profiles == NULL )
	{
		char name[MENU_ENTRY_INFO_LENGTH];
		char full_path[MAX_PROFILE_PATH];
		int i;

		interface_clearMenuEntries((interfaceMenu_t *)&ProfileMenu);
		if( readlink(STB_PROVIDER_PROFILE, output_profile, sizeof(output_profile)) <= 0 )
			output_profile[0] = 0;

		output_profiles_count = scandir( STB_PROVIDER_PROFILES_DIR, &output_profiles, output_select_profile, alphasort );
		for( i = 0; i < output_profiles_count; i++ )
		{
			snprintf(full_path, sizeof(full_path), "%s%s",STB_PROVIDER_PROFILES_DIR,output_profiles[i]->d_name);
			getParam(full_path, "NAME", "", name);
			interface_addMenuEntry( (interfaceMenu_t *)&ProfileMenu, name, output_setProfile, SET_NUMBER(i),
			                        strcmp(full_path, output_profile) == 0 ? radiobtn_filled : radiobtn_empty );
		}
		if( 0 == output_profiles_count )
			interface_addMenuEntryDisabled((interfaceMenu_t *)&ProfileMenu, _T("NO_FILES"), thumbnail_info);
	}
	return 0;
}

static int output_leaveProfileMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int i;
	if( output_profiles != NULL )
	{
		for( i = 0; i < output_profiles_count; i++ )
			free(output_profiles[i]);

		free(output_profiles);
		output_profiles = NULL;
		output_profiles_count = 0;
	}
	return 0;
}

static int output_setProfile(interfaceMenu_t *pMenu, void* pArg)
{
	int index = GET_NUMBER(pArg);
	int i;
	char full_path[MAX_PROFILE_PATH];
	char buffer[MAX_URL+64];
	char *value;
	size_t value_len;
	FILE *profile = NULL;

	snprintf(full_path, sizeof(full_path), "%s%s",STB_PROVIDER_PROFILES_DIR,output_profiles[index]->d_name);
	profile = fopen(full_path, "r");

	if( !profile )
	{
		eprintf("%s: Failed to open profile '%s': %s\n", __FUNCTION__, full_path, strerror(errno));
		interface_showMessageBox(_T("FAIL"), thumbnail_error, 3000);
		return 1;
	}

	interface_showMessageBox(_T("LOADING"), settings_renew, 0);
	eprintf("%s: loading profile %s\n", __FUNCTION__, full_path);
	while( fgets( buffer, sizeof(buffer), profile ) )
	{
		value = strchr(buffer, '=');
		if( NULL == value )
			continue;
		*value = 0;
		value++;
		value_len = strlen(value);
		while( value_len > 0 && (value[value_len-1] == '\n' || value[value_len-1] == '\r' ) )
		{
			value_len--;
			value[value_len] = 0;
		}

		if( strcmp( buffer, "RTPPLAYLIST" ) == 0 )
		{
			strcpy( appControlInfo.rtpMenuInfo.playlist, value );
			appControlInfo.rtpMenuInfo.usePlaylistURL = value[0] != 0;
		} else
		if( strcmp( buffer, "RTPEPG" ) == 0 )
		{
			strcpy( appControlInfo.rtpMenuInfo.epg, value );
		} else
		if( strcmp( buffer, "RTPPIDTIMEOUT" ) == 0 )
		{
			appControlInfo.rtpMenuInfo.pidTimeout = atol( value );
		} else
		if( strcmp( buffer, "VODIP") == 0 )
		{
			strcpy( appControlInfo.rtspInfo.streamIP, value );
		} else
		if( strcmp( buffer, "VODINFOURL" ) == 0 )
		{
			strcpy( appControlInfo.rtspInfo.streamInfoUrl, value );
			appControlInfo.rtspInfo.usePlaylistURL = value[0] != 0;
		} else
		if( strcmp( buffer, "VODINFOIP" ) == 0 )
		{
			strcpy( appControlInfo.rtspInfo.streamIP, "VODIP" );
		} else
		if( strcmp( buffer, "FWUPDATEURL" ) == 0 && value_len > 0 &&
			/* URL can have any characters, so we should  */
			value_len < sizeof(buffer)-(sizeof("hwconfigManager l -1 UPURL '")-1+1) &&
		   (strncasecmp( value, "http://", 7 ) == 0 || strncasecmp( value, "ftp://", 6 ))
		  )
		{
			char *dst, *src;

			eprintf("%s: Setting new fw update url %s\n", __FUNCTION__, value);
			src = &value[value_len-1];
			dst = &buffer[ sizeof("hwconfigManager l -1 UPURL '")-1 + value_len + 1 ];
			*dst-- = 0;
			*dst-- = '\'';
			while( src >= value )
				*dst-- = *src--;
			memcpy(buffer, "hwconfigManager l -1 UPURL '", sizeof("hwconfigManager l -1 UPURL '")-1);
			dprintf("%s: '%s'\n", __FUNCTION__, buffer);
			system(buffer);
		} else
		if( strcmp( buffer, "FWUPDATEADDR" ) == 0 && value_len > 0 && value_len < 24 ) // 234.4.4.4:4323
		{
#ifndef DEBUG
#define DBG_MUTE  " > /dev/null"
#else
#define DBG_MUTE
#endif
			char *dst, *port_str;
			unsigned int port;

			eprintf("%s: Setting new fw update address %s\n", __FUNCTION__, value);
			port_str = strchr(value, ':');
			if( port_str != NULL )
				*port_str++ = 0;
			dst = &value[value_len+1];
			sprintf(dst, "hwconfigManager s -1 UPADDR 0x%08X" DBG_MUTE, inet_addr(value));
			dprintf("%s: '%s'\n", __FUNCTION__, dst);
			system(dst);
			if( port_str != NULL )
			{
				port = atol(port_str) & 0xffff;
				sprintf(dst, "hwconfigManager s -1 UPADDR 0xFFFF%04X" DBG_MUTE, port);
				dprintf("%s: '%s'\n", __FUNCTION__, dst);
				system(dst);
			}
		}
#if 0
		else
		if( strcmp( buffer, "PASSWORD" ) == 0 && value_len == 32 )
		{
			char *dst = &value[value_len+1];

			sprintf( dst, "hwconfigManager l -1 PASSWORD %s", value );
			eprintf("%s: Setting new password %s\n", __FUNCTION__, value);
			dprintf("%s: '%s'\n", __FUNCTION__, buffer);
			system(buffer);
		} else
		if( strcmp( buffer, "COMMUNITY_PASSWORD" ) == 0 && value[0] != 0 && value_len < (sizeof(buffer)-sizeof("hwconfigManager l -1 COMMPWD ")) )
		{
			char *dst, *src;

			system("/usr/local/etc/init.d/S98snmpd stop");
			dst = &buffer[ sizeof("hwconfigManager l -1 COMMPWD ")-1 + value_len ];
			src = &value[value_len];
			eprintf("%s: Setting new SNMP password %s\n", __FUNCTION__, value);
			while( src >= value )
				*dst-- = *src--;
			memcpy(buffer, "hwconfigManager l -1 COMMPWD ", sizeof("hwconfigManager l -1 COMMPWD ")-1);
			dprintf("%s: '%s'\n", __FUNCTION__, buffer);
			system(buffer);
			system("/usr/local/etc/init.d/S98snmpd start");
		}
#endif
	}
	fclose(profile);
	interface_hideMessageBox();

	if (saveAppSettings() != 0 && bDisplayedWarning == 0)
	{
		bDisplayedWarning = 1;
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}

	unlink(STB_PROVIDER_PROFILE);
	symlink(full_path, STB_PROVIDER_PROFILE);
	for( i = 0; i < ProfileMenu.baseMenu.menuEntryCount; i++ )
		ProfileMenu.baseMenu.menuEntry[i].thumbnail = ProfileMenu.baseMenu.menuEntry[i].pArg == pArg ? radiobtn_filled : radiobtn_empty;

	interface_menuActionShowMenu(pMenu, &interfaceMainMenu);

	return 0;
}

int output_checkProfile(void)
{
	int fd = open(STB_PROVIDER_PROFILE, O_RDONLY);
	if( fd < 0 )
	{
		interfaceInfo.currentMenu = (interfaceMenu_t*)&ProfileMenu;
		output_enterProfileMenu(interfaceInfo.currentMenu, NULL);
		return 1;
	}
	close(fd);
	return 0;
}
#endif // (defined ENABLE_IPTV) && (defined ENABLE_PROVIDER_PROFILES)

#if (defined ENABLE_WIFI) && (defined USE_WPA_SUPPLICANT)
int output_readWpaSupplicantConf( const char *filename )
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	getParam( filename, "ssid", "\"" DEFAULT_ESSID "\"", buf );
	strncpy( wifiInfo.essid, &buf[1], sizeof(wifiInfo.essid) );
	wifiInfo.essid[sizeof(wifiInfo.essid)-1]=0;
	if( wifiInfo.essid[0] )
		wifiInfo.essid[strlen(wifiInfo.essid)-1]=0;
	getParam( filename, "psk", "\"\"", buf );
	strncpy( wifiInfo.key, &buf[1], sizeof(wifiInfo.key) );
	wifiInfo.key[sizeof(wifiInfo.key)-1]=0;
	if( wifiInfo.key[0] )
		wifiInfo.key[strlen(wifiInfo.key)-1]=0;
	else
		wifiInfo.auth = wifiAuthOpen;
	return 0;
}

int output_writeWpaSupplicantConf( const char *filename )
{
	FILE *f = fopen( filename, "w" );
	if (f == NULL)
	{
		eprintf("%s: failed to open file: %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}
	fprintf(f, "ctrl_interface=DIR=%s\n", STB_WPA_SUPPLICANT_CTRL_DIR);
	fprintf(f, "ap_scan=%d\n\nnetwork={\n", wifiInfo.mode == wifiModeAdHoc ? 2 : 1 );
	fprintf(f, "ssid=\"%s\"\n", wifiInfo.essid );
	if (wifiInfo.mode == wifiModeAdHoc)
		fprintf(f, "mode=1\n" );
	if (wifiInfo.auth <= wifiAuthWEP)
	{
		fprintf(f, "key_mgmt=NONE\n");
		if (wifiInfo.key[0] != 0)
			fprintf(f, "wep_key0=%s\n",  wifiInfo.key );
	} else
		fprintf(f, "psk=\"%s\"\n",  wifiInfo.key );
	fprintf(f, "}\n");
	fclose(f);
	return 0;
}
#endif //ENABLE_WIFI && USE_WPA_SUPPLICANT

#ifdef STSDK
#define MIDTOKEN " \t"
#define ENDTOKEN " \t\r\n"
int output_readInterfacesFile(void)
{
	int iface = -1; // loopback
	char buf[BUFFER_SIZE];
	char *ptr, *arg;
	struct in_addr addr;
	FILE *f;

	f = fopen( STB_RESOLV_CONF, "r" );
	if( f )
	{
		while( fgets(buf, sizeof(buf), f) != NULL )
		{
			if( (ptr = strstr( buf, "nameserver " )) != NULL )
			{
				ptr += 11;
				inet_aton(ptr, &networkInfo.dns);
				break;
			}
		}
		fclose(f);
		f = NULL;
	}

	f = fopen(NETWORK_INTERFACES_FILE, "r");
	if(!f)
	{
		eprintf("%s: failed to open file: %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	while( fgets(buf, sizeof(buf), f) != NULL )
	{
		if( buf[0] == '#' )
		{
			// hack so we know beforehead that we are using bridge config
			if( strstr(buf, "bridge") != 0 )
			{
				eprintf("%s: using bridge mode\n", __func__);
				networkInfo.lanMode = lanBridge;
			}
			continue;
		}
		if( strncasecmp(buf, "iface ", 6) == 0 ) // iface setting
		{
			ptr = strtok(&buf[6], MIDTOKEN);
			if(!ptr)
				continue;
			if(strcmp(ptr, "lo") == 0)
			{
				iface = -1;
				continue;
			} else
			if(strcmp(ptr, "eth0") == 0)
				iface = ifaceWAN;
			else
			if(strcmp(ptr, "eth1") == 0)
				iface = ifaceLAN;
			else
			if(strcmp(ptr,"br0") == 0)
				iface = networkInfo.lanMode == lanBridge ? ifaceWAN : ifaceLAN;
			else
#ifdef ENABLE_WIFI
			if(strcmp(ptr,"wlan0") == 0)
				iface = ifaceWireless;
			else
#endif
			{
				dprintf("%s: unknown interface %s\n", __FUNCTION__, ptr);
				continue;
			}
			ptr = strtok(NULL, MIDTOKEN); // "inet"
			ptr = strtok(NULL, ENDTOKEN);
			if(!ptr)
			{
				dprintf("%s: undefined mode for %s\n", __FUNCTION__, helperEthDevice(iface));
				continue;
			}
			switch( iface )
			{
				case ifaceWAN:
					if( strcmp(ptr, "dhcp") == 0 )
						networkInfo.wanDhcp = 1;
					break;
				case ifaceLAN:
					if( strcmp(ptr, "static") == 0 )
					{
						if( helperFileExists(STB_DHCPD_CONF) )
							networkInfo.lanMode = lanDhcpServer;
						else
							networkInfo.lanMode = lanStatic;
					} else if(strcmp(ptr, "dhcp") == 0)
						networkInfo.lanMode = lanDhcpClient;
					// else networkInfo.lanMode = lanBridge;
					eprintf("%s: set lan mode to %d\n", __func__, networkInfo.lanMode);
					break;
#ifdef ENABLE_WIFI
				case ifaceWireless:
					if( strcmp(ptr, "manual") == 0 )
					{
						wifiInfo.wanMode = 0;
						wifiInfo.mode = wifiModeMaster;
					} else
					{
						wifiInfo.wanMode = 1;
						iface = ifaceWAN;
						networkInfo.wanDhcp = strcmp(ptr, "static");
					}
					break;
#endif // ENABLE_WIFI
				default:; // ignore
			}
		} else
		if( buf[0] == ' ' ) // option
		{
			ptr = strtok(buf,  MIDTOKEN);
			if( ptr == NULL )
				continue;
			arg = strtok(NULL, ENDTOKEN);
			if( arg == NULL )
				continue;
			if( strcasecmp(ptr, "address") == 0 )
			{
				inet_aton(arg, &addr);
				switch( iface )
				{
					case ifaceWAN: networkInfo.wan.ip = addr; break;
					case ifaceLAN: networkInfo.lan.ip = addr; break;
					default:;
				}
			} else
			if( strcasecmp(ptr, "netmask") == 0 )
			{
				inet_aton(arg, &addr);
				switch( iface )
				{
					case ifaceWAN: networkInfo.wan.mask = addr; break;
					case ifaceLAN: networkInfo.lan.mask = addr; break;
					default:;
				}
			} else
			if( strcasecmp(ptr, "gateway") == 0 )
			{
				inet_aton(arg, &addr);
				switch( iface )
				{
					case ifaceWAN: networkInfo.wan.gw = addr; break;
					case ifaceLAN: networkInfo.lan.gw = addr; break;
					default:;
				}
			}
#ifdef ENABLE_WIFI
			if( iface == ifaceWireless && networkInfo.lanMode == lanBridge &&
			    strcmp( ptr, "pre-up" ) == 0 &&
			    strcmp( arg, "wpa_supplicant" ) == 0 )
			{
				wifiInfo.wanMode = 1;
				wifiInfo.mode = wifiModeManaged;
			}
#endif
		}
		// ignore other lines
	}
	fclose(f);

	if( networkInfo.wan.mask.s_addr == 0 )
		networkInfo.wan.mask.s_addr = 0x00ffffff;
	if( networkInfo.lan.mask.s_addr == 0 )
		networkInfo.lan.mask.s_addr = 0x00ffffff;
	if( networkInfo.lan.ip.s_addr == 0 )
		networkInfo.lan.ip.s_addr = 0x016fa8c0; // 192.168.111.1

#ifdef ENABLE_WIFI
	wifiInfo.auth = wifiAuthWPA2PSK;
	wifiInfo.encryption = wifiEncAES;

	if (wifiInfo.wanMode)
	{
		output_readWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
	} else
	{
		getParam( STB_HOSTAPD_CONF, "ssid", "STB830", buf);
		strncpy( wifiInfo.essid, buf, sizeof(wifiInfo.essid) );
		wifiInfo.essid[sizeof(wifiInfo.essid)-1]=0;
		getParam( STB_HOSTAPD_CONF, "channel", "1", buf );
		wifiInfo.currentChannel = strtol(buf, NULL, 10);

		getParam( STB_HOSTAPD_CONF, "wep_key0", "", buf );
		if (buf[0] != 0)
		{
			wifiInfo.auth = wifiAuthWEP;
		} else
		{
			getParam( STB_HOSTAPD_CONF, "wpa_passphrase", "", buf );
			if (buf[0] != 0)
				wifiInfo.auth = wifiAuthWPAPSK;
			else
				wifiInfo.auth = wifiAuthOpen;
		}
		strncpy( wifiInfo.key, buf, sizeof(wifiInfo.key) );
		wifiInfo.key[sizeof(wifiInfo.key)-1]=0;
		if (wifiInfo.auth > wifiAuthWEP)
		{
			getParam( STB_HOSTAPD_CONF, "wpa_pairwise", "", buf );
			if (strstr( buf, "CCMP" ))
			{
				wifiInfo.auth = wifiAuthWPA2PSK;
				wifiInfo.encryption = wifiEncAES;
			} else
			{
				wifiInfo.auth = wifiAuthWPAPSK;
				wifiInfo.encryption = wifiEncTKIP;
			}
		}
	}
#endif // ENABLE_WIFI
	return 0;
}

int output_writeInterfacesFile(void)
{
	FILE *f = fopen(NETWORK_INTERFACES_FILE, "w");
	if(!f)
	{
		eprintf("%s: failed to open file: %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	if( networkInfo.wan.mask.s_addr == 0 )
		networkInfo.wan.mask.s_addr = 0x00ffffff;
	if( networkInfo.lan.mask.s_addr == 0 )
		networkInfo.lan.mask.s_addr = 0x00ffffff;

	if( networkInfo.lanMode == lanBridge )
	{
		fprintf(f, "# bridge\n\n"); // we always should know beforehead that we're using WAN-LAN bridge
	}

	fprintf(f, "auto lo\n");
	fprintf(f, "iface lo inet loopback\n\n");

#ifdef ENABLE_WIFI
	if( wifiInfo.wanMode )
	{
		if ( networkInfo.lanMode == lanBridge)
			fprintf(f, "iface wlan0 inet manual\n");
		else
		{
			fprintf(f, "auto wlan0\n");
			if( networkInfo.wanDhcp )
				fprintf(f, "iface wlan0 inet dhcp\n");
			else
				fprintf(f, "iface wlan0 inet static\n");
			fprintf(f, "  address %s\n", inet_ntoa(networkInfo.wan.ip));
			fprintf(f, "  netmask %s\n", inet_ntoa(networkInfo.wan.mask));
			if( networkInfo.wan.gw.s_addr != 0 )
				fprintf(f, "  gateway %s\n", inet_ntoa(networkInfo.wan.gw));
		}

		fprintf(f, "  pre-up wpa_supplicant -Dnl80211 -iwlan0 -c %s -B", STB_WPA_SUPPLICANT_CONF);
		if ( networkInfo.lanMode == lanBridge )
			fprintf(f, " -b br0");
		fprintf(f, "\n");
	} else
	{
		fprintf(f, "iface wlan0 inet manual\n");
		fprintf(f, "  pre-up hostapd %s -B\n", STB_HOSTAPD_CONF);
	}
	fprintf(f, "  post-down kill `cat /var/run/udhcpc.wlan0.pid` 2>/dev/null || true\n");
	fprintf(f, "  post-down killall hostapd 2>/dev/null || true\n");
	fprintf(f, "  post-down killall wpa_supplicant 2>/dev/null || true\n");
	fprintf(f, "  post-down rm -rf %s || true\n", STB_WPA_SUPPLICANT_CTRL_DIR);
	fprintf(f, "  post-down ifconfig wlan0 down 0\n");
	fprintf(f, "\n");
#endif

	if( networkInfo.lanMode == lanBridge
#ifdef ENABLE_WIFI
	    || wifiInfo.wanMode
#endif
	)
	{
		fprintf(f, "iface eth0 inet manual\n");
	} else
	{
		fprintf(f, "auto eth0\n");
		fprintf(f, "iface eth0 inet %s\n", networkInfo.wanDhcp ? "dhcp" : "static");
	}

#ifdef ENABLE_WIFI
	if( wifiInfo.wanMode == 0 && networkInfo.lanMode != lanBridge )
#endif
	{
	fprintf(f, "  address %s\n", inet_ntoa(networkInfo.wan.ip));
	fprintf(f, "  netmask %s\n", inet_ntoa(networkInfo.wan.mask));
	if( networkInfo.wan.gw.s_addr != 0 )
		fprintf(f, "  gateway %s\n", inet_ntoa(networkInfo.wan.gw));
	if( networkInfo.wanDhcp == 0 )
		fprintf(f, "  broadcast +\n");
	}
	fprintf(f, "\n");

	if( networkInfo.lanMode == lanBridge
#ifdef ENABLE_WIFI
		&& wifiInfo.wanMode == 0
#endif
	)
	{
		fprintf(f, "iface eth1 inet manual\n");
		fprintf(f, "  address %s\n", inet_ntoa(networkInfo.lan.ip));
		fprintf(f, "  netmask %s\n", inet_ntoa(networkInfo.lan.mask));
		if( networkInfo.lan.gw.s_addr != 0 )
			fprintf(f, "  gateway %s\n", inet_ntoa(networkInfo.lan.gw));
		fprintf(f, "\n");
	}

	outputNfaceInfo_t *br = networkInfo.lanMode == lanBridge ? &networkInfo.wan : &networkInfo.lan;
	fprintf(f, "auto br0\n");
	fprintf(f, "iface br0 inet %s\n", networkInfo.lanMode == lanBridge ?
		(networkInfo.wanDhcp ? "dhcp" : "static") :
		(networkInfo.lanMode == lanDhcpClient ? "dhcp" : "static"));
	if( br->ip.s_addr != 0 )
		fprintf(f, "  address %s\n", inet_ntoa(br->ip));
	if( br->mask.s_addr != 0 )
		fprintf(f, "  netmask %s\n", inet_ntoa(br->mask));
	if( br->gw.s_addr != 0 )
		fprintf(f, "  gateway %s\n", inet_ntoa(br->gw));
	if( networkInfo.lanMode != lanDhcpClient )
		fprintf(f, "  broadcast +\n");
	if( networkInfo.lanMode == lanDhcpServer || networkInfo.lanMode == lanStatic )
	{
		struct in_addr subnet;
		subnet.s_addr = networkInfo.lan.ip.s_addr & 0x00ffffff;
		const char *wan = "eth0";
#ifdef ENABLE_WIFI
		if( wifiInfo.wanMode )
			wan = "wlan0";
#endif
		fprintf(f, "  pre-up iptables -t nat -A POSTROUTING -s %s/24 -o %s -j MASQUERADE\n", inet_ntoa(subnet), wan);
	}
	if( networkInfo.lanMode == lanBridge
#ifdef ENABLE_WIFI
		|| wifiInfo.wanMode
#endif
	)
	{
		fprintf(f, "  pre-up ifconfig eth0 up\n");
		// adding eth0 to bridge breaks nfs
		fprintf(f, "  pre-up cat /proc/cmdline | grep /dev/nfs >/dev/null || brctl addif br0 eth0\n");
	}
	if(helperCheckDirectoryExsists("/sys/class/net/eth1"))
	{
		fprintf(f, "  pre-up ifconfig eth1 up\n");
		fprintf(f, "  pre-up brctl addif br0 eth1\n");
	}
#ifdef ENABLE_WIFI
	if( wifiInfo.wanMode && networkInfo.lanMode == lanBridge )
		fprintf(f, "  pre-up brctl addif br0 wlan0 || true\n");
	fprintf(f, "  pre-up ifup %s || true\n", helperEthDevice(ifaceWireless));
#endif
	if(helperCheckDirectoryExsists("/sys/class/net/eth1"))
		fprintf(f, "  post-down brctl delif br0 eth1\n");
	// ensure WAN is leaving bridge in any mode
	fprintf(f, "  post-down brctl delif br0 eth0 || true\n");
#ifdef ENABLE_WIFI
	if( wifiInfo.wanMode && networkInfo.lanMode == lanBridge )
		fprintf(f, "  post-down brctl delif br0 wlan0 || true\n");
	fprintf(f, "  post-down ifdown %s || true\n", helperEthDevice(ifaceWireless));
#endif

	fclose(f);

#ifdef ENABLE_WIFI
	if (wifiInfo.wanMode)
	{
		return output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
	}
	else
	{
		char ch[4];
		setParam( STB_HOSTAPD_CONF, "ssid", wifiInfo.essid );
		snprintf(ch, sizeof(ch), "%d", wifiInfo.currentChannel);
		ch[3]=0;
		setParam( STB_HOSTAPD_CONF, "channel", ch );
		if( wifiInfo.auth <= wifiAuthWEP )
		{
			if( wifiInfo.auth == wifiAuthOpen )
				setParam( STB_HOSTAPD_CONF, "wep_key0", NULL );
			else
				setParam( STB_HOSTAPD_CONF, "wep_key0", wifiInfo.key );
			setParam( STB_HOSTAPD_CONF, "wpa", NULL );
			setParam( STB_HOSTAPD_CONF, "wpa_key_mgmt",   NULL );
			setParam( STB_HOSTAPD_CONF, "wpa_passphrase", NULL );
			setParam( STB_HOSTAPD_CONF, "wpa_pairwise",   NULL );
		} else
		{
			setParam( STB_HOSTAPD_CONF, "wep_key0", NULL );
			setParam( STB_HOSTAPD_CONF, "wpa", wifiInfo.auth == wifiAuthWPA2PSK ? "2" : "1" );
			setParam( STB_HOSTAPD_CONF, "wpa_key_mgmt", "WPA-PSK" );
			setParam( STB_HOSTAPD_CONF, "wpa_passphrase", wifiInfo.key );
			setParam( STB_HOSTAPD_CONF, "wpa_pairwise", wifiInfo.encryption == wifiEncAES ? "CCMP TKIP" : "TKIP" );
		}
	}
#endif

	return 0;
}

int output_writeDhcpConfig(void)
{
	struct in_addr subnet, range_start, range_end, mask;
	char dns[MENU_ENTRY_INFO_LENGTH];

	FILE *f = fopen(STB_DHCPD_CONF, "w");
	if(!f)
	{
		eprintf("%s: failed to open file: %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	if( networkInfo.lan.mask.s_addr == 0 )
		mask.s_addr = 0x00ffffff;
	else
		mask = networkInfo.lan.mask;
	subnet.s_addr = networkInfo.lan.ip.s_addr & mask.s_addr;
	if( (networkInfo.lan.ip.s_addr & 0xff000000)  < 0x80000000 )
	{
		range_start.s_addr = (networkInfo.lan.ip.s_addr & 0x00ffffff)+( 0x80    << 24);
		range_end.s_addr   = (networkInfo.lan.ip.s_addr & 0x00ffffff)+((0xff-1) << 24);
	} else
	{
		range_start.s_addr = (networkInfo.lan.ip.s_addr & 0x00ffffff)+( 0x01    << 24);
		range_end.s_addr   = (networkInfo.lan.ip.s_addr & 0x00ffffff)+((0x80-1) << 24);
	}
	helperParseLine(INFO_TEMP_FILE,"cat /etc/resolv.conf | grep nameserver | sed 's/.* \\(.*\\)/\\1/' | tr '\\n' ' '", NULL, dns, 0);

	fprintf(f, "ddns-update-style none;\n");
	fprintf(f, "default-lease-time 14400;\n");
	fprintf(f, "subnet %s",  inet_ntoa(subnet)); fprintf(f, " netmask %s {\n", inet_ntoa(mask));
	fprintf(f, "  range %s", inet_ntoa(range_start));        fprintf(f, " %s;\n", inet_ntoa(range_end));
	fprintf(f, "  option routers %s;\n", inet_ntoa(networkInfo.lan.ip));
	if( dns[0] != 0 )
		fprintf(f, "  option domain-name-servers %s;\n", dns);
	fprintf(f, "}\n\n");

	fclose(f);
	return 0;
}
#endif // STSDK
