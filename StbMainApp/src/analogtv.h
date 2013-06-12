
#if !defined(__ANALOGTV_H)
#define __ANALOGTV_H

/*
 analogtv.h

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

/** @file analogtv.h Analog TV playback backend
 */
/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "defines.h"

#include "app_info.h"
#include "interface.h"

#include <platform.h>
//#include <sdp.h>
//#include <service.h>

#ifdef ENABLE_ANALOGTV

/***********************************************
* EXPORTED MACROS                              *
************************************************/
#define MIN_FREQUENCY_HZ  (  40000000)
#define MAX_FREQUENCY_HZ  ( 960000000)

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/
typedef struct {
	uint32_t from_freq;
	uint32_t to_freq;
} analogtv_freq_range_t;
/***********************************************
* EXPORTED DATA                                *
************************************************/
extern int analogtv_service_count;
extern analogtv_freq_range_t analogtv_range;
/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/

/**  @ingroup analogtv
 *   @brief Function used to initialise the Analog TV component
 *
 *   Must be called before initiating offair
 */
void analogtv_init(void);

/**  @ingroup analogtv
 *   @brief Function used to terminate the Analog TV component
 *
 *   Must be called after terminating offair
 */
void analogtv_terminate(void);

/**  @ingroup analogtv
 *   @brief Function used to scan for Analog TV channels
 *
 *   @param[in]  pFunction Callback function
 *
 *   @return 0 on success
 */
int analogtv_serviceScan (interfaceMenu_t *pMenu, void* pArg);

/**  @ingroup analogtv
 *   @brief Function exports current channel list to specified file
 *   @param[in]  filename  Channel file name
 */

int analogtv_clearServiceList(interfaceMenu_t * pMenu, void *pArg);

int analogtv_changeAnalogLowFreq(interfaceMenu_t * pMenu, void *pArg);
int analogtv_changeAnalogHighFreq(interfaceMenu_t * pMenu, void *pArg);

#endif /* ENABLE_ANALOGTV */

#endif /* __ANALOGTV_H      Do not add any thing below this line */