#ifndef __STB_WIRELESS_H
#define __STB_WIRELESS_H

/*
 stb_wireless.h

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
#include "defines.h"
#include "interface.h"


/***********************************************
* EXPORTED MACROS                              *
************************************************/

#ifdef STB82
#define DEFAULT_ESSID "STB820"
#endif
#ifdef STSDK
#define DEFAULT_ESSID "STB830"
#endif
#ifndef DEFAULT_ESSID
#define DEFAULT_ESSID "STB"
#endif

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef enum
{
	wifiModeManaged = 0,
	wifiModeAdHoc,
#ifdef STBPNX
	wifiModeCount, // Master mode unsupported yet
	wifiModeMaster,
#else
	wifiModeMaster,
	wifiModeCount,
#endif
} outputWifiMode_t;

typedef enum
{
	wifiAuthOpen = 0,
	wifiAuthWEP,
	wifiAuthWPAPSK,
	wifiAuthWPA2PSK,
	wifiAuthCount,
} outputWifiAuth_t;

typedef enum
{
	wifiEncTKIP = 0,
	wifiEncAES,
	wifiEncCount
} outputWifiEncryption_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/
#ifdef ENABLE_WIFI
extern interfaceListMenu_t WirelessMenu;
#endif

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
int  wireless_buildMenu(interfaceMenu_t *pParent );
void wireless_cleanupMenu(void);

const char* wireless_mode_print( outputWifiMode_t mode );
const char* wireless_auth_print( outputWifiAuth_t auth );
const char* wireless_encr_print( outputWifiEncryption_t encr );

#endif // __STB_WIRELESS_H
