#ifdef ENABLE_DLNA

/*
 dlna.c

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

#include "dlna.h"

#include "debug.h"
#include "sem.h"
#include "l10n.h"
#include "media.h"
#include "rtsp.h"

#if defined(WIN32)
	#ifndef MICROSTACK_NO_STDAFX
	#include "stdafx.h"
	#endif
	#define _CRTDBG_MAP_ALLOC
	#include <TCHAR.h>
#endif

#if defined(WINSOCK2)
	#include <winsock2.h>
	#include <ws2tcpip.h>
#elif defined(WINSOCK1)
	#include <winsock.h>
	#include <wininet.h>
#endif

#include "ILibParsers.h"
#include "MediaServerCP_ControlPoint.h"
#include "MediaServerCP_ControlPoint.h"

#include <semaphore.h>

#include "DMR_MicroStack.h"
#include "ILibWebServer.h"
#include "ILibAsyncSocket.h"

#include "ILibThreadPool.h"
#include "FilteringBrowser.h"
// hack
extern struct FB_FilteringBrowserManager	*FB_TheManager;
#include "CdsObject.h"
#include "CdsDidlSerializer.h"
#include "DLNAProtocolInfo.h"
#include "DMR.h"
#include "FilteringBrowser.h"

#if defined(WIN32)
	#include <crtdbg.h>
#endif

#include <pthread.h>

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

#ifdef ENABLE_DLNA_DMR
static DMR dmrObject;
static const char* ProtocolInfoList[] = {
	/* Image Formats */
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM;DLNA.ORG_FLAGS=31200000000000000000000000000000",
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED;DLNA.ORG_FLAGS=3120000000000000000000000000000",
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG;DLNA.ORG_FLAGS=3120000000000000000000000000000",
#ifdef INCLUDE_FEATURE_PLAYSINGLEURI
	"playsingle-http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG;DLNA.ORG_FLAGS=2120000000000000000000000000000",
#endif
	/* Audio Formats */
	"http-get:*:audio/L16;channels=1;rate=44100:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=3120000000000000000000000000000",
	"http-get:*:audio/L16;channels=2;rate=44100:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=3120000000000000000000000000000",
	"http-get:*:audio/L16;channels=1;rate=48000:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=3120000000000000000000000000000",
	"http-get:*:audio/L16;channels=2;rate=48000:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=3120000000000000000000000000000",
#ifdef INCLUDE_FEATURE_PLAYSINGLEURI
	"playsingle-http-get:*:audio/L16;channels=1;rate=44100:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:audio/L16;channels=2;rate=44100:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:audio/L16;channels=1;rate=48000:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:audio/L16;channels=2;rate=48000:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=2120000000000000000000000000000",
#endif
	/* VIDEO: PS */
"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC;DLNA.ORG_FLAGS=3120000000000000000000000000000",
#ifdef INCLUDE_FEATURE_PLAYSINGLEURI
	"playsingle-http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC;DLNA.ORG_FLAGS=2120000000000000000000000000000",
#endif
    /* VIDEO: TS NA */
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA;DLNA.ORG_FLAGS=3120000000000000000000000000000",
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA_T;DLNA.ORG_FLAGS=3120000000000000000000000000000",
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA;DLNA.ORG_FLAGS=3120000000000000000000000000000",
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA_T;DLNA.ORG_FLAGS=3120000000000000000000000000000",
#ifdef INCLUDE_FEATURE_PLAYSINGLEURI
	"playsingle-http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA_T;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA;DLNA.ORG_FLAGS=2120000000000000000000000000000",
	"playsingle-http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA_T;DLNA.ORG_FLAGS=2120000000000000000000000000000",
#endif
	"\0"
};
#endif

static void *MicroStackChain;
static void *ILib_Pool;
static void *DMP_Browser = NULL;

static char *protocolInfo;

static void *ILib_Monitor;
static int ILib_IPAddressLength;
static int *ILib_IPAddressList;

static pthread_t dlnaWorkerHandle = 0;

static pmysem_t  dlna_semaphore;

static struct CdsObject* parentContext = NULL;
static char parentObjectID[256] = "";

static void *childContexts = NULL;

/************************************************
* EXPORTED DATA                                 *
*************************************************/

interfaceListMenu_t BrowseServersMenu;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int dlna_fillServerBrowserMenu(interfaceMenu_t *pMenu, void* pArg);

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

static int dlna_stream_change(interfaceMenu_t *pMenu, void* pArg)
{
	struct CdsObject* cdsObj = (struct CdsObject*)pArg;

	if (cdsObj != NULL)
	{
		struct CdsResource *res;

		dprintf("DLNA: start media %s (%08X)\n", cdsObj->Title, cdsObj->Res);

		res = cdsObj->Res;

		while (res != NULL)
		{
			if (res->Value != NULL)
			{
				eprintf("DLNA: Trying URL %s\n", res->Value);
				if (strncasecmp(res->Value, "http", 4) == 0)
				{
					appControlInfo.playbackInfo.playlistMode = playlistModeDLNA;
					appControlInfo.playbackInfo.streamSource = streamSourceDLNA;
					media_playURL(screenMain, res->Value, NULL, resource_thumbnails[thumbnail_workstation_video]);
					return 0;
				} else
#ifdef ENABLE_VOD
				if (strncasecmp(res->Value, "rtsp", 4) == 0)
				{
					appControlInfo.playbackInfo.playlistMode = playlistModeDLNA;
					appControlInfo.playbackInfo.streamSource = streamSourceDLNA;
					rtsp_playURL(screenMain, res->Value, NULL, resource_thumbnails[thumbnail_workstation_video]);
					return 0;
				} else
#endif
				{
					eprintf("DLNA: Media URL is not supported!\n");
				}
			}
			res = res->Next;
		}
	}

	eprintf("DLNA: Service has no supported URLs!\n");

	return 1;
}

static void dlna_IPAddressMonitor(void *data)
{
	int length;
	int *list;
	
	length = ILibGetLocalIPAddressList(&list);
	if(length!=ILib_IPAddressLength || memcmp((void*)list,(void*)ILib_IPAddressList,sizeof(int)*length)!=0)
	{
		
		FB_NotifyIPAddressChange(DMP_Browser);
		
		free(ILib_IPAddressList);
		ILib_IPAddressList = list;
		ILib_IPAddressLength = length;
	}
	else
	{
		free(list);
	}
		
	ILibLifeTime_Add(ILib_Monitor,NULL,4,(void*)&dlna_IPAddressMonitor,NULL);
}

static void dlna_onQuit(void *data)
{

	mysem_get(dlna_semaphore);

	if(ILib_Pool!=NULL)
	{
		dprintf("DLNA: Stopping Thread Pool...\r\n");
		ILibThreadPool_Destroy(ILib_Pool);
		dprintf("DLNA: Thread Pool Destroyed...\r\n");
	}
	
	if(MicroStackChain!=NULL)
	{
		ILibStopChain(MicroStackChain);
		MicroStackChain = NULL;
	}

	mysem_release(dlna_semaphore);
}

static void* dlna_poolThread(void *args)
{
	ILibThreadPool_AddThread(ILib_Pool);
	return(0);
}

static int DMP_OnFB_ExamineObject(FB_Object fb, struct CdsObject *cds_obj)
{
	dprintf("DLNA: Examine object %08X = %s\n", cds_obj, cds_obj->Title);

	return 1;
}

static void DMP_OnFB_Servers (int RemoveFlag, char * udn)
{
	dprintf("DLNA: DMP_OnFB_Servers\n");

	if(RemoveFlag==0)
	{
		dprintf("DLNA: Found Media Server: %s\r\n",FB_GetDevice(udn)->FriendlyName );
	}
	else
	{
		dprintf("DLNA: Removed Media Server: %s\r\n",FB_GetDevice(udn)->FriendlyName );
	}
	if(MicroStackChain!=NULL && !ILibIsChainBeingDestroyed(MicroStackChain) && DMP_Browser!=NULL)
	{
		if (FB_ContextIsAllRoots(DMP_Browser))
		{
			dprintf("DLNA: DMP_OnFB_Servers refresh\n");
			FB_Refresh(DMP_Browser);
		} 
	}

	dprintf("DLNA: DMP_OnFB_Servers out\n");
}

static void DMP_OnFB_Results (void *fbObj, struct FB_ResultsInfo* results)
{
	char *str;
	struct FB_ResultsInfo* resultsInfo = FB_GetResults(fbObj);
	int icount = 0;

	dprintf("DLNA: DMP_OnFB_Results\n");

	mysem_get(dlna_semaphore);

	dprintf("DLNA: Got results on %d pages err %d cnt %d ptr %p!\n",
	   FB_GetPageCount(DMP_Browser),
	   resultsInfo->ErrorCode,
	   ILibLinkedList_GetCount(resultsInfo->LinkedList),
	   resultsInfo->LinkedList);

	interface_clearMenuEntries((interfaceMenu_t*)&BrowseServersMenu);

	if (!FB_ContextIsAllRoots(DMP_Browser))
	{
		dprintf("DLNA: We're not at top\n");
		interface_addMenuEntry((interfaceMenu_t *)&BrowseServersMenu, "..", 
		                       dlna_fillServerBrowserMenu, parentContext, thumbnail_folder);
		icount++;
	}

	void *node = ILibLinkedList_GetNode_Head(resultsInfo->LinkedList);

	if (node == NULL)
	{
		str = FB_ContextIsAllRoots(DMP_Browser) ? _T("NO_MEDIA_SERVERS") : _T("NO_MEDIA_FILES");
		interface_addMenuEntryDisabled((interfaceMenu_t *)&BrowseServersMenu, str, thumbnail_info);
		if (icount == 0)
		{
			interface_setSelectedItem((interfaceMenu_t *)&BrowseServersMenu, MENU_ITEM_BACK);
		}
	}

	while (node != NULL)
	{
		int is_media, media_icon = thumbnail_folder;
		struct CdsObject* cdsObj = (struct CdsObject*)ILibLinkedList_GetDataFromNode(node);

		dprintf("DLNA: Object %08X\n", cdsObj);

		dprintf("DLNA: Object %s cached for %d\n", cdsObj->ID, cdsObj->CpInfo.ServiceObject->Parent->CacheTime);

		if (FB_ContextIsAllRoots(DMP_Browser))
		{
			str = cdsObj->CpInfo.ServiceObject->Parent->FriendlyName;
		} else
		{
			str = cdsObj->Title;
		}
		CDS_ObjRef_Add(cdsObj);
		dprintf("DLNA: Add child context\n");
		ILibLinkedList_Lock(childContexts);
		ILibLinkedList_AddTail(childContexts, cdsObj);
		ILibLinkedList_UnLock(childContexts);
		is_media = cdsObj->MediaClass & CDS_CLASS_MASK_CONTAINER ? 0 : 1;
		if (is_media)
		{
			switch (cdsObj->MediaClass & CDS_CLASS_MASK_MAJOR)
			{
				case CDS_CLASS_MASK_MAJOR_IMAGEITEM:
					media_icon = thumbnail_workstation_image; break;
				case CDS_CLASS_MASK_MAJOR_AUDIOITEM:
					media_icon = thumbnail_workstation_audio; break;
				case CDS_CLASS_MASK_MAJOR_VIDEOITEM:
					media_icon = thumbnail_workstation_video; break;
				default:
					media_icon = thumbnail_workstation_video;
			}
		}
		icount = interface_addMenuEntry2(_M &BrowseServersMenu, str, !is_media || cdsObj->Res != NULL,
			is_media ? dlna_stream_change : dlna_fillServerBrowserMenu, cdsObj,
			FB_ContextIsAllRoots(DMP_Browser) ? thumbnail_workstation : media_icon);
		
		if ((strcmp(cdsObj->ID, "0") == 0 && strcmp(cdsObj->CpInfo.ServiceObject->Parent->UDN, parentObjectID) == 0) ||
			strcmp(cdsObj->ID, parentObjectID) == 0)
		{
			dprintf("DLNA: Selecting %s\n", str);
			interface_setSelectedItem((interfaceMenu_t *)&BrowseServersMenu, icount-1);
		}

		dprintf("DLNA: Next node\n");
		node = ILibLinkedList_GetNextNode(node);
	}

	FB_DestroyResults(resultsInfo);

	if (interfaceInfo.currentMenu == (interfaceMenu_t *)&BrowseServersMenu)
	{
		interface_displayMenu(1);
	}

	mysem_release(dlna_semaphore);
}

static void *dlna_workerThread(void *pArg)
{
	// Blocking call
	ILibStartChain(MicroStackChain);

	// Cleanup
	free(protocolInfo);
	free(ILib_IPAddressList);

	eprintf("DLNA: Worker done.\n");

	return NULL;
}

#ifdef ENABLE_DLNA_DMR
static char* dlna_buildProtocolInfo(const char* infoList[])
{
	int counter;
	int length = 0;
	char* result = NULL;
	char* p;

	if(infoList == NULL)
	{
		return NULL;
	}

	counter = 0;
	p = (char*)infoList[counter];
	while(p[0] != '\0')
	{
		length += ((int)strlen(p) + 1);
		p = (char*)infoList[++counter];
	}

	result = (char*)malloc(length);
	result[0] = 0;

	counter = 0;
	p = (char*)infoList[counter];
	while(p[0] != '\0')
	{
		if(result[0] != '\0')
		{
			strcat(result, ",");
		}
		strcat(result, p);
		p = (char*)infoList[++counter];
	}

	return result;
}

static int dlna_onSetAVTransportURI(DMR instance, void* session, char* uri, struct CdsObject* data)
{
	dprintf("%s: %s\n", __FUNCTION__, uri);

	return 0;
}
#endif // #ifdef ENABLE_DLNA_DMR

static void dlna_createBrowser(void)
{
	struct FB_PageInfo DMP_pi;

	dprintf("DLNA: Create browser\n");
	DMP_Browser = FB_CreateFilteringBrowser(MicroStackChain, ILib_Pool, DMP_OnFB_Results, DMP_OnFB_Servers, NULL);
	//TODO: You can configure how many things to show at once here
	memset(&DMP_pi, 0, sizeof(struct FB_PageInfo));
	DMP_pi.PageSize = FB_MAX_PAGE_SIZE;
	DMP_pi.ExamineMethod = DMP_OnFB_ExamineObject;
	FB_SetPageInfo(DMP_Browser, &DMP_pi);
}

int dlna_start(void) 
{
	int x;
	pthread_t t;

	if (dlnaWorkerHandle == 0)
	{
	MicroStackChain = ILibCreateChain();

	childContexts = ILibLinkedList_Create();

	/* TODO: Each device must have a unique device identifier (UDN) */
	
	/* All evented state variables MUST be initialized before DMR_Start is called. */
	
	eprintf("DLNA: Starting DLNA Stack\n");
	
	ILib_Pool = ILibThreadPool_Create();
	for(x=0;x<3;++x)
	{
		
		pthread_create(&t,NULL,&dlna_poolThread,NULL);
	}

	FB_Init();
	
#ifdef ENABLE_DLNA_DMR
	eprintf("DLNA: Create DMR\n");

	DMR_GetConfiguration()->Manufacturer = "Elecard Ltd.";
	DMR_GetConfiguration()->ManufacturerURL = "http://www.elecard.com/";
	DMR_GetConfiguration()->ModelName = "Elecard iTelec STB 820";
	DMR_GetConfiguration()->ModelDescription = "Hybrid Set-Top Box for IPTV and DVB-T applications";
	DMR_GetConfiguration()->ModelNumber = "820";
	DMR_GetConfiguration()->ModelURL = "http://www.elecard.com/products/iptv-solutions/stb-itelec/820-10.php";

	protocolInfo = dlna_buildProtocolInfo(ProtocolInfoList);
	dmrObject = DMR_Method_Create(MicroStackChain, 0, "Elecard STB820", "serialNumber", 
	                              "c588a8d1-f621-44ca-b350-16929549b320", protocolInfo, ILib_Pool);
	
	dmrObject->Event_SetAVTransportURI = dlna_onSetAVTransportURI;
#endif // #ifdef ENABLE_DLNA_DMR

	ILib_Monitor = ILibCreateLifeTime(MicroStackChain);
	
	ILib_IPAddressLength = ILibGetLocalIPAddressList(&ILib_IPAddressList);
	ILibLifeTime_Add(ILib_Monitor,NULL,4,(void*)&dlna_IPAddressMonitor,NULL);

	pthread_create(&dlnaWorkerHandle, NULL, dlna_workerThread, NULL);

	dlna_createBrowser();

	// without this new devices won't be detected...
	FB_NotifyIPAddressChange(DMP_Browser);

	FB_SetContext(DMP_Browser, (struct CdsObject *)FB_AllRoots);
	}
	return 0;
}

static void dlna_clear_children(int destroy)
{
	void *e;
	struct CdsObject *d;

	ILibLinkedList_Lock(childContexts);

	e = ILibLinkedList_GetNode_Head(childContexts);
	while (e != NULL)
	{
		d = ILibLinkedList_GetDataFromNode(e);

		CDS_ObjRef_Release(d);

		ILibLinkedList_Remove(e);

		e = ILibLinkedList_GetNode_Head(childContexts);
	}

	ILibLinkedList_UnLock(childContexts);

	if (destroy)
	{
		ILibLinkedList_Destroy(childContexts);
	}
}

void dlna_stop(void)
{
	if (dlnaWorkerHandle != 0)
	{
	eprintf("DLNA: Stopping DLNA Stack\n");

	ILibLifeTime_Add(ILib_Monitor,NULL,0,(void*)&dlna_onQuit,NULL);
	pthread_join(dlnaWorkerHandle, NULL);
	dlnaWorkerHandle = 0;

	dlna_clear_children(1);
	}
	dprintf("DLNA: stopped stack\n");
}

static int dlna_fillServerBrowserMenu(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;

	dprintf("%s: in\n", __FUNCTION__);

	mysem_get(dlna_semaphore);

	dprintf("%s: got sem\n", __FUNCTION__);

	// Clear old server list
	interface_clearMenuEntries((interfaceMenu_t*)&BrowseServersMenu);

	// Set new context if specified
	if (pArg != NULL)
	{
		// If we're moving UP - remember parent ID as selected item
		if (pArg != NULL && pArg == parentContext)
		{
			strcpy(parentObjectID, FB_GetContext_CurrentPtr(DMP_Browser)->ID);
			if (strcmp(parentObjectID, "0") == 0)
			{
				strcpy(parentObjectID, FB_GetContext_CurrentPtr(DMP_Browser)->CpInfo.ServiceObject->Parent->UDN);
			}
			dprintf("DLNA: New parent ID: %s\n", parentObjectID);
		}

		// initiate Browse request, it will be processed in dlna thread
		dprintf("DLNA: Set new context %08X\n", pArg);
		FB_SetContext(DMP_Browser, (struct CdsObject*)pArg);

		// release parent context
		if (parentContext != NULL)
		{
			dprintf("DLNA: Release parent %s context\n", parentObjectID);
			CDS_ObjRef_Release(parentContext);
			parentContext = NULL;
		}
		
		dprintf("DLNA: Release children contexts\n");
		dlna_clear_children(0);

		dprintf("DLNA: Fill done\n");
	} else
	{
		dprintf("DLNA: Refresh context\n");
		FB_Refresh(DMP_Browser);

		dprintf("DLNA: Release children contexts\n");
		dlna_clear_children(0);
	}

	if (FB_ContextIsAllRoots(DMP_Browser))
	{
		str = _T("SEARCHING_MEDIA_SERVERS");
		interface_setSelectedItem((interfaceMenu_t *)&BrowseServersMenu, MENU_ITEM_BACK);
	} else
	{
		str = _T("LISTING_MEDIA");
		if (parentContext == NULL)
		{
			parentContext = FB_GetContext_Parent(DMP_Browser);
			dprintf("DLNA: Parent is %08X: %s\n", parentContext, parentObjectID);
		}
		interface_addMenuEntry((interfaceMenu_t *)&BrowseServersMenu, "..",
		                       dlna_fillServerBrowserMenu, parentContext, thumbnail_folder);
		interface_setSelectedItem((interfaceMenu_t *)&BrowseServersMenu, 0);
	}
	interface_addMenuEntryDisabled((interfaceMenu_t *)&BrowseServersMenu, str, thumbnail_search);

	mysem_release(dlna_semaphore);

	dprintf("%s: release sem\n", __FUNCTION__);

	if( interfaceInfo.showMenu )
	{
		interface_menuActionShowMenu(pMenu, (void*)&BrowseServersMenu);
		interface_displayMenu(1);
	}

	return 0;
}

int dlna_initServerBrowserMenu(interfaceMenu_t *pMenu, void* pArg)
{
	dprintf("DLNA: Init Browser menu\n");

	dlna_start();

	FB_NotifyIPAddressChange(DMP_Browser);

	// Browse media servers
	return dlna_fillServerBrowserMenu(pMenu,NULL);
}

static int dlna_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch( cmd->command )
	{
		case interfaceCommandBack:
			if (!FB_ContextIsAllRoots(DMP_Browser))
			{
				dlna_fillServerBrowserMenu(pMenu, (void*)parentContext);
				return 0;
			}
		case interfaceCommandRefresh:
			dlna_fillServerBrowserMenu(pMenu, NULL);
			return 0;
		default: ;
	}


	return 1;
}

void dlna_buildDLNAMenu(interfaceMenu_t *pParent)
{
	int media_icons[4] = { 0, 0, 0, 0 };

	createListMenu(&BrowseServersMenu, _T("MEDIA_SERVERS"), thumbnail_usb, media_icons, pParent,
	               interfaceListMenuIconThumbnail, NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&BrowseServersMenu, dlna_keyCallback);

	mysem_create(&dlna_semaphore);
}

int dlna_setChannel(int channel, void* pArg)
{
	eprintf("DLNA: SET channel %d\n", channel);
	return 0;
}

int dlna_startNextChannel(int direction, void* pArg)
{
	eprintf("DLNA: NEXT direction %d\n", direction);
	return 0;
}

#endif // #ifdef ENABLE_DLNA
