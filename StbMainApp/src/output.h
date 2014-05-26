#if !defined(__OUTPUT_H)
#define __OUTPUT_H

/*
 output.h

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


/*******************
* INCLUDE FILES    *
********************/

#include "defines.h"

#include "interface.h"
#include "app_info.h"
#include "stb_wireless.h"

/*******************
* EXPORTED MACROS  *
********************/

#define MAX_PASSWORD_LENGTH (128)
#define CREATE_REPORT_FILE "/opt/elecard/bin/create-report.sh"

/*********************
* EXPORTED TYPEDEFS  *
**********************/

typedef enum _colorSetting_t
{
	colorSettingSaturation = 0,
	colorSettingContrast = 1,
	colorSettingBrightness = 2,
	colorSettingHue = 3,
	colorSettingCount = 3, // don't include Hue for now...
} colorSetting_t;

#ifdef STSDK
typedef struct {
	interfaceListMenu_t	menu;
	char				name[64];
	char				currentFormat[64];
	uint32_t			formatCount;
	uint8_t				showAdvanced;
	uint8_t				isMajor;
	uint8_t				hasFeedback;	//Defines supporting feedback from video receivers.
										//It mean that receiver can inform about supported (and native) modes.

} videoOutput_t;

typedef struct {
	//interfaceListMenu_t * menu;
	char * currentInput;
	uint32_t inputCount;
} videoInput_t;
#endif

/*******************
* EXPORTED DATA    *
********************/

extern stbTimeZoneDesc_t timezones[];
extern interfaceListMenu_t OutputMenu;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/
interfaceMenu_t *output_getPlaylistEditorMenu();
char *output_getSelectedNamePlaylistEditor();
void output_redrawMenu(interfaceMenu_t *pMenu);
int enablePlayListEditorMenu(interfaceMenu_t *interfaceMenu);
int enablePlayListSelectMenu(interfaceMenu_t *interfaceMenu);

#ifdef __cplusplus
extern "C" {
#endif

/**
*   @brief Function used to set up the output menu
*
*   @retval void
*/
void output_buildMenu(interfaceMenu_t *pParent);
void output_cleanupMenu(void);

#ifdef ENABLE_DVB
int output_showDVBMenu(interfaceMenu_t *pMenu, void* notused);
#endif

#ifdef ENABLE_PROVIDER_PROFILES
/** Check provider profile is already selected, and if not set Profile menu as current
 * @return 0 if profile is selected
 */
int output_checkProfile(void);
#endif

/**
*   @brief Function used to clean up leading zeroes in IP addresses
*
*   @retval value
*/
char* inet_addr_prepare( char *value);

long output_getColorValue(void *pArg);
void output_setColorValue(long value, void *pArg);

int output_isBridge(void);

#ifdef ENABLE_WIFI
extern interfaceListMenu_t WifiSubMenu;

int output_changeWifiKey(interfaceMenu_t *pMenu, void* pArg);
int output_setESSID(interfaceMenu_t *pMenu, char *value, void* pArg);
/**
 * @param[in] pArg outputWifiAuth_t to change wireless authentification to
 */
int output_setAuthMode(interfaceMenu_t *pMenu, void* pArg);

/**
 * @param[in] pArg outputWifiEncryption_t to change wireless encryption to
 */
int output_setWifiEncryption(interfaceMenu_t *pMenu, void* pArg);

/**
 * @param[in] pArg outputWifiMode_t to change wireless mode to
 */
int output_setWifiMode(interfaceMenu_t *pMenu, void* pArg);
#endif

int output_toggleZoom(void);

int output_setZoom(zoomPreset_t preset);

#ifdef STSDK
int output_readInterfacesFile(void);
int output_toggleOutputModes(void);

#define INPUT_NONE "None"
int output_toggleInputs(void);
int output_setInput(interfaceMenu_t *pMenu, void* pArg);

void output_onUpdate(int found);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __OUTPUT_H      Do not add any thing below this line */
