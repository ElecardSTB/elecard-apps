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

#include <stdlib.h>

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define TELETEXT_PACKET_BUFFER_SIZE (5*TS_PACKET_SIZE)
#define TELETEXT_pipe_TS "/tmp/ttx.ts"

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef ENABLE_TELETEXT

void teletext_init(void);

/**
*   @brief Function takes PES packets from buffer of TS packets
* 
*   @param buf		I	Packets buffer
*   @param size	I	Buffer size
*/
void teletext_readPESPacket(unsigned char *buf, size_t size);

/* Displays teletext */
void teletext_displayTeletext(void);
int teletext_StartThread(void);
int teletext_StopThread();
#else //#ifdef ENABLE_TELETEXT

//define fake macros
#define teletext_init(...)
#define teletext_readPESPacket(...)

#endif //#else //#ifdef ENABLE_TELETEXT

#endif
