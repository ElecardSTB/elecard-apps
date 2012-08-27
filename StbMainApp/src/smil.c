
/*
 smil.c

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

#include "smil.h"

#ifdef ENABLE_SMIL

#include "defines.h"
#include "debug.h"
#include "interface.h"
#include "output.h"
#include "media.h"
#include "l10n.h"

#include <curl/curl.h>

#include <string.h>
#include <stdio.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define DATA_BUFF_SIZE (10*1024)

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static char* smil_getLastURL(int field, void* pArg);
static int   smil_inputURL(interfaceMenu_t *pMenu, char *value, void* pArg);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static char smil_lasturl[MAX_URL] = { 0 };
static char smil_rtmpurl[MAX_URL] = { 0 };
static char errbuff[CURL_ERROR_SIZE];

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

int smil_enterURL(interfaceMenu_t *pMenu, void* pArg)
{

	interface_getText(pMenu,_T("ENTER_CUSTOM_URL"), "\\w+", smil_inputURL, smil_getLastURL, inputModeABC, pArg);

	return 0;
}

static char* smil_getLastURL(int field, void* pArg)
{
	return field == 0 ? smil_lasturl : NULL;
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

static int smil_inputURL(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	CURLcode ret;
	CURL *hnd;
	char data[DATA_BUFF_SIZE];
	char *content_type;
	int code;
	data[0] = 0;
	errbuff[0] = 0;

	if( value == NULL)
		return 1;

	strcpy( smil_lasturl, value );

	if (strncasecmp(value, URL_HTTP_MEDIA,  sizeof(URL_HTTP_MEDIA)-1) != 0 &&
		strncasecmp(value, URL_HTTPS_MEDIA, sizeof(URL_HTTPS_MEDIA)-1) != 0)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_PROTO"), thumbnail_error, 0);
		return -1;
	}

	hnd = curl_easy_init();

	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, data);
	curl_easy_setopt(hnd, CURLOPT_URL, value);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, errbuff);
	curl_easy_setopt(hnd, CURLOPT_CONNECTTIMEOUT, 5);
	curl_easy_setopt(hnd, CURLOPT_TIMEOUT, 15);
	appInfo_setCurlProxy(hnd);

	ret = curl_easy_perform(hnd);

	if (ret == 0)
	{
		curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &code);
		if (code != 200)
		{
			ret = code > 0 ? code : -1;
			interface_showMessageBox(_T("ERR_SERVICE_UNAVAILABLE"), thumbnail_error, 0);
		} else
		{
			if( CURLE_OK != curl_easy_getinfo(hnd, CURLINFO_CONTENT_TYPE, &content_type) )
			{
				content_type = NULL;
			}
			dprintf("%s: Content-Type: %s\n", __FUNCTION__, content_type);
			//dprintf("%s: streams data %d:\n%s\n\n", __FUNCTION__, strlen(data), data);
		}
	}
	else
		eprintf("SMIL: Failed(%d) to get '%s' with message:\n%s\n", ret, value, errbuff);

	if ( ret == 0 )
	{
		if((ret = smil_parseRTMPStreams((const char*)data, smil_rtmpurl, sizeof(smil_rtmpurl))) == 0 )
		{
			dprintf("%s: Playing '%s'\n", __FUNCTION__, smil_rtmpurl);
			media_playURL( screenMain, smil_rtmpurl, NULL, NULL );
		}
		else
		{
			eprintf("SMIL: Failed to parse SMIL at '%s'\n", value);
			interface_showMessageBox(_T("ERR_SERVICE_UNAVAILABLE"), thumbnail_error, 0);
		}
	} else
	{
		eprintf("SMIL: Failed to get SMIL playlist from '%s'\n", value);
		interface_showMessageBox(_T("ERR_SERVICE_UNAVAILABLE"), thumbnail_error, 0);
	}

	curl_easy_cleanup(hnd);

	return ret;
}

#endif // ENABLE_SMIL
