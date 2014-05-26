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
#include "interface.h"
#include "defines.h"
#include "dvbChannel.h"

#ifdef ENABLE_DVB

/***********************************************
* EXPORTED MACROS                              *
************************************************/

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

/***********************************************
* EXPORTED DATA                                *
************************************************/



/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/

void bouquet_loadChannelsFile();

int bouquets_getNumberPlaylist();
void bouquets_setNumberPlaylist(int num);

int bouquets_setDigitalBouquet(interfaceMenu_t *pMenu, void* pArg);
int bouquets_setAnalogBouquet(interfaceMenu_t *pMenu, void* pArg);
int bouquet_createNewBouquet(interfaceMenu_t *pMenu, char *value, void* pArg);
void bouquet_loadDigitalBouquetsList();
void bouquet_loadAnalogBouquetsList();
void bouquet_addScanChannels();
int bouquet_getFolder(char *bouquetsFile);
int bouquet_saveDigitalBouquet(interfaceMenu_t* pMenu, void* pArg);

void bouquet_saveAnalogBouquet();
int bouquet_saveAnalogMenuBouquet(interfaceMenu_t* pMenu, void* pArg);

int bouquet_updateDigitalBouquet(interfaceMenu_t* pMenu, void* pArg);
int bouquet_updateAnalogBouquet(interfaceMenu_t* pMenu, void* pArg);
int bouquet_removeBouquet(interfaceMenu_t* pMenu, void* pArg);
int bouquet_enableControl(interfaceMenu_t* pMenu, void* pArg);
int bouquet_enable();
void bouquet_init();
void bouquet_setEnable(int i);
void bouquet_getDigitalName(char *dir, char *fname, char *name);
void bouquet_getAnalogName(char *dir, char *fname, char *name);
char *bouquet_getDigitalBouquetName();
char *bouquet_getAnalogBouquetName();
char *bouquet_getNameBouquetList(list_element_t **head, int number);
void bouquet_setDigitalBouquetName(char *name);
void bouquet_setAnalogBouquetName(char *name);
void bouquet_loadBouquets(list_element_t **services);
int bouquet_enable();
//int bouquet_file();
//int bouquet_getFile();
void bouquet_downloadFileWithServices(char *filename);
void  bouquet_dump(char *filename);
list_element_t *get_bouquet_list();
void bouquet_loadLamedb(char *bouquet_file, list_element_t **);
void bouquets_free_serveces();
int bouquets_compare(list_element_t **);
EIT_service_t *bouquet_findService(EIT_common_t *header);

#endif // ENABLE_DVB
#endif // BOUQUET_H
