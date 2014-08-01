
/*
 youtube.cpp

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

#include "youtube.h"

#ifdef ENABLE_YOUTUBE

#include "debug.h"
#include "stb820.h"
#include "app_info.h"
#include "StbMainApp.h"
#include "output.h"
#include "stb_resource.h"
#include "l10n.h"
#include "media.h"
#include "playlist.h"
#include "helper.h"
#include "list.h"

#ifdef ENABLE_EXPAT
#include <expat.h>
#else
#include "xmlconfig.h"
#include <tinyxml.h>
#endif // ENABLE_EXPAT

#include <string>
#include <pthread.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define YOUTUBE_ID_LENGTH         (12)
#define YOUTUBE_LINK_SIZE         (48)
#define YOUTUBE_THUMBNAIL_SIZE    (64)
#define YOUTUBE_MAX_LINKS (32)
#define YOUTUBE_LINKS_PER_PAGE (4)
#define YOUTUBE_NEW_SEARCH (-1)
#define YOUTUBE_SEARCH_FILE CONFIG_DIR "/youtubesaerch.list"

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
typedef struct {
	char video_id[YOUTUBE_ID_LENGTH];
	char thumbnail[YOUTUBE_THUMBNAIL_SIZE];
} youtubeVideo_t;

typedef struct {
	char search[MAX_FIELD_PATTERN_LENGTH]; /**< Search request in readable form. If empty, standard feed is used. */
	youtubeVideo_t videos[YOUTUBE_MAX_LINKS];
	char current_id[YOUTUBE_ID_LENGTH];
	size_t count;
	size_t index;
	size_t search_offset;/** Current page of search results */
	bool youtube_canceled;
	int last_selected;
	struct list_head last_search;

	pthread_t search_thread;
} youtubeInfo_t;

#ifdef ENABLE_EXPAT
typedef enum {
	parseStart = 0,
	parseFeed,
	parseEntry,
	parseMediaGroup,
} parser_state_t;

typedef struct {
	parser_state_t state;
	char   title[MENU_ENTRY_INFO_LENGTH];
	size_t title_offset;
	XML_Parser p;
} youtube_parser_t;
#endif // ENABLE_EXPAT

typedef void (*youtubeEntryHandler)(const char *, const char *, const char *);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static youtubeInfo_t youtubeInfo;

#ifdef ENABLE_EXPAT
static youtube_parser_t youtube_parser;
#endif

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

interfaceListMenu_t YoutubeMenu;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int youtube_streamChange(interfaceMenu_t *pMenu, void *pArg);
/** @param pArg Result page
 * Each video list request is split to several pages.
 * On each page load youtube_fillMenu is called to update what is available. */
static int youtube_fillMenu( interfaceMenu_t* pMenu, void *pArg );
static int youtube_exitMenu( interfaceMenu_t* pMenu, void *pArg );
static char *youtube_getLastSearch(int index, void* pArg);
/** @param pArg Search offset or -1 to start a new one */
static int youtube_videoSearch(interfaceMenu_t *pMenu, void* pArg);
static int youtube_startVideoSearch(interfaceMenu_t *pMenu, char *value, void* pArg);
static int youtube_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static void youtube_menuDisplay(interfaceMenu_t *pMenu);
/** @param pArg Must be &YoutubeMenu */
static void *youtube_MenuVideoSearchThread(void* pArg);
static void youtube_runSearch(void* pArg);
static int youtubeSearchHist_save();
static int youtubeSearchHist_load();
static int youtubeSearchHist_check(char* search);

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

static size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp)
{
	std::string *data = (std::string *)userp;
	size_t wholesize = size*nmemb;

	if (youtubeInfo.youtube_canceled)
		return 0;

	data->append((const char*)buffer, wholesize);
	return wholesize;
}

#ifdef ENABLE_EXPAT
static size_t youtube_parseXml(void *buffer, size_t size, size_t nmemb, void *userp)
{
	XML_Parser p = (XML_Parser)userp;
	size_t numbytes = size*nmemb;
	if (youtubeInfo.youtube_canceled) 
		return 0;
	if (XML_Parse(p, (const char*)buffer, numbytes, 0) == 0)
		return 0;
	return numbytes;
}

void parser_getString(void *userData, const XML_Char *s, int len)
{
	if (youtube_parser.title_offset+1 >= sizeof(youtube_parser.title))
		return;

	if (youtube_parser.title_offset+len+1 >= sizeof(youtube_parser.title))
		len = sizeof(youtube_parser.title)-youtube_parser.title_offset-1;
	memcpy(&youtube_parser.title[youtube_parser.title_offset], (const char*)s, len);
	youtube_parser.title_offset += len;
	youtube_parser.title[youtube_parser.title_offset]=0;
}

void parser_startElement(void *data, const char *el, const char **attr) {
	XML_Parser p = (XML_Parser)data;
	//dprintf("%s[%d]: %s\n", __func__, youtube_parser.state, el);
	switch (youtube_parser.state)
	{
		case parseStart:
			if (strcmp(el,"feed")==0)
				youtube_parser.state = parseFeed;
			return;
		case parseFeed:
			if (strcmp(el,"entry")==0)
				youtube_parser.state = parseEntry;
			return;
		case parseEntry:
			if (strcmp(el,"media:group"))
				return;
			
			youtube_parser.state = parseMediaGroup;
			youtubeInfo.videos[youtubeInfo.count].video_id[0]=0;
			youtubeInfo.videos[youtubeInfo.count].thumbnail[0]=0;
			youtube_parser.title[0] = 0;
			youtube_parser.title_offset = 0;
			return;
		case parseMediaGroup:
			if (strcmp(el,"media:title")==0)
			{
				XML_SetCharacterDataHandler(p, parser_getString);
			} else
			if ( youtubeInfo.videos[youtubeInfo.count].thumbnail[0] == 0 && strcmp(el,"media:thumbnail")==0)
			{
				int i;
				for (i = 0; attr[i]; i += 2) {
					if (strcmp(attr[i], "url")==0)
					{
						size_t thumbnail_length = strlen(attr[i+1]);
						if (sizeof(youtubeInfo.videos[youtubeInfo.count].thumbnail) <= thumbnail_length)
							thumbnail_length = sizeof(youtubeInfo.videos[youtubeInfo.count].thumbnail)-1;
						memcpy(youtubeInfo.videos[youtubeInfo.count].thumbnail, attr[i+1], thumbnail_length);
						youtubeInfo.videos[youtubeInfo.count].thumbnail[thumbnail_length]=0;
					}
				}
			} else
			if ( youtubeInfo.videos[youtubeInfo.count].video_id[0] == 0 && strcmp(el,"media:content")==0)
			{
				int i;
				for (i = 0; attr[i]; i += 2) {
					if (strcmp(attr[i], "url")==0)
					{
						const char *str = strstr(attr[i+1], "/v/");
						if (str == NULL)
						{
							dprintf("%s: Can't find Youtube VIDEO_ID in '%s'\n", __FUNCTION__, attr[i+1]);
							break;
						}
						memcpy(youtubeInfo.videos[youtubeInfo.count].video_id, &str[3], YOUTUBE_ID_LENGTH-1);
						youtubeInfo.videos[youtubeInfo.count].video_id[YOUTUBE_ID_LENGTH-1]=0;
					}
				}
			}
			break;
	}
}

void parser_endElement(void *data, const char *el) {
	XML_Parser p = (XML_Parser)data;
	XML_SetCharacterDataHandler(p, NULL);
	//dprintf("%s[%d]: %s\n", __func__, youtube_parser.state, el);
	switch (youtube_parser.state)
	{
		case parseFeed:
			if (strcmp(el,"feed")==0)
				youtube_parser.state = parseStart;
			return;
		case parseEntry:
			if (strcmp(el,"entry")==0)
				youtube_parser.state = parseFeed;
			return;
		case parseMediaGroup:
			if (strcmp(el,"media:group")==0)
			{
				youtube_parser.state = parseEntry;

				//dprintf("%s[%d]: %s %s\n", __func__, youtubeInfo.count, youtubeInfo.videos[youtubeInfo.count].video_id, youtube_parser.title);
				if (youtubeInfo.videos[youtubeInfo.count].video_id[0]==0 ||youtube_parser.title_offset == 0)
					return;

				if (youtubeInfo.count >= YOUTUBE_MAX_LINKS || youtubeInfo.count < 0)
					return;

				int entryIndex = interface_addMenuEntry( _M &YoutubeMenu, youtube_parser.title, youtube_streamChange, SET_NUMBER(youtubeInfo.count), thumbnail_internet ) - 1;
				interface_setMenuEntryImage( _M &YoutubeMenu, entryIndex, youtubeInfo.videos[youtubeInfo.count].thumbnail );
				if (entryIndex % YOUTUBE_LINKS_PER_PAGE == 0)
					interface_displayMenu(1);

				youtubeInfo.count++;
			}
			return;
	}
}
#endif // ENABLE_EXPAT

int youtube_getVideoList(const char *url, youtubeEntryHandler pCallback, int page)
{
	CURLcode ret;
	CURL *hnd;
	char *str;

	const char *video_name, *video_type, *video_url, *thumbnail;
	static char err_buff[CURL_ERROR_SIZE];
#ifndef ENABLE_EXPAT
	xmlConfigHandle_t list;
	xmlConfigHandle_t item;
	TiXmlNode *entry;
	TiXmlNode *child;
	TiXmlElement *elem;
	std::string video_list;
#else
	XML_Parser p = XML_ParserCreate(NULL);
	if (!p)
	{
		eprintf("%s: Failed to create XML parser!\n", __FUNCTION__);
		return -1;
	}

	XML_SetUserData(p, p);
	XML_SetElementHandler(p, parser_startElement, parser_endElement);
	youtube_parser.state = parseStart;
#endif // ENABLE_EXPAT

	if ( url == 0 || pCallback == 0 )
		return -2;

	hnd = curl_easy_init();

#ifndef ENABLE_EXPAT
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, &video_list);
#else
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, youtube_parseXml);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, p);
#endif
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, err_buff);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 2);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 1);
	curl_easy_setopt(hnd, CURLOPT_CONNECTTIMEOUT, 15);
	curl_easy_setopt(hnd, CURLOPT_TIMEOUT, 15);
	appInfo_setCurlProxy(hnd);

	if (page == 1)
	{
		interface_showLoading();
		interface_displayMenu(1);
	}

	ret = curl_easy_perform(hnd);

	interface_hideLoading();

#ifdef ENABLE_EXPAT
	XML_ParserFree(p);
#else
	dprintf("%s: page %d of '%s' acquired (length %d)\n", __FUNCTION__, page, youtubeInfo.search[0] ? youtubeInfo.search : "standard feeds", video_list.size());
#endif

	curl_easy_cleanup(hnd);

	if (youtubeInfo.youtube_canceled) 
		return -1;

	if (ret != 0)
	{
		eprintf("Youtube: Failed to get video list from '%s': %s\n", url, err_buff);
		return -1;
	}
#ifndef ENABLE_EXPAT
	list = xmlConfigParse(video_list.c_str());
	if (list == NULL
		|| (item = xmlConfigGetElement(list, "feed", 0)) == NULL)
	{
		if (list)
		{
			eprintf("Youtube: parse error %s\n", ((TiXmlDocument*)list)->ErrorDesc());
			xmlConfigClose(list);
		}
		if (page == 1)interface_hideLoading();
		eprintf("Youtube: Failed to parse video list %d\n", page);
		return -1;
	}
	for ( entry = ((TiXmlNode *)item)->FirstChild(); entry != 0; entry = entry->NextSibling() )
	{
		if (entry->Type() == TiXmlNode::TINYXML_ELEMENT && strcasecmp(entry->Value(), "entry") == 0)
		{
			item = xmlConfigGetElement(entry, "media:group", 0);
			if ( item != NULL )
			{
				video_name = (char *)xmlConfigGetText(item, "media:title");
				video_url = NULL;
				thumbnail = NULL;
				for ( child = ((TiXmlNode *)item)->FirstChild();
					child != 0 && (video_url == NULL || thumbnail == NULL);
					child = child->NextSibling() )
				{
					if (child->Type() == TiXmlNode::TINYXML_ELEMENT )
					{
						if (strcasecmp(child->Value(), "media:content") == 0)
						{
							elem = (TiXmlElement *)child;
							video_type = elem->Attribute("type");
							if (strcmp( video_type, "application/x-shockwave-flash" ) == 0)
							{
								video_url = elem->Attribute("url");
							}
						} else if (thumbnail== 0 && strcasecmp(child->Value(), "media:thumbnail") == 0)
						{
							elem = (TiXmlElement *)child;
							thumbnail = elem->Attribute("url");
						}
					}
				}
				if (video_url != NULL)
				{
					//dprintf("%s: Adding Youtube video '%s' url='%s' thumb='%s'\n", __FUNCTION__,video_name,video_url,thumbnail);
					pCallback(video_url,video_name,thumbnail);
				}
			}
		}
	}
	xmlConfigClose(list);
#endif // !ENABLE_EXPAT
	if (page == 1) interface_hideLoading();
	return ret;
}

void youtube_addMenuEntry(const char *url, const char *name, const char *thumbnail)
{
	const char *str;
	if (youtubeInfo.count >= YOUTUBE_MAX_LINKS || youtubeInfo.count < 0)
		return;

	str = strstr(url, "/v/");
	if (str == NULL)
	{
		dprintf("%s: Can't find Youtube VIDEO_ID in '%s'\n", __FUNCTION__, url);
		return;
	}
	strncpy( youtubeInfo.videos[youtubeInfo.count].video_id, &str[3], 11 );
	youtubeInfo.videos[youtubeInfo.count].video_id[11] = 0;
	str = name != NULL ? name : url;
	if (thumbnail != NULL)
	{
		size_t thumbnail_len = strlen(thumbnail);
		if (thumbnail_len < sizeof( youtubeInfo.videos[youtubeInfo.count].thumbnail ))
		{
			memcpy( youtubeInfo.videos[youtubeInfo.count].thumbnail, thumbnail, thumbnail_len+1 );
		} else
		{
			eprintf("%s: too long thumnail url %d\n", __FUNCTION__, thumbnail_len);
			youtubeInfo.videos[youtubeInfo.count].thumbnail[0] = 0;
		}
	}
	else
		youtubeInfo.videos[youtubeInfo.count].thumbnail[0] = 0;

	int entryIndex = interface_addMenuEntry( _M &YoutubeMenu, str, youtube_streamChange, SET_NUMBER(youtubeInfo.count), thumbnail_internet ) - 1;
	interface_setMenuEntryImage( _M &YoutubeMenu, entryIndex, youtubeInfo.videos[youtubeInfo.count].thumbnail );

	youtubeInfo.count++;
}

int youtube_streamStart()
{
	return youtube_streamChange( (interfaceMenu_t*)&YoutubeMenu, SET_NUMBER(CHANNEL_CUSTOM) );
}

static int youtube_streamChange(interfaceMenu_t *pMenu, void *pArg)
{
	// assert (pMenu == _M &YoutubeMenu);

	CURLcode ret;
	CURL *hnd;
	char *str;
	static char url[MAX_URL];
	static std::string video_info;
	static char err_buff[CURL_ERROR_SIZE];
	int videoIndex = GET_NUMBER(pArg);

	if (videoIndex == CHANNEL_CUSTOM)
	{
		str = strstr( appControlInfo.mediaInfo.lastFile, "watch?v=" );
		if (str == NULL)
		{
			eprintf("%s: can't file YouTube ID in %s\n", __FUNCTION__, appControlInfo.mediaInfo.lastFile);
			interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);
			return -1;
		}
		str += 8;
		str[sizeof(youtubeInfo.current_id)-1] = 0;
		if (strlen(str) != YOUTUBE_ID_LENGTH-1)
		{
			eprintf("%s: invalid YouTube ID %s\n", __FUNCTION__, str);
			interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);
			return -1;
		}
		memcpy( youtubeInfo.current_id, str, sizeof(youtubeInfo.current_id) );
	} else if (videoIndex < 0 || videoIndex >= youtubeInfo.count)
	{
		eprintf("%s: there is no stream %d\n", __FUNCTION__, videoIndex);
		interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);
		return -1;
	} else
	{
		memcpy( youtubeInfo.current_id, youtubeInfo.videos[videoIndex].video_id, sizeof(youtubeInfo.current_id) );
	}

	video_info.clear();
	sprintf(url,"http://www.youtube.com/get_video_info?video_id=%s%s",youtubeInfo.current_id, "&eurl=&el=detailpage&ps=default&gl=US&hl=en");
	hnd = curl_easy_init();

	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, &video_info);
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, err_buff);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 2);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 1);
	curl_easy_setopt(hnd, CURLOPT_CONNECTTIMEOUT, 15);
	curl_easy_setopt(hnd, CURLOPT_TIMEOUT, 15);
	appInfo_setCurlProxy(hnd);

	ret = curl_easy_perform(hnd);

	eprintf("%s: video info for %s acquired (length %d)\n", __FUNCTION__, youtubeInfo.current_id, video_info.length());
	eprintf("%s:  YouTube URL %s\n", __FUNCTION__, url);

	curl_easy_cleanup(hnd);

	if (ret != 0)
	{
		eprintf("Youtube: Failed to get video info from '%s': %s\n", url, err_buff);
		interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);
		return 1;
	}

	/* Find direct link to video */

	char *fmt_url_map;
#ifdef STBPNX
	int supported_formats[] = { 18, 34, 0 };
#else
	int supported_formats[] = { 37, 22, 18, 35, 34, 0 };
#endif
	/* 37: MP4 1920x1080 H.264
	   22: MP4 1280x720  H.264
	   35: FLV  854x480  H.264
	   34: FLV  640x360  H.264
	   18: MP4  640x360  H.264
	*/

	int selected_fmt = sizeof(supported_formats) / sizeof(supported_formats[0]) - 1;
	int fmt;
	char *fmt_url, *next_url, *fmt_str, *saved_str;
	int i;

	// Trim everything except url_encoded_fmt_stream_map
	video_info.erase(0, video_info.find("url_encoded_fmt_stream_map="));
	video_info.erase(0, 27);
	size_t found = video_info.find('&');
	if (found != std::string::npos) {
		video_info.erase(found);
	}

	size_t info_length = video_info.length()+1;
	char buffer[info_length];

	if (utf8_urltomb(video_info.c_str(), info_length, buffer, info_length)  < 0) {
		eprintf("%s: failed to decode video_info\n", __FUNCTION__);
		goto getinfo_failed;
	}

	saved_str = NULL;
	fmt_url = NULL;
	str = buffer;
	do {
		next_url = strchr(str, ',');
		if(next_url) {
			next_url[0] = 0;
			next_url++;
		}

		fmt_str = strstr(str, "itag=");
		if(fmt_str) {
			fmt_str += 5;
			fmt = strtol(fmt_str, NULL, 10);
			for(i = 0; supported_formats[i] != 0; i++) {
				if(fmt == supported_formats[i]) {
					break;
				}
			}
			if(i < selected_fmt) {
				char *encoded_url = strstr(str, "url=");
				if(encoded_url) {

					fmt_url = encoded_url + 4;
					selected_fmt = i;
					saved_str = str;
				}
			}
			if(selected_fmt == 0 && fmt_url) {
				break;
			}
		}
		str = next_url;
	} while(str);

	if(!fmt_url) {
		eprintf("%s: no supported format found\n", __FUNCTION__);
		goto getinfo_failed;
	}

	str = strchr(fmt_url, '&');
	if(str) {
		*str = 0;
	}

	//eprintf("%s: encoded url: %s\n", __FUNCTION__, fmt_url);
	if(utf8_urltomb(fmt_url, strlen(fmt_url) + 1, url, sizeof(url) - 1) < 0) {
		eprintf("%s: Failed to decode '%s'\n", __FUNCTION__, fmt_url);
		goto getinfo_failed;
	}

	//previous extracting signature method
	if(strstr(url, "signature=") == NULL) {
		ssize_t url_len;
		char *sig;

#define SIGNATURE_PREFIX	"sig="
#define SIGNATURE_END		"&"
// #define SIGNATURE_PREFIX	"signature%3D"
// #define SIGNATURE_END		"%26"

		eprintf("%s: trying to search signature (old method)\n", __func__);
		if(saved_str == NULL) {
			eprintf("%s: Error! Nowhere to seek signature.\n", __func__);
			goto getinfo_failed;
		}
		sig = strstr(saved_str, SIGNATURE_PREFIX);
		if(sig) {
			sig += sizeof(SIGNATURE_PREFIX) - 1;

			str = strstr(sig, SIGNATURE_END);
			if(str) {
				*str = 0;
			}

			url_len = strlen(url);
			snprintf(url + url_len, sizeof(url) - url_len, "&signature=%s", sig);
		}
	}

	eprintf("Youtube: Playing (format %2d) '%s'\n", supported_formats[selected_fmt], url );
	{
		char *descr     = NULL;
		char *thumbnail = NULL;
		char temp[MENU_ENTRY_INFO_LENGTH];
		if(videoIndex != CHANNEL_CUSTOM) {
			youtubeInfo.index = videoIndex;
			int menuIndex = videoIndex+1+(youtubeInfo.search_offset > 0);
			if(interface_getMenuEntryInfo( (interfaceMenu_t*)&YoutubeMenu, menuIndex, temp, sizeof(temp) ) == 0) {
				descr = temp;
			}
			interface_setSelectedItem(_M &YoutubeMenu, menuIndex);
			thumbnail = youtubeInfo.videos[videoIndex].thumbnail[0] ? youtubeInfo.videos[videoIndex].thumbnail : NULL;
			appControlInfo.playbackInfo.channel = videoIndex+1;
			appControlInfo.playbackInfo.playlistMode = playlistModeYoutube;
		} else {
			youtubeInfo.index = 0;
			descr     = appControlInfo.playbackInfo.description;
			thumbnail = appControlInfo.playbackInfo.thumbnail;
		}
		appControlInfo.playbackInfo.streamSource = streamSourceYoutube;
		media_playURL(screenMain, url, descr, thumbnail != NULL ? thumbnail : resource_thumbnails[thumbnail_youtube] );
	}
	return 0;

getinfo_failed:
	interface_showMessageBox(_T("ERR_STREAM_NOT_SUPPORTED"), thumbnail_warning, 0);
	return 1;
}

int  youtube_startNextChannel(int direction, void* pArg)
{
	if (youtubeInfo.count == 0)
		return 1;

	int indexChange = (direction?-1:1);
	int newIndex = (youtubeInfo.index + indexChange + youtubeInfo.count)%youtubeInfo.count;

	return youtube_streamChange((interfaceMenu_t*)&YoutubeMenu, SET_NUMBER(newIndex));
}

int  youtube_setChannel(int channel, void* pArg)
{
	if (channel < 1 || channel > youtubeInfo.count)
		return 1;

	return youtube_streamChange((interfaceMenu_t*)&YoutubeMenu, SET_NUMBER(channel-1));
}

char *youtube_getCurrentURL()
{
	static char url[YOUTUBE_LINK_SIZE];

	if (youtubeInfo.current_id[0] == 0)
		url[0] = 0;
	else
		snprintf(url, sizeof(url), "http://www.youtube.com/watch?v=%s", youtubeInfo.current_id);
	return url;
}

static int youtube_fillFirstMenu( interfaceMenu_t* pMenu, void *pArg )
{
	if(youtubeInfo.search[0] != 0) {
		youtube_runSearch(pMenu);
	} else {
		youtube_fillMenu(pMenu, SET_NUMBER(1));
	}
	return 0;
}

void youtube_buildMenu(interfaceMenu_t* pParent)
{
	int youtube_icons[4] = { 0, 
		statusbar_f2_info,
#ifdef ENABLE_FAVORITES
		statusbar_f3_add,
#else
		0,
#endif
		0 };

	INIT_LIST_HEAD(&youtubeInfo.last_search);

	createListMenu(&YoutubeMenu, "YouTube", thumbnail_youtube, youtube_icons, pParent,
		interfaceListMenuIconThumbnail, youtube_fillFirstMenu, youtube_exitMenu, SET_NUMBER(1));
	interface_setCustomKeysCallback((interfaceMenu_t*)&YoutubeMenu, youtube_keyCallback);

// 	memset( &youtubeInfo, 0, sizeof(youtubeInfo) );
}

static int youtube_fillMenu( interfaceMenu_t* pMenu, void *pArg )
{
	int i;
	char *str;
	int page = GET_NUMBER(pArg);
	char url[64+MAX_FIELD_PATTERN_LENGTH];
	
	if (page == 1)
	{
		interface_clearMenuEntries( (interfaceMenu_t*)&YoutubeMenu );

		str = _T("VIDEO_SEARCH");
		interface_addMenuEntry( (interfaceMenu_t*)&YoutubeMenu, str, youtube_videoSearch, SET_NUMBER(YOUTUBE_NEW_SEARCH), thumbnail_search );

		if (youtubeInfo.search_offset > 0)
		{
			str = _T("SEARCHING_PREVIOUS_PAGE");

			interface_addMenuEntry( (interfaceMenu_t*)&YoutubeMenu, str, youtube_videoSearch, 
			                        SET_NUMBER(youtubeInfo.search_offset-1), icon_up );
		}

		youtubeInfo.count = 0;
		youtubeInfo.index = 0;
		YoutubeMenu.baseMenu.selectedItem = youtubeInfo.last_selected;
	}

#ifdef ENABLE_EXPAT
	if (youtubeInfo.search[0] == 0)
		snprintf(url, sizeof(url), "http://gdata.youtube.com/feeds/api/standardfeeds/top_rated?time=today&format=5");
	else
	{
		//snprintf(url, sizeof(url), "http://gdata.youtube.com/feeds/api/videos?format=5&q=%s", youtubeInfo.search);
		snprintf(url, sizeof(url), "http://gdata.youtube.com/feeds/api/videos?format=5&max-results=%d&start-index=%d&q=%s",
		         YOUTUBE_MAX_LINKS, youtubeInfo.search_offset*YOUTUBE_MAX_LINKS+1, youtubeInfo.search);
	}
#else
	if (youtubeInfo.search[0] == 0)
		//snprintf(url, sizeof(url), "http://gdata.youtube.com/feeds/api/standardfeeds/recently_featured?format=5&max-results=%d&start-index=%d", YOUTUBE_LINKS_PER_PAGE, (page-1)*YOUTUBE_LINKS_PER_PAGE+1);
		snprintf(url, sizeof(url), "http://gdata.youtube.com/feeds/api/standardfeeds/top_rated?time=today&format=5&max-results=%d&start-index=%d",
		         YOUTUBE_LINKS_PER_PAGE, (page-1)*YOUTUBE_LINKS_PER_PAGE+1);
	else
	{
		snprintf(url, sizeof(url), "http://gdata.youtube.com/feeds/api/videos?format=5&max-results=%d&start-index=%d&q=%s",
		         YOUTUBE_LINKS_PER_PAGE, (page-1)*YOUTUBE_LINKS_PER_PAGE+youtubeInfo.search_offset*YOUTUBE_MAX_LINKS+1, youtubeInfo.search);
	}
#endif // ENABLE_EXPAT
	int ret = youtube_getVideoList(url, youtube_addMenuEntry, page);

	if (( youtubeInfo.count == 0 ) && ( page == 1 ))
	{
		str = _T("NO_MOVIES");
		interface_addMenuEntryDisabled( (interfaceMenu_t*)&YoutubeMenu, str, thumbnail_info );
	}

#ifndef ENABLE_EXPAT
	if (( youtubeInfo.search[0] == 0 ) && ( page == 1 ) && (ret == 0))
	{
		pthread_create(&youtubeInfo.search_thread, NULL, youtube_MenuVideoSearchThread, &YoutubeMenu);
	}
#endif
	return 0;
}

static int youtube_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	// assert (pMenu == _M &YoutubeMenu);

	if (cmd->command == interfaceCommandSearch)
		return youtube_videoSearch(pMenu, SET_NUMBER(YOUTUBE_NEW_SEARCH));

	int selectedIndex = interface_getSelectedItem(pMenu);
	if (selectedIndex > 0 && pMenu->menuEntry[selectedIndex].pAction == youtube_streamChange)
	{
		char url[YOUTUBE_LINK_SIZE];
		int videoIndex = GET_NUMBER(interface_getMenuEntryArg(pMenu, selectedIndex));
		// assert (videoIndex >= 0 && videoIndex < youtubeInfo.count);

		switch (cmd->command)
		{
			case interfaceCommandInfo:
			case interfaceCommandGreen:
				snprintf(url, sizeof(url), "http://www.youtube.com/watch?v=%s", youtubeInfo.videos[videoIndex].video_id);
				eprintf("Youtube: Stream %02d: '%s'\n", videoIndex, url);
				interface_showMessageBox(url, -1, 0);
				return 0;
#ifdef ENABLE_FAVORITES
			case interfaceCommandYellow:
				char description[MENU_ENTRY_INFO_LENGTH];
				interface_getMenuEntryInfo( pMenu, selectedIndex, description, sizeof(description) );
				snprintf(url, sizeof(url), "http://www.youtube.com/watch?v=%s", youtubeInfo.videos[videoIndex].video_id);
				eprintf("Youtube: Add to Playlist '%s'\n", url);
				playlist_addUrl( url, description );
				return 0;
				break;
#endif
			default:;
		}
	}
	return 1;
}

static int youtube_exitMenu( interfaceMenu_t* pMenu, void *pArg )
{
	int selectedIndex = interface_getSelectedItem(pMenu);
	youtubeInfo.last_selected = (selectedIndex > 0) ? selectedIndex : 0;
	if ( youtubeInfo.search_thread != 0 )
	{
		youtubeInfo.youtube_canceled = true;
		pthread_join( youtubeInfo.search_thread, NULL );
	}
	return 0;
}

static int youtube_videoSearch(interfaceMenu_t *pMenu, void* pArg)
{
	youtubeInfo.search_offset = GET_NUMBER(pArg);
	youtubeInfo.last_selected = 0;

	if(youtubeInfo.search_offset == YOUTUBE_NEW_SEARCH) {
		int32_t i;
		const char *str;

		youtubeSearchHist_load();
		youtubeInfo.search_offset = 0;

		interface_listBoxGetText(pMenu, _T("ENTER_TITLE"), "\\w+", youtube_startVideoSearch, youtube_getLastSearch, inputModeABC, NULL);
		interface_addToListBox(_T("VIDEO_SEARCH"), NULL, NULL);

		i = 0;
		while((str = strList_get(&youtubeInfo.last_search, i)) != NULL) {
			interface_addToListBox(str, NULL, NULL);
			i++;
		}
		interface_displayMenu(1);
	}
	else
		youtube_runSearch(pMenu);

	return 0;
}

static char *youtube_getLastSearch(int index, void* pArg)
{
	if(index == 0) {
		static char last_search[sizeof(youtubeInfo.search)];
		int search_length;

		search_length = utf8_uritomb(youtubeInfo.search, strlen(youtubeInfo.search), last_search, sizeof(last_search)-1 );
		if(search_length < 0) {
			return youtubeInfo.search;
		} else {
			last_search[search_length] = 0;
			return last_search;
		}
	} else {
		return NULL;
	}
}

static void youtube_MenuVideoSearchThreadTerm(void* pArg)
{
	youtubeInfo.search_thread = 0;
	youtubeInfo.youtube_canceled = false;
}

static void *youtube_MenuVideoSearchThread(void* pArg)
{
	int page;
	char *str;
	interfaceMenu_t *pMenu = (interfaceMenu_t *) pArg;
	int old_count = -1;

	if (youtubeInfo.search[0] == 0)
		page = 2;
	else
		page = 1;

	pthread_cleanup_push(youtube_MenuVideoSearchThreadTerm, NULL);

#ifndef ENABLE_EXPAT
	for (;(( page < YOUTUBE_MAX_LINKS/YOUTUBE_LINKS_PER_PAGE + 1 ) &&
	       ( old_count != youtubeInfo.count )); page++)
	{
		old_count = youtubeInfo.count;
#endif
		pthread_testcancel();
		youtube_fillMenu( pMenu, (void *)page );
		interface_displayMenu(1);
#ifndef ENABLE_EXPAT
	}
#endif
	pthread_cleanup_pop(1);

	if (youtubeInfo.search[0] != 0)
	{
		str = _T("SEARCHING_NEXT_PAGE");
		interface_addMenuEntry( (interfaceMenu_t*)&YoutubeMenu, str, youtube_videoSearch, SET_NUMBER(youtubeInfo.search_offset+1), icon_down );
		interface_displayMenu(1);
	}
}

static void youtube_runSearch(void *pArg)
{
	if (youtubeInfo.search_thread)
	{
		/* terminates the previous (not ended) search thread */
		youtubeInfo.youtube_canceled = true;
		pthread_join(youtubeInfo.search_thread, NULL);
	}

	pthread_create(&youtubeInfo.search_thread, NULL, youtube_MenuVideoSearchThread, pArg);
}

static int youtubeSearchHist_load()
{
	FILE *fd = fopen(YOUTUBE_SEARCH_FILE, "r");
	strList_release(&youtubeInfo.last_search);
	if(fd != NULL) {
		int i = 0;
		while(!feof(fd)) {
			char buf[MAX_FIELD_PATTERN_LENGTH];
			if(fgets(buf, sizeof(buf), fd) != NULL) {
				char *str = cutEnterInStr(buf);
				strList_add(&youtubeInfo.last_search, str);
				free(str);
				i++;
				if(i == appControlInfo.youtubeSearchNumber) {
					break;
				}
			}
		}
		fclose(fd);
	}
}

static int youtubeSearchHist_save()
{
	const char *str;
	int32_t i;
	int32_t fd;
	
	fd = open(YOUTUBE_SEARCH_FILE, O_WRONLY | O_CREAT | O_TRUNC);
	if(fd == NULL) {
		eprintf("File with searches can't open!\n");
		return -1;
	}

	i = 0;
	while((str = strList_get(&youtubeInfo.last_search, i)) != NULL) {
		write(fd, str, strlen(str));
		write(fd, "\n", 1);
		i++;
	}

	close(fd);
	strList_release(&youtubeInfo.last_search);
	return 0;
}

static int youtubeSearchHist_check(char* search)
{
	int newSearchIndex = strList_find(&youtubeInfo.last_search, search);
	if(newSearchIndex == -1) {
		if(strList_count(&youtubeInfo.last_search) == appControlInfo.youtubeSearchNumber) {
			strList_remove_last(&youtubeInfo.last_search);
		}
		strList_add_head(&youtubeInfo.last_search, search);
	} else if(newSearchIndex >= 0) {
		strList_remove(&youtubeInfo.last_search, search);
		strList_add_head(&youtubeInfo.last_search, search);
	}
	return 0;
}
static int youtube_startVideoSearch(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	(void)pArg;
	int search_length;
	char buf[sizeof(youtubeInfo.search)];
	int thread_create = -1;

	if ( value == NULL )
		return 1;

	interface_hideMessageBox();
	youtubeInfo.search_offset = 0;

	if (value[0] != 0)
	{
		search_length = utf8_mbtouri(value, strlen(value), buf, sizeof(buf)-1 );
		if (search_length < 0)
		{
			interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 5000);
			return 1;
		}
		buf[search_length] = 0;
		strncpy(youtubeInfo.search, buf, search_length+1);

		youtubeSearchHist_check(value);
		youtubeSearchHist_save();
		youtube_runSearch(pMenu);
	} else {
		youtubeInfo.search[0] = 0;
		youtube_fillMenu(pMenu, SET_NUMBER(1));
	}

	return 0;
}

#endif // ENABLE_YOUTUBE
