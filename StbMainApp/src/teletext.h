#if !defined(__TELETEXT_H)
#define __TELETEXT_H

/*
 teletext.h

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
#include "dvb.h"
#include "interface.h"

#include <stdlib.h>

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define TELETEXT_PACKET_BUFFER_SIZE (5*TS_PACKET_SIZE)
#define TELETEXT_pipe_TS "/tmp/ttx.ts"

#define PG_ACTIVE	0x100
#define BAD_CHAR 0xb8 
/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef ENABLE_TELETEXT

struct vt_page
{
    int pgno, subno;	// the wanted page number
    int lang;		// language code
    int flags;		// misc flags (see PG_xxx below)
    int errors;		// number of single bit errors in page
    u32 lines;		// 1 bit for each line received
    u8 data[25][40];	// page contents
    int flof;		// page has FastText links
    struct {
    int pgno;
    int subno;
    } link[6]; // FastText links (FLOF)
};
void teletext_init(void);

int32_t teletext_start(DvbParam_t *param);
int32_t teletext_stop(void);

int32_t teletext_processCommand(pinterfaceCommandEvent_t cmd, void *pArg);

/* Displays teletext */
void teletext_displayPage(void);

int32_t teletext_isTeletextReady(void);
int32_t teletext_isTeletextShowing(void);
uint32_t teletext_isEnable(void);
uint32_t teletext_enable(uint32_t enable);

#else //ENABLE_TELETEXT

//define fake macros
#define teletext_init(...)
#define teletext_start(...)
#define teletext_stop(...)
#define teletext_processCommand(...)	-1
#define teletext_displayPage(...)
#define teletext_isTeletextReady()		0
#define teletext_isTeletextShowing()	0
#define teletext_isEnable()				0
#define teletext_enable(...)

#endif //ENABLE_TELETEXT

#endif
