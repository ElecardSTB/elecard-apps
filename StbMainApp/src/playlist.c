
/*
 playlist.c

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

#include "playlist.h"

#include "debug.h"
#include "m3u.h"
#include "l10n.h"
#include "rtp.h"
#include "rtsp.h"
#include "media.h"
#include "samba.h"
#include "youtube.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define DATA_BUFF_SIZE (32*1024)
#define PLAYLIST_CONNECT_TIMEOUT (5)
#define PLAYLIST_TIMEOUT         (7)

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

interfaceListMenu_t playlistMenu;

static char  playlist_lastURL[MAX_URL];
static char  errbuff[CURL_ERROR_SIZE];

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int playlist_validateURL(char *value);
static int playlist_getCount();
static int   playlist_stream_change(interfaceMenu_t *pMenu, void* pArg);
static int   playlist_fillMenu(char *selected);
static char* playlist_getURL(int selected);
static char *playlist_getDescription(int selected);
static int playlist_deleteURL(interfaceMenu_t *pMenu, void* pArg);
static int playlist_saveDescription(interfaceMenu_t *pMenu, char *value, void* pArg);
static int playlist_inputURL(interfaceMenu_t *pMenu, char *value, void* pArg);

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

char* playlist_getLastURL()
{
	return playlist_lastURL;
}

int playlist_setLastUrl(char* url)
{
	return (strncpy(playlist_lastURL,url,MAX_URL) != NULL);
}

static char *playlist_getURL(int selected)
{
	if (  m3u_getEntry( PLAYLIST_FILENAME, selected ) == 0)
	{
		return m3u_url;
	}
	return NULL;
}

static char *playlist_getDescription(int selected)
{
	if (  m3u_getEntry( PLAYLIST_FILENAME, selected ) == 0)
	{
		return m3u_description;
	}
	return NULL;
}

int playlist_streamStart()
{
	int streamIndex = -1;
	int i;
	FILE *f = m3u_initFile(PLAYLIST_FILENAME, "r");
	if(!f)
	{
		eprintf("%s: failed to open playlist file '%s'\n", __FUNCTION__, PLAYLIST_FILENAME);
		return 1;
	}

	for( i = 0; m3u_readEntry(f) == 0; i++ )
		if( 0 == strcmp( m3u_url, playlist_lastURL ) )
		{
			fclose(f);
			streamIndex = i;
			break;
		}
	if( streamIndex < 0 )
	{
		eprintf("%s: '%s' not found in favorites\n", __FUNCTION__, playlist_lastURL);
		return 1;
	}

	return playlist_stream_change((interfaceMenu_t *)&playlistMenu, SET_NUMBER(streamIndex));
}

static int playlist_getCount()
{
	return interface_getMenuEntryCount((interfaceMenu_t*)&playlistMenu) - 1 - 
		(1 - interface_isMenuEntrySelectable((interfaceMenu_t*)&playlistMenu, 1));
}

int playlist_startNextChannel(int direction, void* pArg)
{
	int indexChange = (direction?-1:1);
	FILE *file;
	int i,current_index,new_index;
	int playlistCount = playlist_getCount();

	dprintf("%s: playing %s, count=%d indexChange=%d\n", __FUNCTION__,playlist_lastURL,playlistCount,indexChange);

	if( playlistCount < 2 )
	{
		eprintf("%s: playlist has less then 2 items, nothing to navigate to\n", __FUNCTION__);
		return -1;
	}
	file = m3u_initFile(PLAYLIST_FILENAME, "r");
	if( file == NULL )
	{
		eprintf("%s: failed to init playlist file '%s'\n", __FUNCTION__, PLAYLIST_FILENAME);
		return 1;
	}
	current_index = 0;
	for( i = 0; m3u_readEntry(file) == 0; i++ )
	{
		if(strcmp( m3u_url, playlist_lastURL ) == 0)
		{
			fclose(file);
			current_index = i;
			break;
		}
	}
	new_index = current_index;
	if(appControlInfo.mediaInfo.playbackMode == playback_random)
	{
		srand(time(NULL));
		if(playlistCount < 3)
			new_index = (current_index +1 ) % playlistCount;
		else
			while( (new_index = rand() % playlistCount) == current_index );
	} else if(current_index>=0) // if we knew previous index, we can get new index
	{
		new_index = (current_index+playlistCount + indexChange) % playlistCount;
	}
	dprintf("%s: index=%d count=%d change=%d newIndex=%d\n", __FUNCTION__, current_index , playlistCount, appControlInfo.mediaInfo.playbackMode == playback_random ? 0 : indexChange, new_index);
	return playlist_stream_change((interfaceMenu_t *)&playlistMenu, SET_NUMBER(new_index));
}

static int playlist_stream_change(interfaceMenu_t *pMenu, void* pArg)
{
	char* URL;

	URL = playlist_getURL(GET_NUMBER(pArg));
	dprintf("%s: %d = %s\n", __FUNCTION__,GET_NUMBER(pArg),URL);
	if (URL != NULL)
	{
		interface_setSelectedItem(&playlistMenu, GET_NUMBER(pArg) + 1);
		appControlInfo.playbackInfo.channel = GET_NUMBER(pArg) + 1;
		if (strncasecmp(URL, URL_FILE_MEDIA, sizeof(URL_FILE_MEDIA)-1) == 0 ||
		    strncasecmp(URL, URL_HTTP_MEDIA, sizeof(URL_HTTP_MEDIA)-1) == 0 ||
		    strncasecmp(URL, URL_HTTPS_MEDIA,sizeof(URL_HTTPS_MEDIA)-1) == 0 ||
		    strncasecmp(URL, URL_RTMP_MEDIA, sizeof(URL_RTMP_MEDIA)-1) == 0)
		{
			appControlInfo.playbackInfo.playlistMode = playlistModeFavorites;
			appControlInfo.playbackInfo.streamSource = streamSourceFavorites;
			strcpy(playlist_lastURL, URL);
#ifdef ENABLE_YOUTUBE
			if( strstr( playlist_lastURL, "youtube.com/" ) != NULL )
			{
				dprintf("%s: youtube_streamStart(%s)\n", __FUNCTION__, URL);
				strcpy( appControlInfo.mediaInfo.lastFile, playlist_lastURL );
				strcpy( appControlInfo.playbackInfo.description, m3u_description );
				strcpy( appControlInfo.playbackInfo.thumbnail, resource_thumbnails[thumbnail_youtube] );
				appControlInfo.playbackInfo.channel = GET_NUMBER(pArg)+1;
				return youtube_streamStart();
			}
#endif
			dprintf("%s: media_playURL(%s)\n", __FUNCTION__, URL);
			return media_playURL(screenMain, URL, m3u_description, NULL);
		} else
#ifdef ENABLE_VOD
		if (strncasecmp(URL, URL_RTSP_MEDIA, sizeof(URL_RTSP_MEDIA)-1) == 0)
		{
			appControlInfo.playbackInfo.playlistMode = playlistModeFavorites;
			appControlInfo.playbackInfo.streamSource = streamSourceFavorites;
			strcpy(playlist_lastURL, URL);
			dprintf("%s: rtsp_playURL(%s)\n", __FUNCTION__,URL);
			return rtsp_playURL(screenMain, URL, m3u_description, NULL);
		} else
#endif
#ifdef ENABLE_IPTV
		if (strncasecmp(URL, URL_RTP_MEDIA, sizeof(URL_RTP_MEDIA)-1) == 0 ||
		    strncasecmp(URL, URL_UDP_MEDIA, sizeof(URL_UDP_MEDIA)-1) == 0 ||
		    strncasecmp(URL, URL_IGMP_MEDIA, sizeof(URL_IGMP_MEDIA)-1) == 0)
		{
			appControlInfo.playbackInfo.playlistMode = playlistModeFavorites;
			appControlInfo.playbackInfo.streamSource = streamSourceFavorites;
			strcpy(playlist_lastURL, URL);
			dprintf("%s: rtp_playURL(%s)\n", __FUNCTION__,URL);
			return rtp_playURL(screenMain, URL, m3u_description, NULL);
		} else
#endif
#ifdef ENABLE_DVB
		if (strncasecmp(URL, URL_DVB_MEDIA, sizeof(URL_DVB_MEDIA)-1) == 0)
		{
			appControlInfo.playbackInfo.playlistMode = playlistModeFavorites;
			appControlInfo.playbackInfo.streamSource = streamSourceFavorites;
			appControlInfo.playbackInfo.thumbnail[0] = 0;
			strcpy(appControlInfo.playbackInfo.description, m3u_description);
			strcpy(playlist_lastURL, URL);
			dprintf("%s: offair_playURL(%s)\n", __FUNCTION__,URL);
			offair_playURL(URL,screenMain);
		} else
#endif
		{
			eprintf("playlist: Incorrect URL '%s'\n", URL);
			interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
			return -1;
		}
	}

	return 0;
}

int playlist_setChannel(int channel, void* pArg)
{
	if( channel < 1 || channel > playlist_getCount() )
	{
		return 1;
	}
	channel--;
	return playlist_stream_change( (interfaceMenu_t*)&playlistMenu, SET_NUMBER(channel) );
}

static int playlist_validateURL(char *value)
{
	char *ptr1;

	if (value != NULL)
	{
		strcpy(playlist_lastURL, value);

		ptr1 = strstr(value, "://");

		if (ptr1 == 0)
		{
			interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
			return -1;
		}

		if (
#ifdef ENABLE_VOD
			strncasecmp(value, URL_RTSP_MEDIA,  sizeof(URL_RTSP_MEDIA) -1) != 0 &&
#endif
//			strncasecmp(value, URL_FILE_MEDIA,  sizeof(URL_FILE_MEDIA) -1) != 0 &&
#ifdef ENABLE_IPTV
			strncasecmp(value, URL_IGMP_MEDIA,  sizeof(URL_IGMP_MEDIA) -1) != 0 &&
			strncasecmp(value, URL_RTP_MEDIA,   sizeof(URL_RTP_MEDIA)  -1) != 0 &&
			strncasecmp(value, URL_UDP_MEDIA,   sizeof(URL_UDP_MEDIA)  -1) != 0 &&
#endif
#ifdef ENABLE_DVB
			strncasecmp(value, URL_DVB_MEDIA,   sizeof(URL_DVB_MEDIA)  -1) != 0 &&
#endif
			strncasecmp(value, URL_HTTP_MEDIA,  sizeof(URL_HTTP_MEDIA) -1) != 0 &&
			strncasecmp(value, URL_HTTPS_MEDIA, sizeof(URL_HTTPS_MEDIA)-1) != 0 &&
			strncasecmp(value, URL_RTMP_MEDIA,  sizeof(URL_RTMP_MEDIA) -1) != 0)
		{
			interface_showMessageBox(_T("ERR_INCORRECT_PROTO"), thumbnail_error, 0);
			return -1;
		}
	} else {
		interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
		return -1;
	}
	return 0;
}

static int playlist_inputURL(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int res;
	char *str;

	if( value == NULL)
		return 1;

	if( (res = playlist_validateURL(value)) != 0)
	{
		return res;
	}

	str = rindex(value, '/');
	if( str != NULL )
	{
		str++;
		if( strlen( str ) == 0)
			str = NULL;
	}

	if ( (res = playlist_addUrl(value,str)) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
		return res;
	}
	return res;
}

static int playlist_saveURL(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int res;
	char description[MENU_ENTRY_INFO_LENGTH];

	if (value != NULL)
	{
		if( (res = playlist_validateURL(value)) != 0)
		{
			return res;
		}
	} else
	{
		return 1;
	}

	if((res = m3u_getEntry( PLAYLIST_FILENAME, GET_NUMBER(pArg))) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
		return res;
	}
	strcpy(description, m3u_description);
	if((res = m3u_replaceEntryByIndex( PLAYLIST_FILENAME, GET_NUMBER(pArg), value, description )) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
	} else
	{
		playlist_fillMenu(value);
	}
	return res;
}

static int playlist_saveDescription(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int res;
	char url[MAX_URL];

	if( value == NULL )
	{
		return 1;
	}

	if((res = m3u_getEntry( PLAYLIST_FILENAME, GET_NUMBER(pArg) )) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
		return 1;
	}
	strcpy(url, m3u_url);
	if((res = m3u_replaceEntryByIndex( PLAYLIST_FILENAME, GET_NUMBER(pArg), url, value )) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
	} else
	{
		playlist_fillMenu(value);
	}
	return res;
}

int playlist_addUrl(char* url, char* description)
{
#ifdef ENABLE_FAVORITES
	FILE *file = m3u_initFile(PLAYLIST_FILENAME, "r");
	if (file == NULL )
	{
		if( m3u_createFile(PLAYLIST_FILENAME) != 0)
		{
			interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
			return -1;
		}
	} else
	{
		fclose(file);
	}

	if(m3u_findUrl(PLAYLIST_FILENAME, url) == 0)
	{
		interface_showMessageBox(_T("ADDED_TO_PLAYLIST"), thumbnail_yes, 3000);
		return 0;
	}

	if (m3u_addEntry(PLAYLIST_FILENAME, url, description) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
		return -1;
	}

	playlist_fillMenu(url);
	interface_showMessageBox(_T("ADDED_TO_PLAYLIST"), thumbnail_yes, 3000);
	
#else
	eprintf("playlist: disabled\n");
#endif
	return 0;
}

static int playlist_deleteURL(interfaceMenu_t *pMenu, void* pArg)
{
	int res;
	if( (res = m3u_deleteEntryByIndex( PLAYLIST_FILENAME, GET_NUMBER(pArg))) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
	}
	return res;
}

static char *playlist_urlvalue(int num, void *pArg)
{
	return num == 0 ? playlist_lastURL : NULL;
}

static char *playlist_description(int num, void *pArg)
{
	return num == 0 ? playlist_getDescription(GET_NUMBER(pArg)) : NULL;
}

static int playlist_enterURL(interfaceMenu_t *pMenu, void* pArg)
{

	interface_getText(pMenu,_T("ENTER_CUSTOM_URL"), "\\w+", playlist_inputURL, playlist_urlvalue, inputModeABC, pArg);

	return 0;
}

static int playlist_fillMenu(char *value)
{
	FILE *file;
	int i = 0, icon;
	char desc[MENU_ENTRY_INFO_LENGTH];
	
	interface_clearMenuEntries((interfaceMenu_t *)&playlistMenu);

	char *str = _T("ADD_URL");
	interface_addMenuEntry((interfaceMenu_t *)&playlistMenu, str, playlist_enterURL, (void*)-1, thumbnail_add_url);

	file = m3u_initFile(PLAYLIST_FILENAME, "r");
	if(file != NULL)
	{
		while ( m3u_readEntry(file) == 0 )
		{
#ifdef ENABLE_USB
			if( strncasecmp(m3u_url, URL_FILE_MEDIA,  sizeof(URL_FILE_MEDIA)-1) == 0 )
			{
#ifdef ENABLE_SAMBA
				if( strncmp( &m3u_url[sizeof(URL_FILE_MEDIA)-1], sambaRoot, strlen(sambaRoot) ) == 0 )
					icon = thumbnail_workstation;
				else
#endif
				icon = thumbnail_usb;
			} else
#endif
#ifdef ENABLE_IPTV
			if( strncasecmp(m3u_url, URL_IGMP_MEDIA,  sizeof(URL_IGMP_MEDIA)-1) == 0 ||
			    strncasecmp(m3u_url, URL_RTP_MEDIA,   sizeof(URL_RTP_MEDIA) -1) == 0 ||
			    strncasecmp(m3u_url, URL_UDP_MEDIA,   sizeof(URL_UDP_MEDIA) -1) == 0 )
			{
				icon = thumbnail_multicast;
			} else
#endif
#ifdef ENABLE_DVB
			if( strncasecmp(m3u_url, URL_DVB_MEDIA,   sizeof(URL_DVB_MEDIA)-1) == 0 )
			{
				icon = thumbnail_dvb;
			} else
#endif
#ifdef ENABLE_VOD
			if( strncasecmp(m3u_url, URL_RTSP_MEDIA,  sizeof(URL_RTSP_MEDIA)-1) == 0 )
			{
				icon = thumbnail_vod;
			} else
#endif
			if( strncasecmp(m3u_url, URL_HTTP_MEDIA,  sizeof(URL_HTTP_MEDIA)-1) == 0 ||
			    strncasecmp(m3u_url, URL_HTTPS_MEDIA, sizeof(URL_HTTPS_MEDIA)-1) == 0 ||
			    strncasecmp(m3u_url, URL_RTMP_MEDIA,  sizeof(URL_RTMP_MEDIA)-1) == 0 )
			{
#ifdef ENABLE_YOUTUBE
				if( strstr( m3u_url, "youtube.com/" ) != NULL )
					icon = thumbnail_youtube;
				else
#endif
#ifdef ENABLE_RUTUBE
				if( strstr( m3u_url, "rutube.ru/" ) != NULL )
					icon = thumbnail_rutube;
				else
#endif
				icon = thumbnail_internet;
			} else
			{
				eprintf("playlist: '%s' unsupported!\n", m3u_url);
				continue;
			}
			snprintf(desc, sizeof(desc), "%03d: %s", i+1, m3u_description);
			smartLineTrim(desc, sizeof(desc)-1 );
			interface_addMenuEntry((interfaceMenu_t*)&playlistMenu, desc, playlist_stream_change, SET_NUMBER(i), icon );
			i++;
			if( value != NULL && strcmp( m3u_url, value ) == 0 )
			{
				interface_setSelectedItem((interfaceMenu_t*)&playlistMenu, i);
			}
		}
	}

	if ( i == 0 )
	{
		char *str = _T("NO_MEDIA_URLS");
		interface_addMenuEntryDisabled((interfaceMenu_t *)&playlistMenu, str, thumbnail_info);
		interface_setSelectedItem((interfaceMenu_t *)&playlistMenu, MENU_ITEM_MAIN);
	}

	return 0;
}

int playlist_deleteCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int index;
	if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		index = GET_NUMBER(pArg);
		if( playlist_deleteURL(pMenu, pArg) == 0)
		{
			playlist_fillMenu(NULL);
			index++;
			if(index >= interface_getMenuEntryCount((interfaceMenu_t*)&playlistMenu))
			{
				index = interface_getMenuEntryCount((interfaceMenu_t*)&playlistMenu)-1;
			}
			if( interface_isMenuEntrySelectable((interfaceMenu_t*)&playlistMenu,index) == 0 )
			{
				index = 0;
			}
			interface_setSelectedItem((interfaceMenu_t*)&playlistMenu,index);
			interface_displayMenu(1);
		}
		return 0;
	} else if (cmd->command == interfaceCommandExit || cmd->command == interfaceCommandRed || cmd->command == interfaceCommandLeft)
	{
		return 0;
	}
	return 1;
}

int playlist_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	char URL[MAX_URL];
	char *str;
	if(pMenu->selectedItem <= 0 || GET_NUMBER(pArg) < 0)
	{
		return 1;
	}

	switch( cmd->command )
	{
		case interfaceCommandRed:
			// delete
			sprintf(URL, "%s\n\n%s", _T("CONFIRM_PLAYLIST_DELETE"), playlist_getURL(GET_NUMBER(pArg)));
			interface_showConfirmationBox(URL, thumbnail_question, playlist_deleteCallback, pArg);
			return 0;
		case interfaceCommandGreen:
			// info
			str = playlist_getURL(GET_NUMBER(pArg));
			eprintf("playlist: url %02d: %s\n", GET_NUMBER(pArg), str);
			interface_showMessageBox(str, thumbnail_info, 0);
			return 0;
		case interfaceCommandYellow:
			// modify
			strcpy(playlist_lastURL, playlist_getURL(GET_NUMBER(pArg)));
			{
				size_t url_len = strlen(playlist_lastURL);
				if( url_len < MAX_FIELD_PATTERN_LENGTH
#ifdef ENABLE_YOUTUBE
				    && strstr(playlist_lastURL, "youtube.com/") == NULL // forbid editing of YouTube links
#endif
				  )
				{
					interface_getText(pMenu, _T("ENTER_CUSTOM_URL"), "\\w+", playlist_saveURL, playlist_urlvalue, inputModeABC, pArg);
				} else
				{
					interface_showMessageBox( playlist_lastURL, thumbnail_info, 0 );
				}
			}
			return 0;
		case interfaceCommandBlue:
			// rename
			interface_getText(pMenu, _T("ENTER_TITLE"), "\\w+", playlist_saveDescription, playlist_description, inputModeABC, pArg);
			return 0;
		default:;
	}

	return 1;
}

void playlist_buildMenu(interfaceMenu_t *pParent)
{
	int playlist_icons[4] = { statusbar_f1_delete, statusbar_f2_info, statusbar_f3_edit, statusbar_f4_rename };
	createListMenu(&playlistMenu, _T("PLAYLIST"), thumbnail_favorites, playlist_icons, pParent,
				   /* interfaceInfo.clientX, interfaceInfo.clientY,
				   interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
				   NULL, NULL, NULL);

	interface_setCustomKeysCallback((interfaceMenu_t*)&playlistMenu, playlist_keyCallback);

	playlist_fillMenu(NULL);
}

static size_t http_playlist_callback(void *buffer, size_t size, size_t nmemb, void *userp)
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

static int playlist_getFromBufferXSPF( const char *data, const size_t size, xspfEntryHandler pEntryCallback, void *pArg )
{
	int ret = PLAYLIST_ERR_OK;
	
	//eprintf("%s: data = %s\n", __FUNCTION__, data);
	
	if( (ret = xspf_parseBuffer( data, pEntryCallback, pArg )) != 0 )
	{
		eprintf("%s: Error %d parsing XSPF playlist\n", __FUNCTION__, ret);
		ret = PLAYLIST_ERR_PARSE;
	} else
		ret = PLAYLIST_ERR_OK;
	return ret;
}

static int playlist_getFromBufferM3U( const char *data, const size_t size, xspfEntryHandler pEntryCallback, void *pArg )
{
	int ret = PLAYLIST_ERR_OK;
	int length = size;
	char *str = (char*)data;
	while( length > 0 && (ret = m3u_readEntryFromBuffer( &str, &length )) == 0 )
	{
		pEntryCallback(pArg, m3u_url, m3u_description, NULL);
	}
	if( length > 0 )
	{
		eprintf("%s: %d bytes left after M3U parse\n", __FUNCTION__, length);
		//ret = PLAYLIST_ERR_PARSE;
	}
	return ret;
}

int playlist_getFromBuffer(const char* data, const size_t size, xspfEntryHandler pEntryCallback, void *pArg)
{
	int ret;
	if( (ret = playlist_getFromBufferXSPF(data, size, pEntryCallback, pArg )) == PLAYLIST_ERR_PARSE )
		return playlist_getFromBufferM3U (data, size, pEntryCallback, pArg );
	else
		return ret;
}


#ifdef ENABLE_PLAYLIST_HTTP_HEADER
#define MAC_PATH "/sys/class/net/eth0/address"

char* providerCommonGetStbSerial(void)
{
	static char buf[64] = { 0 };
	if( buf[0] == 0 )
	{
		systemId_t     sysid;
		systemSerial_t serial;
		FILE *f = fopen("/dev/sysid", "r");
		if( f != NULL )
		{
			fread( buf, 1, sizeof(buf), f );
			fclose(f);
			if( sscanf( buf, "SYSID: %X, SERNO: %X", &sysid.IDFull, &serial.SerialFull ) == 2 )
				get_composite_serial(sysid, serial, buf);
			else
				buf[0] = 0;
		}
	}
	return buf;
}
#endif

int playlist_getFromURL(const char *url, xspfEntryHandler pEntryCallback, void *pArg)
{
	CURLcode ret;
	CURL *hnd;
	char data[DATA_BUFF_SIZE];
	char *content_type, *ext;
	int code, length;
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
	struct curl_slist  *headers = NULL;
	char header[128];
	char mac[32];
	FILE *file = NULL;
	
	snprintf(header, sizeof(header), "Elecard-StbSerial: %s", providerCommonGetStbSerial());
	headers = curl_slist_append(headers, header);
	file = fopen(MAC_PATH, "rt");
	if (file){
		fscanf (file, "%s", mac);
		fclose(file);
	} else {
		eprintf ("%s: ERROR! reading MAC from /sys/class/net/eth0/address file.\n", __FUNCTION__);
	}	
	snprintf(header, sizeof(header), "Elecard-StbMAC: %s", mac);
	headers = curl_slist_append(headers, header);
#endif

	data[0] = 0;

	hnd = curl_easy_init();

	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, http_playlist_callback);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, data);
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, errbuff);
	curl_easy_setopt(hnd, CURLOPT_CONNECTTIMEOUT, PLAYLIST_CONNECT_TIMEOUT);
	curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(hnd, CURLOPT_TIMEOUT, PLAYLIST_TIMEOUT);
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
	if(headers != NULL)
		curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, headers);
#endif
	appInfo_setCurlProxy(hnd);

	ret = curl_easy_perform(hnd);
	if (ret == CURLE_OK)
	{
		curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &code);
	} else
	{
		eprintf("%s: Failed(%d) to get playlist from '%s' with message:\n%s\n", __FUNCTION__, ret, url, errbuff);
		code = -1;
	}
	if ( code != 200 && appControlInfo.networkInfo.proxy[0] != 0 )
	{
		dprintf("%s: Retrying without proxy (code=%d)\n", __FUNCTION__, code);
		data[0]  = 0;
		curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, http_playlist_callback);
		curl_easy_setopt(hnd, CURLOPT_WRITEDATA, data);
		curl_easy_setopt(hnd, CURLOPT_URL, url);
		curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, errbuff);
		curl_easy_setopt(hnd, CURLOPT_CONNECTTIMEOUT, PLAYLIST_CONNECT_TIMEOUT);
		curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1);
		curl_easy_setopt(hnd, CURLOPT_TIMEOUT, PLAYLIST_TIMEOUT);
		curl_easy_setopt(hnd, CURLOPT_PROXY, "");
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
		if(headers != NULL)
			curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, headers);
#endif
		ret = curl_easy_perform(hnd);
		if(ret != CURLE_OK && ret != CURLE_WRITE_ERROR)
		{
			eprintf("%s: Failed to get playlist from '%s' (no proxy) with message:\n%s\n", __FUNCTION__, url, errbuff);
			ret = PLAYLIST_ERR_DOWNLOAD;
			goto finish;
		}
		curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &code);
		//dprintf("%s: 2nd perform response code=%d\n", __FUNCTION__, code);
	}
	if (code != 200)
	{
		eprintf("%s: Failed to get playlist from '%s' with message:\n%s\n", __FUNCTION__, url, errbuff);
		//interface_showMessageBox(_T("ERR_SERVICE_UNAVAILABLE"), thumbnail_error, 0);
		ret = PLAYLIST_ERR_DOWNLOAD;
		goto finish;
	} else
	{
		length = strlen(data);
		if( CURLE_OK != curl_easy_getinfo(hnd, CURLINFO_CONTENT_TYPE, &content_type) )
		{
			content_type = NULL;
		}
		dprintf("%s: Playlist Content-Type: %s\n", __FUNCTION__, content_type);
		//dprintf("%s: streams data %d:\n%s\n\n", __FUNCTION__, length, data);
	}

	ext = rindex(url, '.');
	if( (ext != NULL && strcasecmp(ext,".xspf") == 0 ) || (content_type != NULL
		&& strcasecmp( content_type, "application/xspf+xml" ) == 0) )
	{
		//eprintf("%s: playlist_getFromBufferXSPF...\n", __FUNCTION__);
		
		ret = playlist_getFromBufferXSPF(data, length, pEntryCallback, pArg);
	} else
	{
		/* Assume default playlist format m3u */
		ret = playlist_getFromBufferM3U (data, length, pEntryCallback, pArg);
	}
finish:
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
	if( headers != NULL )
		curl_slist_free_all(headers);
#endif
	curl_easy_cleanup(hnd);
	return ret;
}
