/*
StbMainApp/src/output_network.c

Copyright (C) 2016  Elecard Devices
Anton Sergeev <Anton.Sergeev@elecard.ru>

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

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/

#include "output_network.h"
#include "output.h"
#include "app_info.h"
#include "l10n.h"
#include "rtp.h"
#include "stb_wireless.h"
#include "wpa_ctrl.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#ifdef ENABLE_WIFI
# include <iwlib.h>
#endif

#include <common.h>


/******************************************************************
* LOCAL MACROS                                                    *
*******************************************************************/
#define DATA_BUFF_SIZE              (32*1024)
#define MAX_CONFIG_PATH             (64)

#ifdef STB82
# define PPP_CHAP_SECRETS_FILE       "/etc/ppp/chap-secrets"
#endif
#if defined(STBPNX)
# define XWORKS_INIT_SCRIPT          "/usr/local/etc/init.d/S94xworks"
# define IFACE_CONFIG_PREFIX         "/config/ifcfg-"
# define WAN_CONFIG_FILE             IFACE_CONFIG_PREFIX "eth0"
# define LAN_CONFIG_FILE             IFACE_CONFIG_PREFIX "eth1"
# define WLAN_CONFIG_FILE            IFACE_CONFIG_PREFIX "wlan0"
#elif defined(STSDK)
# define XWORKS_INIT_SCRIPT          "/etc/init.d/S94xworks"
# define PPP_CHAP_SECRETS_FILE       "/var/etc/ppp/chap-secrets"
# define WAN_CONFIG_FILE             "/var/etc/ifcfg-wan"
# define LAN_CONFIG_FILE             "/var/etc/ifcfg-lan"
# define WLAN_CONFIG_FILE            "/var/etc/ifcfg-wlan0"
#endif //#if defined(STBPNX), #elif defined(STSDK)

#define MOBILE_APN_FILE             SYSTEM_CONFIG_DIR "/ppp/chatscripts/apn"
#define MOBILE_PHONE_FILE           SYSTEM_CONFIG_DIR "/ppp/chatscripts/phone"

#define OUTPUT_INFO_SET(type,index) (void*)(intptr_t)(((int32_t)type << 16) | (index))
#define OUTPUT_INFO_GET_TYPE(info)  ((int32_t)(intptr_t)info >> 16)
#define OUTPUT_INFO_GET_INDEX(info) ((int32_t)(intptr_t)info & 0xFFFF)

#if defined(STSDK) && defined(ENABLE_WIFI)
#define USE_WPA_SUPPLICANT
#endif


/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
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
    optionRtpEpg,
    optionRtpPlaylist,
    optionVodPlaylist,
#ifdef ENABLE_TELETES
    optionTeletesPlaylist,
#endif
} outputUrlOption;


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
} eLanMode_t;

typedef enum {
    eIface_unknown = 0,
    eIface_eth0,
    eIface_eth1,
    eIface_wlan0,
    eIface_ppp0,
    eIface_br0,
} eIface_t;

#ifdef ENABLE_PPP
typedef struct
{
    char login[MENU_ENTRY_INFO_LENGTH];
    char password[MENU_ENTRY_INFO_LENGTH];
} pppInfo_t;
#endif

#ifdef STSDK
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
    outputNfaceInfo_t lan;
    int32_t           wanDhcp;
    struct in_addr    dns;
#endif
    eIface_t          wanIface;
    int32_t           wanChanged;
    eLanMode_t        lanMode;
    int32_t           changed;
} outputNetworkInfo_t;


#ifdef ENABLE_WIFI
typedef struct
{
    int32_t enable;
#ifdef STSDK
    outputNfaceInfo_t wlan;
#endif
    int32_t dhcp;
    int32_t channelCount;
    int32_t currentChannel;
    outputWifiMode_t       mode;
    outputWifiAuth_t       auth;
    outputWifiEncryption_t encryption;
    char essid[IW_ESSID_MAX_SIZE];
    char key[IW_ENCODING_TOKEN_MAX+1];
    int32_t showAdvanced;
} outputWifiInfo_t;
#endif


/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int32_t outputNetwork_LANIsBridge(void);

static int32_t output_pingMenu(interfaceMenu_t* pMenu, void* pArg);
static int32_t outputNetwork_leaveMenu(interfaceMenu_t *pMenu, void* notused);
static int32_t output_confirmNetworkSettings(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int32_t output_enterEth0Menu (interfaceMenu_t *wanMenu, void* pArg);
static int32_t output_fillWANMenu(interfaceMenu_t *wanMenu, eVirtIface_t iface);


#ifdef ENABLE_MOBILE
static void    output_readMobileSettings(void);
static int32_t output_writeMobileSettings(void);
static int32_t output_enterMobileMenu (interfaceMenu_t *pMenu, void* ignored);
static int32_t output_changeMobileAPN(interfaceMenu_t *pMenu, void* ignored);
static int32_t output_changeMobilePhone(interfaceMenu_t *pMenu, void* ignored);
#endif // ENABLE_MOBILE

#ifdef ENABLE_PPP
static int32_t output_enterPPPMenu (interfaceMenu_t *pMenu, void* pArg);
#endif
#ifdef ENABLE_ETH1
static int32_t output_enterEth1Menu (interfaceMenu_t *lanMenu, void* pArg);
#endif
static int32_t output_fillLANMenu(interfaceMenu_t *lanMenu, eVirtIface_t iface);
static int32_t output_toggleDhcpServer(interfaceMenu_t *pMenu, void* pForce);
#if defined(STBPNX) && (defined(ENABLE_ETH1) || defined(ENABLE_WIFI))
static int32_t output_confirmGatewayMode(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int32_t output_toggleGatewayMode(interfaceMenu_t *pMenu, void* pArg);
static int32_t output_enterGatewayMenu(interfaceMenu_t *pMenu, void* ignored);
#endif // defined(STBPNX) && (defined(ENABLE_ETH1) || defined(ENABLE_WIFI))

#ifdef ENABLE_WIFI
static int32_t output_readWirelessSettings(void);
static int32_t output_enterWifiMenu (interfaceMenu_t *pMenu, void* ignored);
static int32_t output_wifiKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int32_t output_changeWifiWAN(int32_t switchOn);
static int32_t outputNetwork_WiFipresentInMenu(void);
# ifdef STSDK
static int32_t output_setHostapdChannel(int32_t channel);
# endif
# ifdef USE_WPA_SUPPLICANT
static int32_t output_readWpaSupplicantConf(const char *filename);
static int32_t output_writeWpaSupplicantConf(const char *filename);
# endif
#endif // ENABLE_WIFI

static char*   output_getOption(outputUrlOption option);
static char*   output_getURL(int32_t index, void* pArg);
static int32_t output_setURL(interfaceMenu_t *pMenu, char *value, void* pArg);
static int32_t output_changeURL(interfaceMenu_t *pMenu, void* urlOption);

#ifdef ENABLE_IPTV
static int32_t output_enterIPTVMenu(interfaceMenu_t *pMenu, void* pArg);
static int32_t output_changeIPTVPlaylist(interfaceMenu_t *pMenu, void* pArg);

#ifdef ENABLE_PROVIDER_PROFILES
static int32_t output_enterProfileMenu(interfaceMenu_t *pMenu, void* pArg);
static int32_t output_leaveProfileMenu(interfaceMenu_t *pMenu, void* pArg);
static int32_t output_setProfile(interfaceMenu_t *pMenu, void* pArg);
#endif

#endif // ENABLE_IPTV
#ifdef ENABLE_VOD
static int32_t output_enterVODMenu (interfaceMenu_t *pMenu, void* pArg);
static int32_t output_toggleVODPlaylist(interfaceMenu_t *pMenu, void* pArg);
#endif
static int32_t output_enterWebMenu (interfaceMenu_t *pMenu, void* pArg);

static const char* output_getLanModeName(eLanMode_t mode);
static void        output_setIfaceMenuName(interfaceMenu_t *pMenu, const char *ifaceName, int32_t wan, eLanMode_t lanMode);
static int32_t     output_isIfaceDhcp(eVirtIface_t iface);

#if (defined ENABLE_PPP) || (defined ENABLE_MOBILE)
static void*   output_refreshLoop(void *pMenu);
static int32_t output_refreshStop(interfaceMenu_t *pMenu, void *pArg);
static int32_t output_refreshStart(interfaceMenu_t *pMenu, void *pArg);
#endif

#ifdef STSDK
static int32_t output_writeInterfacesFile(void);
static int32_t output_writeDhcpConfig(void);
#endif

static const char *outputNetwork_WANIfaceLabel(eIface_t mode);


/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static outputNetworkInfo_t networkInfo;
#ifdef ENABLE_WIFI
static outputWifiInfo_t wifiInfo;
#endif
#ifdef ENABLE_PPP
static pppInfo_t pppInfo;
#endif

static interfaceListMenu_t NetworkSubMenu;
static interfaceListMenu_t Eth0SubMenu;
#ifdef ENABLE_PPP
static interfaceListMenu_t PPPSubMenu;
#endif
#ifdef ENABLE_ETH1
static interfaceListMenu_t Eth1SubMenu;
#endif

#if defined(STBPNX) && (defined(ENABLE_ETH1) || defined(ENABLE_WIFI))
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
static int32_t             output_profiles_count = 0;
static char            output_profile[MAX_PROFILE_PATH] = {0};
static interfaceListMenu_t ProfileMenu;
#endif
#endif // ENABLE_IPTV

#ifdef ENABLE_VOD
static interfaceListMenu_t VODSubMenu;
#endif
static interfaceListMenu_t WebSubMenu;


#ifdef ENABLE_MOBILE
static interfaceListMenu_t MobileMenu;
static struct {
    char apn  [MENU_ENTRY_INFO_LENGTH];
    char phone[MENU_ENTRY_INFO_LENGTH];
} mobileInfo = {
    .apn     = {0},
    .phone   = {0},
};
#endif // ENABLE_MOBILE

#if defined(ENABLE_PPP) || defined(ENABLE_MOBILE)
static pthread_t output_refreshThread = 0;
#endif

static char output_ip[4*4];

table_IntStr_t ifaceNames[] = {
    {eIface_eth0,  "eth0"},
    {eIface_eth1,  "eth1"},
    {eIface_wlan0, "wlan0"},
    {eIface_ppp0,  "ppp0"},
    {eIface_br0,   "br0"},
};

struct wanMode {
    eIface_t mode;
    char      *name;
    char      *l10n_label;
    int32_t  (*change)(int32_t enable);
    int32_t  (*presentInMenu)(void);
} wanModes[] = {
    {eIface_eth0,    "Ethernet", NULL,       NULL, NULL},
#ifdef ENABLE_WIFI
    {eIface_wlan0,   "WiFi",     "WIRELESS", &output_changeWifiWAN, &outputNetwork_WiFipresentInMenu},
#endif
#ifdef ENABLE_WAN_BRIDGE
    {eIface_br0,     "Bridge",   NULL,       NULL, NULL},
#endif
};



/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>_<Word>+    *
*******************************************************************/
static int32_t output_checkIfaceExists(eIface_t iface)
{
    char buf[64];
    const char *iface_name = table_IntStrLookup(ifaceNames, iface, "");

    snprintf(buf, sizeof(buf), "/sys/class/net/%s", iface_name);
    return helperCheckDirectoryExsists(buf);
}

#ifdef ENABLE_WIFI
int32_t output_setESSID(interfaceMenu_t *pMenu, char *value, void* pArg)
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
    if(networkInfo.wanIface == eIface_wlan0) {
        output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
    } else {
        output_warnIfFailed(setParam(STB_HOSTAPD_CONF, "ssid", value));
    }
#endif // STSDK
    networkInfo.changed = 1;
    output_refillMenu(pMenu);
    return 0;
}

static char* output_getESSID(int32_t index, void* pArg)
{
    return index == 0 ? wifiInfo.essid : NULL;
}

static int32_t output_changeESSID(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("ENTER_ESSID"), "\\w+", output_setESSID, output_getESSID, inputModeABC, pArg );
}

static int32_t output_setWifiChannel(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if (value == NULL)
        return 1;

    int32_t channel = strtol( value, NULL, 10 );
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

static char* output_getWifiChannel(int32_t index, void* pArg)
{
    if( index == 0 )
    {
        static char temp[8];
        snprintf(temp, sizeof(temp), "%02d", wifiInfo.currentChannel);
        return temp;
    } else
        return NULL;
}

static int32_t output_changeWifiChannel(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("ENTER_WIFI_CHANNEL"), "\\d{2}", output_setWifiChannel, output_getWifiChannel, inputModeDirect, pArg );
}

static int32_t output_toggleAuthMode(interfaceMenu_t *pMenu, void* pArg)
{
    outputWifiAuth_t maxAuth = wifiInfo.mode == wifiModeAdHoc ? wifiAuthWEP+1 : wifiAuthCount;
    return output_setAuthMode(pMenu, (void*)((wifiInfo.auth+1)%maxAuth));
}

int32_t output_setAuthMode(interfaceMenu_t *pMenu, void* pArg)
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

    int32_t show_error = 0;
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
        strncpy(wifiInfo.key, "0102030405", sizeof(wifiInfo.key));

    show_error = output_writeInterfacesFile();
#endif
    networkInfo.changed = 1;
    return output_saveAndRedraw(show_error, pMenu);
}

static int32_t output_toggleWifiEncryption(interfaceMenu_t *pMenu, void* pArg)
{
    return output_setWifiEncryption(pMenu, SET_NUMBER((wifiInfo.encryption+1)%wifiEncCount));
}

int32_t output_setWifiEncryption(interfaceMenu_t *pMenu, void* pArg)
{
    wifiInfo.encryption = GET_NUMBER(pArg);

    int32_t show_error = 0;
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

static int32_t output_setWifiKey(interfaceMenu_t *pMenu, char *value, void* pArg)
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
            int32_t j;
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

static char* output_getWifiKey(int32_t index, void* pArg)
{
    return index == 0 ? wifiInfo.key : NULL;
}

/**
* @param[in] pArg Pointer to menuActionFunction, which will be called after successfull password change.
* Callback will be called as pArg(pMenu, SET_NUMBER(ifaceWireless)). Can be NULL.
*/
int32_t output_changeWifiKey(interfaceMenu_t *pMenu, void* pArg)
{
    if( wifiInfo.auth == wifiAuthWEP )
        return interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\d{10}", output_setWifiKey, output_getWifiKey, inputModeDirect, pArg );
    else
        return interface_getText(pMenu, _T("ENTER_PASSWORD"), "\\w+", output_setWifiKey, output_getWifiKey, inputModeABC, pArg );
}

static int32_t output_changeWifiWAN(int32_t switchOn)
{
    if(switchOn) {
        wifiInfo.enable  = 1;
    }

#ifdef STBPNX
    if (!switchOn)
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
#endif
#ifdef STSDK
    if (switchOn)
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
#endif

    return 0;
}

static void outputNetwork_showWireless(const char *iface_name, char *info_text)
{
    char temp[MENU_ENTRY_INFO_LENGTH];

    snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("ESSID"), wifiInfo.essid);
    strcat(info_text, temp);
    snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("MODE"), wireless_mode_print( wifiInfo.mode ));
    strcat(info_text, temp);
    if(networkInfo.wanIface != eIface_wlan0)
    {
        snprintf(temp, sizeof(temp), "%s %s: %d\n", iface_name, _T("CHANNEL_NUMBER"), wifiInfo.currentChannel );
        strcat(info_text, temp);
        snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("AUTHENTICATION"), wireless_auth_print( wifiInfo.auth ));
        strcat(info_text, temp);
        snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("ENCRYPTION"), wireless_encr_print( wifiInfo.encryption ));
        strcat(info_text, temp);
    }
}

static int32_t output_checkWiFiExists(void)
{
#if defined(STB225)
    return 1; // don't check
#else
    return output_checkIfaceExists(eIface_wlan0);
#endif
}

static int32_t outputNetwork_WiFipresentInMenu(void)
{
    if(output_checkWiFiExists() || (networkInfo.wanIface == eIface_wlan0)) {
        return 1;
    }
    return 0;
}

static int32_t output_toggleWifiEnable(interfaceMenu_t *pMenu, void* pArg)
{
    int32_t show_error = 0;

    wifiInfo.enable = !wifiInfo.enable;
    show_error = setParam(WLAN_CONFIG_FILE, "ENABLE_WIRELESS", wifiInfo.enable ? "1" : "0");
    networkInfo.changed = 1;
    return output_saveAndRedraw(show_error, pMenu);
}

#ifdef STBPNX
static int32_t output_toggleWifiMode(interfaceMenu_t *pMenu, void* pArg)
{
    return output_setWifiMode( pMenu, (void*)((wifiInfo.mode+1)%wifiModeCount));
}
#endif

int32_t output_setWifiMode(interfaceMenu_t *pMenu, void* pArg)
{
    wifiInfo.mode = (outputWifiMode_t)pArg;

    int32_t show_error = 0;
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

static void output_parseIP(char *value)
{
    int32_t i;
    int32_t ip_index, j;
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

static int32_t output_getIP(eVirtIface_t iface, outputIPOption type, char value[MENU_ENTRY_INFO_LENGTH])
{
    int32_t ret = 0;
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
    snprintf(path, sizeof(path), IFACE_CONFIG_PREFIX "%s", outputNetwork_virtIfaceName(iface));
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

static void output_initIPfield(outputIPOption type, eVirtIface_t iface)
{
    char value[MENU_ENTRY_INFO_LENGTH];
    if (output_getIP(iface, type, value) == 0)
    {
        output_parseIP(value);
    }
    else
    {
        int32_t i;
        memset(&output_ip, 0, sizeof(output_ip));
        for (i = 0; i<16; i+=4)
            output_ip[i] = '0';
    }
}

static char* output_getIPfield(int32_t field, void* pArg)
{
    return field < 4 ? &output_ip[field*4] : NULL;
}

static int32_t output_setIP(interfaceMenu_t *pMenu, char *value, void* pOptionIface)
{
    in_addr_t ip;
    int32_t i = OUTPUT_INFO_GET_INDEX(pOptionIface);
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

    int32_t show_error = 0;
#ifdef STBPNX
    char path[MAX_CONFIG_PATH];
    char *key = "";

    snprintf(path, sizeof(path), IFACE_CONFIG_PREFIX "%s", outputNetwork_virtIfaceName(i));

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
        //case optionMode:
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

static int32_t output_changeIP(interfaceMenu_t *pMenu, void* pArg)
{
    output_initIPfield(optionIP,GET_NUMBER(pArg));
    return interface_getText(pMenu, _T("ENTER_DEFAULT_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionIP,GET_NUMBER(pArg)) );
}

static int32_t output_changeGw(interfaceMenu_t *pMenu, void* pArg)
{
    output_initIPfield(optionGW,GET_NUMBER(pArg));
    return interface_getText(pMenu, _T("ENTER_GATEWAY"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionGW,GET_NUMBER(pArg)) );
}

static int32_t output_changeDNS(interfaceMenu_t *pMenu, void* pArg)
{
    output_initIPfield(optionDNS,GET_NUMBER(pArg));
    return interface_getText(pMenu, _T("ENTER_DNS_ADDRESS"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionDNS,GET_NUMBER(pArg)) );
}

static int32_t output_changeNetmask(interfaceMenu_t *pMenu, void* pArg)
{
    output_initIPfield(optionMask,GET_NUMBER(pArg));
    return interface_getText(pMenu, _T("ENTER_NETMASK"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setIP, output_getIPfield, inputModeDirect, OUTPUT_INFO_SET(optionMask,GET_NUMBER(pArg)) );
}

static int32_t output_PPPstart()
{
#ifdef ENABLE_PPP
    system("/etc/init.d/S65ppp start");
#endif
    return 0;
}

static int32_t output_PPPstop()
{
#ifdef ENABLE_PPP
    system("/etc/init.d/S65ppp stop");
#endif
    return 0;
}

#ifdef STBPNX
#ifdef ENABLE_ETH1
static char *output_getBandwidth(int32_t field, void* pArg)
{
    if( field == 0 )
    {
        static char BWValue[MENU_ENTRY_INFO_LENGTH];
        getParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", "0", BWValue);
        return BWValue;
    } else
        return NULL;
}

// static char *output_getMAC(int32_t field, void* pArg)
// {
//     int32_t i;
//     char *ptr;
//     static char MACValue[MENU_ENTRY_INFO_LENGTH];
//     getParam(STB_CONFIG_FILE, "CONFIG_GATEWAY_CLIENT_MAC", "", MACValue);
//
//     ptr = MACValue;
//     for (i=0;i<field;i++)
//     {
//         ptr = strchr(ptr, ':');
//         if (ptr == NULL)
//         {
//             return NULL;
//         }
//         ptr++;
//     }
//
//     ptr[2] = 0;
//
//     return ptr;
// }

static int32_t output_setBandwidth(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    int32_t ivalue;
    char buf[32];

    if (value == NULL)
        return 1;

    if (helperFileExists(STB_CONFIG_OVERRIDE_FILE))
        return 0;

    interface_showMessageBox(_T("GATEWAY_IN_PROGRESS"), settings_renew, 0);

    ivalue = atoi(value);
    if (value[0] == 0 || ivalue <= 0) {
        buf[0] = 0;
    } else {
        snprintf(buf, sizeof(buf), "%d", ivalue);
    }

    // Stop network interfaces
    system("/usr/local/etc/init.d/S90dhcpd stop");
    // Update settings
    setParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", buf);
    // Start network interfaces
    system("/usr/local/etc/init.d/S90dhcpd start");

    output_refillMenu(pMenu);
    return 0;
}

static int32_t output_changeGatewayBandwidth(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("GATEWAY_BANDWIDTH_INPUT"), "\\d*", output_setBandwidth, output_getBandwidth, inputModeDirect, pArg);
}
#endif // ENABLE_ETH1

#if (defined ENABLE_ETH1) || (defined ENABLE_WIFI)
static int32_t output_confirmGatewayMode(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
    eLanMode_t mode = GET_NUMBER(pArg);

    if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
    {
        return 0;
    } else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
    {
        if (helperFileExists(STB_CONFIG_OVERRIDE_FILE))
        {
            return 0;
        }
        if (mode >= lanModeCount )
        {
            return 0;
        }
        interface_showMessageBox(_T("GATEWAY_IN_PROGRESS"), settings_renew, 0);
        char *str = "";
        switch (mode) {
            case lanBridge:     str = "BRIDGE"; break;
            case lanStatic:     str = "NAT"; break;
            case lanDhcpServer: str = "FULL"; break;
            default:            str = "OFF"; break;
        }
        networkInfo.lanMode = mode;

        // Stop network interfaces
#ifdef ENABLE_WIFI
        system("/usr/local/etc/init.d/S80wifi stop");
#endif
        system("/usr/local/etc/init.d/S90dhcpd stop");
        system("/etc/init.d/S70servers stop");
        output_PPPstop();
        system("/etc/init.d/S19network stop");
        // Update settings
        setParam(STB_CONFIG_FILE, "CONFIG_GATEWAY_MODE", str);
        // Start network interfaces
        system("/etc/init.d/S19network start");
        output_PPPstart();
        system("/etc/init.d/S70servers start");
        system("/usr/local/etc/init.d/S90dhcpd start");
#ifdef ENABLE_WIFI
        system("/usr/local/etc/init.d/S80wifi start");
#endif
//-----------------------------------------------------------------//

        networkInfo.changed = 0;
        interface_hideMessageBox();
        output_redrawMenu(pMenu);

        return 0;
    }
    return 1;
}

static int32_t output_toggleGatewayMode(interfaceMenu_t *pMenu, void* pArg)
{
    interface_showConfirmationBox(_T("GATEWAY_MODE_CONFIRM"), thumbnail_question, output_confirmGatewayMode, pArg);
    return 1;
}

static int32_t output_enterGatewayMenu(interfaceMenu_t *gatewayMenu, void* ignored)
{
    interface_clearMenuEntries(gatewayMenu);
    for (eLanMode_t mode = 0; mode < lanModeCount; mode++)
        interface_addMenuEntry(gatewayMenu, output_getLanModeName(mode),
            mode == networkInfo.lanMode ? NULL : output_toggleGatewayMode, (void*)mode,
            mode == networkInfo.lanMode ? radiobtn_filled : radiobtn_empty);
    return 0;
}
#endif // ENABLE_ETH1 || ENABLE_WIFI
#endif // STBPNX

static int32_t output_applyNetworkSettings(interfaceMenu_t *pMenu, void* pArg)
{
    interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 0);

#ifdef STBPNX
    char buf[PATH_MAX];
    int32_t i = GET_NUMBER(pArg);
    switch(i)
    {
        case ifaceWAN:
#if !(defined STB225)
            snprintf(buf, sizeof(buf), "/usr/sbin/ifdown %s", outputNetwork_virtIfaceName(i));
            system(buf);

            sleep(1);

            snprintf(buf, sizeof(buf), "/usr/sbin/ifup %s", outputNetwork_virtIfaceName(i));
#else
            strncpy(buf, "/etc/init.d/additional/dhcp.sh", sizeof(buf));
#endif
            system(buf);
            break;
#ifdef ENABLE_ETH1
        case ifaceLAN:
            system("/usr/local/etc/init.d/S90dhcpd stop");
            sleep(1);
            system("/usr/local/etc/init.d/S90dhcpd start");
            break;
#endif
#ifdef ENABLE_WIFI
        case ifaceWireless:
            gfx_stopVideoProviders(screenMain);
#ifdef USE_WPA_SUPPLICANT
            if (networkInfo.wanIface == eIface_wlan0)
            {
                output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
            }
#endif
#if (defined STB225)
            strncpy(buf, "/etc/init.d/additional/wifi.sh stop", sizeof(buf));
#else
            strncpy(buf, "/usr/local/etc/init.d/S80wifi stop", sizeof(buf));
#endif
            system(buf);

            sleep(1);

#if (defined STB225)
            snprintf(buf, sizeof(buf), "/etc/init.d/additional/wifi.sh start");
#else
            snprintf(buf, sizeof(buf), "/usr/local/etc/init.d/S80wifi start");
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
# ifdef ENABLE_WIFI
    if (ifaceWireless == GET_NUMBER(pArg) &&
        networkInfo.lanMode != lanBridge &&
        networkInfo.wanChanged == 0)
    {
        if(networkInfo.wanIface == eIface_wlan0)
        {

            output_PPPstop();
        }
        system("ifdown wlan0");
        output_writeDhcpConfig();
        system("ifcfg config > " NETWORK_INTERFACES_FILE);
        system("ifup wlan0");
        if(networkInfo.wanIface == eIface_wlan0)
        {
            output_PPPstart();
        }
    } else
# endif
    {
        output_PPPstop();
        system("/etc/init.d/S40network stop");
        output_writeDhcpConfig();
        system("ifcfg config > " NETWORK_INTERFACES_FILE);
        sleep(1);
        system("/etc/init.d/S40network start");
        output_PPPstart();
    }
#endif // STSDK

    networkInfo.wanChanged = 0;
    networkInfo.changed = 0;
    output_refillMenu(pMenu);
    interface_hideMessageBox();

    return 0;
}

static int32_t output_setProxyAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    char *ptr1, *ptr2;
    char buf[MENU_ENTRY_INFO_LENGTH];
    int32_t ret = 0;
    int32_t port, i;

    if( value == NULL )
        return 1;

    strncpy(buf, value, sizeof(buf));

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
        strncpy(appControlInfo.networkInfo.proxy, ptr2 + 7, sizeof(appControlInfo.networkInfo.proxy));
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



static int32_t outputNetwork_leaveMenu(interfaceMenu_t *pMenu, void *pArg)
{
    (void)pArg;
    if (networkInfo.changed)
    {
        interface_showConfirmationBox(_T("CONFIRM_NETWORK_SETTINGS"), thumbnail_question, output_confirmNetworkSettings, NULL);
        return 1;
    }
    return 0;
}

static int32_t output_confirmNetworkSettings(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
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

static int32_t output_toggleIPMode(interfaceMenu_t *pMenu, void* pArg)
{
    int32_t i = GET_NUMBER(pArg);
    int32_t ret = 0;

    (void)i; //hide "unused variable" warnings
#ifdef STBPNX
    char value[MENU_ENTRY_INFO_LENGTH];
    char path[MAX_CONFIG_PATH];

    snprintf(path, sizeof(path), IFACE_CONFIG_PREFIX "%s", outputNetwork_virtIfaceName(i));
    getParam(path, "BOOTPROTO", "static", value);

    if (strcmp("dhcp+dns", value) == 0)
    {
        strncpy(value, "static", sizeof(value));
    } else
    {
        strncpy(value, "dhcp+dns", sizeof(value));
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

static int32_t output_fillWANMenu(interfaceMenu_t *wanMenu, eVirtIface_t iface)
{
    char  buf[MENU_ENTRY_INFO_LENGTH];
    char temp[MENU_ENTRY_INFO_LENGTH];
    int32_t dhcp = 0;

    dhcp = output_isIfaceDhcp(iface);
    strcpy(temp, _T( dhcp ? "ADDR_MODE_DHCP" : "ADDR_MODE_STATIC" ));
    snprintf(buf, sizeof(buf), "%s: %s", _T("ADDR_MODE"), temp);
    interface_addMenuEntry(wanMenu, buf, output_toggleIPMode, SET_NUMBER(iface), thumbnail_configure);

    char *not_available = _T("NOT_AVAILABLE_SHORT");
    if (dhcp == 0)
    {
        int32_t ret;

        ret = output_getIP(iface, optionIP, temp);
        snprintf(buf, sizeof(buf), "%s: %s",  _T("IP_ADDRESS"), ret ? not_available : temp);
        interface_addMenuEntry(wanMenu, buf, output_changeIP, SET_NUMBER(iface), thumbnail_configure);

        ret = output_getIP(iface, optionMask, temp);
        snprintf(buf, sizeof(buf), "%s: %s", _T("NETMASK"),     ret ? not_available : temp);
        interface_addMenuEntry(wanMenu, buf, output_changeNetmask, SET_NUMBER(iface), thumbnail_configure);

        ret = output_getIP(iface, optionGW, temp);
        snprintf(buf, sizeof(buf), "%s: %s", _T("GATEWAY"),     ret ? not_available : temp);
        interface_addMenuEntry(wanMenu, buf, output_changeGw, SET_NUMBER(iface), thumbnail_configure);

        ret = output_getIP(iface, optionDNS, temp);
        snprintf(buf, sizeof(buf), "%s: %s", _T("DNS_SERVER"),  ret ? not_available : temp);
        interface_addMenuEntry(wanMenu, buf, output_changeDNS, SET_NUMBER(iface), thumbnail_configure);
    }
    else
    {
        char path[MAX_CONFIG_PATH];
        const char *ifaceName = outputNetwork_virtIfaceName(iface);
        snprintf(path, sizeof(path), "ifconfig %s | grep \"inet addr\"", ifaceName);
        if (!helperParseLine(INFO_TEMP_FILE, path, "inet addr:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
            strncpy(temp, not_available, sizeof(temp));
        snprintf(buf, sizeof(buf), "%s: %s", _T("IP_ADDRESS"), temp);
        interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);

        snprintf(path, sizeof(path), "ifconfig %s | grep \"Mask:\"", ifaceName);
        if (!helperParseLine(INFO_TEMP_FILE, path, "Mask:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
            strncpy(temp, not_available, sizeof(temp));
        snprintf(buf, sizeof(buf), "%s: %s", _T("NETMASK"), temp);
        interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);

        snprintf(path, sizeof(path), "route -n | grep -e \"0\\.0\\.0\\.0 .* 0\\.0\\.0\\.0 *UG .* %s\"", ifaceName);
        if (!helperParseLine(INFO_TEMP_FILE, path, "0.0.0.0", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
            strncpy(temp, not_available, sizeof(temp));
        snprintf(buf, sizeof(buf), "%s: %s", _T("GATEWAY"), temp);
        interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);

        int32_t dns_found = 0;
        int32_t dns_fd = open( "/etc/resolv.conf", O_RDONLY );
        if (dns_fd > 0)
        {
            char *ptr;
            while( helperReadLine( dns_fd, temp ) == 0 && temp[0] )
            {
                if( (ptr = strstr( temp, "nameserver " )) != NULL )
                {
                    ptr += 11;
                    dns_found++;
                    snprintf(buf, sizeof(buf), "%s %d: %s", _T("DNS_SERVER"), dns_found, ptr);
                    interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);
                }
            }
            close(dns_fd);
        }
        if (!dns_found)
        {
            snprintf(buf, sizeof(buf), "%s: %s", _T("DNS_SERVER"), not_available);
            interface_addMenuEntryDisabled(wanMenu, buf, thumbnail_configure);
        }
    }

    return 0;
}

static int32_t output_enterEth0Menu(interfaceMenu_t *wanMenu, void *pArg)
{
    int32_t wan = 0;
    const char *iface_name;
    (void)pArg;

    if(networkInfo.wanIface == eIface_br0) {
        iface_name = outputNetwork_WANIfaceLabel(eIface_br0);
        wan = 1;
    } else {
        iface_name = outputNetwork_WANIfaceLabel(eIface_eth0);
        if(networkInfo.wanIface == eIface_eth0) {
            wan = 1;
        }
    }

//     "Ethernet"
    output_setIfaceMenuName(wanMenu, iface_name, wan, networkInfo.lanMode);
    interface_clearMenuEntries(wanMenu);

    if(wan) {
        output_fillWANMenu(wanMenu, ifaceWAN);
    } else {
# ifdef STBPNX
        interface_addMenuEntryDisabled(wanMenu, _T("NOT_AVAILABLE"), thumbnail_info);
# else
        output_fillLANMenu(wanMenu, ifaceLAN);
# endif
    }

    interface_addMenuEntry(wanMenu, _T("APPLY_NETWORK_SETTINGS"), output_applyNetworkSettings, SET_NUMBER(ifaceWAN), settings_renew);

#ifndef HIDE_EXTRA_FUNCTIONS
    char temp[MENU_ENTRY_INFO_LENGTH];
    int32_t offset = snprintf(temp, sizeof(temp), "%s: ",  _T("MAC_ADDRESS"));
    int32_t mac_fd = open("/sys/class/net/eth0/address", O_RDONLY);
    if (mac_fd > 0)
    {
        int32_t len = read(mac_fd, temp+offset, sizeof(temp)-offset);
        if (len <= 0) len = 1;
        temp[offset+len-1] = 0;
        close(mac_fd);
    } else
        strcpy(temp+offset, _T("NOT_AVAILABLE_SHORT"));
    interface_addMenuEntryDisabled(wanMenu, temp, thumbnail_configure);
#endif
    return 0;
}

#if (defined ENABLE_PPP) || (defined ENABLE_MOBILE)
static void* output_refreshLoop(void *pMenu)
{
    for (;;)
    {
        sleep(2);
        output_redrawMenu(pMenu);
    }
    pthread_exit(NULL);
}

static int32_t output_refreshStart(interfaceMenu_t* pMenu, void* pArg)
{
    int32_t err = 0;
    if (output_refreshThread == 0) {
        err = pthread_create( &output_refreshThread, NULL, output_refreshLoop, pMenu );
        if (err != 0) {
            eprintf("%s: failed to start refresh thread: %s\n", __FUNCTION__, strerror(-err));
            output_refreshThread = 0;
        }
    }
    return err;
}

static int32_t output_refreshStop(interfaceMenu_t* pMenu, void* pArg)
{
    if (output_refreshThread != 0) {
        pthread_cancel(output_refreshThread);
        pthread_join  (output_refreshThread, NULL);
        output_refreshThread = 0;
    }
    return 0;
}
#endif

#ifdef ENABLE_PPP
static char* output_getPPPPassword(int32_t field, void* pArg)
{
    return field == 0 ? pppInfo.password : NULL;
}

static char* output_getPPPLogin(int32_t field, void* pArg)
{
    return field == 0 ? pppInfo.login : NULL;
}

static int32_t output_setPPPPassword(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if (value == NULL )
        return 1;

    strncpy(pppInfo.password, value , sizeof(pppInfo.password));

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

static int32_t output_changePPPPassword(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText( pMenu, _T("PASSWORD"), "\\w+", output_setPPPPassword, output_getPPPPassword, inputModeABC, NULL );
}

static int32_t output_setPPPLogin(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if (value == NULL )
        return 1;

    if (value[0] == 0)
    {
        system("rm -f " PPP_CHAP_SECRETS_FILE);
        output_refillMenu(pMenu);
        return 0;
    }
    strncpy(pppInfo.login, value, sizeof(pppInfo.login));

    output_changePPPPassword(pMenu, pArg);
    return 1; // don't hide message box
}

static int32_t output_changePPPLogin(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText( pMenu, _T("LOGIN"), "\\w+", output_setPPPLogin, output_getPPPLogin, inputModeABC, NULL );
}

static int32_t output_PPPrestart(interfaceMenu_t *pMenu, void* pArg)
{
    interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 0);

    output_PPPstart();
    output_PPPstop();

    output_refillMenu(pMenu);
    interface_hideMessageBox();

    output_refreshStart(pMenu, pArg);

    return 0;
}

static int32_t output_enterPPPMenu(interfaceMenu_t *pMenu, void* pArg)
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

    interface_addMenuEntry(pMenu, _T("APPLY_NETWORK_SETTINGS"), output_PPPrestart, NULL, settings_renew);


    if(output_checkIfaceExists(eIface_ppp0))
    {
        str = _T("ON");
    }
    else
    {
        int32_t res;
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

#ifdef ENABLE_ETH1
static int32_t output_enterEth1Menu(interfaceMenu_t *lanMenu, void *)
{
    output_setIfaceMenuName(lanMenu, "Ethernet 2", 0, networkInfo.lanMode);

    interface_clearMenuEntries(lanMenu);
    output_fillLANMenu(lanMenu, ifaceLAN);
    interface_addMenuEntry(lanMenu, _T("APPLY_NETWORK_SETTINGS"), output_applyNetworkSettings, SET_NUMBER(ifaceLAN), settings_renew);
    return 0;
}
#endif

#if (defined STSDK) && (defined ENABLE_WIFI)
static int32_t output_confirmDhcpServerEnable(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* ignored)
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


static int32_t output_toggleDhcpServer(interfaceMenu_t *pMenu, void* pForce)
{
#if defined(STBPNX)
    return output_toggleGatewayMode(pMenu, SET_NUMBER(networkInfo.lanMode == lanDhcpServer ? lanStatic : lanDhcpServer));
#elif defined(STSDK)
# ifdef ENABLE_WIFI
    if((networkInfo.wanIface == eIface_wlan0)
    && (networkInfo.lanMode != lanDhcpServer)
    && (pForce == NULL))
    {
        interface_showConfirmationBox(_T("CONFIRM_DHCP_ENABLE"), thumbnail_warning, output_confirmDhcpServerEnable, NULL);
        return 1;
    }
# endif
    networkInfo.lanMode = networkInfo.lanMode == lanDhcpServer ? lanStatic : lanDhcpServer;
    networkInfo.changed = 1;
    output_writeDhcpConfig();
    return output_saveAndRedraw(output_writeInterfacesFile(), pMenu);
#endif // STSDK
    return 0;
}

static int32_t output_fillLANMenu(interfaceMenu_t *lanMenu, eVirtIface_t iface)
{
    char buf[MENU_ENTRY_INFO_LENGTH];

    if (!outputNetwork_LANIsBridge())
    {
#ifdef STBPNX
        char temp[MENU_ENTRY_INFO_LENGTH];
        char *path = LAN_CONFIG_FILE;
#ifdef ENABLE_WIFI
        if (iface == ifaceWireless)
            path = WLAN_CONFIG_FILE;
#endif
        getParam(path, "IPADDR", _T("NOT_AVAILABLE_SHORT"), temp);
        snprintf(buf, sizeof(buf), "%s: %s", _T("IP_ADDRESS"), temp);
#endif
#ifdef STSDK
        snprintf(buf, sizeof(buf), "%s: %s", _T("IP_ADDRESS"),
            networkInfo.lan.ip.s_addr != 0 ? inet_ntoa(networkInfo.lan.ip) : _T("NOT_AVAILABLE_SHORT"));
#endif
        interface_addMenuEntry(lanMenu, buf, output_changeIP, SET_NUMBER(iface), thumbnail_configure);
    } else
    {
        snprintf(buf, sizeof(buf), "%s: %s", _T("IP_ADDRESS"), _T("GATEWAY_BRIDGE"));
        interface_addMenuEntryDisabled(lanMenu, buf, thumbnail_configure);
    }

#ifdef STBPNX
    if (!helperFileExists(STB_CONFIG_OVERRIDE_FILE))
    {
        snprintf(buf, sizeof(buf), "%s: %s", _T("GATEWAY_MODE"), output_getLanModeName(networkInfo.lanMode));
        interface_addMenuEntry(lanMenu, buf, interface_menuActionShowMenu, &GatewaySubMenu, thumbnail_configure);
        if (networkInfo.lanMode != lanStatic)
        {
            char temp[MENU_ENTRY_INFO_LENGTH];
            getParam(STB_CONFIG_FILE, "CONFIG_TRAFFIC_SHAPE", "0", temp);
            snprintf(buf, sizeof(buf), "%s: %s %s", _T("GATEWAY_BANDWIDTH"), atoi(temp) <= 0 ? _T("NONE") : temp, atoi(temp) <= 0 ? "" : _T("KBPS"));
            interface_addMenuEntry(lanMenu, buf, output_changeGatewayBandwidth, (void*)0, thumbnail_configure);
        }
    }
#endif // STBPNX

    snprintf(buf, sizeof(buf), "%s: %s", _T("INTERNET_CONNECTION_SHARING"), _T(networkInfo.lanMode == lanDhcpServer ? "YES" : "NO") );
    interface_addMenuEntry(lanMenu, buf, output_toggleDhcpServer, NULL, thumbnail_configure);

    return 0;
}

#ifdef ENABLE_WIFI
#ifdef HIDE_EXTRA_FUNCTIONS
static int32_t output_wifiToggleAdvanced(interfaceMenu_t *pMenu, void* pIndex)
{
    wifiInfo.showAdvanced = !wifiInfo.showAdvanced;
    if (!wifiInfo.showAdvanced)
        interface_setSelectedItem(pMenu, GET_NUMBER(pIndex)); // jump to toggle advanced entry
    output_redrawMenu(pMenu);
    return 0;
}
#endif

static int32_t output_enterWifiMenu(interfaceMenu_t *wifiMenu, void* pArg)
{
    char  buf[MENU_ENTRY_INFO_LENGTH];
    int32_t exists;
    char *str;
    const int32_t i = ifaceWireless;
    char *iface_name  = _T("WIRELESS");
    int32_t wan = (networkInfo.wanIface == eIface_wlan0) ? 1 : 0;

    // assert (wifiMenu == wifiMenu);
    exists = output_checkWiFiExists();
    output_readWirelessSettings();

    if (wifiInfo.enable) {
        output_setIfaceMenuName(wifiMenu, iface_name, wan, networkInfo.lanMode);
    } else {
        size_t len = snprintf(buf, sizeof(buf), "%s: %s", iface_name, _T("OFF"));
        interface_setMenuName(wifiMenu, buf, len+1);
    }

    interface_clearMenuEntries(wifiMenu);

    snprintf(buf, sizeof(buf), "%s: %s", iface_name, _T(wifiInfo.enable ? "ON" : "OFF"));
    interface_addMenuEntry(wifiMenu, buf, output_toggleWifiEnable, NULL, thumbnail_configure);

    if (!exists)
    {
        snprintf(buf, sizeof(buf), "%s: %s", iface_name, _T("NOT_AVAILABLE"));
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
    if (wan)
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
    }
    else
    {
        snprintf(buf, sizeof(buf), "%s: %s", _T("ESSID"), wifiInfo.essid);
        interface_addMenuEntry(wifiMenu, buf, output_changeESSID, SET_NUMBER(i), thumbnail_enterurl);

        if (wifiInfo.auth != wifiAuthOpen && wifiInfo.auth < wifiAuthCount)
        {
            snprintf(buf, sizeof(buf), "%s: %s", _T("PASSWORD"), wifiInfo.key );
            interface_addMenuEntry(wifiMenu, buf, output_changeWifiKey, NULL, thumbnail_enterurl);
        }
    }

    // IP settings
    if (wan)
    {
        output_fillWANMenu(wifiMenu, i);
    }
    else
    {
#ifdef STBPNX
        output_fillLANMenu(wifiMenu, i);
#else
        output_fillLANMenu(wifiMenu, ifaceLAN);
#endif
    }

#ifdef HIDE_EXTRA_FUNCTIONS
    int32_t basicOptionsCount = interface_getMenuEntryCount(wifiMenu);
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

        snprintf(buf, sizeof(buf), "%s: %s", _T("MODE"), wifiInfo.mode == wifiModeAdHoc ? "Ad-Hoc" : "Managed");
        interface_addMenuEntry2(wifiMenu, buf, wan, output_toggleWifiMode, SET_NUMBER(i), thumbnail_configure);

        snprintf(buf, sizeof(buf), "%s: %s", _T("ADDR_MODE"), wifiInfo.dhcp ? _T("ADDR_MODE_DHCP") : _T("ADDR_MODE_STATIC"));
        interface_addMenuEntry2(wifiMenu, buf, wan, output_toggleIPMode, SET_NUMBER(i), thumbnail_configure);

        if ((wifiInfo.dhcp == 0) || !wan)
        {
            getParam(path, "IPADDR", _T("NOT_AVAILABLE_SHORT"), temp);
            snprintf(buf, sizeof(buf), "%s: %s", _T("IP_ADDRESS"), temp);
            interface_addMenuEntry(wifiMenu, buf, output_changeIP, SET_NUMBER(i), thumbnail_configure);

            getParam(path, "NETMASK", _T("NOT_AVAILABLE_SHORT"), temp);
            snprintf(buf, sizeof(buf), "%s: %s", _T("NETMASK"), temp);
            interface_addMenuEntry(wifiMenu, buf, output_changeNetmask, SET_NUMBER(i), thumbnail_configure);

            if(wan)
            {
                getParam(path, "DEFAULT_GATEWAY", _T("NOT_AVAILABLE_SHORT"), temp);
                snprintf(buf, sizeof(buf), "%s: %s", _T("GATEWAY"), temp);
                interface_addMenuEntry(wifiMenu, buf, output_changeGw, SET_NUMBER(i), thumbnail_configure);

                getParam(path, "NAMESERVERS", _T("NOT_AVAILABLE_SHORT"), temp);
                snprintf(buf, sizeof(buf), "%s: %s", _T("DNS_SERVER"), temp);
                interface_addMenuEntry(wifiMenu, buf, output_changeDNS, SET_NUMBER(i), thumbnail_configure);
            }
        }
#endif // STBPNX
        if (wan)
        {
            snprintf(buf, sizeof(buf), "%s: %s", _T("ESSID"), wifiInfo.essid);
            interface_addMenuEntry(wifiMenu, buf, output_changeESSID, SET_NUMBER(i), thumbnail_enterurl);

            if (wifiInfo.auth != wifiAuthOpen && wifiInfo.auth < wifiAuthCount)
            {
                snprintf(buf, sizeof(buf), "%s: %s", _T("PASSWORD"), wan ? "***" : wifiInfo.key );
                interface_addMenuEntry(wifiMenu, buf, output_changeWifiKey, NULL, thumbnail_enterurl);
            }
        }
        else
        {
            snprintf(buf, sizeof(buf), "iwlist %s channel > %s", outputNetwork_virtIfaceName(i), INFO_TEMP_FILE);
            system(buf);
            FILE* f = fopen( INFO_TEMP_FILE, "r" );
            if (f)
            {
                char *ptr;
                while( fgets(buf, sizeof(buf), f ) != NULL )
                {
                    if( strncmp( buf, outputNetwork_virtIfaceName(i), 5 ) == 0 )
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
                snprintf(buf, sizeof(buf), "%s: %d", _T("CHANNEL_NUMBER"), wifiInfo.currentChannel);
                interface_addMenuEntry(wifiMenu, buf, output_changeWifiChannel, SET_NUMBER(i), thumbnail_configure);
            }
            snprintf(buf, sizeof(buf), "%s: %s", _T("AUTHENTICATION"), wireless_auth_print( wifiInfo.auth ));
            interface_addMenuEntry(wifiMenu, buf, output_toggleAuthMode, SET_NUMBER(i), thumbnail_configure);
            if( wifiInfo.auth == wifiAuthWPAPSK || wifiInfo.auth == wifiAuthWPA2PSK )
            {
                snprintf(buf, sizeof(buf), "%s: %s", _T("ENCRYPTION"), wireless_encr_print( wifiInfo.encryption ));
                interface_addMenuEntry(wifiMenu, buf, output_toggleWifiEncryption, SET_NUMBER(i), thumbnail_configure);
            }
        } // !wan
#ifdef HIDE_EXTRA_FUNCTIONS
        interface_addMenuEntry(wifiMenu, _T("HIDE_ADVANCED"), output_wifiToggleAdvanced, SET_NUMBER(basicOptionsCount), thumbnail_configure);
#endif
    }

    interface_addMenuEntry(wifiMenu, _T("APPLY_NETWORK_SETTINGS"), output_applyNetworkSettings, SET_NUMBER(i), settings_renew);

    return 0;
}

static int32_t output_wifiKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
    if (cmd->command == interfaceCommandBlue)
    {
        interface_menuActionShowMenu( pMenu, &WirelessMenu );
        return 0;
    }
    return 1;
}

static int32_t output_readWirelessSettings(void)
{
    char buf[BUFFER_SIZE];

#ifdef STBPNX
    char *path = WLAN_CONFIG_FILE;

    output_readWanIface();

    wifiInfo.auth       = wifiAuthOpen;
    wifiInfo.encryption = wifiEncTKIP;

    getParam(path, "ENABLE_WIRELESS", "0", buf);
    wifiInfo.enable = strtol( buf, NULL, 10 );

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
#else // STBPNX
    wifiInfo.auth = wifiAuthWPA2PSK;
    wifiInfo.encryption = wifiEncAES;

#ifdef USE_WPA_SUPPLICANT
    if (networkInfo.wanIface == eIface_wlan0)
    {
        output_readWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
    }
    else
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
static int32_t output_setHostapdChannel(int32_t channel)
{
    if (channel < 1 || channel > 14)
        return -1;

    int32_t res = 0;
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

#if (defined ENABLE_WIFI) && (defined USE_WPA_SUPPLICANT)
static int32_t output_readWpaSupplicantConf( const char *filename )
{
    char buf[MENU_ENTRY_INFO_LENGTH];

    getParam( filename, "ap_scan", "1", buf );
    wifiInfo.mode = strtol(buf,NULL,10) == 2 ? wifiModeAdHoc : wifiModeManaged;
    getParam( filename, "ssid", "", buf );
    if(buf[0] == '"') {
        buf[strlen(buf)-1]=0;
        strncpy( wifiInfo.essid, &buf[1], sizeof(wifiInfo.essid) );
    }
    else
    {
        strncpy(wifiInfo.essid, DEFAULT_ESSID, sizeof(wifiInfo.essid));
    }
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

static int32_t output_writeWpaSupplicantConf( const char *filename )
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

static int32_t output_setURL(interfaceMenu_t *pMenu, char *value, void* urlOption)
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
            //TODO: danger! here can be buffer overflow
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

static char *output_getURL(int32_t index, void* pArg)
{
    return index == 0 ? output_getOption((outputUrlOption)pArg) : NULL;
}

static int32_t output_changeURL(interfaceMenu_t *pMenu, void* urlOption)
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

#if defined(ENABLE_XWORKS)
#if defined(STBPNX)
static int32_t output_togglexWorks(interfaceMenu_t *pMenu, void* pArg)
{
    return output_saveAndRedraw(setParam( STB_CONFIG_FILE, "XWORKS", pArg ? "ON" : "OFF" ), pMenu);
}
#endif //#if defined(STBPNX)
static int32_t output_togglexWorksProto(interfaceMenu_t *pMenu, void* pArg)
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

static int32_t output_restartxWorks(interfaceMenu_t *pMenu, void* pArg)
{
    interface_showMessageBox(_T("LOADING"), settings_renew, 0);

    system(XWORKS_INIT_SCRIPT " restart");

    output_refillMenu(pMenu);
    interface_hideMessageBox();
    return 0;
}
#endif // ENABLE_XWORKS

static int32_t output_changeIPTVtimeout(interfaceMenu_t *pMenu, void* pArg)
{
    static const time_t timeouts[] = { 3, 5, 7, 10, 15, 30 };
    static const int32_t    timeouts_count = sizeof(timeouts)/sizeof(timeouts[0]);
    int32_t i;
    for( i = 0; i < timeouts_count; i++ )
        if( timeouts[i] >= appControlInfo.rtpMenuInfo.pidTimeout )
            break;
    if( i >= timeouts_count )
        appControlInfo.rtpMenuInfo.pidTimeout = timeouts[0];
    else
        appControlInfo.rtpMenuInfo.pidTimeout = timeouts[ (i+1)%timeouts_count ];

    return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int32_t output_enterIPTVMenu(interfaceMenu_t *iptvMenu, void* pArg)
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
    int32_t xworks_enabled = 1;
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
        snprintf(buf, sizeof(buf), "ifconfig %s | grep \"inet addr\"", outputNetwork_virtIfaceName(ifaceWAN));
        if (helperParseLine(INFO_TEMP_FILE, buf, "inet addr:", temp, ' ')) //           inet addr:192.168.200.15  Bcast:192.168.200.255  Mask:255.255.255.0
        {
            snprintf(buf, sizeof(buf), "http://%s:1080/xworks.xspf", temp );
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

static int32_t output_changeIPTVPlaylist(interfaceMenu_t *pMenu, void* pArg)
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

#if defined(ENABLE_PROVIDER_PROFILES)
static int32_t output_select_profile(const struct dirent * de)
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

static int32_t output_enterProfileMenu(interfaceMenu_t *pMenu, void* pArg)
{
    if( output_profiles == NULL )
    {
        char name[MENU_ENTRY_INFO_LENGTH];
        char full_path[MAX_PROFILE_PATH];
        int32_t i;

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

static int32_t output_leaveProfileMenu(interfaceMenu_t *pMenu, void* pArg)
{
    int32_t i;
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

static int32_t output_setProfile(interfaceMenu_t *pMenu, void* pArg)
{
    int32_t index = GET_NUMBER(pArg);
    int32_t i;
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
            strncpy(appControlInfo.rtpMenuInfo.playlist, value , sizeof(appControlInfo.rtpMenuInfo.playlist));
            appControlInfo.rtpMenuInfo.usePlaylistURL = value[0] != 0;
        } else
        if( strcmp( buffer, "RTPEPG" ) == 0 )
        {
            strncpy(appControlInfo.rtpMenuInfo.epg, value , sizeof(appControlInfo.rtpMenuInfo.epg));
        } else
        if( strcmp( buffer, "RTPPIDTIMEOUT" ) == 0 )
        {
            appControlInfo.rtpMenuInfo.pidTimeout = atol( value );
        } else
        if( strcmp( buffer, "VODIP") == 0 )
        {
            strncpy(appControlInfo.rtspInfo.streamIP, value , sizeof(appControlInfo.rtspInfo.streamIP));
        } else
        if( strcmp( buffer, "VODINFOURL" ) == 0 )
        {
            strncpy(appControlInfo.rtspInfo.streamInfoUrl, value , sizeof(appControlInfo.rtspInfo.streamInfoUrl));
            appControlInfo.rtspInfo.usePlaylistURL = value[0] != 0;
        } else
        if( strcmp( buffer, "VODINFOIP" ) == 0 )
        {
            strncpy(appControlInfo.rtspInfo.streamIP, "VODIP" , sizeof(appControlInfo.rtspInfo.streamIP));
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
            uint32_t port;

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

static int32_t output_checkProfile(void)
{
    int32_t fd = open(STB_PROVIDER_PROFILE, O_RDONLY);
    if( fd < 0 )
    {
        interfaceInfo.currentMenu = (interfaceMenu_t*)&ProfileMenu;
        output_enterProfileMenu(interfaceInfo.currentMenu, NULL);
        return 1;
    }
    close(fd);
    return 0;
}
#endif //#if defined(ENABLE_PROVIDER_PROFILES)
#endif //#if defined(ENABLE_IPTV)



static int32_t output_setProxyLogin(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    int32_t ret = 0;

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

static int32_t output_setProxyPassword(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    int32_t ret = 0;

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

static int32_t output_changeProxyAddress(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("ENTER_PROXY_ADDR"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}:\\d+", output_setProxyAddress, NULL, inputModeDirect, pArg);
}

static int32_t output_changeProxyLogin(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("ENTER_PROXY_LOGIN"), "\\w+", output_setProxyLogin, NULL, inputModeABC, pArg);
}

static int32_t output_changeProxyPassword(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("ENTER_PROXY_PASSWD"), "\\w+", output_setProxyPassword, NULL, inputModeABC, pArg);
}

#ifdef ENABLE_BROWSER
static char *output_getMiddlewareUrl(int32_t field, void* pArg)
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

static int32_t output_setMiddlewareUrl(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    int32_t ret = 0;

    if( value == NULL )
        return 1;

#ifdef STBPNX
    char buf[MENU_ENTRY_INFO_LENGTH];

    strncpy(buf, value, sizeof(buf));

    if(strncmp(value,"http",4) != 0)
    {
        snprintf(buf, sizeof(buf), "http://%s/", value);
    } else
    {
        strncpy(buf, value, sizeof(buf));
    }
    ret = setParam(BROWSER_CONFIG_FILE, "HomeURL", buf);
#endif
    return output_saveAndRedraw(ret, pMenu);
}

static int32_t output_changeMiddlewareUrl(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("ENTER_MW_ADDR"), "\\w+", output_setMiddlewareUrl, output_getMiddlewareUrl, inputModeABC, pArg);
}

static int32_t output_toggleMWAutoLoading(interfaceMenu_t *pMenu, void* pArg)
{
    int32_t ret = 0;
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


#ifdef ENABLE_VOD
static int32_t output_toggleVODPlaylist(interfaceMenu_t *pMenu, void* pArg)
{
    appControlInfo.rtspInfo.usePlaylistURL = !appControlInfo.rtspInfo.usePlaylistURL;
    return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int32_t output_setVODIP(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if( value == NULL )
        return 1;

    value = inet_addr_prepare(value);
    if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
    {
        interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
        return -1;
    }
    strncpy(appControlInfo.rtspInfo.streamIP, value, sizeof(appControlInfo.rtspInfo.streamIP));

    return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int32_t output_setVODINFOIP(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if( value == NULL )
        return 1;

    value = inet_addr_prepare(value);
    if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
    {
        interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
        return -1;
    }
    strncpy(appControlInfo.rtspInfo.streamInfoIP, value, sizeof(appControlInfo.rtspInfo.streamInfoIP));

    return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int32_t output_changeVODIP(interfaceMenu_t *pMenu, void* pArg)
{
    output_parseIP( appControlInfo.rtspInfo.streamIP );
    return interface_getText(pMenu, _T("ENTER_VOD_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setVODIP, output_getIPfield, inputModeDirect, NULL);
}

static int32_t output_changeVODINFOIP(interfaceMenu_t *pMenu, void* pArg)
{
    output_parseIP( appControlInfo.rtspInfo.streamInfoIP );
    return interface_getText(pMenu, _T("ENTER_VOD_INFO_IP"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setVODINFOIP, output_getIPfield, inputModeDirect, NULL);
}

static int32_t output_enterVODMenu(interfaceMenu_t *vodMenu, void* pArg)
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

static int32_t output_enterWebMenu(interfaceMenu_t *webMenu, void* pArg)
{
    char buf[MENU_ENTRY_INFO_LENGTH];

    interface_clearMenuEntries(webMenu);
    snprintf(buf, sizeof(buf), "%s: %s", _T("PROXY_ADDR"), appControlInfo.networkInfo.proxy[0] != 0 ? appControlInfo.networkInfo.proxy : _T("NONE"));
    interface_addMenuEntry(webMenu, buf, output_changeProxyAddress, pArg, thumbnail_enterurl);

    snprintf(buf, sizeof(buf), "%s: %s", _T("PROXY_LOGIN"), appControlInfo.networkInfo.login[0] != 0 ? appControlInfo.networkInfo.login : _T("NONE"));
    interface_addMenuEntry(webMenu, buf, output_changeProxyLogin, pArg, thumbnail_enterurl);

    snprintf(buf, sizeof(buf), "%s: ***", _T("PROXY_PASSWD"));
    interface_addMenuEntry(webMenu, buf, output_changeProxyPassword, pArg, thumbnail_enterurl);

#ifdef ENABLE_BROWSER
#ifdef STBPNX
    char temp[MENU_ENTRY_INFO_LENGTH];

        getParam(BROWSER_CONFIG_FILE, "HomeURL", "", temp);
        snprintf(buf, sizeof(buf), "%s: %s", _T("MW_ADDR"), temp);
        interface_addMenuEntry(webMenu, buf, output_changeMiddlewareUrl, pArg, thumbnail_enterurl);

        getParam(BROWSER_CONFIG_FILE, "AutoLoadingMW", "", temp);
        if (temp[0] != 0)
        {
            snprintf(buf, sizeof(buf), "%s: %s", _T("MW_AUTO_MODE"), strcmp(temp,"ON")==0 ? _T("ON") : _T("OFF"));
            interface_addMenuEntry(webMenu, buf, output_toggleMWAutoLoading, pArg, thumbnail_configure);
        }else
        {
            setParam(BROWSER_CONFIG_FILE, "AutoLoadingMW","OFF");
            snprintf(buf, sizeof(buf), "%s: %s", _T("MW_AUTO_MODE"), _T("OFF"));
            interface_addMenuEntry(webMenu, buf, output_toggleMWAutoLoading, pArg, thumbnail_configure);
        }
#endif
#endif
    return 0;
}

static int32_t output_readWanIface(void)
{
    char buf[MENU_ENTRY_INFO_LENGTH];

    getParam(WAN_CONFIG_FILE, "IFACE", "", buf);
    networkInfo.wanIface = table_IntStrLookupR(ifaceNames, buf, eIface_unknown);
    if(networkInfo.wanIface == eIface_unknown) {
        getParam(WLAN_CONFIG_FILE, "WAN_MODE", "0", buf); //legacy
        if(atol(buf) == 1) {
            networkInfo.wanIface = eIface_wlan0;
        } else {
            //default value
            networkInfo.wanIface = eIface_eth0;
        }
    }
    return 0;
}

static int32_t output_writeWanIface(void)
{
    const char *iface_name = table_IntStrLookup(ifaceNames, networkInfo.wanIface, "");
    return setParam(WAN_CONFIG_FILE, "IFACE", iface_name);
}

#ifdef STSDK
int32_t output_readInterfacesFile(void)
{
    char buf[MENU_ENTRY_INFO_LENGTH];
    struct in_addr addr;

    // WAN
    output_readWanIface();

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
#if (defined ENABLE_ETH1) || (defined ENABLE_WIFI)
    getParam(LAN_CONFIG_FILE, "MODE", "NAT", buf );
    if (strcasecmp(buf, "BRIDGE") == 0) {
        networkInfo.lanMode = lanBridge;
    } else if (strcasecmp(buf, "STATIC") == 0) {
        networkInfo.lanMode = lanStatic;
    } else {
        networkInfo.lanMode = lanDhcpServer;
    }
    getParam(LAN_CONFIG_FILE, "IPADDR", "0.0.0.0", buf);
    inet_aton(buf, &addr);
    networkInfo.lan.ip = addr;
#endif
#ifdef ENABLE_WIFI
    getParam(WLAN_CONFIG_FILE, "ENABLE_WIRELESS", "0", buf);
    wifiInfo.enable = atol(buf);

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

    if (networkInfo.wanIface == eIface_wlan0)
    {
        output_readWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
    }
    else
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

static int32_t output_writeInterfacesFile(void)
{
    output_writeWanIface();

    setParam(WAN_CONFIG_FILE, "BOOTPROTO", networkInfo.wanDhcp ? "dhcp" : "static");
    if(networkInfo.wan.ip.s_addr != 0)
    {
        setParam(WAN_CONFIG_FILE, "IPADDR", inet_ntoa(networkInfo.wan.ip));
    }
    if(networkInfo.wan.mask.s_addr == 0)
    {
        networkInfo.wan.mask.s_addr = 0x00ffffff;
    }
    setParam(WAN_CONFIG_FILE, "NETMASK", inet_ntoa(networkInfo.wan.mask));
    if (networkInfo.wan.gw.s_addr != 0)
        setParam(WAN_CONFIG_FILE, "DEFAULT_GATEWAY", inet_ntoa(networkInfo.wan.gw));

#if (defined ENABLE_ETH1) || (defined ENABLE_WIFI)
    char *mode = NULL;
    switch (networkInfo.lanMode)
    {
        case lanModeCount:  // fall through
        case lanDhcpServer: mode = "NAT";    break;
        case lanBridge:     mode = "BRIDGE"; break;
        case lanDhcpClient: mode = "DHCP";   break;
        case lanStatic:     mode = "STATIC"; break;
    }

    if(networkInfo.wanIface != eIface_br0) {
        eIface_t lanIface = (networkInfo.wanIface == eIface_eth0) ? eIface_wlan0 : eIface_eth0;
        const char *iface_name = table_IntStrLookup(ifaceNames, lanIface, "");

        setParam(LAN_CONFIG_FILE, "IFACE", iface_name);
    }

    setParam(LAN_CONFIG_FILE, "MODE", mode);
    setParam(LAN_CONFIG_FILE, "IPADDR", inet_ntoa(networkInfo.lan.ip));
    if (networkInfo.lan.mask.s_addr == 0)
        networkInfo.lan.mask.s_addr = 0x00ffffff;
    //setParam(WAN_CONFIG_FILE, "NETMASK", inet_ntoa(networkInfo.lan.mask));
#endif

#ifdef ENABLE_WIFI
    setParam(WLAN_CONFIG_FILE, "BOOTPROTO", wifiInfo.dhcp ? "dhcp" : "static");
    if(wifiInfo.wlan.ip.s_addr != 0)
    {
        setParam(WLAN_CONFIG_FILE, "IPADDR", inet_ntoa(wifiInfo.wlan.ip));
    }
    if(wifiInfo.wlan.mask.s_addr == 0)
    {
        wifiInfo.wlan.mask.s_addr = 0x00ffffff;
    }
    setParam(WLAN_CONFIG_FILE, "NETMASK", inet_ntoa(wifiInfo.wlan.mask));
    if(wifiInfo.wlan.gw.s_addr != 0)
    {
        setParam(WLAN_CONFIG_FILE, "DEFAULT_GATEWAY", inet_ntoa(wifiInfo.wlan.gw));
    }

    if(networkInfo.wanIface == eIface_wlan0)
    {
        return output_writeWpaSupplicantConf(STB_WPA_SUPPLICANT_CONF);
    }

    setParam( STB_HOSTAPD_CONF, "ssid", wifiInfo.essid );
    output_setHostapdChannel(wifiInfo.currentChannel);
#ifdef ENABLE_ETH1
    if(helperCheckDirectoryExsists("/sys/class/net/eth1"))
    {
        setParam(STB_HOSTAPD_CONF, "bridge", table_IntStrLookup(ifaceNames, eIface_br0, ""));
    }
    else
#endif
    {
        setParam( STB_HOSTAPD_CONF, "bridge", NULL);
    }
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

static int32_t output_writeDhcpConfig(void)
{
    if((networkInfo.lanMode != lanDhcpServer) ||
       (networkInfo.wanIface == eIface_br0))
    {
        unlink(STB_DHCPD_CONF);
        return 0;
    }

    system("ifcfg dhcp > " STB_DHCPD_CONF);

    return 0;
}
#endif // STSDK

static const char* output_getLanModeName(eLanMode_t mode)
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

static void output_setIfaceMenuName(interfaceMenu_t *pMenu, const char *ifaceName, int32_t wan, eLanMode_t lanMode)
{
    char name[MENU_ENTRY_INFO_LENGTH];
    size_t len;
    const char *net_mode_name = wan ? _T("INTERNET_CONNECTION") : output_getLanModeName(lanMode);

    len = snprintf(name, sizeof(name), "%s - %s", ifaceName, net_mode_name);
    interface_setMenuName(pMenu, name, len+1);
}

static int32_t output_isIfaceDhcp(eVirtIface_t iface)
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
        case ifaceWireless:
#ifdef ENABLE_WIFI
# ifdef STBPNX
            getParam(WLAN_CONFIG_FILE, "BOOTPROTO", "static", temp);
            return strcasecmp(temp, "static");
# endif
# ifdef STSDK
            return wifiInfo.dhcp;
# endif
#endif // ENABLE_WIFI
            return 0;
        default:;
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

static int32_t output_writeMobileSettings(void)
{
    int32_t err = 0;
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

static char* output_getMobileAPN(int32_t index, void* pArg)
{
    if (index == 0)
        return mobileInfo.apn;
    return NULL;
}

static char* output_getMobilePhone(int32_t index, void* pArg)
{
    if (index == 0)
        return mobileInfo.phone;
    return NULL;
}

static int32_t output_setMobileAPN(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if (value == NULL)
        return 1;
    if (value[0] == 0)
        return 0;
    strncpy(mobileInfo.apn, value, sizeof(mobileInfo.apn)-1);
    return output_saveAndRedraw(output_writeMobileSettings(), pMenu);
}

static int32_t output_setMobilePhone(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if (value == NULL)
        return 1;
    if (value[0] == 0)
        return 0;
    strncpy(mobileInfo.phone, value, sizeof(mobileInfo.phone)-1);
    return output_saveAndRedraw(output_writeMobileSettings(), pMenu);
}

static int32_t output_changeMobileAPN(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, "", "\\w+", output_setMobileAPN,   output_getMobileAPN,   inputModeABC, pArg);
}

static int32_t output_changeMobilePhone(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, "", "\\w+", output_setMobilePhone, output_getMobilePhone, inputModeABC, pArg);
}

static int32_t output_restartMobile(interfaceMenu_t *pMenu, void* pArg)
{
    interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 0);

    int32_t timeout = 3;
    int32_t res = system("killall pppd 2> /dev/null");
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

static int32_t output_enterMobileMenu(interfaceMenu_t *mobileMenu, void *ignored)
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
    if(output_checkIfaceExists(eIface_ppp0))
    {
        str = _T("ON");
    }
    else
    {
        int32_t res = system("killall -0 pppd 2> /dev/null");
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


static int32_t output_ping(char *value)
{
    char cmd[256];
    int32_t ret = -1;
    snprintf(cmd, sizeof(cmd), "ping -c 1 %s", value);
    printf("cmd: %s\n",cmd);
    ret = system(cmd);

    if (ret != -1)
        ret = WEXITSTATUS(ret);
    return ret;
}

static int32_t output_pingVisual(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if(value == NULL) {
        return 0;
    }
    int32_t valuePing;
    valuePing = output_ping(value);

    if (valuePing == 0){
        interface_showMessageBox(_T("IP_RELEASE_SUCCESSFUL"), thumbnail_yes, 0);
    } else {
        interface_showMessageBox(_T("IP_RELEASE_NOT_SUCCESSFUL"), thumbnail_error, 0);
    }
    return -1;
}

static int32_t output_pingMenu(interfaceMenu_t* pMenu, void* pArg)
{
    interface_getText(pMenu, _T("IP_SERVER"), "\\w+", output_pingVisual, NULL, inputModeABC, &pArg);
    return 0;
}

static void outputNetwork_showIp(eIface_t iface, const char *iface_name, char *info_text)
{
    char temp[256];
    struct ifreq ifr;
    int32_t fd;


    if(iface != eIface_ppp0)
    {
        snprintf(temp, sizeof(temp), "/sys/class/net/%s/address", table_IntStrLookup(ifaceNames, iface, ""));
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
            snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("MAC_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
            strcat(info_text, temp);
        }
    }
#ifdef STBPNX
    if((iface == eIface_eth1) && outputNetwork_LANIsBridge())
    {
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
    
    strncpy(ifr.ifr_name, table_IntStrLookup(ifaceNames, iface, ""), IFNAMSIZ-1);
    if(ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("IP_ADDRESS"), _T("NOT_AVAILABLE_SHORT"));
    } else {
        snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("IP_ADDRESS"), inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    }
    strcat(info_text, temp);

//     if (i != ifacePPP) // PPP netmask is 255.255.255.255
    if(iface != eIface_ppp0) // PPP netmask is 255.255.255.255
    {
        if (ioctl(fd, SIOCGIFNETMASK, &ifr) < 0)
            snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("NETMASK"), _T("NOT_AVAILABLE_SHORT"));
        else
            snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("NETMASK"), inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
        strcat(info_text, temp);
    }

    close(fd);
}

#ifdef ENABLE_VERIMATRIX
static int32_t output_toggleVMEnable(interfaceMenu_t *pMenu, void* pArg)
{
    appControlInfo.useVerimatrix = !appControlInfo.useVerimatrix;
    return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int32_t output_setVMAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if (value == NULL)
        return 1;

    value = inet_addr_prepare(value);
    if (inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY)
    {
        interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
        return -1;
    }

    int32_t ret = setParam(VERIMATRIX_INI_FILE, "SERVERADDRESS", value);
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
    int32_t len = strlen((char*)userp);

    if (len+size*nmemb >= DATA_BUFF_SIZE)
    {
        size = 1;
        nmemb = DATA_BUFF_SIZE-len-1;
    }

    memcpy(&((char*)userp)[len], buffer, size*nmemb);
    ((char*)userp)[len+size*nmemb] = 0;
    return size*nmemb;
}

static int32_t output_getVMRootCert(interfaceMenu_t *pMenu, void* pArg)
{
    char info_url[MAX_CONFIG_PATH];
    int32_t fd, code;
    char rootcert[DATA_BUFF_SIZE];
    char errbuff[CURL_ERROR_SIZE];
    CURLcode ret;
    CURL *hnd;

    hnd = curl_easy_init();

    memset(rootcert, 0, sizeof(rootcert));

    snprintf(info_url, sizeof(info_url), "http://%s/%s", appControlInfo.rtspInfo.streamInfoIP, "rootcert.pem");

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

static int32_t output_setVMCompany(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    if (value == NULL)
        return 1;

    int32_t ret = setParam(VERIMATRIX_INI_FILE, "COMPANY", value);
    if (ret == 0)
        output_refillMenu(pMenu);
    return ret;
}

static int32_t output_changeVMAddress(interfaceMenu_t *pMenu, void* pArg)
{
    return interface_getText(pMenu, _T("VERIMATRIX_ADDRESS"), "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setVMAddress, NULL, inputModeDirect, pArg);
}

static int32_t output_changeVMCompany(interfaceMenu_t *pMenu, void* pArg)
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
static int32_t output_toggleSMEnable(interfaceMenu_t *pMenu, void* pArg)
{
    appControlInfo.useSecureMedia = !appControlInfo.useSecureMedia;
    return output_saveAndRedraw(saveAppSettings(), pMenu);
}

static int32_t output_setSMAddress(interfaceMenu_t *pMenu, char *value, void* pArg)
{
    int32_t type = GET_NUMBER(pArg);
    int32_t ret;

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

static int32_t output_changeSMAddress(interfaceMenu_t *pMenu, void* pArg)
{
    int32_t type = GET_NUMBER(pArg);
    return interface_getText(pMenu, _T(type == smEsamHost ? "SECUREMEDIA_ESAM_HOST" : "SECUREMEDIA_RANDOM_HOST"),
        "\\d{3}.\\d{3}.\\d{3}.\\d{3}", output_setSMAddress, NULL, inputModeDirect, pArg);
}
#endif // #ifdef ENABLE_SECUREMEDIA

static const struct wanMode *outputNetwork_WANIfaceGet(eIface_t mode)
{
    uint32_t i;
    struct wanMode *pos = wanModes;
    for(i = 0; i < ARRAY_SIZE(wanModes); i++, pos++) {
        if(mode == pos->mode) {
            return pos;
        }
    }

    return NULL;
}

static const struct wanMode *outputNetwork_WANIfaceGetNext(eIface_t mode)
{
    uint32_t i;
    struct wanMode *pos = wanModes;
    for(i = 0; i < ARRAY_SIZE(wanModes); i++, pos++) {
        if(mode == pos->mode) {
            break;
        }
    }
    if((i + 1) < ARRAY_SIZE(wanModes)) {
        pos++;
    } else {
        pos = wanModes;
    }
    return pos;
}


static uint32_t outputNetwork_WANIfaceCount(void)
{
    uint32_t i;
    uint32_t wan_mode_count = 0;
    struct wanMode *pos = wanModes;
    for(i = 0; i < ARRAY_SIZE(wanModes); i++, pos++) {
        if((pos->presentInMenu == NULL) || pos->presentInMenu()) {
            wan_mode_count++;
        }
    }
    return wan_mode_count;
}

static const char *outputNetwork_WANIfaceLabel(eIface_t mode)
{
    const struct wanMode *wanMode = outputNetwork_WANIfaceGet(mode);
    if(wanMode) {
        if(wanMode->l10n_label) {
            return _T(wanMode->l10n_label);
        } else if(wanMode->name) {
            return wanMode->name;
        }
    }
    return "";
}

static int32_t output_toggleWAN(interfaceMenu_t *pMenu, void *pArg)
{
    int32_t ret = 0;
    const struct wanMode *wanMode;
    (void)pArg;

    //switch off previous mode
    wanMode = outputNetwork_WANIfaceGet(networkInfo.wanIface);
    if(wanMode && wanMode->change)
    {
        ret = wanMode->change(0);
        if(ret != 0 )
        {
            return output_saveAndRedraw(ret, pMenu);
        }
    }

    wanMode = outputNetwork_WANIfaceGetNext(networkInfo.wanIface);
    if(wanMode)
    {
        networkInfo.wanIface = wanMode->mode;
        if(wanMode->change)
        {
            ret = wanMode->change(1);
            if(ret != 0 )
            {
                //TODO: switch on previous WAN mode
            }
        }
    }
    else
    {
        return output_saveAndRedraw(-1, pMenu);
    }

    networkInfo.wanChanged = 1;
    networkInfo.changed = 1;

#if defined(STBPNX)
    ret = output_writeWanIface();
#elif defined(STSDK)
# if defined(ENABLE_WIFI)
    // Re-read wpa_supplicant or hostapd settings to update essid/password settings
    output_readWirelessSettings();
# endif

    output_writeDhcpConfig();
    ret = output_writeInterfacesFile();
#endif

    return output_saveAndRedraw(ret, pMenu);
}

static int32_t outputNetwork_enterMenu(interfaceMenu_t *networkMenu, void* notused)
{
    char  buf[MENU_ENTRY_INFO_LENGTH];
    char *str;

    // assert (networkMenu == _M &NetworkSubMenu);
    interface_clearMenuEntries(networkMenu);

    // Read required network settings
#ifdef STBPNX
# ifdef ENABLE_ETH1
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
# endif // ENABLE_ETH1
    output_readWanIface();
# ifdef ENABLE_WIFI
    {
        char temp[MENU_ENTRY_INFO_LENGTH];

        getParam(WLAN_CONFIG_FILE, "ENABLE_WIRELESS", "1", temp);
        wifiInfo.enable = atoi(temp);
    }
# endif // ENABLE_WIFI
#endif // STBPNX

    // ------------------ Display current Internet connection ------------------
    const char *str2 = outputNetwork_WANIfaceLabel(networkInfo.wanIface);
    snprintf(buf, sizeof(buf), "%s: %s", _T("INTERNET_CONNECTION"), str2);

    if(outputNetwork_WANIfaceCount() == 1)
    {
        interface_addMenuEntryDisabled(networkMenu, buf, thumbnail_info);
    }
    else
    {
        interface_addMenuEntry(networkMenu, buf, output_toggleWAN, NULL, thumbnail_info);
    }


    // Get current LAN mode for future use
    char *lanMode = (char*)output_getLanModeName(networkInfo.lanMode);
    (void)lanMode; //hide "unused variable" warnings


    // --------------------------- Ethernet 1 | Bridge -------------------------
    eIface_t eth0_br0_iface;
    const char *iface_name;

    eth0_br0_iface = (networkInfo.wanIface == eIface_br0) ? eIface_br0 : eIface_eth0;
    iface_name = outputNetwork_WANIfaceLabel(eth0_br0_iface);

    if((networkInfo.wanIface == eIface_br0)
    || ((networkInfo.wanIface != eIface_br0) && output_checkIfaceExists(eIface_eth0)))
    {

        if(networkInfo.wanIface == eth0_br0_iface)
        {
            str = _T("INTERNET_CONNECTION");
        }
        else
        {
#ifdef STBPNX
            str = _T("NOT_AVAILABLE");
#else
            str = lanMode;
#endif
        }

        snprintf(buf, sizeof(buf), "%s - %s", iface_name, str);
        interface_addMenuEntry(networkMenu, buf, interface_menuActionShowMenu, &Eth0SubMenu, settings_network);
    }
    else
    {
        interface_addMenuEntryDisabled(networkMenu, iface_name, settings_network);
    }

    // --------------------------- Ethernet 2 ----------------------------------
#ifdef ENABLE_ETH1
    if (output_checkIfaceExists(eIface_eth1))
    {
        snprintf(buf, sizeof(buf), "Ethernet 2 - %s", lanMode);
        interface_addMenuEntry(networkMenu, buf, interface_menuActionShowMenu, &Eth1SubMenu, settings_network);
    }
#endif // ENABLE_ETH1

    // ----------------------------- Wi-Fi -------------------------------------
#ifdef ENABLE_WIFI
    if(output_checkWiFiExists())
    {
        if(wifiInfo.enable) {
            str = (networkInfo.wanIface == eIface_wlan0) ? _T("INTERNET_CONNECTION") : lanMode;
        } else {
            str = _T("OFF");
        }
        if(networkInfo.wanIface == eIface_br0) {
            snprintf(buf, sizeof(buf), "%s", _T("WIRELESS"));
        } else {
            snprintf(buf, sizeof(buf), "%s - %s", _T("WIRELESS"), str);
        }
        interface_addMenuEntry(networkMenu, buf, interface_menuActionShowMenu, &WifiSubMenu, settings_network);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%s - %s", _T("WIRELESS"), _T("NOT_AVAILABLE"));
        interface_addMenuEntryDisabled(networkMenu, buf, settings_network);
    }
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
        snprintf(buf, sizeof(buf), "%s: %s", _T("VERIMATRIX_ENABLE"), appControlInfo.useVerimatrix == 0 ? _T("OFF") : _T("ON"));
        interface_addMenuEntry(networkMenu, buf, output_toggleVMEnable, NULL, thumbnail_configure);
        if (appControlInfo.useVerimatrix != 0)
        {
            char temp[MENU_ENTRY_INFO_LENGTH];
            getParam(VERIMATRIX_INI_FILE, "COMPANY", "", temp);
            if (temp[0] != 0)
            {
                snprintf(buf, sizeof(buf), "%s: %s", _T("VERIMATRIX_COMPANY"), temp);
                interface_addMenuEntry(networkMenu, buf, output_changeVMCompany, NULL, thumbnail_enterurl);
            }
            getParam(VERIMATRIX_INI_FILE, "SERVERADDRESS", "", temp);
            if (temp[0] != 0)
            {
                snprintf(buf, sizeof(buf), "%s: %s", _T("VERIMATRIX_ADDRESS"), temp);
                interface_addMenuEntry(networkMenu, buf, output_changeVMAddress, NULL, thumbnail_enterurl);
            }
            interface_addMenuEntry(networkMenu, _T("VERIMATRIX_GET_ROOTCERT"), output_getVMRootCert, NULL, thumbnail_turnaround);
        }
    }
#endif
#ifdef ENABLE_SECUREMEDIA
    if (helperFileExists(SECUREMEDIA_CONFIG_FILE))
    {
        snprintf(buf, sizeof(buf), "%s: %s", _T("SECUREMEDIA_ENABLE"), appControlInfo.useSecureMedia == 0 ? _T("OFF") : _T("ON"));
        interface_addMenuEntry(networkMenu, buf, output_toggleSMEnable, NULL, thumbnail_configure);
        if (appControlInfo.useSecureMedia != 0)
        {
            char temp[MENU_ENTRY_INFO_LENGTH];
            getParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_ESAM_HOST", "", temp);
            if (temp[0] != 0)
            {
                snprintf(buf, sizeof(buf), "%s: %s", _T("SECUREMEDIA_ESAM_HOST"), temp);
                interface_addMenuEntry(networkMenu, buf, output_changeSMAddress, SET_NUMBER(smEsamHost), thumbnail_enterurl);
            }
            getParam(SECUREMEDIA_CONFIG_FILE, "SECUREMEDIA_RANDOM_HOST", "", temp);
            if (temp[0] != 0)
            {
                snprintf(buf, sizeof(buf), "%s: %s", _T("SECUREMEDIA_RANDOM_HOST"), temp);
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

static int32_t outputNetwork_LANIsBridge(void)
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

const char *outputNetwork_virtIfaceName(eVirtIface_t i)
{
    eIface_t iface = eIface_unknown;

    switch( i )
    {
        case ifaceWireless:
            iface = eIface_wlan0;
            break;
#ifdef STSDK
        case ifaceLAN:
            iface = eIface_br0;
            break;
#endif
        case ifaceWAN:
//             iface = networkInfo.wanIface;
//             break;
            if(outputNetwork_LANIsBridge() || (networkInfo.wanIface == eIface_br0))
            {
                iface = eIface_br0;
                break;
            }
        // fall through
        default:
            //TODO: 
            iface = eIface_eth0 + i;
            break;
    }
    return table_IntStrLookup(ifaceNames, iface, "unknown");
}


int32_t outputNetwork_showNetwork(char *info_text)
{
    char buf[256];
    char temp[256];
    int32_t i;
    int32_t fd;

    outputNetwork_showIp(eIface_eth0, "Ethernet", &info_text[strlen(info_text)]);
    strcat(info_text, "\n");
#ifdef ENABLE_ETH1
    if(output_checkIfaceExists(eIface_eth1))
    {
        outputNetwork_showIp(eIface_eth1, "Ethernet 2", &info_text[strlen(info_text)]);
        strcat(info_text, "\n");
    }
#endif
#ifdef ENABLE_WIFI
    if(output_checkWiFiExists())
    {
        char *iface_name = _T("WIRELESS");
        outputNetwork_showWireless(iface_name, &info_text[strlen(info_text)]);
        outputNetwork_showIp(eIface_wlan0, iface_name, &info_text[strlen(info_text)]);
        strcat(info_text, "\n");
    }
#endif
#ifdef ENABLE_PPP
    if(output_checkIfaceExists(eIface_ppp0))
    {
        outputNetwork_showIp(eIface_ppp0, "PPP", &info_text[strlen(info_text)]);
        strcat(info_text, "\n");
    }
#endif
    if (helperParseLine(INFO_TEMP_FILE, "route -n | grep -e \"0\\.0\\.0\\.0 .* 0\\.0\\.0\\.0 *UG .*\"", "0.0.0.0", buf, ' '))
    {
        snprintf(temp, sizeof(temp), "%s: ", _T("GATEWAY"));
        strcat(info_text, temp);
        strcat(info_text, buf);
        strcat(info_text, "\n");
    }
    /* else {
        snprintf(temp, sizeof(temp), "%s %s: %s\n", iface_name, _T("GATEWAY"), _T("NOT_AVAILABLE_SHORT"));
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
                snprintf(temp, sizeof(temp), "%s %d: %s\n", _T("DNS_SERVER"), i+1, ptr);
                strcat(info_text, temp);
            }
        }
        close(fd);
    }
    if ( i < 0 ) {
        snprintf(temp, sizeof(temp), "%s: %s\n", _T("DNS_SERVER"), _T("NOT_AVAILABLE_SHORT"));
        strcat(info_text, temp);
    }
    return 0;
}


static int32_t outputNetwork_init(void)
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

    return 0;
}

static int32_t outputNetwork_terminate(void)
{
#ifdef ENABLE_WIFI
    wireless_cleanupMenu();
#endif
    return 0;
}

void outputNetwork_buildMenu(interfaceMenu_t *pOutputMenu)
{
    outputNetwork_init();

    createListMenu(&NetworkSubMenu, _T("NETWORK_CONFIG"), settings_network, NULL, pOutputMenu,
        interfaceListMenuIconThumbnail, outputNetwork_enterMenu, outputNetwork_leaveMenu, NULL);

    createListMenu(&Eth0SubMenu, "WAN", settings_network, NULL, _M &NetworkSubMenu,
        interfaceListMenuIconThumbnail, output_enterEth0Menu, outputNetwork_leaveMenu, NULL);

#ifdef ENABLE_PPP
    createListMenu(&PPPSubMenu, _T("PPP"), settings_network, NULL, _M &NetworkSubMenu,
        interfaceListMenuIconThumbnail, output_enterPPPMenu, output_refreshStop, SET_NUMBER(ifaceWAN));
#endif
#ifdef ENABLE_ETH1
    createListMenu(&Eth1SubMenu, "LAN", settings_network, NULL, _M &NetworkSubMenu,
        interfaceListMenuIconThumbnail, output_enterEth1Menu, outputNetwork_leaveMenu, NULL);
#endif
#if defined(STBPNX) && (defined(ENABLE_ETH1) || defined(ENABLE_WIFI))
    createListMenu(&GatewaySubMenu, _T("GATEWAY_MODE"), settings_network, NULL, _M &NetworkSubMenu,
        interfaceListMenuIconThumbnail, output_enterGatewayMenu, NULL, NULL);
#endif
#ifdef ENABLE_WIFI
    int32_t wifi_icons[4] = { 0, 0, 0, statusbar_f4_enterurl };
    createListMenu(&WifiSubMenu, _T("WIRELESS"), settings_network, wifi_icons, _M &NetworkSubMenu,
        interfaceListMenuIconThumbnail, output_enterWifiMenu, outputNetwork_leaveMenu, SET_NUMBER(ifaceWireless));
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

}

void outputNetwork_cleanupMenu(void)
{
    outputNetwork_terminate();
}

interfaceListMenu_t *outputNetwork_getMenu(void)
{
    return &NetworkSubMenu;
}
