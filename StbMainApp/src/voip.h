#ifndef __VOIP_H
#define __VOIP_H

/*
 voip.h

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

#ifdef ENABLE_VOIP

#include "interface.h"

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define MAX_VOIP_ADDRESS_LENGTH (128)

#define VOIP_SOCKET	"/tmp/voip.socket"

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef struct
{
	char title[MENU_ENTRY_INFO_LENGTH];
	char uri[MAX_VOIP_ADDRESS_LENGTH];
} voipEntry_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

extern interfaceListMenu_t VoIPMenu;
extern interfaceListMenu_t AddressBookMenu;
extern interfaceListMenu_t MissedCallsMenu;
extern interfaceListMenu_t AnsweredCallsMenu;
extern interfaceListMenu_t DialedNumbersMenu;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

void voip_init(void);
void voip_cleanup(void);
void voip_buildMenu(interfaceMenu_t *pParent);
int  voip_dialNumber(interfaceMenu_t *pMenu, void *pArg);
int  voip_fillMenu(interfaceMenu_t *pMenu, void *pArg);
int  voip_answerCall(interfaceMenu_t *pMenu, void *pArg);
int  voip_hangup(interfaceMenu_t *pMenu, void *pArg);
void voip_setBuzzer(void);

#endif // ENABLE_VOIP

#endif //__VOIP_H
