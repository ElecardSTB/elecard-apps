
/*
 rtp.c

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

#include "rtp.h"

#include "debug.h"
#include "app_info.h"
#include "sem.h"
#include "gfx.h"
#include "interface.h"
#include "l10n.h"
#include "menu_app.h"
#include "StbMainApp.h"
#include "rtp_func.h"
#include "media.h"
#include "rtsp.h"
#include "playlist.h"
#include "list.h"
#include "rtp_common.h"
#include "output.h"
#include "m3u.h"
#include "xmlconfig.h"
#include "pvr.h"
#ifdef ENABLE_TELETES
#include "../third_party/teletes/teletes.h"
#endif

// NETLib
#include <service.h>
#include <tools.h>
#include <sdp.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <fcntl.h>

#include <pthread.h>
#include <signal.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

// Time in seconds between infinite tries to start RTP stream
// If undefined playback cancels after first failed attempt
//#define RTP_RECONNECT 5

#define STREAM_INFO_SET(screen, channel) ((void*)(ptrdiff_t)((screen << 16) | (channel)))
#define STREAM_INFO_GET_SCREEN(info)     (((ptrdiff_t)info >> 16) & 0xFFFF)
#define STREAM_INFO_GET_STREAM(info)     ( (ptrdiff_t)info        & 0xFFFF)

#define RTP_AUDIO_TRACL_LIST    CONFIG_DIR "/audiotracks.txt"

#define DATA_BUFF_SIZE (32*1024)

#define GENRE_INVALID  (0x1F)
#define GENRE_COUNT    (0x0C)
#define GENRE_ALL      (0xFF)

/* RTP menu colors */

#define RTP_PROGRAM_BAR_RED   (0x88)
#define RTP_PROGRAM_BAR_GREEN (0x88)
#define RTP_PROGRAM_BAR_BLUE  (0x88)
#define RTP_PROGRAM_BAR_ALPHA INTERFACE_BACKGROUND_ALPHA

#define ENABLE_EPG

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

typedef enum
{
	rtpInfoTypeName = 0,
	rtpInfoTypeShort,
	rtpInfoTypeFull,
	rtpInfoTypeList,
	rtpInfoTypeCount
} rtpInfoType_t;

typedef struct
{
	payload_type video_type;
	payload_type audio_type;
	int video_pid;
	int audio_pid;
	char *url;
	char *thumb;
	char *poster;
	list_element_t *schedule;
	unsigned int id;
	unsigned char genre;
	unsigned int audio;
} rtpMediaInfo_t;

typedef struct
{
	playControlSetupFunction pSetup;
	void *pArg;
} rtpEPGControlInfo_t;

typedef struct
{
	char *title;
	char info[MAX_MESSAGE_BOX_LENGTH];
	struct tm start_tm;
	struct tm end_tm;
} rtpEPGProgramInfo_t;

typedef struct
{
	rtpEPGProgramInfo_t program;
	interfaceMenu_t *previousMenu;
	int showMenuOnExit;
} rtpEPGInfo_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int rtp_setChannelFromURL(interfaceMenu_t *pMenu, char *value, char *description, char *thumbnail, void* pArg);
static int rtp_stream_change(interfaceMenu_t *pMenu, void* pArg);
#ifdef ENABLE_AUTOPLAY
static int rtp_stream_selected(interfaceMenu_t *pMenu, void* pArg);
static int rtp_stream_deselected(interfaceMenu_t *pMenu, void* pArg);
#endif
static int rtp_stream_start(void* pArg);
static void rtp_setupPlayControl(void *pArg);
static int rtp_fillStreamMenu(int which);
static int rtp_fillGenreMenu();

static int rtp_stopCollect(interfaceMenu_t* pMenu, void *pArg);

static void* rtp_epgThread(void *pArg);
static void rtp_showEpg(void);

static int rtp_getProgramInfo(int channel, int offset, rtpInfoType_t type);
static void rtp_displayShortInfo();
static int rtp_shortInfoProcessCommand(pinterfaceCommandEvent_t cmd, void* pArg);
static int rtp_longInfoCallback(interfaceMenu_t* pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static void rtp_urlHandler(void *pArg, const char *location, const char *desc, xmlConfigHandle_t track);

static void rtp_setStateCheckTimer(int which, int bEnable);
static int rtp_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int rtp_menuEntryDisplay(interfaceMenu_t *pMenu, DFBRectangle *rect, int i);

static int rtp_epgKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#ifdef ENABLE_PVR
#ifdef STBPNX
static int rtp_epgEntryDisplay(interfaceMenu_t *pMenu, DFBRectangle *rect, int i);
#endif
#endif

#ifdef ENABLE_MULTI_VIEW
static int rtp_multiviewPlay(interfaceMenu_t *pMenu, void *pArg);
#endif

#ifdef RTP_RECONNECT
static int rtp_reconnectEvent(void *pArg);
#endif

static int rtp_saveAudioTrackList();
static int rtp_loadAudioTrackList();

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static interfaceListMenu_t rtpEpgMenu;
rtp_common_instance rtp;

static struct rtp_list streams;

/** start/stop semaphore */
static pmysem_t  rtp_semaphore;
static pmysem_t  rtp_epg_semaphore;
static pmysem_t  rtp_curl_semaphore;

static rtpMediaInfo_t rtp_info[RTP_MAX_STREAM_COUNT];
/** Streams are sorted by genre and by title */
static int sorted_streams[RTP_MAX_STREAM_COUNT];

/** Indeces of first stream of each genre or -1 if none */
static int genre_indeces[0x10];
static unsigned char genre_index = GENRE_COUNT; /**< current genre index */

static interfaceListMenu_t rtpGenreMenu;

static int program_offset = 0; /**< programme offset for IPTV EPG */
static char   channel_buff[DATA_BUFF_SIZE];
static rtpEPGControlInfo_t rtp_epgControl;
static rtpEPGInfo_t        rtpEpgInfo;

static int rtp_sap_collected = 0; 

#ifdef ENABLE_MULTI_VIEW
rtp_common_instance rtp_multiview[4];
#endif

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

interfaceListMenu_t rtpStreamMenu;

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

void rtp_buildMenu(interfaceMenu_t *pParent)
{
	int rtp_icons[4] = { statusbar_f1_sorting, statusbar_f2_info, statusbar_f3_add,
#ifdef ENABLE_EPG
	statusbar_f4_schedule
#else
	statusbar_f4_enterurl
#endif
	};

	rtp.selectedDesc.connection.address.IPv4.s_addr = INADDR_NONE;

	{
		createListMenu(&rtpStreamMenu, _T("TV_CHANNELS_LIST"), thumbnail_multicast, rtp_icons, pParent,
					   /* interfaceInfo.clientX, interfaceInfo.clientY,
					   interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
					   NULL, rtp_stopCollect, SET_NUMBER(screenMain));
		interface_setCustomKeysCallback((interfaceMenu_t*)&rtpStreamMenu, rtp_keyCallback);

		rtp_common_init_instance(&rtp);
		rtp_session_init(&rtp.rtp_session);
	}

	createListMenu(&rtpGenreMenu, _T("CHOOSE_GENRE"), thumbnail_multicast, NULL, (interfaceMenu_t*)&rtpStreamMenu,
		/* interfaceInfo.clientX, interfaceInfo.clientY,
		interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
		NULL, NULL, NULL);

	createListMenu(&rtpEpgMenu, _T("EPG"), thumbnail_epg, NULL, (interfaceMenu_t*)&rtpStreamMenu,
		/* interfaceInfo.clientX, interfaceInfo.clientY,
		interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuNoThumbnail,
		NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&rtpEpgMenu, rtp_epgKeyCallback);

	mysem_create(&rtp_semaphore);
	mysem_create(&rtp_epg_semaphore);
	mysem_create(&rtp_curl_semaphore);

	rtpEpgInfo.program.title = NULL;
	rtpEpgInfo.program.info[0] = 0;
	rtpEpgInfo.showMenuOnExit = 0;
	rtpEpgInfo.previousMenu = NULL;
}

void rtp_cleanupMenu()
{
	rtp_session_destroy(rtp.rtp_session);
	rtp_cleanupEPG();
	mysem_destroy(rtp_semaphore);
	mysem_destroy(rtp_epg_semaphore);
	mysem_destroy(rtp_curl_semaphore);
}

static int rtp_stateTimerEvent(void *pArg)
{
	DFBVideoProviderStatus status;

	status = gfx_getVideoProviderStatus(screenMain);
	switch( status )
	{
		case DVSTATE_FINISHED:
		case DVSTATE_STOP:
			interface_showMenu(1, 0);
			if( status == DVSTATE_FINISHED )
				interface_showMessageBox(_T("ERR_STREAM_NOT_SUPPORTED"), thumbnail_error, 0);
			rtp_stopVideo(screenMain);
			break;
		default:
			rtp_setStateCheckTimer(screenMain, 1);
	}

	return 0;
}

static void rtp_setStateCheckTimer(int which, int bEnable)
{
	dprintf("%s: %s state timer\n", __FUNCTION__, bEnable ? "set" : "unset");

	if (bEnable)
	{
		interface_addEvent(rtp_stateTimerEvent, STREAM_INFO_SET(which, 0), 1000, 1);
	} else
	{
		interface_removeEvent(rtp_stateTimerEvent, STREAM_INFO_SET(which, 0));
	}
}

static int rtp_checkStream(void *pArg)
{
	struct timeval lastts, curts;
	suseconds_t timeout_threshold;
	int redraw;

	dprintf("%s: in\n", __FUNCTION__);

	timeout_threshold = RTP_TIMEOUT*1000000;

	if (appControlInfo.rtpInfo.active != 0)
	{
		rtp_get_last_data_timestamp(rtp.rtp_session, &lastts);
		gettimeofday(&curts, 0);

		if ((curts.tv_sec - lastts.tv_sec)*1000000+(curts.tv_usec - lastts.tv_usec) >= timeout_threshold)
		{
			if (rtp.data_timeout == 0)
			{
				eprintf("RTP: TIMEOUT %d while waiting for data\n", timeout_threshold);
				rtp.data_timeout = 1;
				interface_notifyText(_T("ERR_NO_DATA"), 1);
			}
		} else
		{
			redraw = rtp.data_timeout;
			rtp.data_timeout = 0;
			interface_notifyText(NULL, redraw);
		}
		interface_addEvent(rtp_checkStream, pArg, 1000, 1);
	} else
	{
		rtp.data_timeout = 0;
		interface_notifyText(NULL, 0);
		interface_removeEvent(rtp_checkStream, pArg);
	}

	return 0;
}

void rtp_stopVideo(int which)
{
	mysem_get(rtp_semaphore);

	dprintf("%s: got sem\n", __FUNCTION__);

#ifdef RTP_RECONNECT
	interface_removeEvent( rtp_reconnectEvent, NULL );
#endif
	interface_playControlSelect(interfacePlayControlStop);

	if ( appControlInfo.rtpInfo.active != 0 )
	{
		rtp_setStateCheckTimer(which, 0);
		rtp_stop_receiver(rtp.rtp_session);

		dprintf("%s: screen%s\n", __FUNCTION__, which ? "Pip" : "Main");
		rtp_common_close(&rtp);

		gfx_stopVideoProvider(which, 1, 1);
		unlink(rtp.pipeString);
		appControlInfo.rtpInfo.active = 0;
#ifdef ENABLE_MULTI_VIEW
		appControlInfo.multiviewInfo.count = 0;
#endif

		rtp_checkStream(STREAM_INFO_SET(which, 0));
		interface_disableBackground();
	}

	mysem_release(rtp_semaphore);
}

int rtp_startVideo(int which)
{
	int ret;
	char qualifier[64];
	char pipeString[256];

	interface_showLoadingAnimation();

	gfx_stopVideoProviders(which);

	media_slideshowStop(1);

	mysem_get(rtp_semaphore);

	dprintf("%s: got sem\n", __FUNCTION__);

	if ( rtp.selectedDesc.connection.address.IPv4.s_addr == INADDR_NONE )
	{
//		if (streams.items[0].is_mp2t) {
//			memcpy(&rtp.streamdesc, &streams.items[0], sizeof(sdp_desc));
//		} else {
		mysem_release(rtp_semaphore);
		interface_hideLoadingAnimation();
		interface_showMessageBox(_T("ERR_NO_STREAM_SELECTED"), thumbnail_error, 0);
		eprintf("RTP: No stream is currently selected\n");
		return -1;
//		}
	}

	if (rtp.selectedDesc.media[0].fmt == payloadTypeH264 ||
		rtp.selectedDesc.media[0].fmt == payloadTypeMpeg2)
	{
#if 1
		mysem_release(rtp_semaphore);
		interface_hideLoadingAnimation();
		interface_showMessageBox(_T("ERR_ES_NOT_SUPPORTED"), thumbnail_error, 0);
		eprintf("RTP: ES streaming not supported\n");
		return -1;
#else
		qualifier[0] = 0;
		sprintf(pipeString,"%s://%s:%d",
			proto_toa(rtp.selectedDesc.media[0].proto),
			inet_ntoa(rtp.selectedDesc.connection.address.IPv4),
			rtp.selectedDesc.media[0].port);
		dprintf("RTP: start video url=%s\n",pipeString);
		ret = gfx_startVideoProvider(pipeString, which, 1, qualifier);

		if (appControlInfo.rtpMenuInfo.channel >= 0 &&
		    appControlInfo.rtpMenuInfo.channel != CHANNEL_CUSTOM &&
		    rtp_info[appControlInfo.rtpMenuInfo.channel].video_type > 0 )
		{
			sprintf(&pipeString[strlen(pipeString)], ":manualDetectVideoInfo:videoType=%d:videoPid=%d:audioType=%d:audioPid=%d",rtp_info[appControlInfo.rtpMenuInfo.channel].video_type,rtp_info[appControlInfo.rtpMenuInfo.channel].video_pid,rtp_info[appControlInfo.rtpMenuInfo.channel].audio_type,rtp_info[appControlInfo.rtpMenuInfo.channel].audio_pid);
		}

		dprintf("%s: video provider started with return code %d\n", __FUNCTION__, ret);

		if ( ret != 0 )
		{
			rtp_common_close(&rtp);
			mysem_release(rtp_semaphore);
			interface_showMessageBox(_T("ERR_VIDEO_PROVIDER"), thumbnail_error, 0);
			eprintf("RTP: Failed to start video provider %s\n", pipeString);
			return ret;
		}

		if ((ret = rtp_common_open(NULL, rtp.pipeString, "/dev/dsppipe0", rtp.pipeString, 0, 0, 0, &rtp.dvrfd, NULL, NULL, NULL)) != 0)
		{
			rtp_common_close(&rtp);
			mysem_release(rtp_semaphore);
			interface_hideLoadingAnimation();
			interface_showMessageBox(_T("ERR_SPECIAL_FILE"), thumbnail_error, 0);
			eprintf("RTP: open dvr failed - error %d\n", ret);
			return -1;
		}

		dprintf("%s: start h264 receiver\n", __FUNCTION__);

		rtp_change_eng(rtp.rtp_session, RTP_ENGINE);	// set RTP engine

		if ((ret = rtp_start_receiver(rtp.rtp_session, &rtp.selectedDesc, -1, 0, appControlInfo.useVerimatrix|appControlInfo.useSecureMedia)) != 0)
		{
			rtp_common_close(&rtp);
			gfx_stopVideoProvider(which, 1, 1);
			mysem_release(rtp_semaphore);
			interface_hideLoadingAnimation();
			interface_showMessageBox(_T("ERR_RTP_RECEIVER"), thumbnail_error, 0);
			eprintf("RTP: Failed to start RTP receiver - error %d\n", ret);
			return -1;
		}

		//rtp.dvrfd = open("/rtp_h264.dump", O_CREAT|O_TRUNC|O_WRONLY);

		if (rtp.selectedDesc.media[0].sps_pps.length > 0)
		{
			char buf[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			//dprintf("%s: Write %d bytes of media[0].sps_pps\n", __FUNCTION__, rtp.selectedDesc.media[0].sps_pps.length);
			*((short*)&buf[0]) = 1; // video
			*((short*)&buf[2]) = 12+rtp.selectedDesc.media[0].sps_pps.length;
			write(rtp.dvrfd, buf, 12);
			write(rtp.dvrfd, rtp.selectedDesc.media[0].sps_pps.data, rtp.selectedDesc.media[0].sps_pps.length);
			/*
			int fd = open("/sps_pps.dump", O_CREAT|O_TRUNC|O_WRONLY);
			write(fd, rtp.selectedDesc.media[0].sps_pps.data, rtp.selectedDesc.media[0].sps_pps.length);
			close(fd);
			*/
		}

		dprintf("%s: start output: dvr %d, fdv %d, fda %d, fdp %d\n", __FUNCTION__, rtp.dvrfd, rtp.fdv, rtp.fda, rtp.fdp);

		if (rtp_start_output(rtp.rtp_session, rtp.dvrfd, rtp.fdv) != 0)
		{
			rtp_stop_receiver(rtp.rtp_session);
			rtp_common_close(&rtp);
			gfx_stopVideoProvider(which, 1, 1);
			mysem_release(rtp_semaphore);
			interface_hideLoadingAnimation();
			interface_showMessageBox(_T("ERR_STREAM_UNAVAILABLE"), thumbnail_error, 0);
			eprintf("RTP: Stream is unavailable\n");
			return -1;
		}
#endif
	} else if (rtp.selectedDesc.media[0].fmt == payloadTypeMpegTS)
	{
		qualifier[0] = 0;
		sprintf(pipeString,"%s://%s:%d",
			proto_toa(rtp.selectedDesc.media[0].proto),
			inet_ntoa(rtp.selectedDesc.connection.address.IPv4),
			rtp.selectedDesc.media[0].port);

		if (appControlInfo.rtpMenuInfo.channel >= 0 &&
		    appControlInfo.rtpMenuInfo.channel != CHANNEL_CUSTOM &&
		    rtp_info[appControlInfo.rtpMenuInfo.channel].video_type > 0 )
		{
			sprintf(&pipeString[strlen(pipeString)], ":manualDetectVideoInfo:videoType=%d:videoPid=%d:audioType=%d:audioPid=%d",rtp_info[appControlInfo.rtpMenuInfo.channel].video_type,rtp_info[appControlInfo.rtpMenuInfo.channel].video_pid,rtp_info[appControlInfo.rtpMenuInfo.channel].audio_type,rtp_info[appControlInfo.rtpMenuInfo.channel].audio_pid);
		}

		dprintf("%s: video provider start %s\n", __FUNCTION__,pipeString);
		ret = gfx_startVideoProvider(pipeString, which, 1, qualifier);

		dprintf("%s: video provider started with return code %d\n", __FUNCTION__, ret);

		if ( ret != 0 )
		{
			rtp_common_close(&rtp);
			eprintf("RTP: Failed to start video provider url=%s\n", pipeString);
#ifndef RTP_RECONNECT
			mysem_release(rtp_semaphore);
			interface_showMessageBox(_T("ERR_VIDEO_PROVIDER"), thumbnail_error, 0);
			return ret;
#endif
		}

#ifdef ENABLE_VERIMATRIX
		if (appControlInfo.useVerimatrix != 0)
		{
			eprintf("RTP: Enable verimatrix...\n");
			if (gfx_enableVideoProviderVerimatrix(screenMain, VERIMATRIX_INI_FILE) != 0)
			{
				interface_showMessageBox(_T("ERR_VERIMATRIX_INIT"), thumbnail_error, 0);
			}
		}
#endif
#ifdef ENABLE_SECUREMEDIA
		if (appControlInfo.useSecureMedia != 0)
		{
			eprintf("RTP: Enable securemedia...\n");
			if (gfx_enableVideoProviderSecureMedia(screenMain) != 0)
			{
				interface_showMessageBox(_T("ERR_SECUREMEDIA_INIT"), thumbnail_error, 0);
			}
		}
#endif
	} else
	{
			mysem_release(rtp_semaphore);
			interface_hideLoadingAnimation();
			interface_showMessageBox(_T("ERR_RTP_RECEIVER"), thumbnail_error, 0);
			eprintf("RTP: Unknown payload type %d\n", rtp.selectedDesc.media[0].fmt);
			return -1;
	}

#ifdef RTP_RECONNECT
	if( ret == 0 ) {
#endif
	appControlInfo.mediaInfo.bHttp = 0;
	appControlInfo.rtpInfo.active = 1;

	rtp_setStateCheckTimer(which, 1);
	//rtp_checkStream(STREAM_INFO_SET(which, 0));
#ifdef RTP_RECONNECT
	} else
	{
		interface_notifyText(_T("ERR_NO_DATA"), 1);
		interface_addEvent(rtp_reconnectEvent, NULL, RTP_RECONNECT*1000, 1);
	}
#endif

	interface_playControlSelect(interfacePlayControlStop);
	interface_playControlSelect(interfacePlayControlPlay);

	mysem_release(rtp_semaphore);

	interface_hideLoadingAnimation();
	interface_displayMenu(1);

	return 0;
}

static int rtp_menuEntryDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i)
{
	if( pMenu->menuType != interfaceMenuList )
	{
		eprintf("%s: unsupported menu type %d\n", __FUNCTION__, pMenu->menuType);
		return -1;
	}

	int selected                   = pMenu->selectedItem == i;
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t *)pMenu;
	char *second_line = NULL;
	int index = -1;

	switch( pListMenu->listMenuType )
	{
		case interfaceListMenuIconThumbnail:
			if ( pListMenu->baseMenu.menuEntry[i].type == interfaceMenuEntryText )
			{
				int maxWidth;
				int fh;
				int x,y;
				int r,g,b,a;

				/* Draw selection rectangle */
				if ( selected )
				{
					DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
					gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, rect->x, rect->y, rect->w, rect->h);
				}

				/* Draw custom thumbnail (by url) */
				if ( pListMenu->baseMenu.menuEntry[i].pAction == rtp_stream_change )
				{
					x = rect->x+interfaceInfo.paddingSize+/*INTERFACE_ARROW_SIZE +*/ pListMenu->baseMenu.thumbnailWidth/2;
					y = rect->y+pListMenu->baseMenu.thumbnailHeight/2;
					index = STREAM_INFO_GET_STREAM(pListMenu->baseMenu.menuEntry[i].pArg);

					if( appControlInfo.rtpMenuInfo.usePlaylistURL == 0 || //pListMenu->baseMenu.menuEntry[i].thumbnail != thumbnail_multicast ||
					    index < 0 || index >= RTP_MAX_STREAM_COUNT  ||
					    rtp_info[index].thumb == NULL ||
					    0 != interface_drawImage(DRAWING_SURFACE, rtp_info[index].thumb, x, y, pMenu->thumbnailWidth, pMenu->thumbnailHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0)
					)
						interface_drawImage(DRAWING_SURFACE, resource_thumbnails[pListMenu->baseMenu.menuEntry[i].thumbnail], x, y, pMenu->thumbnailWidth, pMenu->thumbnailHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0);
				}

				if ( pListMenu->baseMenu.menuEntry[i].isSelectable )
				{
					r = INTERFACE_BOOKMARK_RED;
					g = INTERFACE_BOOKMARK_GREEN;
					b = INTERFACE_BOOKMARK_BLUE;
					a = INTERFACE_BOOKMARK_ALPHA;
				} else
				{
					r = INTERFACE_BOOKMARK_DISABLED_RED;
					g = INTERFACE_BOOKMARK_DISABLED_GREEN;
					b = INTERFACE_BOOKMARK_DISABLED_BLUE;
					a = INTERFACE_BOOKMARK_DISABLED_ALPHA;
				}
				pgfx_font->GetHeight(pgfx_font, &fh);

				/* Two-lined entries workaround */
				second_line = strchr(pListMenu->baseMenu.menuEntry[i].info, '\n');
				if (second_line != NULL)
					*second_line = 0;

				maxWidth = interfaceInfo.clientWidth - interfaceInfo.paddingSize*5/* - INTERFACE_ARROW_SIZE*/ - interfaceInfo.thumbnailSize - INTERFACE_SCROLLBAR_WIDTH;
				x = rect->x+interfaceInfo.paddingSize*2/*+INTERFACE_ARROW_SIZE*/+pListMenu->baseMenu.thumbnailWidth;
				//if (second_line)
					y = rect->y + pListMenu->baseMenu.thumbnailHeight/2 - 2; // + fh/4;
				//else
				//	y =rect->y + pListMenu->baseMenu.thumbnailHeight/2 + fh/4;

				/* Display current programme progress bar */
				if ( appControlInfo.rtpMenuInfo.epg[0] != 0 &&
				     index >= 0 && index < RTP_MAX_STREAM_COUNT &&
				     rtp_info[index].schedule != NULL
				   )
				{
					/* Find current programme */
					EIT_event_t *event;
					time_t event_start, event_length, now;
					struct tm start_tm;

					mysem_get( rtp_epg_semaphore );
					time(&now);
					offair_findCurrentEvent(rtp_info[index].schedule, now, &event, &event_start, &event_length, &start_tm);
					if( event != NULL )
					{
						char buf[MENU_ENTRY_INFO_LENGTH];
						size_t length;
						int text_x = x, text_y;

						/* Draw progress bar
							* |        <thumb> {Channel name                                   }| *
							*                   event_start   now        event_start+event_length *
							* |rect->x         [x             x+bar_w]   rect->x+rect->w        | */
						int bar_w = ( now-event_start )*( rect->w-(x-rect->x) )/( event_length );
						//dprintf("%s: '%s' %ld %ld %ld |%d [%d %d] %d|\n", __func__, event->description.event_name, event_start, now, event_length, rect->x, text_x, x+bar_w, rect->x+rect->w);
						text_y = rect->y+rect->h-fh;
						DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
						gfx_drawRectangle( DRAWING_SURFACE, RTP_PROGRAM_BAR_RED, RTP_PROGRAM_BAR_GREEN, RTP_PROGRAM_BAR_BLUE, RTP_PROGRAM_BAR_ALPHA, text_x, rect->y, bar_w, rect->h );

						/* Dy default, prefer event from schedule over second line in playlist */
						second_line = NULL;
						//if (second_line == NULL)
						{
							/* Draw start and end time */
							text_y = rect->y+rect->h-fh/4;
							strftime( buf, sizeof(buf), "%H:%M", &start_tm );
							gfx_drawText(DRAWING_SURFACE, pgfx_smallfont, r, g, b, a, text_x+fh, text_y, buf, 0, 0);
							event_start += event_length;
							localtime_r( &event_start, &start_tm );
							strftime( buf, sizeof(buf), "%H:%M", &start_tm );
							gfx_drawText(DRAWING_SURFACE, pgfx_smallfont, r, g, b, a, rect->x+rect->w - interfaceInfo.thumbnailSize, text_y, buf, 0, 0);

							/* Draw programme name */
							text_x += fh+interfaceInfo.thumbnailSize;
							maxWidth = rect->x+rect->w - interfaceInfo.thumbnailSize - text_x;
							strncpy( buf, (char*)event->description.event_name, sizeof(buf) );
							buf[sizeof(buf)-1] = 0;
							length = getMaxStringLengthForFont(pgfx_smallfont, buf, maxWidth);
							if( length+3 < strlen(buf) )
							{
								buf[length-2] = '.';
								buf[length-1] = '.';
								buf[length-0] = '.';
								buf[length+1] = 0;
							}
							gfx_drawText(DRAWING_SURFACE, pgfx_smallfont, r, g, b, a, text_x, text_y, buf, 0, 0);
						}
					} else 
					{
						dprintf("%s: not found current event for %d %s\n", __FUNCTION__, index, streams.items[index].session_name);
					}
					mysem_release( rtp_epg_semaphore );
				}

				tprintf("%c%c%s\t\t\t|%d\n", selected ? '>' : ' ', pListMenu->baseMenu.menuEntry[i].isSelectable ? ' ' : '-', pListMenu->baseMenu.menuEntry[i].info, pListMenu->baseMenu.menuEntry[i].thumbnail);
				gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, pListMenu->baseMenu.menuEntry[i].info, 0, selected);

				if (second_line != NULL)
				{
					char entryText[MENU_ENTRY_INFO_LENGTH];
					char *info_second_line = entryText + (second_line - pListMenu->baseMenu.menuEntry[i].info) + 1;
					int length;

					*second_line = '\n';

					interface_getMenuEntryInfo(pMenu,i,entryText,MENU_ENTRY_INFO_LENGTH);
					length = getMaxStringLengthForFont(pgfx_smallfont, info_second_line, maxWidth-fh);
					info_second_line[length] = 0;
					gfx_drawText(DRAWING_SURFACE, pgfx_smallfont, r, g, b, a, x+fh, y+fh, info_second_line, 0, selected);
				}
			} else
			{
				return interface_menuEntryDisplay(pMenu, rect, i);
			}
			break;
		case interfaceListMenuNoThumbnail:
		{
			int res;
			/* Draw progress bar for current programme */
			if( appControlInfo.rtpMenuInfo.epg[0] != 0 && pListMenu->baseMenu.menuEntry[i].pAction == rtp_stream_change )
			{
				index = STREAM_INFO_GET_STREAM(pListMenu->baseMenu.menuEntry[i].pArg);
				if( rtp_info[index].schedule != NULL )
				{
					/* Find current programme */
					EIT_event_t *event;
					time_t event_start, event_length, now;

					mysem_get( rtp_epg_semaphore );
					time(&now);
					offair_findCurrentEvent(rtp_info[index].schedule, now, &event, &event_start, &event_length, NULL);
					if( event != NULL )
					{
						int bar_w = ( now-event_start )*( rect->w )/( event_length );
						DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
						gfx_drawRectangle( DRAWING_SURFACE, RTP_PROGRAM_BAR_RED, RTP_PROGRAM_BAR_GREEN, RTP_PROGRAM_BAR_BLUE, RTP_PROGRAM_BAR_ALPHA, rect->x, rect->y, bar_w, rect->h );
					}
					mysem_release( rtp_epg_semaphore );
				}
			}
			/* Workaround for two-lined entries */
			second_line = strchr(pListMenu->baseMenu.menuEntry[i].info, '\n');
			if( second_line != NULL )
				*second_line = 0;

			/* Draw entry on top of progress bar */
			res = interface_menuEntryDisplay(pMenu, rect, i);

			if( second_line != NULL )
				*second_line = '\n';
			return res;
		}
		default:
			return interface_menuEntryDisplay(pMenu, rect, i);
	}

	return 0;
}

static int rtp_fillStreamMenu(int which)
{
	int selected;
	int streamNumber;
	int sortedNumber;
	char channelEntry[MENU_ENTRY_INFO_LENGTH], *str;
	unsigned char cur_genre = GENRE_INVALID, genre;
	url_desc_t src, cmp;
	int last_channel_is_udp = 0;

	if( appControlInfo.rtpMenuInfo.lastUrl[0] != 0 )
		last_channel_is_udp = strncasecmp( appControlInfo.rtpMenuInfo.lastUrl, "udp", 3 ) == 0 || strncasecmp( appControlInfo.rtpMenuInfo.lastUrl, "rtp", 3 ) == 0;
	if( last_channel_is_udp )
	{
		if( parseURL( appControlInfo.rtpMenuInfo.lastUrl, &src ) != 0 )
			last_channel_is_udp = 0;
	}
	//dprintf("%s: %d compose %d (%08X)\n", __FUNCTION__, which, streams.count, streams);

	selected = interface_getSelectedItem((interfaceMenu_t*)&rtpStreamMenu);

	interface_clearMenuEntries((interfaceMenu_t*)&rtpStreamMenu);

	interfaceMenu_t *pMenu = (interfaceMenu_t*)&rtpStreamMenu;
	// maxima
	menuEventFunction savReinit = pMenu->reinitializeMenu;
	pMenu->reinitializeMenu = NULL;
	mysem_get(rtp_semaphore);
	if ( streams.count > 0 )
	{
		streamNumber = genre_index >= GENRE_COUNT ? 0 : genre_indeces[genre_index];
		if( streamNumber < 0 )
		{
			genre_index = GENRE_COUNT;
			streamNumber = 0;
		}
		for ( ; streamNumber<streams.count; streamNumber++ )
		{
			if( genre_index != GENRE_COUNT /* && appControlInfo.rtpMenuInfo.usePlaylistURL != 0 */ )
				sortedNumber = sorted_streams[streamNumber];
			else
				sortedNumber = streamNumber;
			genre = (rtp_info[ sortedNumber ].genre >> 4) & 0x0F;
			if( genre_index < GENRE_COUNT && genre != genre_index )
				break;
			if( genre_index != GENRE_COUNT && genre != cur_genre )
			{
				cur_genre = genre;
				switch( cur_genre )
				{
					case  1: str = _T("MOVIE_DRAMA"); break;
					case  2: str = _T("NEWS"); break;
					case  3: str = _T("SHOW_GAME"); break;
					case  4: str = _T("SPORTS"); break;
					case  5: str = _T("CHILDRENS"); break;
					case  6: str = _T("MUSIC"); break;
					case  7: str = _T("CULTURE"); break;
					case  8: str = _T("SOCIAL_POLITICS"); break;
					case  9: str = _T("EDUCATION"); break;
					case 10: str = _T("LEISURE"); break;
					case 11: str = _T("SPECIAL"); break;
					default: str = NULL;
				}
				if( str != NULL )
				{
					interface_addMenuEntry((interfaceMenu_t*)&rtpStreamMenu, str, (menuActionFunction)menuDefaultActionShowMenu, &rtpGenreMenu, -1);
				}
			}
			//dprintf("%s: Check-add stream 0 (fmt %d): %d: %s %s:%d\n", __FUNCTION__, streams.items[streamNumber].media[0].fmt, position, streams.items[streamNumber].session_name, inet_ntoa(streams.items[streamNumber].connection.address.IPv4), streams.items[streamNumber].media[0].port);
			//dprintf("%s: Check-add stream 1 (fmt %d): %d: %s %s:%d\n", __FUNCTION__, streams.items[streamNumber].media[1].fmt, position, streams.items[streamNumber].session_name, inet_ntoa(streams.items[streamNumber].connection.address.IPv4), streams.items[streamNumber].media[1].port);
			switch( streams.items[sortedNumber].media[0].proto )
			{
				case mediaProtoRTP:
				case mediaProtoUDP:
					if ( streams.items[sortedNumber].media[0].fmt != payloadTypeUnknown && rtp_engine_supports_transport(RTP_ENGINE, streams.items[sortedNumber].media[0].proto ))
					{
						if( streams.items[sortedNumber].session_name[0] != 0 )
						{
							sprintf(channelEntry, "%d: %s", sortedNumber+1, streams.items[sortedNumber].session_name);
						} else
						{
							sprintf(channelEntry, "%d: %s://%s:%d", sortedNumber+1, streams.items[sortedNumber].media[0].proto == mediaProtoRTP ? "rtp" : "udp", inet_ntoa(streams.items[sortedNumber].connection.address.IPv4), streams.items[sortedNumber].media[0].port);
						}
						interface_addMenuEntryCustom((interfaceMenu_t*)&rtpStreamMenu, interfaceMenuEntryText,  channelEntry, strlen(channelEntry)+1, 1, rtp_stream_change,
#ifdef ENABLE_AUTOPLAY
						  rtp_stream_selected, rtp_stream_deselected,
#else
						  NULL, NULL,
#endif
						  rtp_menuEntryDisplay, STREAM_INFO_SET(which, sortedNumber), thumbnail_multicast);
						if( last_channel_is_udp &&
						    strncasecmp( rtp_info[sortedNumber].url, appControlInfo.rtpMenuInfo.lastUrl, 3 ) == 0 &&
						    parseURL( rtp_info[sortedNumber].url, &cmp ) == 0 )
						{
							// ignore fast PIDs and @ before address
							if( src.port == cmp.port && strcmp( src.address, cmp.address ) == 0 )
								selected = interface_getMenuEntryCount((interfaceMenu_t*)&rtpStreamMenu)-1;
						}
					}
					break;
				case mediaProtoRTSP:
				case mediaProtoHTTP:
				case mediaProtoRTMP:
					if( streams.items[sortedNumber].session_name[0] != 0 )
					{
						snprintf(channelEntry, sizeof(channelEntry)-1, "%d: %s", sortedNumber+1, streams.items[sortedNumber].session_name);
					} else
					{
						snprintf(channelEntry, sizeof(channelEntry)-1, "%d: %s", sortedNumber+1, rtp_info[sortedNumber].url);
					}
					channelEntry[sizeof(channelEntry)-1] = 0;
					interface_addMenuEntryCustom((interfaceMenu_t*)&rtpStreamMenu, interfaceMenuEntryText,  channelEntry, strlen(channelEntry)+1, 1, rtp_stream_change,
#ifdef ENABLE_AUTOPLAY
					  NULL, rtp_stream_deselected,
#else
					  NULL, NULL,
#endif
					  rtp_menuEntryDisplay, STREAM_INFO_SET(which, sortedNumber), (streams.items[sortedNumber].media[0].proto == mediaProtoRTSP ? thumbnail_vod : thumbnail_internet));
					if( !last_channel_is_udp && 0 == strcmp( appControlInfo.rtpMenuInfo.lastUrl, rtp_info[sortedNumber].url ))
						selected = interface_getMenuEntryCount((interfaceMenu_t*)&rtpStreamMenu)-1;
					break;
				default:;
			}
		}
	}
	pMenu->reinitializeMenu = savReinit;
	interface_menuReset(pMenu);
	mysem_release(rtp_semaphore);

	if (streams.count <= 0 || (genre_index != GENRE_COUNT && cur_genre == GENRE_INVALID))
	{
		str = _T("NO_CHANNELS");
		interface_addMenuEntryDisabled((interfaceMenu_t*)&rtpStreamMenu, str, thumbnail_info);
		selected = MENU_ITEM_MAIN;
	} else if (selected >= interface_getMenuEntryCount((interfaceMenu_t*)&rtpStreamMenu))
	{
		selected = MENU_ITEM_MAIN;
	}
	interface_setSelectedItem((interfaceMenu_t*)&rtpStreamMenu, selected);

	//dprintf("%s: compose done\n", __FUNCTION__);

	if( interfaceInfo.showMenu )
	{
		interface_displayMenu(1);
	}

	return 0;
}

static int rtp_getURL(char *url, int which, int channelNumber)
{
	if( channelNumber < 0 || channelNumber >= streams.count)
		return -1;
#if 1
	strcpy(url, rtp_info[channelNumber].url);
	return 1;
#else
	return sprintf(url,"%s://%s:%d",
		proto_toa(streams.items[channelNumber].media[0].proto),
		inet_ntoa(streams.items[channelNumber].connection.address.IPv4),
		streams.items[channelNumber].media[0].port);
#endif
}

int rtp_startNextChannel(int direction, void* pArg)
{
	int streamNumber;
	int prev = -1;
	int found = 0;
	int new_index = -1;
	int first = -1;
	unsigned char genre;

	if(appControlInfo.playbackInfo.playlistMode == playlistModeFavorites)
	{
		playlist_setLastUrl(appControlInfo.rtpMenuInfo.lastUrl);
		return playlist_startNextChannel(direction,SET_NUMBER(-1));
	}
#ifdef ENABLE_TELETES
	if(appControlInfo.playbackInfo.playlistMode == playlistModeTeletes)
	{
		return teletes_startNextChannel(direction, NULL);
	}
#endif

	mysem_get(rtp_semaphore);

	if ( streams.count > 1 )
	{
		if( appControlInfo.rtpMenuInfo.usePlaylistURL != 0 && genre_index < GENRE_COUNT && genre_indeces[genre_index] >= 0 )
		{
			first = genre_indeces[genre_index];
			for( streamNumber = first; streamNumber<streams.count; streamNumber++ )
			{
				genre = (rtp_info[sorted_streams[streamNumber]].genre >> 4) & 0x0F;
				if( genre != genre_index )
					break;
				if (found == 1 && direction == 0) // next
				{
					new_index = sorted_streams[streamNumber];
					break;
				}
				if ( strcmp( appControlInfo.rtpMenuInfo.lastUrl, rtp_info[sorted_streams[streamNumber]].url ) == 0 )
/*				     (playlistModeIPTV == appControlInfo.playbackInfo.playlistMode && 0 == strcmp( appControlInfo.rtpMenuInfo.lastUrl, rtp_info[sorted_streams[streamNumber]].url )) ||
				     (playlistModeIPTV != appControlInfo.playbackInfo.playlistMode &&
				      rtp.selectedDesc.connection.address.IPv4.s_addr == streams.items[sorted_streams[streamNumber]].connection.address.IPv4.s_addr &&
				      rtp.selectedDesc.media[0].port == streams.items[sorted_streams[streamNumber]].media[0].port) ) */
				{
					found = 1;
					if (first != streamNumber && direction != 0) // prev
					{
						new_index = sorted_streams[prev];
						break;
					}
				}
				prev = streamNumber;
			}
			if( new_index < 0 && found == 1 )
			{
				if( direction == 0 )
					new_index = sorted_streams[first];
				if( direction != 0 && prev != -1 )
					new_index = sorted_streams[prev];
			}
			if (new_index < 0 && first != prev) // we have at least 2 streams
				new_index = direction == 0 ? sorted_streams[first] : sorted_streams[prev];
		} else
		{
			for ( streamNumber = 0; streamNumber<streams.count; streamNumber++ )
			{
				if ( appControlInfo.rtpMenuInfo.usePlaylistURL != 0 || (streams.items[streamNumber].media[0].fmt != payloadTypeUnknown && rtp_engine_supports_transport(RTP_ENGINE, streams.items[streamNumber].media[0].proto)) )
				{
					if (first == -1)
					{
						first = streamNumber;
					}
					if (found == 1 && direction == 0) // next
					{
						new_index = streamNumber;
						break;
					}
					if ( strcmp( appControlInfo.rtpMenuInfo.lastUrl, rtp_info[streamNumber].url ) == 0 )
/*					    (playlistModeIPTV == appControlInfo.playbackInfo.playlistMode && 0 == strcmp( appControlInfo.rtpMenuInfo.lastUrl, rtp_info[streamNumber].url )) ||
					    (playlistModeIPTV != appControlInfo.playbackInfo.playlistMode &&
					     rtp.selectedDesc.connection.address.IPv4.s_addr == streams.items[streamNumber].connection.address.IPv4.s_addr &&
					     rtp.selectedDesc.media[0].port == streams.items[streamNumber].media[0].port))*/
					{
						found = 1;
						if (first != streamNumber && direction != 0) // prev
						{
							new_index = prev;
							break;
						}
					}
					prev = streamNumber;
				}
			}
			if (new_index < 0 && first != prev) // we have at least 2 streams
				new_index = direction == 0 ? first : prev;
		}
	}

	mysem_release(rtp_semaphore);

	if (new_index >= 0)
		rtp_stream_change(interfaceInfo.currentMenu, STREAM_INFO_SET(screenMain, new_index));

	return 0;
}

static int rtp_audioChange(void* pArg)
{
	int selected = GET_NUMBER(pArg);
	if( appControlInfo.rtpMenuInfo.channel != CHANNEL_CUSTOM )
	{
		rtp_info[appControlInfo.rtpMenuInfo.channel].audio = selected;
		rtp_saveAudioTrackList();
	}
	return 0;
}

int rtp_play_callback(interfacePlayControlButton_t button, void *pArg)
{
	dprintf("%s: %d\n", __FUNCTION__, button);

	switch( button )
	{
		case interfacePlayControlPrevious:
			rtp_startNextChannel(1, pArg);
			break;
		case interfacePlayControlNext:
			rtp_startNextChannel(0, pArg);
			break;
		case interfacePlayControlPlay:
			if ( !appControlInfo.rtpInfo.active )
				rtp_startVideo(screenMain);
			break;
		case interfacePlayControlStop:
			rtp_stopVideo(screenMain);
			break;
		case interfacePlayControlAddToPlaylist:
			playlist_addUrl(appControlInfo.rtpMenuInfo.lastUrl, rtp.selectedDesc.session_name);
			break;
		case interfacePlayControlMode:
			if(appControlInfo.rtpMenuInfo.usePlaylistURL != 0 && appControlInfo.rtpMenuInfo.epg[0] != 0 )
				rtp_showEPG(screenMain, rtp_setupPlayControl);
			break;
		default:
			return 1;
	}

	interface_displayMenu(1);

	dprintf("%s: done\n", __FUNCTION__);

	return 0;
}

#ifdef ENABLE_AUTOPLAY
static int rtp_stream_selected(interfaceMenu_t *pMenu, void* pArg)
{
	int streamNumber = STREAM_INFO_GET_STREAM(pArg);

	dprintf("%s: %d\n", __FUNCTION__, streamNumber);

	mysem_get(rtp_semaphore);

	dprintf("%s: in\n", __FUNCTION__);

	if ( /*memcmp(&rtp.streamdesc, &streams.items[streamNumber], sizeof(sdp_desc)) != 0 ||*/ appControlInfo.rtpInfo.active == 0 )
	{
		rtp.playingFlag = 0;

		interface_addEvent(rtp_stream_start, pArg, 3000, 1);
	}

	dprintf("%s: done\n", __FUNCTION__);

	mysem_release(rtp_semaphore);

	return 0;
}

static int rtp_stream_deselected(interfaceMenu_t *pMenu, void* pArg)
{
	//int streamNumber = STREAM_INFO_GET_STREAM(pArg);

	dprintf("%s: in\n", __FUNCTION__);

	interface_removeEvent(rtp_stream_start, pArg);

	if ( rtp.playingFlag == 0 )
	{
		interface_playControlDisable(1);
		rtp.selectedDesc.connection.address.IPv4.s_addr = INADDR_NONE;
		rtp_stopVideo(which);
	}

	dprintf("%s: done\n", __FUNCTION__);

	return 0;
}
#endif

static int rtp_stream_start(void* pArg)
{
	int streamNumber = STREAM_INFO_GET_STREAM(pArg);

	dprintf("%s: stream check\n", __FUNCTION__);

	//dprintf("%s: show menu %d, active %d\n", __FUNCTION__, interfaceInfo.showMenu, appControlInfo.rtpInfo.active);

	if ( /*interfaceInfo.showMenu &&*/ /*(memcmp(&rtp.streamdesc, &streams.items[streamNumber], sizeof(sdp_desc)) != 0 ||*/
	     appControlInfo.rtpInfo.active == 0 /*)*/
	   )
	{
		if (CHANNEL_CUSTOM != streamNumber)
		{
			memcpy(&rtp.selectedDesc, &streams.items[streamNumber], sizeof(sdp_desc));
			strcpy(appControlInfo.rtpMenuInfo.lastUrl, rtp_info[streamNumber].url );
		}

		//dprintf("%s: play control\n", __FUNCTION__);
		//interface_playControlSetInputFocus(inputFocusPlayControl);
		//interface_playControlSelect(interfacePlayControlStop);
		//interface_playControlHighlight(interfacePlayControlPlay);

		dprintf("%s: start video %d\n", __FUNCTION__, streamNumber);

		rtp_startVideo(screenMain);

		if( CHANNEL_CUSTOM != streamNumber && rtp_info[streamNumber].audio > 0 )
		{
			dprintf("%s: setting default audio %d for channel %d\n", __FUNCTION__, rtp_info[streamNumber].audio, streamNumber);
			gfx_setVideoProviderAudioStream(screenMain, rtp_info[streamNumber].audio);
		}

		//dprintf("%s: refill menu\n", __FUNCTION__);
		//rtp_fillStreamMenu(which);

		//if ( appControlInfo.rtpInfo.active != 0 )
		rtp_setupPlayControl(pArg);

		saveAppSettings();
		/*else
		{
			interface_playControlDisable(0);
			//rtp.selectedDesc.connection.address.IPv4.s_addr = INADDR_NONE;
			return -1;
		}*/
		if ( appControlInfo.rtpInfo.active == 0 )
		{
#ifndef RTP_RECONNECT
			interface_playControlSelect(interfacePlayControlStop);
			interface_showMenu(1, 1);
#endif
		}
	}

	helperFlushEvents();

	dprintf("%s: done\n", __FUNCTION__);

#ifdef RTP_RECONNECT
	return 0;
#else
	return appControlInfo.rtpInfo.active != 0 ? 0 : -1;
#endif
}

#ifdef RTP_RECONNECT
int rtp_reconnectEvent(void* pArg)
{
	rtp_stream_start(interfacePlayControl.pArg);
	return 0;
}
#endif

static int rtp_stream_change(interfaceMenu_t *pMenu, void* pArg)
{
	int streamNumber = STREAM_INFO_GET_STREAM(pArg);

	dprintf("%s: %d (0x%04x)\n", __FUNCTION__, streamNumber, streamNumber);

	rtp.playingFlag = 1;

#ifdef RTP_RECONNECT
	interface_removeEvent(rtp_reconnectEvent, NULL);
#endif
	interface_removeEvent(rtp_stream_start, pArg);

	if ( appControlInfo.rtpInfo.active != 0 )
	{
		/*interface_playControlDisable(1);
		rtp.streamDesc.connection.address.IPv4.s_addr = INADDR_NONE;*/
		// force showState to NOT be triggered
		
		interfacePlayControl.activeButton = interfacePlayControlStop;
		rtp_stopVideo(screenMain);
	}

	appControlInfo.rtpMenuInfo.channel = streamNumber;
	appControlInfo.playbackInfo.streamSource = streamSourceIPTV;
	if (CHANNEL_CUSTOM != streamNumber)
	{
		int i = interface_getSelectedItem(_M &rtpStreamMenu);
		if (i < 0 ||
		    rtpStreamMenu.baseMenu.menuEntry[i].pAction != rtp_stream_change ||
		    rtpStreamMenu.baseMenu.menuEntry[i].pArg != pArg)
		{
			for (i = 0; i < rtpStreamMenu.baseMenu.menuEntryCount; i++)
			{
				if (rtpStreamMenu.baseMenu.menuEntry[i].pAction == rtp_stream_change &&
				    rtpStreamMenu.baseMenu.menuEntry[i].pArg == pArg)
				{
					interface_setSelectedItem(_M &rtpStreamMenu, i);
					break;
				}
			}
		}
		appControlInfo.playbackInfo.channel = streamNumber+1;
		switch( streams.items[streamNumber].media[0].proto )
		{
			case mediaProtoRTP:
			case mediaProtoUDP:
				appControlInfo.playbackInfo.playlistMode = playlistModeNone;
				if( rtp_info[streamNumber].thumb )
					strcpy( appControlInfo.playbackInfo.thumbnail, rtp_info[streamNumber].thumb );
				break;
			case mediaProtoHTTP:
			case mediaProtoRTMP:
				{
					char desc[MENU_ENTRY_INFO_LENGTH];
					sprintf(desc, "%s: %s", _T("CHANNEL"), streams.items[streamNumber].session_name);
					appControlInfo.playbackInfo.playlistMode = playlistModeIPTV;
					return media_playURL(screenMain, rtp_info[streamNumber].url, desc, rtp_info[streamNumber].thumb ? rtp_info[streamNumber].thumb : resource_thumbnails[thumbnail_multicast]);
				}
			case mediaProtoRTSP:
				{
					char desc[MENU_ENTRY_INFO_LENGTH];
					sprintf(desc, "%s: %s", _T("CHANNEL"), streams.items[streamNumber].session_name);
					appControlInfo.playbackInfo.playlistMode = playlistModeIPTV;
					return rtsp_playURL(screenMain, rtp_info[streamNumber].url, desc, rtp_info[streamNumber].thumb ? rtp_info[streamNumber].thumb : resource_thumbnails[thumbnail_multicast]);
				}
			default:
				appControlInfo.playbackInfo.playlistMode = playlistModeIPTV;
		}
		rtp.stream_info.custom_url = 0;
	}

	if ( rtp_stream_start(pArg) == 0 )
	{
		interface_showMenu(0, 1);

		//interface_menuActionShowMenu(pMenu, &rtpMenu);

		return 0;
	}

	//rtp_startVideo(which);

	return -1;
}

static void rtp_setupPlayControl(void *pArg)
{
	int buttons;
	int streamNumber = STREAM_INFO_GET_STREAM(pArg);

	buttons = interfacePlayControlStop|interfacePlayControlPlay;
	buttons |= appControlInfo.playbackInfo.playlistMode != playlistModeFavorites ? interfacePlayControlAddToPlaylist :  interfacePlayControlMode;

	if(CHANNEL_CUSTOM != streamNumber)
	{
		if(appControlInfo.rtpMenuInfo.usePlaylistURL != 0 && appControlInfo.rtpMenuInfo.epg[0] != 0 && rtp_getProgramInfo( streamNumber, 0, rtpInfoTypeName ) == 0 && channel_buff[0] != 0 )
			strcpy(appControlInfo.playbackInfo.description, rtpEpgInfo.program.info);
		else
			sprintf(appControlInfo.playbackInfo.description, "%s: %s", _T("CHANNEL"), rtp.selectedDesc.session_name);
		if(rtp_info[streamNumber].thumb)
			strcpy(appControlInfo.playbackInfo.thumbnail, rtp_info[streamNumber].thumb);
		else
			appControlInfo.playbackInfo.thumbnail[0] = 0;
	}
	if ( CHANNEL_CUSTOM != streamNumber || rtp.stream_info.custom_url == 0 || appControlInfo.playbackInfo.playlistMode == playlistModeFavorites
#ifdef ENABLE_TELETES
	     || appControlInfo.playbackInfo.playlistMode == playlistModeTeletes
#endif
	)
	{
		buttons|= interfacePlayControlPrevious|interfacePlayControlNext;
	}
#ifdef ENABLE_PVR
	url_desc_t url;
	if ( parseURL( appControlInfo.rtpMenuInfo.lastUrl, &url ) == 0 )
	{
		switch( url.protocol )
		{
			case mediaProtoFTP:
			case mediaProtoHTTP:
			case mediaProtoUDP:
			case mediaProtoRTP:
				buttons|= interfacePlayControlRecord;
				break;
			default:;
		}
	} else
		url.protocol = mediaProtoUnknown;
#endif

	interface_playControlSetup(rtp_play_callback, pArg, buttons, appControlInfo.playbackInfo.description, thumbnail_multicast);
	interface_playControlSetAudioCallback( rtp_audioChange );

	if ( CHANNEL_CUSTOM != streamNumber || rtp.stream_info.custom_url == 0 || appControlInfo.playbackInfo.playlistMode == playlistModeFavorites)
		interface_playControlSetChannelCallbacks(rtp_startNextChannel,
			appControlInfo.playbackInfo.playlistMode == playlistModeFavorites ? playlist_setChannel : (appControlInfo.rtpMenuInfo.usePlaylistURL != 0 ? rtp_setChannel : NULL));
#ifdef ENABLE_TELETES
	if( appControlInfo.playbackInfo.playlistMode == playlistModeTeletes )
		interface_playControlSetChannelCallbacks(teletes_startNextChannel, NULL );
#endif
	if( appControlInfo.playbackInfo.channel >= 0 )
		interface_channelNumberShow(appControlInfo.playbackInfo.channel);
	interface_playControlSetProcessCommand(rtp_playControlProcessCommand);
}

int rtp_setChannel(int channel, void* pArg)
{
	if( channel < 1 || channel > streams.count )
		return 1;
	channel--;

#ifdef ENABLE_AUTOPLAY
	if( streams.items[channel].media[0].proto == mediaProtoRTP || streams.items[channel].media[0].proto == mediaProtoUDP )
		rtp_stream_selected((interfaceMenu_t*)&rtpStreamMenu, STREAM_INFO_SET(which, channel));
#endif
	rtp_stream_change((interfaceMenu_t*)&rtpStreamMenu, STREAM_INFO_SET(screenMain, channel));

	return 0;
}

static void rtp_urlHandler(void *pArg, const char *location, const char *desc, xmlConfigHandle_t track)
{
	url_desc_t url;
	in_addr_t url_ip;
	int ret;
	char *ptr;
	unsigned int  id = 0;
	unsigned char content = 0x0;

	if(streams.count >= RTP_MAX_STREAM_COUNT )
		return;

	if ((ret = parseURL(location, &url)) != 0)
	{
		eprintf("RTP: Invalid url '%s' in playlist (parse error %d)\n", location, ret);
		return;
	}

	rtp_info[streams.count].audio = 0;
	rtp_info[streams.count].audio_pid  = 0;
	rtp_info[streams.count].audio_type = 0;
	rtp_info[streams.count].video_pid  = 0;
	rtp_info[streams.count].video_type = 0;
	memset(&streams.items[streams.count], 0, sizeof(sdp_desc));
	switch(url.protocol)
	{
		case mediaProtoUDP:
		case mediaProtoRTP:
			streams.items[streams.count].media[0].fmt = payloadTypeMpegTS;
			streams.items[streams.count].media[0].type = mediaTypeVideo;
			inet_addr_prepare(url.address);
			url_ip = inet_addr(url.address);
			if (url_ip == INADDR_NONE || url_ip == INADDR_ANY)
			{
				eprintf("RTP: '%s' invalid IP in playlist\n", url.address);
				return;
			}
			ptr = strstr(url.stream,"manualDetectVideoInfo");
			if( ptr != NULL )
			{
				ptr = &ptr[21];
				while( *ptr )
				{
					switch( *ptr )
					{
						case 'v':
							if( strncasecmp( ptr, "videoType=", 10 ) == 0 )
							{
								ptr += 10;
								rtp_info[streams.count].video_type = (payload_type)atoi(ptr);
								ptr++;
							} else if( strncasecmp( ptr, "videoPid=", 9 ) == 0 )
							{
								ptr += 9;
								rtp_info[streams.count].video_pid = atoi(ptr);
								ptr++;
							} else
								ptr++;
							break;
						case 'a':
							if( strncasecmp( ptr, "audioType=", 10 ) == 0 )
							{
								ptr += 10;
								rtp_info[streams.count].audio_type = (payload_type)atoi(ptr);
								ptr++;
							} else if( strncasecmp( ptr, "audioPid=", 9 ) == 0 )
							{
								ptr += 9;
								rtp_info[streams.count].audio_pid = atoi(ptr);
								ptr++;
							} else
								ptr++;
							break;
						default:
							ptr++;
					}
				}
			}
			streams.items[streams.count].media[0].port = url.port;
			streams.items[streams.count].connection.addrtype = addrTypeIPv4;
			streams.items[streams.count].connection.address.IPv4.s_addr = url_ip;
		case mediaProtoHTTP:
		case mediaProtoRTMP:
		case mediaProtoRTSP:
			streams.items[streams.count].media[0].proto = url.protocol;
			helperSafeStrCpy( &rtp_info[streams.count].url, location );
			break;
		default:
			eprintf("RTP: '%s' unsupported proto %d in playlist!\n", location, url.protocol);
			return;
	}

	if( desc != NULL )
		strncpy(streams.items[streams.count].session_name, desc, sizeof(streams.items[streams.count].session_name)-1 );
	else
		streams.items[streams.count].session_name[0] = 0;

	if( track )
	{
		xmlConfigHandle_t meta;

		ptr = (char*)xmlConfigGetAttribute(track, "channel_id");
		if( ptr )
			id = strtoul(ptr, NULL, 10);

		// maxima
		ptr = (char*)xmlConfigGetText(track, "descr");
		if( ptr && *ptr )
		{
			//dprintf("%s: Adding '%s' description to %03d stream\n", __FUNCTION__, ptr, *pIndex);
			char *str = &streams.items[streams.count].session_name[strlen(streams.items[streams.count].session_name)];
			if( str != streams.items[streams.count].session_name )
				*str++ = '\n';
			strncpy(str, ptr, sizeof(streams.items[streams.count].session_name) - (str-streams.items[streams.count].session_name));
			streams.items[streams.count].session_name[sizeof(streams.items[streams.count].session_name)-1] = 0;
		}

		ptr = (char*)xmlConfigGetText(track, "thumb");
		if( ptr && *ptr )
		{
			helperSafeStrCpy( &rtp_info[streams.count].thumb, ptr );
		} else
		{
			FREE( rtp_info[streams.count].thumb );
		}

		ptr = (char*)xmlConfigGetText(track, "poster");
		if( ptr && *ptr )
		{
			helperSafeStrCpy( &rtp_info[streams.count].poster, ptr );
		} else
		{
			FREE( rtp_info[streams.count].poster );
		}

		for( meta = xmlConfigGetElement(track, "meta", 0); meta != NULL; meta = xmlConfigGetNextElement( meta, "meta" ) )
		{
			ptr = (char*)xmlConfigGetAttribute( meta, "rel" );
			if( ptr && strcasecmp(ptr, "genre") == 0 )
			{
				ptr = (char*)xmlConfigGetElementText(meta);
				if( ptr )
				{
					if( sscanf( ptr, "0x%hhX",   &content ) != 1 )
						sscanf( ptr, "\"%hhu\"", &content );
				}
				break;
			}
		}
	} else
	{
		FREE( rtp_info[streams.count].thumb );
		FREE( rtp_info[streams.count].poster );
	}

	if( id == 0 || id != rtp_info[streams.count].id )
	{
		mysem_get(rtp_epg_semaphore);
		dprintf("%s: free schedule %d\n", __FUNCTION__, streams.count);
		free_elements( &rtp_info[streams.count].schedule );
		mysem_release(rtp_epg_semaphore);
	}

	rtp_info[streams.count].id    = id;
	rtp_info[streams.count].genre = content;
	sorted_streams[streams.count] = streams.count;

	streams.count++;
}

static int rtp_sortByGenre()
{
	int i, index;
	int tmp[RTP_MAX_STREAM_COUNT];
	unsigned char genre;

	memset( genre_indeces, -1, sizeof(genre_indeces));
	memset( tmp, -1, sizeof(int) * streams.count );

	for( i = streams.count-1; i >= 0; i-- )
	{
		genre = (rtp_info[i].genre >> 4)&0x0F;
		tmp[i] = genre_indeces[genre];
		genre_indeces[genre] = i;
	}
	index = 0;
	for( genre = 0; genre < 0x10; genre++ )
	{
		i = genre_indeces[genre];
		if( i >= 0 )
		{
			genre_indeces[genre] = index;
			for( ; i >= 0; i = tmp[i] )
			{
				sorted_streams[index] = i;
				index++;
			}
		}
	}
	//genre_index = GENRE_ALL;
	rtp_fillGenreMenu();
	return 0;
}

static int stream_compare(const void *e1, const void *e2)
{
	const sdp_desc *s1 = e1, *s2 = e2;
	int res = ntohl(s1->connection.address.IPv4.s_addr) - ntohl(s2->connection.address.IPv4.s_addr);
	if (res) return res;
	return s1->media[0].port - s2->media[0].port;
}

static void *stream_list_updater(void *pArg)
{
	int i;
	int which;
	int sleepTime = 3;
	char url[MAX_URL];

	which = GET_NUMBER(pArg);
	
	rtp_sdp_start_collecting(rtp.rtp_session);

	while (rtp.collectFlag && streams.count <= 0 ) // collect SAP announces until we found something
	{
		interface_showLoadingAnimation();

		rtp_sdp_set_collecting_state(rtp.rtp_session, 1);

		dprintf("%s: collecting/waiting\n", __FUNCTION__);

		i = 0;
		while (i++ < sleepTime*10)
		{
			if (rtp.collectFlag)
			{
				usleep(100000);
			} else
			{
				dprintf("%s: stop waiting\n", __FUNCTION__);
				interface_hideLoadingAnimation();
				break;
			}
		}

		if (rtp.collectFlag)
		{
			mysem_get(rtp_semaphore);
			rtp_sdp_set_collecting_state(rtp.rtp_session, 0);
			//memcpy(&streams, rtp_sdp_set_collecting_state(rtp.rtp_session, 0), sizeof(struct rtp_list));
			rtp_get_found_streams(rtp.rtp_session, &streams);
			mysem_release(rtp_semaphore);

			dprintf("%s: found streams: %d\n", __FUNCTION__, streams.count);
			if (streams.count > 0)
			{
				rtp_sap_collected = 1;

				qsort(streams.items, streams.count, sizeof(streams.items[0]), stream_compare);

				mysem_get(rtp_epg_semaphore);
				for( i = 0; i < streams.count; i++ )
				{
					sorted_streams[i] = i;
					sprintf(url, "%s://%s:%d",
						proto_toa(streams.items[i].media[0].proto),
						inet_ntoa(streams.items[i].connection.address.IPv4),
						streams.items[i].media[0].port);
					helperSafeStrCpy( &rtp_info[i].url, url );
					rtp_info[i].audio = 0;
					rtp_info[i].video_pid = 0;
					rtp_info[i].video_type = 0;
					rtp_info[i].audio_pid = 0;
					rtp_info[i].audio_type = 0;
					rtp_info[i].id = 0;
					rtp_info[i].genre = 0;
					FREE(rtp_info[i].thumb);
					FREE(rtp_info[i].poster);
					//dprintf("%s: free schedule %d\n", __FUNCTION__, i);
					free_elements(&rtp_info[i].schedule);
				}
				mysem_release(rtp_epg_semaphore);
				rtp_loadAudioTrackList();
			}

			interface_hideLoadingAnimation();

			rtp_fillStreamMenu(which);

			sleepTime = 2;
		}
	}

	rtp_sdp_stop_collecting(rtp.rtp_session);

	dprintf("%s: exit normal\n", __FUNCTION__);

	return NULL;
}

static int rtp_stopCollect(interfaceMenu_t* pMenu, void *pArg)
{
	dprintf("%s: in\n", __FUNCTION__);

	rtp.collectFlag = 0;

	if (rtp.collectThread != 0)
	{
		pthread_join(rtp.collectThread, NULL);
		rtp.collectThread = 0;
	}

	dprintf("%s: out\n", __FUNCTION__);

	return 0;
}

void rtp_getPlaylist(int which)
{
#if (defined STB225)
	rtp_sap_collected = 0;
#endif
	if (appControlInfo.rtpMenuInfo.usePlaylistURL != 0)
	{
		if (rtp_sap_collected)
		{
			/* Streams were collected using SAP announces. List reset is needed */
			streams.count = 0;
			rtp_sap_collected = 0;
		}

		rtp.collectFlag = 1;

		if (streams.count == 0)
		{
			dprintf("%s: get playlist from %s\n", __FUNCTION__, appControlInfo.rtpMenuInfo.playlist);

			if (PLAYLIST_ERR_DOWNLOAD == playlist_getFromURL( appControlInfo.rtpMenuInfo.playlist, rtp_urlHandler, NULL))
			{
				interface_showMessageBox(_T("ERR_SERVICE_UNAVAILABLE"), thumbnail_error, 0);
			}

			if (streams.count > 0)
			{
				rtp_loadAudioTrackList();
				rtp_sortByGenre();
			}
		}
		if (appControlInfo.rtpMenuInfo.epg[0] != 0)
		{
			pthread_t *thread;

			dprintf("%s: Starting EPG thread\n", __FUNCTION__);
			CREATE_THREAD(thread, rtp_epgThread, NULL );
			if (thread == NULL)
				eprintf("%s: failed to start update EPG thread\n", __FUNCTION__);
		}
		rtp_fillStreamMenu(which);
	}
	if ( rtp_sap_collected == 0 &&
	    (appControlInfo.rtpMenuInfo.usePlaylistURL == 0 || streams.count == 0))
	{
		int ret;

		if (rtp.collectThread != 0)
		{
			rtp.collectFlag = 0;
			pthread_join(rtp.collectThread, NULL);
		}

		dprintf("%s: start thread\n", __FUNCTION__);

		rtp_cleanupPlaylist(which);

		rtp.collectFlag = 1;

		ret = pthread_create(&rtp.collectThread, NULL, stream_list_updater, SET_NUMBER(which));
		if (ret != 0)
			eprintf("%s: failed to create collect thread: %s\n", __FUNCTION__, strerror(errno));
	}
}

int rtp_getChannelNumber(const char *url)
{
	int i;
	url_desc_t src, cmp;
	int udp = strncasecmp( url, "udp", 3 ) == 0 || strncasecmp( url, "rtp", 3 ) == 0;
	if( udp )
	{
		if( parseURL( url, &src ) != 0 )
			udp = 0;
	}
	for( i = 0; i<streams.count; i++ )
	{
		if( udp )
		{
			// ignore fast PIDs and @ before address
			if( strncasecmp( rtp_info[i].url, url, 3 ) == 0)
			{
				if( parseURL( rtp_info[i].url, &cmp ) == 0 )
				{
					if( src.port == cmp.port && strcmp( src.address, cmp.address ) == 0 )
						return i;
				}
			}
		} else if( 0 == strcmp( url, rtp_info[i].url ))
			return i;
	}
	return CHANNEL_CUSTOM;
}

static int rtp_setChannelFromURL(interfaceMenu_t *pMenu, char *value, char *description, char *thumbnail, void* pArg)
{
	int res;
	int channelNumber = CHANNEL_CUSTOM;
	sdp_desc desc;
	url_desc_t url;
	int which = GET_NUMBER(pArg);

	dprintf("%s: url '%s' screen %d\n", __FUNCTION__, value, which);

	if (value == NULL)
		return 1;

	if (value != appControlInfo.rtpMenuInfo.lastUrl)
	strcpy(appControlInfo.rtpMenuInfo.lastUrl, value);

	if ((res = parseURL(value, &url)) != 0)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
		eprintf("RTP: Error %d parsing '%s'\n", res, value);
		return -1;
	}

	if (playlistModeFavorites != appControlInfo.playbackInfo.playlistMode)
	{
		channelNumber= rtp_getChannelNumber(appControlInfo.rtpMenuInfo.lastUrl);
	}

	if (CHANNEL_CUSTOM == channelNumber)
	{
		rtp.stream_info.custom_url = 1;
	
		switch (url.protocol)
		{
			case mediaProtoRTP:
			case mediaProtoUDP:
				if (!description)
					sprintf(appControlInfo.playbackInfo.description, "%s: %s", _T("CHANNEL"), value);
				else
					strcpy(appControlInfo.playbackInfo.description, description);
				if (!thumbnail)
					appControlInfo.playbackInfo.thumbnail[0] = 0;
				else
					strcpy(appControlInfo.playbackInfo.thumbnail, thumbnail);
				break;
			case mediaProtoHTTP:
			case mediaProtoRTMP:
				media_playURL(which, value, description, thumbnail);
				return 0;
			case mediaProtoRTSP:
				return rtsp_playURL(which, value, description, thumbnail);
			default:
				interface_showMessageBox(_T("ERR_INCORRECT_PROTO"), thumbnail_error, 0);
				eprintf("RTP: Incorrect proto %d parsing '%s'\n", url.protocol, value);
				return -1;
		}

		inet_addr_prepare(url.address);
		if (inet_addr(url.address) == INADDR_NONE || inet_addr(url.address) == INADDR_ANY)
		{
			interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
			eprintf("RTP: Incorrect IP parsing '%s'\n", url.protocol, value);
			return -1;
		}

		memset(&desc, 0, sizeof(sdp_desc));

		desc.media[0].fmt = payloadTypeMpegTS;
		desc.media[0].type = mediaTypeVideo;
		desc.connection.addrtype = addrTypeIPv4;
		desc.connection.address.IPv4.s_addr = inet_addr(url.address);

		desc.media[0].port = url.port;

		strcpy(desc.session_name, value);

		memcpy(&rtp.selectedDesc, &desc, sizeof(sdp_desc));
	}

	return rtp_stream_change(pMenu, STREAM_INFO_SET(which, channelNumber));
}

int rtp_setChannelFromUserURL(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	appControlInfo.playbackInfo.playlistMode = playlistModeNone;
	return rtp_setChannelFromURL(pMenu, value, NULL, NULL, pArg);
}

int rtp_playURL(int which, char *value, char *description, char *thumbnail)
{
	return rtp_setChannelFromURL(interfaceInfo.currentMenu, value, description, thumbnail, SET_NUMBER(which));
}

#ifndef ENABLE_EPG
static char *rtp_urlvalue(int num, void *pArg)
{
	return num == 0 ? appControlInfo.rtpMenuInfo.lastUrl : NULL;
}

static int rtp_enterURL(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_CUSTOM_URL"), "\\w+", rtp_setChannelFromUserURL, rtp_urlvalue, inputModeABC, pArg);

	return 0;
}
#endif

int rtp_initStreamMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int which = GET_NUMBER(pArg);

	dprintf("%s: in %d\n", __FUNCTION__, which);

	//dprintf("%s: stop collect\n", __FUNCTION__);
	//rtp_stopCollect(pMenu, pArg);

	rtp_getPlaylist(which);

	if( appControlInfo.playbackInfo.bAutoPlay &&
	    gfx_videoProviderIsActive( screenMain ) == 0 &&
	    appControlInfo.slideshowInfo.state == slideshowDisabled &&
	    appControlInfo.rtpMenuInfo.lastUrl[0] != 0 && appControlInfo.rtpMenuInfo.lastUrl[6] != 0 /* "rtp://" */ )
	{
		rtp_setChannelFromURL((interfaceMenu_t*)&interfaceMainMenu,appControlInfo.rtpMenuInfo.lastUrl, NULL, NULL, screenMain);
		interface_showMenu(1, 0);
	}

	interface_menuActionShowMenu(pMenu, &rtpStreamMenu);

	return 0;
}

static int rtp_changeGenre(interfaceMenu_t* pMenu, void *pArg)
{
	genre_index = GET_NUMBER(pArg) & 0x00FF;
	rtp_fillStreamMenu(screenMain);
	interface_menuActionShowMenu(pMenu, &rtpStreamMenu);
	return 0;
}

static int rtp_fillGenreMenu()
{
	unsigned int genre;
	int index = 0;
	char *str;

	interface_clearMenuEntries((interfaceMenu_t*)&rtpGenreMenu);

	str = _T("ALL");
	genre = GENRE_ALL;
	interface_addMenuEntry((interfaceMenu_t*)&rtpGenreMenu, str, rtp_changeGenre, SET_NUMBER(genre), -1);
	if( genre == genre_index )
	{
		interface_setSelectedItem((interfaceMenu_t*)&rtpGenreMenu, index);
	}
	index++;

	for( genre = 0x00; genre < GENRE_COUNT; genre++ )
	{
		if( genre_indeces[genre] >= 0 )
		{
			switch( genre )
			{
				case  1: str = _T("MOVIE_DRAMA"); break;
				case  2: str = _T("NEWS"); break;
				case  3: str = _T("SHOW_GAME"); break;
				case  4: str = _T("SPORTS"); break;
				case  5: str = _T("CHILDRENS"); break;
				case  6: str = _T("MUSIC"); break;
				case  7: str = _T("CULTURE"); break;
				case  8: str = _T("SOCIAL_POLITICS"); break;
				case  9: str = _T("EDUCATION"); break;
				case 10: str = _T("LEISURE"); break;
				case 11: str = _T("SPECIAL"); break;
				default: str = _T("NOT_AVAILABLE_SHORT");
			}
			interface_addMenuEntry((interfaceMenu_t*)&rtpGenreMenu, str, rtp_changeGenre, SET_NUMBER(genre), -1);
			if( genre == genre_index )
			{
				interface_setSelectedItem((interfaceMenu_t*)&rtpGenreMenu, index);
			}
			index++;
		}
	}
	return 0;
}

static int rtp_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int which = STREAM_INFO_GET_SCREEN(pArg);
	int streamNumber = interface_getSelectedItem(pMenu);

	dprintf("%s: cmd %d selected %d pArg 0x%08x (%d %d)\n", __FUNCTION__, cmd->command, streamNumber, pArg, which, STREAM_INFO_GET_STREAM(pArg));

	if( cmd->command == interfaceCommandRefresh )
	{
		rtp_cleanupPlaylist(screenMain);
		interface_displayMenu(1);

		rtp_initStreamMenu( pMenu, SET_NUMBER(screenMain) );
		return 0;
	}
	if( cmd->command == interfaceCommandRed && appControlInfo.rtpMenuInfo.usePlaylistURL != 0 )
	{
		if( genre_index == GENRE_COUNT )
			genre_index = GENRE_ALL;
		for( genre_index++; genre_index < GENRE_COUNT && genre_indeces[genre_index] < 0; genre_index++ );
		rtp_fillStreamMenu(screenMain);
		return 0;
	}

#ifdef ENABLE_PVR
	if( streamNumber < 0 )
	{
		switch(cmd->command)
		{
			case interfaceCommandRecord:
			case interfaceCommandBlue:
				pvr_initPvrMenu( pMenu, SET_NUMBER(pvrJobTypeRTP) );
				return 0;
			default:;
		}
	}
#endif

	/* Check pArg sense */
	if( streamNumber < 0 || which < 0 || which >= screenOutputs )
		return 1;

	if( interface_getMenuEntryAction(pMenu, streamNumber) != rtp_stream_change )
		return 1;

	streamNumber = STREAM_INFO_GET_STREAM(pArg);

	char URL[MAX_URL];
	switch( cmd->command )
	{
		case interfaceCommandBlue:
#ifdef ENABLE_EPG
		case interfaceCommandEpg:
			if( appControlInfo.rtpMenuInfo.epg[0] != 0 )
				rtp_initEpgMenu( pMenu, pArg );
			else
				interface_showMessageBox(_T("EPG_UNAVAILABLE"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
#else
			rtp_enterURL( pMenu, SET_NUMBER(which) );
#endif
			return 0;
		case interfaceCommandYellow:
			rtp_getURL(URL,which,streamNumber);
			eprintf("RTP: Add to playlist '%s'\n",URL);
			playlist_addUrl(URL, streams.items[streamNumber].session_name);
			return 0;
		case interfaceCommandInfo:
		case interfaceCommandGreen:
			rtp_getURL(URL,which,streamNumber);
			eprintf("RTP: Stream %03d: '%s'\n", streamNumber, URL);
			if( appControlInfo.rtpMenuInfo.usePlaylistURL && rtp_info[streamNumber].poster && rtp_info[streamNumber].poster[0] )
			{
				char *ptr = NULL;
				if( appControlInfo.rtpMenuInfo.epg[0] && rtp_getProgramInfo(streamNumber, 0, rtpInfoTypeFull) == 0 )
				{
					/* Skip first lines of description */
					ptr = index( rtpEpgInfo.program.info, '\n' );
					if( !ptr )
						ptr = rtpEpgInfo.program.info;
					else
					{
						char *str = index( ptr+1, '\n' );
						if (str)
							ptr = str+1;
						else
							ptr = ptr+1;
					}
				}
				interface_showPosterBox( ptr,
											streams.items[streamNumber].session_name,
											INTERFACE_BORDER_RED, INTERFACE_BORDER_GREEN, INTERFACE_BORDER_BLUE, INTERFACE_BORDER_ALPHA,
											thumbnail_multicast,
											rtp_info[streamNumber].poster, NULL, SET_NUMBER(streamNumber));
			} else
				interface_showMessageBox(URL, thumbnail_info, 0);
			return 0;
#if (defined ENABLE_PVR) && (defined STBPNX)
		case interfaceCommandRecord:
#if 1
			{
				size_t desc_len;
				char *str = index(streams.items[streamNumber].session_name, '\n');
				if( str )
					desc_len = str - streams.items[streamNumber].session_name;
				else
					desc_len = strlen(streams.items[streamNumber].session_name);
				memcpy( URL, streams.items[streamNumber].session_name, desc_len );
				URL[desc_len] = 0;

				if( pvr_manageRecord(which, rtp_info[streamNumber].url, URL ) != 0 )
					interface_showMessageBox(_T("ERR_INCORRECT_PROTO"), thumbnail_error, 0);
			}
#else
			if( appControlInfo.pvrInfo.rtp.desc.fmt != payloadTypeUnknown )
			{
				pvr_stopRecordingRTP(which);
			} else
			{
				pvr_record(which, rtp_info[streamNumber].url, streams.items[streamNumber].session_name );
			}
#endif // ENABLE_PVR && STBPNX
			return 0;
#endif
#ifdef ENABLE_MULTI_VIEW
		case interfaceCommandTV:
			if((strncmp( rtp_info[streamNumber].url, URL_UDP_MEDIA, sizeof(URL_UDP_MEDIA)-1 ) == 0 ||
				strncmp( rtp_info[streamNumber].url, URL_RTP_MEDIA, sizeof(URL_RTP_MEDIA)-1 ) == 0 ) )
				{
					rtp_multiviewPlay(pMenu, pArg);
				} else
					rtp_multiviewPlay(pMenu, CHANNEL_INFO_SET(which, CHANNEL_CUSTOM));
				return 0;
			
			break;
#endif
		default:;
	}

	return 1;
}

static size_t http_epg_callback(void *buffer, size_t size, size_t nmemb, void *userp)
{
	char *src = buffer, *dest = (char*)userp;
	size_t dest_len = strlen(dest);
	size_t src_len = size*nmemb;
	size_t i,j;

	if (dest_len+src_len >= DATA_BUFF_SIZE)
	{
		src_len = DATA_BUFF_SIZE-dest_len-1;
	}

	j = 0;
	for( i=0; i<src_len; i++ )
	{
		if( (unsigned char)src[i] >= (unsigned char)' ' || src[i] == '\n' ) // skip non-printable
			dest[dest_len + j++] = src[i];
	}
	dest[dest_len+j] = 0;
	return src_len;
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

static int rtp_getProgramInfo(int channel, int offset, rtpInfoType_t type)
{
	CURLcode ret;
	CURL *hnd;
	int code;
	char url[MAX_URL];
	static char errbuff[CURL_ERROR_SIZE];
	char *str, *ptr;
	int event_offset;
	list_element_t *element;
	EIT_event_t *event;
	time_t start_time, end_time;
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
	struct curl_slist  *headers = NULL;
	char header[128];
	char mac[128];
	FILE *file = NULL;
#endif

	switch( type )
	{
		case rtpInfoTypeShort:
		case rtpInfoTypeFull:
		case rtpInfoTypeName:
		case rtpInfoTypeList:
			sprintf(url, "%s?type=%d&channel=%u&offset=%d",appControlInfo.rtpMenuInfo.epg, (int)type, channel >= 0 && channel < RTP_MAX_STREAM_COUNT && rtp_info[channel].id > 0 ? rtp_info[channel].id : (unsigned int)channel, offset);
			break;
		default:
			eprintf("RTP: Unsupported info type %d\n",type);
			return -1;
	}
	channel_buff[0] = 0;

	mysem_get( rtp_curl_semaphore );
	hnd = curl_easy_init();
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
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
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, http_epg_callback);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, channel_buff);
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, errbuff);
	curl_easy_setopt(hnd, CURLOPT_CONNECTTIMEOUT, 5);
	curl_easy_setopt(hnd, CURLOPT_TIMEOUT, 15);
	curl_easy_setopt(hnd, CURLOPT_PROXY, "");
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
	if(headers != NULL)
		curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, headers);
#endif
	
	ret = curl_easy_perform(hnd);

	channel_buff[sizeof(channel_buff)-1] = 0;
	if (ret == 0)
	{
		curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &code);
		if (code != 200 )
		{
			ret = -1;
			channel_buff[0] = 0;
			rtpEpgInfo.program.title = channel_buff;
		} else
		{
			switch(type)
			{
				case rtpInfoTypeList:
					str = index( channel_buff, '\n' );
					if( str == NULL ) // no title
					{
						ret = -1;
						channel_buff[0] = 0;
						rtpEpgInfo.program.title = channel_buff;
						break;
					}
					*str = 0;
					str++;
					//if( *str == '\r' )
					//{
					//	*str = 0;
					//	str++;
					//}
					if(str[0] == 0 ) // empty program list
					{
						ret = -1;
						rtpEpgInfo.program.title = channel_buff;
						break;
					}
					mysem_get(rtp_epg_semaphore);
					dprintf("%s: free schedule for %d '%s'\n", __FUNCTION__, channel, streams.items[channel].session_name);
					free_elements(&rtp_info[channel].schedule);
					mysem_release(rtp_epg_semaphore);
					while( str[0] > 0 && str[0] != '\n' )
					{
						ptr = index( str, '\n' );
						if( !ptr || ptr[1] == 0 )
						{
							eprintf("%s: no description string, aborting\n", __FUNCTION__);
							break;
						}
						*ptr = 0;
						ptr++;
						//if( *ptr == '\r' )
						//{
						//	*ptr = 0;
						//	ptr++;
						//}
						if( sscanf(str, "%d %04d-%02d-%02d %d:%d %d:%d", &event_offset, &rtpEpgInfo.program.start_tm.tm_year, &rtpEpgInfo.program.start_tm.tm_mon, &rtpEpgInfo.program.start_tm.tm_mday, &rtpEpgInfo.program.start_tm.tm_hour, &rtpEpgInfo.program.start_tm.tm_min, &rtpEpgInfo.program.end_tm.tm_hour, &rtpEpgInfo.program.end_tm.tm_min) != 8 )
						{
							eprintf("%s: wrong formatted time string '%s'\n", __FUNCTION__, str);
							break;
						}
						str = ptr;
						ptr = index( str, '\n' );
						if( ptr )
						{
							*ptr = 0;
							ptr++;
						} else
							ptr = str + strlen(str);

						mysem_get(rtp_epg_semaphore);
						if (rtp_info[channel].schedule == NULL)
						{
							element = rtp_info[channel].schedule = allocate_element(sizeof(EIT_event_t));
						} else
						{
							element = append_new_element(rtp_info[channel].schedule, sizeof(EIT_event_t));
						}
						mysem_release(rtp_epg_semaphore);
						if( !element )
						{
							eprintf("%s: error: can't allocate new event %d '%s'\n", __FUNCTION__, event_offset, str);
							break;
						}
						event = (EIT_event_t*)element->data;

						rtpEpgInfo.program.start_tm.tm_year -= 1900;

						event->start_day   = rtpEpgInfo.program.start_tm.tm_mday;
						event->start_month = rtpEpgInfo.program.start_tm.tm_mon;
						event->start_year  = rtpEpgInfo.program.start_tm.tm_year;

						rtpEpgInfo.program.start_tm.tm_mon  --;

						rtpEpgInfo.program.end_tm.tm_mday  = rtpEpgInfo.program.start_tm.tm_mday;
						rtpEpgInfo.program.end_tm.tm_mon   = rtpEpgInfo.program.start_tm.tm_mon;
						rtpEpgInfo.program.end_tm.tm_year  = rtpEpgInfo.program.start_tm.tm_year;
						rtpEpgInfo.program.end_tm.tm_sec   = rtpEpgInfo.program.start_tm.tm_sec   = 0;
						rtpEpgInfo.program.end_tm.tm_isdst = rtpEpgInfo.program.start_tm.tm_isdst = -1;
						start_time = gmktime(&rtpEpgInfo.program.start_tm);
						//dprintf("%s: %s start %02d-%02d-%04d %02d:%02d %d\n\n", __FUNCTION__, str, rtpEpgInfo.program.start_tm.tm_mday, rtpEpgInfo.program.start_tm.tm_mon,rtpEpgInfo.program.start_tm.tm_year, rtpEpgInfo.program.start_tm.tm_hour, rtpEpgInfo.program.start_tm.tm_min, start_time);
						end_time   = gmktime(&rtpEpgInfo.program.end_tm);
						//dprintf("%s: %s end   %02d-%02d-%04d %02d:%02d %d\n\n", __FUNCTION__, str, rtpEpgInfo.program.end_tm.tm_mday, rtpEpgInfo.program.end_tm.tm_mon, rtpEpgInfo.program.end_tm.tm_year, rtpEpgInfo.program.end_tm.tm_hour, rtpEpgInfo.program.end_tm.tm_min, end_time);
						if( end_time <= start_time )
							end_time += 24*60*60;

						end_time -= start_time;
						event->start_time  = encode_bcd_time(&rtpEpgInfo.program.start_tm);
						event->duration    = encode_bcd_time(gmtime(&end_time));
						//dprintf("%s: %s duration %d %06x\n", __FUNCTION__, str, end_time/60, event->duration);
						event->event_id    = event_offset;
						strncpy((char*)event->description.event_name, str, sizeof(event->description.event_name) );
						event->description.event_name[sizeof(event->description.event_name)-1] = 0;
						dprintf("%s: adding event '%s' [%ld:%ld]\n", __FUNCTION__, event->description.event_name, start_time, end_time);

						str = ptr;
					}
					break;
				default:
					str = index( channel_buff, '\n' );
					if( str == NULL )
					{
						ret = -1;
						channel_buff[0] = 0;
						rtpEpgInfo.program.title = channel_buff;
					} else
					{
						*str = 0;
						if( sscanf(channel_buff, "%04d-%02d-%02d %d:%d %d:%d", &rtpEpgInfo.program.start_tm.tm_year, &rtpEpgInfo.program.start_tm.tm_mon, &rtpEpgInfo.program.start_tm.tm_mday, &rtpEpgInfo.program.start_tm.tm_hour, &rtpEpgInfo.program.start_tm.tm_min, &rtpEpgInfo.program.end_tm.tm_hour, &rtpEpgInfo.program.end_tm.tm_min) == 7 )
						{
							rtpEpgInfo.program.start_tm.tm_mon  --;
							rtpEpgInfo.program.start_tm.tm_year -= 1900;
							rtpEpgInfo.program.start_tm.tm_sec  = 0;
							start_time = gmktime(&rtpEpgInfo.program.start_tm);

							rtpEpgInfo.program.end_tm.tm_year = rtpEpgInfo.program.start_tm.tm_year;
							rtpEpgInfo.program.end_tm.tm_mon  = rtpEpgInfo.program.start_tm.tm_mon;
							rtpEpgInfo.program.end_tm.tm_mday = rtpEpgInfo.program.start_tm.tm_mday;
							rtpEpgInfo.program.end_tm.tm_sec  = 0;
							end_time = gmktime(&rtpEpgInfo.program.end_tm);
							if ( end_time < start_time )
							{
								end_time += 24*3600;
							}
							localtime_r(&start_time, &rtpEpgInfo.program.start_tm);
							localtime_r(&end_time, &rtpEpgInfo.program.end_tm);
							rtpEpgInfo.program.title = str+1;
							for( ; *rtpEpgInfo.program.title > 0 && *rtpEpgInfo.program.title <= ' '; rtpEpgInfo.program.title++ );
							str = index( rtpEpgInfo.program.title, '\n' );
							if( str != NULL )
							{
								*str = 0;
								str++;
								for( ; *str > 0 && *str <= ' '; str++ );
							} else
							{
								str = &rtpEpgInfo.program.title[strlen(rtpEpgInfo.program.title)];
							}
						} else
						{
							ret = -1;
							channel_buff[0] = 0;
							str = rtpEpgInfo.program.title = channel_buff;
						}
					}
			}
		}
	} else
	{
		eprintf("RTP: Failed to get info from '%s' with message:\n%s\n", url, errbuff);
		channel_buff[0] = 0;
	};
#ifdef ENABLE_PLAYLIST_HTTP_HEADER
	if( headers != NULL )
		curl_slist_free_all(headers);
#endif
	curl_easy_cleanup(hnd);

	if( ret == 0 )
	{
		// Remove channel info from session description if EPG is enabled
		ptr = index(streams.items[channel].session_name, '\n');
		if( ptr )
			*ptr = 0;
		sprintf(rtpEpgInfo.program.info, "%d: %s\n", channel+1, streams.items[channel].session_name);
		if( ptr )
			*ptr = '\n';
		strftime( &rtpEpgInfo.program.info[strlen(rtpEpgInfo.program.info)],11,"%H:%M", &rtpEpgInfo.program.start_tm);
		strftime( &rtpEpgInfo.program.info[strlen(rtpEpgInfo.program.info)],11,"-%H:%M", &rtpEpgInfo.program.end_tm);
		sprintf( &rtpEpgInfo.program.info[strlen(rtpEpgInfo.program.info)], " %s\n%s", rtpEpgInfo.program.title, str );
		for( ptr = &rtpEpgInfo.program.info[strlen(rtpEpgInfo.program.info)-1]; (unsigned char)(*ptr) <= (unsigned char)' '; *ptr-- = 0 ); // trim empty strings
	}
	mysem_release( rtp_curl_semaphore );

	//dprintf("rtpEpgInfo.program.title='%s' str='%s'\n",rtpEpgInfo.program.title, str);
	dprintf("%s: (%d,%d)=%d\n%s\n", __FUNCTION__,channel, program_offset,ret,rtpEpgInfo.program.info);

	return ret;
}

static void rtp_displayShortInfo()
{
	if ( !interfaceInfo.showMenu )
	{
		tprintf("--- %s ---\n", rtpEpgInfo.program.info);
		interface_displayTextBox(interfaceInfo.screenWidth/2, interfaceInfo.marginSize, rtpEpgInfo.program.info, resource_thumbnails[thumbnail_multicast], interfaceInfo.screenWidth - 2*interfaceInfo.marginSize, NULL, 0);
	}
}

int rtp_showEPG(int which, playControlSetupFunction pSetup)
{
	if( appControlInfo.rtpMenuInfo.channel < 0 || appControlInfo.rtpMenuInfo.channel == CHANNEL_CUSTOM || pSetup == NULL )
		return 1;

	program_offset = 0;
	if( rtp_getProgramInfo( appControlInfo.rtpMenuInfo.channel, program_offset, rtpInfoTypeShort ) == 0 && channel_buff[0] != 0 )
	{
		rtp_epgControl.pArg     = interfacePlayControl.pArg;
		rtp_epgControl.pSetup   = pSetup;

		interfacePlayControl.pArg = STREAM_INFO_SET(which, appControlInfo.rtpMenuInfo.channel);
		interface_playControlSetProcessCommand(rtp_shortInfoProcessCommand);
		interface_playControlSetDisplayFunction(rtp_displayShortInfo);
		interface_displayMenu(1);
		return 0;
	}
	interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
	return 1;
}

static int rtp_shortInfoProcessCommand(pinterfaceCommandEvent_t cmd, void* pArg)
{
	int channelNumber = STREAM_INFO_GET_STREAM(pArg);
	int which = STREAM_INFO_GET_SCREEN(pArg);

	switch(cmd->command)
	{
		case interfaceCommandLeft:
			if( program_offset > 0 )
			{
				if( rtp_getProgramInfo( channelNumber, program_offset-1, rtpInfoTypeShort ) == 0 && channel_buff[0] != 0 )
				{
					program_offset--;
					interface_playControlRefresh(1);
				} else
				{
					interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
					rtp_epgControl.pSetup( rtp_epgControl.pArg );
				}
			}
			return 0;
		case interfaceCommandRight:
			if( rtp_getProgramInfo( channelNumber, program_offset+1, rtpInfoTypeShort ) == 0 && channel_buff[0] != 0 )
			{
				program_offset++;
				interface_playControlRefresh(1);
			} else
			{
				interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
				rtp_epgControl.pSetup( rtp_epgControl.pArg );
			}
			return 0;
		case interfaceCommandPageUp:
			if( channelNumber > 0 )
			{
				if( rtp_getProgramInfo( channelNumber-1, 0, rtpInfoTypeShort ) == 0 && channel_buff[0] != 0 )
				{
					program_offset = 0;
					channelNumber--;
					interfacePlayControl.pArg = STREAM_INFO_SET(which, channelNumber);
					interface_playControlRefresh(1);
				} else
				{
					interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
				}
			}
			return 0;
		case interfaceCommandPageDown:
			if( channelNumber < streams.count -1 )
			{
				if( rtp_getProgramInfo( channelNumber+1, 0, rtpInfoTypeShort ) == 0 && channel_buff[0] != 0 )
				{
					program_offset = 0;
					channelNumber++;
					interfacePlayControl.pArg = STREAM_INFO_SET(which, channelNumber);
					interface_playControlRefresh(1);
				} else
				{
					interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
				}
			}
			return 0;
		default:
			rtp_epgControl.pSetup( rtp_epgControl.pArg );
			switch( cmd->command )
			{
				case interfaceCommandMainMenu:
					interface_showMenu(1, 1);
					return 0;
				case interfaceCommandBlue:
					if( appControlInfo.rtpMenuInfo.epg[0] != 0 )
					{
						if( rtp_getProgramInfo( channelNumber, program_offset, rtpInfoTypeFull ) == 0 )
						{
							interface_playControlHide(0);
							interface_showScrollingBox( rtpEpgInfo.program.info, thumbnail_multicast, rtp_longInfoCallback, pArg);
						} else
							interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
					} else
						interface_showMessageBox(_T("EPG_UNAVAILABLE"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
					return 0;
				case interfaceCommandEpg:
					rtp_showEpg();
					return 0;
				default:
					interface_playControlRefresh(1);
					return 0;
			}
	}
}

static int rtp_longInfoCallback(interfaceMenu_t* pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int channelNumber = STREAM_INFO_GET_STREAM(pArg);
	int which = STREAM_INFO_GET_SCREEN(pArg);

	switch( cmd->command )
	{
		case interfaceCommandLeft:
			if( program_offset > 0 )
			{
				if( rtp_getProgramInfo( channelNumber, program_offset-1, rtpInfoTypeFull ) == 0 )
				{
					program_offset--;
					interface_showScrollingBox( rtpEpgInfo.program.info, thumbnail_multicast, rtp_longInfoCallback, pArg);
				} else
				{
					interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
				}
			}
			return 0;
		case interfaceCommandRight:
			if( rtp_getProgramInfo( channelNumber, program_offset+1, rtpInfoTypeFull ) == 0 )
			{
				program_offset++;
				interface_showScrollingBox( rtpEpgInfo.program.info, thumbnail_multicast, rtp_longInfoCallback, pArg);
			} else
			{
				interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
			}
			return 0;
		case interfaceCommandPageUp:
			if( channelNumber > 0 )
			{
				if( rtp_getProgramInfo( channelNumber-1, 0, rtpInfoTypeFull ) == 0 && channel_buff[0] != 0 )
				{
					program_offset = 0;
					channelNumber--;
					interfacePlayControl.pArg = STREAM_INFO_SET(which, channelNumber);
					interface_showScrollingBox( rtpEpgInfo.program.info, thumbnail_multicast, rtp_longInfoCallback, STREAM_INFO_SET(which, channelNumber));
				} else
				{
					interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
				}
			}
			return 0;
		case interfaceCommandPageDown:
			if( channelNumber < streams.count -1 )
			{
				if( rtp_getProgramInfo( channelNumber+1, 0, rtpInfoTypeFull ) == 0 && channel_buff[0] != 0 )
				{
					program_offset = 0;
					channelNumber++;
					interfacePlayControl.pArg = STREAM_INFO_SET(which, channelNumber);
					interface_showScrollingBox( rtpEpgInfo.program.info, thumbnail_multicast, rtp_longInfoCallback, STREAM_INFO_SET(which, channelNumber));
				} else
				{
					interface_showMessageBox(_T("ERR_CONNECT"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
				}
			}
			return 0;
		default:
			// default action
			return 1;
	}
}

void rtp_showEpg(void)
{
	if (appControlInfo.rtpMenuInfo.channel != CHANNEL_CUSTOM && appControlInfo.rtpMenuInfo.epg[0] != 0)
	{
		if (rtp_initEpgMenu( _M &rtpEpgMenu, SET_NUMBER(appControlInfo.rtpMenuInfo.channel) ) == 0)
			interface_showMenu(1, 1);
	} else
		interface_showMessageBox(_T("EPG_UNAVAILABLE"), thumbnail_epg, RTP_EPG_ERROR_TIMEOUT);
}

int rtp_initEpgMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
	list_element_t *element;
	EIT_event_t *event;
	char text[MENU_ENTRY_INFO_LENGTH];
	time_t last_time = time(NULL)+24*60*60;
	time_t start_time;
	struct tm start_tm;
	char *str;

	if( rtp_info[channelNumber].schedule == NULL )
	{
		if( rtp_getProgramInfo( channelNumber, 0, rtpInfoTypeList ) != 0 || rtp_info[channelNumber].schedule == NULL )
		{
			interface_showMessageBox(_T("EPG_UNAVAILABLE"), thumbnail_epg, 3000);
			return 1;
		}
	}

	rtpEpgInfo.showMenuOnExit = interfaceInfo.showMenu;
	rtpEpgInfo.previousMenu   = interfaceInfo.currentMenu;

	interface_clearMenuEntries((interfaceMenu_t *)&rtpEpgMenu);

	strncpy(text, streams.items[channelNumber].session_name, sizeof(text));
	text[sizeof(text)-1] = 0;
	str = index(text, '\n');
	if( str )
		*str = 0;
	interface_setMenuName((interfaceMenu_t *)&rtpEpgMenu, text, strlen(text)+1 );

	mysem_get(rtp_epg_semaphore);
	for( element = rtp_info[channelNumber].schedule; element != NULL; element=element->next )
	{
		event = (EIT_event_t*)element->data;
		offair_getLocalEventTime( event, &start_tm, &start_time);
		if( start_time < last_time )
		{
			strftime( text, 25, _T("DATESTAMP"), &start_tm);
			interface_addMenuEntryDisabled((interfaceMenu_t *)&rtpEpgMenu, text, -1);
			last_time = start_time;
		}

		strftime( text, 25, _T("%H:%M "), &start_tm);

		strncpy(&text[strlen(text)], (char*)event->description.event_name, sizeof(text)-6);
		text[sizeof(text)-1] = 0;
		interface_addMenuEntryCustom( (interfaceMenu_t *)&rtpEpgMenu, interfaceMenuEntryText, text, strlen(text)+1, 1, NULL, NULL, NULL,
#if (defined ENABLE_PVR) && (defined STBPNX)
			rtp_epgEntryDisplay,
#else
			NULL,
#endif
			CHANNEL_INFO_SET(event->event_id,channelNumber), -1 );
	}
	mysem_release(rtp_epg_semaphore);

	return interface_menuActionShowMenu(pMenu, &rtpEpgMenu );
}

#ifdef ENABLE_PVR
#ifdef STBPNX
static EIT_event_t* rtp_findEvent( int channelNumber, unsigned int event_id )
{
	list_element_t *element;
	EIT_event_t *event = NULL;

	mysem_get(rtp_epg_semaphore);
	for( element = rtp_info[channelNumber].schedule; element != NULL; element = element->next )
	{
		event = (EIT_event_t*)element->data;
		if(event->event_id == event_id)
		{
			mysem_release(rtp_epg_semaphore);
			return event;
		}
	}
	mysem_release(rtp_epg_semaphore);
	return NULL;
}

static int rtp_epgEntryDisplay(interfaceMenu_t *pMenu, DFBRectangle *rect, int i)
{
	pvrJob_t *job;
	list_element_t *element;
	EIT_event_t *event = NULL;
	time_t event_start;
	time_t event_end;
	int channelNumber = CHANNEL_INFO_GET_CHANNEL( (int)rtpEpgMenu.baseMenu.menuEntry[i].pArg );
	unsigned int event_id = CHANNEL_INFO_GET_SCREEN( (int)rtpEpgMenu.baseMenu.menuEntry[i].pArg );

	event = rtp_findEvent(channelNumber, event_id);
	if( !event )
		goto finish;

	offair_getLocalEventTime( event, NULL, &event_start );
	event_end = event_start + offair_getEventDuration( event );

	for( element = pvr_jobs; element != NULL; element = element->next )
	{
		job = (pvrJob_t*)element->data;
		if( job->start_time >= event_end )
		{
			//eprintf("%s: no jobs before %d\n", __FUNCTION__, event_end);
			break;
		}
		if( job->type != pvrJobTypeDVB && job->start_time == event_start && job->end_time == event_end )
		{
			gfx_drawRectangle( DRAWING_SURFACE, 255, 0, 0, interface_colors[interfaceInfo.highlightColor].A, rect->x, rect->y, rect->w, rect->h );
			break;
		}
	}
finish:
	return interface_menuEntryDisplay(pMenu, rect, i);
}

int rtp_recordNow()
{
	if ( appControlInfo.rtpMenuInfo.channel != CHANNEL_CUSTOM )
	{
		char desc[MENU_ENTRY_INFO_LENGTH], *str;
		strcpy(desc, streams.items[appControlInfo.rtpMenuInfo.channel].session_name);
		str = index(desc, '\n');
		if( str )
			*str = 0;
		return pvr_record( screenMain, rtp_info[appControlInfo.rtpMenuInfo.channel].url, desc );
	} else
	{
		return pvr_record( screenMain, appControlInfo.rtpMenuInfo.lastUrl, appControlInfo.playbackInfo.description );
	}
}
#endif //STBPNX
#endif //ENABLE_PVR

static int rtp_epgKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	dprintf("%s: in %s %d\n", __FUNCTION__, interface_commandName(cmd->command), cmd->command);

	switch(cmd->command)
	{
		case interfaceCommandEpg:
			interface_switchMenu(pMenu, rtpEpgInfo.previousMenu);
			if (rtpEpgInfo.showMenuOnExit)
				interface_showMenu(1, 1);
			else
				interface_showMenu(0, 1);
			return 0;
#ifdef ENABLE_PVR
#ifdef STBPNX
		case interfaceCommandRecord:
			if( rtpEpgMenu.baseMenu.selectedItem < 0 )
				return 1;

		{
			list_element_t *element;
			EIT_event_t *event = NULL;
			int channelNumber;
			unsigned int event_id;
			pvrJob_t new_job;
			url_desc_t url;
			char buf[BUFFER_SIZE];

			channelNumber = CHANNEL_INFO_GET_CHANNEL( rtpEpgMenu.baseMenu.menuEntry[rtpEpgMenu.baseMenu.selectedItem].pArg );
			event_id      = CHANNEL_INFO_GET_SCREEN(  rtpEpgMenu.baseMenu.menuEntry[rtpEpgMenu.baseMenu.selectedItem].pArg );

			event = rtp_findEvent(channelNumber, event_id);
			if( !event )
			{
				eprintf("%s: event %hu not found in '%s'\n", __FUNCTION__, event_id, streams.items[channelNumber].session_name);
				return 0;
			}

			/* Init job from event */
			if( strncmp( rtp_info[channelNumber].url, "http://", 7 ) == 0  ||
			    strncmp( rtp_info[channelNumber].url, "https://", 8 ) == 0 ||
			    strncmp( rtp_info[channelNumber].url, "ftp://", 6 ) == 0 )
			{
				new_job.type = pvrJobTypeHTTP;
				new_job.info.http.url = rtp_info[channelNumber].url;
				strncpy( new_job.info.http.session_name, (char*)event->description.event_name, sizeof(new_job.info.http.session_name) );
				new_job.info.http.session_name[sizeof(new_job.info.http.session_name)-1] = 0;
			} else
			{
				parseURL( rtp_info[channelNumber].url, &url );
				switch( url.protocol )
				{
					case mediaProtoRTP:
					case mediaProtoUDP:
						new_job.type = pvrJobTypeRTP;
						strncpy( new_job.info.rtp.session_name, (char*)event->description.event_name, sizeof(new_job.info.rtp.session_name) );
						//strncpy( new_job.info.rtp.session_name, streams.items[channelNumber].session_name, sizeof(new_job.info.rtp.session_name) );
						new_job.info.rtp.session_name[sizeof(new_job.info.rtp.session_name)-1] = 0;
						new_job.info.rtp.desc.fmt   = payloadTypeMpegTS;
						new_job.info.rtp.desc.type  = mediaTypeVideo;
						new_job.info.rtp.desc.port  = url.port;
						new_job.info.rtp.desc.proto = url.protocol;
						new_job.info.rtp.ip.s_addr  = inet_addr(url.address);
						break;
					default:
						eprintf("%s: unsupported proto %d\n", __FUNCTION__, url.protocol);
						return 0;
				}
			}
			offair_getLocalEventTime( event, NULL, &new_job.start_time );
			new_job.end_time = new_job.start_time + offair_getEventDuration(event);
			pvr_jobprint( buf, sizeof(buf), &new_job );

			switch( pvr_findOrInsertJob( &new_job, &element) )
			{
			case PVR_RES_MATCH:
				eprintf("RTP: deleting job %s\n", buf);
				if( pvr_deleteJob(element) != 0 )
					interface_showMessageBox( _T("ERROR_SAVE_RECORD_LIST") , thumbnail_error, 0 );
				else if( pvr_exportJobList() != 0 )
					interface_showMessageBox( _T("ERROR_SAVE_RECORD_LIST") , thumbnail_error, 0 );
				break;
			case PVR_RES_COLLISION:
				interface_showMessageBox( _T("RECORD_JOB_COLLISION") , thumbnail_info, 0 );
				eprintf("RTP: failed to add job %s: collision with existing job\n", buf);
				return 0;
			case PVR_RES_ADDED:
				eprintf("RTP: added job %s\n", buf);
				if( pvr_exportJobList() != 0 )
				{
					interface_showMessageBox( _T("ERROR_SAVE_RECORD_LIST") , thumbnail_error, 0 );
				}
				break;
			default:
				eprintf("%s: pvr_findOrInsertJob %s failed\n", __FUNCTION__, buf);
				//case 1: interface_showMessageBox( _T("RECORD_JOB_INVALID_SERVICE") , thumbnail_info, 0 );
				return 0;
			}
			interface_displayMenu(1);
		}
			return 0;
#endif // STBPNX
#endif // ENABLE_PVR
		default:
			return 1;
	}
	return 1;
}

static int rtp_saveAudioTrackList()
{
	char buf[24];
	int  i;
	unlink(RTP_AUDIO_TRACL_LIST);
	if( m3u_createFile(RTP_AUDIO_TRACL_LIST) != 0 )
		return -1;
	for( i = 0; i < streams.count; i++ )
	{
		if( rtp_info[i].audio > 0 )
		{
			sprintf(buf, "%u", rtp_info[i].audio);
			if( m3u_addEntry(RTP_AUDIO_TRACL_LIST, rtp_info[i].url, buf) != 0 )
				return -2;
		}
	}
	return 0;
}

static int rtp_loadAudioTrackList()
{
	FILE *f;
	int i;
	f = m3u_initFile(RTP_AUDIO_TRACL_LIST, "r");
	if( !f )
		return -1;
	while( m3u_readEntry(f) == 0 )
	{
		for( i = 0; i < streams.count; i++ )
		{
			if( strcmp( m3u_url, rtp_info[i].url ) == 0 )
			{
				rtp_info[i].audio = strtol(m3u_description, NULL, 10);
				break;
			}
		}
	}
	return 0;
}

int rtp_playControlProcessCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: in %s (%d)\n", __FUNCTION__, interface_commandName(cmd->command), cmd->command);

	switch (cmd->command)
	{
	case interfaceCommandEpg:
		rtp_showEpg();
		return 0;
#ifdef ENABLE_MULTI_VIEW
	case interfaceCommandTV:
		if(appControlInfo.rtpMenuInfo.channel != CHANNEL_CUSTOM)
		{
			if( strncmp( rtp_info[appControlInfo.rtpMenuInfo.channel].url, URL_RTP_MEDIA, sizeof(URL_RTP_MEDIA)-1 ) == 0 ||
			    strncmp( rtp_info[appControlInfo.rtpMenuInfo.channel].url, URL_UDP_MEDIA, sizeof(URL_UDP_MEDIA)-1 ) == 0)
			{
				rtp_multiviewPlay( (interfaceMenu_t*)&rtpStreamMenu, CHANNEL_INFO_SET(screenMain, appControlInfo.rtpMenuInfo.channel) );
			} else
				rtp_multiviewPlay( (interfaceMenu_t*)&rtpStreamMenu, CHANNEL_INFO_SET(screenMain, CHANNEL_CUSTOM) );
			return 0;
		}
		break;
#endif
	default:; // fall through
	}
	return 1;
}

#ifdef ENABLE_MULTI_VIEW
static int rtp_multiviewNext(int direction, void* pArg)
{
	int indexdiff = direction == 0 ? 1 : -1;
	int channelIndex = CHANNEL_INFO_GET_CHANNEL((int)pArg);
	int which = CHANNEL_INFO_GET_SCREEN((int)pArg);
	int newIndex = channelIndex;
	int i;
	for( i = 0; i < (indexdiff > 0 ? appControlInfo.multiviewInfo.count : 4); i++)
	{
		do
		{
			newIndex = (newIndex + streams.count + indexdiff) % streams.count;
		} while( newIndex != channelIndex &&
		         strncmp( rtp_info[newIndex].url, URL_RTP_MEDIA, sizeof(URL_RTP_MEDIA)-1 ) != 0 &&
		         strncmp( rtp_info[newIndex].url, URL_UDP_MEDIA, sizeof(URL_UDP_MEDIA)-1 ) != 0 );
		if( newIndex == channelIndex )
		{
			return -1;
		}
	}

	return rtp_multiviewPlay( interfaceInfo.currentMenu, CHANNEL_INFO_SET(which, newIndex) );
}

static int rtp_multi_callback(pinterfaceCommandEvent_t cmd, void* pArg)
{
	dprintf("%s: in %d\n", __FUNCTION__, cmd->command);

	switch( cmd->command )
	{
		case interfaceCommandStop:
			rtp_stopVideo(screenMain);
			interface_showMenu(1, 1);
			return 0;
		case interfaceCommandOk:
		case interfaceCommandEnter:
			rtp_stream_change( (interfaceMenu_t*)&rtpStreamMenu, appControlInfo.multiviewInfo.pArg[appControlInfo.multiviewInfo.selected] );
			return 0;
		default:
			return interface_multiviewProcessCommand(cmd,pArg);
	}
	return 1;
}

static void rtp_displayMultiviewControl()
{
	int x, y, fa;
	char title[MENU_ENTRY_INFO_LENGTH], *str;
	DFBRectangle rectangle;

	if( appControlInfo.multiviewInfo.count <= 0 || interfaceInfo.showMenu )
		return;

	switch( appControlInfo.multiviewInfo.selected )
	{
		case 1:  x = interfaceInfo.clientX + 3*interfaceInfo.clientWidth/4; y = interfaceInfo.clientY; break;
		case 2:  x = interfaceInfo.clientX +   interfaceInfo.clientWidth/4; y = interfaceInfo.clientY + interfaceInfo.clientHeight/2; break;
		case 3:  x = interfaceInfo.clientX + 3*interfaceInfo.clientWidth/4; y = interfaceInfo.clientY + interfaceInfo.clientHeight/2; break;
		default: x = interfaceInfo.clientX +   interfaceInfo.clientWidth/4; y = interfaceInfo.clientY;
	}
	strcpy( title, streams.items[ CHANNEL_INFO_GET_CHANNEL((int)appControlInfo.multiviewInfo.pArg[appControlInfo.multiviewInfo.selected]) ].session_name );
	str = index( title, '\n' );
	if( str )
		*str = 0;
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, title, -1, &rectangle, NULL) );
	pgfx_font->GetAscender( pgfx_font, &fa);

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x-rectangle.w/2, y, rectangle.w, INTERFACE_ROUND_CORNER_RADIUS*2);

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
	DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
		x-rectangle.w/2-INTERFACE_ROUND_CORNER_RADIUS,
		y,
		INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
		x+rectangle.w/2,
		y,
		INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
		x+rectangle.w/2,
		y+INTERFACE_ROUND_CORNER_RADIUS,
		INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
		x-rectangle.w/2-INTERFACE_ROUND_CORNER_RADIUS,
		y+INTERFACE_ROUND_CORNER_RADIUS,
		INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignLeft|interfaceAlignTop);

	gfx_drawText( DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x-rectangle.w/2, y+INTERFACE_ROUND_CORNER_RADIUS+(rectangle.h-fa), title, 0, 0);
}

static int rtp_multiviewPlay(interfaceMenu_t *pMenu, void *pArg)
{
	int which = STREAM_INFO_GET_SCREEN(pArg);
	int streamNumber = STREAM_INFO_GET_STREAM(pArg);
	int ret, i, mvCount = 0;
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str, *src, *dest;

	if( streamNumber != CHANNEL_CUSTOM )
	{
		appControlInfo.multiviewInfo.source = streamSourceIPTV;
		appControlInfo.multiviewInfo.pArg[0] = pArg;
		mvCount = 1;
	} else
		streamNumber = -1;

	for( i = streamNumber+1; mvCount<4 && i<streams.count; i++ )
	{
		if( strncmp( rtp_info[i].url, URL_RTP_MEDIA, sizeof(URL_RTP_MEDIA)-1 ) == 0 ||
		    strncmp( rtp_info[i].url, URL_UDP_MEDIA, sizeof(URL_UDP_MEDIA)-1 ) == 0 )
		{
			appControlInfo.multiviewInfo.pArg[mvCount++] = CHANNEL_INFO_SET(which, i);
		}
	}
	for( i = 0; mvCount < 4 && i<streamNumber; i++ )
	{
		if( strncmp( rtp_info[i].url, URL_RTP_MEDIA, sizeof(URL_RTP_MEDIA)-1 ) == 0 ||
		    strncmp( rtp_info[i].url, URL_UDP_MEDIA, sizeof(URL_UDP_MEDIA)-1 ) == 0 )
		{
			appControlInfo.multiviewInfo.pArg[mvCount++] = CHANNEL_INFO_SET(which, i);
		}
	}
	if( mvCount == 0 )
	{
		eprintf("%s: no RTP/UDP streams to multi view\n", __FUNCTION__);
		return -1;
	}

	appControlInfo.multiviewInfo.selected = 0;
	src = rtp_info[ CHANNEL_INFO_GET_CHANNEL((int)appControlInfo.multiviewInfo.pArg[0]) ].url;
	str = index( &src[7], '/' );
	if( str )
		*str = 0;
	sprintf( buf, "multi:%s", src );
	if( str )
		*str = '/';
	dest = &buf[strlen(buf)];
	for( i = 1; i < mvCount; i++ )
	{
		*dest++ = '-';
		src = rtp_info[ CHANNEL_INFO_GET_CHANNEL((int)appControlInfo.multiviewInfo.pArg[i]) ].url;
		str = index( &src[7], '/' );
		if( str )
			*str = 0;
		strcpy( dest, src );
		if( str )
			*str = '/';
		dest = &dest[strlen(dest)];
	}

	gfx_stopVideoProviders(screenMain);
	media_slideshowStop(1);

	eprintf("%s: playing '%s'\n", __FUNCTION__, buf);

	appControlInfo.multiviewInfo.count = mvCount;
	ret = gfx_startVideoProvider(buf, which, 0, "");

	if (ret != 0)
	{
		eprintf("%s: Failed to start video provider '%s'\n", __FUNCTION__, buf);
		interface_showMessageBox(_T("ERR_VIDEO_PROVIDER"), thumbnail_error, 0);
		return 1;
	}

	appControlInfo.rtpInfo.active = 1;
	appControlInfo.rtpMenuInfo.channel = CHANNEL_CUSTOM;

	interface_playControlSetup(NULL, appControlInfo.multiviewInfo.pArg[0], 0, NULL, thumbnail_channels);
	interface_playControlSetDisplayFunction(rtp_displayMultiviewControl);
	interface_playControlSetProcessCommand(rtp_multi_callback);
	interface_playControlSetChannelCallbacks(rtp_multiviewNext, appControlInfo.rtpMenuInfo.usePlaylistURL != 0 ? rtp_setChannel : NULL);
	interface_playControlSelect(interfacePlayControlPlay);
	interface_playControlRefresh(0);
	interface_showMenu(0, 1);

	return 0;
}
#endif

static void* rtp_epgThread(void *pArg)
{
	unsigned int i;
	int res;

	dprintf("%s: updating EPG\n", __FUNCTION__);

	for( i = 0; i < (unsigned)streams.count; i++ )
	{
		if ( rtp.collectFlag == 0 )
		{
			eprintf("%s: aborted\n", __FUNCTION__);
			break;
		}
		if( rtp_info[i].schedule == NULL )
		{
			res = rtp_getProgramInfo( i, 0, rtpInfoTypeList );
			dprintf("%s: got schedule for %d (id %d) : %d\n", __FUNCTION__, i, rtp_info[i].id, res);
			(void)res;
		}
	}
	dprintf("%s: finished on %d of %d\n", __FUNCTION__, i, streams.count);

	pthread_exit(NULL);
}

void rtp_cleanupEPG()
{
	int which;
	rtp.collectFlag = 0;

	mysem_get(rtp_epg_semaphore);
	for ( which=0; which<RTP_MAX_STREAM_COUNT; which++ )
	{
		FREE( rtp_info[which].url );
		FREE( rtp_info[which].thumb );
		FREE( rtp_info[which].poster );
		//dprintf("%s: free schedule %d\n", __FUNCTION__, which);
		free_elements( &rtp_info[which].schedule );
	}
	mysem_release(rtp_epg_semaphore);
}

void rtp_cleanupPlaylist(int which)
{
	rtp_cleanupEPG();
	streams.count = 0;
	rtp_sap_collected = 0;
	appControlInfo.rtpMenuInfo.channel = CHANNEL_CUSTOM;

	interface_clearMenuEntries((interfaceMenu_t*)&rtpStreamMenu);
	interface_addMenuEntryDisabled((interfaceMenu_t*)&rtpStreamMenu, _T("SEARCHING_CHANNELS"), thumbnail_search);
	interface_setSelectedItem((interfaceMenu_t*)&rtpStreamMenu, MENU_ITEM_MAIN);
}
