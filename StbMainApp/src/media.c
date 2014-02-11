
/*
 media.c

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

#include "media.h"

#include "debug.h"
#include "app_info.h"
#include "gfx.h"
#include "l10n.h"
#include "StbMainApp.h"
#include "helper.h"
#include "menu_app.h"
#include "rtp.h"
#include "rtsp.h"
#include "off_air.h"
#include "list.h"
#include "rtp_common.h"
#include "playlist.h"
#include "sound.h"
#include "samba.h"
#include "dlna.h"
#include "youtube.h"
#include "rutube.h"
#ifdef ENABLE_TELETES
#include "../third_party/teletes/teletes.h"
#endif
#ifdef ENABLE_VIDIMAX
#include "vidimax.h"
#endif

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <libgen.h>
#include <time.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#ifdef STBPNX
#define MEDIA_ROOT "/usb/"
#endif
#if (defined STSDK) || (defined STBuemd)
#define MEDIA_ROOT "/mnt/"
#endif
#ifndef MEDIA_ROOT
#define MEDIA_ROOT "/media/"
#endif

#define MAX_BUF_LEN_GETSTREAMS (188*88)

#define MAX_READ_COUNT_GETSTREAMS (120)

#define MAX_DEV_PATH   (16)

#ifdef ENABLE_SAMBA
#define ROOT         (media_browseType == mediaBrowseTypeUSB ? usbRoot : sambaRoot)
#define IS_SMB(path) (strncmp((path), sambaRoot, strlen(sambaRoot))==0)
#else
#define ROOT (usbRoot)
#endif
#define IS_USB(path) (strncmp((path), usbRoot, strlen(usbRoot))==0)

#define FORMAT_ICON (appControlInfo.mediaInfo.typeIndex < 0 ? thumbnail_file : media_formats[appControlInfo.mediaInfo.typeIndex].icon )

#define IS_URL(x) ((strncasecmp((x), "http://", 7) == 0) || (strncasecmp((x), "rtmp://", 7) == 0) || (strncasecmp((x), "https://", 8) == 0) || (strncasecmp((x), "ftp://", 6) == 0) || (strncasecmp((x), "fusion://", 7) == 0))

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

typedef enum __mediaBrowseType_t
{
	mediaBrowseTypeUSB = 0,
	mediaBrowseTypeSamba,
	mediaBrowseTypeCount
} mediaBrowseType_t;

typedef struct __mediaFormatInfo_t
{
	char *name;
	char *ext;
} mediaFormatInfo_t;

typedef struct __mediaFormats_t
{
	const mediaFormatInfo_t* info;
	const size_t count;
	const imageIndex_t icon;
} mediaFormats_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/** Refresh content of BrowseFilesMenu
 *  Can change currentPath automatically if necessary. Initializes media_currentDirEntries and media_currentFileEntries. Don't use blocks.
 *  @param[in] pMenu Should be &BrowseFilesMenu
 *  @param[in] pArg  Index of selected item in updated menu. Can also have special values MENU_ITEM_LAST and MENU_ITEM_BACK
 */
static int  media_refreshFileBrowserMenu(interfaceMenu_t *browseMenu, void* pSelected);

/** ActivatedAction of BrowseFilesMenu
 *  Calls media_refreshFileBrowserMenu using blocks.
 */
static int  media_enterFileBrowserMenu(interfaceMenu_t *browseMenu, void* pSelected);

static int  media_enterSettingsMenu(interfaceMenu_t *settingsMenu, void* pArg);

//static int  media_startPlayback();
int  media_startPlayback();
void media_pauseOrStop(int stop);
inline static void media_pausePlayback();
static int  media_stream_change(interfaceMenu_t *pMenu, void* pArg);
#ifdef ENABLE_AUTOPLAY
static int  media_auto_play(void* pArg);
static int  media_stream_selected(interfaceMenu_t *pMenu, void* pArg);
static int  media_stream_deselected(interfaceMenu_t *pMenu, void* pArg);
#endif
static void media_setupPlayControl(void* pArg);
static int  media_check_storages(void* pArg);
static int  media_leaveBrowseMenu(interfaceMenu_t *pMenu, void* pArg);
static int  media_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int  media_settingsKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);

int         media_startNextChannel(int direction, void* pArg);
static int  media_browseFolder(interfaceMenu_t *pMenu, void* pArg);
static int  media_browseParentFolder(interfaceMenu_t *pMenu, void* ignored);
static int  media_toggleMediaType(interfaceMenu_t *pMenu, void* pArg);
static int  media_togglePlaybackMode(interfaceMenu_t *pMenu, void* pArg);
static int  media_toggleSlideshowMode(interfaceMenu_t *pMenu, void* pArg);
#ifndef STBPNX
static void media_slideshowReleaseImage(void);
#else
static inline void media_slideshowReleaseImage(void) {}
#endif

static void media_freeBrowseInfo(void);

static int  media_select(const struct dirent * de, char *dir, int fmt);
static int  media_select_current(const struct dirent * de);
static int  media_select_list(const struct dirent * de);
static int  media_select_dir(const struct dirent * de);
static int  media_select_image(const struct dirent * de);

static void media_getFileDescription();

static int  media_slideshowEvent(void *pArg);
static int  media_showCover(char* filename);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static const mediaFormatInfo_t media_videoFormats[] =
{
	{ "MPEG-1",              ".m1v" },
	{ "MPEG-1",              ".mpv" },
//	{ "MPEG-1",              ".dat" },
	{ "MPEG-2",              ".vob" },
	{ "MPEG-2",              ".mpg" },
	{ "MPEG-2",              ".mpeg" },
	{ "MPEG-2",              ".m2v" },
	{ "MPEG-4",              ".mp4" },
	{ "DivX/XviD",           ".avi" },
	{ "Windows Media Video", ".wmv" },
	{ "H.264 Video",         ".h264" },
	{ "H.264 Video",         ".264" },
	{ "MPEG TS",             ".m2ts" },
	{ "MPEG TS",             ".mpt" },
	{ "MPEG TS",             ".ts" },
	{ "MPEG TS",             ".mts" },
	{ "MPEG TS",             ".m2t" },
	{ "MPEG TS",             ".trt" },
	{ "Flash Video",         ".flv" },
	{ "MPEG-2 PS",           ".m2p" },
	{ "Matroska",            ".mkv" },
	{ "H.264 ES",            ".es_v_h264" },
	{ "MPEG-2 ES",           ".es_v_mpeg2" },
	{ "MPEG-4 ES",           ".es_v_mpeg4" }
};
static const mediaFormatInfo_t media_audioFormats[] =
{
	{ "MP3",                 ".mp3" },
	{ "Windows Media Audio", ".wma" },
	{ "Ogg Vorbis",          ".ogg" },
	{ "AAC",                 ".aac" },
	{ "AC3",                 ".ac3" },
	{ "MPEG-2 Audio",        ".m2a" },
	{ "MPEG-2 Audio",        ".mp2" },
	{ "MPEG-1 Audio",        ".m1a" },
	{ "MPEG-1 Audio",        ".mp1" },
	{ "MPEG-1 Audio",        ".mpa" },
	{ "Audio ES",            ".aes" }
};
static const mediaFormatInfo_t media_imageFormats[] =
{
	{ "JPEG",                ".jpeg" },
	{ "JPG",                 ".jpg" },
	{ "PNG",                 ".png" },
	{ "BMP",                 ".bmp" }
};

// Order must match mediaType enum from app_info.h
static const mediaFormats_t media_formats[] = {
	{ media_videoFormats, sizeof(media_videoFormats)/sizeof(mediaFormatInfo_t), thumbnail_usb_video },
	{ media_audioFormats, sizeof(media_audioFormats)/sizeof(mediaFormatInfo_t), thumbnail_usb_audio },
	{ media_imageFormats, sizeof(media_imageFormats)/sizeof(mediaFormatInfo_t), thumbnail_usb_image }
};

static int media_forceRestart = 0;

/* start/stop semaphore */
static pmysem_t  media_semaphore;
static pmysem_t  slideshow_semaphore;

interfaceListMenu_t media_settingsMenu;

/* File browser */
interfaceListMenu_t BrowseFilesMenu;

static struct dirent **media_currentDirEntries = NULL;
static int             media_currentDirCount = 0;
static struct dirent **media_currentFileEntries = NULL;
static int             media_currentFileCount = 0;

static char  * playingPath = (char*)usbRoot;
static char    media_previousDir[PATH_MAX] = { 0 };
static char    media_failedImage[PATH_MAX] = { 0 };
static char    media_failedMedia[PATH_MAX] = { 0 };

static mediaBrowseType_t media_browseType = mediaBrowseTypeUSB;

#ifdef ENABLE_AUTOPLAY
static int playingStream = 0;
#endif

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

const  char    usbRoot[] = MEDIA_ROOT;
// current path for selected media type
char           currentPath[PATH_MAX] = MEDIA_ROOT;

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

void media_getFilename(int fileNumber)
{
	appControlInfo.playbackInfo.playlistMode = playlistModeNone;
	appControlInfo.playbackInfo.playingType = appControlInfo.mediaInfo.typeIndex;
	snprintf(appControlInfo.mediaInfo.filename, PATH_MAX, "%s%s", currentPath, media_currentFileEntries[ fileNumber - media_currentDirCount - 1 /* ".." */ ]->d_name);
}

mediaType media_getMediaType(const char *filename)
{
	size_t i, nameLength, searchLength;
	nameLength = strlen(filename);

	for( i = 0; i < media_formats[mediaVideo].count; ++i)
	{
		searchLength = strlen(media_formats[mediaVideo].info[i].ext);
		//dprintf("%s: %s : %s\n", __FUNCTION__,de->d_name,searchStrings[i]);
		if(nameLength>searchLength && strcasecmp(&filename[nameLength-searchLength], media_formats[mediaVideo].info[i].ext)==0)
			return mediaVideo;
	}
	for( i = 0; i < media_formats[mediaAudio].count; ++i)
	{
		searchLength = strlen(media_formats[mediaAudio].info[i].ext);
		//dprintf("%s: %s : %s\n", __FUNCTION__,de->d_name,searchStrings[i]);
		if(nameLength>searchLength && strcasecmp(&filename[nameLength-searchLength], media_formats[mediaAudio].info[i].ext)==0)
			return mediaAudio;
	}
	for( i = 0; i < media_formats[mediaImage].count; ++i)
	{
		searchLength = strlen(media_formats[mediaImage].info[i].ext);
		//dprintf("%s: %s : %s\n", __FUNCTION__,de->d_name,searchStrings[i]);
		if(nameLength>searchLength && strcasecmp(&filename[nameLength-searchLength], media_formats[mediaImage].info[i].ext)==0)
			return mediaImage;
	}
	return mediaAll;
}

int strnaturalcmp(const char *s1, const char *s2) {
  for (;;) {
    if (*s2 == '\0')
      return *s1 != '\0';
    else if (*s1 == '\0')
      return 1;
    else if (!(isdigit(*s1) && isdigit(*s2))) {
      if (*s1 != *s2)
        return (int)*s1 - (int)*s2;
      else
        (++s1, ++s2);
    } else {
      char *lim1, *lim2;
      unsigned long n1 = strtoul(s1, &lim1, 10);
      unsigned long n2 = strtoul(s2, &lim2, 10);
      if (n1 > n2)
        return 1;
      else if (n1 < n2)
        return -1;
      s1 = lim1;
      s2 = lim2;
    }
  }
}

#ifndef STBPNX
int naturalsort (const struct dirent **e1,
                 const struct dirent **e2)
{
#else
int naturalsort (const void *v1,
                 const void *v2)
{
	struct dirent* const *e1 = v1;
	struct dirent* const *e2 = v2;
#endif
	int result = ((*e1)->d_type & DT_DIR) - ((*e2)->d_type & DT_DIR);
	if (result)
		return result;
	return strnaturalcmp((*e1)->d_name, (*e2)->d_name);
}

#ifdef STBPNX
static int  media_adjustStoragePath(char *oldPath, char *newPath)
{
	char *filePath;
	struct dirent **storages;
	int             storageCount;
	int i;
	char _currentPath[PATH_MAX];

	if( media_scanStorages() < 1 )
		return -1;
	/* 10 == strlen("/usb/Disk "); */
	filePath = strchr(&oldPath[10],'/');
	if(filePath == NULL)
		return -1;
	dprintf("%s: %s\n", __FUNCTION__,filePath);
	strcpy(_currentPath,currentPath);
	strcpy(currentPath,usbRoot);
	storageCount = scandir(usbRoot, &storages, media_select_dir, appControlInfo.mediaInfo.fileSorting);
	strcpy(currentPath,_currentPath);
	if(storageCount < 0)
	{
		eprintf("media: Error getting list of storages at '%s'\n",usbRoot);
		return -3;
	}
	for( i = 0; i < storageCount ; ++i )
	{
		sprintf(newPath,"%s%s%s", usbRoot, storages[i]->d_name, filePath);
		dprintf("%s: Trying '%s'\n", __FUNCTION__,newPath);
		if(helperFileExists(newPath))
		{
			for( ; i < storageCount; ++i )
				free(storages[i]);
			free(storages);
			return 0;
		}
		free(storages[i]);
	}
	free(storages);
	return -2;
}
#endif

int media_play_callback(interfacePlayControlButton_t button, void *pArg)
{
	double position,length_stream;
	int ret_val;
	char URL[MAX_URL], *str, *description;

	//dprintf("%s: in\n", __FUNCTION__);

	switch(button)
	{
	case interfacePlayControlPrevious:
		media_startNextChannel(1, pArg);
		break;
	case interfacePlayControlNext:
		media_startNextChannel(0, pArg);
		break;
	case interfacePlayControlAddToPlaylist:
		if( appControlInfo.playbackInfo.playlistMode != playlistModeFavorites && appControlInfo.mediaInfo.filename[0] != 0)
		{
			if( appControlInfo.mediaInfo.bHttp )
			{
#ifdef ENABLE_YOUTUBE
				if( strstr( appControlInfo.mediaInfo.filename, "youtube.com/" ) != NULL )
				{
					playlist_addUrl( youtube_getCurrentURL(), interfacePlayControl.description );
					break;
				}
#endif
#ifdef ENABLE_RUTUBE
				if( strstr( appControlInfo.mediaInfo.filename, "rutube.ru/" ) != NULL )
				{
					playlist_addUrl( rutube_getCurrentURL(), interfacePlayControl.description );
					break;
				}
#endif
				str = appControlInfo.mediaInfo.filename;
				description = interfacePlayControl.description;
			} else
			{
				snprintf(URL, MAX_URL,"file://%s",appControlInfo.mediaInfo.filename);
				str = URL;
				description = strrchr(str, '/');
				if( description != NULL )
				{
					description++;
				}
			}
			eprintf("media: Add to Playlist '%s' (%s)\n", str, description);
			playlist_addUrl(str, description);
		}
		break;
	case interfacePlayControlPlay:
		if ( !appControlInfo.mediaInfo.active )
		{
			appControlInfo.playbackInfo.scale = 1.0;
			media_startPlayback();
		}
		if (appControlInfo.mediaInfo.active && gfx_isTrickModeSupported(screenMain))
		{
			appControlInfo.playbackInfo.scale = 1.0;
			gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			interface_notifyText(NULL, 0);
			interface_playControlSelect(interfacePlayControlPlay);
			interface_playControlRefresh(1);
		}
		break;
	case interfacePlayControlFastForward:
		if ( !appControlInfo.mediaInfo.active )
		{
			media_startPlayback();
			if(gfx_isTrickModeSupported(screenMain))
			{
				gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			}
		}
		if (appControlInfo.mediaInfo.active && gfx_isTrickModeSupported(screenMain))
		{
			float newScale;

			if (gfx_videoProviderIsPaused(screenMain))
				gfx_resumeVideoProvider(screenMain);

			if( appControlInfo.playbackInfo.scale >= MAX_SCALE )
				newScale = 0.0;
			else if( appControlInfo.playbackInfo.scale > 0.0 )
				newScale = appControlInfo.playbackInfo.scale * 2;
			else if( appControlInfo.playbackInfo.scale < -2.0 )
				newScale = appControlInfo.playbackInfo.scale / 2;
			else
				newScale = 1.0;

			if( newScale != 0.0 && gfx_setSpeed(screenMain, newScale) == 0 )
				appControlInfo.playbackInfo.scale = newScale;
		}
		if( appControlInfo.playbackInfo.scale == 1.0 )
		{
			interface_notifyText(NULL, 0);
			interface_playControlSelect(interfacePlayControlPlay);
		} else
		{
			sprintf(URL, "%1.0fx", appControlInfo.playbackInfo.scale);
			interface_notifyText(URL, 0);
			interface_playControlSelect(appControlInfo.playbackInfo.scale > 0.0 ? interfacePlayControlFastForward : interfacePlayControlRewind);
		}
		break;
	case interfacePlayControlRewind:
		if ( !appControlInfo.mediaInfo.active )
		{
			media_startPlayback();
			if(gfx_isTrickModeSupported(screenMain))
			{
				gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			}
		}
		if (appControlInfo.mediaInfo.active && gfx_isTrickModeSupported(screenMain))
		{
			float newScale;

			if (gfx_videoProviderIsPaused(screenMain))
				gfx_resumeVideoProvider(screenMain);

			if( appControlInfo.playbackInfo.scale >= 2.0 )
				newScale = appControlInfo.playbackInfo.scale / 2;
			else if( appControlInfo.playbackInfo.scale > 0.0 )
				newScale = -2.0;
			else if( appControlInfo.playbackInfo.scale > -MAX_SCALE )
				newScale = appControlInfo.playbackInfo.scale * 2;
			else
				newScale = 0.0;

			if( newScale != 0.0 && gfx_setSpeed(screenMain, newScale) == 0 )
				appControlInfo.playbackInfo.scale = newScale;
		}
		if( appControlInfo.playbackInfo.scale == 1.0 )
		{
			interface_notifyText(NULL, 0);
			interface_playControlSelect(interfacePlayControlPlay);
		} else
		{
			sprintf(URL, "%1.0fx", appControlInfo.playbackInfo.scale);
			interface_notifyText(URL, 0);
			interface_playControlSelect(appControlInfo.playbackInfo.scale > 0.0 ? interfacePlayControlFastForward : interfacePlayControlRewind);
		}
		break;
	case interfacePlayControlStop:
		if ( appControlInfo.mediaInfo.active || appControlInfo.mediaInfo.paused )
		{
			media_stopPlayback();
			interface_showMenu(1, 1);
		}
		break;
	case interfacePlayControlPause:
		if (appControlInfo.mediaInfo.paused)
		{
			media_startPlayback();
			if( appControlInfo.playbackInfo.scale == 1.0 )
			{
				interface_notifyText(NULL, 0);
				interface_playControlSelect(interfacePlayControlPlay);
			} else
			{
				sprintf(URL, "%1.0fx", appControlInfo.playbackInfo.scale);
				interface_notifyText(URL, 0);
				interface_playControlSelect(appControlInfo.playbackInfo.scale > 0.0 ? interfacePlayControlFastForward : interfacePlayControlRewind);
			}
		} else
		{
			if ( appControlInfo.mediaInfo.active )
				media_pausePlayback();
		}
		break;
	case interfacePlayControlSetPosition:
		sound_fadeInit();
		if (!appControlInfo.mediaInfo.active && !appControlInfo.mediaInfo.paused )
		{
			position					=	interface_playControlSliderGetPosition();
			appControlInfo.playbackInfo.scale = 1.0;
			media_startPlayback();
			gfx_setVideoProviderPosition(screenMain,position);
			ret_val	=	gfx_getPosition(&length_stream,&position);
			//dprintf("%s: got position %f, set it\n", __FUNCTION__, position_stream);
			if((ret_val == 0)&&(position < length_stream))
			{
				interface_playControlSlider(0, (unsigned int)length_stream, (unsigned int)position);
			}
		}else
		{
			position	=	interface_playControlSliderGetPosition();
			if (appControlInfo.mediaInfo.paused)
			{
				media_startPlayback();
			}
			gfx_setVideoProviderPosition(screenMain, position);
			if( gfx_isTrickModeSupported(screenMain) )
				gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			else
				appControlInfo.playbackInfo.scale = 1.0;
			// Call to media_startPlayback resets slider position, moving slider back:
			ret_val	=	gfx_getPosition(&length_stream,&position);
			//dprintf("%s: got position %f, set it\n", __FUNCTION__, position_stream);
			if((ret_val == 0)&&(position < length_stream))
			{
				interface_playControlSlider(0, (unsigned int)length_stream, (unsigned int)position);
			}
			if( appControlInfo.playbackInfo.scale == 1.0 )
			{
				interface_notifyText(NULL, 0);
				interface_playControlSelect(interfacePlayControlPlay);
			} else
			{
				sprintf(URL, "%1.0fx", appControlInfo.playbackInfo.scale);
				interface_notifyText(URL, 0);
				interface_playControlSelect(appControlInfo.playbackInfo.scale > 0.0 ? interfacePlayControlFastForward : interfacePlayControlRewind);
			}
		}
		sound_fadein();
		break;
	case interfacePlayControlInfo:
		interface_playControlSliderEnable(!interface_playControlSliderIsEnabled());
		interface_displayMenu(1);
		return 0;
		break;
	case interfacePlayControlMode:
		if( appControlInfo.playbackInfo.playlistMode == playlistModeIPTV && appControlInfo.rtpMenuInfo.epg[0] != 0 )
		{
			rtp_showEPG(screenMain, media_setupPlayControl);
			return 0;
		}
		break;
	default:
		// default action
		//dprintf("%s: unknown button = %d \n", __FUNCTION__,button);
		return 1;
		break;
	}

	interface_displayMenu(1);
	//dprintf("%s: done\n", __FUNCTION__);
	return 0;
}

#ifdef ENABLE_AUTOPLAY
static int media_auto_play(void* pArg)
{
	return media_stream_change(_M &BrowseFilesMenu, pArg);
}

static int media_stream_selected(interfaceMenu_t *pMenu, void* pArg)
{
	//int streamNumber = GET_NUMBER(pArg);
	//dprintf("%s: in\n", __FUNCTION__);
	if ( /*strcmp(appControlInfo.mediaInfo.filename, &streams->items[streamNumber], sizeof(struct media_desc)) != 0 ||*/
		appControlInfo.mediaInfo.active == 0 )
	{
		playingStream = 0;
		//dprintf("%s: event %08X\n", __FUNCTION__, pArg);
		interface_addEvent(media_auto_play, pArg, 3000, 1);
	}
	//dprintf("%s: done\n", __FUNCTION__);
	return 0;
}

static int media_stream_deselected(interfaceMenu_t *pMenu, void* pArg)
{
	//dprintf("%s: in\n", __FUNCTION__);
	//dprintf("%s: event %08X\n", __FUNCTION__, pArg);
	interface_removeEvent(media_auto_play, pArg);
	if ( playingStream == 0 )
	{
		interface_playControlDisable(1);
		appControlInfo.mediaInfo.filename[0] = 0;
		media_stopPlayback();
	}
	//dprintf("%s: done\n", __FUNCTION__);
	return 0;
}
#endif

int media_startNextChannel(int direction, void* pArg)
{
	int             indexChange = (direction?-1:1);
	static char     playingDir[MAX_URL];
	char           *playingFile = NULL;
	char           *str;
	struct dirent **playDirEntries;
	int             playDirCount;
	int             current_index = -1;
	int             new_index = -1;
	int             i;
	static int		lock = 0;
	int				counter = 0;

	if (lock)
	{
		dprintf("%s: Block recursion!\n", __FUNCTION__);
		return 0;
	}

	switch(appControlInfo.playbackInfo.playlistMode)
	{
		case playlistModeNone: break;
		case playlistModeFavorites:
			if( appControlInfo.mediaInfo.bHttp )
				str = appControlInfo.mediaInfo.filename;
			else
			{
				sprintf(playingDir,"file://%s",appControlInfo.mediaInfo.filename);
				str = playingDir;
			}
#ifdef ENABLE_YOUTUBE
			if( appControlInfo.playbackInfo.streamSource != streamSourceYoutube )
#endif
			playlist_setLastUrl(str);
			return playlist_startNextChannel(direction,SET_NUMBER(-1));
		case playlistModeIPTV:
			return rtp_startNextChannel(direction, pArg);
		case playlistModeDLNA:
#ifdef ENABLE_DLNA
			return dlna_startNextChannel(direction,appControlInfo.mediaInfo.filename);
#else
			return 1;
#endif
		case playlistModeYoutube:
#ifdef ENABLE_YOUTUBE
			return youtube_startNextChannel(direction, pArg);
#else
			return 1;
#endif
		case playlistModeRutube:
#ifdef ENABLE_RUTUBE
			return rutube_startNextChannel(direction, pArg);
#else
			return 1;
#endif
#ifdef ENABLE_TELETES
		case playlistModeTeletes:
			return teletes_startNextChannel(direction, pArg);
#endif
	}
	if( appControlInfo.mediaInfo.bHttp )
	{
		//media_stopPlayback();
		return 1;
	}

	/* We are already playing some file in some (!=current) dir */
	/* So we need to navigate back to that dir and select next/previous file to play */
	strcpy(playingDir,appControlInfo.mediaInfo.filename);
	playingFile = strrchr(appControlInfo.mediaInfo.filename,'/')+1;
	playingDir[(playingFile-appControlInfo.mediaInfo.filename)]=0;
	playingPath = playingDir;
	playDirCount = scandir(playingDir, &playDirEntries, media_select_current, appControlInfo.mediaInfo.fileSorting);
	if (playDirCount < 0)
	{
		interface_showMessageBox(_T("ERR_FILE_NOT_FOUND"), thumbnail_error, 0);
		return 1;
	}

	if (playDirCount < 2) /* Nothing to navigate to */
	{
		eprintf("%s: '%s' file count < 2\n", __FUNCTION__, playingDir);
		goto cleanup;
	}

	/* Trying to get index of playing file in directory listing */
	for ( i = 0 ; i < playDirCount; ++i )
	{
		if (strcmp(playingFile, playDirEntries[i]->d_name) == 0)
		{
			current_index = i;
			break;
		}
	}

	do
	{
		interfaceCommand_t cmd;

		if(appControlInfo.mediaInfo.playbackMode == playback_random)
		{
			srand(time(NULL));
			if(playDirCount < 3)
				new_index = (current_index +1 ) % playDirCount;
			else
				while( (new_index = rand() % playDirCount) == current_index );
		} else if(current_index>=0) // if we knew previous index, we can get new index
		{
			new_index = (current_index+playDirCount+indexChange) % playDirCount;
		}
		sprintf(appControlInfo.mediaInfo.filename,"%s%s",playingDir,playDirEntries[new_index]->d_name);
		snprintf(appControlInfo.playbackInfo.description, sizeof(appControlInfo.playbackInfo.description), "%s: %s", _T( "RECORDED_VIDEO"), playDirEntries[new_index]->d_name);
		appControlInfo.playbackInfo.description[sizeof(appControlInfo.playbackInfo.description)-1] = 0;
		appControlInfo.playbackInfo.thumbnail[0] = 0;

		if (new_index >= 0)
		{
			/* fileName already in the right place, pArg = -1 */
			lock = 1;
			dprintf("%s: new index = %d, playing '%s'\n", __FUNCTION__, new_index, appControlInfo.mediaInfo.filename);
			media_stream_change(interfaceInfo.currentMenu, SET_NUMBER(CHANNEL_CUSTOM));
			lock = 0;
			if (appControlInfo.mediaInfo.active)
			{
				for (i = 0; i < media_currentFileCount; i++)
				{
					if (strcmp(media_currentFileEntries[i]->d_name, playDirEntries[new_index]->d_name) == 0)
					{
						interface_setSelectedItem(_M &BrowseFilesMenu, i + media_currentDirCount + 1 /* ".." */);
						if (interfaceInfo.showMenu && interfaceInfo.currentMenu == _M &BrowseFilesMenu)
							interface_displayMenu(1);
						break;
					}
				}
				break;
			}
		}

		while ((cmd = helperGetEvent(0)) != interfaceCommandNone)
		{
			dprintf("%s: got command %d\n", __FUNCTION__, cmd);
			if (cmd != interfaceCommandCount)
			{
				dprintf("%s: exit on command %d\n", __FUNCTION__, cmd);
				/* Flush any waiting events */
				helperGetEvent(1);
				for( i = 0 ; i < playDirCount; ++i )
					free(playDirEntries[i]);
				free(playDirEntries);
				return -1;
			}
		}
		if (!keepCommandLoopAlive)
		{
			break;
		}

		dprintf("%s: at %d tried %d next in %d step %d\n", __FUNCTION__, current_index, new_index, playDirCount, indexChange);

		counter++;
		current_index = new_index;
	} while (new_index >= 0 && counter < playDirCount);

cleanup:
	for( i = 0 ; i < playDirCount; ++i )
		free(playDirEntries[i]);
	free(playDirEntries);

	return 0;
}

static void media_getFileDescription()
{
	char *str;
	str = strrchr(appControlInfo.mediaInfo.filename, '/');
	if( str == NULL )
		str = appControlInfo.mediaInfo.filename;
	else
		str++;
	snprintf(appControlInfo.playbackInfo.description, sizeof(appControlInfo.playbackInfo.description), "%s: %s", _T( "RECORDED_VIDEO"), str);
	appControlInfo.playbackInfo.description[sizeof(appControlInfo.playbackInfo.description)-1] = 0;
}

/** Start playing specified or preset stream
 *  @param[in] pArg Stream number in BrowseFilesMenu or CHANNEL_CUSTOM
 */
static int media_stream_change(interfaceMenu_t *pMenu, void* pArg)
{
	int fileNumber = GET_NUMBER(pArg);

	dprintf("%s: in, fileNumber = %d\n", __FUNCTION__, fileNumber);

	if( CHANNEL_CUSTOM != fileNumber )
		appControlInfo.mediaInfo.bHttp = 0;

	if( CHANNEL_CUSTOM != fileNumber && fileNumber >= 0 &&
		(mediaImage == appControlInfo.mediaInfo.typeIndex || (appControlInfo.mediaInfo.typeIndex < 0 &&
		 mediaImage == media_getMediaType(media_currentFileEntries[ fileNumber - media_currentDirCount - 1]->d_name))) )
	{
		snprintf(appControlInfo.slideshowInfo.filename, PATH_MAX, "%s%s", currentPath, media_currentFileEntries[ fileNumber - media_currentDirCount - 1 /* ".." */ ]->d_name);
		appControlInfo.slideshowInfo.showingCover = 0;
		dprintf("%s: starting slideshow from '%s'\n", __FUNCTION__, appControlInfo.slideshowInfo.filename);
		if( media_slideshowStart() == 0 )
		{
			interface_showSlideshowControl();
			return 0;
		}
		return 1;
	}

#ifdef ENABLE_AUTOPLAY
	interface_removeEvent(media_auto_play, pArg);
#endif

	gfx_stopVideoProviders(screenMain);

	media_setupPlayControl(pArg);

	dprintf("%s: start video\n", __FUNCTION__);

	appControlInfo.playbackInfo.scale = 1.0;

	media_startPlayback();	

	helperFlushEvents();

	dprintf("%s: done %d\n", __FUNCTION__, appControlInfo.mediaInfo.active);

	return appControlInfo.mediaInfo.active != 0 ? 0 : 1;
}

static void media_setupPlayControl(void* pArg)
{
	int fileNumber = GET_NUMBER(pArg);
	int buttons;
	playControlChannelCallback set_channel  = NULL;
	playControlChannelCallback next_channel = media_startNextChannel;

	buttons = interfacePlayControlPlay|interfacePlayControlPause|interfacePlayControlStop;
	if( appControlInfo.mediaInfo.bHttp == 0 )
		buttons |= interfacePlayControlPrevious|interfacePlayControlNext|interfacePlayControlMode;
	else
		switch( appControlInfo.playbackInfo.playlistMode )
		{
			case playlistModeNone: break;
			case playlistModeDLNA:
			case playlistModeIPTV:
			case playlistModeFavorites:
			case playlistModeYoutube:
			case playlistModeRutube:
#ifdef ENABLE_TELETES
			case playlistModeTeletes:
#endif
				buttons |= interfacePlayControlPrevious|interfacePlayControlNext|interfacePlayControlMode;
				break;
		}

	if(appControlInfo.playbackInfo.playlistMode != playlistModeFavorites
#ifdef ENABLE_YOUTUBE
	   // youtube entries should never be added to playlist usual way
	   && !(appControlInfo.mediaInfo.bHttp && strstr( appControlInfo.mediaInfo.filename, "youtube.com/" ) != NULL)
#endif
#ifdef ENABLE_RUTUBE
	   && !(appControlInfo.mediaInfo.bHttp && strstr( appControlInfo.mediaInfo.filename, "rutube.ru/" ) != NULL)
#endif
	  )
	{
		buttons |= interfacePlayControlAddToPlaylist;
	}
#ifdef ENABLE_PVR
	if( appControlInfo.mediaInfo.bHttp )
		buttons|= interfacePlayControlRecord;
#endif

	if(CHANNEL_CUSTOM != fileNumber && fileNumber >= 0) // Playing from media menu - initializing play control
	{
		media_getFilename(fileNumber);
		media_getFileDescription();
		appControlInfo.playbackInfo.thumbnail[0] = 0;
		appControlInfo.playbackInfo.channel = -1;
	}

	interface_playControlSetup(media_play_callback,
		pArg,
		buttons,
		appControlInfo.playbackInfo.description,
		appControlInfo.mediaInfo.bHttp ? thumbnail_internet : ( IS_USB( appControlInfo.mediaInfo.filename ) ? thumbnail_usb : thumbnail_workstation) );
	set_channel = NULL;
	switch (appControlInfo.playbackInfo.playlistMode)
	{
		case playlistModeFavorites: set_channel = playlist_setChannel; break;
		case playlistModeIPTV:      set_channel = rtp_setChannel;
			interface_playControlSetProcessCommand(rtp_playControlProcessCommand);
			strcpy(appControlInfo.rtpMenuInfo.lastUrl, appControlInfo.mediaInfo.filename);
			break;
#ifdef ENABLE_DLNA
		case playlistModeDLNA:      set_channel = dlna_setChannel; break;
#endif
#ifdef ENABLE_YOUTUBE
		case playlistModeYoutube:   
			set_channel  = youtube_setChannel;
			next_channel = youtube_startNextChannel;
			break;
#endif
#ifdef ENABLE_RUTUBE
		case playlistModeRutube:   
			set_channel  = rutube_setChannel;
			next_channel = rutube_startNextChannel;
			break;
#endif
		default:
			break;
	}
	interface_playControlSetChannelCallbacks(next_channel, set_channel);
	if( appControlInfo.playbackInfo.channel >= 0 )
		interface_channelNumberShow(appControlInfo.playbackInfo.channel);
}

int media_streamStart()
{
	return media_stream_change(_M &BrowseFilesMenu, SET_NUMBER(CHANNEL_CUSTOM));
}

static int media_onStop()
{
	media_stopPlayback();
	switch(appControlInfo.mediaInfo.playbackMode)
	{
	case playback_looped:
		return media_startPlayback();
	case playback_sequential:
	case playback_random:
		return media_startNextChannel(0,NULL);
	default:
		interface_showMenu(1, 1);
		break;
	}
	return 0;
}

int media_check_status(void *pArg)
{
	DFBVideoProviderStatus status;

	status = gfx_getVideoProviderStatus(screenMain);

#ifdef DEBUG
	if (status != DVSTATE_PLAY)
		dprintf("%s: %d\n", __FUNCTION__, status);
#endif

	switch( status )
	{
		case DVSTATE_FINISHED:
			interface_showMessageBox(_T("ERR_STREAM_NOT_SUPPORTED"), thumbnail_error, 3000);
			// fall through
		case DVSTATE_STOP:
			// FIXME: STB810/225's ES video provider returns 'finished' status immediately
			// after it has finished reading input file, while decoder still processes remaining
			// data in buffers.

#ifdef ENABLE_VIDIMAX
			if (appControlInfo.vidimaxInfo.active && !appControlInfo.vidimaxInfo.seeking){
				vidimax_stopVideoCallback();
			}
#ifndef STBxx
			else if (appControlInfo.vidimaxInfo.seeking)
			{
				eprintf ("%s: DVSTATE_FINISHED/DVSTATE_STOP. But no video stop. Seeking.\n", __FUNCTION__);
				break;
			}
#endif			
#endif
			media_onStop();
			break;
		case DVSTATE_BUFFERING:
			//eprintf ("%s: DVSTATE_BUFFERING\n", __FUNCTION__);
			break;
#ifdef STBPNX
		case DVSTATE_STOP_REWIND:
			appControlInfo.playbackInfo.scale = 1.0;
			gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			interface_notifyText(NULL, 0);
			interface_playControlSelect(interfacePlayControlPlay);
			// fall through
#endif
		default:
#ifdef STBPNX
			if(!appControlInfo.mediaInfo.paused)
			{
				double 	length_stream;
				double 	position_stream;
				int ret_val	=	gfx_getPosition(&length_stream,&position_stream);

				//dprintf("%s: got position %f, set it\n", __FUNCTION__, position_stream);
				if((ret_val == 0)&&(position_stream >= length_stream))
				{
					media_onStop();
				}
			}
#endif
			if( interfaceInfo.notifyText[0] != 0 && appControlInfo.playbackInfo.scale == 1.0 )
			{
				interface_notifyText(NULL, 1);
			}
			
			interface_addEvent(media_check_status, pArg, 1000, 1);
	}

	return 0;
}

void media_stopPlayback(void)
{
	media_pauseOrStop(1);
}

inline static void media_pausePlayback()
{
	media_pauseOrStop(0);
}

void media_pauseOrStop(int stop)
{
	mysem_get(media_semaphore);

	//media_setStateCheckTimer(screenMain, 0, 0);
	if( interfaceInfo.notifyText[0] != 0 && appControlInfo.playbackInfo.scale == 1.0)
	{
		interface_notifyText(NULL, 1);
	}
	interface_playControlSetInputFocus(inputFocusPlayControl);

	if ( stop )
	{
		interface_notifyText(NULL,0);
		interface_playControlSlider(0, 0, 0);
		interface_playControlSelect(interfacePlayControlStop);
	} else
	{
		interface_playControlSelect(interfacePlayControlPause);
	}
	//interface_playControlRefresh(1);

	dprintf("%s: %s\n", __FUNCTION__, stop ? "stop" : "pause");

#ifndef STBxx
	media_forceRestart = (gfx_getVideoProviderLength(screenMain) == gfx_getVideoProviderPosition(screenMain));
#endif

	appControlInfo.mediaInfo.endOfStream = 0;
	interface_removeEvent(media_check_status, NULL);

	//dprintf("%s:\n -->>>>>>> Stop, stop: %d, stop restart: %d, length: %f, position: %f\n\n", __FUNCTION__, stop, media_forceRestart, gfx_getVideoProviderLength(screenMain), gfx_getVideoProviderPosition(screenMain));
	gfx_stopVideoProvider(screenMain, stop, stop);
#ifdef ENABLE_AUTOPLAY
	if (stop)
		playingStream = 0;
#endif
	interface_setBackground(0,0,0,0,NULL);

	dprintf("%s: Set playback inactive\n", __FUNCTION__);
	appControlInfo.mediaInfo.active = 0;
	appControlInfo.mediaInfo.paused = !stop;

	mysem_release(media_semaphore);
}

int media_slideshowStart()
{
	char *failedDir, *str;
	int firstStart = appControlInfo.slideshowInfo.state == 0;
	interfacePlayControlButton_t slideshowButton;

	dprintf("%s: %s\n", __FUNCTION__, appControlInfo.slideshowInfo.filename);

	if( helperFileExists(appControlInfo.slideshowInfo.filename) )
	{
		slideshowButton = interfaceSlideshowControl.highlightedButton;
		if(gfx_videoProviderIsActive(screenMain))
		{
			if(gfx_getVideoProviderHasVideo(screenMain))
			{
				gfx_stopVideoProviders(screenMain);
				interface_playControlDisable(0);
				interface_playControlSetInputFocus(inputFocusSlideshow);
				//interfacePlayControl.visibleFlag = 0;
			}
		} else
		{
			interface_playControlDisable(0);
			interface_playControlSetInputFocus(inputFocusSlideshow);
			//interfacePlayControl.highlightedButton = 0;
			//interfacePlayControl.visibleFlag = 0;
		}
		if( firstStart && !(appControlInfo.slideshowInfo.showingCover) )
			interface_playControlSetInputFocus(inputFocusSlideshow);
		interfaceSlideshowControl.highlightedButton = slideshowButton;

		appControlInfo.slideshowInfo.state = appControlInfo.slideshowInfo.defaultState;
#ifdef STBPNX
		if(gfx_decode_and_render_Image(appControlInfo.slideshowInfo.filename) != 0)
#else
		IDirectFBSurface *pImage = gfx_decodeImage(appControlInfo.slideshowInfo.filename,
			interfaceInfo.screenWidth, interfaceInfo.screenHeight, 0);
		if (!pImage)
#endif
		{
			interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_usb_image, 3000);
			failedDir = strrchr(media_failedImage,'/');
			if(media_failedImage[0] == 0 || ( failedDir != NULL && strncmp( appControlInfo.slideshowInfo.filename, media_failedImage, failedDir - media_failedImage ) != 0 ))
			{
				strcpy(media_failedImage,appControlInfo.slideshowInfo.filename); // memorizing first failed image
			} else if(strcmp(media_failedImage,appControlInfo.slideshowInfo.filename) != 0) // another failed picure, trying next
			{
				media_slideshowNext(0);
				return 0;
			}
			goto failure; // after some failed images we returned to first one - cycle
		}
		if( appControlInfo.slideshowInfo.state == 0 ) // slideshow was stopped in the middle
		{
			goto failure;
		}
#ifdef STBPNX
		//actually show picture
		gfx_showImage(screenPip);
#endif
		if( appControlInfo.slideshowInfo.state > slideshowImage )
		{
			interface_addEvent(media_slideshowEvent, NULL, appControlInfo.slideshowInfo.timeout, 1);
		}
		media_failedImage[0] = 0; // image loaded successfully - reseting failed marker
		if( gfx_videoProviderIsActive( screenMain ) == 0 )
		{
			str = strrchr(appControlInfo.slideshowInfo.filename, '/');
			if( str != NULL )
			{
				interface_playControlUpdateDescriptionThumbnail( str+1, thumbnail_usb_image );
				appControlInfo.playbackInfo.thumbnail[0] = 0;
				if( firstStart && interfacePlayControl.showOnStart )
					interface_playControlRefresh(0);
			}
		}
		interfaceSlideshowControl.enabled = 1;
		interface_displayMenu(1);
		return 0;
	}
	//interfaceSlideshowControl.enabled = appControlInfo.slideshowInfo.state > 0;
failure:
	media_slideshowStop(0);
	return 1;
}

int  media_slideshowSetMode(int mode)
{
	if (mode < 0)
	{
		switch(appControlInfo.slideshowInfo.defaultState)
		{
			case slideshowImage:
				appControlInfo.slideshowInfo.defaultState = slideshowShow;

				break;
			case slideshowShow:
				appControlInfo.slideshowInfo.defaultState = slideshowRandom;
				interface_addEvent(media_slideshowEvent, NULL, appControlInfo.slideshowInfo.timeout, 1);
				break;
			default:
				interface_removeEvent(media_slideshowEvent, NULL);
				appControlInfo.slideshowInfo.defaultState = slideshowImage;
		}
	}
	else
		appControlInfo.slideshowInfo.defaultState = mode;

	if( appControlInfo.slideshowInfo.state != slideshowDisabled )
	{
		appControlInfo.slideshowInfo.state = appControlInfo.slideshowInfo.defaultState;
		if (appControlInfo.slideshowInfo.state > slideshowImage )
		{
			interface_addEvent(media_slideshowEvent, NULL, appControlInfo.slideshowInfo.timeout, 1);
		} else
		{
			interface_removeEvent(media_slideshowEvent, NULL);
		}
	}

	return saveAppSettings();
}

int  media_slideshowSetTimeout(int timeout)
{
	switch (timeout)
	{
		case VALUE_NEXT:
			switch(appControlInfo.slideshowInfo.timeout)
			{
				case 3000:
					appControlInfo.slideshowInfo.timeout = 10000;
					break;
				case 10000:
					appControlInfo.slideshowInfo.timeout = 30000;
					break;
				case 30000:
					appControlInfo.slideshowInfo.timeout = 60000;
					break;
				case 60000:
					appControlInfo.slideshowInfo.timeout = 120000;
					break;
				case 120000:
					appControlInfo.slideshowInfo.timeout = 300000;
					break;
				default:
					appControlInfo.slideshowInfo.timeout = 3000;
			}
			break;
		case VALUE_PREV:
			switch(appControlInfo.slideshowInfo.timeout)
			{
				case 300000:
					appControlInfo.slideshowInfo.timeout = 120000;
					break;
				case 120000:
					appControlInfo.slideshowInfo.timeout = 60000;
					break;
				case 60000:
					appControlInfo.slideshowInfo.timeout = 30000;
					break;
				case 30000:
					appControlInfo.slideshowInfo.timeout = 10000;
					break;
				default: // 10000
					appControlInfo.slideshowInfo.timeout = 3000;
			}
			break;
		default:
			appControlInfo.slideshowInfo.timeout = timeout;
	}
	return 0;
}

static int media_slideshowEvent(void *pArg)
{
	int result;

	dprintf("%s: in\n", __FUNCTION__);

	gfx_waitForProviders();
	mysem_get(media_semaphore);
	mysem_release(media_semaphore);
	if( appControlInfo.slideshowInfo.state == 0 )
		return 1;

	result = media_slideshowNext(0);

	if(result == 0)
	{
		mysem_get(slideshow_semaphore);
		interface_addEvent(media_slideshowEvent, NULL, appControlInfo.slideshowInfo.timeout, 1);
		mysem_release(slideshow_semaphore);
	}

	return result;
}

#ifndef STBPNX
void media_slideshowReleaseImage(void)
{
	if (appControlInfo.slideshowInfo.filename[0] == 0)
		return;
	stb810_gfxImageEntry *entry = gfx_findImageEntryByName(appControlInfo.slideshowInfo.filename);
	if (entry)
		gfx_releaseImageEntry(entry);
	else
		eprintf("%s: warning: failed to release image entry for %s\n", __FUNCTION__, appControlInfo.slideshowInfo.filename);
}
#endif

int  media_slideshowNext(int direction)
{
	int             indexChange = (direction?-1:1);
	struct dirent **imageDirEntries;
	int             imageDirCount;
	int             current_index = -1;
	int             new_index = -1;
	int             i;
	char           *delimeter,c;
	char            buf[BUFFER_SIZE];

	dprintf("%s: %d\n", __FUNCTION__, indexChange);

	delimeter = strrchr(appControlInfo.slideshowInfo.filename,'/');
	if( delimeter == NULL )
		return -2;
	c = delimeter[1];
	delimeter[1] = 0;
	imageDirCount = scandir(appControlInfo.slideshowInfo.filename, &imageDirEntries, media_select_image, appControlInfo.mediaInfo.fileSorting);
	delimeter[1] = c;

	if(imageDirCount < 0)
	{
		interface_removeEvent(media_slideshowEvent, NULL);
		interfaceSlideshowControl.enabled = 0;
		sprintf(buf,"%s: %s",_T("SLIDESHOW"),_T("ERR_FILE_NOT_FOUND"));
		interface_showMessageBox(buf, thumbnail_error, 0);
		return -1;
	}

	if (imageDirCount == 0)
	{
		/* There is no images on old place */
		free(imageDirEntries);
		interface_removeEvent(media_slideshowEvent, NULL);
		appControlInfo.slideshowInfo.state = slideshowImage;
		sprintf(buf,"%s: %s",_T("SLIDESHOW"),_T("ERR_FILE_NOT_FOUND"));
		interface_showMessageBox(buf, thumbnail_error, 0);
		return 1;
	}

	if(imageDirCount < 2) /* Nothing to navigate to */
	{
		for( i = 0 ; i < imageDirCount; ++i )
			free(imageDirEntries[i]);
		free(imageDirEntries);

		return 2;
	}

	/* Trying to get index of current image */
	for( i = 0 ; i < imageDirCount; ++i )
	{
		if(strcmp(&delimeter[1],imageDirEntries[i]->d_name) == 0)
		{
			current_index = i;
			break;
		}
	}
	if( appControlInfo.slideshowInfo.state == slideshowRandom )
	{
		srand(time(NULL));
		if(imageDirCount < 3)
			new_index = (current_index +1 ) % imageDirCount;
		else
			while( (new_index = rand() % imageDirCount) == current_index );
	}
	else if(current_index>=0)
	{
		new_index = (current_index+imageDirCount+indexChange) % imageDirCount;
	}

	if (new_index >= 0)
	{
		media_slideshowReleaseImage();
		strcpy(&delimeter[1],imageDirEntries[new_index]->d_name);
	}

	for( i = 0 ; i < imageDirCount; ++i )
		free(imageDirEntries[i]);
	free(imageDirEntries);

	if (new_index >= 0)
	{
		/* fileName already in the right place, pArg = -1 */
		return media_slideshowStart();
	}
	return 0;
}

int media_slideshowStop(int disable)
{
	appControlInfo.slideshowInfo.state = 0;
	gfx_waitForProviders();
	mysem_get(slideshow_semaphore);
	interface_removeEvent(media_slideshowEvent, NULL);
#ifdef STBPNX
	gfx_hideImage(screenPip);
#endif
	media_slideshowReleaseImage();
	mysem_release(slideshow_semaphore);
	interfaceSlideshowControl.enabled = disable ? 0 : helperFileExists(appControlInfo.slideshowInfo.filename);
	interface_disableBackground();
	return 0;
}

static int media_showCover(char* filename)
{
	struct dirent **imageDirEntries;
	int             imageDirCount, i;
	char  *delimeter;

	strcpy(appControlInfo.slideshowInfo.filename,filename);

	delimeter = strrchr(appControlInfo.slideshowInfo.filename,'/');
	if( delimeter == NULL )
		return -2;
	delimeter[1] = 0;
	imageDirCount = scandir(appControlInfo.slideshowInfo.filename, &imageDirEntries, media_select_image, appControlInfo.mediaInfo.fileSorting);

	if(imageDirCount < 0)
	{
		appControlInfo.slideshowInfo.filename[0] = 0;
		media_slideshowStop(0);
		return -1;
	}

	if (imageDirCount == 0)
	{
		free(imageDirEntries);
		appControlInfo.slideshowInfo.filename[0] = 0;
		media_slideshowStop(0);
		return 1;
	}

	appControlInfo.slideshowInfo.showingCover = 1;
	strcpy(&delimeter[1],imageDirEntries[0]->d_name);
	for(i = 0; i < imageDirCount; ++i)
	{
		free(imageDirEntries[i]);
	}
	free(imageDirEntries);

	return media_slideshowStart();
}

int media_playURL(int which, const char* URL, const char *description, const char* thumbnail)
{
	const char *fileName = URL+7; /* 7 == strlen("file://"); */
	size_t url_length;

	if( IS_URL(URL) )
	{
		url_length = strlen(URL)+1;
		appControlInfo.mediaInfo.bHttp = 1;
		memcpy(appControlInfo.mediaInfo.filename,URL,url_length);
		if( description != NULL )
		{
			if( description != appControlInfo.playbackInfo.description )
				strncpy(appControlInfo.playbackInfo.description, description, sizeof(appControlInfo.playbackInfo.description)-1);
		} else
		{
			if( utf8_urltomb(appControlInfo.mediaInfo.filename, url_length,
			    appControlInfo.playbackInfo.description, sizeof(appControlInfo.playbackInfo.description)-1 ) < 0 )
			{
				strncpy(appControlInfo.playbackInfo.description, appControlInfo.mediaInfo.filename, sizeof(appControlInfo.playbackInfo.description)-1);
			}
		}
		appControlInfo.playbackInfo.description[sizeof(appControlInfo.playbackInfo.description)-1] = 0;
		if( thumbnail )
		{
			if( thumbnail != appControlInfo.playbackInfo.thumbnail )
				strcpy(appControlInfo.playbackInfo.thumbnail, thumbnail);
		}
		else
			appControlInfo.playbackInfo.thumbnail[0] = 0;
			//strcpy(appControlInfo.playbackInfo.thumbnail, resource_thumbnails[thumbnail_internet]);
			
		return media_stream_change(_M &BrowseFilesMenu, SET_NUMBER(CHANNEL_CUSTOM));
	}

	dprintf("%s: '%s'\n", __FUNCTION__,fileName);
	appControlInfo.mediaInfo.bHttp = 0;
	switch(media_getMediaType(fileName))
	{
		case mediaImage:
			{
				int res;
				strcpy(appControlInfo.slideshowInfo.filename,fileName);
				if( (res = media_slideshowStart()) == 0 )
				{
					interface_showSlideshowControl();
				}
				return res;
			}
		default:
#if (defined STB225)
			strcpy(appControlInfo.mediaInfo.filename,URL);
//			strcpy(appControlInfo.mediaInfo.filename,fileName);
#else
			strcpy(appControlInfo.mediaInfo.filename,fileName);
#endif
			if( description )
			{
				strncpy(appControlInfo.playbackInfo.description, description, sizeof(appControlInfo.playbackInfo.description)-1);
				appControlInfo.playbackInfo.description[sizeof(appControlInfo.playbackInfo.description)-1] = 0;
					//strcpy(appControlInfo.playbackInfo.thumbnail, resource_thumbnails[
					//( IS_USB( appControlInfo.mediaInfo.filename ) ? thumbnail_usb : thumbnail_workstation) ]);
			} else
				media_getFileDescription();
			if( thumbnail )
				strcpy(appControlInfo.playbackInfo.thumbnail, thumbnail);
			else
				appControlInfo.playbackInfo.thumbnail[0] = 0;
			return media_stream_change(_M &BrowseFilesMenu, SET_NUMBER(-1));
	}
}

//static int media_startPlayback()
int media_startPlayback()
{
	int res;
	int alreadyExist;
	char altPath[PATH_MAX];
	char *fileName, *failedDir;

	sound_fadeInit();

	if( appControlInfo.mediaInfo.bHttp || helperFileExists(appControlInfo.mediaInfo.filename) )
	{
		fileName = appControlInfo.mediaInfo.filename;
#ifdef STBPNX
		/* Check USB storage still plugged in */
		if( appControlInfo.mediaInfo.bHttp == 0 && IS_USB(fileName) )
		{
			strcpy(altPath,"/dev/sd__");
			altPath[7] = 'a' + appControlInfo.mediaInfo.filename[10] - 'A';
			if( appControlInfo.mediaInfo.filename[11] == '/' )
			{
				altPath[8] = 0;
			} else
			{
				altPath[8] = appControlInfo.mediaInfo.filename[22];
			}
			if( !check_file_exsists(altPath) )
			{
				interface_showMessageBox(_T("ERR_FILE_NOT_FOUND"), thumbnail_error, 3000);
				interface_hideLoading();
				sound_restoreVolume();
				return 1;
			}
		}
#endif
	} else
	{
		dprintf("%s: File '%s' not found on it's place\n", __FUNCTION__,appControlInfo.mediaInfo.filename);
#ifdef STBPNX
		if(media_adjustStoragePath(appControlInfo.mediaInfo.filename, altPath) == 0)
		{
			fileName = altPath;
		} else
#endif
		{
			eprintf("media: File '%s' not found\n", appControlInfo.mediaInfo.filename);
			interface_showMessageBox(_T("ERR_FILE_NOT_FOUND"), thumbnail_error, 3000);
			interface_hideLoading();
			sound_restoreVolume();
			return 1;
		}
	}

	interface_showLoading();
	interface_displayMenu(1);

	interface_showLoadingAnimation();

	mysem_get(media_semaphore);

	dprintf("%s: Start media\n", __FUNCTION__);
	switch( appControlInfo.playbackInfo.playlistMode)
	{
		case  playlistModeNone:
			if( !appControlInfo.mediaInfo.bHttp )
			{
				appControlInfo.playbackInfo.streamSource = streamSourceUSB;
				strcpy( appControlInfo.mediaInfo.lastFile, appControlInfo.mediaInfo.filename );
			}
			break;
#ifdef ENABLE_YOUTUBE
		case playlistModeYoutube:
			strcpy(appControlInfo.mediaInfo.lastFile, youtube_getCurrentURL());
			break;
#endif
#ifdef ENABLE_RUTUBE
		case playlistModeRutube:
			strcpy(appControlInfo.mediaInfo.lastFile, rutube_getCurrentURL());
			break;
#endif
		default:;
	}
	saveAppSettings();
	appControlInfo.mediaInfo.endOfStream 			= 	0;
	appControlInfo.mediaInfo.endOfStreamReported 	= 	0;
	appControlInfo.mediaInfo.paused					= 	0;
	/* Creating provider can take some time, in which user can try to launch another file.
	To prevent it one should set active=1 in advance. (#10101) */
	appControlInfo.mediaInfo.active = 1;

	dprintf("%s: Start Media playback of %s force %d\n", __FUNCTION__, fileName, media_forceRestart);

	alreadyExist = gfx_videoProviderIsCreated(screenMain, fileName);
	(void)alreadyExist;

	res = gfx_startVideoProvider(fileName,
								screenMain,
								media_forceRestart,
								(appControlInfo.soundInfo.rcaOutput==1) ? "" : ":I2S0");

	dprintf("%s: Start provider: %d\n", __FUNCTION__, res);

	if ( res == 0 )
	{
		double 					length_stream;
		double 					position_stream;
		int						ret_val;

		if (appControlInfo.mediaInfo.bHttp &&
		    appControlInfo.playbackInfo.playlistMode == playlistModeIPTV)
		{
			gfx_setVideoProviderLive(screenMain);
		}

#ifdef ENABLE_AUTOPLAY
		playingStream = 1;
#endif

		/* After video provider is created, it starts playing video.
		So hiding menus early as probing video position can take time on bad streams (#9351) */
		interface_disableBackground();
		interface_showMenu(0, 0);
		interface_hideLoadingAnimation();

#ifdef ENABLE_TVMYWAY
		if (strstr(appControlInfo.mediaInfo.filename, ".m3u8") == NULL)
#endif // ENABLE_TVMYWAY
		interface_hideLoading();

		media_failedMedia[0] = 0;
//SergA: wait some time for starting video provider
#if (defined STB225)
//		usleep(200000);
#endif
		if(!gfx_getVideoProviderHasVideo(screenMain))
		{
			if((appControlInfo.slideshowInfo.state == 0 || appControlInfo.slideshowInfo.showingCover == 1) && media_showCover(fileName) != 0)
			{
				interface_setBackground(0,0,0,0xFF, INTERFACE_WALLPAPER_IMAGE);
			}
		}
		else
		{
			media_slideshowStop(1);
			if( interfacePlayControl.descriptionThumbnail == thumbnail_usb_image ) // play control set to display slideshow info
			{
				// Refresh descrition
				if( appControlInfo.mediaInfo.bHttp == 0)
				{
					fileName = strrchr(appControlInfo.mediaInfo.filename, '/');
					if( fileName == NULL )
						fileName = appControlInfo.mediaInfo.filename;
					else
						fileName++;
					sprintf(altPath, "%s: %s", _T( "RECORDED_VIDEO"), fileName);
				}
				interface_playControlUpdateDescriptionThumbnail( appControlInfo.mediaInfo.bHttp ? appControlInfo.mediaInfo.filename : altPath,
					appControlInfo.mediaInfo.bHttp ? thumbnail_internet : ( IS_USB( appControlInfo.mediaInfo.filename ) ? thumbnail_usb : thumbnail_workstation) );
			}
		}
		if (gfx_isTrickModeSupported(screenMain))
		{
			interface_playControlSetButtons( interface_playControlGetButtons()|interfacePlayControlFastForward|interfacePlayControlRewind );
		}
		interface_playControlSelect(interfacePlayControlStop);
		interface_playControlSelect(interfacePlayControlPlay);
		interface_displayMenu(1);
#ifdef ENABLE_VERIMATRIX
		if (appControlInfo.useVerimatrix != 0 && alreadyExist == 0)
		{
			eprintf("media: Enable verimatrix...\n");
			if (gfx_enableVideoProviderVerimatrix(screenMain, VERIMATRIX_INI_FILE) != 0)
			{
				interface_showMessageBox(_T("ERR_VERIMATRIX_INIT"), thumbnail_error, 0);
			}
		}
#endif
#ifdef ENABLE_SECUREMEDIA
		if (appControlInfo.useSecureMedia != 0 && alreadyExist == 0)
		{
			eprintf("media: Enable securemedia...\n");
			if (gfx_enableVideoProviderSecureMedia(screenMain) != 0)
			{
				interface_showMessageBox(_T("ERR_SECUREMEDIA_INIT"), thumbnail_error, 0);
			}
		}
#endif
		ret_val	=	gfx_getPosition(&length_stream,&position_stream);
		if(ret_val == 0)
		{

#ifdef ENABLE_TVMYWAY
		if (strstr(appControlInfo.mediaInfo.filename, ".m3u8") == NULL)
#endif // ENABLE_TVMYWAY
			interface_playControlSlider(0, (unsigned int)length_stream, (unsigned int)position_stream);

			//dprintf("%s: start from pos %f\n", __FUNCTION__, position_stream);
			//interface_playControlSlider(0, (unsigned int)length_stream, (unsigned int)position_stream);	
#ifndef STSDK			
			// for HLS only
#ifdef ENABLE_VIDIMAX	
			if (!appControlInfo.vidimaxInfo.active)
#endif	
				if (strstr(appControlInfo.mediaInfo.filename, ".m3u8") != NULL){
					eprintf ("%s: .m3u8 started! setting position = %d...\n", __FUNCTION__, position_stream);
					gfx_setVideoProviderPosition(screenMain, position_stream);		// here HLS video starts 	
				}
#endif // STSDK
		}else
		{
#ifdef ENABLE_TVMYWAY
			if (strstr(appControlInfo.mediaInfo.filename, ".m3u8") == NULL)
#endif // ENABLE_TVMYWAY
				interface_playControlSlider(0, 0, 0);

#ifndef STSDK
			// for HLS only
#ifdef ENABLE_VIDIMAX	
			if (!appControlInfo.vidimaxInfo.active)
#endif	
			if (strstr(appControlInfo.mediaInfo.filename, ".m3u8") != NULL){
				eprintf ("%s: .m3u8 started! setting position = 0...\n", __FUNCTION__);
				gfx_setVideoProviderPosition(screenMain, 0);		// here HLS video starts 	
			}
#endif // STSDK			
		}
		//media_setStateCheckTimer(screenMain, 1, 0);

		// Forces play control to reappear after detecting slider
		//interface_playControlSelect(interfacePlayControlStop);
		//interface_playControlSelect(interfacePlayControlPlay);
		//interface_playControlRefresh(1);


		interface_addEvent(media_check_status, NULL, 1000, 1);

		media_forceRestart = 0;
		mysem_release(media_semaphore);
		sound_fadein();
	} else
	{
		eprintf("media: Failed to play '%s'\n", fileName);
		media_forceRestart = 1;
		appControlInfo.mediaInfo.active = 0;
		mysem_release(media_semaphore);
		interface_hideLoadingAnimation();
		interface_hideLoading();
		interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);

		if( appControlInfo.mediaInfo.bHttp == 0 && appControlInfo.mediaInfo.playbackMode != playback_looped && appControlInfo.mediaInfo.playbackMode != playback_single )
		{
			failedDir = strrchr(media_failedMedia,'/');
			if(media_failedMedia[0] == 0 || ( failedDir != NULL && strncmp( appControlInfo.mediaInfo.filename, media_failedMedia, failedDir - media_failedMedia ) != 0 ))
			{
				strcpy(media_failedMedia,appControlInfo.mediaInfo.filename);
			} else if(strcmp(media_failedMedia,appControlInfo.mediaInfo.filename) == 0)
			{
				sound_restoreVolume();
				return 1;
			}
			res = media_startNextChannel(0,NULL);
		}
		sound_restoreVolume();
	}

	dprintf("%s: finished %d\n", __FUNCTION__, res);	
	
	return res;
}

int media_setNextPlaybackMode(void)
{
	appControlInfo.mediaInfo.playbackMode++;
	appControlInfo.mediaInfo.playbackMode %= playback_modes;
#if (defined STB225)
	gfx_setVideoProviderPlaybackFlags(screenMain, DVPLAY_LOOPING, (appControlInfo.mediaInfo.playbackMode==playback_looped) );
#endif
	return saveAppSettings();
}

void media_cleanupMenu()
{
	mysem_destroy(media_semaphore);
	mysem_destroy(slideshow_semaphore);
}

void media_buildMediaMenu(interfaceMenu_t *pParent)
{
	int media_icons[4] = { 0,
		statusbar_f2_settings,
#ifdef ENABLE_FAVORITES
		statusbar_f3_add,
#else
		0,
#endif
		statusbar_f4_filetype };

	createListMenu(&BrowseFilesMenu, _T("RECORDED_LIST"), thumbnail_usb, media_icons, pParent,
		interfaceListMenuIconThumbnail, media_enterFileBrowserMenu, media_leaveBrowseMenu, SET_NUMBER(MENU_ITEM_LAST));
	interface_setCustomKeysCallback(_M &BrowseFilesMenu, media_keyCallback);

	media_icons[0] = 0;
	media_icons[1] = statusbar_f2_playmode;
	media_icons[2] = statusbar_f3_sshow_mode;
	createListMenu(&media_settingsMenu, _T("RECORDED_LIST"), thumbnail_usb, media_icons, _M &BrowseFilesMenu,
		interfaceListMenuIconThumbnail, media_enterSettingsMenu, NULL, NULL);
	interface_setCustomKeysCallback(_M &media_settingsMenu, media_settingsKeyCallback);

	mysem_create(&media_semaphore);
	mysem_create(&slideshow_semaphore);
}

/* File browser */

int media_select_usb(const struct dirent * de)
{
	struct stat stat_info;
	int         status;
	char        full_path[PATH_MAX];
	if (strncmp(de->d_name, "Drive ", sizeof("Drive ")-1) == 0)
	{
		// Skip CD/DVD Drives since we add them manually
		return 0;
	}
	sprintf(full_path,"%s%s",usbRoot,de->d_name);
	status = stat( full_path, &stat_info);
	if(status<0)
		return 0;
	return S_ISDIR(stat_info.st_mode) && (de->d_name[0] != '.');
}

static int media_select_dir(const struct dirent * de)
{
	struct stat stat_info;
	int         status;
	char        full_path[PATH_MAX];
	int			isRoot;

	isRoot = (strcmp(currentPath,usbRoot) == 0);
	if (isRoot && strncmp(de->d_name, "Drive ", sizeof("Drive ")-1) == 0)
	{
		// Skip CD/DVD Drives since we add them manually
		return 0;
	}
	sprintf(full_path,"%s%s",currentPath,de->d_name);
	status = lstat( full_path, &stat_info);
	if(status<0)
		return 0;
	return S_ISDIR(stat_info.st_mode) && !S_ISLNK(stat_info.st_mode) && (de->d_name[0] != '.');
}

int media_select_list(const struct dirent * de)
{
    return media_select(de, currentPath, appControlInfo.mediaInfo.typeIndex);
}

int media_select_current(const struct dirent * de)
{
    return media_select(de, playingPath, appControlInfo.playbackInfo.playingType);
}

static int media_select_image(const struct dirent * de)
{
	return media_select(de, appControlInfo.slideshowInfo.filename, mediaImage );
}

int media_select(const struct dirent * de, char *dir, int fmt)
{
	struct stat stat_info;
	int         status;
	int         searchLength,nameLength;
	size_t      i;
	static char full_path[PATH_MAX];
	sprintf(full_path,"%s%s",dir,de->d_name);
	//dprintf("%s: '%s'\n", __FUNCTION__,full_path);
	status = lstat( full_path, &stat_info);
	/* Files with size > 2GB will give overflow error */
	if(status<0 && errno != EOVERFLOW)
	{
		return 0;
	} else if (status>=0 && (!S_ISREG(stat_info.st_mode) || S_ISLNK(stat_info.st_mode)) )
	{
		return 0;
	}
	if( fmt < 0 )
	{
		return 1;
	}
	nameLength=strlen(de->d_name);
	for( i = 0; i < media_formats[fmt].count; ++i)
	{
		searchLength = strlen(media_formats[fmt].info[i].ext);
		//dprintf("%s: %s : %s\n", __FUNCTION__,de->d_name,searchStrings[i]);

 		if(nameLength>searchLength && strcasecmp(&de->d_name[nameLength-searchLength], media_formats[fmt].info[i].ext)==0)
		{
			//dprintf("%s: found ok\n", __FUNCTION__);
			return 1;
		}
	}
	return 0;
}

#if !(defined STBPNX)
#define MOUNT_PREFIX "sd"
int sd_filter(const struct dirent *entry)
{
	int ret = 0;
	if (strncmp(entry->d_name, MOUNT_PREFIX, sizeof(MOUNT_PREFIX) - 1) == 0)
		ret = 1;
	return ret;
}
#endif

int media_scanStorages()
{
	int devCount = 0;
#ifdef STBPNX
	char dev_path[MAX_DEV_PATH];
	char usb_path[MAX_PATH_LENGTH];
	for (int dev=0; 'a'+dev<='z'; dev++) {
		sprintf(dev_path, "/dev/sd%c", 'a'+dev);
		if (check_file_exsists(dev_path)) {
			for (int part = 1; part <= 8; part++) {
				/* Now check if we have such partition on given device */
				sprintf(dev_path, "/dev/sd%c%d", 'a'+dev, part);
				sprintf(usb_path, "%sDisk %c Partition %d", usbRoot, 'A'+dev, part);
				//dprintf("%s: check part %s\n", __FUNCTION__, dev_path);
				if (check_file_exsists(dev_path)) {
					eprintf("media: checking '%s'\n", usb_path);
					devCount+=helperCheckDirectoryExsists(usb_path);
				}
			}
			/* Workaround for partitionless devices */
			sprintf(usb_path, "%sDisk %c", usbRoot, 'A'+dev);
			eprintf("media: checking '%s'\n", usb_path);
			devCount+=helperCheckDirectoryExsists(usb_path);
		}
	}
#else
	//autofs is removed by hotplug with mdev
	// so we can only calculate number of files in /usb
	DIR *usbDir = opendir(usbRoot);
	if ( usbDir != NULL ) {
		struct dirent *item = readdir(usbDir);
		while (item) {
			if (sd_filter(item))
				devCount++;
			item = readdir(usbDir);
		}
		closedir(usbDir);
	} else
		eprintf("%s: Failed to open %s directory\n", __FUNCTION__, usbRoot);
#endif
	dprintf("%s: found %d devices\n", __FUNCTION__, devCount);
	interfaceSlideshowControl.enabled = ( appControlInfo.slideshowInfo.filename[0] && ((devCount > 0) | (appControlInfo.slideshowInfo.state > 0)) );
	return devCount;
}

/** Check the presence of USB storages
 * @param[in] pArg Should be NULL
 */
static int media_check_storages(void* pArg)
{
	int isRoot, devCount;
	int  selectedIndex = MENU_ITEM_LAST;
	interfaceMenu_t *browseMenu = _M &BrowseFilesMenu;

	isRoot = strcmp(currentPath,usbRoot) == 0;
	devCount = media_scanStorages();

	dprintf("%s: root %d devcount %d\n", __FUNCTION__, isRoot, devCount);
	if (isRoot || devCount == 0)
		interface_addEvent(media_check_storages, NULL, 3000, 1);

	if (devCount == 0 || interfaceInfo.currentMenu != browseMenu)
		return 0;

	if (!helperCheckDirectoryExsists(currentPath)) {
		media_previousDir[0] = 0;
		strcpy(currentPath, ROOT);
	} else {
		selectedIndex = interface_getSelectedItem(browseMenu);
		//dprintf("%s: selected=%d\n", __FUNCTION__,selectedIndex);
		if(selectedIndex != MENU_ITEM_MAIN && selectedIndex != MENU_ITEM_BACK)
			interface_getMenuEntryInfo(browseMenu, selectedIndex, media_previousDir, MENU_ENTRY_INFO_LENGTH);
	}

	media_enterFileBrowserMenu(browseMenu, SET_NUMBER(selectedIndex<0 ? selectedIndex : MENU_ITEM_LAST));
	interface_displayMenu(1);
	return 0;
}

void media_storagesChanged()
{
	if (strncmp(currentPath,usbRoot,strlen(usbRoot)) != 0)
		return;
	interfaceMenu_t *browseMenu = _M &BrowseFilesMenu;
	if (interfaceInfo.currentMenu == browseMenu) {
		mysem_get(media_semaphore);
		media_refreshFileBrowserMenu(browseMenu, SET_NUMBER(MENU_ITEM_LAST));
		mysem_release(media_semaphore);
		interface_displayMenu(1);
	}
}

static int media_leaveBrowseMenu(interfaceMenu_t *pMenu, void* pArg)
{
	dprintf("%s: in\n", __FUNCTION__);
	interface_removeEvent(media_check_storages, NULL);
	media_freeBrowseInfo();
	return 0;
}

static void media_freeBrowseInfo()
{
	if (media_currentDirEntries != NULL) {
		for(int i = 0 ; i < media_currentDirCount; ++i)
			free(media_currentDirEntries[i]);
		free(media_currentDirEntries);
		media_currentDirEntries = NULL;
		media_currentDirCount = 0;
	}
	if (media_currentFileEntries != NULL) {
		for(int i = 0 ; i < media_currentFileCount; ++i)
			free(media_currentFileEntries[i]);
		free(media_currentFileEntries);
		media_currentFileEntries = NULL;
		media_currentFileCount = 0;
	}
}

static int media_refreshFileBrowserMenu(interfaceMenu_t *browseMenu, void* pSelected)
{
	int             selectedItem = GET_NUMBER(pSelected);
	int             hasDrives = 0;
	char            showPath[PATH_MAX];
	char           *str;
	int             file_icon;

	dprintf("%s: sel %d prev %s\n", __FUNCTION__, selectedItem, media_previousDir);

	if (!helperCheckDirectoryExsists(currentPath)) {
		strcpy( currentPath, ROOT );
		return media_refreshFileBrowserMenu(browseMenu, SET_NUMBER(MENU_ITEM_LAST));
	}

	media_freeBrowseInfo();
	interface_clearMenuEntries(browseMenu);

	strncpy(showPath, currentPath, PATH_MAX);

	/*
	*strrchr(showPath,'/')=0;
	if(strlen(showPath)>=MENU_ENTRY_INFO_LENGTH)
	{
	    dprintf("%s: showPath=%s\n", __FUNCTION__,showPath);
	}
	else
	    interface_addMenuEntryDisabled(browseMenu, showPath, 0);
	*/
	int isRoot = (strcmp(currentPath, ROOT) == 0);
	if (!isRoot) {
		interface_addMenuEntry(browseMenu, "..", media_browseParentFolder, NULL, thumbnail_folder);
		interface_setMenuName(browseMenu,basename(showPath),MENU_ENTRY_INFO_LENGTH);
	}

#ifdef ENABLE_SAMBA
	if (media_browseType == mediaBrowseTypeUSB)
#endif
	{
		if (isRoot) {
			str = _T("USB_AVAILABLE");
			interface_setMenuName(browseMenu, str, strlen(str)+1);
		}
#ifdef STBPNX
		for (int dev=0; dev < 10; dev++) {
			sprintf(showPath, "/dev/sr%d", dev);
			if (check_file_exsists(showPath)) {
				sprintf(showPath, "Drive %d", dev);
				if (isRoot)
					interface_addMenuEntry(browseMenu, showPath, media_browseFolder, SET_NUMBER(-1-dev), thumbnail_folder);
				hasDrives++;
			}
		}
		int storageCount = media_scanStorages();
		hasDrives += storageCount;

		if (isRoot && hasDrives == 1 && storageCount == 1)
		{
			for (int dev=0; 'a'+dev<='z'; dev++) {
				sprintf(showPath, "/dev/sd%c", 'a'+dev);
				if (check_file_exsists(showPath)) {
					for (int part=8; part>0; part--) {
						/* Now check if we have such partition on given device */
						sprintf(showPath, "/dev/sd%c%d", 'a'+dev, part);
						if (check_file_exsists(showPath)) {
							sprintf(currentPath, "%sDisk %c Partition %d/", usbRoot, 'A'+dev, part);
							return media_refreshFileBrowserMenu(browseMenu,SET_NUMBER(MENU_ITEM_LAST));
						}
					}
					sprintf(currentPath, "%sDisk %c/", usbRoot, 'A'+dev);
					return media_refreshFileBrowserMenu(browseMenu,SET_NUMBER(MENU_ITEM_LAST));
				}
			}
		}
#else // STBPNX
		//autofs is removed by hotplug with mdev
		DIR *usbDir = opendir(usbRoot);
		if (usbDir != NULL) {
			struct dirent *first_item = NULL;
			struct dirent *item = readdir(usbDir);
			while (item) {
				if(sd_filter(item)) {
					hasDrives++;
					if(!first_item)
						first_item = item;
				}
				item = readdir(usbDir);
			}
			if(isRoot && (hasDrives == 1)) {
				sprintf(currentPath, "%s%s/", usbRoot, first_item->d_name);
				closedir(usbDir);
				return media_refreshFileBrowserMenu(browseMenu,SET_NUMBER(MENU_ITEM_LAST));
			}
			closedir(usbDir);
		} else
			eprintf("%s: Failed to open %s directory\n", __FUNCTION__, usbRoot);
#endif // !STBPNX
	}
#ifdef ENABLE_SAMBA
	else
	{
		if (isRoot)
		{
			interface_addMenuEntry(browseMenu, _T("NETWORK_BROWSING"), interface_menuActionShowMenu, &SambaMenu, thumbnail_workstation);
			str = _T("NETWORK_PLACES");
			interface_setMenuName(browseMenu,str, strlen(str)+1);
			browseMenu->statusBarIcons[0] = statusbar_f1_delete;
			browseMenu->statusBarIcons[2] = statusbar_f3_edit;
			browseMenu->statusBarIcons[3] = statusbar_f4_enterurl;

			for (void *share = samba_readShares();
			     share;
			     share = samba_nextShare()) {
				const char *name = samba_shareGetName(share);
				if(selectedItem == MENU_ITEM_PREV && strcmp(name, media_previousDir)==0)
					selectedItem = interface_getMenuEntryCount(browseMenu);
				interface_addMenuEntry(browseMenu, name, samba_browseShare, share, samba_shareGetIcon(share));
				hasDrives--;
			}
		} else {
			browseMenu->statusBarIcons[0] = 0;
			browseMenu->statusBarIcons[2] = statusbar_f3_add;
			browseMenu->statusBarIcons[3] = statusbar_f4_filetype;
			hasDrives = 1; // not in root
		}
	}
#endif // ENABLE_SAMBA

	if (hasDrives > 0)
	{
		/* Build directory list */
		media_currentDirCount = scandir(currentPath, &media_currentDirEntries, media_select_dir, appControlInfo.mediaInfo.fileSorting);
		media_currentFileCount = scandir(currentPath, &media_currentFileEntries, media_select_list, appControlInfo.mediaInfo.fileSorting);

		if (media_currentFileCount + media_currentDirCount > 100 ) { // displaying lot of items takes more time then scanning
			interface_showLoading();
			interface_displayMenu(1);
		}
		for (int i = 0 ; i < media_currentDirCount; ++i) {
			if(selectedItem == MENU_ITEM_PREV && strncmp(media_currentDirEntries[i]->d_name, media_previousDir, MENU_ENTRY_INFO_LENGTH)==0)
				selectedItem = interface_getMenuEntryCount(browseMenu);
			interface_addMenuEntry(browseMenu, media_currentDirEntries[i]->d_name, media_browseFolder, NULL, thumbnail_folder);
		}
		for (int i = 0 ; i < media_currentFileCount; ++i) {
			if(appControlInfo.mediaInfo.typeIndex < 0)
				file_icon = (file_icon = media_getMediaType(media_currentFileEntries[i]->d_name)) >=0 ? media_formats[file_icon].icon : thumbnail_file;
			else
				file_icon = FORMAT_ICON;

			interface_addMenuEntryCustom(browseMenu, interfaceMenuEntryText, media_currentFileEntries[i]->d_name, strlen(media_currentFileEntries[i]->d_name)+1, 1, media_stream_change,
#ifdef ENABLE_AUTOPLAY
				media_stream_selected, media_stream_deselected,
#else
				NULL, NULL,
#endif
				NULL, SET_NUMBER(interface_getMenuEntryCount(_M &BrowseFilesMenu)), file_icon);
		}
	}

	if (selectedItem <= MENU_ITEM_LAST)
		selectedItem = interface_getSelectedItem(browseMenu);/*interface_getMenuEntryCount(browseMenu) > 0 ? 0 : MENU_ITEM_MAIN;*/
	if (selectedItem >= interface_getMenuEntryCount(browseMenu))
		selectedItem = media_currentDirCount + media_currentFileCount > 0 ? 0 : MENU_ITEM_MAIN;

	switch(appControlInfo.mediaInfo.typeIndex)
	{
		case mediaVideo:
			file_icon = media_browseType == mediaBrowseTypeUSB ? thumbnail_usb_video : thumbnail_workstation_video;
			break;
		case mediaAudio:
			file_icon = media_browseType == mediaBrowseTypeUSB ? thumbnail_usb_audio : thumbnail_workstation_audio;
			break;
		case mediaImage:
			file_icon = media_browseType == mediaBrowseTypeUSB ? thumbnail_usb_image : thumbnail_workstation_image;
			break;
		default:
			file_icon = media_browseType == mediaBrowseTypeUSB ? thumbnail_usb : thumbnail_workstation;
	}

	if (hasDrives == 0) {
#ifdef ENABLE_SAMBA
		if (media_browseType == mediaBrowseTypeSamba)
			str = _T("SAMBA_NO_SHARES");
		else
#endif
		{
			str = _T("USB_NOTFOUND");
			interface_addEvent( media_check_storages, NULL, 3000, 1 );
		}
		interface_addMenuEntryDisabled(browseMenu, str, thumbnail_info);
	} else if (!isRoot && media_currentFileCount == 0 && media_currentDirCount == 0)
		interface_addMenuEntryDisabled(browseMenu, _T("NO_FILES"), thumbnail_info);
#ifdef ENABLE_SAMBA
	if (media_browseType == mediaBrowseTypeSamba && isRoot)
		file_icon = thumbnail_network;
#endif
	interface_setMenuLogo(browseMenu, file_icon, -1, 0, 0, 0);
	interface_setSelectedItem(browseMenu, selectedItem);
	interface_hideLoading();

	return 0;
}

static int media_enterFileBrowserMenu(interfaceMenu_t *browseMenu, void* pSelected)
{
	mysem_get(media_semaphore);
	media_refreshFileBrowserMenu(browseMenu, pSelected);
	mysem_release(media_semaphore);
	return 0;
}

int media_initUSBBrowserMenu(interfaceMenu_t *pMenu, void* pSelected)
{
	media_browseType = mediaBrowseTypeUSB;
	interfaceMenu_t *browseMenu = _M &BrowseFilesMenu;
	browseMenu->pArg = pSelected;
	browseMenu->statusBarIcons[0] = 0;

	if (strncmp(currentPath, usbRoot, strlen(usbRoot)) !=0 || !helperCheckDirectoryExsists(currentPath) )
		strncpy(currentPath, usbRoot, strlen(usbRoot)+1);

	dprintf("%s: %s\n", __FUNCTION__, currentPath);
	return interface_menuActionShowMenu( pMenu, browseMenu );
}

#ifdef ENABLE_SAMBA
int media_initSambaBrowserMenu(interfaceMenu_t *pMenu, void* pSelected)
{
	media_browseType = mediaBrowseTypeSamba;
	interfaceMenu_t *browseMenu = _M &BrowseFilesMenu;
	browseMenu->pArg = pSelected;

	if (strncmp( currentPath, sambaRoot, strlen(sambaRoot) ) != 0 || !helperCheckDirectoryExsists(currentPath) )
		strcpy( currentPath, sambaRoot );

	dprintf("%s: %s\n", __FUNCTION__,currentPath);
	if (pMenu == browseMenu) {
		media_enterFileBrowserMenu(browseMenu, pSelected);
		interface_displayMenu(1);
		return 0;
	} else
	return interface_menuActionShowMenu( pMenu, browseMenu );
}
#endif

int media_isBrowsingFiles(void)
{
	return interfaceInfo.currentMenu == _M &BrowseFilesMenu;
}

static int media_browseFolder(interfaceMenu_t *pMenu, void* pArg)
{
	size_t currentPathLength = strlen(currentPath);
	int hasParent = strcmp(currentPath,usbRoot) == 0 ? 0 : 1; /**< Samba root has virtual parent - Network Browse */
	int deviceNumber = GET_NUMBER(pArg);

	if (media_browseType == mediaBrowseTypeUSB && deviceNumber < 0) {
		// CD/DVD Drive
		char driveName[32];
		sprintf(driveName, "Drive %d", -(deviceNumber+1));
		strncpy(currentPath+currentPathLength, driveName, PATH_MAX-currentPathLength);
	} else {
		strncpy(currentPath+currentPathLength,
		media_currentDirEntries[ interface_getSelectedItem(pMenu) - hasParent ]->d_name, PATH_MAX-currentPathLength);
	}
	currentPathLength = strlen(currentPath);
	currentPath[currentPathLength]='/';
	currentPath[currentPathLength+1]=0;
	dprintf("%s: entering %s\n", __FUNCTION__,currentPath);

	interface_removeEvent(media_check_storages, NULL);
	if (!helperCheckDirectoryExsists(currentPath)) {
		strcpy(currentPath,ROOT);
		if (media_browseType == mediaBrowseTypeUSB)
			media_check_storages(NULL);
	}

	media_enterFileBrowserMenu(pMenu, NULL);
	interface_displayMenu(1);
	return 0;
}

static int media_browseParentFolder(interfaceMenu_t *pMenu, void* ignored)
{
	*(strrchr(currentPath,'/')) = 0;
	char* parentDir = (strrchr(currentPath,'/')+1);
	strncpy(media_previousDir,parentDir,sizeof(media_previousDir));
	*parentDir = 0;
	dprintf("%s: media_previousDir = %s\n", __FUNCTION__,media_previousDir);
	if (!helperCheckDirectoryExsists(currentPath)) {
		media_previousDir[0] = 0;
		strcpy(currentPath, ROOT);
	}

	dprintf("%s: browsing %s\n", __FUNCTION__,currentPath);
	/* If we have browsed up folder and there is only one drive,
	 * we will automatically be brought back by media_refreshFileBrowserMenu.
	 */
	if (media_browseType == mediaBrowseTypeUSB &&
	    strcmp(currentPath, usbRoot) == 0) 
	{
		int hasDrives = 0;
		char showPath[10];
		for (int dev=0; dev < 10; dev++) {
			sprintf(showPath, "/dev/sr%d", dev);
			if (check_file_exsists(showPath)) {
				hasDrives = 1;
				break;
			}
		}
		if (hasDrives == 0 && media_scanStorages() == 1) {
			interface_menuActionShowMenu(pMenu, pMenu->pParentMenu);
			return 0;
		}
	}

	media_enterFileBrowserMenu(pMenu, SET_NUMBER(MENU_ITEM_PREV));
	interface_displayMenu(1);
	return 0;
}

int media_setBrowseMediaType(int type)
{
	if (type < 0)
		appControlInfo.mediaInfo.typeIndex++;
	else
		appControlInfo.mediaInfo.typeIndex = type;
	if (appControlInfo.mediaInfo.typeIndex >= mediaTypeCount)
		appControlInfo.mediaInfo.typeIndex = mediaAll;
	return saveAppSettings();
}

static inline int media_redrawSettingsMenu(interfaceMenu_t *pMenu, void *pArg)
{
	media_enterSettingsMenu(pMenu,pArg);
	interface_displayMenu(1);
	return 0;
}

static int  media_toggleMediaType(interfaceMenu_t *pMenu, void* pArg)
{
	media_setBrowseMediaType(-1);
	return media_redrawSettingsMenu(pMenu, pArg);
}

int media_togglePlaybackMode(interfaceMenu_t *pMenu, void *pArg)
{
	media_setNextPlaybackMode();
	return media_redrawSettingsMenu(pMenu, pArg);
}

static int  media_toggleSlideshowMode(interfaceMenu_t *pMenu, void* pArg)
{
	media_slideshowSetMode(-1);
	return media_redrawSettingsMenu(pMenu, pArg);
}

static int  media_enterSettingsMenu(interfaceMenu_t *settingsMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH], *str;
	int icon = thumbnail_usb;

	interface_clearMenuEntries(settingsMenu);
	interface_setMenuCapacity(settingsMenu, 3);
	switch( appControlInfo.mediaInfo.playbackMode )
	{
		case playback_looped:     str = _T("LOOPED");    break;
		case playback_sequential: str = _T("SEQUENTAL"); break;
		case playback_random:     str = _T("RANDOM");    break;
		default:                  str = _T("SINGLE");
	}
	sprintf(buf, "%s: %s", _T("PLAYBACK_MODE"), str);
	interface_addMenuEntry(settingsMenu, buf, media_togglePlaybackMode, NULL, thumbnail_turnaround);

	switch( appControlInfo.slideshowInfo.defaultState )
	{
		case slideshowShow:       str = _T("ON");     break;
		case slideshowRandom:     str = _T("RANDOM"); break;
		default:                  str = _T("OFF");
	}
	sprintf(buf, "%s: %s", _T("SLIDESHOW"), str);
	interface_addMenuEntry(settingsMenu, buf, media_toggleSlideshowMode, NULL, thumbnail_turnaround);

	switch( appControlInfo.mediaInfo.typeIndex )
	{
		case mediaVideo:
			str = _T("VIDEO");
			icon = thumbnail_usb_video;
			break;
		case mediaAudio:
			str = _T("AUDIO");
			icon = thumbnail_usb_audio;
			break;
		case mediaImage:
			str = _T("IMAGES");
			icon = thumbnail_usb_image;
			break;
		default:
			str = _T("ALL_FILES");
			icon = thumbnail_usb;
	}
	sprintf(buf, "%s: %s", _T("RECORDED_FILTER_SETUP"), str);
	interface_addMenuEntry(settingsMenu, buf, media_toggleMediaType, NULL, icon);
	return 0;
}

static int media_keyCallback(interfaceMenu_t *browseMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int selectedIndex;
	char URL[MAX_URL];

	switch( cmd->command )
	{
		case interfaceCommandGreen:
			interface_menuActionShowMenu(browseMenu, _M &media_settingsMenu);
			return 0;
#ifdef ENABLE_SAMBA
		case interfaceCommandYellow:
			if (strcmp(currentPath, sambaRoot) == 0) {
				samba_enterLogin(browseMenu, NULL);
				return 0;
			}
			break;
#endif
		case interfaceCommandBlue:
#ifdef ENABLE_SAMBA
			if (strcmp(currentPath, sambaRoot) == 0) {
				samba_manualBrowse(browseMenu, NULL);
				return 0;
			}
#endif
			media_setBrowseMediaType(-1);
			media_enterFileBrowserMenu(browseMenu, SET_NUMBER(MENU_ITEM_LAST));
			interface_displayMenu(1);
			return 0;
		case interfaceCommandBack:
			if (strcmp(currentPath, ROOT) != 0) {
				media_browseParentFolder(browseMenu, NULL);
				return 0;
			} else
			return 1;
		default: ;
	}

	selectedIndex = interface_getSelectedItem(browseMenu);
	if (selectedIndex < 0 || interface_getMenuEntryCount(browseMenu) == 0)
		return 1;

#ifdef ENABLE_SAMBA
	if ( media_browseType == mediaBrowseTypeSamba && cmd->command == interfaceCommandRed )
	{
		char *str = NULL;

		if (selectedIndex == 0 || strcmp(currentPath, sambaRoot ) != 0)
			return 0;
		selectedIndex--; /* "Network browse" */;
		if (selectedIndex >= media_currentDirCount) {
			/* Disabled shares */
			if (interface_getMenuEntryInfo(browseMenu, selectedIndex+1, URL, MENU_ENTRY_INFO_LENGTH) == 0)
				str = URL;
			else
				return 0;
		} else
			str = media_currentDirEntries[ selectedIndex ]->d_name;
		selectedIndex = samba_unmountShare(str);
		if (selectedIndex == 0 ) {
			media_enterFileBrowserMenu(browseMenu, SET_NUMBER(MENU_ITEM_LAST));
			interface_showMessageBox(_T("SAMBA_UNMOUNTED"), thumbnail_info, 3000);
		} else
			eprintf("media: Failed to unmount '%s' (%d)\n", str, selectedIndex);
		return 0;
	} else
#endif // ENABLE_SAMBA
	if ( cmd->command == interfaceCommandYellow)
	{
		if (appControlInfo.mediaInfo.typeIndex == mediaImage)
			return 0;
		selectedIndex = selectedIndex - media_currentDirCount - 1 /* ".." */;
		if (selectedIndex < 0 || selectedIndex >= media_currentFileCount)
			return 0;
		snprintf(URL, MAX_URL, "file://%s%s", currentPath, media_currentFileEntries[ selectedIndex ]->d_name);
		eprintf("media: Add to Playlist '%s'\n",URL);
		playlist_addUrl(URL, media_currentFileEntries[ selectedIndex ]->d_name);
		return 0;
	}

	return 1;
}

static int media_settingsKeyCallback(interfaceMenu_t *settingsMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch(cmd->command)
	{
		case interfaceCommandGreen:
			media_togglePlaybackMode(settingsMenu, pArg);
			return 0;
		case interfaceCommandYellow:
			media_toggleSlideshowMode(settingsMenu, pArg);
			return 0;
		case interfaceCommandBlue:
			media_toggleMediaType(settingsMenu, pArg);
			return 0;
		default: ;
	}
	return 1;
}
