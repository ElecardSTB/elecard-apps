
/*
 pvr.c

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

#include "pvr.h"

#ifdef ENABLE_PVR

#include "debug.h"
#include "app_info.h"
#include "StbMainApp.h"
#include "l10n.h"
#include "client.h"
#include "gfx.h"
#include "off_air.h"
#include "rtp.h"
#include "media.h"
#include "menu_app.h"
#include "stsdk.h"

// NETLib
#include <tools.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

/** @def PVR_CAN_EDIT_SOURCE If defined, user can change source (DVB channel, url) of recording job
 */
//#define PVR_CAN_EDIT_SOURCE

#define PVR_SLEEP_MS (1000)

#define PVR_LOCATION_UPDATE_INTERVAL (3000)

#define PVR_JOB_NEW                 (0xFFFF)
#define PVR_INFO_SET(type, index) ((type << 16) | (index))
#define PVR_INFO_GET_TYPE(info)     ((pvrJobType_t)(((int)info) >> 16))
#define PVR_INFO_GET_INDEX(info)  (((int)info) & 0xFFFF)

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

typedef struct
{
	interfaceMenu_t baseMenu;
	int channel_w;
	int event_w;
	int date_w;
	int start_w;
	int end_w;
} pvrManageMenu_t;

typedef struct
{
	pvrJob_t job;
	struct tm start_tm;
	struct tm end_tm;
	interfaceEditEntry_t date_entry;
	interfaceEditEntry_t start_entry;
	interfaceEditEntry_t duration_entry;
	int dirty;
	char url[MAX_URL];
} pvrEditInfo_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

#ifdef STBPNX
static int pvr_jobcmp(pvrJob_t *x, pvrJob_t *y);
static void* pvr_jobcpy(pvrJob_t *dest, pvrJob_t *src);
static char* pvr_jobGetName(pvrJob_t *job);
#endif

static int pvr_fillPvrMenu(interfaceMenu_t *pMenu, void *pArg);

static int pvr_checkLocations(void *pArg);
static int pvr_fillLocationMenu(interfaceMenu_t *pMenu, void* pArg);
static int pvr_leavingLocationMenu(interfaceMenu_t *pMenu, void* pArg);
static int pvr_browseRecords( interfaceMenu_t *pMenu, void* pArg );

#ifdef STBPNX
static int pvr_fillManageMenu(interfaceMenu_t *pMenu, void* pArg);
static int pvr_clearManageMenu(interfaceMenu_t *pMenu, void* pArg);
static void *pvrThread(void *pArg);
static int pvr_manageKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int pvr_initEditMenu(interfaceMenu_t *pMenu, void* pArg);
static int pvr_fillEditMenu(interfaceMenu_t *pMenu, void* pArg);
static int pvr_editKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int pvr_saveJobEntry(interfaceMenu_t *pMenu, void* pArg);
static int pvr_openRecord( interfaceMenu_t *pMenu, void* pArg );

static char* pvr_getRTPChannel(int field, void *pArg);
static char* pvr_getHTTPURL(int field, void *pArg);
static int pvr_editRTPName(interfaceMenu_t *pMenu, void* pArg);

#ifdef PVR_CAN_EDIT_SOURCE
static int pvr_editRTPChannel(interfaceMenu_t *pMenu, void* pArg);
static int pvr_editHTTPURL(interfaceMenu_t *pMenu, void* pArg);
static int pvr_showHTTPURL(interfaceMenu_t *pMenu, void* pArg);
#ifdef ENABLE_DVB
static char* pvr_getDVBChannel(int field, void *pArg);
static int pvr_editDVBChannel(interfaceMenu_t *pMenu, void* pArg);
#endif
#endif
static int pvr_editHTTPName(interfaceMenu_t *pMenu, void* pArg);
static int pvr_confirmStopPvr(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int pvr_defaultActionEvent(void *pArg);
static int pvr_setDate(interfaceMenu_t *pMenu, void* pArg);
static int pvr_setStart(interfaceMenu_t *pMenu, void* pArg);
static int pvr_setDuration(interfaceMenu_t *pMenu, void* pArg);
static int pvr_resetEditMenu(interfaceMenu_t *pMenu, void* pArg);

static int pvr_confirmSaveJob(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int pvr_leavingEditMenu(interfaceMenu_t *pMenu, void* pArg);

static void pvr_manageMenuDisplay(interfaceMenu_t *pMenu);
static int pvr_manageMenuProcessCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd);
#endif // STBPBX

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static interfaceListMenu_t PvrLocationMenu;

#ifdef STBPNX
static const time_t notifyTimeout = 10;

static interfaceListMenu_t PvrEditMenu;
//static interfaceListMenu_t PvrManageMenu;
static pvrManageMenu_t     pvrManageMenu;
static pvrEditInfo_t       pvrEditInfo;

static char pvr_httpUrl[MAX_URL];

static pthread_t  pvr_jobThread;
static socketClient_t pvr_socket;
static volatile int displayedWarning;
static int        pvr_storageReady = 0;
#ifdef ENABLE_DVB
static int        pvr_requestedDvbChannel;
#endif
#endif // STBPNX

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

interfaceListMenu_t PvrMenu;
#ifdef STBPNX
list_element_t *pvr_jobs = NULL;
#endif

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

#ifdef STBPNX
static int pvr_get_status(socketClient_t *s)
{
	return client_write(s, "?", 2);
}
#endif

void pvr_init()
{
#ifdef STBPNX
	appControlInfo.pvrInfo.http.url = pvr_httpUrl;
	appControlInfo.pvrInfo.http.url[0] = 0;

	client_create(&pvr_socket, STBPVR_SOCKET_FILE, NULL, pvr_get_status, NULL, NULL);

	appControlInfo.pvrInfo.active = 1;
	if( pthread_create(&pvr_jobThread, NULL, pvrThread, NULL) >= 0)
	{
		pthread_detach(pvr_jobThread);
	} else
	{
		appControlInfo.pvrInfo.active = 0;
	}
#endif
}

void pvr_cleanup()
{
	appControlInfo.pvrInfo.active = 0;
#ifdef STBPNX
	client_destroy(&pvr_socket);
#endif
}

int pvr_getActive()
{
	return
#ifdef ENABLE_DVB
		pvr_isRecordingDVB() ||
#endif
		(appControlInfo.pvrInfo.rtp.desc.fmt != payloadTypeUnknown) ||
		(appControlInfo.pvrInfo.http.url != NULL && appControlInfo.pvrInfo.http.url[0] != 0);
}

#ifdef STBPNX
static void *pvrThread(void *pArg)
{
	char buf[MAX_URL];
	char *type  =  buf;
	char *cmd   = &buf[1];
	char *value = &buf[2];
	ssize_t readbytes;
	
	int channel;

	dprintf("%s: in\n", __FUNCTION__);

	while( appControlInfo.pvrInfo.active )
	{
		if( (readbytes = client_read(&pvr_socket, buf, sizeof(buf))) > 0)
		{
			do
			{
			dprintf("%s: recieved '%s' (%d)\n", __FUNCTION__, buf, readbytes);
			switch(*type)
			{
#ifdef ENABLE_DVB
				case 'd':
					switch(*cmd)
					{
						case 'i': //idle
							dprintf("%s: pvr.channel %d dvb.channel %d req %d\n", __FUNCTION__, appControlInfo.pvrInfo.dvb.channel, appControlInfo.dvbInfo[screenMain].channel, pvr_requestedDvbChannel);
							channel = appControlInfo.pvrInfo.dvb.channel;
							appControlInfo.pvrInfo.dvb.channel = STBPVR_DVB_CHANNEL_NONE;
							interface_playControlRefresh(0);
							if( pvr_requestedDvbChannel != STBPVR_DVB_CHANNEL_NONE )
							{
								offair_channelChange( interfaceInfo.currentMenu, (void*)CHANNEL_INFO_SET(screenMain, pvr_requestedDvbChannel));
							} else if( appControlInfo.dvbInfo[screenMain].active && offair_getIndex( channel ) == appControlInfo.dvbInfo[screenMain].channel )
							{
								offair_channelChange( interfaceInfo.currentMenu, (void*)CHANNEL_INFO_SET(screenMain, appControlInfo.dvbInfo[screenMain].channel)); // restart normal playback
							}
							pvr_requestedDvbChannel = STBPVR_DVB_CHANNEL_NONE;
							if( channel  != STBPVR_DVB_CHANNEL_NONE )
							{
								pvr_importJobList(); // if there was a timed job, it can be cancelled by StbPvr
								pvr_fillManageMenu( (interfaceMenu_t*)&pvrManageMenu, NULL );
								interface_displayMenu(1);
							}
							break;
						case 'r': // recording
							displayedWarning = 0;
							pvr_requestedDvbChannel = STBPVR_DVB_CHANNEL_NONE;
							appControlInfo.pvrInfo.dvb.channel = atoi( value );
							if( pvr_isPlayingDVB(screenMain) )
							{
								offair_stopVideo(screenMain, 1); // stop playback if on the same channel
							}
							interface_showMenu( 1, 1);
							break;
						case 'p': // recording & playing
							displayedWarning = 0;
							pvr_requestedDvbChannel = STBPVR_DVB_CHANNEL_NONE;
							appControlInfo.pvrInfo.dvb.channel = atoi( value );
							if( appControlInfo.dvbInfo[screenMain].active == 0 )
							{
								offair_startPvrVideo(screenMain); // restart playback
							}
							break;
						case 'o': // starts soon
							channel = atoi( value );
							if( appControlInfo.pvrInfo.directory[0] != 0 && displayedWarning == 0 && 
							    channel != appControlInfo.pvrInfo.dvb.channel && (appControlInfo.pvrInfo.dvb.channel >= 0 || appControlInfo.dvbInfo[screenMain].active ))
							{
								pvr_showStopPvr( interfaceInfo.currentMenu, (void*)(-pvrJobTypeDVB) );
							}
							displayedWarning = 1;
							break;
						default:; // ignore
					}
					break;
#endif /* ENABLE_DVB */
				case 'u':
					switch (*cmd)
					{
						case 'i':
							channel = appControlInfo.pvrInfo.rtp.desc.fmt != payloadTypeUnknown;
							appControlInfo.pvrInfo.rtp.desc.fmt = payloadTypeUnknown;
							interface_playControlRefresh(0);
							if( channel )
							{
								pvr_importJobList(); // if there was a timed job, it can be cancelled by StbPvr
								pvr_fillManageMenu( (interfaceMenu_t*)&pvrManageMenu, NULL );
								interface_displayMenu(1);
							}
							break;
						case 'r':
						{
							url_desc_t url;
							dprintf("%s: got '%s'\n", __FUNCTION__, buf);
							if( parseURL(value, &url) == 0 )
							{
								dprintf("%s: proto=%d ip=%s port=%d\n", __FUNCTION__, url.protocol, url.address, url.port);
								appControlInfo.pvrInfo.rtp.desc.fmt   = payloadTypeMpegTS;
								appControlInfo.pvrInfo.rtp.desc.type  = mediaTypeVideo;
								appControlInfo.pvrInfo.rtp.desc.port  = url.port;
								appControlInfo.pvrInfo.rtp.desc.proto = url.protocol;
								appControlInfo.pvrInfo.rtp.ip.s_addr  = inet_addr(url.address);
								interface_displayMenu(1);
							}
							break;
						}
						case 'o': // starts soon
							channel = atoi( value );
							if( appControlInfo.pvrInfo.directory[0] != 0 && displayedWarning == 0 && 
							    appControlInfo.pvrInfo.rtp.desc.fmt != payloadTypeUnknown)
							{
								pvr_showStopPvr( interfaceInfo.currentMenu, (void*)(-pvrJobTypeRTP) );
							}
							displayedWarning = 1;
							break;
						default: ;// ignore
					}
					break;
				case 'h':
					switch( *cmd )
					{
						case 'i':
							channel = appControlInfo.pvrInfo.http.url[0] != 0;
							appControlInfo.pvrInfo.http.url[0] = 0;
							if( channel )
							{
								pvr_importJobList(); // if there was a timed job, it can be cancelled by StbPvr
								pvr_fillManageMenu( (interfaceMenu_t*)&pvrManageMenu, NULL );
								interface_displayMenu(1);
							}
							break;
						case 'r':
						{
							char *str;
							str = index( value, ' ' );
							if( str != NULL )
							{
								*str = 0;
								str++;
								strncpy(appControlInfo.pvrInfo.http.session_name, str, sizeof(appControlInfo.pvrInfo.http.session_name));
							} else
								strcpy(appControlInfo.pvrInfo.http.session_name, "http");
							appControlInfo.pvrInfo.http.session_name[sizeof(appControlInfo.pvrInfo.http.session_name)-1] = 0;
							strncpy(appControlInfo.pvrInfo.http.url, value, MAX_URL );
							break;
						}
						case 'o':
							if( appControlInfo.pvrInfo.directory[0] != 0 && displayedWarning == 0 && 
							    appControlInfo.pvrInfo.http.url[0] != 0 )
							{
								pvr_showStopPvr( interfaceInfo.currentMenu, (void*)(-pvrJobTypeHTTP) );
							}
							break;
					}
					break;
				case 'e':
					displayedWarning = 0;
#ifdef ENABLE_DVB
					if( pvr_isPlayingDVB(screenMain) )
						offair_stopVideo(screenMain, 1);
					appControlInfo.pvrInfo.dvb.channel = STBPVR_DVB_CHANNEL_NONE;
#endif
					appControlInfo.pvrInfo.rtp.desc.fmt = payloadTypeUnknown;
					pvr_importJobList(); // if there was a timed job, it can be cancelled by StbPvr
					//if( interfaceInfo.showMenu )
					{
						interface_showMenu(1, 0);
						interface_menuActionShowMenu(interfaceInfo.currentMenu, &PvrLocationMenu);
					}
					switch(*cmd)
					{
						case 'd': // dir not accessable
							interface_showMessageBox( _T("ERR_PVR_DIR_NOT_EXIST"), thumbnail_error, 5000);
							break;
						case 'e': // dir not set
							interface_showMessageBox( _T("ERR_PVR_DIR_NOT_SET"), thumbnail_error, 5000);
							break;
						case 's': // no space left
							interface_showMessageBox( _T("ERR_PVR_NO_SPACE_LEFT"), thumbnail_error, 5000);
							break;
						default: // some other error
							interface_showMessageBox( _T("ERR_PVR_RECORD"), thumbnail_error, 5000);
					}
					break;
				default:; //ignore
			}
			ssize_t len = strlen(buf)+1;
			if( len < readbytes )
			{
				memmove( buf, &buf[len], readbytes-len );
			}
			readbytes -= len;
			} while( readbytes > 0 );
			if( interfaceInfo.showMenu && interfaceInfo.currentMenu == (interfaceMenu_t*)&pvrManageMenu)
			{
				pvr_fillManageMenu(interfaceInfo.currentMenu, NULL);
				interface_displayMenu(1);
			}
#ifdef ENABLE_DVB
			offair_fillDVBTMenu();
			if( interfaceInfo.showMenu && interfaceInfo.currentMenu == (interfaceMenu_t*)&DVBTMenu)
			{
				interface_displayMenu(1);
			}
#endif
		} else
		{
			//dprintf("%s: can't read socket\n");
			usleep(100000);
		}
	}

	dprintf("%s: Stopping PVR Thread\n", __FUNCTION__);
	return NULL;
}
#endif // STBPNX

/* Functions used to control the PVR recording */

#ifdef STSDK
static void pvr_stopRecordingST(int which)
{
	elcdRpcType_t type;
	int           ret;
	cJSON        *res = NULL;
	// TODO: add request params
	ret = st_rpcSync (elcmd_recstop, NULL, &type, &res);
	// TODO: handle answer
	(void)ret;
	cJSON_Delete(res);
	appControlInfo.pvrInfo.dvb.channel = STBPVR_DVB_CHANNEL_NONE;
}
#endif // STSDK

#ifdef ENABLE_DVB
void pvr_stopRecordingDVB(int which)
{
	dprintf("%s: Stop PVR record on tuner %d\n", __FUNCTION__, which);
	//dvb_stopDVB(appControlInfo.pvrInfo.tuner, 1);
	//appControlInfo.tunerInfo[appControlInfo.pvrInfo.tuner].status = tunerInactive;

#ifdef STBPNX
	client_write(&pvr_socket, "ds",3);
#endif
#ifdef STSDK
	pvr_stopRecordingST(which);
	offair_fillDVBTMenu();
#endif
}

int  pvr_isRecordingDVB()
{
	return appControlInfo.pvrInfo.dvb.channel != STBPVR_DVB_CHANNEL_NONE;
}

#endif // ENABLE_DVB

void pvr_stopRecordingRTP(int which)
{
	dprintf("%s: %d\n", __FUNCTION__, which);

#ifdef STBPNX
	client_write(&pvr_socket, "us",3);
#endif
#ifdef STSDK
	pvr_stopRecordingST(which);
#endif
}

void pvr_stopRecordingHTTP(int which)
{
	dprintf("%s: %d\n", __FUNCTION__, which);

#ifdef STBPNX
	client_write(&pvr_socket, "hs",3);
#endif
#ifdef STSDK
	pvr_stopRecordingST(which);
#endif
}

/* Functions used to control the PVR playback */

void pvr_stopPlayback(int which)
{
	dprintf("%s: in\n", __FUNCTION__);

	gfx_stopVideoProvider(which, 0, 1);

	dprintf("%s: Set %d playback inactive\n", __FUNCTION__, which);

#ifdef STBPNX
	client_write(&pvr_socket, "dn", 3);
#endif
#ifdef STSDK
	pvr_stopRecordingST(which);
#endif
}

#ifdef ENABLE_DVB
#ifdef STBPNX
void pvr_startPlaybackDVB(int which)
{
	char buf[24];
	sprintf(buf, "dp%d", appControlInfo.pvrInfo.dvb.channel );
	if( client_write(&pvr_socket,  buf, strlen(buf)+1 ) <= 0 )
	{
		interface_showMessageBox( _T("ERR_PVR_RECORD"), thumbnail_error, 5000);
	}
}

int  pvr_isPlayingDVB(int which)
{
	return appControlInfo.dvbInfo[screenMain].active && offair_getIndex( appControlInfo.pvrInfo.dvb.channel ) == appControlInfo.dvbInfo[screenMain].channel;
}

int  pvr_hasDVBRecords(void)
{
	if( pvr_isRecordingDVB() )
		return 1;
	list_element_t *job_element;
	for( job_element = pvr_jobs; job_element != NULL; job_element = job_element->next )
	{
		if( ((pvrJob_t*)job_element->data)->type == pvrJobTypeDVB )
			return 1;
	}
	return 0;
}

void pvr_purgeDVBRecords(void)
{
	pvr_stopRecordingDVB( screenMain );

	list_element_t *job_element  = pvr_jobs;
	list_element_t *dvb_record   = NULL;
	list_element_t *prev_element = NULL;

	while( job_element != NULL )
	{
		if( ((pvrJob_t*)job_element->data)->type == pvrJobTypeDVB )
		{
			dvb_record  = job_element;
			job_element = job_element->next;
			if( prev_element != NULL )
				prev_element->next = job_element;
			else
				pvr_jobs = job_element;
			free_element(dvb_record);
		} else
		{
			prev_element = job_element;
			job_element  = job_element->next;
		}
	}

	pvr_exportJobList();
}
#endif // STBPNX
#endif // ENABLE_DVB

static int pvr_selectStorage(interfaceMenu_t *pMenu, void* pArg)
{
	char storagePath[MENU_ENTRY_INFO_LENGTH];
	int storageIndex = (int)pArg;
	if(storageIndex > 0)
	{
		interface_getMenuEntryInfo( (interfaceMenu_t*)&PvrLocationMenu,storageIndex,storagePath,MENU_ENTRY_INFO_LENGTH);
		sprintf(appControlInfo.pvrInfo.directory,"%s%s", usbRoot, storagePath);
		saveAppSettings();
		pvr_updateSettings();
		interface_menuActionShowMenu(interfaceInfo.currentMenu, &PvrLocationMenu);
	}
	return 0;
}

static int pvr_browseRecords( interfaceMenu_t *pMenu, void* pArg )
{
#ifdef ENABLE_USB
		sprintf( currentPath, "%s" STBPVR_FOLDER "/", appControlInfo.pvrInfo.directory );
		return media_initUSBBrowserMenu( pMenu, (void*)NULL );
	
#endif
	return 1;
}

static int pvr_checkLocations(void *pArg)
{
	pvr_fillLocationMenu((interfaceMenu_t *)&PvrLocationMenu, NULL);
	interface_displayMenu(1);
	interface_addEvent( pvr_checkLocations, NULL, PVR_LOCATION_UPDATE_INTERVAL, 1 );
	return 0;
}

static int pvr_fillLocationMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int i,storageCount;
	struct dirent **usb_storages;
	char *str;
	char buf[MENU_ENTRY_INFO_LENGTH];

	interface_clearMenuEntries((interfaceMenu_t *)&PvrLocationMenu);

	str = appControlInfo.pvrInfo.directory[0] == 0 ? _T("NONE") : &appControlInfo.pvrInfo.directory[strlen(usbRoot)];
	snprintf(buf,MENU_ENTRY_INFO_LENGTH,"%s: %s", _T("RECORDS_PATH"), str);
	str = buf;
	interface_addMenuEntryCustom((interfaceMenu_t *)&PvrLocationMenu, interfaceMenuEntryText, str, strlen(str)+1, appControlInfo.pvrInfo.directory[0] != 0 && appControlInfo.pvrInfo.directory[1] != 'p', pvr_browseRecords, NULL, NULL, NULL, NULL, thumbnail_folder);

	if( media_scanStorages() <= 0 )
	{
		str = _T("USB_NOTFOUND");
		interface_addMenuEntryDisabled((interfaceMenu_t *)&PvrLocationMenu, str, thumbnail_info);
	} else
	{
		storageCount = scandir(usbRoot, &usb_storages, media_select_usb, alphasort);
		for( i = 0 ; i < storageCount; ++i )
		{
			interface_addMenuEntry((interfaceMenu_t *)&PvrLocationMenu, usb_storages[i]->d_name, pvr_selectStorage, (void*)interface_getMenuEntryCount((interfaceMenu_t *)&PvrLocationMenu), thumbnail_usb);
			free(usb_storages[i]);
		}
		free(usb_storages);
	}

	interface_addEvent( pvr_checkLocations, NULL, PVR_LOCATION_UPDATE_INTERVAL, 1 );

	return 0;
}

static int pvr_leavingLocationMenu(interfaceMenu_t *pMenu, void* pArg)
{
	interface_removeEvent(pvr_checkLocations, NULL);
	return 0;
}

void pvr_storagesChanged()
{
	if( interfaceInfo.currentMenu == (interfaceMenu_t *)&PvrLocationMenu )
	{
		pvr_fillLocationMenu( interfaceInfo.currentMenu, NULL );
		interface_displayMenu(1);
	}
}

#ifdef STBPNX
static int pvr_initEditMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int i;
	int          jobIndex = PVR_INFO_GET_INDEX(pArg);
	list_element_t *job_element;
	time_t cur_tt;
	int ret;

	pvrEditInfo.dirty = 0;

	if( PVR_JOB_NEW == jobIndex )
	{
		time(&cur_tt);
		pvrEditInfo.job.start_time = cur_tt;
		pvrEditInfo.job.end_time   = cur_tt;
#ifdef ENABLE_DVB
		if( pvrEditInfo.job.type == pvrJobTypeDVB )
		{
			pvrEditInfo.job.info.dvb.channel =  offair_getCurrentServiceIndex(screenMain);
			if( pvrEditInfo.job.info.dvb.channel < 0 )
				pvrEditInfo.job.info.dvb.channel = 0;
		}
#endif
	} else
	{
		job_element = pvr_jobs;
		for( i = 0; i < jobIndex && job_element != NULL; i++ )
		{
			job_element = job_element->next;
		}
		if( i == jobIndex && job_element != NULL )
		{
			memcpy( &pvrEditInfo.job, job_element->data, sizeof(pvrJob_t) );
			switch( pvrEditInfo.job.type )
			{
				case pvrJobTypeUnknown:
				case pvrJobTypeDVB:
				case pvrJobTypeRTP:
					break;
				case pvrJobTypeHTTP:
					pvrEditInfo.job.info.http.url = pvrEditInfo.url;
					strcpy( pvrEditInfo.url, ((pvrJob_t*)job_element->data)->info.http.url );
					break;
			}
		} else
			return -1;
	}
	localtime_r(&pvrEditInfo.job.start_time, &pvrEditInfo.start_tm);
	localtime_r(&pvrEditInfo.job.end_time,   &pvrEditInfo.end_tm);

	switch( pvrEditInfo.job.type )
	{
		case pvrJobTypeUnknown: break;
		case pvrJobTypeDVB:
#ifdef ENABLE_DVB
			PvrMenu.baseMenu.pParentMenu = (interfaceMenu_t*)&DVBTMenu;
#endif
			break;
		case pvrJobTypeRTP:  PvrMenu.baseMenu.pParentMenu = (interfaceMenu_t*)&rtpStreamMenu[screenMain]; break;
		case pvrJobTypeHTTP: PvrMenu.baseMenu.pParentMenu = (interfaceMenu_t*)&interfaceMainMenu; break;
	}

	PvrEditMenu.baseMenu.pArg = pArg;
	//if( jobIndex != PVR_JOB_NEW )
	{
		interface_setSelectedItem((interfaceMenu_t*)&PvrEditMenu, 2); // start time
		pvrEditInfo.start_entry.active = 1;                           // edit now
	}
	ret = pvr_fillEditMenu(pMenu, pArg);
	return ret;
}

static int pvr_resetEditMenu(interfaceMenu_t *pMenu, void* pArg)
{
	interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t *)pArg;
	return pvr_fillEditMenu( pMenu, pEditEntry->pArg );
}

static int pvr_fillEditMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	struct tm *t;
	time_t     duration;
	interface_clearMenuEntries((interfaceMenu_t *)&PvrEditMenu);

	switch( pvrEditInfo.job.type )
	{
		case pvrJobTypeUnknown:
			break;
		case pvrJobTypeDVB:
#ifdef ENABLE_DVB
			snprintf(buf, sizeof(buf), "%s: %s", _T("CHANNEL"), dvb_getTempServiceName(pvrEditInfo.job.info.dvb.channel));
#ifdef PVR_CAN_EDIT_SOURCE
			interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, buf, pvr_editDVBChannel, pArg, thumbnail_channels);
#else
			interface_addMenuEntryDisabled((interfaceMenu_t *)&PvrEditMenu, buf, thumbnail_channels);
#endif
#endif // ENABLE_DVB
			break;
		case pvrJobTypeRTP:
			snprintf(buf, sizeof(buf), "%s: %s", _T("CHANNEL"), pvr_getRTPChannel(0, NULL) );
#ifdef PVR_CAN_EDIT_SOURCE
			interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, buf, pvr_editRTPChannel, pArg, thumbnail_channels);
#else
			interface_addMenuEntryDisabled((interfaceMenu_t *)&PvrEditMenu, buf, thumbnail_channels);
#endif
			break;
		case pvrJobTypeHTTP:
			snprintf(buf, sizeof(buf), "%s: %s", _T("URL"), pvr_getHTTPURL(0, NULL) );
			buf[sizeof(buf)-1] = 0;
#ifdef PVR_CAN_EDIT_SOURCE
			interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, buf, strlen(pvr_httpUrl) < MAX_FIELD_PATTERN_LENGTH ? pvr_editHTTPURL : pvr_showHTTPURL, pArg, thumbnail_channels);
#else
			interface_addMenuEntryDisabled((interfaceMenu_t *)&PvrEditMenu, buf, thumbnail_channels);
#endif
			break;
	}

/*
	str = _T("SAVE");
	interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, str, pvr_saveJobEntry, pArg, thumbnail_file);

	sprintf(buf, "%s: ", _T("RECORD_DATE"));
	str = &buf[ strlen(buf) ];
	strftime(str, 25, "%D", t);
	str = buf;
	interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, str, pvr_editDate, pArg, thumbnail_account);

	sprintf(buf, "%s: ", _T("RECORD_START_TIME"));
	str = &buf[ strlen(buf) ];
	strftime(str, 15, "%H:%M", t);
	str = buf;
	interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, str, pvr_editTime, (void*)0, thumbnail_account);
*/
	t = &pvrEditInfo.start_tm;
	strftime( pvrEditInfo.date_entry.info.date.value, sizeof(pvrEditInfo.date_entry.info.date.value), "%d%m%Y", t);
	interface_addEditEntryDate((interfaceMenu_t *)&PvrEditMenu, _T("RECORD_DATE"), pvr_setDate, pvr_resetEditMenu, pArg, thumbnail_account, &pvrEditInfo.date_entry);
	strftime( pvrEditInfo.start_entry.info.time.value, sizeof(pvrEditInfo.start_entry.info.time.value), "%H%M", t);
	interface_addEditEntryTime((interfaceMenu_t *)&PvrEditMenu, _T("RECORD_START_TIME"), pvr_setStart, pvr_resetEditMenu, pArg, thumbnail_account, &pvrEditInfo.start_entry);

/*
	t = localtime(&pvrEditInfo.job.end_time);
	sprintf(buf, "%s: ", _T("RECORD_END_TIME"));
	str = &buf[ strlen(buf) ];
	strftime(str, 15, "%H:%M", t);
	str = buf;
	interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, str, pvr_editTime, (void*)1, thumbnail_account);
*/
	duration = pvrEditInfo.job.end_time - pvrEditInfo.job.start_time;
	duration -= duration % 60;
	t = gmtime(&duration);
	strftime( pvrEditInfo.duration_entry.info.time.value, sizeof(pvrEditInfo.duration_entry.info.time.value), "%H%M", t);
	interface_addEditEntryTime((interfaceMenu_t *)&PvrEditMenu, _T("DURATION"), pvr_setDuration, pvr_resetEditMenu, pArg, thumbnail_account, &pvrEditInfo.duration_entry);

	switch( pvrEditInfo.job.type )
	{
		case pvrJobTypeUnknown:
		case pvrJobTypeDVB:
			break;
		case pvrJobTypeRTP:
			sprintf(buf, "%s: %s", _T("TITLE"), pvrEditInfo.job.info.rtp.session_name);
			interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, buf, pvr_editRTPName, pArg, thumbnail_channels);
			break;
		case pvrJobTypeHTTP:
			sprintf(buf, "%s: %s", _T("TITLE"), pvrEditInfo.job.info.http.session_name);
			interface_addMenuEntry((interfaceMenu_t *)&PvrEditMenu, buf, pvr_editHTTPName, pArg, thumbnail_channels);
			break;
	}

	interface_menuActionShowMenu(pMenu,(void*)&PvrEditMenu);

	return 0;
}

static int pvr_leavingEditMenu(interfaceMenu_t *pMenu, void* pArg)
{
	dprintf("%s: in\n", __FUNCTION__);

	if(pvrEditInfo.dirty)
	{
		interface_showConfirmationBox(_T("CONFIRM_SAVE_CHANGES"), thumbnail_question, pvr_confirmSaveJob, pArg);
		return 1;
	}

	return 0;
}

static int pvr_editKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int channelNumber;

	switch( cmd->command )
	{
		case interfaceCommandRed:
			pvrEditInfo.dirty = 0;
			interface_menuActionShowMenu(pMenu, (void*)&pvrManageMenu);
			return 0;
		case interfaceCommandGreen:
			pvr_saveJobEntry(pMenu, PvrEditMenu.baseMenu.pArg);
			return 0;
		case interfaceCommandBlue:
			switch( pvrEditInfo.job.type )
			{
				case pvrJobTypeUnknown:
					return 0;
				case pvrJobTypeRTP:
				case pvrJobTypeHTTP:
				{
					char *url = pvrEditInfo.job.type == pvrJobTypeHTTP ? pvr_getHTTPURL(0, NULL) : pvr_getRTPChannel(0, NULL);
					channelNumber = rtp_getChannelNumber( url );
					if( CHANNEL_CUSTOM != channelNumber )
						rtp_initEpgMenu( pMenu, (void*)channelNumber );
					else
						eprintf("%s: Can't find '%s' in RTP playlist\n", __FUNCTION__, url);
					return 0;
				}
				case pvrJobTypeDVB:
#ifdef ENABLE_DVB
					channelNumber = offair_getServiceIndex( pvrEditInfo.job.info.dvb.service == NULL ? dvb_getService( pvrEditInfo.job.info.dvb.channel ) : pvrEditInfo.job.info.dvb.service );
					if( CHANNEL_CUSTOM != channelNumber && channelNumber >= 0 )
					{
						EPGRecordMenu.baseMenu.pArg = (void*)channelNumber;
						interface_menuActionShowMenu( pMenu, (void*)&EPGRecordMenu );
					} else
						eprintf("%s: Can't find %d channel\n", __FUNCTION__, pvrEditInfo.job.info.dvb.channel);
#endif
					return 0;
			}
			break;
		default:
			return 1;
	}
	return 1;
}

static int pvr_confirmSaveJob(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch( cmd->command )
	{
		case interfaceCommandExit:
		case interfaceCommandLeft:
		case interfaceCommandRed:
			pvrEditInfo.dirty = 0;
			interface_menuActionShowMenu(pMenu, (void*)&pvrManageMenu);
			return 0;
		case interfaceCommandGreen:
		case interfaceCommandOk:
		case interfaceCommandEnter:
			return pvr_saveJobEntry(pMenu, pArg);
		default:
			return 1;
	}
}

static int pvr_clearManageMenu(interfaceMenu_t *pMenu, void* pArg)
{
	pvr_clearJobList();
	pvr_updateSettings();
	pvr_fillManageMenu(pMenu, pArg);
	interface_displayMenu(1);
	return 0;
}

static int pvr_fillManageMenu(interfaceMenu_t *pMenu, void* pArg)
{
	list_element_t *job_element;
	pvrJob_t *job;
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str, *name;
	struct tm *t;
	time_t current_time;
	int jobIndex;
	int icon;

	pvr_storageReady = appControlInfo.pvrInfo.directory[0] != 0 && helperCheckDirectoryExsists(appControlInfo.pvrInfo.directory);

	interface_clearMenuEntries((interfaceMenu_t *)&pvrManageMenu);

	if( pvr_jobs == NULL )
	{
		str = _T("NO_RECORDS");
		interface_addMenuEntryDisabled((interfaceMenu_t *)&pvrManageMenu, str, thumbnail_info);
		interface_setSelectedItem((interfaceMenu_t *)&pvrManageMenu, MENU_ITEM_MAIN);
	} else
	{
		time(&current_time);

		str = _T("CLEARLIST");
		interface_addMenuEntry((interfaceMenu_t *)&pvrManageMenu, str, pvr_clearManageMenu, (void*)-1, thumbnail_file);
		interface_addMenuEntryCustom((interfaceMenu_t *)&pvrManageMenu, interfaceMenuEntryHeading, "", 1, 0, NULL, NULL, NULL, NULL, (void*)-1, 0);

		jobIndex = 0;
		for( job_element = pvr_jobs; job_element != NULL; job_element = job_element->next )
		{
			job = (pvrJob_t*)job_element->data;
			str = buf;
			t = localtime(&job->start_time);
			strftime( str, 25, _T("DATESTAMP"), t);
			str = &str[strlen(str)];
			strftime( str, 15, " %H:%M", t);
			str = &str[strlen(str)];
			t = localtime(&job->end_time);
			strftime( str, 15, "-%H:%M ", t);
			str = &str[strlen(str)];
			name = pvr_jobGetName( job );
			sprintf(str,"%s", name ? name : _T("NOT_AVAILABLE_SHORT"));
			str = buf;
			if( current_time > job->end_time )
				icon = thumbnail_selected;
			else if( current_time >= job->start_time )
				icon = thumbnail_recording;
			else
				icon = thumbnail_recorded_epg;
			interface_addMenuEntry((interfaceMenu_t *)&pvrManageMenu, str, pvr_openRecord, (void*)jobIndex, icon);
			jobIndex++;
		}
	}

	return 0;
}
#endif // STBPNX

void pvr_buildPvrMenu(interfaceMenu_t* pParent)
{
	createListMenu(&PvrMenu, _T("RECORDING"), thumbnail_recording, NULL, pParent,
		/* interfaceInfo.clientX, interfaceInfo.clientY,
		interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
		pvr_fillPvrMenu, NULL, NULL);

#ifdef STBPNX
	int pvr_icons[4] = { 0, 0, 0, 0 };
	char *str;
	char  buf[10];
	DFBRectangle rect;

	pvr_importJobList();

	str = _T("RECORDS_MANAGE");
	pvr_icons[0] = statusbar_f1_delete;
	pvr_icons[1] = statusbar_f2_add_record;
	pvr_icons[2] = statusbar_f3_edit_record;
	pvr_icons[3] = statusbar_f4_schedule;
	
	//createListMenu(&pvrManageMenu, str, thumbnail_recording, pvr_icons, (interfaceMenu_t*)&PvrMenu,
	// interfaceListMenuIconThumbnail, pvr_fillManageMenu, NULL, NULL);
	//interface_setCustomKeysCallback((interfaceMenu_t*)&pvrManageMenu, pvr_manageKeyCallback);

	createBasicMenu((interfaceMenu_t*)&pvrManageMenu, interfaceMenuCustom, _T("RECORDS_MANAGE"), thumbnail_recording, pvr_icons, (interfaceMenu_t *)&PvrMenu, pvr_manageMenuProcessCommand, pvr_manageMenuDisplay, NULL, pvr_fillManageMenu, NULL, NULL);
	pvrManageMenu.channel_w = ERM_CHANNEL_NAME_LENGTH;
	strcpy(buf,"00:00");
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );
	pvrManageMenu.end_w = pvrManageMenu.start_w = rect.w;
	strcpy(buf,"00.00.00");
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rect, NULL) );
	pvrManageMenu.date_w = rect.w;
	interface_setCustomKeysCallback((interfaceMenu_t*)&pvrManageMenu, pvr_manageKeyCallback);

	pvr_icons[0] = statusbar_f1_cancel;
	pvr_icons[1] = statusbar_f2_ok;
	pvr_icons[2] = 0;
	pvr_icons[3] = statusbar_f4_schedule;
	createListMenu(&PvrEditMenu, str, thumbnail_recording, pvr_icons, (interfaceMenu_t*)&pvrManageMenu,
		/* interfaceInfo.clientX, interfaceInfo.clientY,
		interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
		NULL, pvr_leavingEditMenu, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&PvrEditMenu, pvr_editKeyCallback);
#endif // STBPNX
	createListMenu(&PvrLocationMenu, _T("RECORDED_LOCATION"), thumbnail_usb, NULL, (interfaceMenu_t*)&PvrMenu,
		/* interfaceInfo.clientX, interfaceInfo.clientY,
		interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
		pvr_fillLocationMenu, pvr_leavingLocationMenu, NULL);

#ifdef STBPNX
	pvrEditInfo.start_entry.info.time.type    = interfaceEditTime24;
	pvrEditInfo.duration_entry.info.time.type = interfaceEditTimeCommon;
#endif
}

static int pvr_fillPvrMenu(interfaceMenu_t *pMenu, void *pArg)
{
	char *str;

	interface_clearMenuEntries((interfaceMenu_t *)&PvrMenu);
#ifdef STBPNX
	switch( pvrEditInfo.job.type )
	{
		case pvrJobTypeUnknown: break;
		case pvrJobTypeDVB:
#ifdef ENABLE_DVB
			{
				int epg_enabled = offair_epgEnabled();
				str = _T( epg_enabled ? "EPG_RECORD" : "EPG_UNAVAILABLE");
				interface_addMenuEntryCustom((interfaceMenu_t*)&PvrMenu, interfaceMenuEntryText, str, strlen(str)+1, epg_enabled, (menuActionFunction)menuDefaultActionShowMenu, NULL, NULL, NULL, (void*)&EPGRecordMenu, thumbnail_recorded_epg);
			}
#endif
		case pvrJobTypeHTTP:
		case pvrJobTypeRTP:
			str = _T("RECORDS_MANAGE");
			interface_addMenuEntry((interfaceMenu_t*)&PvrMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&pvrManageMenu, thumbnail_schedule_record);

			str = _T("RECORDED_LOCATION");
			interface_addMenuEntry((interfaceMenu_t*)&PvrMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&PvrLocationMenu, thumbnail_usb);
			break;
	}
#endif // STBPNX
#ifdef STSDK
	str = _T("RECORDED_LOCATION");
	interface_addMenuEntry((interfaceMenu_t*)&PvrMenu, str, (menuActionFunction)menuDefaultActionShowMenu, (void*)&PvrLocationMenu, thumbnail_usb);
#endif
	return 0;
}

int pvr_initPvrMenu(interfaceMenu_t *pMenu, void *pArg)
{
#ifdef STBPNX
	pvrJobType_t new_type = (pvrJobType_t)pArg;
	if( new_type != pvrEditInfo.job.type )
	{
		switch( new_type )
		{
			case pvrJobTypeUnknown: break;
			case pvrJobTypeDVB:
				pvrEditInfo.job.info.dvb.channel = 0;
				pvrEditInfo.job.info.dvb.service = NULL;
				break;
			case pvrJobTypeRTP:
				pvrEditInfo.job.info.rtp.desc.proto = mediaProtoUDP;
				pvrEditInfo.job.info.rtp.desc.fmt   = payloadTypeMpegTS;
				pvrEditInfo.job.info.rtp.desc.port  = 0;
				pvrEditInfo.job.info.rtp.ip.s_addr  = INADDR_ANY;
				pvrEditInfo.job.info.rtp.session_name[0] = 0;
				break;
			case pvrJobTypeHTTP:
				pvrEditInfo.job.info.http.url = pvrEditInfo.url;
				pvrEditInfo.job.info.http.url[0] = 0;
				pvrEditInfo.job.info.http.session_name[0] = 0;
				break;
		}
		pvrEditInfo.job.type = new_type;
	}
#endif // STBPNX
	interface_menuActionShowMenu(pMenu, (void*)&PvrMenu);

	return 0;
}

void pvr_recordNow(void)
{
	if( appControlInfo.pvrInfo.directory[0] == 0 )
	{
		interface_menuActionShowMenu(interfaceInfo.currentMenu, &PvrLocationMenu);
		interface_showMessageBox( _T("ERR_PVR_DIR_NOT_SET"), thumbnail_warning, 5000);
		return;
	}

#ifdef ENABLE_DVB

	if( appControlInfo.dvbInfo[screenMain].active )
	{
		int channel = offair_getCurrentServiceIndex(screenMain);
#ifdef STBPNX
		char buf[10];

		offair_stopVideo(screenMain, 1);

		if ( pvr_isRecordingDVB() )
		{
			client_write(&pvr_socket, "ds", 3);
			//sprintf("%s: Unable to record requested programme - all tuners in use!\n", __FUNCTION__);
			//return;
		}

		sprintf(buf, "dp%d", channel);
		if( client_write(&pvr_socket, buf,strlen(buf)+1) <= 0 )
		{
			interface_showMessageBox( _T("ERR_PVR_RECORD"), thumbnail_error, 5000);
		}
		pvrEditInfo.job.type = pvrJobTypeDVB;
		pvrEditInfo.job.info.dvb.channel = channel;
		pvrEditInfo.job.info.dvb.service = NULL;
#endif // STBPNX
#ifdef STSDK
		//offair_stopVideo( screenMain, 1 );

		int tuner = appControlInfo.dvbInfo[screenMain].tuner;
		if (tuner >= VMSP_COUNT)
			tuner -= VMSP_COUNT;
		elcdRpcType_t type;
		cJSON *answer   = NULL;
		int    ret;
		cJSON *param = cJSON_CreateObject();
		if (!param)
			return;
		char url[24];
		snprintf(url, sizeof(url), "dvb://%d@%d", channel, tuner);
		cJSON_AddItemToObject(param, "url", cJSON_CreateString(url));
		
		cJSON_AddItemToObject(param, "filename", cJSON_CreateString(appControlInfo.pvrInfo.directory));
		ret = st_rpcSync( elcmd_recstart, param, &type, &answer );
		cJSON_Delete(param);
		(void)ret;
		if (type == elcdRpcResult && answer && answer->type == cJSON_String && 
		    strcmp(answer->valuestring, "ok") == 0)
		{
			appControlInfo.pvrInfo.dvb.channel = appControlInfo.dvbInfo[screenMain].channel;
			offair_fillDVBTMenu();
		}
#endif
		PvrMenu.baseMenu.pParentMenu = (interfaceMenu_t*)&DVBTMenu;
	} else
#endif
	if (appControlInfo.rtpInfo[screenMain].active)// || appControlInfo.playbackInfo.streamSource == streamSourceIPTV )
	{
		rtp_recordNow();
	} else if( appControlInfo.mediaInfo.active )
	{
		pvr_record( screenMain, appControlInfo.mediaInfo.filename, appControlInfo.playbackInfo.description);
	}
}

int pvr_record(int which, char *url, char *desc )
{
	char buf[MAX_URL];
	url_desc_t url_desc;
	int url_len;

	if( strncmp( url, "http://", 7 ) == 0  ||
	    strncmp( url, "https://", 8 ) == 0 ||
	    strncmp( url, "ftp://", 6 ) == 0 )
	{
#ifdef STBPNX
		char *str;
		snprintf(buf, sizeof(buf), "hr%s", url);
		buf[sizeof(buf)-1] = 0;
		str = &buf[strlen(buf)];
		if( desc != NULL && desc[0] != 0 && strlen(desc) < sizeof(buf)-(str-buf) )
		{
			*str = ' ';
			str++;
			strcpy(str, desc);
		}
		buf[sizeof(buf)-1] = 0;
		eprintf("PVR: Recording '%s' (%s)\n", url, desc);
		if( client_write(&pvr_socket, buf,strlen(buf)+1) <= 0 )
		{
			interface_showMessageBox( _T("ERR_PVR_RECORD"), thumbnail_error, 5000);
		}
		pvrEditInfo.job.type = pvrJobTypeHTTP;
		pvrEditInfo.job.info.http.url = pvrEditInfo.url;
		strcpy(pvrEditInfo.job.info.http.url, url);
		if( appControlInfo.playbackInfo.playlistMode == playlistModeIPTV )
		{
			PvrMenu.baseMenu.pParentMenu = (interfaceMenu_t*)&rtpStreamMenu[screenMain];
		}
#endif // STBPNX
		return 0;
	}

	if( parseURL( url, &url_desc ) != 0 )
	{
		eprintf("%s: Failed to parse '%s'\n", __FUNCTION__, url);
		return 1;
	}

	switch( url_desc.protocol )
	{
		case mediaProtoRTP:
		case mediaProtoUDP:
		{
			char *str;
			str = index( &url[6], '/');
			if( str )
				url_len = str - url;
			else
				url_len = strlen(url);
			memcpy(buf, url, url_len);
			buf[url_len] = 0;
			eprintf("PVR: Recording '%s' (%s)\n", buf, desc);
#ifdef STBPNX
			if( (size_t)url_len + 3 < sizeof(buf) )
			{
				buf[0] = 'u';
				buf[1] = 'r';
				memcpy( &buf[2], url, url_len );
				if( desc && strlen(desc) + url_len + 4 < sizeof(buf) )
				{
					buf[2+url_len] = ' ';
					strcpy(&buf[2+url_len+1], desc);
					if( buf[2+url_len+1] == 0 )
						buf[2+url_len] = 0;
				} else
					buf[2+url_len] = 0;
				dprintf("%s: writing '%s'\n", __FUNCTION__, buf);
				if( client_write(&pvr_socket, buf,strlen(buf)+1) <= 0 )
				{
					interface_showMessageBox( _T("ERR_PVR_RECORD"), thumbnail_error, 5000);
				}
				pvrEditInfo.job.type = pvrJobTypeRTP;
				PvrMenu.baseMenu.pParentMenu = (interfaceMenu_t*)&rtpStreamMenu[screenMain];
				return 0;
			} else
				eprintf("%s: RTP URL '%s' is too long\n", __FUNCTION__, url);
#endif // STBPNX
			return 1;
		}
		default:
			eprintf("%s: Unsupported proto %d\n", __FUNCTION__, url_desc.protocol);
			return 1;
	}
	return 1;
}

#ifdef STBPNX
int pvr_manageRecord(int which, char *url, char *desc )
{
	url_desc_t url_desc;

	if( strncmp( url, "http://", 7 ) == 0  ||
	    strncmp( url, "https://", 8 ) == 0 ||
	    strncmp( url, "ftp://", 6 ) == 0 )
	{
		pvrEditInfo.job.type = pvrJobTypeHTTP;
		pvrEditInfo.job.info.http.url = pvrEditInfo.url;
		strcpy(pvrEditInfo.job.info.http.url, url);
		if( desc && strlen(desc) < sizeof(pvrEditInfo.job.info.http.session_name) )
		{
			strcpy(pvrEditInfo.job.info.http.session_name, desc);
		} else
			pvrEditInfo.job.info.http.session_name[0] = 0;
		pvr_initEditMenu((interfaceMenu_t*)&pvrManageMenu, (void*)PVR_INFO_SET(pvrJobTypeHTTP,PVR_JOB_NEW));
		return 0;
	}

	if( parseURL( url, &url_desc ) != 0 )
	{
		eprintf("%s: Failed to parse '%s'\n", __FUNCTION__, url);
		return 1;
	}

	switch( url_desc.protocol )
	{
		case mediaProtoRTP:
		case mediaProtoUDP:
		{
			//pvr_initPvrMenu( (interfaceMenu_t*)&PvrMenu, (void*)pvrJobTypeRTP );
			pvrEditInfo.job.type = pvrJobTypeRTP;
			pvrEditInfo.job.info.rtp.desc.proto = url_desc.protocol;
			pvrEditInfo.job.info.rtp.desc.fmt   = payloadTypeMpegTS;
			pvrEditInfo.job.info.rtp.desc.port  = url_desc.port;
			pvrEditInfo.job.info.rtp.ip.s_addr  = inet_addr(url_desc.address);
			if( desc && strlen(desc) < sizeof(pvrEditInfo.job.info.rtp.session_name) )
			{
				strcpy(pvrEditInfo.job.info.rtp.session_name, desc);
			} else
				pvrEditInfo.job.info.rtp.session_name[0] = 0;
			pvr_initEditMenu((interfaceMenu_t*)&pvrManageMenu, (void*)PVR_INFO_SET(pvrJobTypeRTP,PVR_JOB_NEW));
			return 0;
		}
		default:
			eprintf("%s: Unsupported proto %d\n", __FUNCTION__, url_desc.protocol);
			return 1;
	}
}
#endif // STBPNX

void pvr_toggleRecording()
{
	int active = pvr_getActive();
	dprintf("%s: set recording to %d\n", __FUNCTION__, 0 == active);
	if( active )
	{
#ifdef ENABLE_DVB
		if( pvr_isRecordingDVB() )
			pvr_stopRecordingDVB(screenMain);
#endif
		if(appControlInfo.pvrInfo.rtp.desc.fmt != payloadTypeUnknown)
			pvr_stopRecordingRTP(screenMain);
		if(appControlInfo.pvrInfo.http.url[0] != 0 )
			pvr_stopRecordingHTTP(screenMain);
	} else
	{
		pvr_recordNow();
	}
}

int pvr_updateSettings()
{
#ifdef STBPNX
	FILE* f;
	pid_t app_pid;
	char buf[24];

	if( (f = fopen( STBPVR_PIDFILE, "r")) != NULL )
	{
		if( fgets( buf, sizeof(buf), f ) != NULL )
		{
			app_pid = atoi(buf);
			dprintf( "%s: Sending sig %d to %d to update\n", __FUNCTION__, SIGUSR1, app_pid);
			if( app_pid > 0 && kill( app_pid, SIGUSR1 ) == 0)
			{
				fclose(f);
				return 0;
			}
		}
		fclose(f);
	}
	eprintf("PVR: Record service seems to not running\n");
#endif // STBPNX
	return 1;
}

#ifdef STBPNX
static char* pvr_jobGetName(pvrJob_t *job)
{
	switch(job->type)
	{
		case pvrJobTypeUnknown:
			return NULL;
		case pvrJobTypeDVB:
#ifdef ENABLE_DVB
		{
			char *name;
			if( job->info.dvb.service == NULL )
			{
				dprintf("%s: DVB service %d is null\n", __FUNCTION__, job->info.dvb.channel);
				return NULL;
			}
			if( job->info.dvb.event_id == CHANNEL_CUSTOM ||
				NULL == (name = dvb_getEventName(job->info.dvb.service, job->info.dvb.event_id))
			  )
				name = dvb_getServiceName(job->info.dvb.service);
			return name;
		}
#else
			return NULL;
#endif
		case pvrJobTypeRTP:
			return job->info.rtp.session_name[0] ? job->info.rtp.session_name : NULL;
		case pvrJobTypeHTTP:
			return job->info.http.session_name[0] ? job->info.http.session_name : NULL;
	}
	return NULL;
}

static int pvr_jobcmp(pvrJob_t *x, pvrJob_t *y)
{
	int res;
	if( (res = y->start_time - x->start_time) != 0 )
		return res;
	if( (res = y->end_time - x->end_time) != 0 )
		return res;
	if( (res = y->type - x->type ) != 0 )
		return res;
	switch( y->type )
	{
		case pvrJobTypeUnknown:
			return 0;
		case pvrJobTypeDVB:
			if( (res = y->info.dvb.channel - x->info.dvb.channel ) != 0 )
				return res;
			break;
		case pvrJobTypeRTP:
			if( (res = y->info.rtp.ip.s_addr - x->info.rtp.ip.s_addr ) != 0 )
				return res;
			if( (res = y->info.rtp.desc.port - x->info.rtp.desc.port ) != 0 )
				return res;
			break;
		case pvrJobTypeHTTP:
			return strcmp( y->info.http.url, x->info.http.url );
	}
	return 0;
}

static void* pvr_jobcpy(pvrJob_t *dest, pvrJob_t *src)
{
	memcpy( dest, src, sizeof(pvrJob_t) );
	switch( dest->type )
	{
		case pvrJobTypeUnknown:
		case pvrJobTypeDVB:
		case pvrJobTypeRTP:
			break;
		case pvrJobTypeHTTP:
			dest->info.http.url = NULL;
			helperSafeStrCpy( &dest->info.http.url, src->info.http.url );
			break;
	}
	return dest;
}

ssize_t pvr_jobprint(char *buf, size_t buf_size, pvrJob_t *job)
{
	char *str;
	char *name;
	struct tm *t;
	if( buf_size < 24 )
		return -1;
	str = buf;
	t = gmtime(&job->start_time);
	strftime( str, 25, "%F", t);
	str = &str[strlen(str)];
	strftime( str, 15, " %T", t);
	str = &str[strlen(str)];
	t = gmtime(&job->end_time);
	strftime( str, 15, "-%T ", t);
	str = &str[strlen(str)];
	switch( job->type )
	{
		case pvrJobTypeUnknown:
			name = NULL;
			break;
		case pvrJobTypeDVB:
#ifdef ENABLE_DVB
			name = dvb_getTempServiceName(job->info.dvb.channel);
#else
			name = "DVB";
#endif
			break;
		case pvrJobTypeRTP:
			name = job->info.rtp.session_name;
			break;
		case pvrJobTypeHTTP:
			name = job->info.http.session_name;
			break;
	}
	strncpy(str, name, buf_size-(str-buf));
	buf[buf_size-1] = 0;
	return strlen(buf)+1;
}

int pvr_findOrInsertJob(pvrJob_t *job, list_element_t **jobListEntry)
{
	list_element_t *prev_element = NULL;
	pvrJob_t *curJob;

	if( jobListEntry == NULL )
	{
		return -1;
	}

	dprintf("%s: [%d-%d] \n", __FUNCTION__, job->start_time, job->end_time);

	for( *jobListEntry = pvr_jobs; *jobListEntry != NULL; *jobListEntry = (*jobListEntry)->next )
	{
		curJob = (pvrJob_t*)(*jobListEntry)->data;
		dprintf("%s: test: [%d-%d] \n", __FUNCTION__, curJob->start_time, curJob->end_time);
		if(  curJob->type       == job->type &&
		   ((curJob->start_time <= job->start_time    && job->start_time    < curJob->end_time) ||
		    (curJob->start_time <  job->end_time      && job->end_time      < curJob->end_time) ||
		    (job->start_time    <= curJob->start_time && curJob->start_time < job->end_time)) )
		{
			return 0 == pvr_jobcmp(job, curJob)
				? PVR_RES_MATCH
				: PVR_RES_COLLISION;
		}
		if( curJob->start_time >= job->end_time )
			break;
		prev_element = *jobListEntry;
	}
	if( prev_element != NULL )
	{
		*jobListEntry = insert_new_element(prev_element, sizeof(pvrJob_t));
	} else {
		prev_element = pvr_jobs;
		pvr_jobs = allocate_element(sizeof(pvrJob_t));
		*jobListEntry = pvr_jobs;
		(*jobListEntry)->next = prev_element;
	}
	pvr_jobcpy( (*jobListEntry)->data, job );
	return PVR_RES_ADDED;
}

int pvr_addJob(pvrJob_t *job)
{
	pvrJob_t *curJob;
	list_element_t *cur_element;
	list_element_t *prev_element = NULL;

	if( !job )
		return -2;

	dprintf("%s: [%d-%d] \n", __FUNCTION__, job->start_time, job->end_time);

	for( cur_element = pvr_jobs; cur_element != NULL; cur_element = cur_element->next )
	{
		curJob = (pvrJob_t*)cur_element->data;
		dprintf("%s: test: [%d-%d] \n", __FUNCTION__, curJob->start_time, curJob->end_time);
		if( (curJob->start_time <= job->start_time    && job->start_time    < curJob->end_time) ||
			(curJob->start_time <  job->end_time      && job->end_time      < curJob->end_time) ||
			(job->start_time    <= curJob->start_time && curJob->start_time < job->end_time) )
		{
			return 1; // collision
		}
		if( curJob->start_time >= job->end_time )
		{
			break;
		}
		prev_element = cur_element;
	}
	if( prev_element != NULL )
	{
		cur_element = insert_new_element(prev_element, sizeof(pvrJob_t));
	} else {
		prev_element = pvr_jobs;
		pvr_jobs = allocate_element(sizeof(pvrJob_t));
		cur_element = pvr_jobs;
		cur_element->next = prev_element;
	}
	pvr_jobcpy( cur_element->data, job );
	return 0;
}

int pvr_deleteJob(list_element_t* job_element)
{
	list_element_t *prev_element = NULL;
	list_element_t *cur_element;
	pvrJob_t *job;
	for( cur_element = pvr_jobs; cur_element != NULL && cur_element != job_element; cur_element = cur_element->next )
	{
		prev_element = cur_element;
	}
	if( cur_element != job_element )
	{
		return 1;
	}
	if( prev_element == NULL )
	{
		if( pvr_jobs->next == pvr_jobs )
			pvr_jobs = NULL;
		else
			pvr_jobs = pvr_jobs->next;
	} else {
		prev_element->next = job_element->next;
	}
	job = (pvrJob_t *)job_element->data;
	if( job->type == pvrJobTypeHTTP )
		free(job->info.http.url);
	free_element(job_element);
	return 0;
}

int pvr_clearJobList()
{
	list_element_t *cur_element, *del_element;
	pvrJob_t *job;
	cur_element = pvr_jobs;
	while( cur_element != NULL )
	{
		del_element = cur_element;
		cur_element = cur_element->next;
		job = (pvrJob_t*)del_element->data;
		if( job->type == pvrJobTypeHTTP )
			dfree(job->info.http.url);
		free(del_element->data);
		free(del_element);
	}
	pvr_jobs = NULL;

	return 0;
}

int pvr_exportJobList(void)
{
	FILE* f;
	pvrJob_t *curJob;
	list_element_t *cur_element;

	f = fopen( STBPVR_JOBLIST, "w" );
	if ( f == NULL )
	{
		eprintf("%s: Failed to open '%s' for writing: %s\n", __FUNCTION__, strerror(errno));
		return 1;
	}
	for( cur_element = pvr_jobs; cur_element != NULL; cur_element = cur_element->next )
	{
		curJob = (pvrJob_t*)cur_element->data;
		fprintf(f,"JOBSTART=%d\nJOBEND=%d\n", (int)curJob->start_time, (int)curJob->end_time );
		switch( curJob->type )
		{
			case pvrJobTypeUnknown:
				eprintf("%s: wrong job type %d\n", __FUNCTION__, curJob->type);
				break;
			case pvrJobTypeDVB:
				fprintf(f,"JOBCHANNELDVB=%d\n", curJob->info.dvb.channel);
				break;
			case pvrJobTypeRTP:
				fprintf(f,"JOBCHANNELRTP=%s://%s:%d/\nJOBCHANNELRTPNAME=%s\n", proto_toa(curJob->info.rtp.desc.proto), inet_ntoa(curJob->info.rtp.ip), curJob->info.rtp.desc.port, curJob->info.rtp.session_name);
				break;
			case pvrJobTypeHTTP:
				fprintf(f,"JOBURL=%s\nJOBNAME=%s\n", curJob->info.http.url, curJob->info.http.session_name);
				break;
		}
	}
	fclose(f);

	pvr_updateSettings();

	return 0;
}

int pvr_importJobList(void)
{
	FILE* f;
	pvrJob_t *curJob;
	list_element_t *cur_element;
	char buf[MENU_ENTRY_INFO_LENGTH+18]; //strlen("JOBCHANNELRTPNAME)"
	char value[MENU_ENTRY_INFO_LENGTH];
	time_t jobStart, jobEnd;
	int channel_dvb;
	media_desc desc;
	url_desc_t url;
	in_addr_t  rtp_ip;
	char *url_str, *str;
	size_t job_count = 0;
#ifdef ENABLE_DVB
	EIT_service_t *service;
	list_element_t *event_element;
	EIT_event_t *event;
	time_t event_time;
#endif

	f = fopen( STBPVR_JOBLIST, "r" );
	if ( f == NULL )
	{
		eprintf("%s: Can't open job list!\n", __FUNCTION__);
		return 1;
	}
	pvr_clearJobList();
	cur_element = pvr_jobs;
	buf[0] = 0; // buffer is empty and ready for reading next line from input file
	while(1)
	{
		if ( buf[0] == 0 && fgets( buf, sizeof(buf), f ) == NULL )
		{
			fclose(f);
			eprintf("%s: imported %d jobs\n", __FUNCTION__, job_count);
			return 0;
		}
		channel_dvb = STBPVR_DVB_CHANNEL_NONE;
		jobStart    = 0;
		jobEnd      = 0;
		url_str     = NULL;
		if( sscanf(buf, "JOBSTART=%d", (int*)&jobStart) != 1 )
		{
			buf[0] = 0;
			continue;
		}
		if( fgets( buf, sizeof(buf), f ) == NULL)
			break;
		if( sscanf(buf, "JOBEND=%d", (int*)&jobEnd) != 1 )
		{
			buf[0] = 0;
			continue;
		}
		if( fgets( buf, sizeof(buf), f ) == NULL)
			break;
		if( sscanf(buf, "JOBCHANNELDVB=%d", &channel_dvb) != 1 )
		{
			channel_dvb = STBPVR_DVB_CHANNEL_NONE;

			if( sscanf(buf, "JOBCHANNELRTP=%[^\r\n]", value) != 1 )
			{
				if( sscanf(buf, "JOBURL=%[^\r\n]", value) != 1 )
				{
					eprintf("%s: failed to get job type for [%ld-%ld]\n", __FUNCTION__, jobStart, jobEnd);
					buf[0] = 0;
					continue;
				}
				if( strncmp( value, "http://",  7 ) != 0 &&
				    strncmp( value, "https://", 8 ) != 0 &&
				    strncmp( value, "ftp://",   6 ) != 0 )
				{
					eprintf("%s: wrong url proto %s\n", __FUNCTION__, value);
					buf[0] = 0;
					continue;
				}
				url_str = malloc( strlen(value)+1 );
				if( url_str == NULL )
				{
					eprintf("%s: failed to allocate memory!\n", __FUNCTION__);
					buf[0] = 0;
					continue;
				}
				strcpy( url_str, value );
				if( fgets( buf, sizeof(buf), f ) == NULL)
				{
					eprintf("%s: unexpected end of file (http title wanted)!\n", __FUNCTION__);
					buf[0] = 0;
				}
				if( strncmp( buf, "JOBNAME=", sizeof("JOBNAME=")-1 ) == 0 )
				{
					strcpy( value, &buf[sizeof("JOBNAME=")-1] );
					/* Trim following spaces */
					for( str = &value[strlen(value)-1];
					     str >= value && (*str == '\n' || *str == '\r' || *str <= ' ');
					     str-- )
						*str = 0;
				} else
				{
					eprintf("%s: failed to get http job name for [%ld-%ld]\n", __FUNCTION__, jobStart, jobEnd);
					value[0] = 0;
				}
			} else
			{
				memset(&desc, 0, sizeof(desc));
				memset(&url,  0, sizeof(url));

				if( parseURL( value, &url ) != 0 )
				{
					eprintf("%s: invalid RTP url %s!\n", __FUNCTION__, value);
					buf[0] = 0;
					continue;
				}
				switch(url.protocol)
				{
					case mediaProtoUDP:
					case mediaProtoRTP:
						desc.fmt   = payloadTypeMpegTS;
						desc.type  = mediaTypeVideo;
						desc.port  = url.port;
						desc.proto = url.protocol;
						rtp_ip = inet_addr(url.address);
					default:;
				}
				if(desc.fmt == 0 || rtp_ip == INADDR_NONE || rtp_ip == INADDR_ANY)
				{
					eprintf("%s: invalid proto %d!\n", __FUNCTION__, url.protocol);
					buf[0] = 0;
					continue;
				}
				if( fgets( buf, sizeof(buf), f ) == NULL)
				{
					eprintf("%s: unexpected end of file (rtp title wanted)!\n", __FUNCTION__);
					buf[0] = 0;
				}
				if( strncmp( buf, "JOBCHANNELRTPNAME=", sizeof("JOBCHANNELRTPNAME=")-1 ) == 0 )
				{
					strcpy( value, &buf[sizeof("JOBCHANNELRTPNAME=")-1] );
					/* Trim following spaces */
					for( str = &value[strlen(value)-1];
					     str >= value && (*str == '\n' || *str == '\r' || *str <= ' ');
					     str-- )
						*str = 0;
				} else
				{
					eprintf("%s: failed to get rtp job name for [%ld-%ld]\n", __FUNCTION__, jobStart, jobEnd);
					value[0] = 0;
				}
			}
		}
#ifdef ENABLE_DVB
		else
		{
			service = dvb_getService(channel_dvb);
			if( !service )
			{
				eprintf("%s: can't find service %d\n", __FUNCTION__, channel_dvb);
				buf[0] = 0;
				continue;
			}
		}
#endif
		if( pvr_jobs == NULL )
		{
			pvr_jobs = allocate_element(sizeof(pvrJob_t));
			cur_element = pvr_jobs;
		} else {
			cur_element = insert_new_element(cur_element, sizeof(pvrJob_t));
		}
		curJob = (pvrJob_t*)cur_element->data;
		curJob->start_time = jobStart;
		curJob->end_time = jobEnd;
		if( channel_dvb != STBPVR_DVB_CHANNEL_NONE )
		{
			curJob->type = pvrJobTypeDVB;
			curJob->info.dvb.channel  = channel_dvb;
			curJob->info.dvb.event_id = CHANNEL_CUSTOM;
#ifdef ENABLE_DVB
			curJob->info.dvb.service  = service;
			for( event_element = service->schedule; event_element != NULL; event_element = event_element->next )
			{
				event = (EIT_event_t*)event_element->data;
				if( offair_getLocalEventTime( event, NULL, &event_time) == 0 )
				{
					if( event_time == curJob->start_time && event_time + offair_getEventDuration( event ) == curJob->end_time )
					{
						curJob->info.dvb.event_id = event->event_id;
						break;
					}
				}
			}
#else
			curJob->info.dvb.service = NULL;
#endif
		} else if( url_str != NULL )
		{
			curJob->type = pvrJobTypeHTTP;
			curJob->info.http.url = url_str;
			strncpy( curJob->info.http.session_name, value, sizeof(curJob->info.http.session_name) );
		} else
		{
			curJob->type = pvrJobTypeRTP;
			memcpy(&curJob->info.rtp.desc, &desc, sizeof(desc));
			curJob->info.rtp.ip.s_addr = rtp_ip;
			strncpy(curJob->info.rtp.session_name, value, sizeof(curJob->info.rtp.session_name));
			curJob->info.rtp.session_name[sizeof(curJob->info.rtp.session_name)-1] = 0;
		}
		pvr_jobprint( value, sizeof(value), curJob );
		eprintf("%s: imported %d %s\n", __FUNCTION__, job_count, value);
		job_count++;

		buf[0] = 0; // mark buffer ready for next job
	}
	fclose(f);
	eprintf("%s: failed, imported %d jobs", __FUNCTION__, job_count);
	return 1;
}

static void pvr_adjustEndTime()
{
	time_t start_date, start_time, end_time;
	start_time = pvrEditInfo.job.start_time % (24*60*60);
	start_date = pvrEditInfo.job.start_time - start_time;
	end_time   = pvrEditInfo.job.end_time   % (24*60*60);
	pvrEditInfo.job.end_time = start_date + end_time;
	if( end_time < start_time )
	{
		pvrEditInfo.job.end_time += 24*60*60;
	}
	localtime_r(&pvrEditInfo.job.end_time, &pvrEditInfo.end_tm);
}

static int pvr_setStart(interfaceMenu_t *pMenu, void* pArg)
{
	int hour, min;
	char temp[3];
	time_t time_diff;

	temp[2] = 0;
	temp[0] = pvrEditInfo.start_entry.info.time.value[0];
	temp[1] = pvrEditInfo.start_entry.info.time.value[1];
	hour = atoi(temp);
	temp[0] = pvrEditInfo.start_entry.info.time.value[2];
	temp[1] = pvrEditInfo.start_entry.info.time.value[3];
	min = atoi(temp);
	if( hour < 0 || hour > 23 || min  < 0 || min  > 59 )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_TIME"), thumbnail_error, 0);
		return 2;
	}
	if( pvrEditInfo.start_tm.tm_hour != hour ||
	    pvrEditInfo.start_tm.tm_min  != min)
	{
		time_diff = (hour-pvrEditInfo.start_tm.tm_hour)*3600 + (min-pvrEditInfo.start_tm.tm_min)*60;
		pvrEditInfo.dirty             = 1;
		pvrEditInfo.start_tm.tm_hour  = hour;
		pvrEditInfo.start_tm.tm_min   = min;

		pvrEditInfo.job.start_time   += time_diff;
		pvrEditInfo.job.end_time     += time_diff;
		pvr_adjustEndTime();
		pvr_fillEditMenu(pMenu, PvrEditMenu.baseMenu.pArg);
	}
	return 0;
}

static int pvr_setDuration(interfaceMenu_t *pMenu, void* pArg)
{
	time_t duration;
	int hour, min;
	char temp[3];

	temp[2] = 0;
	temp[0] = pvrEditInfo.duration_entry.info.time.value[0];
	temp[1] = pvrEditInfo.duration_entry.info.time.value[1];
	hour = atoi(temp);
	temp[0] = pvrEditInfo.duration_entry.info.time.value[2];
	temp[1] = pvrEditInfo.duration_entry.info.time.value[3];
	min = atoi(temp);
	if( hour < 0 || hour > 23 || min  < 0 || min  > 59 )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_TIME"), thumbnail_error, 0);
		return 2;
	}

	duration = hour*3600+60*min;
	if( pvrEditInfo.job.start_time + duration != pvrEditInfo.job.end_time )
	{
		pvrEditInfo.dirty        = 1;
		pvrEditInfo.job.end_time = pvrEditInfo.job.start_time + duration;
		localtime_r(&pvrEditInfo.job.end_time, &pvrEditInfo.end_tm);
		pvr_fillEditMenu(pMenu, PvrEditMenu.baseMenu.pArg);
	}
	return 0;
}

static int pvr_setDate(interfaceMenu_t *pMenu, void* pArg)
{
	int year, mon, day;
	char temp[5];

	temp[2] = 0;
	temp[0] = pvrEditInfo.date_entry.info.date.value[0];
	temp[1] = pvrEditInfo.date_entry.info.date.value[1];
	day = atoi(temp);
	temp[0] = pvrEditInfo.date_entry.info.date.value[2];
	temp[1] = pvrEditInfo.date_entry.info.date.value[3];
	mon = atoi(temp);
	temp[4] = 0;
	memcpy(temp, &pvrEditInfo.date_entry.info.date.value[4], 4);
	year = atoi(temp);
	year -= 1900;
	mon -= 1;
	if( year < 0 || mon < 0 || mon > 11 || day < 0 || day > 31 )
	{
		interface_showMessageBox(_T("ERR_INCORRECT_DATE"), thumbnail_error, 0);
		return 2;
	}
	if( pvrEditInfo.start_tm.tm_mday != day ||
	    pvrEditInfo.start_tm.tm_mon  != mon ||
	    pvrEditInfo.start_tm.tm_year != year)
	{
		pvrEditInfo.dirty             = 1;
		pvrEditInfo.start_tm.tm_year  = year;
		pvrEditInfo.start_tm.tm_mon   = mon;
		pvrEditInfo.start_tm.tm_mday  = day;
		pvrEditInfo.start_tm.tm_isdst = -1;
		pvrEditInfo.job.start_time    = gmktime(&pvrEditInfo.start_tm);
		pvr_adjustEndTime();
		pvr_fillEditMenu(pMenu, PvrEditMenu.baseMenu.pArg);
	}
	return 0;
}

#ifdef PVR_CAN_EDIT_SOURCE
static int pvr_setChannelValue(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	url_desc_t url;
	in_addr_t ip;
	if( value == NULL )
		return 1;
	switch( pvrEditInfo.job.type )
	{
		case pvrJobTypeUnknown:
			break;
#ifdef ENABLE_DVB
		case pvrJobTypeDVB:
		{
			int dvb_channel;
			EIT_service_t *service;

			service = offair_getService( atoi(value) );
			dvb_channel = dvb_getServiceIndex( service );
			if( pvrEditInfo.job.info.dvb.channel != dvb_channel )
			{
				pvrEditInfo.dirty = 1;
				pvrEditInfo.job.info.dvb.service = service;
				pvrEditInfo.job.info.dvb.channel = dvb_channel;
			}
		}
			break;
#endif
		case pvrJobTypeRTP:
			if( parseURL(value, &url) == 0 )
			{
				if(url.protocol == mediaProtoRTP || url.protocol == mediaProtoUDP)
				{
					ip = inet_addr(url.address);
					if( ip == INADDR_NONE || ip == INADDR_ANY )
					{
						interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
						break;
					}
					if( pvrEditInfo.job.info.rtp.desc.proto != url.protocol ||
					    pvrEditInfo.job.info.rtp.desc.port  != url.port ||
					    pvrEditInfo.job.info.rtp.ip.s_addr  != ip )
					{
						pvrEditInfo.dirty = 1;
						pvrEditInfo.job.info.rtp.desc.fmt   = payloadTypeMpegTS;
						pvrEditInfo.job.info.rtp.desc.proto = url.protocol;
						pvrEditInfo.job.info.rtp.desc.port  = url.port;
						pvrEditInfo.job.info.rtp.ip.s_addr  = ip;
					}
				} else
				{
					interface_showMessageBox(_T("ERR_INCORRECT_PROTO"), thumbnail_error, 0);
				}
			} else
			{
				interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
			}
			break;
		case pvrJobTypeHTTP:
			if( strncmp( url, "http://", 7 ) == 0  ||
			    strncmp( url, "https://", 8 ) == 0 ||
			    strncmp( url, "ftp://", 6 ) == 0 )
			{
				strcpy(pvrEditInfo.url, value);
				pvrEditInfo.job.info.http.url = pvrEditInfo.url;
			} else
			{
				interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
			}
			break;
	}
	pvr_fillEditMenu(pMenu, PvrEditMenu.baseMenu.pArg);
	return 0;
}
#endif

static int pvr_setRTPName(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if( value == NULL )
		return 1;
	switch( pvrEditInfo.job.type )
	{
		case pvrJobTypeRTP:
			if( strcmp( pvrEditInfo.job.info.rtp.session_name, value ) )
			{
				pvrEditInfo.dirty = 1;
				strcpy(pvrEditInfo.job.info.rtp.session_name, value);
			}
			break;
		default:
			eprintf("%s: invalid job type %d\n", __FUNCTION__, pvrEditInfo.job.type);
			return 0;
	}
	pvr_fillEditMenu(pMenu, PvrEditMenu.baseMenu.pArg);
	return 0;
}

#ifdef ENABLE_DVB
#ifdef PVR_CAN_EDIT_SOURCE
static char* pvr_getDVBChannel(int field, void *pArg)
{
	if( field == 0 )
	{
		static char buf[4];
		sprintf(buf,"%03d",offair_getIndex(pvrEditInfo.job.info.dvb.channel));
		return buf;
	} else
		return NULL;
}

static int pvr_editDVBChannel(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	str = "";
	interface_getText(pMenu, str, "\\d{3}", pvr_setChannelValue, pvr_getDVBChannel, inputModeDirect, pArg);

	return 0;
}
#endif
#endif

static char* pvr_getRTPChannel(int field, void *pArg)
{
	if( field == 0 )
	{
		static char buf[MAX_FIELD_PATTERN_LENGTH];
		snprintf(buf, sizeof(buf), "%s://%s:%d", proto_toa(pvrEditInfo.job.info.rtp.desc.proto), inet_ntoa(pvrEditInfo.job.info.rtp.ip), pvrEditInfo.job.info.rtp.desc.port );
		return buf;
	} else
		return NULL;
}

static char* pvr_getRTPName(int field, void *pArg)
{
	return field == 0 ? pvrEditInfo.job.info.rtp.session_name : NULL;
}

#ifdef PVR_CAN_EDIT_SOURCE
static int pvr_editRTPChannel(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	str = _T("ENTER_CUSTOM_URL");
	interface_getText(pMenu, str, "\\w+", pvr_setChannelValue, pvr_getRTPChannel, inputModeABC, pArg);

	return 0;
}
#endif

static int pvr_editRTPName(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	str = _T("ENTER_TITLE");
	interface_getText(pMenu, str, "\\w+", pvr_setRTPName, pvr_getRTPName, inputModeABC, pArg);

	return 0;
}

static char* pvr_getHTTPURL(int field, void *pArg)
{
	if (field == 0)
		return pvrEditInfo.url;
	
	return NULL;
}

#ifdef PVR_CAN_EDIT_SOURCE
static int pvr_editHTTPURL(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	str = _T("ENTER_CUSTOM_URL");
	interface_getText(pMenu, str, "\\w+", pvr_setChannelValue, pvr_getHTTPURL, inputModeABC, pArg);
	return 0;
}

static int pvr_showHTTPURL(interfaceMenu_t *pMenu, void* pArg)
{
	interface_showMessageBox( pvr_httpUrl, thumbnail_info, 0 );
	return 0;
}
#endif

static char* pvr_getHTTPName(int field, void *pArg)
{
	return field == 0 ? pvrEditInfo.job.info.http.session_name : NULL;
}

static int pvr_setHTTPName(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if( value == NULL )
		return 1;
	switch( pvrEditInfo.job.type )
	{
		case pvrJobTypeHTTP:
			if( strcmp( pvrEditInfo.job.info.http.session_name, value ) )
			{
				pvrEditInfo.dirty = 1;
				strcpy(pvrEditInfo.job.info.http.session_name, value);
			}
			break;
		default:
			eprintf("%s: invalid job type %d\n", __FUNCTION__, pvrEditInfo.job.type);
			return 0;
	}
	pvr_fillEditMenu(pMenu, PvrEditMenu.baseMenu.pArg);
	return 0;
}

static int pvr_editHTTPName(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	str = _T("ENTER_TITLE");
	interface_getText(pMenu, str, "\\w+", pvr_setHTTPName, pvr_getHTTPName, inputModeABC, pArg);

	return 0;
}

static int pvr_saveJobEntry(interfaceMenu_t *pMenu, void* pArg)
{
	int jobIndex = PVR_INFO_GET_INDEX(pArg);
	int i;
	list_element_t *job_element, *test_element;
	pvrJob_t* curJob;
	char buf[BUFFER_SIZE];

	dprintf("%s: jobIndex = %d\n", __FUNCTION__,jobIndex);
	pvr_jobprint( buf, sizeof(buf), &pvrEditInfo.job );

	if( PVR_JOB_NEW == jobIndex )
	{
		switch( pvr_addJob(&pvrEditInfo.job) )
		{
			case -1:
				interface_showMessageBox( _T("RECORD_JOB_INVALID_SERVICE") , thumbnail_info, 0 );
				eprintf("PVR: failed to add %s: invalid service\n", buf);
				return 1;
			case 1:
				interface_showMessageBox( _T("RECORD_JOB_COLLISION") , thumbnail_info, 0 );
				eprintf("PVR: failed to add %s: collision with existing job\n", buf);
				return 1;
			default:
				eprintf("PVR: added job %s\n", buf);
		}
	} else
	{
		job_element = pvr_jobs;
		for( i = 0; i < jobIndex && job_element != NULL; i++ )
		{
			job_element = job_element->next;
		}
		if( i == jobIndex && job_element != NULL )
		{
			for( test_element = pvr_jobs; test_element != NULL; test_element = test_element->next )
			{
				if( test_element != job_element )
				{
					curJob = (pvrJob_t*)test_element->data;
					if( (curJob->start_time <= pvrEditInfo.job.start_time && pvrEditInfo.job.start_time < curJob->end_time) ||
						(curJob->start_time <  pvrEditInfo.job.end_time   && pvrEditInfo.job.end_time   < curJob->end_time) ||
						(pvrEditInfo.job.start_time <= curJob->start_time && curJob->start_time < pvrEditInfo.job.end_time) )
					{
						interface_showMessageBox( _T("RECORD_JOB_COLLISION") , thumbnail_info, 0 );
						eprintf("PVR: Failed to add insert %s at %d: collision with existing job\n", buf, jobIndex);
						return 5;
					}
					if( curJob->start_time >= pvrEditInfo.job.end_time )
					{
						break;
					}
				}
			}
			eprintf("PVR: replacing job %d with %s\n", jobIndex, buf);
			pvr_jobcpy( job_element->data, &pvrEditInfo.job );
		}
	}
	pvr_exportJobList();
	pvrEditInfo.dirty = 0;
	return interface_menuActionShowMenu(pMenu, (void*)&pvrManageMenu);
}

static int pvr_manageKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	list_element_t *job_element;
	pvrJob_t *job;
#ifdef ENABLE_DVB
	EIT_service_t* service;
#endif
	int i;
	int jobIndex = interface_getSelectedItem((interfaceMenu_t*)&pvrManageMenu)-2; // clear all and header

	if (cmd->command == interfaceCommandGreen)
	{
		pvr_initEditMenu(pMenu, (void*)PVR_INFO_SET(pvrEditInfo.job.type,PVR_JOB_NEW));
		return 0;
	}
	
	if(jobIndex < 0)
	{
		return 1;
	}

	switch(cmd->command)
	{
		case interfaceCommandRed:
			for( i = 0, job_element = pvr_jobs; i < jobIndex && job_element != NULL; i++, job_element = job_element->next );
			if( i == jobIndex && job_element != NULL )
			{
				char buf[BUFFER_SIZE];
				pvr_jobprint( buf, sizeof(buf), job_element->data );
				eprintf("PVR: deleting job %d: %s\n", jobIndex, buf);
				pvr_deleteJob(job_element);
				pvr_exportJobList();
				pvr_fillManageMenu( pMenu, pArg );
				if( pvrManageMenu.baseMenu.selectedItem >= pvrManageMenu.baseMenu.menuEntryCount )
				{
					pvrManageMenu.baseMenu.selectedItem = pvrManageMenu.baseMenu.menuEntryCount <= 1 ? MENU_ITEM_BACK : pvrManageMenu.baseMenu.menuEntryCount - 1;
				}
				interface_displayMenu(1);
			}
			return 0;
		case interfaceCommandYellow:
			pvr_initEditMenu(pMenu, (void*)PVR_INFO_SET(pvrEditInfo.job.type,jobIndex));
			return 0;
		case interfaceCommandBlue:
			job_element = pvr_jobs;
			for( i = 0; i < jobIndex && job_element != NULL; i++ )
			{
				job_element = job_element->next;
			}
			if( i == jobIndex && job_element != NULL )
			{
				job = (pvrJob_t*)job_element->data;
				switch( job->type )
				{
					case pvrJobTypeUnknown: job_element = NULL; break;
					case pvrJobTypeDVB:
#ifdef ENABLE_DVB
						service = dvb_getService(job->info.dvb.channel);
						if( service != NULL && service->schedule != NULL && (i = offair_getServiceIndex(service)) >= 0 )
						{
							EPGRecordMenu.baseMenu.pArg = (void*)i;
							return interface_menuActionShowMenu(interfaceInfo.currentMenu, (void*)&EPGRecordMenu);
						} else
#else
							job_element = NULL;
#endif
						break;
					case pvrJobTypeRTP:
						{
							char rtpurl[SDP_SHORT_FIELD_LENGTH];
							snprintf(rtpurl, sizeof(rtpurl) ,"%s://%s:%d",
								proto_toa(job->info.rtp.desc.proto),
								inet_ntoa(job->info.rtp.ip),
								job->info.rtp.desc.port);
							i = rtp_getChannelNumber( rtpurl );
						}
						if( i != CHANNEL_CUSTOM )
						{
							rtp_initEpgMenu( pMenu, (void*)i );
						} else
							job_element = NULL;
						break;
					case pvrJobTypeHTTP:
						i = rtp_getChannelNumber( job->info.http.url );
						if( i != CHANNEL_CUSTOM )
						{
							rtp_initEpgMenu( pMenu, (void*)i );
						} else
							job_element = NULL;
						break;
				}
			}
			return 0;
			if( job_element == NULL )
			{
				interface_showMessageBox(_T("EPG_UNAVAILABLE"), thumbnail_info, 3000);
			}
		default: ;
	}

	return 1;
}

static int pvr_cancelCurrentJob(pvrJobType_t jobType)
{
	list_element_t *job_element;
	pvrJob_t *job;
	time_t current_time;

	time(&current_time);

	job_element = pvr_jobs;
	while(job_element != NULL)
	{
		job = (pvrJob_t*)job_element->data;
		if( job->type == jobType && job->start_time-current_time > 0 && job->start_time-current_time < notifyTimeout )
		{
			char buf[BUFFER_SIZE];
			pvr_jobprint( buf, sizeof(buf), job );
			eprintf("PVR: cancelling job %s\n", buf);
			job->end_time = time(NULL)-1;
			//pvr_deleteJob( job_element );
			pvr_exportJobList();
			return 0;
		}
		job_element = job_element->next;
	}
	return 1;
}

int pvr_showStopPvr( interfaceMenu_t *pMenu, void* pArg )
{
	int channel = (int)pArg;
	char *str;

	interface_addEvent( pvr_defaultActionEvent, pArg, channel < 0 ? (notifyTimeout-3)*1000 : 10000, 1);

	str = _T( channel < 0 ? "CONFIRM_PVR_STARTS_SOON" : "DVB_WATCH_STOP_PVR" );

	interface_showConfirmationBox(str, thumbnail_question, pvr_confirmStopPvr, pArg);

	return 0;
}

static int pvr_confirmStopPvr(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int channel = (int)pArg;

	switch( cmd->command )
	{
		case interfaceCommandExit:
		case interfaceCommandLeft:
		case interfaceCommandRed:
			displayedWarning = 0;
			interface_removeEvent( pvr_defaultActionEvent, pArg );
			if( channel < 0 )
			{
				pvr_cancelCurrentJob( (pvrJobType_t)(-channel) );
			}
			return 0;
		case interfaceCommandGreen:
		case interfaceCommandOk:
		case interfaceCommandEnter:
			pvr_defaultActionEvent(pArg);
			return 0;
		default: return 1;
	}
}

static int pvr_defaultActionEvent(void *pArg)
{
	interface_removeEvent( pvr_defaultActionEvent, pArg );

	displayedWarning = 0;
	int channel = (int)pArg;
#ifdef ENABLE_DVB
	pvr_requestedDvbChannel = channel;
#endif

	if( interfaceInfo.messageBox.type == interfaceMessageBoxCallback && interfaceInfo.messageBox.pCallback == pvr_confirmStopPvr)
	{
		interface_hideMessageBox();
	}

#ifdef ENABLE_DVB
	if( channel > 0 )
	{
		pvr_stopRecordingDVB(screenMain);
		appControlInfo.pvrInfo.dvb.channel = STBPVR_DVB_CHANNEL_NONE;
		offair_stopVideo( screenMain, 1 );
	} else
#endif
	if( channel == (-pvrJobTypeRTP) )
	{
		pvr_stopRecordingRTP(screenMain);
	} else if( channel == (-pvrJobTypeHTTP) )
	{
		pvr_stopRecordingHTTP(screenMain);
	}

	return 0;
}

static int pvr_openRecord( interfaceMenu_t *pMenu, void* pArg )
{
	int job_index = (int)pArg;
	list_element_t *job_element;
	pvrJob_t *job;
	char buf[PATH_MAX];
	char *str, *name;
	int i,st;
	struct tm *t;

	if( job_index < 0 )
		return 1;

	job_element = pvr_jobs;
	for( i = 0; i < job_index && job_element != NULL; i++ )
	{
		job_element = job_element->next;
	}
	if( job_element == NULL )
		return 1;
	job = (pvrJob_t *)job_element->data;

	sprintf(buf, "%s" STBPVR_FOLDER "/", appControlInfo.pvrInfo.directory);
	str = &buf[strlen(buf)];
	name = pvr_jobGetName( job );
	if( name )
		strcpy( str, name );
	st = strlen(str);
	/* FAT16/32 character case insensitivity workaround */
	for( i = 0; i < st; i++)
	{
		str[i] = tolower(str[i]);
	}
	str = &str[strlen(str)];
	*str = '/';
	str++;
	t = localtime(&job->start_time);
	strftime(str, 30, "%Y-%m-%d/%H-%M/part00.ts", t);
	dprintf("%s: opening '%s'\n", __FUNCTION__, buf);
	if( helperFileExists( buf ) )
	{
		gfx_stopVideoProviders(screenMain);
		strcpy(appControlInfo.mediaInfo.filename, buf);
		media_streamStart();
		return 0;
	} else
	{
		interface_showMessageBox(_T("ERR_FILE_NOT_FOUND"), thumbnail_error, 0);
		return 1;
	}
}

static void pvr_manageMenuGetItemsInfo(pvrManageMenu_t* pvrMenu, int *fh, int *itemHeight, int *maxVisibleItems)
{
	pgfx_font->GetHeight(pgfx_font, fh);

	*itemHeight = interfaceInfo.paddingSize+ *fh;
	if ( (*itemHeight) * pvrMenu->baseMenu.menuEntryCount > interfaceInfo.clientHeight-interfaceInfo.paddingSize*2 )
	{
		*maxVisibleItems = interfaceInfo.clientHeight/(*itemHeight);
	} else
	{
		*maxVisibleItems = pvrMenu->baseMenu.menuEntryCount;
	}
}

static int pvr_manageMenuProcessCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd)
{
	pvrManageMenu_t *pvrMenu = (pvrManageMenu_t*)pMenu;
	int fh, itemHeight, maxVisibleItems, n;

	if ( cmd->command == interfaceCommandPageUp )
	{
		//dprintf("%s: up\n", __FUNCTION__);
		pvr_manageMenuGetItemsInfo(pvrMenu, &fh, &itemHeight,&maxVisibleItems);
		if ( pvrMenu->baseMenu.selectedItem == MENU_ITEM_BACK && pvrMenu->baseMenu.pParentMenu != NULL && pvrMenu->baseMenu.pParentMenu->pParentMenu != NULL )
		{
			n = MENU_ITEM_MAIN;
		} else if ( pvrMenu->baseMenu.menuEntryCount <= maxVisibleItems )
		{
			n = 0;
		}else
		{
			n = pvrMenu->baseMenu.selectedItem-(maxVisibleItems-1);
			if(n < 0)
				n = 0;
		}
		while ( n >= 0 && pvrMenu->baseMenu.menuEntry[n].isSelectable == 0 )
		{
			n--;
		};
		//printf("%s: n=%d\n", __FUNCTION__, n);
		if ((pvrMenu->baseMenu.pParentMenu != NULL && n < MENU_ITEM_MAIN) ||
			(pvrMenu->baseMenu.pParentMenu == NULL && n < 0))
		{
			//printf("%s: loop\n", __FUNCTION__);
			n = pvrMenu->baseMenu.menuEntryCount-1;
			while ( n >= 0 && pvrMenu->baseMenu.menuEntry[n].isSelectable == 0 )
			{
				//printf("%s: loop n=%d\n", __FUNCTION__, n);
				n--;
			};
		}
		if ( pvrMenu->baseMenu.pParentMenu != NULL )
		{
			//pvrMenu->baseMenu.selectedItem = pvrMenu->baseMenu.selectedItem > MENU_ITEM_MAIN ? n : pvrMenu->baseMenu.selectedItem;
			pvrMenu->baseMenu.selectedItem = n;
			if ( pvrMenu->baseMenu.selectedItem == MENU_ITEM_BACK && pvrMenu->baseMenu.pParentMenu->pParentMenu == NULL )
			{
				pvrMenu->baseMenu.selectedItem = MENU_ITEM_MAIN;
			}
		} else
		{
			pvrMenu->baseMenu.selectedItem = n >= 0 ? n : pvrMenu->baseMenu.selectedItem;
		}
		interface_displayMenu(1);
	} else if ( cmd->command == interfaceCommandPageDown )
	{
		pvr_manageMenuGetItemsInfo(pvrMenu, &fh, &itemHeight,&maxVisibleItems);
		//dprintf("%s: down\n", __FUNCTION__);
		if ( pvrMenu->baseMenu.selectedItem == MENU_ITEM_MAIN && pvrMenu->baseMenu.pParentMenu != NULL && pvrMenu->baseMenu.pParentMenu->pParentMenu != NULL )
		{
			n = MENU_ITEM_BACK;
		} else
		{
			if ( pvrMenu->baseMenu.menuEntryCount <= maxVisibleItems )
			{
				n = pvrMenu->baseMenu.menuEntryCount-1;
			}else
			{
				n = pvrMenu->baseMenu.selectedItem+(maxVisibleItems-1);
				if( n >= pvrMenu->baseMenu.menuEntryCount )
					n = pvrMenu->baseMenu.menuEntryCount-1;
			}
			while ( n < pvrMenu->baseMenu.menuEntryCount && pvrMenu->baseMenu.menuEntry[n].isSelectable == 0 )
			{
				n++;
			};
		}
		if (n >= pvrMenu->baseMenu.menuEntryCount)
		{
			//dprintf("%s: loop\n", __FUNCTION__);
			if ( pvrMenu->baseMenu.pParentMenu != NULL )
			{
				n = MENU_ITEM_MAIN;
			} else
			{
				//dprintf("%s: zero\n", __FUNCTION__);
				n = 0;
				while ( n < pvrMenu->baseMenu.menuEntryCount && pvrMenu->baseMenu.menuEntry[n].isSelectable == 0 )
				{
					n++;
				};
			}
		}
		//dprintf("%s: new n = %d of %d\n", __FUNCTION__, n, pvrMenu->baseMenu.menuEntryCount);
		//dprintf("%s: n = %d of %d\n", __FUNCTION__, n, pvrMenu->baseMenu.menuEntryCount);
		pvrMenu->baseMenu.selectedItem = n;
		interface_displayMenu(1);
	} else
	{
		interface_MenuDefaultProcessCommand(&pvrMenu->baseMenu, cmd);
	}
	return 0;
}

static void pvr_manageMenuDisplay(interfaceMenu_t *pMenu)
{
	int fh, x, y, h, l, i, r,g,b,a;
	pvrManageMenu_t *pvrMenu = (pvrManageMenu_t *)pMenu;
	int itemHeight, maxVisibleItems, itemOffset, itemDisplayIndex;
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str;
	int job_index;
	list_element_t *job_element;
	pvrJob_t *job;
	struct tm *t;
	time_t cur_time;
	l = 0;

	//dprintf("%s: displaying EPG menu (menu.sel=%d ri.cur=%d ri.hi=%d ri.he=%p)\n", __FUNCTION__,EPGRecordMenu.selectedItem, recordInfo.currentService, recordInfo.highlightedService, recordInfo.highlightedEvent);
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
	if (interfaceInfo.currentMenu->logo > 0 && interfaceInfo.currentMenu->logoX > 0)
	{
		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[interfaceInfo.currentMenu->logo],
			interfaceInfo.currentMenu->logoX, interfaceInfo.currentMenu->logoY, interfaceInfo.currentMenu->logoWidth, interfaceInfo.currentMenu->logoHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0);
	}
	
	interface_displayClock( 0 /* not detached */ );

	pgfx_font->GetHeight(pgfx_font, &fh);

	itemHeight = interfaceInfo.paddingSize+fh;

	/* Check if menu is empty */
	if( pvrMenu->baseMenu.menuEntryCount < 3 )
	{
		//if ( pvrMenu->listMenuType == interfaceListMenuBigThumbnail || pvrMenu->listMenuType == interfaceListMenuNoThumbnail )
		x = interfaceInfo.clientX+interfaceInfo.paddingSize*2;//+INTERFACE_ARROW_SIZE;
		y = interfaceInfo.clientY+fh+interfaceInfo.paddingSize;

		tprintf("%c%c%s\t\t\t|%d\n", ' ', '-', pvrMenu->baseMenu.menuEntry[0].info, pvrMenu->baseMenu.menuEntry[0].thumbnail);

		gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_DISABLED_RED, INTERFACE_BOOKMARK_DISABLED_GREEN, INTERFACE_BOOKMARK_DISABLED_BLUE, INTERFACE_BOOKMARK_DISABLED_ALPHA, x, y, pvrMenu->baseMenu.menuEntry[0].info, 0, 0);
		return;
	}

	/* Non-empty manage menu consists of at least three items:
	* Clear all
	* [ | Header | ]
	* Entry 1
	* ...
	* and so on.
	* Header is always visible on the screen
	*/
	if ( itemHeight * pvrMenu->baseMenu.menuEntryCount > interfaceInfo.clientHeight-interfaceInfo.paddingSize*2 )
	{
		maxVisibleItems = interfaceInfo.clientHeight/itemHeight;
	} else
	{
		maxVisibleItems = pvrMenu->baseMenu.menuEntryCount;
	}

	if ( pvrMenu->baseMenu.selectedItem > maxVisibleItems/2 )
	{
		itemOffset = pvrMenu->baseMenu.selectedItem - maxVisibleItems/2;
		itemOffset = itemOffset > (pvrMenu->baseMenu.menuEntryCount-maxVisibleItems+(itemOffset>1)) ? pvrMenu->baseMenu.menuEntryCount-maxVisibleItems+(itemOffset>1) : itemOffset;
	} else
	{
		itemOffset = 0;
	}

	pvrMenu->event_w = interfaceInfo.clientWidth - 4*PVR_CELL_BORDER_WIDTH - PVR_MENU_ICON_WIDTH - pvrMenu->channel_w - pvrMenu->date_w - pvrMenu->start_w - pvrMenu->end_w;
	if (maxVisibleItems < pMenu->menuEntryCount )
		pvrMenu->event_w -= INTERFACE_SCROLLBAR_WIDTH+2*interfaceInfo.paddingSize;

	itemDisplayIndex = 0;
	if( itemOffset == 0 )
	{
		/* Clear all entry */
		if ( 0 == pvrMenu->baseMenu.selectedItem )
		{
			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
			// selection rectangle
			x = interfaceInfo.clientX+interfaceInfo.paddingSize;
			y = interfaceInfo.clientY+interfaceInfo.paddingSize;
			gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, x, y, interfaceInfo.clientWidth - 2 * interfaceInfo.paddingSize - (maxVisibleItems < pvrMenu->baseMenu.menuEntryCount ? INTERFACE_SCROLLBAR_WIDTH + interfaceInfo.paddingSize : 0), fh + interfaceInfo.paddingSize);
		}
		r = INTERFACE_BOOKMARK_RED;
		g = INTERFACE_BOOKMARK_GREEN;
		b = INTERFACE_BOOKMARK_BLUE;
		a = INTERFACE_BOOKMARK_ALPHA;
		x = interfaceInfo.clientX+interfaceInfo.paddingSize*2;//+INTERFACE_ARROW_SIZE;
		y = interfaceInfo.clientY+interfaceInfo.paddingSize+fh;

		tprintf("%c%c%s\t\t\t|%d\n", i == pvrMenu->baseMenu.selectedItem ? '>' : ' ', pvrMenu->baseMenu.menuEntry[0].isSelectable ? ' ' : '-', pvrMenu->baseMenu.menuEntry[0].info, pvrMenu->baseMenu.menuEntry[0].thumbnail);

		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, pvrMenu->baseMenu.menuEntry[0].info, 0, 0 == pvrMenu->baseMenu.selectedItem);

		itemDisplayIndex = 1;
	}

	/* Header element
	 * ->| i | Channel | <~ Event ~> | Date | Start | End |<- clientWidth
	 */
	{
		r = INTERFACE_BOOKMARK_RED;
		g = INTERFACE_BOOKMARK_GREEN;
		b = INTERFACE_BOOKMARK_BLUE;
		a = INTERFACE_BOOKMARK_ALPHA;

		x = interfaceInfo.clientX;
		y = interfaceInfo.clientY+(interfaceInfo.paddingSize+fh)*itemDisplayIndex + interfaceInfo.paddingSize;
		i = y+fh;
		h = (interfaceInfo.clientHeight/itemHeight - itemDisplayIndex)*(fh + interfaceInfo.paddingSize);
		l = (interfaceInfo.paddingSize - PVR_CELL_BORDER_HEIGHT)/2;

		gfx_drawRectangle(DRAWING_SURFACE, PVR_CELL_BORDER_RED, PVR_CELL_BORDER_GREEN, PVR_CELL_BORDER_BLUE, PVR_CELL_BORDER_ALPHA, x, i+l, interfaceInfo.clientWidth, PVR_CELL_BORDER_HEIGHT);
		x += PVR_MENU_ICON_WIDTH;

		gfx_drawRectangle(DRAWING_SURFACE, PVR_CELL_BORDER_RED, PVR_CELL_BORDER_GREEN, PVR_CELL_BORDER_BLUE, PVR_CELL_BORDER_ALPHA, x, y, PVR_CELL_BORDER_WIDTH, h);
		x += PVR_CELL_BORDER_WIDTH;
		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, i, _T("CHANNEL"), 0, 0);
		x += pvrMenu->channel_w;

		gfx_drawRectangle(DRAWING_SURFACE, PVR_CELL_BORDER_RED, PVR_CELL_BORDER_GREEN, PVR_CELL_BORDER_BLUE, PVR_CELL_BORDER_ALPHA, x, y, PVR_CELL_BORDER_WIDTH, h);
		x += PVR_CELL_BORDER_WIDTH;
		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, i, _T("PROGRAMME"), 0, 0);
		x += pvrMenu->event_w;

		gfx_drawRectangle(DRAWING_SURFACE, PVR_CELL_BORDER_RED, PVR_CELL_BORDER_GREEN, PVR_CELL_BORDER_BLUE, PVR_CELL_BORDER_ALPHA, x, y, PVR_CELL_BORDER_WIDTH, h);
		x += PVR_CELL_BORDER_WIDTH;
		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, i, _T("RECORD_TIME"), 0, 0);
		x += pvrMenu->date_w;

		l += fh;
		y += l;
		h -= l;
		gfx_drawRectangle(DRAWING_SURFACE, PVR_CELL_BORDER_RED, PVR_CELL_BORDER_GREEN, PVR_CELL_BORDER_BLUE, PVR_CELL_BORDER_ALPHA, x, y, PVR_CELL_BORDER_WIDTH, h);
		x += PVR_CELL_BORDER_WIDTH+pvrMenu->start_w;
		gfx_drawRectangle(DRAWING_SURFACE, PVR_CELL_BORDER_RED, PVR_CELL_BORDER_GREEN, PVR_CELL_BORDER_BLUE, PVR_CELL_BORDER_ALPHA, x, y, PVR_CELL_BORDER_WIDTH, h);
	}

	i = itemOffset > 2 ? itemOffset - 2 : 0;
	job_element = pvr_jobs;
	for( job_index = 0; job_index < i; job_index++ )
	{
		job_element = job_element->next;
	}

	for ( i = itemOffset > 2 ? itemOffset : 2; job_element != NULL && i<itemOffset+maxVisibleItems-(itemOffset>1); i++ )
	{
		if ( pvrMenu->baseMenu.menuEntry[i].type == interfaceMenuEntryText )
		{
			itemDisplayIndex++;
			if ( i == pvrMenu->baseMenu.selectedItem )
			{
				DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
				// selection rectangle
				x = interfaceInfo.clientX;
				y = interfaceInfo.clientY+(interfaceInfo.paddingSize+fh)*itemDisplayIndex + interfaceInfo.paddingSize;
				gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, x, y, interfaceInfo.clientWidth - (maxVisibleItems < pvrMenu->baseMenu.menuEntryCount ? INTERFACE_SCROLLBAR_WIDTH + interfaceInfo.paddingSize : 0), fh + interfaceInfo.paddingSize);
			}
			job = (pvrJob_t *)job_element->data;
			//dprintf("%s: draw text\n", __FUNCTION__);
			r = INTERFACE_BOOKMARK_RED;
			g = INTERFACE_BOOKMARK_GREEN;
			b = INTERFACE_BOOKMARK_BLUE;
			a = INTERFACE_BOOKMARK_ALPHA;

			x = interfaceInfo.clientX;
			y = interfaceInfo.clientY+(interfaceInfo.paddingSize+fh)*(itemDisplayIndex+1);
			/* Status icon
			 * 0 = can't record; 1 = waiting; 2 = recording; 3 = recorded;
			 */
			if( pvr_storageReady == 0 )
				l = 0;
			else
			{
				time(&cur_time);
				if( cur_time > job->end_time )
					l = 3;
				else if ( cur_time >= job->start_time )
					l = 2;
				else l = 1;
			}
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "icons_pvr_status.png", x, y, PVR_MENU_ICON_WIDTH, PVR_MENU_ICON_HEIGHT, 0, l, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignLeft|interfaceAlignBottom);
			x += PVR_MENU_ICON_WIDTH;

			buf[0] = 0;
			switch( job->type )
			{
				case pvrJobTypeUnknown: break;
				case pvrJobTypeDVB:
#ifdef ENABLE_DVB
					strncpy(  buf, dvb_getTempServiceName(job->info.dvb.channel), sizeof(buf));
#endif
					break;
				case pvrJobTypeRTP:
					snprintf( buf, sizeof(buf), "%s://%s:%d", proto_toa( job->info.rtp.desc.proto ), inet_ntoa( job->info.rtp.ip ), job->info.rtp.desc.port );
					break;
				case pvrJobTypeHTTP:
					strncpy(  buf, job->info.http.url, sizeof(buf) );
					break;
			}
			if( buf[0] )
			{
				l = getMaxStringLength(buf, pvrMenu->channel_w);
				buf[l] = 0;
				gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, buf, 0, 1);
			}
			x += PVR_CELL_BORDER_WIDTH + pvrMenu->channel_w;

			str = NULL;
			switch( job->type )
			{
				case pvrJobTypeUnknown:
					str = NULL;
					break;
				case pvrJobTypeDVB:
#ifdef ENABLE_DVB
					if( job->info.dvb.event_id != CHANNEL_CUSTOM )
						str = dvb_getEventName( job->info.dvb.service, job->info.dvb.event_id );
#else
					str = NULL;
#endif
					break;
				case pvrJobTypeRTP:
					str = job->info.rtp.session_name;
					break;
				case pvrJobTypeHTTP:
					str = job->info.http.session_name;
					break;
			}
			if( str != NULL )
			{
				strcpy(buf, str);
				l = getMaxStringLength(buf, pvrMenu->event_w);
				buf[l] = 0;
				str = buf;
			} else
				str = _T("NOT_AVAILABLE_SHORT");

			gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, str, 0, 1);
			x += PVR_CELL_BORDER_WIDTH + pvrMenu->event_w;

			t = localtime(&job->start_time);
			strftime( buf, 15, _T("%y.%m.%d"), t);
			gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, buf, 0, 1);
			x += PVR_CELL_BORDER_WIDTH + pvrMenu->date_w;

			strftime( buf, 15, "%H:%M", t);
			gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, buf, 0, 1);
			x += PVR_CELL_BORDER_WIDTH + pvrMenu->start_w;

			t = localtime(&job->end_time);
			strftime( buf, 15, "%H:%M", t);
			gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, buf, 0, 1);
			/*
			x = interfaceInfo.clientX+interfaceInfo.paddingSize*2;
			y = interfaceInfo.clientY+(interfaceInfo.paddingSize+fh)*(itemDisplayIndex+1);

			tprintf("%c%c%s\t\t\t|%d\n", i == pvrMenu->baseMenu.selectedItem ? '>' : ' ', pvrMenu->baseMenu.menuEntry[i].isSelectable ? ' ' : '-', pvrMenu->baseMenu.menuEntry[i].info, pvrMenu->baseMenu.menuEntry[i].thumbnail);

			gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, pvrMenu->baseMenu.menuEntry[i].info, 0, i == pvrMenu->baseMenu.selectedItem);*/
			job_element = job_element->next;
		}
	}

	/* draw scroll bar if needed */
	if ( maxVisibleItems < pMenu->menuEntryCount )
	{
		int width, height;
		float step;

		step = (float)(interfaceInfo.clientHeight - interfaceInfo.paddingSize*2 - INTERFACE_SCROLLBAR_WIDTH*2)/(float)(pvrMenu->baseMenu.menuEntryCount);

		x = interfaceInfo.clientX + interfaceInfo.clientWidth - interfaceInfo.paddingSize - INTERFACE_SCROLLBAR_WIDTH;
		y = interfaceInfo.clientY + interfaceInfo.paddingSize + INTERFACE_SCROLLBAR_WIDTH + step*itemOffset;
		width = INTERFACE_SCROLLBAR_WIDTH;
		height = step * maxVisibleItems;

		//dprintf("%s: step = %f\n", __FUNCTION__, step);

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

		//gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 200, 200, x, interfaceInfo.clientY + interfaceInfo.paddingSize*2 + INTERFACE_SCROLLBAR_WIDTH, width, step*pvrMenu->baseMenu.menuEntryCount);

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_RED, INTERFACE_SCROLLBAR_COLOR_GREEN, INTERFACE_SCROLLBAR_COLOR_BLUE, INTERFACE_SCROLLBAR_COLOR_ALPHA, x, y, width, height);
		interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_LT_RED, INTERFACE_SCROLLBAR_COLOR_LT_GREEN, INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA, x, y, width, height, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideLeft);
		interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_DK_RED, INTERFACE_SCROLLBAR_COLOR_DK_GREEN, INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA, x, y, width, height, interfaceInfo.borderWidth, interfaceBorderSideBottom|interfaceBorderSideRight);

		y = interfaceInfo.clientY + interfaceInfo.paddingSize;
		height = INTERFACE_SCROLLBAR_WIDTH;

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_RED, INTERFACE_SCROLLBAR_COLOR_GREEN, INTERFACE_SCROLLBAR_COLOR_BLUE, INTERFACE_SCROLLBAR_COLOR_ALPHA, x, y, width, height);
		interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_LT_RED, INTERFACE_SCROLLBAR_COLOR_LT_GREEN, INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA, x, y, width, height, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideLeft);
		interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_DK_RED, INTERFACE_SCROLLBAR_COLOR_DK_GREEN, INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA, x, y, width, height, interfaceInfo.borderWidth, interfaceBorderSideBottom|interfaceBorderSideRight);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "arrows.png", x+INTERFACE_SCROLLBAR_WIDTH/2, y+INTERFACE_SCROLLBAR_WIDTH/2, INTERFACE_SCROLLBAR_ARROW_SIZE, INTERFACE_SCROLLBAR_ARROW_SIZE, 0, 0, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle);

		y = interfaceInfo.clientY + interfaceInfo.clientHeight - interfaceInfo.paddingSize - INTERFACE_SCROLLBAR_WIDTH;

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_RED, INTERFACE_SCROLLBAR_COLOR_GREEN, INTERFACE_SCROLLBAR_COLOR_BLUE, INTERFACE_SCROLLBAR_COLOR_ALPHA, x, y, width, height);
		interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_LT_RED, INTERFACE_SCROLLBAR_COLOR_LT_GREEN, INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA, x, y, width, height, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideLeft);
		interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_SCROLLBAR_COLOR_DK_RED, INTERFACE_SCROLLBAR_COLOR_DK_GREEN, INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA, x, y, width, height, interfaceInfo.borderWidth, interfaceBorderSideBottom|interfaceBorderSideRight);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "arrows.png", x+INTERFACE_SCROLLBAR_WIDTH/2, y+INTERFACE_SCROLLBAR_WIDTH/2, INTERFACE_SCROLLBAR_ARROW_SIZE, INTERFACE_SCROLLBAR_ARROW_SIZE, 0, 1, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle);

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
	}
}

#endif // STBPNX
#endif // ENABLE_PVR
