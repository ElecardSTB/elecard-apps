#if !defined(__DVB_TYPES_H)
	#define __DVB_TYPES_H

/*
 dvb_types.h

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

/** @defgroup dvb_types Common DVB data structure definitions
 *  @ingroup  StbMainApp
 */

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "defines.h"

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define FE_TYPE_COUNT (4)

#define TS_PACKET_SIZE (188)

#define FILESIZE_THRESHOLD (1024*1024*1024)

#define BER_THRESHOLD (30000)

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

/** @ingroup dvb_types
 * Possible input tuners
 */
typedef enum
{
	inputTuner0 = 0,
	inputTuner1,
	inputTuners
} tunerFormat;

/** @ingroup dvb_types
 * The modes in which DVB can operate
 */
typedef enum
{
	DvbMode_Watch,  /**< Watching the output of a tuner */
	DvbMode_Record, /**< Recording the output of a tuner */
	DvbMode_Play,   /**< Playing back a recorded file */
	DvbMode_Multi,  /**< Watching four channels simultaneously */
} DvbMode_t;

#endif /* __DVB_TYPES_H      Do not add any thing below this line */
