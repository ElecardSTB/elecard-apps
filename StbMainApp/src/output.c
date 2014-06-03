
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
#include "bouquet.h"

#include "dvb.h"
#include "analogtv.h"
#include "rtp.h"
#include "pvr.h"
#include "md5.h"
#include "voip.h"
#include "messages.h"
#include "Stb225.h"
#include "wpa_ctrl.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

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
#ifdef STBPNX
#define IFACE_CONFIG_PREFIX         "/config/ifcfg-"
#define WAN_CONFIG_FILE             IFACE_CONFIG_PREFIX "eth0"
#define LAN_CONFIG_FILE             IFACE_CONFIG_PREFIX "eth1"
#define WLAN_CONFIG_FILE            IFACE_CONFIG_PREFIX "wlan0"
#define XWORKS_INIT_SCRIPT          "/usr/local/etc/init.d/S94xworks"
#endif
#ifdef STSDK
#define PPP_CHAP_SECRETS_FILE       "/var/etc/ppp/chap-secrets"
#define TIMEZONE_FILE               "/var/etc/localtime"
#define NTP_CONFIG_FILE             "/var/etc/ntpd"
#define WAN_CONFIG_FILE             "/var/etc/ifcfg-wan"
#define LAN_CONFIG_FILE             "/var/etc/ifcfg-lan"
#define WLAN_CONFIG_FILE            "/var/etc/ifcfg-wlan0"
#define XWORKS_INIT_SCRIPT          "/etc/init.d/S94xworks"
#endif //STSDK

#define MOBILE_APN_FILE             SYSTEM_CONFIG_DIR "/ppp/chatscripts/apn"
#define MOBILE_PHONE_FILE           SYSTEM_CONFIG_DIR "/ppp/chatscripts/phone"

#define OUTPUT_INFO_SET(type,index) (void*)(intptr_t)(((int)type << 16) | (index))
#define OUTPUT_INFO_GET_TYPE(info)  ((int)(intptr_t)info >> 16)
#define OUTPUT_INFO_GET_INDEX(info) ((int)(intptr_t)info & 0xFFFF)

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
	optionRtpEpg,
	optionRtpPlaylist,
	optionVodPlaylist,
#ifdef ENABLE_TELETES
	optionTeletesPlaylist,
#endif
} outputUrlOption;

typedef enum
{
	optionIP   = 0x01,
	optionGW   = 0x02,
	optionMask = 0x04,
	optionDNS  = 0x08,
	optionMode = 0x10,
} outputIPOption;

typedef enum
{
	optionLowFreq    = 0,
	optionHighFreq,
	optionFreqStep,
	optionSymbolRate,
} outputDvbOption;

typedef enum {
	lanDhcpServer = 0,
	lanStatic,
#ifdef STSDK
	lanModeCount,
#endif
	lanBridge,
#ifndef STSDK
	lanModeCount,
#endif
	lanDhcpClient,
} lanMode_t;

#ifdef ENABLE_PPP
typedef struct
{
	char login[MENU_ENTRY_INFO_LENGTH];
	char password[MENU_ENTRY_INFO_LENGTH];
} pppInfo_t;
#endif

#ifdef STSDK

#define USE_WPA_SUPPLICANT

typedef struct
{
	struct in_addr ip;
	struct in_addr mask;
	struct in_addr gw;
} outputNfaceInfo_t;
#endif // STSDK

typedef struct
{
#ifdef STSDK
	outputNfaceInfo_t wan;
	int               wanDhcp;
	struct in_addr    dns;
	outputNfaceInfo_t lan;
#endif
	lanMode_t         lanMode;
	int               changed;
} outputNetworkInfo_t;

#ifdef ENABLE_WIFI

//#define USE_WPA_SUPPLICANT

#include <iwlib.h>

typedef struct
{
	int enable;
	int wanMode;
#ifdef STSDK
	int wanChanged;
	outputNfaceInfo_t wlan;
#endif
	int dhcp;
	int channelCount;
	int currentChannel;
	outputWifiMode_t       mode;
	outputWifiAuth_t       auth;
	outputWifiEncryption_t encryption;
	char essid[IW_ESSID_MAX_SIZE];
	char key[IW_ENCODING_TOKEN_MAX+1];
	int showAdvanced;
} outputWifiInfo_t;

typedef struct
{
	char inputName[20];
	char inputType[20];
	int inputSelect1;
	int inputSelect2;
} outputInputMode_t;

#endif

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
static int output_pingMenu(interfaceMenu_t* pMenu, void* pArg);
static int output_enterNetworkMenu(interfaceMenu_t *pMenu, void* notused);
static int output_leaveNetworkMenu(interfaceMenu_t *pMenu, void* notused);
static int output_confirmNetworkSettings(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int output_enterVideoMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterGraphicsModeMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_enterTimeMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterInterfaceMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterPlaybackMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterWANMenu (interfaceMenu_t *wanMenu, void* pArg);
static int output_fillWANMenu(interfaceMenu_t *wanMenu, void* pIface);

#ifdef ENABLE_ANALOGTV
static int output_enterAnalogTvMenu(interfaceMenu_t *pMenu, void* notused);
#endif
#ifdef ENABLE_PPP
static int output_enterPPPMenu (interfaceMenu_t *pMenu, void* pArg);
#endif
#ifdef ENABLE_LAN
static int output_enterLANMenu (interfaceMenu_t *lanMenu, void* pArg);
#endif
#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
static int output_fillLANMenu(interfaceMenu_t *lanMenu, void* pIface);
static int output_toggleDhcpServer(interfaceMenu_t *pMenu, void* pForce);
#ifdef STBPNX
static int output_confirmGatewayMode(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int output_toggleGatewayMode(interfaceMenu_t *pMenu, void* pArg);
static int output_enterGatewayMenu(interfaceMenu_t *pMenu, void* ignored);
#endif
#endif //(defined ENABLE_LAN) || (defined ENABLE_WIFI)

#ifdef ENABLE_WIFI
static int output_readWirelessSettings(void);
static int output_enterWifiMenu (interfaceMenu_t *pMenu, void* ignored);
static int output_wifiKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#ifdef STSDK
static int output_setHostapdChannel(int channel);
#endif
#ifdef USE_WPA_SUPPLICANT
static int output_readWpaSupplicantConf(const char *filename);
static int output_writeWpaSupplicantConf(const char *filename);
#endif
#endif // ENABLE_WIFI
#ifdef ENABLE_MOBILE
static void output_readMobileSettings(void);
static int output_writeMobileSettings(void);
static int output_enterMobileMenu (interfaceMenu_t *pMenu, void* ignored);
static int output_changeMobileAPN(interfaceMenu_t *pMenu, void* ignored);
static int output_changeMobilePhone(interfaceMenu_t *pMenu, void* ignored);
#endif // ENABLE_MOBILE

#ifdef ENABLE_IPTV
static int output_enterIPTVMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_changeIPTVPlaylist(interfaceMenu_t *pMenu, void* pArg);

#ifdef ENABLE_PROVIDER_PROFILES
static int output_enterProfileMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_leaveProfileMenu(interfaceMenu_t *pMenu, void* pArg);
static int output_setProfile(interfaceMenu_t *pMenu, void* pArg);
#endif

#endif // ENABLE_IPTV
#ifdef ENABLE_VOD
static int output_enterVODMenu (interfaceMenu_t *pMenu, void* pArg);
static int output_toggleVODPlaylist(interfaceMenu_t *pMenu, void* pArg);
#endif
static int output_enterWebMenu (interfaceMenu_t *pMenu, void* pArg);

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
static int output_enterPlaylistMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterPlaylistDigital(interfaceMenu_t *pMenu, void* notused);
static int output_enterPlaylistAnalog(interfaceMenu_t *pMenu, void* notused);
static int output_toggleDvbShowScrambled(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbBandwidth(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbPolarization(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbType(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDvbTuner(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDiseqcSwitch(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDiseqcPort(interfaceMenu_t *pMenu, void* pArg);
static int output_toggleDiseqcUncommited(interfaceMenu_t *pMenu, void* pArg);
static int output_clearDvbSettings(interfaceMenu_t *pMenu, void* pArg);
static int output_clearOffairSettings(interfaceMenu_t *pMenu, void* pArg);
static int output_confirmClearDvb(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int output_confirmClearOffair(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#endif // ENABLE_DVB

#ifdef ENABLE_3D
static interfaceListMenu_t Video3DSubMenu;
static int output_enter3DMenu(interfaceMenu_t *pMenu, void* pArg);
#endif // #ifdef STB225

static void output_colorSliderUpdate(void *pArg);

static char* output_getOption(outputUrlOption option);
static char* output_getURL(int index, void* pArg);
static int output_setURL(interfaceMenu_t *pMenu, char *value, void* pArg);
static int output_changeURL(interfaceMenu_t *pMenu, void* urlOption);

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

#ifdef STSDK

static int output_enterCalibrateMenu(interfaceMenu_t *pMenu, void * pArg);
static int output_calibrateCurrentMeter(interfaceMenu_t *pMenu, void* pArg);

static int output_enterInputsMenu(interfaceMenu_t *pMenu, void* notused);
static int output_enterUpdateMenu(interfaceMenu_t *pMenu, void* notused);

static int output_writeInterfacesFile(void);
static int output_writeDhcpConfig(void);

static int output_toggleAdvancedVideoOutput(interfaceMenu_t *pMenu, void* pArg);

#endif

static int32_t output_checkInputs(void);
static const char* output_getLanModeName(lanMode_t mode);
static void output_setIfaceMenuName(interfaceMenu_t *pMenu, const char *ifaceName, int wan, lanMode_t lanMode);
static int  output_isIfaceDhcp(stb810_networkInterface_t iface);

static void* output_refreshLoop(void *pMenu);
static int   output_refreshStop(interfaceMenu_t *pMenu, void *pArg);
static int   output_refreshStart(interfaceMenu_t *pMenu, void *pArg);

/** Display message box if failed is non-zero and no previous failures occured.
 *  Return non-zero if message was displayed (and display updated). */
static int output_warnIfFailed(int failed);

/** Display message box if saveFailed and redraw menu */
static int output_saveAndRedraw(int saveFailed, interfaceMenu_t *pMenu);

/** Refill menu using pActivatedAction
 * Use this function in messageBox handlers, as display will be updated on exit automatically */
static inline int output_refillMenu(interfaceMenu_t *pMenu)
{
	// assert (pMenu->pActivatedAction != NULL);
	return pMenu->pActivatedAction(pMenu, pMenu->pArg);
}

/** Refill menu using pActivatedAction and update display */
void output_redrawMenu(interfaceMenu_t *pMenu)
{
	output_refillMenu(pMenu);
	interface_displayMenu(1);
}

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

#ifdef ENABLE_DVB
static interfaceListMenu_t InterfacePlaylistSelectDigital;
static interfaceListMenu_t InterfacePlaylistSelectAnalog;
interfaceListMenu_t InterfacePlaylistEditorDigital;
interfaceListMenu_t InterfacePlaylistEditorAnalog;
static interfaceListMenu_t InterfacePlaylistMain;
static interfaceListMenu_t InterfacePlaylistAnalog;
static interfaceListMenu_t InterfacePlaylistDigital;
static interfaceListMenu_t DVBSubMenu;
static interfaceListMenu_t DiSEqCMenu;

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
#ifdef STSDK
static interfaceListMenu_t InputsSubMenu;
#endif
#ifdef ENABLE_ANALOGTV
static interfaceListMenu_t AnalogTvSubMenu;
#endif

static interfaceListMenu_t CurrentmeterSubMenu;
static interfaceListMenu_t VideoSubMenu;
static interfaceListMenu_t TimeSubMenu;
static interfaceListMenu_t NetworkSubMenu;

static interfaceListMenu_t WANSubMenu;
#ifdef ENABLE_PPP
static interfaceListMenu_t PPPSubMenu;
#endif
#ifdef ENABLE_LAN
static interfaceListMenu_t LANSubMenu;
#endif
#ifdef STBPNX
#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
static interfaceListMenu_t GatewaySubMenu;
#endif
#endif
#ifdef ENABLE_WIFI
interfaceListMenu_t WifiSubMenu;
#endif
#ifdef ENABLE_MOBILE
interfaceListMenu_t MobileMenu;
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

static interfaceEditEntry_t TimeEntry;
static interfaceEditEntry_t DateEntry;

static long info_progress;

static char output_ip[4*4];

static outputNetworkInfo_t networkInfo;
#ifdef ENABLE_WIFI
static outputWifiInfo_t wifiInfo;
#endif
#ifdef ENABLE_PPP
static pppInfo_t pppInfo;
#endif

#ifdef STSDK
static interfaceListMenu_t UpdateMenu;

videoOutput_t	*p_mainVideoOutput = NULL;
static char		previousFormat[64]; //this needs for cancel switching video output format
static uint32_t	isToggleContinues = 0; //this indicates that "togle vfmt" button pressed several times without apply/cancaling format

static uint32_t g_inputCount = 0;
static char inputNames[MENU_ENTRY_INFO_LENGTH][32];

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

#ifdef ENABLE_MOBILE
static struct {
	char apn  [MENU_ENTRY_INFO_LENGTH];
	char phone[MENU_ENTRY_INFO_LENGTH];
} mobileInfo = {
	.apn     = {0},
	.phone   = {0},
};
#endif // ENABLE_MOBILE

static pthread_t output_refreshThread = 0;

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

extern int8_t services_edit_able;
extern char channel_names_file_full[256];

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

static int32_t output_runInput(void *inputMode, char *inputName)
{
	elcdRpcType_t type;
	cJSON        *res   = NULL;
	cJSON * param = cJSON_CreateObject();
	if (param){
		cJSON_AddItemToObject(param, "input", cJSON_CreateString(inputMode));
	}
	st_rpcSync (elcmd_setvinput, param, &type, &res);
	cJSON_Delete(param);
	st_isOk(type, res, __FUNCTION__);
	cJSON_Delete(res);
	return 1;
}

static int32_t output_inputFilmTypeCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: %d %s\n", __func__, interface_commandName(cmd->command));

	outputInputMode_t *input = (outputInputMode_t *)pArg;
	input->inputSelect2 = cmd->command - interfaceCommand1 + 1;
	char text[60];

	switch(cmd->command) {
		case interfaceCommand1:
		case interfaceCommand2:
		case interfaceCommand5:
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand6: {
			sprintf(text, "%s:%d%d", input->inputType, input->inputSelect1, input->inputSelect2);
			output_runInput((void*)input->inputName, text);
			return 0;
		}
		case DIKS_HOME:
		case interfaceCommandExit:
		case interfaceCommandGreen:
		case interfaceCommandRed:
		default:
			return 0;
	}
}

static int32_t output_inputTypeCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: %d %s\n", __func__, interface_commandName(cmd->command));

	static outputInputMode_t input;
	input.inputSelect1 = cmd->command - interfaceCommand1 + 1;
	input.inputSelect2 = 0;
	strcpy(input.inputName, (char*)pArg);
	
	char menuText[1024];
	strncpy(menuText, "Choose type of film:\n", sizeof(menuText));
	strncat(menuText, "\n1. Thriller", sizeof(menuText));
	strncat(menuText, "\n2. Drama", sizeof(menuText));
	strncat(menuText, "\n3. Romance", sizeof(menuText));
	strncat(menuText, "\n4. Comedy", sizeof(menuText));
	strncat(menuText, "\n5. Sports", sizeof(menuText));
	strncat(menuText, "\n6. Documentary\n", sizeof(menuText));
	
	char text[60];
	switch(cmd->command) {
		case interfaceCommand1: {
			strcpy(input.inputType, "DVD");
			interface_showConfirmationBox(menuText, thumbnail_account_active, output_inputFilmTypeCallback, (void*)&input);
			return 1;
		}
		case interfaceCommand2: {
			strcpy(input.inputType, "VCR");
			interface_showConfirmationBox(menuText, thumbnail_account_active, output_inputFilmTypeCallback, (void*)&input);
			return 1;
		}
		case interfaceCommand3: {
			sprintf(text, "%s:%d%d", "GAME", input.inputSelect1, input.inputSelect2);
			output_runInput(pArg, text);
			return 0;
		}
		case interfaceCommand4: {
			sprintf(text, "%s:%d%d", "PC", input.inputSelect1, input.inputSelect2);
			output_runInput(pArg, text);
			return 0;
		}
		case interfaceCommand5: {
			sprintf(text, "%s:%d%d", "OTHER", input.inputSelect1, input.inputSelect2);
			output_runInput(pArg, text);
			return 0;
		}
		case DIKS_HOME:
		case interfaceCommandExit:
		case interfaceCommandGreen:
		case interfaceCommandRed:
		default:
			return 0;
	}
}

int output_setInput(interfaceMenu_t *pMenu, void* pArg)
{
	if (!pArg) {
		eprintf ("%s: Error setting input.\n", __FUNCTION__);
		return 0;
	}
#ifdef STSDK
	elcdRpcType_t type;
	cJSON        *res   = NULL;

	if (strcmp(pArg, INPUT_NONE) == 0){
		st_rpcSync (elcmd_disablevinput, NULL, &type, &res);
		st_isOk(type, res, __FUNCTION__);
		cJSON_Delete(res);
	}
	else {
		interface_showConfirmationBox("Choose using device:\n"
					      "\n1. DVD"
					      "\n2. VCR"
					      "\n3. Video Game"
					      "\n4. PC"
					      "\n5. Other\n", thumbnail_account_active, output_inputTypeCallback, pArg);
	}
#endif
	return 0;
}

static void output_fillInputsMenu(interfaceMenu_t *pMenu, void *pArg)
{
	int32_t			selected = MENU_ITEM_BACK;
	interfaceMenu_t	*inputsMenu = &(InputsSubMenu.baseMenu);
	elcdRpcType_t	type;
	cJSON			*list;
	int32_t			ret;

	int32_t  icon = thumbnail_channels;

	interface_clearMenuEntries(inputsMenu);
	g_inputCount = 0;

	ret = st_rpcSync(elcmd_listvinput, NULL, &type, &list);
	if (ret == 0 && type == elcdRpcResult && list && list->type == cJSON_Array)
	{
		cJSON * inputItem;
		int isSelected = 0;
		uint32_t i;
		cJSON * value;
		
		for (i = 0; (inputItem = cJSON_GetArrayItem(list, i)) != NULL; i++) {
			value = cJSON_GetObjectItem(inputItem, "name");
			if (!value || value->type != cJSON_String) continue;
			
			sprintf(inputNames[g_inputCount], cJSON_GetObjectItem(inputItem, "name")->valuestring);

			interface_addMenuEntry(inputsMenu, inputNames[g_inputCount], output_setInput, inputNames[g_inputCount], icon);
			g_inputCount++;

			value = cJSON_GetObjectItem(inputItem, "selected");
			if (!value) continue;
			isSelected = value->valueint;
			if (isSelected == 1)
			{
				selected = interface_getMenuEntryCount(inputsMenu) - 1;
			}
		}
	} else {
		if (type == elcdRpcError && list && list->type == cJSON_String) {
			eprintf("%s: failed to get video inputs: %s\n", __FUNCTION__, list->valuestring);
		}
	}
	if (list) cJSON_Delete(list);

	interface_setSelectedItem(inputsMenu, selected);
}

int output_toggleInputs(void)
{
	uint32_t next = 0;

	if(output_checkInputs() == 0) {
		return -1;
	}
	if(g_inputCount == 0) {
		output_fillInputsMenu(NULL, NULL);
	}
	
	interfaceMenu_t *inputsMenu = &InputsSubMenu.baseMenu; 
	uint32_t menuEntryCount = interface_getMenuEntryCount(inputsMenu);

	interface_switchMenu(interfaceInfo.currentMenu, inputsMenu);
	interface_menuActionShowMenu(interfaceInfo.currentMenu, inputsMenu);

	next = inputsMenu->selectedItem + 1;
	eprintf ("%s: menuEntryCount = %d\n", __FUNCTION__, menuEntryCount);
	if (next > (menuEntryCount - 1)) next = 0;		// todo : check if menuEntryCount == real input count
	
	inputsMenu->selectedItem = next;

	output_setInput(inputsMenu, inputsMenu->menuEntry[next].info);

	return 0;
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


static int32_t output_checkInputs(void)
{
#if (defined STSDK)
	static int32_t inputsExist = -1;

	if(inputsExist == -1) { //check inputs once
		elcdRpcType_t	type = elcdRpcInvalid;
		cJSON			*list;

		st_rpcSync(elcmd_listvinput, NULL, &type, &list);

		inputsExist = 0;
		if((type == elcdRpcResult) &&
			list && (list->type == cJSON_Array) &&
			(cJSON_GetArraySize(list) > 0))
		{
			inputsExist = 1;
		}
	}

	return inputsExist;
#else
	return 0;
#endif
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

#ifdef ENABLE_WIFI
int output_setESSID(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;

	size_t essid_len = strlen(value);
	if (essid_len < 1 || essid_len >= sizeof(wifiInfo.essid))
		return 0;

	memcpy( wifiInfo.essid, value, essid_len+1 );
#ifdef STBPNX
	output_warnIfFailed(setParam(WLAN_CONFIG_FILE, "ESSID", value));
#endif // STBPNX
#ifdef STSDK
	if (wifiInfo.wanMode)
		output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
	else
	output_warnIfFailed(setParam(STB_HOSTAPD_CONF, "ssid", value));
#endif // STSDK
	networkInfo.changed = 1;
	output_refillMenu(pMenu);
	return 0;
}

char* output_getESSID(int index, void* pArg)
{
	return index == 0 ? wifiInfo.essid : NULL;
}

static int output_changeESSID(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ENTER_ESSID"), "\\w+", output_setESSID, output_getESSID, inputModeABC, pArg );
}

static int output_setWifiChannel(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;

	int channel = strtol( value, NULL, 10 );
	if (channel < 1 || channel > wifiInfo.channelCount)
		return 0;

	wifiInfo.currentChannel = channel;
#ifdef STBPNX
	output_warnIfFailed(setParam(WLAN_CONFIG_FILE, "CHANNEL", value));
#endif // STBPNX
#ifdef STSDK
	output_warnIfFailed(output_setHostapdChannel(atol(value)));
	
#endif // STSDK
	networkInfo.changed = 1;
	output_refillMenu(pMenu);
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

static int output_changeWifiChannel(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ENTER_WIFI_CHANNEL"), "\\d{2}", output_setWifiChannel, output_getWifiChannel, inputModeDirect, pArg );
}

static int output_toggleAuthMode(interfaceMenu_t *pMenu, void* pArg)
{
	outputWifiAuth_t maxAuth = wifiInfo.mode == wifiModeAdHoc ? wifiAuthWEP+1 : wifiAuthCount;
	return output_setAuthMode(pMenu, (void*)((wifiInfo.auth+1)%maxAuth));
}

int output_setAuthMode(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STSDK
	if (wifiInfo.key[0])
	{
		switch (wifiInfo.auth)
		{
			case wifiAuthOpen:
			case wifiAuthCount:
				break;
			case wifiAuthWEP:
				setParam(WLAN_CONFIG_FILE, "LAST_WEP", wifiInfo.key);
				break;
			case wifiAuthWPAPSK:
			case wifiAuthWPA2PSK:
				setParam(WLAN_CONFIG_FILE, "LAST_PSK", wifiInfo.key);
				break;
		}
	}
#endif
	wifiInfo.auth = (outputWifiAuth_t)pArg;

	int show_error = 0;
#ifdef STBPNX
	char *path = WLAN_CONFIG_FILE;
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

	show_error = setParam(path, "AUTH", value);
#endif
#ifdef STSDK
	char buf[MENU_ENTRY_INFO_LENGTH] = "";
	switch (wifiInfo.auth)
	{
		case wifiAuthOpen:
		case wifiAuthCount:
			break;
		case wifiAuthWEP:
			getParam(WLAN_CONFIG_FILE, "LAST_WEP", "0102030405", buf);
			break;
		case wifiAuthWPAPSK:
		case wifiAuthWPA2PSK:
			getParam(WLAN_CONFIG_FILE, "LAST_PSK", "0102030405", buf);
			break;
	}
	if (buf[0])
		strncpy(wifiInfo.key, buf, sizeof(wifiInfo.key));

	if (wifiInfo.auth > wifiAuthOpen && wifiInfo.key[0] == 0)
		strcpy(wifiInfo.key, "0102030405");

	show_error = output_writeInterfacesFile();
#endif
	networkInfo.changed = 1;
	return output_saveAndRedraw(show_error, pMenu);
}

static int output_toggleWifiEncryption(interfaceMenu_t *pMenu, void* pArg)
{
	return output_setWifiEncryption(pMenu, SET_NUMBER((wifiInfo.encryption+1)%wifiEncCount));
}

int output_setWifiEncryption(interfaceMenu_t *pMenu, void* pArg)
{
	wifiInfo.encryption = GET_NUMBER(pArg);

	int show_error = 0;
#ifdef STBPNX
	char *value = NULL;
	switch(wifiInfo.encryption)
	{
		case wifiEncAES:  value = "AES";  break;
		case wifiEncTKIP: value = "TKIP"; break;
		default: return 1;
	}
	show_error = setParam(WLAN_CONFIG_FILE, "ENCRYPTION", value);
#endif
#ifdef STSDK
	show_error = output_writeInterfacesFile();
#endif
	networkInfo.changed = 1;
	return output_saveAndRedraw(show_error, pMenu);
}

static int output_setWifiKey(interfaceMenu_t *pMenu, char *value, void* pArg)
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
	output_warnIfFailed(setParam(WLAN_CONFIG_FILE, "KEY", value));
#endif // STBPNX
#ifdef STSDK
	output_warnIfFailed(output_writeInterfacesFile());
#endif

	networkInfo.changed = 1;
	if( pAction != NULL )
	{
		return pAction(pMenu, SET_NUMBER(ifaceWireless));
	}
	output_refillMenu(pMenu);
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
int output_changeWifiKey(interfaceMenu_t *pMenu, void* pArg)
{
	if( wifiInfo.auth == wifiAuthWEP )
		return interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\d{10}", output_setWifiKey, output_getWifiKey, inputModeDirect, pArg );
	else
		return interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\w+", output_setWifiKey, output_getWifiKey, inputModeABC, pArg );
}

static int output_toggleWifiWAN(interfaceMenu_t *pMenu, void* pArg)
{
	wifiInfo.wanMode = !wifiInfo.wanMode;
	wifiInfo.enable  = 1;

	int show_error = 0;
#ifdef STBPNX
	char value[16];
	snprintf(value,sizeof(value),"%d",wifiInfo.wanMode);
	if (!wifiInfo.wanMode)
	{
		setParam(WLAN_CONFIG_FILE, "BOOTPROTO",      "static");
		setParam(WLAN_CONFIG_FILE, "MODE",           "ad-hoc");
		if (wifiInfo.auth > wifiAuthWEP)
		{
			setParam(WLAN_CONFIG_FILE, "AUTH",       "SHARED");
			setParam(WLAN_CONFIG_FILE, "ENCRYPTION", "WEP");
			setParam(WLAN_CONFIG_FILE, "KEY",        "0102030405");
		}
	}
	show_error = setParam(WLAN_CONFIG_FILE, "WAN_MODE", value);
#endif
#ifdef STSDK
	if (wifiInfo.wanMode)
	{
		wifiInfo.mode = wifiModeManaged;

		// Disable DHCP server
		networkInfo.lanMode = lanStatic;
	} else {
		wifiInfo.mode = wifiModeMaster;
		wifiInfo.auth = wifiAuthWPA2PSK;
		wifiInfo.encryption = wifiEncAES;

		// Enable DHCP server
		networkInfo.lanMode = lanDhcpServer;
	}
	wifiInfo.wanChanged = 1;

	// Re-read wpa_supplicant or hostapd settings to update essid/password settings
	output_readWirelessSettings();

	output_writeDhcpConfig();
	show_error = output_writeInterfacesFile();
#endif
	networkInfo.changed = 1;
	return output_saveAndRedraw(show_error, pMenu);
}

static int output_toggleWifiEnable(interfaceMenu_t *pMenu, void* pArg)
{
	wifiInfo.enable = !wifiInfo.enable;

	int show_error = 0;
#ifdef STBPNX
	char value[16];
	snprintf(value,sizeof(value),"%d",wifiInfo.enable);
	show_error = setParam(WLAN_CONFIG_FILE, "ENABLE_WIRELESS", value);
#endif
#ifdef STSDK
	show_error = setParam(WLAN_CONFIG_FILE, "ENABLE_WIRELESS", wifiInfo.enable ? "1" : "0");
#endif
	networkInfo.changed = 1;
	return output_saveAndRedraw(show_error, pMenu);
}

#ifdef STBPNX
static int output_toggleWifiMode(interfaceMenu_t *pMenu, void* pArg)
{
	return output_setWifiMode( pMenu, (void*)((wifiInfo.mode+1)%wifiModeCount));
}
#endif

int output_setWifiMode(interfaceMenu_t *pMenu, void* pArg)
{
	wifiInfo.mode = (outputWifiMode_t)pArg;

	int show_error = 0;
#ifdef STBPNX
	char *value = NULL;
	switch(wifiInfo.mode)
	{
		case wifiModeCount:
		case wifiModeAdHoc:   value = "ad-hoc"; break;
		case wifiModeMaster:  value = "master"; break;
		case wifiModeManaged: value = "managed"; break;
	}
	show_error = setParam(WLAN_CONFIG_FILE, "MODE", value);
#endif
#ifdef STSDK
	show_error = output_writeInterfacesFile();
#endif
	networkInfo.changed = 1;
	return output_saveAndRedraw(show_error, pMenu);
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

static int output_getIP(stb810_networkInterface_t iface, outputIPOption type, char value[MENU_ENTRY_INFO_LENGTH])
{
	int ret = 0;
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
			return 1;
	}
	sprintf(path, "/config/ifcfg-%s", helperEthDevice(iface));
	ret = getParam(path, key, "0.0.0.0", value);
#endif
#ifdef STSDK
	if (type == optionDNS)
	{
		strcpy(value, inet_ntoa(networkInfo.dns));
		return 0;
	}
	outputNfaceInfo_t *nface = NULL;
	struct in_addr *ip = NULL;
	switch (iface)
	{
		case ifaceWAN: nface = &networkInfo.wan; break;
		case ifaceLAN: nface = &networkInfo.lan; break;
#ifdef ENABLE_WIFI
		case ifaceWireless: nface = &wifiInfo.wlan; break;
#endif
		default:
			return 1;
	}
	switch (type)
	{
		case optionIP:   ip = &nface->ip;   break;
		case optionGW:   ip = &nface->gw;   break;
		case optionMask: ip = &nface->mask; break;
		default:
			return 1;
	}
	if (ip->s_addr == INADDR_NONE || ip->s_addr == INADDR_ANY)
		return 1;
	strcpy(value, inet_ntoa(*ip));
#endif
	return ret;
}

static void output_initIPfield(outputIPOption type, stb810_networkInterface_t iface)
{
	char value[MENU_ENTRY_INFO_LENGTH];
	if (output_getIP(iface, type, value) == 0)
	{
		output_parseIP(value);
	}
	else
	{
		int i;
		memset(&output_ip, 0, sizeof(output_ip));
		for (i = 0; i<16; i+=4)
			output_ip[i] = '0';
	}
}

static char* output_getIPfield(int field, void* pArg)
{
	return field < 4 ? &output_ip[field*4] : NULL;
}

static int output_setIP(interfaceMenu_t *pMenu, char *value, void* pOptionIface)
{
	in_addr_t ip;
	int i = OUTPUT_INFO_GET_INDEX(pOptionIface);
	outputIPOption type = OUTPUT_INFO_GET_TYPE(pOptionIface);

	(void)i; //hide "unused variable" warnings
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

	int show_error = 0;
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

	show_error = setParam(path, key, value);
#endif
#ifdef STSDK
	outputNfaceInfo_t *nface = NULL;
	switch( i )
	{
		case ifaceWAN: nface = &networkInfo.wan; break;
		case ifaceLAN: nface = &networkInfo.lan; break;
#ifdef ENABLE_WIFI
		case ifaceWireless: nface = &wifiInfo.wlan; break;
#endif
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
	if (i == ifaceLAN)
		output_writeDhcpConfig();
	show_error = output_writeInterfacesFile();
#endif
	networkInfo.changed = 1;
	return output_saveAndRedraw(show_error, pMenu);
}

static int output_changeIP(interfaceMenu_t *pMenu, void* pArg)
{
	output_initIPfield(optionIP,GET_NUMBER(pArg));
	return interface_getText(pMenu, _T("ENTER_DEFAULT_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionIP,GET_NUMBER(pArg)) );
}

static int output_changeGw(interfaceMenu_t *pMenu, void* pArg)
{
	output_initIPfield(optionGW,GET_NUMBER(pArg));
	return interface_getText(pMenu, _T("ENTER_GATEWAY"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionGW,GET_NUMBER(pArg)) );
}

static int output_changeDNS(interfaceMenu_t *pMenu, void* pArg)
{
	output_initIPfield(optionDNS,GET_NUMBER(pArg));
	return interface_getText(pMenu, _T("ENTER_DNS_ADDRESS"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionDNS,GET_NUMBER(pArg)) );
}

static int output_changeNetmask(interfaceMenu_t *pMenu, void* pArg)
{
	output_initIPfield(optionMask,GET_NUMBER(pArg));
	return interface_getText(pMenu, _T("ENTER_NETMASK"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionMask,GET_NUMBER(pArg)) );
}

#ifdef ENABLE_LAN
#ifdef STBPNX
char *output_getBandwidth(int field, void* pArg)
{
	if( field == 0 )
	{
		static char BWValue[MENU_ENTRY_INFO_LENGTH];
		getParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", "0", BWValue);
		return BWValue;
	} else
		return NULL;
}

char *output_getMAC(int field, void* pArg)
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

static int output_setBandwidth(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ivalue;
	char buf[32];

	if (value == NULL)
		return 1;

	if (helperFileExists(STB_CONFIG_OVERRIDE_FILE))
		return 0;

	interface_showMessageBox(_T("GATEWAY_IN_PROGRESS"), settings_renew, 0);

	ivalue = atoi(value);
	if (value[0] == 0 || ivalue <= 0)
		buf[0] = 0;
	else
		sprintf(buf, "%d", ivalue);

	// Stop network interfaces
	system("/usr/local/etc/init.d/S90dhcpd stop");
	// Update settings
	setParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", buf);
	// Start network interfaces
	system("/usr/local/etc/init.d/S90dhcpd start");

	output_refillMenu(pMenu);
	return 0;
}

static int output_changeGatewayBandwidth(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("GATEWAY_BANDWIDTH_INPUT"), "\\d*", output_setBandwidth, output_getBandwidth, inputModeDirect, pArg);
}
#endif // STBPNX
#endif // ENABLE_LAN

#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
#ifdef STBPNX
static int output_confirmGatewayMode(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	lanMode_t mode = GET_NUMBER(pArg);

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
#endif
		if (mode >= lanModeCount )
		{
			return 0;
		}
		interface_showMessageBox(_T("GATEWAY_IN_PROGRESS"), settings_renew, 0);
#ifdef STBPNX
		char *str = "";
		switch (mode) {
			case lanBridge:     str = "BRIDGE"; break;
			case lanStatic:     str = "NAT"; break;
			case lanDhcpServer: str = "FULL"; break;
			default:            str = "OFF"; break;
		}
#endif
		networkInfo.lanMode = mode;

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
//-----------------------------------------------------------------//
#ifdef STSDK
#ifdef ENABLE_PPP
		system("/etc/init.d/S65ppp stop");
#endif
		system("/etc/init.d/S40network stop");
		output_writeInterfacesFile();
		output_writeDhcpConfig();
		system("ifcfg config > " NETWORK_INTERFACES_FILE);
		system("/etc/init.d/S40network start");
#ifdef ENABLE_PPP
		system("/etc/init.d/S65ppp start");
#endif
#endif

		networkInfo.changed = 0;
		interface_hideMessageBox();
		output_redrawMenu(pMenu);

		return 0;
	}
	return 1;
}

static int output_toggleGatewayMode(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showConfirmationBox(_T("GATEWAY_MODE_CONFIRM"), thumbnail_question, output_confirmGatewayMode, pArg);
	return 1;
}

int output_enterGatewayMenu(interfaceMenu_t *gatewayMenu, void* ignored)
{
	interface_clearMenuEntries(gatewayMenu);
	for (lanMode_t mode = 0; mode < lanModeCount; mode++)
		interface_addMenuEntry(gatewayMenu, output_getLanModeName(mode),
			mode == networkInfo.lanMode ? NULL : output_toggleGatewayMode, (void*)mode,
			mode == networkInfo.lanMode ? radiobtn_filled : radiobtn_empty);
	return 0;
}
#endif // STBPNX
#endif // ENABLE_LAN || ENABLE_WIFI

static int output_applyNetworkSettings(interfaceMenu_t *pMenu, void* pArg)
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
//-----------------------------------------------------------------//
#ifdef STSDK
#ifdef ENABLE_WIFI
	if (ifaceWireless == GET_NUMBER(pArg) &&
	    networkInfo.lanMode != lanBridge &&
	    wifiInfo.wanChanged == 0)
	{
#ifdef ENABLE_PPP
		if (wifiInfo.wanMode)
			system("/etc/init.d/S65ppp stop");
#endif
		system("ifdown wlan0");
		output_writeDhcpConfig();
		system("ifcfg config > " NETWORK_INTERFACES_FILE);
		system("ifup wlan0");
#ifdef ENABLE_PPP
		if (wifiInfo.wanMode)
			system("/etc/init.d/S65ppp start");
#endif
	} else
#endif
	{
#ifdef ENABLE_PPP
	system("/etc/init.d/S65ppp stop");
#endif
	system("/etc/init.d/S40network stop");
	output_writeDhcpConfig();
	system("ifcfg config > " NETWORK_INTERFACES_FILE);
	sleep(1);
	system("/etc/init.d/S40network start");
#ifdef ENABLE_PPP
	system("/etc/init.d/S65ppp start");
#endif
#ifdef ENABLE_WIFI
	wifiInfo.wanChanged = 0;
#endif
	}
#endif // STSDK

	networkInfo.changed = 0;
	output_refillMenu(pMenu);
	interface_hideMessageBox();

	return 0;
}

static int output_setProxyAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
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
		ret = saveProxySettings();
#endif
		ret |= setenv("http_proxy", ptr2, 1);
	}
#ifdef STSDK
	system( "killall -SIGHUP '" ELCD_BASENAME "'" );
#endif
	return output_saveAndRedraw(ret, pMenu);
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
#ifdef ENABLE_TELETES
		case optionTeletesPlaylist:
			return appControlInfo.rtpMenuInfo.teletesPlaylist;
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

static int output_setURL(interfaceMenu_t *pMenu, char *value, void* urlOption)
{
	outputUrlOption option = GET_NUMBER(urlOption);

	if ( value == NULL )
	{
		if (option == optionRtpPlaylist && appControlInfo.rtpMenuInfo.playlist[0] == 0)
		{
			appControlInfo.rtpMenuInfo.usePlaylistURL = 0;
			output_refillMenu(pMenu);
		}
		return 1;
	}

	char *dest = output_getOption(option);
	if ( dest == NULL )
		return 1;

	if ( value[0] == 0 )
	{
		dest[0] = 0;
		if (option == optionRtpPlaylist)
		{
			appControlInfo.rtpMenuInfo.usePlaylistURL = 0;
		}
	} else
	{
		if ( strlen( value ) < 8 )
		{
			interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_warning, 3000);
			return 1;
		}
		if ( strncasecmp( value, "http://", 7 ) == 0 || strncasecmp( value, "https://", 8 ) == 0 )
		{
			strcpy(dest, value);
		} else
		{
			sprintf(dest, "http://%s",value);
		}
	}
	switch (option)
	{
#ifdef ENABLE_IPTV
		case optionRtpEpg:
			rtp_cleanupEPG();
			break;
		case optionRtpPlaylist:
			rtp_cleanupPlaylist(screenMain);
			break;
#endif
#ifdef ENABLE_TELETES
		case optionTeletesPlaylist:
			teletes_cleanupMenu();
			break;
#endif
		default:;
	}
	output_refillMenu(pMenu);
	return output_warnIfFailed(saveAppSettings());
}

static int output_setProxyLogin(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret = 0;

	if( value == NULL )
		return 1;

	strncpy(appControlInfo.networkInfo.login, value, sizeof(appControlInfo.networkInfo.login));
	appControlInfo.networkInfo.login[sizeof(appControlInfo.networkInfo.login)-1]=0;
	setenv("http_proxy_login", value, 1);
#ifdef STBPNX
	ret = setParam(BROWSER_CONFIG_FILE, "HTTPProxyLogin", value);
#endif
#ifdef STSDK
	ret = saveProxySettings();
#endif
	return output_saveAndRedraw(ret, pMenu);
}

static int output_setProxyPassword(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int ret = 0;

	if( value == NULL )
		return 1;

	strncpy(appControlInfo.networkInfo.passwd, value, sizeof(appControlInfo.networkInfo.passwd));
	appControlInfo.networkInfo.passwd[sizeof(appControlInfo.networkInfo.passwd)-1]=0;
	setenv("http_proxy_passwd", value, 1);
#ifdef STBPNX
	ret = setParam(BROWSER_CONFIG_FILE, "HTTPProxyPasswd", value);
#endif
#ifdef STSDK
	ret = saveProxySettings();
#endif
	return output_saveAndRedraw(ret, pMenu);
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

static int output_statusReport(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	system(CREATE_REPORT_FILE);
	interface_showMessageBox(_T("STATUS_REPORT_COMPLETE"), thumbnail_configure, 3000);

	return 0;
}

#ifdef STSDK
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
#endif

static int output_resetSettings(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showConfirmationBox(_T("RESET_SETTINGS_CONFIRM"), thumbnail_question, output_confirmReset, pArg);
	return 1; // when called from askPassword, should return non-0 to leave getText message box opened
}

static char *output_getURL(int index, void* pArg)
{
	return index == 0 ? output_getOption((outputUrlOption)pArg) : NULL;
}

static int output_changeURL(interfaceMenu_t *pMenu, void* urlOption)
{
	char *str = "";
	switch (GET_NUMBER(urlOption))
	{
		case optionRtpEpg:      str = _T("ENTER_IPTV_EPG_ADDRESS");  break;
#ifdef ENABLE_TELETES
		case optionTeletesPlaylist: // fall through
#endif
		case optionRtpPlaylist: str = _T("ENTER_IPTV_LIST_ADDRESS"); break;
		case optionVodPlaylist: str = _T("ENTER_VOD_LIST_ADDRESS");  break;
	}
	return interface_getText(pMenu, str, "\\w+", output_setURL, output_getURL, inputModeABC, urlOption);
}

#ifdef ENABLE_IPTV
static int output_changeIPTVPlaylist(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.rtpMenuInfo.usePlaylistURL = (appControlInfo.rtpMenuInfo.usePlaylistURL+1)%(appControlInfo.rtpMenuInfo.hasInternalPlaylist?3:2);
	if( appControlInfo.rtpMenuInfo.usePlaylistURL && appControlInfo.rtpMenuInfo.playlist[0] == 0 )
	{
		return output_changeURL( pMenu, SET_NUMBER(optionRtpPlaylist) );
	} else
	{
		output_saveAndRedraw(saveAppSettings(), pMenu);
	}
	return 0;
}
#endif

#ifdef ENABLE_VOD
static int output_toggleVODPlaylist(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.rtspInfo.usePlaylistURL = !appControlInfo.rtspInfo.usePlaylistURL;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}
#endif

static int output_changeProxyAddress(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ENTER_PROXY_ADDR"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}:\\d+", output_setProxyAddress, NULL, inputModeDirect, pArg);
}

static int output_changeProxyLogin(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ENTER_PROXY_LOGIN"), "\\w+", output_setProxyLogin, NULL, inputModeABC, pArg);
}

static int output_changeProxyPassword(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ENTER_PROXY_PASSWD"), "\\w+", output_setProxyPassword, NULL, inputModeABC, pArg);
}

#ifdef ENABLE_BROWSER
char *output_getMiddlewareUrl(int field, void* pArg)
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

static int output_setMiddlewareUrl(interfaceMenu_t *pMenu, char *value, void* pArg)
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
#endif
	return output_saveAndRedraw(ret, pMenu);
}

static int output_changeMiddlewareUrl(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ENTER_MW_ADDR"), "\\w+", output_setMiddlewareUrl, output_getMiddlewareUrl, inputModeABC, pArg);
}

static int output_toggleMWAutoLoading(interfaceMenu_t *pMenu, void* pArg)
{
	int ret = 0;
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
	ret = setParam(BROWSER_CONFIG_FILE, "AutoLoadingMW",str);
#endif
	return output_saveAndRedraw(ret, pMenu);
}
#endif // ENABLE_BROWSER

static int output_toggleMode(interfaceMenu_t *pMenu, void* pArg)
{
	int i = GET_NUMBER(pArg);
	int ret = 0;

	(void)i; //hide "unused variable" warnings
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

	ret = output_setIP(pMenu, value, OUTPUT_INFO_SET(optionMode,i));
#endif
#ifdef STSDK
	switch (i)
	{
		case ifaceWAN:
			networkInfo.wanDhcp = !networkInfo.wanDhcp;
			break;
		case ifaceLAN:
			networkInfo.lanMode = (networkInfo.lanMode+1)%lanModeCount;
			output_writeDhcpConfig();
			break;
#ifdef ENABLE_WIFI
		case ifaceWireless:
			wifiInfo.dhcp = !wifiInfo.dhcp;
			break;
#endif
		default:
			return 0;
	}

	ret = output_writeInterfacesFile();
#endif
	networkInfo.changed = 1;
	return output_saveAndRedraw(ret, pMenu);
}

#ifdef ENABLE_VOD
static int output_setVODIP(interfaceMenu_t *pMenu, char *value, void* pArg)
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

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_setVODINFOIP(interfaceMenu_t *pMenu, char *value, void* pArg)
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

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_changeVODIP(interfaceMenu_t *pMenu, void* pArg)
{
	output_parseIP( appControlInfo.rtspInfo.streamIP );
	return interface_getText(pMenu, _T("ENTER_VOD_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setVODIP, output_getIPfield, inputModeDirect, NULL);
}

static int output_changeVODINFOIP(interfaceMenu_t *pMenu, void* pArg)
{
	output_parseIP( appControlInfo.rtspInfo.streamInfoIP );
	return interface_getText(pMenu, _T("ENTER_VOD_INFO_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setVODINFOIP, output_getIPfield, inputModeDirect, NULL);
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
	return 1;
}

static int output_clearOffairSettings(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showConfirmationBox(_T("DVB_CLEAR_CHANNELS_CONFIRM"), thumbnail_question, output_confirmClearOffair, pArg);
	return 1;
}

static int output_confirmClearDvb(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		offair_clearServiceList(1);
		dvbChannel_terminate();
		output_redrawMenu(pMenu);
		return 0;
	}
	return 1;
}

static int output_confirmClearOffair(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int32_t ret = 1;
	switch(cmd->command) {
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
			ret = 0;
			break;
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			dvbChannel_sort(serviceSortNone);
			offair_fillDVBTMenu();
			ret = 0;
			break;
		default:
			break;
	}

	return ret;
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
	offair_fillDVBTMenu();
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

	str = _T("DVB_CLEAR_CHANNELS");
	interface_addMenuEntry(dvbMenu, str, output_clearOffairSettings, screenMain, thumbnail_scan);

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
#endif /* ENABLE_DVB */

#ifdef ENABLE_VERIMATRIX
static int output_toggleVMEnable(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.useVerimatrix = !appControlInfo.useVerimatrix;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_setVMAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;

	value = inet_addr_prepare(value);
	if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
		return -1;
	}

	int ret = setParam(VERIMATRIX_INI_FILE, "SERVERADDRESS", value);
	if (ret == 0)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%s/%d", value, VERIMATRIX_VKS_PORT);
		ret = setParam(VERIMATRIX_INI_FILE, "PREFERRED_VKS", buf);
	}
	if (ret == 0)
		output_refillMenu(pMenu);
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
	char info_url[MAX_CONFIG_PATH];
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

static int output_setVMCompany(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;

	int ret = setParam(VERIMATRIX_INI_FILE, "COMPANY", value);
	if (ret == 0)
		output_refillMenu(pMenu);
	return ret;
}

static int output_changeVMAddress(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("VERIMATRIX_ADDRESS"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setVMAddress, NULL, inputModeDirect, pArg);
}

static int output_changeVMCompany(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("VERIMATRIX_COMPANY"), "\\w+", output_setVMCompany, NULL, inputModeABC, pArg);
}
#endif // #ifdef ENABLE_VERIMATRIX

#ifdef ENABLE_SECUREMEDIA
enum
{
	smEsamHost = 1,
	smRandomHost = 2,
};
static int output_toggleSMEnable(interfaceMenu_t *pMenu, void* pArg)
{
	appControlInfo.useSecureMedia = !appControlInfo.useSecureMedia;
	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_setSMAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int type = GET_NUMBER(pArg);
	int ret;

	if  (value == NULL)
		return 1;

	value = inet_addr_prepare(value);
	if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
		return -1;
	}
	ret = setParam(SECUREMEDIA_CONFIG_FILE, type == smEsamHost ? "SECUREMEDIA_ESAM_HOST" : "SECUREMEDIA_RANDOM_HOST", value);
	if (ret == 0)
	{
		/* Restart smdaemon */
		system("killall smdaemon");
		output_redrawMenu(pMenu);
	}
	return ret;
}

static int output_changeSMAddress(interfaceMenu_t *pMenu, void* pArg)
{
	int type = GET_NUMBER(pArg);
	return interface_getText(pMenu, _T(type == smEsamHost ? "SECUREMEDIA_ESAM_HOST" : "SECUREMEDIA_RANDOM_HOST"),
		"\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setSMAddress, NULL, inputModeDirect, pArg);
}
#endif // #ifdef ENABLE_SECUREMEDIA

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

static void show_ip(stb810_networkInterface_t i, const char *iface_name, char *info_text)
{
	char temp[256];
	struct ifreq ifr;
	int fd;

#ifdef ENABLE_PPP
	if (i != ifacePPP)
#endif
	{
		snprintf(temp, sizeof(temp), "/sys/class/net/%s/address", helperEthDevice(i));
		fd = open(temp, O_RDONLY);
		if (fd > 0) {
			ssize_t len = read(fd, temp, sizeof(temp)-1)-1;
			close(fd);
			if (len<0) len = 0;
			temp[len]=0;

			sprintf(info_text, "%s %s: ", iface_name, _T("MAC_ADDRESS"));
			strcat(info_text, temp);
			strcat(info_text, "\n");
		} else {
			sprintf(temp, "%s %s: %s\n", iface_name, _T("MAC_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
			strcat(info_text, temp);
		}
	}
#ifdef STBPNX
	if (i == ifaceLAN && output_isBridge()) {
		snprintf(temp, sizeof(temp), "%s: %s", iface_name, _T("GATEWAY_BRIDGE"));
		strcat(info_text, temp);
		return;
	}
#endif
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		snprintf(temp, sizeof(temp), "%s %s: %s\n%s %s: %s\n",
			iface_name, _T("IP_ADDRESS"), _T("NOT_AVAILABLE_SHORT"),
			iface_name, _T("NETMASK"),    _T("NOT_AVAILABLE_SHORT"));
		strcat(info_text, temp);
		return;
	}

	/* I want to get an IPv4 IP address */
	ifr.ifr_addr.sa_family = AF_INET;
	/* I want IP address attached to "eth0" */
	strncpy(ifr.ifr_name, helperEthDevice(i), IFNAMSIZ-1);
	if (ioctl(fd, SIOCGIFADDR, &ifr) < 0)
		snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("IP_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
	else
		snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("IP_ADDRESS"), inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
	strcat(info_text, temp);

#ifdef ENABLE_PPP
	if (i != ifacePPP) // PPP netmask is 255.255.255.255
#endif
	{
		if (ioctl(fd, SIOCGIFNETMASK, &ifr) < 0)
			snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("NETMASK"), _T("NOT_AVAILABLE_SHORT"));
		else
			snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("NETMASK"), inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
		strcat(info_text, temp);
	}

	close(fd);
}

#ifdef ENABLE_WIFI
static void show_wireless(const char *iface_name, char *info_text)
{
	char temp[MENU_ENTRY_INFO_LENGTH];

	sprintf(temp, "%s %s: %s\n", iface_name, _T("ESSID"), wifiInfo.essid);
	strcat(info_text, temp);
	sprintf(temp, "%s %s: %s\n", iface_name, _T("MODE"), wireless_mode_print( wifiInfo.mode ));
	strcat(info_text, temp);
	if (!wifiInfo.wanMode)
	{
		sprintf(temp, "%s %s: %d\n", iface_name, _T("CHANNEL_NUMBER"), wifiInfo.currentChannel );
		strcat(info_text, temp);
		sprintf(temp, "%s %s: %s\n", iface_name, _T("AUTHENTICATION"), wireless_auth_print( wifiInfo.auth ));
		strcat(info_text, temp);
		sprintf(temp, "%s %s: %s\n", iface_name, _T("ENCRYPTION"), wireless_encr_print( wifiInfo.encryption ));
		strcat(info_text, temp);
	}
}
#endif

int show_info(interfaceMenu_t* pMenu, void* pArg)
{
	char buf[256];
	char temp[256];
	char info_text[4096];
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

	show_ip(ifaceWAN, "Ethernet", &info_text[strlen(info_text)]);
	strcat(info_text, "\n");
#ifdef ENABLE_LAN
	if (helperCheckDirectoryExsists("/sys/class/net/eth1")) {
		show_ip(ifaceLAN, "Ethernet 2", &info_text[strlen(info_text)]);
		strcat(info_text, "\n");
	}
#endif
#ifdef ENABLE_WIFI
	if (helperCheckDirectoryExsists("/sys/class/net/wlan0")) {
		char *iface_name = _T("WIRELESS");
		show_wireless(iface_name, &info_text[strlen(info_text)]);
		show_ip(ifaceWireless, iface_name, &info_text[strlen(info_text)]);
		strcat(info_text, "\n");
	}
#endif
#ifdef ENABLE_PPP
	sprintf(temp, "/sys/class/net/%s", helperEthDevice(ifacePPP));
	if (helperCheckDirectoryExsists(temp)) {
		show_ip(ifacePPP, "PPP", &info_text[strlen(info_text)]);
		strcat(info_text, "\n");
	}
#endif
	if (helperParseLine(INFO_TEMP_FILE, "route -n | grep -e \"0\\.0\\.0\\.0 .* 0\\.0\\.0\\.0 *UG .*\"", "0.0.0.0", buf, ' '))
	{
		sprintf(temp, "%s: ", _T("GATEWAY"));
		strcat(info_text, temp);
		strcat(info_text, buf);
		strcat(info_text, "\n");
	}
	/* else {
		sprintf(temp, "%s %s: %s\n", iface_name, _T("GATEWAY"), _T("NOT_AVAILABLE_SHORT"));
		strcat(info_text, temp);
	} */
	i = -1;
	fd = open( "/etc/resolv.conf", O_RDONLY );
	if ( fd > 0 ) {
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
	if ( i < 0 ) {
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
	return interface_menuActionShowMenu(pMenu, &NetworkSubMenu);
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

static int analogtv_setServiceFileName(interfaceMenu_t *pMenu, char* pStr, void* pArg)
{
	(void)pArg;
	if (pStr == NULL) {
		return 0;
	}
	sprintf(channel_names_file_full, "/tmp/%s.txt", pStr);
	strncpy(appControlInfo.tvInfo.channelNamesFile, pStr, sizeof(appControlInfo.tvInfo.channelNamesFile));
	analogtv_updateFoundServiceFile();

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int analogtv_changeServiceFileName(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, _T("ANALOGTV_SET_CHANNEL_FILE_NAME"), "\\w+", analogtv_setServiceFileName, NULL, inputModeABC, pArg);
}

static int analogtv_sendToServer(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox(_T("ANALOGTV_SENDING_CHFILE"), thumbnail_info, 0);
	interface_hideMessageBox();
	return 0;
}

static int analogtv_downloadFromServer(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox(_T("ANALOGTV_DOWNLOADING_CHFILE"), thumbnail_info, 0);
	interface_hideMessageBox();

	return 0;
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
	if (services_edit_able)
	{
		sprintf(buf, "%s: %s", _T("ANALOGTV_SET_CHANNEL_FILE_NAME"), appControlInfo.tvInfo.channelNamesFile);
		interface_addMenuEntry(tvMenu,buf , analogtv_changeServiceFileName, NULL, thumbnail_configure);
		
		interface_addMenuEntry(tvMenu, _T("ANALOGTV_DOWNLOAD_CHFILE"), analogtv_downloadFromServer, NULL, thumbnail_configure);
		
		interface_addMenuEntry(tvMenu, _T("ANALOGTV_SEND_CHFILE"), analogtv_sendToServer, NULL, thumbnail_configure);//garb_sendToServer
	}

	sprintf(buf, "%s (%d)", _T("ANALOGTV_CLEAR"), analogtv_getChannelCount(0)); //analogtv_service_count
	interface_addMenuEntry(tvMenu, buf, analogtv_clearServiceList, (void *)1, thumbnail_scan);
	
	return 0;
}
#endif //#ifdef ENABLE_ANALOGTV

#ifdef STSDK
int output_enterInputsMenu(interfaceMenu_t *pMenu, void* notused)
{
	output_fillInputsMenu(pMenu, NULL);
	return 0;
};
#endif

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

int output_enterNetworkMenu(interfaceMenu_t *networkMenu, void* notused)
{
	char  buf[MENU_ENTRY_INFO_LENGTH];
	char *str;

	// assert (networkMenu == _M &NetworkSubMenu);
	interface_clearMenuEntries(networkMenu);

	// Read required network settings
#ifdef STBPNX
#ifdef ENABLE_LAN
	if (!helperFileExists(STB_CONFIG_OVERRIDE_FILE))
	{
		char temp[MENU_ENTRY_INFO_LENGTH];
		getParam(STB_CONFIG_FILE, "CONFIG_GATEWAY_MODE", "OFF", temp);
		if (strcmp("FULL", temp) == 0)
			networkInfo.lanMode = lanDhcpServer;
		else if (strcmp("BRIDGE", temp) == 0)
			networkInfo.lanMode = lanBridge;
		else
			networkInfo.lanMode = lanStatic;
	}
#endif // ENABLE_LAN
#ifdef ENABLE_WIFI
	{
		char temp[MENU_ENTRY_INFO_LENGTH];
		getParam(WLAN_CONFIG_FILE, "WAN_MODE", "0", temp);
		wifiInfo.wanMode = atoi(temp);

		getParam(WLAN_CONFIG_FILE, "ENABLE_WIRELESS", "1", temp);
		wifiInfo.enable = atoi(temp);
	}
#endif // ENABLE_WIFI
#endif // STBPNX

	// Get current LAN mode for future use
	char *lanMode = (char*)output_getLanModeName(networkInfo.lanMode);
	(void)lanMode; //hide "unused variable" warnings

	// ------------------ Display current Internet connection ------------------
#ifdef ENABLE_WIFI
	if (wifiInfo.wanMode)
		str = _T("WIRELESS");
	else
#endif
	str = "Ethernet";
	sprintf(buf, "%s: %s", _T("INTERNET_CONNECTION"), str);
#ifdef ENABLE_WIFI
	int wifiExists = helperCheckDirectoryExsists("/sys/class/net/wlan0");
	if (wifiExists || wifiInfo.wanMode)
		interface_addMenuEntry(networkMenu, buf, output_toggleWifiWAN, NULL, thumbnail_info);
	else
#endif
	interface_addMenuEntryDisabled(networkMenu, buf, thumbnail_info);

	// --------------------------- Ethernet 1 ----------------------------------
	if (helperCheckDirectoryExsists("/sys/class/net/eth0"))
	{
#ifdef ENABLE_WIFI
		if (wifiInfo.wanMode)
#ifdef STBPNX
			str = _T("NOT_AVAILABLE");
#else
			str = lanMode;
#endif
		else
#endif
		str = _T("INTERNET_CONNECTION");
		snprintf(buf, sizeof(buf), "Ethernet - %s", str);
		interface_addMenuEntry(networkMenu, buf, interface_menuActionShowMenu, &WANSubMenu, settings_network);
	} else
	{
		interface_addMenuEntryDisabled(networkMenu, "Ethernet", settings_network);
	}

	// --------------------------- Ethernet 2 ----------------------------------
#ifdef ENABLE_LAN
	if (helperCheckDirectoryExsists("/sys/class/net/eth1"))
	{
		snprintf(buf, sizeof(buf), "Ethernet 2 - %s", lanMode);
		interface_addMenuEntry(networkMenu, buf, interface_menuActionShowMenu, &LANSubMenu, settings_network);
	}
#endif // ENABLE_LAN

	// ----------------------------- Wi-Fi -------------------------------------
#ifdef ENABLE_WIFI
#if !(defined STB225)
	if (wifiExists)
#endif
	{
		if (wifiInfo.enable)
			str = wifiInfo.wanMode ? _T("INTERNET_CONNECTION") : lanMode;
		else
			str = _T("OFF");
		snprintf(buf, sizeof(buf), "%s - %s", _T("WIRELESS"), str);
		interface_addMenuEntry(networkMenu, buf, interface_menuActionShowMenu, &WifiSubMenu, settings_network);
	}
#if !(defined STB225)
	else
	{
		snprintf(buf, sizeof(buf), "%s - %s", _T("WIRELESS"), _T("NOT_AVAILABLE"));
		interface_addMenuEntryDisabled(networkMenu, buf, settings_network);
	}
#endif
#endif // ENABLE_WIFI

	// ----------------------------- PPP ---------------------------------------
#ifdef ENABLE_PPP
	interface_addMenuEntry(networkMenu, _T("PPP"), interface_menuActionShowMenu, &PPPSubMenu, settings_network);
#endif
	// ---------------------------- Mobile -------------------------------------
#ifdef ENABLE_MOBILE
	if (helperFileExists("/dev/ttyUSB0"))
	interface_addMenuEntry(networkMenu, _T("MOBILE"), interface_menuActionShowMenu, &MobileMenu, settings_network);
#endif
	// -------------------------------------------------------------------------

#ifdef ENABLE_IPTV
	interface_addMenuEntry(networkMenu, _T("TV_CHANNELS"), interface_menuActionShowMenu, (void*)&IPTVSubMenu, thumbnail_multicast);
#endif
#ifdef ENABLE_VOD
	interface_addMenuEntry(networkMenu, _T("MOVIES"), interface_menuActionShowMenu, (void*)&VODSubMenu, thumbnail_vod);
#endif

#ifdef STBPNX
	if (helperFileExists(BROWSER_CONFIG_FILE))
#endif
	interface_addMenuEntry(networkMenu, _T("INTERNET_BROWSING"), interface_menuActionShowMenu, (void*)&WebSubMenu, thumbnail_internet);
	interface_addMenuEntry(networkMenu, _T("PING"), output_pingMenu, NULL, settings_interface);

#ifdef ENABLE_VERIMATRIX
	if (helperFileExists(VERIMATRIX_INI_FILE))
	{
		sprintf(buf,"%s: %s", _T("VERIMATRIX_ENABLE"), appControlInfo.useVerimatrix == 0 ? _T("OFF") : _T("ON"));
		interface_addMenuEntry(networkMenu, buf, output_toggleVMEnable, NULL, thumbnail_configure);
		if (appControlInfo.useVerimatrix != 0)
		{
			char temp[MENU_ENTRY_INFO_LENGTH];
			getParam(VERIMATRIX_INI_FILE, "COMPANY", "", temp);
			if (temp[0] != 0)
			{
				sprintf(buf, "%s: %s", _T("VERIMATRIX_COMPANY"), temp);
				interface_addMenuEntry(networkMenu, buf, output_changeVMCompany, NULL, thumbnail_enterurl);
			}
			getParam(VERIMATRIX_INI_FILE, "SERVERADDRESS", "", temp);
			if (temp[0] != 0)
			{
				sprintf(buf, "%s: %s", _T("VERIMATRIX_ADDRESS"), temp);
				interface_addMenuEntry(networkMenu, buf, output_changeVMAddress, NULL, thumbnail_enterurl);
			}
			interface_addMenuEntry(networkMenu, _T("VERIMATRIX_GET_ROOTCERT"), output_getVMRootCert, NULL, thumbnail_turnaround);
		}
	}
#endif
#ifdef ENABLE_SECUREMEDIA
	if (helperFileExists(SECUREMEDIA_CONFIG_FILE))
	{
		sprintf(buf,"%s: %s", _T("SECUREMEDIA_ENABLE"), appControlInfo.useSecureMedia == 0 ? _T("OFF") : _T("ON"));
		interface_addMenuEntry(networkMenu, buf, output_toggleSMEnable, NULL, thumbnail_configure);
		if (appControlInfo.useSecureMedia != 0)
		{
			char temp[MENU_ENTRY_INFO_LENGTH];
			getParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_ESAM_HOST", "", temp);
			if (temp[0] != 0)
			{
				sprintf(buf, "%s: %s", _T("SECUREMEDIA_ESAM_HOST"), temp);
				interface_addMenuEntry(networkMenu, buf, output_changeSMAddress, SET_NUMBER(smEsamHost), thumbnail_enterurl);
			}
			getParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_RANDOM_HOST", "", temp);
			if (temp[0] != 0)
			{
				sprintf(buf, "%s: %s", _T("SECUREMEDIA_RANDOM_HOST"), temp);
				interface_addMenuEntry(networkMenu, buf, output_changeSMAddress, SET_NUMBER(smRandomHost), thumbnail_enterurl);
			}
		}
	}
#endif
//HTTPProxyServer=http://192.168.1.2:3128
//http://192.168.1.57:8080/media/stb/home.html
#ifndef HIDE_EXTRA_FUNCTIONS
	str = _T("PROCESS_PCR");
	interface_addMenuEntry(networkMenu, str, output_togglePCR, NULL, appControlInfo.bProcessPCR ? thumbnail_yes : thumbnail_no);
	str = _T( appControlInfo.bUseBufferModel ? "BUFFER_TRACKING" : "PCR_TRACKING");
	interface_addMenuEntry(networkMenu, str, output_toggleBufferTracking, NULL, thumbnail_configure);
	str = _T("RENDERER_SYNC");
	interface_addMenuEntry(networkMenu, str, output_toggleRSync, NULL, appControlInfo.bRendererDisableSync ? thumbnail_no : thumbnail_yes);
#endif

	return 0;
}

int output_leaveNetworkMenu(interfaceMenu_t *pMenu, void* pArg)
{
	if (networkInfo.changed)
	{
		interface_showConfirmationBox(_T("CONFIRM_NETWORK_SETTINGS"), thumbnail_question, output_confirmNetworkSettings, NULL);
		return 1;
	}
	return 0;
}

int output_confirmNetworkSettings(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else
	if ((cmd->command == interfaceCommandGreen) ||
	    (cmd->command == interfaceCommandEnter) ||
	    (cmd->command == interfaceCommandOk))
	{
		output_applyNetworkSettings(pMenu, SET_NUMBER(ifaceWAN));
	}
	return 0;
}

int output_enterWANMenu(interfaceMenu_t *wanMenu, void* pArg)
{
	// assert (wanMenu == &WANSubMenu);
	int wan = 1;
#if (defined ENABLE_WIFI) && (!defined STBPNX)
	wan = !wifiInfo.wanMode;
#endif
	output_setIfaceMenuName(wanMenu, "Ethernet", wan, networkInfo.lanMode);
	interface_clearMenuEntries(wanMenu);

#ifdef ENABLE_WIFI
	if (wifiInfo.wanMode) {
#ifdef STBPNX
		interface_addMenuEntryDisabled(wanMenu, _T("NOT_AVAILABLE"), thumbnail_info);
#else
		output_fillLANMenu(wanMenu, SET_NUMBER(ifaceLAN));
#endif
	} else
#endif // ENABLE_WIFI
	output_fillWANMenu(wanMenu, SET_NUMBER(ifaceWAN));

	interface_addMenuEntry(wanMenu, _T("APPLY_NETWORK_SETTINGS"), output_applyNetworkSettings, SET_NUMBER(ifaceWAN), settings_renew);

#ifndef HIDE_EXTRA_FUNCTIONS
	char temp[MENU_ENTRY_INFO_LENGTH];
	int offset = sprintf(temp, "%s: ",  _T("MAC_ADDRESS"));
	int mac_fd = open("/sys/class/net/eth0/address", O_RDONLY);
	if (mac_fd > 0)
	{
		int len = read(mac_fd, temp+offset, sizeof(temp)-offset);
		if (len <= 0) len = 1;
		temp[offset+len-1] = 0;
		close(mac_fd);
	} else
		strcpy(temp+offset, _T("NOT_AVAILABLE_SHORT"));
	interface_addMenuEntryDisabled(wanMenu, temp, thumbnail_configure);
#endif
	return 0;
}

int output_fillWANMenu(interfaceMenu_t *wanMenu, void* pIface)
{
	char  buf[MENU_ENTRY_INFO_LENGTH];
	char temp[MENU_ENTRY_INFO_LENGTH];
	int dhcp = 0;
	stb810_networkInterface_t iface = GET_NUMBER(pIface);

	dhcp = output_isIfaceDhcp(iface);
	strcpy(temp, _T( dhcp ? "ADDR_MODE_DHCP" : "ADDR_MODE_STATIC" ));
	sprintf(buf, "%s: %s", _T("ADDR_MODE"), temp);
	interface_addMenuEntry(wanMenu, buf, output_toggleMode, SET_NUMBER(iface), thumbnail_configure);

	char *not_available = _T("NOT_AVAILABLE_SHORT");
	if (dhcp == 0)
	{
		int ret;

		ret = output_getIP(iface, optionIP, temp);
		sprintf(buf, "%s: %s",  _T("IP_ADDRESS"), ret ? not_available : temp);
		interface_addMenuEntry(wanMenu, buf, output_changeIP, SET_NUMBER(iface), thumbnail_configure);

		ret = output_getIP(iface, optionMask, temp);
		sprintf(buf, "%s: %s", _T("NETMASK"),     ret ? not_available : temp);
		interface_addMenuEntry(wanMenu, buf, output_changeNetmask, SET_NUMBER(iface), thumbnail_configure);

		ret = output_getIP(iface, optionGW, temp);
		sprintf(buf, "%s: %s", _T("GATEWAY"),     ret ? not_available : temp);
		interface_addMenuEntry(wanMenu, buf, output_changeGw, SET_NUMBER(iface), thumbnail_configure);

		ret = output_getIP(iface, optionDNS, temp);
		sprintf(buf, "%s: %s", _T("DNS_SERVER"),  ret ? not_available : temp);
		interface_addMenuEntry(wanMenu, buf, output_changeDNS, SET_NUMBER(iface), thumbnail_configure);
	} else
	{
		char path[MAX_CONFIG_PATH];
		sprintf(path, "ifconfig %s | grep \"inet addr\"", helperEthDevice(iface));
		if (!helperParseLine(INFO_TEMP_FILE, path, "inet addr:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			strcpy(temp, not_available);
		sprintf(buf, "%s: %s", _T("IP_ADDRESS"), temp);
		interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);

		sprintf(path, "ifconfig %s | grep \"Mask:\"", helperEthDevice(iface));
		if (!helperParseLine(INFO_TEMP_FILE, path, "Mask:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			strcpy(temp, not_available);
		sprintf(buf, "%s: %s", _T("NETMASK"), temp);
		interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);

		sprintf(path, "route -n | grep -e \"0\\.0\\.0\\.0 .* 0\\.0\\.0\\.0 *UG .* %s\"", helperEthDevice(iface));
		if (!helperParseLine(INFO_TEMP_FILE, path, "0.0.0.0", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
			strcpy(temp, not_available);
		sprintf(buf, "%s: %s", _T("GATEWAY"), temp);
		interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);

		int dns_found = 0;
		int dns_fd = open( "/etc/resolv.conf", O_RDONLY );
		if (dns_fd > 0)
		{
			char *ptr;
			while( helperReadLine( dns_fd, temp ) == 0 && temp[0] )
			{
				if( (ptr = strstr( temp, "nameserver " )) != NULL )
				{
					ptr += 11;
					dns_found++;
					sprintf(buf, "%s %d: %s", _T("DNS_SERVER"), dns_found, ptr);
					interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);
				}
			}
			close(dns_fd);
		}
		if (!dns_found)
		{
			sprintf(buf, "%s: %s", _T("DNS_SERVER"), not_available);
			interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);
		}
	}

	return 0;
}

void* output_refreshLoop(void *pMenu)
{
	for (;;)
	{
		sleep(2);
		output_redrawMenu(pMenu);
	}
	pthread_exit(NULL);
}

int output_refreshStart(interfaceMenu_t* pMenu, void* pArg)
{
	int err = 0;
	if (output_refreshThread == 0) {
		err = pthread_create( &output_refreshThread, NULL, output_refreshLoop, pMenu );
		if (err != 0) {
			eprintf("%s: failed to start refresh thread: %s\n", __FUNCTION__, strerror(-err));
			output_refreshThread = 0;
		}
	}
	return err;
}

int output_refreshStop(interfaceMenu_t* pMenu, void* pArg)
{
	if (output_refreshThread != 0) {
		pthread_cancel(output_refreshThread);
		pthread_join  (output_refreshThread, NULL);
		output_refreshThread = 0;
	}
	return 0;
}

#ifdef ENABLE_PPP
static char* output_getPPPPassword(int field, void* pArg)
{
	return field == 0 ? pppInfo.password : NULL;
}

static char* output_getPPPLogin(int field, void* pArg)
{
	return field == 0 ? pppInfo.login : NULL;
}

static int output_setPPPPassword(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL )
		return 1;

	strcpy( pppInfo.password, value );

	FILE *f = fopen(PPP_CHAP_SECRETS_FILE, "w");
	if (f != NULL)
	{
		fprintf( f, "%s pppoe %s *\n", pppInfo.login, pppInfo.password );
		fclose(f);
	} else
	{
		output_warnIfFailed(1);
	}

	output_refillMenu(pMenu);
	return 0;
}

static int output_changePPPPassword(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText( pMenu, _T("PASSWORD"), "\\w+", output_setPPPPassword, output_getPPPPassword, inputModeABC, NULL );
}

static int output_setPPPLogin(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL )
		return 1;

	if (value[0] == 0)
	{
		system("rm -f " PPP_CHAP_SECRETS_FILE);
		output_refillMenu(pMenu);
		return 0;
	}
	strcpy(pppInfo.login, value);

	output_changePPPPassword(pMenu, pArg);
	return 1; // don't hide message box
}

static int output_changePPPLogin(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText( pMenu, _T("LOGIN"), "\\w+", output_setPPPLogin, output_getPPPLogin, inputModeABC, NULL );
}

static int output_restartPPP(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 0);

	system("/etc/init.d/S65ppp stop");
	system("/etc/init.d/S65ppp start");

	output_refillMenu(pMenu);
	interface_hideMessageBox();

	output_refreshStart(pMenu, pArg);

	return 0;
}

static int output_enterPPPMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str;

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
	interface_addMenuEntry(pMenu, buf, output_changePPPLogin, NULL, thumbnail_enterurl);

	if( pppInfo.login[0] != 0 )
	{
		snprintf(buf, sizeof(buf), "%s: ***", _T("PASSWORD"));
		interface_addMenuEntry(pMenu, buf, output_changePPPPassword, NULL, thumbnail_enterurl);
	}

	interface_addMenuEntry(pMenu, _T("APPLY_NETWORK_SETTINGS"), output_restartPPP, NULL, settings_renew);

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
			output_refreshStart(pMenu, pArg);
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
int output_enterLANMenu(interfaceMenu_t *lanMenu, void* pArg)
{
	// assume (lanMenu == _M &LANSubMenu);
	output_setIfaceMenuName(lanMenu, "Ethernet 2", 0, networkInfo.lanMode);

	interface_clearMenuEntries(lanMenu);
	output_fillLANMenu(lanMenu, SET_NUMBER(ifaceLAN));
	interface_addMenuEntry(lanMenu, _T("APPLY_NETWORK_SETTINGS"), output_applyNetworkSettings, SET_NUMBER(ifaceLAN), settings_renew);
	return 0;
}
#endif

#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
int output_fillLANMenu(interfaceMenu_t *lanMenu, void* iFace)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	stb810_networkInterface_t iface = GET_NUMBER(iFace);

	if (!output_isBridge())
	{
#ifdef STBPNX
		char temp[MENU_ENTRY_INFO_LENGTH];
		char *path = LAN_CONFIG_FILE;
#ifdef ENABLE_WIFI
		if (iface == ifaceWireless)
			path = WLAN_CONFIG_FILE;
#endif
		getParam(path, "IPADDR", _T("NOT_AVAILABLE_SHORT"), temp);
		sprintf(buf, "%s: %s", _T("IP_ADDRESS"), temp);
#endif
#ifdef STSDK
		sprintf(buf, "%s: %s", _T("IP_ADDRESS"),
			networkInfo.lan.ip.s_addr != 0 ? inet_ntoa(networkInfo.lan.ip) : _T("NOT_AVAILABLE_SHORT"));
#endif
		interface_addMenuEntry(lanMenu, buf, output_changeIP, SET_NUMBER(iface), thumbnail_configure);
	} else
	{
		sprintf(buf, "%s: %s", _T("IP_ADDRESS"), _T("GATEWAY_BRIDGE"));
		interface_addMenuEntryDisabled(lanMenu, buf, thumbnail_configure);
	}

#ifdef STBPNX
	if (!helperFileExists(STB_CONFIG_OVERRIDE_FILE))
	{
		sprintf(buf,"%s: %s", _T("GATEWAY_MODE"), output_getLanModeName(networkInfo.lanMode));
		interface_addMenuEntry(lanMenu, buf, interface_menuActionShowMenu, &GatewaySubMenu, thumbnail_configure);
		if (networkInfo.lanMode != lanStatic)
		{
			char temp[MENU_ENTRY_INFO_LENGTH];
			getParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", "0", temp);
			sprintf(buf,"%s: %s %s", _T("GATEWAY_BANDWIDTH"), atoi(temp) <= 0 ? _T("NONE") : temp, atoi(temp) <= 0 ? "" : _T("KBPS"));
			interface_addMenuEntry(lanMenu, buf, output_changeGatewayBandwidth, (void*)0, thumbnail_configure);
		}
	}
#endif // STBPNX

	sprintf(buf,"%s: %s", _T("INTERNET_CONNECTION_SHARING"), _T(networkInfo.lanMode == lanDhcpServer ? "YES" : "NO") );
	interface_addMenuEntry(lanMenu, buf, output_toggleDhcpServer, NULL, thumbnail_configure);

	return 0;
}
#endif // ENABLE_LAN

#ifdef ENABLE_WIFI
#ifdef HIDE_EXTRA_FUNCTIONS
static int output_wifiToggleAdvanced(interfaceMenu_t *pMenu, void* pIndex)
{
	wifiInfo.showAdvanced = !wifiInfo.showAdvanced;
	if (!wifiInfo.showAdvanced)
		interface_setSelectedItem(pMenu, GET_NUMBER(pIndex)); // jump to toggle advanced entry
	output_redrawMenu(pMenu);
	return 0;
}
#endif

static int output_enterWifiMenu(interfaceMenu_t *wifiMenu, void* pArg)
{
	char  buf[MENU_ENTRY_INFO_LENGTH];
	int exists;
	char *str;
	const int i = ifaceWireless;
	char *iface_name  = _T("WIRELESS");

	// assert (wifiMenu == wifiMenu);
#if !(defined STB225)
	exists = helperCheckDirectoryExsists("/sys/class/net/wlan0");
#else
	exists = 1; // don't check
#endif
	output_readWirelessSettings();

	if (wifiInfo.enable)
		output_setIfaceMenuName(wifiMenu, iface_name, wifiInfo.wanMode, networkInfo.lanMode);
	else {
		size_t len = snprintf(buf, sizeof(buf), "%s: %s", iface_name, _T("OFF"));
		interface_setMenuName(wifiMenu, buf, len+1);
	}

	interface_clearMenuEntries(wifiMenu);

	sprintf(buf, "%s: %s", iface_name, _T(wifiInfo.enable ? "ON" : "OFF"));
	interface_addMenuEntry(wifiMenu, buf, output_toggleWifiEnable, NULL, thumbnail_configure);

	if (!exists)
	{
		sprintf(buf, "%s: %s", iface_name, _T("NOT_AVAILABLE"));
		interface_addMenuEntryDisabled(wifiMenu, buf, thumbnail_no);
		if (WifiSubMenu.baseMenu.selectedItem >= 0)
			WifiSubMenu.baseMenu.selectedItem = MENU_ITEM_BACK;
	}

	if (!wifiInfo.enable || !exists)
	{
		interface_addMenuEntry(wifiMenu, _T("APPLY_NETWORK_SETTINGS"), output_applyNetworkSettings, SET_NUMBER(i), settings_renew);
		return 0;
	}

	// Wireless-specific settings
	if (wifiInfo.wanMode)
	{
		interface_addMenuEntry(wifiMenu, _T("WIRELESS_LIST"), interface_menuActionShowMenu, &WirelessMenu, thumbnail_search);

#ifdef USE_WPA_SUPPLICANT
		char *state = NULL;
		struct wpa_ctrl *ctrl = wpa_ctrl_open(STB_WPA_SUPPLICANT_CTRL_DIR "/wlan0");
		if (ctrl)
		{
			char temp[MENU_ENTRY_INFO_LENGTH];
			size_t reply_len = sizeof(temp);

			if (wpa_ctrl_request(ctrl, "STATUS", 6, temp, &reply_len, NULL) == 0)
			{
				temp[reply_len]=0;
				state = strstr(temp, "wpa_state=");
				if (state)
				{
					state += 10;
					char *newline = strchr(state, '\n');
					if (newline)
						*newline=0;
				}
			} else
				eprintf("%s: failed to get status: %s\n", __FUNCTION__, strerror(errno));
			wpa_ctrl_close(ctrl);
			if (state && strcasecmp(state, "COMPLETED") == 0)
			{
				snprintf(temp, sizeof(temp), "%s %s", _T("CONNECTED_TO"), wifiInfo.essid);
				interface_addMenuEntryDisabled(wifiMenu, temp, thumbnail_info);
			} else
			{
				snprintf(temp, sizeof(temp), "%s %s", _T("CONNECTING_TO"), wifiInfo.essid);
				interface_addMenuEntryDisabled(wifiMenu, temp, thumbnail_question);
			}
		} else
		{
			eprintf("%s: failed to connect to wpa_supplicant: %s\n", __FUNCTION__, strerror(errno));
			interface_addMenuEntryDisabled(wifiMenu, _T("SETTINGS_APPLY_REQUIRED"), thumbnail_info);
		}
#endif // USE_WPA_SUPPLICANT
	} else
	{
		sprintf(buf, "%s: %s", _T("ESSID"), wifiInfo.essid);
		interface_addMenuEntry(wifiMenu, buf, output_changeESSID, SET_NUMBER(i), thumbnail_enterurl);

		if (wifiInfo.auth != wifiAuthOpen && wifiInfo.auth < wifiAuthCount)
		{
			sprintf(buf, "%s: %s", _T("PASSWORD"), wifiInfo.key );
			interface_addMenuEntry(wifiMenu, buf, output_changeWifiKey, NULL, thumbnail_enterurl);
		}
	}

	// IP settings
	if (wifiInfo.wanMode)
		output_fillWANMenu(wifiMenu, SET_NUMBER(i));
	else
#ifdef STBPNX
		output_fillLANMenu(wifiMenu, SET_NUMBER(i));
#else
		output_fillLANMenu(wifiMenu, SET_NUMBER(ifaceLAN));
#endif

#ifdef HIDE_EXTRA_FUNCTIONS
	int basicOptionsCount = interface_getMenuEntryCount(wifiMenu);
	if (!wifiInfo.showAdvanced)
	{
		interface_addMenuEntry(wifiMenu, _T("SHOW_ADVANCED"), output_wifiToggleAdvanced, SET_NUMBER(basicOptionsCount), thumbnail_configure);
	} else
#endif
	// Advanced settings
	{
#ifdef STBPNX
		char temp[MENU_ENTRY_INFO_LENGTH];
		char path[MAX_CONFIG_PATH];

		sprintf(buf, "%s: %s", _T("MODE"), wifiInfo.mode == wifiModeAdHoc ? "Ad-Hoc" : "Managed");
		interface_addMenuEntry2(wifiMenu, buf, wifiInfo.wanMode, output_toggleWifiMode, SET_NUMBER(i), thumbnail_configure);

		sprintf(buf, "%s: %s", _T("ADDR_MODE"), wifiInfo.dhcp ? _T("ADDR_MODE_DHCP") : _T("ADDR_MODE_STATIC"));
		interface_addMenuEntry2(wifiMenu, buf, wifiInfo.wanMode, output_toggleMode, SET_NUMBER(i), thumbnail_configure);

		if (wifiInfo.dhcp == 0 || wifiInfo.wanMode == 0)
		{
			getParam(path, "IPADDR", _T("NOT_AVAILABLE_SHORT"), temp);
			sprintf(buf, "%s: %s", _T("IP_ADDRESS"), temp);
			interface_addMenuEntry(wifiMenu, buf, output_changeIP, SET_NUMBER(i), thumbnail_configure);

			getParam(path, "NETMASK", _T("NOT_AVAILABLE_SHORT"), temp);
			sprintf(buf, "%s: %s", _T("NETMASK"), temp);
			interface_addMenuEntry(wifiMenu, buf, output_changeNetmask, SET_NUMBER(i), thumbnail_configure);
			
			if ( wifiInfo.wanMode )
			{
				getParam(path, "DEFAULT_GATEWAY", _T("NOT_AVAILABLE_SHORT"), temp);
				sprintf(buf, "%s: %s", _T("GATEWAY"), temp);
				interface_addMenuEntry(wifiMenu, buf, output_changeGw, SET_NUMBER(i), thumbnail_configure);

				getParam(path, "NAMESERVERS", _T("NOT_AVAILABLE_SHORT"), temp);
				sprintf(buf, "%s: %s", _T("DNS_SERVER"), temp);
				interface_addMenuEntry(wifiMenu, buf, output_changeDNS, SET_NUMBER(i), thumbnail_configure);
			}
		}
#endif // STBPNX
		if (wifiInfo.wanMode)
		{
			sprintf(buf, "%s: %s", _T("ESSID"), wifiInfo.essid);
			interface_addMenuEntry(wifiMenu, buf, output_changeESSID, SET_NUMBER(i), thumbnail_enterurl);

			if (wifiInfo.auth != wifiAuthOpen && wifiInfo.auth < wifiAuthCount)
			{
				sprintf(buf, "%s: %s", _T("PASSWORD"), wifiInfo.wanMode ? "***" : wifiInfo.key );
				interface_addMenuEntry(wifiMenu, buf, output_changeWifiKey, NULL, thumbnail_enterurl);
			}
		} else
		{
		sprintf(buf, "iwlist %s channel > %s", helperEthDevice(i), INFO_TEMP_FILE);
		system(buf);
		FILE* f = fopen( INFO_TEMP_FILE, "r" );
		if (f)
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
		if (wifiInfo.channelCount > 0)
		{
			sprintf(buf, "%s: %d", _T("CHANNEL_NUMBER"), wifiInfo.currentChannel);
			interface_addMenuEntry(wifiMenu, buf, output_changeWifiChannel, SET_NUMBER(i), thumbnail_configure);
		}
		sprintf(buf, "%s: %s", _T("AUTHENTICATION"), wireless_auth_print( wifiInfo.auth ));
		interface_addMenuEntry(wifiMenu, buf, output_toggleAuthMode, SET_NUMBER(i), thumbnail_configure);
		if( wifiInfo.auth == wifiAuthWPAPSK || wifiInfo.auth == wifiAuthWPA2PSK )
		{
			sprintf(buf, "%s: %s", _T("ENCRYPTION"), wireless_encr_print( wifiInfo.encryption ));
			interface_addMenuEntry(wifiMenu, buf, output_toggleWifiEncryption, SET_NUMBER(i), thumbnail_configure);
		}
		} // !wifiInfo.wanMode
#ifdef HIDE_EXTRA_FUNCTIONS
		interface_addMenuEntry(wifiMenu, _T("HIDE_ADVANCED"), output_wifiToggleAdvanced, SET_NUMBER(basicOptionsCount), thumbnail_configure);
#endif
	}

	interface_addMenuEntry(wifiMenu, _T("APPLY_NETWORK_SETTINGS"), output_applyNetworkSettings, SET_NUMBER(i), settings_renew);

	return 0;
}

static int output_wifiKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandBlue)
	{
		interface_menuActionShowMenu( pMenu, &WirelessMenu );
		return 0;
	}
	return 1;
}

int output_readWirelessSettings(void)
{
	char buf[BUFFER_SIZE];

#ifdef STBPNX
	char *path = WLAN_CONFIG_FILE;

	wifiInfo.auth       = wifiAuthOpen;
	wifiInfo.encryption = wifiEncTKIP;

	getParam(path, "ENABLE_WIRELESS", "0", buf);
	wifiInfo.enable = strtol( buf, NULL, 10 );

	getParam(path, "WAN_MODE", "0", buf);
	wifiInfo.wanMode = strtol( buf, NULL, 10 );

	getParam(path, "MODE", "ad-hoc", buf);
	wifiInfo.mode = strcmp(buf, "managed") == 0 ? wifiModeManaged : wifiModeAdHoc;

	getParam(path, "BOOTPROTO", "static", buf);
	wifiInfo.dhcp = strcmp("dhcp+dns", buf) == 0;

	getParam(path, "ESSID", _T("NOT_AVAILABLE_SHORT"), buf);
	strncpy(wifiInfo.essid, buf, sizeof(wifiInfo.essid));
	wifiInfo.essid[sizeof(wifiInfo.essid)-1]=0;

	getParam(path, "CHANNEL", "1", buf);
	wifiInfo.currentChannel = strtol( buf, NULL, 10 );

	getParam(path, "AUTH", "SHARED", buf);
	if (strcasecmp( buf, "WPAPSK"  ) == 0)
		wifiInfo.auth = wifiAuthWPAPSK;
	else
	if (strcasecmp( buf, "WPA2PSK" ) == 0)
		wifiInfo.auth = wifiAuthWPA2PSK;

	getParam(path, "ENCRYPTION", "WEP", buf);
	if (strcasecmp( buf, "WEP" ) == 0)
		wifiInfo.auth = wifiAuthWEP;
	else
	if (strcasecmp( buf, "AES" ) == 0)
		wifiInfo.encryption = wifiEncAES;

	getParam(path, "KEY", "", buf);
	memcpy( wifiInfo.key, buf, sizeof(wifiInfo.key)-1 );
	wifiInfo.key[sizeof(wifiInfo.key)-1] = 0;
// STBPNX
#else
	wifiInfo.auth = wifiAuthWPA2PSK;
	wifiInfo.encryption = wifiEncAES;

#ifdef USE_WPA_SUPPLICANT
	if (wifiInfo.wanMode)
	{
		output_readWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
	} else
#endif
	{
		wifiInfo.mode = wifiModeMaster;
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
#endif // !STBPNX
	return 0;
}

#ifdef STSDK
int output_setHostapdChannel(int channel)
{
	if (channel < 1 || channel > 14)
		return -1;

	int res = 0;
	char value[MENU_ENTRY_INFO_LENGTH];

	snprintf(value, sizeof(value), "%d", channel);
	res = setParam(STB_HOSTAPD_CONF, "channel", value);
	if (res == 0)
	{
		char *ht40;

		getParam(STB_HOSTAPD_CONF, "ht_capab", "", value);
		if (value[0] == 0)
			getParam(SYSTEM_CONFIG_DIR "/defaults/hostapd.conf", "ht_capab", "", value);

		ht40 = strstr(value, "[HT40");
		if (ht40 && (ht40[5] == '-' || ht40[5] == '+'))
		{
			ht40[5] = channel < 8 ? '+' : '-';
		} else
		{
			snprintf(value, sizeof(value), "[HT40%c][SHORT-GI-20][SHORT-GI-40]", channel < 8 ? '+' : '-');
		}
		setParam(STB_HOSTAPD_CONF, "ht_capab", value);
	}

	return res;
}
#endif // STSDK
#endif // ENABLE_WIFI

#if (defined ENABLE_IPTV) && (defined ENABLE_XWORKS)
#ifdef STBPNX
static int output_togglexWorks(interfaceMenu_t *pMenu, void* pArg)
{
	return output_saveAndRedraw(setParam( STB_CONFIG_FILE, "XWORKS", pArg ? "ON" : "OFF" ), pMenu);
}
#endif
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

	if (setParam( STB_CONFIG_FILE, "XWORKS_PROTO", str ) != 0)
	{
		output_warnIfFailed(1);
		return 1;
	}
	system(XWORKS_INIT_SCRIPT " config");
	output_redrawMenu(pMenu);
	return 0;
}

static int output_restartxWorks(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox(_T("LOADING"), settings_renew, 0);

	system(XWORKS_INIT_SCRIPT " restart");

	output_refillMenu(pMenu);
	interface_hideMessageBox();
	return 0;
}
#endif // ENABLE_XWORKS

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

#ifdef ENABLE_IPTV
int output_changeIPTVtimeout(interfaceMenu_t *pMenu, void* pArg)
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

	return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int output_enterIPTVMenu(interfaceMenu_t *iptvMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries(iptvMenu);

	{
	char *str = NULL;
	switch (appControlInfo.rtpMenuInfo.usePlaylistURL)
	{
		case iptvPlaylistUrl:  str = "URL"; break;
		case iptvPlaylistFw:   str = "FW";  break;
		default: str = "SAP"; break;
	}
	snprintf(buf, sizeof(buf), "%s: %s", _T("IPTV_PLAYLIST"), str);
	interface_addMenuEntry(iptvMenu, buf, output_changeIPTVPlaylist, NULL, thumbnail_configure);
	}

	if (appControlInfo.rtpMenuInfo.usePlaylistURL == iptvPlaylistUrl)
	{
		snprintf(buf, sizeof(buf), "%s: %s", _T("IPTV_PLAYLIST"),
			appControlInfo.rtpMenuInfo.playlist[0] != 0 ? appControlInfo.rtpMenuInfo.playlist : _T("NONE"));
		interface_addMenuEntry2(iptvMenu, buf, appControlInfo.rtpMenuInfo.usePlaylistURL,
			output_changeURL, SET_NUMBER(optionRtpPlaylist), thumbnail_enterurl);
	}

	if (appControlInfo.rtpMenuInfo.usePlaylistURL != iptvPlaylistSap)
	{
		snprintf(buf, sizeof(buf), "%s: %s", _T("IPTV_EPG"),
			appControlInfo.rtpMenuInfo.epg[0] != 0 ? appControlInfo.rtpMenuInfo.epg : _T("NONE"));
		interface_addMenuEntry2(iptvMenu, buf, appControlInfo.rtpMenuInfo.usePlaylistURL,
			output_changeURL, SET_NUMBER(optionRtpEpg), thumbnail_enterurl);
	}

	snprintf(buf, sizeof(buf), "%s: %ld", _T("IPTV_WAIT_TIMEOUT"), appControlInfo.rtpMenuInfo.pidTimeout);
	interface_addMenuEntry(iptvMenu, buf, output_changeIPTVtimeout, pArg, thumbnail_configure);

#ifdef ENABLE_XWORKS
	int xworks_enabled = 1;
	media_proto proto;
	char *str;
#ifdef STBPNX
	getParam( STB_CONFIG_FILE, "XWORKS", "OFF", buf );
	xworks_enabled = strcasecmp( "ON", buf ) == 0;
	snprintf(buf, sizeof(buf), "%s: %s", _T("XWORKS"), xworks_enabled ? _T("ON") : _T("OFF"));
	interface_addMenuEntry(iptvMenu, buf, output_togglexWorks, SET_NUMBER(!xworks_enabled), thumbnail_configure);
#endif
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

	snprintf(buf, sizeof(buf), "%s: %s", _T("USE_PROTOCOL"), str);
	if( xworks_enabled )
		interface_addMenuEntry(iptvMenu, buf, output_togglexWorksProto, (void*)proto, thumbnail_configure);
	else
		interface_addMenuEntryDisabled(iptvMenu, buf, thumbnail_configure);

	snprintf(buf, sizeof(buf), "%s: %s", _T("XWORKS"), _T("RESTART"));
	interface_addMenuEntry(iptvMenu, buf, output_restartxWorks, NULL, settings_renew);

	if( xworks_enabled && appControlInfo.rtpMenuInfo.usePlaylistURL )
	{
		char temp[256];
		sprintf(buf, "ifconfig %s | grep \"inet addr\"", helperEthDevice(ifaceWAN));
		if (helperParseLine(INFO_TEMP_FILE, buf, "inet addr:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
		{
			sprintf(buf, "http://%s:1080/xworks.xspf", temp );
			interface_addMenuEntryDisabled(iptvMenu, buf, thumbnail_enterurl);
		}
	}

	if( interface_getSelectedItem( iptvMenu ) >= interface_getMenuEntryCount( iptvMenu ) )
	{
		interface_setSelectedItem( iptvMenu, 0 );
	}
#endif
#ifdef ENABLE_PROVIDER_PROFILES
	interface_addMenuEntry( iptvMenu, _T("PROFILE"), interface_menuActionShowMenu, (void*)&ProfileMenu, thumbnail_account );
#endif
#ifdef ENABLE_TELETES
	snprintf(buf, sizeof(buf), "%s: %s", "Teletes playlist", appControlInfo.rtpMenuInfo.teletesPlaylist[0] != 0 ? appControlInfo.rtpMenuInfo.teletesPlaylist : _T("NONE"));
	interface_addMenuEntry(iptvMenu, buf, output_changeURL, SET_NUMBER(optionTeletesPlaylist), thumbnail_enterurl);
#endif

	return 0;
}
#endif

#ifdef ENABLE_VOD
static int output_enterVODMenu(interfaceMenu_t *vodMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(vodMenu);

	snprintf(buf, sizeof(buf), "%s: %s", _T("VOD_PLAYLIST"), appControlInfo.rtspInfo.usePlaylistURL ? "URL" : _T("IP_ADDRESS") );
	interface_addMenuEntry(vodMenu, buf, output_toggleVODPlaylist, NULL, thumbnail_configure);

	if (appControlInfo.rtspInfo.usePlaylistURL) {
		snprintf(buf, sizeof(buf), "%s: %s", _T("VOD_PLAYLIST"),
			appControlInfo.rtspInfo.streamInfoUrl != 0 ? appControlInfo.rtspInfo.streamInfoUrl : _T("NONE"));
		interface_addMenuEntry(vodMenu, buf, output_changeURL, SET_NUMBER(optionVodPlaylist), thumbnail_enterurl);
	} else {
		snprintf(buf, sizeof(buf), "%s: %s", _T("VOD_INFO_IP_ADDRESS"), appControlInfo.rtspInfo.streamInfoIP);
		interface_addMenuEntry(vodMenu, buf, output_changeVODINFOIP, NULL, thumbnail_enterurl);
	}
	snprintf(buf, sizeof(buf), "%s: %s", _T("VOD_IP_ADDRESS"), appControlInfo.rtspInfo.streamIP);
	interface_addMenuEntry(vodMenu, buf, output_changeVODIP, NULL, thumbnail_enterurl);
	return 0;
}
#endif

static int output_enterWebMenu(interfaceMenu_t *webMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries(webMenu);
	sprintf(buf, "%s: %s", _T("PROXY_ADDR"), appControlInfo.networkInfo.proxy[0] != 0 ? appControlInfo.networkInfo.proxy : _T("NONE"));
	interface_addMenuEntry(webMenu, buf, output_changeProxyAddress, pArg, thumbnail_enterurl);

	sprintf(buf, "%s: %s", _T("PROXY_LOGIN"), appControlInfo.networkInfo.login[0] != 0 ? appControlInfo.networkInfo.login : _T("NONE"));
	interface_addMenuEntry(webMenu, buf, output_changeProxyLogin, pArg, thumbnail_enterurl);

	sprintf(buf, "%s: ***", _T("PROXY_PASSWD"));
	interface_addMenuEntry(webMenu, buf, output_changeProxyPassword, pArg, thumbnail_enterurl);

#ifdef ENABLE_BROWSER
#ifdef STBPNX
	char temp[MENU_ENTRY_INFO_LENGTH];

		getParam(BROWSER_CONFIG_FILE, "HomeURL", "", temp);
		sprintf(buf, "%s: %s", _T("MW_ADDR"), temp);
		interface_addMenuEntry(webMenu, buf, output_changeMiddlewareUrl, pArg, thumbnail_enterurl);

		getParam(BROWSER_CONFIG_FILE, "AutoLoadingMW", "", temp);
		if (temp[0] != 0)
		{
			sprintf(buf, "%s: %s", _T("MW_AUTO_MODE"), strcmp(temp,"ON")==0 ? _T("ON") : _T("OFF"));
			interface_addMenuEntry(webMenu, buf, output_toggleMWAutoLoading, pArg, thumbnail_configure);
		}else
		{
			setParam(BROWSER_CONFIG_FILE, "AutoLoadingMW","OFF");
			sprintf(buf, "%s: %s", _T("MW_AUTO_MODE"), _T("OFF"));
			interface_addMenuEntry(webMenu, buf, output_toggleMWAutoLoading, pArg, thumbnail_configure);
		}
#endif
#endif
	return 0;
}

#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
#if (defined STSDK) && (defined ENABLE_WIFI)
static int output_confirmDhcpServerEnable(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* ignored)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else
	if ((cmd->command == interfaceCommandGreen) ||
	    (cmd->command == interfaceCommandEnter) ||
	    (cmd->command == interfaceCommandOk))
	{
		output_toggleDhcpServer(pMenu, SET_NUMBER(1));
		return 0;
	}
	return 1;
}
#endif // STSDK && ENABLE_WIFI

int output_toggleDhcpServer(interfaceMenu_t *pMenu, void* pForce)
{
#ifdef STSDK
#ifdef ENABLE_WIFI
	if (wifiInfo.wanMode && networkInfo.lanMode != lanDhcpServer && !pForce) {
		interface_showConfirmationBox(_T("CONFIRM_DHCP_ENABLE"), thumbnail_warning, output_confirmDhcpServerEnable, NULL);
		return 1;
	}
#endif
	networkInfo.lanMode = networkInfo.lanMode == lanDhcpServer ? lanStatic : lanDhcpServer;
	networkInfo.changed = 1;
	output_writeDhcpConfig();
	return output_saveAndRedraw(output_writeInterfacesFile(), pMenu);
#endif // STSDK
#ifdef STBPNX
	return output_toggleGatewayMode(pMenu, SET_NUMBER(networkInfo.lanMode == lanDhcpServer ? lanStatic : lanDhcpServer));
#endif
	return 0;
}
#endif // ENABLE_LAN || ENABLE_WIFI

interfaceMenu_t *output_getPlaylistEditorMenu(void) {
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu))
		return interfaceInfo.currentMenu;
	return NULL;
}

char *output_getSelectedNamePlaylistEditor(void)
{
	interfaceMenu_t  *baseMenu;
	baseMenu = output_getPlaylistEditorMenu();
	if (baseMenu != NULL)
		return baseMenu->menuEntry[baseMenu->selectedItem].info;
	return NULL;
}

int enablePlayListEditorMenu(interfaceMenu_t *interfaceMenu)
{
#ifdef ENABLE_DVB
	if (!memcmp(interfaceMenu, &InterfacePlaylistEditorDigital.baseMenu, sizeof(interfaceListMenu_t)))
		return 1;
	if (!memcmp(interfaceMenu, &InterfacePlaylistEditorAnalog.baseMenu, sizeof(interfaceListMenu_t)))
		return 2;
#endif
	return false;
}
int enablePlayListSelectMenu(interfaceMenu_t *interfaceMenu)
{
#ifdef ENABLE_DVB
	if (!memcmp(interfaceMenu, &InterfacePlaylistSelectDigital.baseMenu, sizeof(interfaceListMenu_t)))
		return 1;
	if (!memcmp(interfaceMenu, &InterfacePlaylistSelectAnalog.baseMenu, sizeof(interfaceListMenu_t)))
		return 2;
#endif
	return false;
}

int output_ping(char *value)
{
	char cmd[256];
	int ret = -1;
	sprintf(cmd , "ping -c 1 %s", value);
	printf("cmd: %s\n",cmd);
	ret = system(cmd);

	if (ret != -1)
		ret = WEXITSTATUS(ret);
	return ret;
}

int output_pingVisual(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if(value == NULL) {
		return 0;
	}
	int valuePing;
	valuePing = output_ping(value);

	if (valuePing == 0){
		interface_showMessageBox(_T("IP_RELEASE_SUCCESSFUL"), thumbnail_yes, 0);
	} else {
		interface_showMessageBox(_T("IP_RELEASE_NOT_SUCCESSFUL"), thumbnail_error, 0);
	}
	return -1;
}

static int output_pingMenu(interfaceMenu_t* pMenu, void* pArg)
{
	interface_getText(pMenu, _T("IP_SERVER"), "\\w+", output_pingVisual, NULL, inputModeABC, &pArg);
	return 0;
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

#ifdef ENABLE_DVB
static int output_saveParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	unsigned char out[16];
	char out_hex[32];
	if((value == NULL) || (strlen(value) < 4)) {
		return -1;
	}

	md5((unsigned char*)value, strlen(value), out);
	for (int i=0;i<16;i++) {
		sprintf(&out_hex[i*2], "%02hhx", out[i]);
	}
	FILE *pass_file = fopen(PARENT_CONTROL_FILE, "w");
	if(pass_file == NULL) {
		return 0;
	}
	fwrite(out_hex, 32, 1, pass_file);
	fclose(pass_file);

	return 0;
}

static int output_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if(value == NULL) {
		return 0;
	}
	unsigned char out[16];
	char out_hex[32];
	char pass[32];
	md5((unsigned char*)value, strlen(value), out);
	for (int i=0;i<16;i++) {
		sprintf(&out_hex[i*2], "%02hhx", out[i]);
	}
	FILE *pass_file = fopen(PARENT_CONTROL_FILE, "r");
	if(pass_file == NULL) {
		pass_file = fopen(PARENT_CONTROL_DEFAULT_FILE, "r");
		if(pass_file == NULL) {
			return 0;
		}
	}
	fread(pass, 32, 1, pass_file);
	fclose(pass_file);

	if(strncmp(out_hex, pass, 32) == 0) {
		const char *mask = "\\d{6}";
		interface_getText(pMenu, _T("ENTER_NEW_PASSWORD"), mask, output_saveParentControlPass, NULL, inputModeDirect, &pArg);
		return 1;
	}

	return 0;
}

static int output_changeParentControlPass(interfaceMenu_t* pMenu, void* pArg)
{
	const char *mask = "\\d{6}";
	interface_getText(pMenu, _T("ENTER_CURRENT_PASSWORD"), mask, output_checkParentControlPass, NULL, inputModeDirect, &pArg);
	return 0;
}

static int output_changeCreateNewBouquet(interfaceMenu_t* pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_BOUQUET_NAME"), "\\w+", bouquet_createNewBouquet, NULL, inputModeABC, &pArg);
	return 0;
}

int output_enterPlaylistMenu(interfaceMenu_t *interfaceMenu, void* notused)
{
	char str[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(interfaceMenu);
	snprintf(str, sizeof(str), "%s: %s", _T("PLAYLIST_ENABLE"), bouquet_enable()? "ON" : "OFF");
	interface_addMenuEntry(interfaceMenu, str, bouquet_enableControl, NULL, settings_interface);
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_ANALOG"), interface_menuActionShowMenu, &InterfacePlaylistAnalog, settings_interface);
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_DIGITAL"), interface_menuActionShowMenu, &InterfacePlaylistDigital, settings_interface);
	return 0;
}

int output_enterPlaylistAnalog(interfaceMenu_t *interfaceMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(interfaceMenu);
	if (bouquet_enable()) {
		snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquet_getAnalogBouquetName());
		interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectAnalog, settings_interface);
	}
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_EDITOR"), interface_menuActionShowMenu, &InterfacePlaylistEditorAnalog, settings_interface);	
	if (bouquet_enable()) {
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_UPDATE"), bouquet_updateAnalogBouquet, NULL, settings_interface);
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), bouquet_saveAnalogMenuBouquet, NULL, settings_interface);
	}
	return 0;
}

int output_enterPlaylistDigital(interfaceMenu_t *interfaceMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(interfaceMenu);

	if (bouquet_enable()) {
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_NEW_BOUQUETS"), output_changeCreateNewBouquet, NULL, settings_interface);
		snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquet_getDigitalBouquetName());
		interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectDigital, settings_interface);
	}
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_EDITOR"), interface_menuActionShowMenu, &InterfacePlaylistEditorDigital, settings_interface);
	if (bouquet_enable()) {
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), bouquet_saveDigitalBouquet, NULL, settings_interface);
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_UPDATE"), bouquet_updateDigitalBouquet, NULL, settings_interface);
	}
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_REMOVE"), bouquet_removeBouquet, NULL, settings_interface);
	interface_addMenuEntry(interfaceMenu, _T("PARENT_CONTROL_CHANGE"), output_changeParentControlPass, NULL, settings_interface);
	return 0;
}
#endif //#ifdef ENABLE_DVB

int output_enterPlaybackMenu(interfaceMenu_t *pMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	const char *str = NULL;

	interface_clearMenuEntries(pMenu);

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
#if (defined STSDK)
	if(output_checkInputs() > 0) {
		str = _T("INPUTS_CONFIG");
		interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &InputsSubMenu, settings_video);
	}
#endif

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
#ifndef ENABLE_PASSWORD
		interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &DVBSubMenu, settings_dvb);
#else
		interface_addMenuEntry(outputMenu, str, output_askPassword, (void*)output_showDVBMenu, settings_dvb);
#endif
	}
#endif // #ifdef ENABLE_DVB

	str = _T("NETWORK_CONFIG");
#ifndef ENABLE_PASSWORD
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &NetworkSubMenu, settings_network);
#else
	interface_addMenuEntry(outputMenu, str, output_askPassword, (void*)output_showNetworkMenu, settings_network);
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
		//interface_addMenuEntry(outputMenu, str, output_calibrateCurrentMeter, NULL, thumbnail_configure);
		//interface_addMenuEntry(outputMenu, str, output_enterCalibrateMenu, &CurrentmeterSubMenu, thumbnail_configure);
		interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &CurrentmeterSubMenu, thumbnail_configure);
	}

	str = _T("UPDATES");
	interface_addMenuEntry(outputMenu, str, interface_menuActionShowMenu, &UpdateMenu, settings_updates);
#endif /* STSDK */

	str = _T("STATUS_REPORT");
	interface_addMenuEntry(outputMenu, str, (void*)output_statusReport, NULL, thumbnail_configure);

	str = _T("RESET_SETTINGS");
#ifndef ENABLE_PASSWORD
	interface_addMenuEntry(outputMenu, str, output_resetSettings, NULL, thumbnail_warning);
#else
	interface_addMenuEntry(outputMenu, str, output_askPassword, (void*)output_resetSettings, thumbnail_warning);
#endif

}

void output_buildMenu(interfaceMenu_t *pParent)
{
#ifdef ENABLE_WIFI
	memset(&wifiInfo, 0, sizeof(wifiInfo));
	wifiInfo.channelCount   = 0;
	wifiInfo.currentChannel = 1;
	wifiInfo.showAdvanced   = 0;
#endif

#ifdef STSDK
	memset(&networkInfo, 0, sizeof(networkInfo));
	output_readInterfacesFile();
#endif
	createListMenu(&OutputMenu, _T("SETTINGS"), thumbnail_configure, NULL, pParent,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	createListMenu(&VideoSubMenu, _T("VIDEO_CONFIG"), settings_video, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterVideoMenu, NULL, NULL);
#ifdef STSDK
	if(output_checkInputs() > 0) {
		createListMenu(&InputsSubMenu, _T("INPUTS_CONFIG"), settings_video, NULL, _M &OutputMenu,
			interfaceListMenuIconThumbnail, output_enterInputsMenu, NULL, NULL);
	}
#endif
#ifdef ENABLE_ANALOGTV
	if(analogtv_hasTuner()) {
		createListMenu(&AnalogTvSubMenu, _T("ANALOGTV_CONFIG"), settings_dvb, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterAnalogTvMenu, NULL, NULL);
	}
#endif
#ifdef STSDK
	if(currentmeter_isExist()){
		createListMenu(&CurrentmeterSubMenu, _T("CURRENTMETER_CALIBRATE"), settings_dvb, NULL, _M &OutputMenu,
			interfaceListMenuIconThumbnail, output_enterCalibrateMenu, NULL, NULL);
	}
#endif

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
#endif // ENABLE_DVB
	createListMenu(&NetworkSubMenu, _T("NETWORK_CONFIG"), settings_network, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterNetworkMenu, output_leaveNetworkMenu, NULL);

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

#ifdef ENABLE_DVB
	createListMenu(&InterfacePlaylistMain, _T("PLAYLIST_MAIN"), settings_interface, NULL, _M &OutputMenu,
        interfaceListMenuIconThumbnail, output_enterPlaylistMenu, NULL, NULL);

	createListMenu(&InterfacePlaylistAnalog, _T("PLAYLIST_ANALOG"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistAnalog, NULL, NULL);

	createListMenu(&InterfacePlaylistDigital, _T("PLAYLIST_DIGITAL"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistDigital, NULL, NULL);

	createListMenu(&InterfacePlaylistSelectDigital, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, enterPlaylistDigitalSelect, NULL, NULL);

	createListMenu(&InterfacePlaylistSelectAnalog, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, enterPlaylistAnalogSelect, NULL, NULL);

	int playlistEditor_icons[4] = { statusbar_f1_cancel, statusbar_f2_ok, statusbar_f3_edit, 0};
	createListMenu(&InterfacePlaylistEditorDigital, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, enterPlaylistEditorDigital, NULL, NULL);

	createListMenu(&InterfacePlaylistEditorAnalog, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, enterPlaylistEditorAnalog, NULL, NULL);
#endif //#ifdef ENABLE_DVB

	createListMenu(&PlaybackMenu, _T("PLAYBACK"), thumbnail_loading, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, output_enterPlaybackMenu, NULL, NULL);

	createListMenu(&WANSubMenu, "WAN", settings_network, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterWANMenu, output_leaveNetworkMenu, SET_NUMBER(ifaceWAN));

#ifdef ENABLE_PPP
	createListMenu(&PPPSubMenu, _T("PPP"), settings_network, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterPPPMenu, output_refreshStop, SET_NUMBER(ifaceWAN));
#endif
#ifdef ENABLE_LAN
	createListMenu(&LANSubMenu, "LAN", settings_network, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterLANMenu, output_leaveNetworkMenu, SET_NUMBER(ifaceLAN));
#endif
#ifdef STBPNX
#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
	createListMenu(&GatewaySubMenu, _T("GATEWAY_MODE"), settings_network, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterGatewayMenu, NULL, NULL);
#endif
#endif
#ifdef ENABLE_WIFI
	int wifi_icons[4] = { 0, 0, 0, statusbar_f4_enterurl };
	createListMenu(&WifiSubMenu, _T("WIRELESS"), settings_network, wifi_icons, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterWifiMenu, output_leaveNetworkMenu, SET_NUMBER(ifaceWireless));
	interface_setCustomKeysCallback(_M &WifiSubMenu, output_wifiKeyCallback);

	wireless_buildMenu(_M &WifiSubMenu);
#endif
#ifdef ENABLE_MOBILE
	createListMenu(&MobileMenu, _T("MOBILE"), settings_network, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterMobileMenu, output_refreshStop, NULL);
#endif
#ifdef ENABLE_IPTV
	createListMenu(&IPTVSubMenu, _T("TV_CHANNELS"), thumbnail_multicast, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterIPTVMenu, NULL, NULL);
#ifdef ENABLE_PROVIDER_PROFILES
	createListMenu(&ProfileMenu, _T("PROFILE"), thumbnail_account, NULL, _M &IPTVSubMenu,
		interfaceListMenuIconThumbnail, output_enterProfileMenu, output_leaveProfileMenu, NULL);
#endif
#endif
#ifdef ENABLE_VOD
	createListMenu(&VODSubMenu, _T("MOVIES"), thumbnail_vod, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterVODMenu, NULL, NULL);
#endif
	createListMenu(&WebSubMenu, _T("INTERNET_BROWSING"), thumbnail_internet, NULL, _M &NetworkSubMenu,
		interfaceListMenuIconThumbnail, output_enterWebMenu, NULL, NULL);

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

		interface_clearMenuEntries(pMenu);
		if( readlink(STB_PROVIDER_PROFILE, output_profile, sizeof(output_profile)) <= 0 )
			output_profile[0] = 0;

		output_profiles_count = scandir( STB_PROVIDER_PROFILES_DIR, &output_profiles, output_select_profile, alphasort );
		for( i = 0; i < output_profiles_count; i++ )
		{
			snprintf(full_path, sizeof(full_path), "%s%s",STB_PROVIDER_PROFILES_DIR,output_profiles[i]->d_name);
			getParam(full_path, "NAME", "", name);
			interface_addMenuEntry( pMenu, name, output_setProfile, SET_NUMBER(i),
			                        strcmp(full_path, output_profile) == 0 ? radiobtn_filled : radiobtn_empty );
		}
		if( 0 == output_profiles_count )
			interface_addMenuEntryDisabled(pMenu, _T("NO_FILES"), thumbnail_info);
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
#undef DBG_MUTE
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

	output_warnIfFailed(saveAppSettings());
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

	getParam( filename, "ap_scan", "1", buf );
	wifiInfo.mode = strtol(buf,NULL,10) == 2 ? wifiModeAdHoc : wifiModeManaged;
	getParam( filename, "ssid", "", buf );
	if (buf[0] == '"') {
		buf[strlen(buf)-1]=0;
		strncpy( wifiInfo.essid, &buf[1], sizeof(wifiInfo.essid) );
	} else
		strcpy(wifiInfo.essid, DEFAULT_ESSID);
	wifiInfo.essid[sizeof(wifiInfo.essid)-1]=0;
	getParam( filename, "key_mgmt", "", buf );
	if (strcasecmp(buf, "NONE") == 0) {
		wifiInfo.auth = wifiAuthWEP;
		getParam( filename, "wep_key0", "", buf );
	} else {
		wifiInfo.auth = wifiAuthWPA2PSK;
		getParam( filename, "psk"     , "", buf );
	}
	if (buf[0] == '"') {
		buf[strlen(buf)-1]=0;
		strncpy(wifiInfo.key, &buf[1], sizeof(wifiInfo.key));
		wifiInfo.key[sizeof(wifiInfo.key)-1]=0;
	} else
		wifiInfo.key[0] =  0;
	if (wifiInfo.key[0] == 0)
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
int output_readInterfacesFile(void)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	struct in_addr addr;

	// WAN
	getParam(WAN_CONFIG_FILE, "BOOTPROTO", "dhcp", buf);
	networkInfo.wanDhcp = strcasecmp(buf, "static");

	getParam(WAN_CONFIG_FILE, "IPADDR", "0.0.0.0", buf);
	inet_aton(buf, &addr);
	networkInfo.wan.ip = addr;

	getParam(WAN_CONFIG_FILE, "NETMASK", "255.255.255.0", buf);
	inet_aton(buf, &addr);
	networkInfo.wan.mask = addr;

	getParam(WAN_CONFIG_FILE, "DEFAULT_GATEWAY", "0.0.0.0", buf);
	inet_aton(buf, &addr);
	networkInfo.wan.gw = addr;

	if (helperParseLine(STB_RESOLV_CONF, NULL, "nameserver ", buf, ' '))
		inet_aton(buf, &networkInfo.dns);

	// LAN
#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
	getParam(LAN_CONFIG_FILE, "MODE", "NAT", buf );
	if (strcasecmp(buf, "BRIDGE") == 0)
		networkInfo.lanMode = lanBridge;
	else
	if (strcasecmp(buf, "STATIC") == 0)
		networkInfo.lanMode = lanStatic;
	else
		networkInfo.lanMode = lanDhcpServer;
	getParam(LAN_CONFIG_FILE, "IPADDR", "0.0.0.0", buf);
	inet_aton(buf, &addr);
	networkInfo.lan.ip = addr;
#endif
#ifdef ENABLE_WIFI
	getParam(WLAN_CONFIG_FILE, "ENABLE_WIRELESS", "0", buf);
	wifiInfo.enable = atol(buf);
	getParam(WLAN_CONFIG_FILE, "WAN_MODE", "0", buf);
	wifiInfo.wanMode = atol(buf);
	getParam(WLAN_CONFIG_FILE, "BOOTPROTO", "0", buf);
	wifiInfo.dhcp = strcasecmp(buf, "static");
	getParam(WLAN_CONFIG_FILE, "IPADDR", "0.0.0.0", buf);
	inet_aton(buf, &addr);
	wifiInfo.wlan.ip = addr;

	getParam(WLAN_CONFIG_FILE, "NETMASK", "255.255.255.0", buf);
	inet_aton(buf, &addr);
	wifiInfo.wlan.mask = addr;

	getParam(WLAN_CONFIG_FILE, "DEFAULT_GATEWAY", "0.0.0.0", buf);
	inet_aton(buf, &addr);
	wifiInfo.wlan.gw = addr;

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
#endif

	if (networkInfo.wan.mask.s_addr == 0)
		networkInfo.wan.mask.s_addr = 0x00ffffff;
	if (networkInfo.lan.mask.s_addr == 0)
		networkInfo.lan.mask.s_addr = 0x00ffffff;
#ifdef ENABLE_WIFI
	if (  wifiInfo.wlan.mask.s_addr == 0)
		  wifiInfo.wlan.mask.s_addr = 0x00ffffff;
#endif
	if (networkInfo.lan.ip.s_addr == 0)
		networkInfo.lan.ip.s_addr = 0x016fa8c0; // 192.168.111.1
	return 0;
}

int output_writeInterfacesFile(void)
{
	setParam(WAN_CONFIG_FILE, "BOOTPROTO", networkInfo.wanDhcp ? "dhcp" : "static");
	if (networkInfo.wan.ip.s_addr != 0)
		setParam(WAN_CONFIG_FILE, "IPADDR", inet_ntoa(networkInfo.wan.ip));
	if (networkInfo.wan.mask.s_addr == 0)
		networkInfo.wan.mask.s_addr = 0x00ffffff;
	setParam(WAN_CONFIG_FILE, "NETMASK", inet_ntoa(networkInfo.wan.mask));
	if (networkInfo.wan.gw.s_addr != 0)
		setParam(WAN_CONFIG_FILE, "DEFAULT_GATEWAY", inet_ntoa(networkInfo.wan.gw));

#if (defined ENABLE_LAN) || (defined ENABLE_WIFI)
	char *mode = NULL;
	switch (networkInfo.lanMode)
	{
		case lanModeCount:  // fall through
		case lanDhcpServer: mode = "NAT";    break;
		case lanBridge:     mode = "BRIDGE"; break;
		case lanDhcpClient: mode = "DHCP";   break;
		case lanStatic:     mode = "STATIC"; break;
	}
	setParam(LAN_CONFIG_FILE, "MODE", mode);
	setParam(LAN_CONFIG_FILE, "IPADDR", inet_ntoa(networkInfo.lan.ip));
	if (networkInfo.lan.mask.s_addr == 0)
		networkInfo.lan.mask.s_addr = 0x00ffffff;
	//setParam(WAN_CONFIG_FILE, "NETMASK", inet_ntoa(networkInfo.lan.mask));
#endif

#ifdef ENABLE_WIFI
	setParam(WLAN_CONFIG_FILE, "WAN_MODE", wifiInfo.wanMode ? "1" : "0");
	setParam(WLAN_CONFIG_FILE, "BOOTPROTO", wifiInfo.dhcp ? "dhcp" : "static");
	if (wifiInfo.wlan.ip.s_addr != 0)
		setParam(WLAN_CONFIG_FILE, "IPADDR", inet_ntoa(wifiInfo.wlan.ip));
	if (wifiInfo.wlan.mask.s_addr == 0)
		wifiInfo.wlan.mask.s_addr = 0x00ffffff;
	setParam(WLAN_CONFIG_FILE, "NETMASK", inet_ntoa(wifiInfo.wlan.mask));
	if (wifiInfo.wlan.gw.s_addr != 0)
		setParam(WLAN_CONFIG_FILE, "DEFAULT_GATEWAY", inet_ntoa(wifiInfo.wlan.gw));

	if (wifiInfo.wanMode)
		return output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);

	setParam( STB_HOSTAPD_CONF, "ssid", wifiInfo.essid );
	output_setHostapdChannel(wifiInfo.currentChannel);
#ifdef ENABLE_LAN
	if (helperCheckDirectoryExsists("/sys/class/net/eth1"))
		setParam( STB_HOSTAPD_CONF, "bridge", "br0");
	else
#endif
	setParam( STB_HOSTAPD_CONF, "bridge", NULL);
	if (wifiInfo.auth <= wifiAuthWEP)
	{
		setParam( STB_HOSTAPD_CONF, "wep_key0", wifiInfo.auth == wifiAuthOpen ? NULL : wifiInfo.key );
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
#endif

	return 0;
}

int output_writeDhcpConfig(void)
{
	if (networkInfo.lanMode != lanDhcpServer)
	{
		unlink(STB_DHCPD_CONF);
		return 0;
	}

	system("ifcfg dhcp > " STB_DHCPD_CONF);

	return 0;
}
#endif // STSDK

const char* output_getLanModeName(lanMode_t mode)
{
	switch (mode)
	{
		case lanBridge:     return _T("GATEWAY_BRIDGE");
		case lanStatic:     return _T("LOCAL_NETWORK");
		case lanDhcpServer: return _T("LOCAL_NETWORK_ICS");
		case lanDhcpClient: return _T("ADDR_MODE_DHCP");
		default:            return _T("OFF");
	}
}

void output_setIfaceMenuName(interfaceMenu_t *pMenu, const char *ifaceName, int wan, lanMode_t lanMode)
{
	char name[MENU_ENTRY_INFO_LENGTH];
	size_t len;
	if (wan)
		len = snprintf(name, sizeof(name), "%s - %s", ifaceName, _T("INTERNET_CONNECTION"));
	else
	{
		len  = snprintf(name, sizeof(name), "%s - %s", ifaceName, output_getLanModeName(lanMode));
	}
	interface_setMenuName(pMenu, name, len+1);
}

int  output_isIfaceDhcp(stb810_networkInterface_t iface)
{
#ifdef STBPNX
	char temp[MENU_ENTRY_INFO_LENGTH];
#endif
	switch (iface)
	{
		case ifaceWAN:
#ifdef STBPNX
			getParam(WAN_CONFIG_FILE, "BOOTPROTO", "static", temp);
			return strcasecmp(temp, "static");
#endif
#ifdef STSDK
			return networkInfo.wanDhcp;
#endif
			return 0;
		case ifaceLAN:
			return networkInfo.lanMode == lanDhcpClient;
#ifdef ENABLE_WIFI
		case ifaceWireless:
#ifdef STBPNX
			getParam(WLAN_CONFIG_FILE, "BOOTPROTO", "static", temp);
			return strcasecmp(temp, "static");
#endif
#ifdef STSDK
			return wifiInfo.dhcp;
#endif
			return 0;
#endif // ENABLE_WIFI
#ifdef ENABLE_PPP
		case ifacePPP:
			return 1;
#endif
	}
	return 0;
}

#ifdef ENABLE_MOBILE
static void output_readMobileSettings(void)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	FILE *f = fopen(MOBILE_APN_FILE, "ro");
	if (f) {
		fgets(buf, sizeof(buf), f);
		fclose(f);
		if (sscanf(buf, "AT+CGDCONT=1,\"IP\",\"%[^\"\n\r]\"", mobileInfo.apn) != 1)
			eprintf("%s: failed to get apn from string: '%s'\n", __FUNCTION__, buf);
	}
	f = fopen(MOBILE_PHONE_FILE, "ro");
	if (f) {
		fgets(buf, sizeof(buf), f);
		fclose(f);
		if (sscanf(buf, "ATDT%[^\n\r]", mobileInfo.phone) != 1)
			eprintf("%s: failed to get phone from string: '%s'\n", __FUNCTION__, buf);
	}
}

static int output_writeMobileSettings(void)
{
	int err = 0;
	FILE *f = fopen(MOBILE_APN_FILE, "w");
	if (f) {
		fprintf(f, "AT+CGDCONT=1,\"IP\",\"%s\"", mobileInfo.apn);
		fclose(f);
	} else {
		eprintf("%s: failed to open %s for writing: %s\n", __FUNCTION__, MOBILE_APN_FILE, strerror(errno));
		err = 1;
	}
	f = fopen(MOBILE_PHONE_FILE, "w");
	if (f) {
		fprintf(f, "ATDT%s", mobileInfo.phone);
		fclose(f);
	} else {
		eprintf("%s: failed to open %s for writing: %s\n", __FUNCTION__, MOBILE_APN_FILE, strerror(errno));
		err = 1;
	}
	return err;
}

static char* output_getMobileAPN(int index, void* pArg)
{
	if (index == 0)
		return mobileInfo.apn;
	return NULL;
}

static char* output_getMobilePhone(int index, void* pArg)
{
	if (index == 0)
		return mobileInfo.phone;
	return NULL;
}

static int output_setMobileAPN(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;
	if (value[0] == 0)
		return 0;
	strncpy(mobileInfo.apn, value, sizeof(mobileInfo.apn)-1);
	return output_saveAndRedraw(output_writeMobileSettings(), pMenu);
}

static int output_setMobilePhone(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if (value == NULL)
		return 1;
	if (value[0] == 0)
		return 0;
	strncpy(mobileInfo.phone, value, sizeof(mobileInfo.phone)-1);
	return output_saveAndRedraw(output_writeMobileSettings(), pMenu);
}

static int output_changeMobileAPN(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, "", "\\w+", output_setMobileAPN,   output_getMobileAPN,   inputModeABC, pArg);
}

static int output_changeMobilePhone(interfaceMenu_t *pMenu, void* pArg)
{
	return interface_getText(pMenu, "", "\\w+", output_setMobilePhone, output_getMobilePhone, inputModeABC, pArg);
}

static int output_restartMobile(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 0);

	int timeout = 3;
	int res = system("killall pppd 2> /dev/null");
	while (timeout && WIFEXITED(res) == 1 && WEXITSTATUS(res) == 0) {
		sleep(1);
		res = system("killall pppd 2> /dev/null");
		timeout--;
	}
	system("killall -9 pppd 2> /dev/null");
	system("pppd call mobile-noauth");

	output_refillMenu(pMenu);
	interface_hideMessageBox();
	output_refreshStart(pMenu, pArg);
	return 0;
}

int output_enterMobileMenu(interfaceMenu_t *mobileMenu, void *ignored)
{
	interface_clearMenuEntries(mobileMenu);
	output_readMobileSettings();

	char buf[MENU_ENTRY_INFO_LENGTH];
	snprintf(buf, sizeof(buf), "%s: %s", "APN",   mobileInfo.apn);
	interface_addMenuEntry(mobileMenu, buf, output_changeMobileAPN,   NULL, thumbnail_enterurl);
	snprintf(buf, sizeof(buf), "%s: %s", _T("PHONE_NUMBER"), mobileInfo.phone);
	interface_addMenuEntry(mobileMenu, buf, output_changeMobilePhone, NULL, thumbnail_enterurl);
	interface_addMenuEntry(mobileMenu, _T("APPLY_NETWORK_SETTINGS"), output_restartMobile, NULL, settings_renew);
	char *str = NULL;
	if (helperCheckDirectoryExsists("/sys/class/net/ppp0"))
		str = _T("ON");
	else {
		int res = system("killall -0 pppd 2> /dev/null");
		if (WIFEXITED(res) == 1 && WEXITSTATUS(res) == 0) {
			str = _T("CONNECTING");
			output_refreshStart(mobileMenu, NULL);
		} else
			str = _T("OFF");
	}
	snprintf(buf, sizeof(buf), "%s: %s", _T("MOBILE"), str);
	interface_addMenuEntryDisabled(mobileMenu, buf, thumbnail_info);
	return 0;
}
#endif // ENABLE_MOBILE
