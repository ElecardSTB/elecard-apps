/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2002 - 2005 Intel Corporation.  All rights reserved.
 * 
 * The source code contained or described herein and all documents
 * related to the source code ("Material") are owned by Intel
 * Corporation or its suppliers or licensors.  Title to the
 * Material remains with Intel Corporation or its suppliers and
 * licensors.  The Material contains trade secrets and proprietary
 * and confidential information of Intel or its suppliers and
 * licensors. The Material is protected by worldwide copyright and
 * trade secret laws and treaty provisions.  No part of the Material
 * may be used, copied, reproduced, modified, published, uploaded,
 * posted, transmitted, distributed, or disclosed in any way without
 * Intel's prior express written permission.
 
 * No license under any patent, copyright, trade secret or other
 * intellectual property right is granted to or conferred upon you
 * by disclosure or delivery of the Materials, either expressly, by
 * implication, inducement, estoppel or otherwise. Any license
 * under such intellectual property rights must be express and
 * approved by Intel in writing.
 * 
 * $Workfile: Main.c
 * $Revision: #1.0.2777.24812
 * $Author:   Intel Corporation, Intel Device Builder
 * $Date:     20 ноября 2009 г.
 *
 *
 *
 */

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

#include "DMR_MicroStack.h"
#include "ILibWebServer.h"
#include "ILibAsyncSocket.h"



#include "ILibThreadPool.h"
#include <pthread.h>
#include "FilteringBrowser.h"
#include "CdsObject.h"
#include "CdsDidlSerializer.h"
#include "DLNAProtocolInfo.h"
#include "DMR.h"

#if defined(WIN32)
	#include <crtdbg.h>
#endif


void *MicroStackChain;








void *ILib_Pool;


void Common__CP_IPAddressListChanged();

void *DMP_Browser;
DMR dmrObject;
const char* ProtocolInfoList[] = {
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


int WaitForExit = 0;


void *ILib_Monitor;
int ILib_IPAddressLength;
int *ILib_IPAddressList;



void OutputDebugString(char *str)
{
    puts(str);
}





















void ILib_IPAddressMonitor(void *data)
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
	
	
	ILibLifeTime_Add(ILib_Monitor,NULL,4,(void*)&ILib_IPAddressMonitor,NULL);
}





void ILib_LinuxQuit(void *data)
{

	if(ILib_Pool!=NULL)
	{
		printf("Stopping Thread Pool...\r\n");
		ILibThreadPool_Destroy(ILib_Pool);
		printf("Thread Pool Destroyed...\r\n");
	}
	
	if(MicroStackChain!=NULL)
	{
		ILibStopChain(MicroStackChain);
		MicroStackChain = NULL;
	}
	
	
}
void BreakSink(int s)
{
	if(WaitForExit==0)
	{
		ILibLifeTime_Add(ILib_Monitor,NULL,0,(void*)&ILib_LinuxQuit,NULL);
		WaitForExit = 1;
	}
}





void* ILibPoolThread(void *args)
{
	ILibThreadPool_AddThread(ILib_Pool);
	return(0);
}


void DMP_OnFB_Servers (int RemoveFlag, char * udn)
{

	if(RemoveFlag==0)
	{
		printf("Found Media Server: %s\r\n",FB_GetDevice(udn)->FriendlyName );
	}
	else
	{
		printf("Removed Media Server: %s\r\n",FB_GetDevice(udn)->FriendlyName );
		FB_DestroyResults((struct FB_ResultsInfo*)FB_GetDevice(udn)->Tag);
	}
	if(MicroStackChain!=NULL && !ILibIsChainBeingDestroyed(MicroStackChain) && DMP_Browser!=NULL)
	{
		if (FB_ContextIsAllRoots(DMP_Browser))
		{
			FB_Refresh(DMP_Browser);
		} 
	}
}
void DMP_OnFB_Results (void *fbObj, struct FB_ResultsInfo* results)
{
	struct FB_ResultsInfo* resultsInfo = FB_GetResults(fbObj);
	void *node = ILibLinkedList_GetNode_Tail(resultsInfo->LinkedList);
	struct CdsObject* cdsObj = (struct CdsObject*)ILibLinkedList_GetDataFromNode(node);
	if(cdsObj->CpInfo.ServiceObject->Parent->Tag==NULL)
	{
		cdsObj->CpInfo.ServiceObject->Parent->Tag = resultsInfo; // Save this for later
	}
	else
	{
		FB_DestroyResults(resultsInfo);
	}
}


char* BuildProtocolInfo(const char* infoList[])
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


int main(void) 
{
	struct FB_PageInfo DMP_pi;
char *protocolInfo;

	
	int x;
	

	struct sigaction setup_action;
    sigset_t block_mask;
	pthread_t t;
     
		
	

	
	MicroStackChain = ILibCreateChain();
	
	

	/* TODO: Each device must have a unique device identifier (UDN) */
	
		
	
	/* All evented state variables MUST be initialized before DMR_Start is called. */
	
	

	
	printf("Intel MicroStack 1.0 - Intel DLNA DMR,\r\n\r\n");

	
	ILib_Pool = ILibThreadPool_Create();
	for(x=0;x<3;++x)
	{
		
		pthread_create(&t,NULL,&ILibPoolThread,NULL);
	}
	
	DMR_GetConfiguration()->Manufacturer = "Elecard Ltd.";
	DMR_GetConfiguration()->ManufacturerURL = "http://www.elecard.com/";
	DMR_GetConfiguration()->ModelName = "Elecard iTelec STB 820";
	DMR_GetConfiguration()->ModelDescription = "Hybrid Set-Top Box for IPTV and DVB-T applications";
	DMR_GetConfiguration()->ModelNumber = "820";
	DMR_GetConfiguration()->ModelURL = "http://www.elecard.com/products/iptv-solutions/stb-itelec/820-10.php";
		FB_Init();
	DMP_Browser = FB_CreateFilteringBrowser(MicroStackChain, ILib_Pool, DMP_OnFB_Results, DMP_OnFB_Servers, NULL);
	//TODO: You can configure how many things to show at once here
	memset(&DMP_pi, 0, sizeof(struct FB_PageInfo));
	DMP_pi.PageSize = 9;//FB_MAX_PAGE_SIZE;
	DMP_pi.ExamineMethod = NULL; //OnExamineObject
	FB_SetPageInfo(DMP_Browser, &DMP_pi);

protocolInfo = BuildProtocolInfo(ProtocolInfoList);
dmrObject = DMR_Method_Create(MicroStackChain, 0, "Intel Code Wizard Generated DMR", "serialNumber", "c588a8d1-f621-44ca-b350-16929549b320", protocolInfo, ILib_Pool);



	

	
	
	

	
	ILib_Monitor = ILibCreateLifeTime(MicroStackChain);
	
	ILib_IPAddressLength = ILibGetLocalIPAddressList(&ILib_IPAddressList);
	ILibLifeTime_Add(ILib_Monitor,NULL,4,(void*)&ILib_IPAddressMonitor,NULL);
	
	



	sigemptyset (&block_mask);
    /* Block other terminal-generated signals while handler runs. */
    sigaddset (&block_mask, SIGINT);
    sigaddset (&block_mask, SIGQUIT);
    setup_action.sa_handler = BreakSink;
    setup_action.sa_mask = block_mask;
    setup_action.sa_flags = 0;
    sigaction (SIGINT, &setup_action, NULL);
	WaitForExit = 0;


	
	ILibStartChain(MicroStackChain);
free(protocolInfo);


	
	

	
	free(ILib_IPAddressList);
	
	
	return 0;
}

