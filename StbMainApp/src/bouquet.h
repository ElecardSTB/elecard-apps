/*
 bouquet.h

Copyright (C) 2013  Elecard Devices

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

#ifndef BOUQUET_H
#define BOUQUET_H

/***********************************************
* INCLUDE FILES                                *
************************************************/
#include <stdint.h>

#include "interface.h"
#include "defines.h"
#include "dvbChannel.h"

/***********************************************
* EXPORTED MACROS                              *
************************************************/

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef enum _typeBouquet_t {
	fullBouquet = 0,
	analogBouquet = 1,
	digitalBouquet = 2,
} typeBouquet_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/



/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/
#ifdef ENABLE_DVB

void bouquet_loadChannelsFile(void);

int32_t bouquets_getNumberPlaylist(void);
void bouquets_setNumberPlaylist(int32_t num);

void bouquet_loadBouquet(typeBouquet_t index, char * name);
void bouquet_stashBouquet(typeBouquet_t index, char * name);

int32_t bouquets_setDigitalBouquet(interfaceMenu_t *pMenu, void* pArg);
int32_t bouquets_setAnalogBouquet(interfaceMenu_t *pMenu, void* pArg);
int32_t bouquet_createNewBouquet(interfaceMenu_t *pMenu, char *value, void* pArg);
void bouquet_loadDigitalBouquetsList(int force);
void bouquet_loadAnalogBouquetsList(int force);
void bouquet_addScanChannels(void);
int32_t bouquet_getFolder(char *bouquetsFile);
int32_t bouquet_saveDigitalBouquet(interfaceMenu_t* pMenu, void* pArg);

void bouquet_saveAnalogBouquet(void);
int32_t bouquet_saveAnalogMenuBouquet(interfaceMenu_t* pMenu, void* pArg);

int32_t bouquet_updateDigitalBouquet(interfaceMenu_t* pMenu, void* pArg);
int32_t bouquet_updateAnalogBouquet(interfaceMenu_t* pMenu, void* pArg);
int32_t bouquet_updateAnalogBouquetList(interfaceMenu_t* pMenu, void* pArg);
int32_t bouquet_removeBouquet(interfaceMenu_t* pMenu, void* pArg);
int32_t bouquet_enableControl(interfaceMenu_t* pMenu, void* pArg);
int32_t bouquet_enable(void);
void bouquet_init(void);
void bouquet_setEnable(int32_t i);
void bouquet_getDigitalName(char *dir, char *fname, char *name);
void bouquet_getAnalogJsonName(char *fname, char *name);
char *bouquet_getDigitalBouquetName(void);
char *bouquet_getAnalogBouquetName(void);
char *bouquet_getNameBouquetList(list_element_t **head, int32_t number);
void bouquet_setDigitalBouquetName(char *name);
void bouquet_setAnalogBouquetName(char *name);
void bouquet_loadBouquets(list_element_t **services);

//int32_t bouquet_file(void);
//int32_t bouquet_getFile(void);
void bouquet_downloadFileWithServices(char *filename);
void  bouquet_dump(char *filename);
list_element_t *get_bouquet_list(void);
void bouquet_loadLamedb(char *bouquet_file, list_element_t **);
void bouquets_free_serveces(void);
int32_t bouquets_compare(list_element_t **);
EIT_service_t *bouquet_findService(EIT_common_t *header);
#else // ENABLE_DVB

#define bouquet_init()
#define bouquet_setDigitalBouquetName(name)
#define bouquet_setAnalogBouquetName(name)
#define bouquet_setEnable(n)
#define bouquet_getDigitalBouquetName()		""
#define bouquet_getAnalogBouquetName()		""
#define bouquet_enable()					0
#define bouquet_createNewBouquet			NULL
#define bouquet_enableControl				NULL

#endif // ENABLE_DVB
#endif // BOUQUET_H
