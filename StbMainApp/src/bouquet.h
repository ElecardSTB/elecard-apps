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

#include "defines.h"
#include "helper.h"
#include <service.h>

/***********************************************
* EXPORTED MACROS                              *
************************************************/

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/
typedef enum {
	eBouquet_analog = 0,
	eBouquet_digital,
	eBouquet_all,
} typeBouquet_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_DVB

void bouquet_addScanChannels(void);
void bouquet_saveAnalogBouquet(void);


int32_t bouquet_isEnable(void);
void bouquet_setEnable(int32_t enable);

//Name list
listHead_t *bouquet_getNameList(typeBouquet_t btype);

const char *bouquet_getCurrentName(typeBouquet_t btype);
void        bouquet_setCurrentName(typeBouquet_t btype, const char *name);

int32_t bouquet_isDownloaded   (typeBouquet_t btype, const char *name);
int32_t bouquet_open           (typeBouquet_t btype, const char *name, int32_t force);
int32_t bouquet_create         (typeBouquet_t btype, const char *name);
int32_t bouquet_save           (typeBouquet_t btype, const char *name);
int32_t bouquet_remove         (typeBouquet_t btype, const char *name);

//work with server
int32_t bouquet_updateNameList (typeBouquet_t btype, int32_t isDownload);
int32_t bouquet_update         (typeBouquet_t btype, const char *name);
int32_t bouquet_upload         (typeBouquet_t btype, const char *name);

void bouquet_init(void);
void bouquet_terminate(void);

#else // ENABLE_DVB

#define bouquet_isEnable()          0
#define bouquet_setEnable(...)

#define bouquet_getCurrentName(...) NULL
#define bouquet_setCurrentName(...)

#define bouquet_init()
#define bouquet_terminate()

#endif // ENABLE_DVB

//Parent control API
int32_t parentControl_savePass(const char *value);
int32_t parentControl_checkPass(const char *value);

#ifdef __cplusplus
}
#endif

#endif // BOUQUET_H
