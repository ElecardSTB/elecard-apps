#if !defined(__DEFINES_H)
	#define __DEFINES_H

/*
 defines.h

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

/** @file defines.h Compile-time application settings
 * Compile-time application settings
 */
/** @defgroup StbMainApp StbMainApp - STB820 default user interface application
 */
/** @defgroup globals Global application compile-time settings
 *  @ingroup  StbMainApp
 *  @{
 */

/*************************************
 * Debug options (debug.h)
 *************************************/

/** @def DEBUG Enable lots of debug output
 */
//#define DEBUG

/** @def ALLOC_DEBUG Enable alloc/free debug output */
//#define ALLOC_DEBUG

/** @def TRACE Trace function entering/leaving */
//#define TRACE

/** @def EVENT_TRACE Event creation trace */
//#define EVENT_TRACE

/** @def SCREEN_TRACE Screen update trace */
//#define SCREEN_TRACE

/*************************************
 * Additional features (define them in
 * MAKEFILE as some of them require
 * additional libraries)
 *************************************/

/*************************************
 * Common features
 *************************************/

/** @def ENABLE_DVB Enable DVB functions
 */
#define ENABLE_DVB

/** @def ENABLE_IPTV Enable IPTV menu and features
 */
#define ENABLE_IPTV

/** @def ENABLE_VOD Enable VoD menu and features (defined in Makefile)
 */
//#define ENABLE_VOD

/** @def ENABLE_FAVORITES Enable Favorites menu
 */
#define ENABLE_FAVORITES

/** @def ENABLE_USB Enable USB menu
 */
#define ENABLE_USB

#ifdef ENABLE_DVB
	/** @def ENABLE_TELETEXT Enable DVB Teletext
	 */
	#define ENABLE_TELETEXT

	/** @def ENABLE_DVB_DIAG Enable diagnostics mode for DVB
	 */
	//#define ENABLE_DVB_DIAG

	/** @def ENABLE_STATS Enable DVB watch statistics
	 */
	//#define ENABLE_STATS
#endif

/** @def ENABLE_WEB_SERVICES Enable Web services
 */
#define ENABLE_WEB_SERVICES

#ifdef  ENABLE_WEB_SERVICES
#if (defined STB82) || (defined STSDK)
	/** @def ENABLE_SAMBA Enable Samba menu and features
	 */
	#define ENABLE_SAMBA
#endif // STB82 || STSDK
	/*
	 * STB820/810 only Web services
	 */
#if (defined STB82)
	/** @def ENABLE_BROWSER Enable Browser menu
	 */
	#define ENABLE_BROWSER

	/** @def ENABLE_DLNA Enable DLNA DMP (do it in makefile!)
	 */
	//#define ENABLE_DLNA
	#ifdef ENABLE_DLNA
		/** @def ENABLE_DLNA_DMR Enable DMR
		 */
		//#define ENABLE_DLNA_DMR
	#endif
#endif // STB82

	/** @def ENABLE_YOUTUBE Enable YouTube menu
	  */
	#define ENABLE_YOUTUBE

	/** @def ENABLE_RUTUBE Enable RuTube menu
	 */
	#define ENABLE_RUTUBE
#endif

/** @def HIDE_EXTRA_FUNCTIONS Remove development/extra functions from menu
 */
#define HIDE_EXTRA_FUNCTIONS

/** @def ENABLE_VIDIMAX Enable VidiMax menu, done in makefile
 */
//#define ENABLE_VIDIMAX 1

/** @def ENABLE_LAN Enable LAN if STB has second network interface (defined in build scripts)
 */
//#define ENABLE_LAN

/** @def ENABLE_REGPLAT Enable RegPlat support (defined in Makefile)
 */
//#define ENABLE_REGPLAT

/** @def ENABLE_WIFI Enable wifi support (defined in Makefile)
 */
//#define ENABLE_WIFI

/*************************************
 * STB820/810 only features
 *************************************/

#if (defined STB82)

/** @def ENABLE_VOIP Enable VoIP menu and functions
 */
#define ENABLE_VOIP

#ifdef ENABLE_VOIP
	/** @def ENABLE_VOIP_CONFERENCE Enable VoIP conference function
	 */
	#define ENABLE_VOIP_CONFERENCE
#endif

/** @def ENABLE_PASSWORD Enable password protection of settings (defined in Makefile)
 */
//#define ENABLE_PASSWORD

/** @def ENABLE_MULTI_VIEW Enable multiview feature
 */
#define ENABLE_MULTI_VIEW

/** @def ENABLE_PVR Enable PVR functions
 */
#define ENABLE_PVR

/** @def ENABLE_PPP Enable PPP connections (defined in Makefile)
 */
//#define ENABLE_PPP

#ifdef ENABLE_LAN
	/** @def ENABLE_BRIDGE Enable BRIDGE option in networking menu
	 */
	#define ENABLE_BRIDGE
#endif

/** @def ENABLE_MESSAGES Enable provider messages
 */
#define ENABLE_MESSAGES

/** @def SHOW_CARD_MENU Enable smart card menu
 */
//#define SHOW_CARD_MENU

/** @def ENABLE_XWORKS Enable xWorks support for stream switching IPTV (defined in Makefile)
 */
//#define ENABLE_XWORKS

/** @def ENABLE_PROVIDER_PROFILES Enable support for predefined by provider settings for IPTV/VOD (defined in Makefile)
 */
//#define ENABLE_PROVIDER_PROFILES

#endif // STB82

/* Other common options */

/** @def ENABLE_TEST_MODE Enable test mode (scan predetermined freqs, default volume, default rtsp url)
 */
//#define ENABLE_TEST_MODE

/** @def ENABLE_VERIMATRIX Enable verimatrix support (defined in Makefile)
 */
//#define ENABLE_VERIMATRIX

/** @def ENABLE_SECUREMEDIA Enable securemedia support (defined in Makefile)
 */
//#define ENABLE_SECUREMEDIA

/** @def WCHAR_SUPPORT Enable localized input support
 */
#define WCHAR_SUPPORT

/*************************************
 * Application options (app_info.h)
 *************************************/

#ifdef ENABLE_TEST_MODE
#ifndef ENABLE_TEST_SERVER
	#define ENABLE_TEST_SERVER
#endif
#ifndef TEST_SERVER_INET
	#define TEST_SERVER_INET
#endif
#endif // #ifdef ENABLE_TEST_MODE

#define VERIMATRIX_VKS_PORT			(12699)

/* Set to path to verimatrix ini file */
#ifndef VERIMATRIX_INI_FILE
	#define VERIMATRIX_INI_FILE		"/config/verimatrix/VERIMATRIX.INI"
#endif

/* Set to path to default verimatrix root cert path */
#ifndef VERIMATRIX_ROOTCERT_FILE
	#define VERIMATRIX_ROOTCERT_FILE		"/config/verimatrix/rootcert.pem"
#endif

/* Set to path to default verimatrix root cert path */
#ifndef SECUREMEDIA_CONFIG_FILE
	#define SECUREMEDIA_CONFIG_FILE		"/config/securemedia/settings.conf"
#endif

/* set to path to stream.txt file which contains RTSP file list (if HTTP server is unavailable) */
#ifndef RTSP_STREAM_FILE
	#define RTSP_STREAM_FILE	"/config/StbMainApp/streams.txt"
#endif

#ifndef DIVERSITY_FILE
	#define DIVERSITY_FILE	"/config.firmware/diversity"
#endif

#ifndef SETTINGS_FILE
	#define SETTINGS_FILE	"/config/StbMainApp/settings.conf"
#endif

#ifndef STB_CONFIG_FILE
	#define STB_CONFIG_FILE		"/config/settings.conf"
#endif

#ifndef STB_CONFIG_OVERRIDE_FILE
	#define STB_CONFIG_OVERRIDE_FILE		"/profile/config/settings.conf"
#endif

#ifndef STB_PROVIDER_PROFILES_DIR
	#define STB_PROVIDER_PROFILES_DIR		"/config.templates/providers/"
#endif

#ifndef STB_PROVIDER_PROFILE
	#define STB_PROVIDER_PROFILE		"/config/StbMainApp/provider"
#endif

#ifndef PLAYLIST_FILENAME
	#define PLAYLIST_FILENAME	"/config/StbMainApp/playlist.m3u"
#endif

#ifndef ADDRESSBOOK_FILENAME
	#define ADDRESSBOOK_FILENAME	"/config/StbMainApp/addressbook.m3u"
#endif

#ifndef DIALED_FILENAME
	#define DIALED_FILENAME	"/config/StbMainApp/dialed.m3u"
#endif

#ifndef ANSWERED_FILENAME
	#define ANSWERED_FILENAME	"/config/StbMainApp/answered.m3u"
#endif

#ifndef MISSED_FILENAME
	#define MISSED_FILENAME	"/config/StbMainApp/missed.m3u"
#endif

#ifndef VOIP_CONFIG_FILE
	#define VOIP_CONFIG_FILE "/config/StbMainApp/voip.conf"
#endif

#ifndef NETWORK_INTERFACES_FILE
	#define NETWORK_INTERFACES_FILE "/etc/network/interfaces"
#endif

#ifndef STB_DHCPD_CONF
	#define STB_DHCPD_CONF     "/var/etc/dhcpd.conf"
#endif

#ifndef STB_RESOLV_CONF
	#define STB_RESOLV_CONF     "/var/etc/resolv.conf"
#endif

#ifndef STB_HOSTAPD_CONF
	#define STB_HOSTAPD_CONF    "/var/etc/hostapd.conf"
#endif

#ifndef STB_WPA_SUPPLICANT_CONF
#ifdef STBPNX
	#define STB_WPA_SUPPLICANT_CONF    "/config/wpa_supplicant.conf"
#else
	#define STB_WPA_SUPPLICANT_CONF    "/var/etc/wpa_supplicant.conf"
#endif
#endif

#ifndef STB_WPA_SUPPLICANT_CTRL_DIR
	#define STB_WPA_SUPPLICANT_CTRL_DIR "/var/run/wpa_supplicant"
#endif

/* File which signals that application is already initialized and running */
#define APP_LOCK_FILE	"/var/tmp/app.lock"

/* set this to RTSP and HTTP server address */
#define RTSP_SERVER_ADDR	"192.168.200.1"

/* set this to array of RTSP stream list filenames (HTTP://RTSP_SERVER_ADDR/RTSP_SERVER_FILE)
  last entry must be NULL
*/
#define RTSP_SERVER_FILES	{ "streams.txt", "modules/encoder/encoded_stb.php", NULL }

/* set this to RTSP server control port */
#define RTSP_SERVER_PORT	(554)

/* uncomment this to enable play-when-highlighted feature (currently not fully supported) */
//#define ENABLE_AUTOPLAY

/* uncomment this to enable loading animation (not fully implemented yet) */
//#define ENABLE_LOADING_ANUMATION

/* Application/firmware release type (release build, development build) */
#ifndef RELEASE_TYPE
    #define RELEASE_TYPE	"Custom"
#endif

/*************************************
 * GFX options (gfx.h)
 *************************************/

#ifndef STBTI
    #define FB_DEVICE "/dev/fb0"
#else
	#define FB_DEVICE "/dev/fb/0"
#endif

/* set this to the location of font files */
#ifndef FONT_DIR
	#define FONT_DIR			"/opt/elecard/share/StbMainApp/fonts/"
#endif

/* default font */
#ifndef DEFAULT_FONT
#define DEFAULT_FONT			"arialbd.ttf"
#endif

/* default small font */
#define DEFAULT_SMALL_FONT		DEFAULT_FONT

/* default font height */
#define DEFAULT_FONT_HEIGHT	(18)

/* default small (description) font height */
#define DEFAULT_SMALL_FONT_HEIGHT (16)

/*************************************
 * Interface options (interface.h)
 *************************************/

/* With default font height(=18) only 144 dots can fit on one line */
#define MENU_ENTRY_INFO_LENGTH   (256)

/* Show logo text in upper part of the screen */
//#define SHOW_LOGO_TEXT			" i T e l e c    S T B"
//#define SHOW_LOGO_IMAGE				"logo.png"

/** Path to StbMainApp images
 */
#ifndef IMAGE_DIR
	#define IMAGE_DIR			"/opt/elecard/share/StbMainApp/images/"
#endif


/***********************************************************
 * RTP/RTSP functions options (rtp_func.h, rtp.c, rtsp.c)
 ***********************************************************/

/** Allow AAC audio streams decoding
 */
#define ALLOW_AAC

/** Number of seconds to wait for data before stopping playback
 */
#define RTP_TIMEOUT	(2)

/** RTP engine to use with RTP streaming, 0 - Elecard, 1 - ccRTP, 2 - smallRTP
 */
#define RTP_ENGINE	(2)

/** specify RTP engine to use with RTSP streaming, 0 - Elecard, 1 - ccRTP, 2 - smallRTP
 */
#define RTSP_ENGINE	(2)

/***********************************************************
* DVB Options
***********************************************************/

/** Default DVB channel file which contains available channel list
 */
#ifndef CHANNEL_FILE_NAME
	#define CHANNEL_FILE_NAME   "/config/StbMainApp/channels.conf"
#endif

/** Default path to profile locations storage
 */
#ifndef PROFILE_LOCATIONS_PATH
	#define PROFILE_LOCATIONS_PATH   "/profile/operator/locations"
#endif

/***********************************************************
* Misc
***********************************************************/

/** Browser config file
 */
#ifndef BROWSER_CONFIG_FILE
	#define BROWSER_CONFIG_FILE	"/config/konqueror/konq-embedrc"
#endif

/** MW Browser config file
 */
#ifndef MW_CONFIG_FILE
	#define MW_CONFIG_FILE	"/config/konqueror/konq-embedrc-mw"
#endif

/** DHCP vendor options
 */
#ifndef DHCP_VENDOR_FILE
	#define DHCP_VENDOR_FILE	"/var/tmp/ifcfg-eth0.vend"
#endif

/** Temp file for text processing
 */
#define INFO_TEMP_FILE "/tmp/info.txt"

#ifndef ELCD_SOCKET_FILE
	#define ELCD_SOCKET_FILE   "/tmp/elcd.sock"
#endif

#ifndef ELCD_PROXY_CONFIG_FILE
	#define ELCD_PROXY_CONFIG_FILE   "/var/etc/proxy"
#endif

#ifndef ELCD_BASENAME
	#define ELCD_BASENAME "main_sdk7105_7105_ST40_LINUX_32BITS.out"
#endif

/** @} */

#endif /* __DEFINES_H      Do not add any thing below this line */
