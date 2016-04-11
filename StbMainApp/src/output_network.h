/*
 StbMainApp/src/output_network.h

Copyright (C) 2016  Elecard Devices
Anton Sergeev <Anton.Sergeev@elecard.ru>

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
#if !defined(__OUTPUT_NETWORK_H__)
#define __OUTPUT_NETWORK_H__

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include <stdint.h>
#include "interface.h"


/******************************************************************
* EXPORTED TYPEDEFS                            [for headers only] *
*******************************************************************/
typedef enum
{
    ifaceWAN = 0,
    ifaceLAN = 1,
    ifaceWireless,
//     ifacePPP,
//     ifaceBridge,
} eVirtIface_t;


/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

void outputNetwork_buildMenu(interfaceMenu_t *);
void outputNetwork_cleanupMenu(void);
interfaceListMenu_t *outputNetwork_getMenu(void);

int32_t outputNetwork_showNetwork(char *info_text);


/**
*   @brief Function used to clean up leading zeroes in IP addresses
*
*   @retval value
*/
char* inet_addr_prepare( char *value);

const char *outputNetwork_virtIfaceName(eVirtIface_t i);
int32_t output_readInterfacesFile(void);


#ifdef ENABLE_WIFI
extern interfaceListMenu_t WifiSubMenu;

int32_t output_changeWifiKey(interfaceMenu_t *pMenu, void* pArg);
int32_t output_setESSID(interfaceMenu_t *pMenu, char *value, void* pArg);
/**
 * @param[in] pArg outputWifiAuth_t to change wireless authentification to
 */
int32_t output_setAuthMode(interfaceMenu_t *pMenu, void* pArg);

/**
 * @param[in] pArg outputWifiEncryption_t to change wireless encryption to
 */
int32_t output_setWifiEncryption(interfaceMenu_t *pMenu, void* pArg);

/**
 * @param[in] pArg outputWifiMode_t to change wireless mode to
 */
int32_t output_setWifiMode(interfaceMenu_t *pMenu, void* pArg);
#endif

#ifdef __cplusplus
}
#endif

#endif //#if !defined(__OUTPUT_NETWORK_H__)
