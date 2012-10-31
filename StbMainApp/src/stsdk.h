#ifdef STSDK

#ifndef __STSDK_H
#define __STSDK_H

/*
 stsdk.c

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

#include <elcd-rpc.h>
#ifdef ENABLE_DVB
#include <linux/dvb/frontend.h>
#endif

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#ifndef TUNER_MAX_NUMBER
	#define TUNER_MAX_NUMBER (4)
#endif

#define RPC_SETSTREAM_TIMEOUT (7)
#define RPC_TIMEOUT (3)
#define RPC_SCAN_TIMEOUT      (20)

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef void (*rpcCallback_t)(elcdRpcType_t type, cJSON *result, void* pArg);

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

int  st_init(void);
void st_terminate(void);

/** Makes synchronous call to elcd server. Answer is waited RPC_TIMEOUT seconds.
 * Pointer stored in result should be freed by caller function.
 * 
 * @return 0 on success.
 *         If RPC has failed, non-zero is returned. type and result are left unchanged.
 */
int st_rpcSync (elcdRpcCommand_t cmd, cJSON* params, elcdRpcType_t *type, cJSON **result);

int st_rpcSyncTimeout(elcdRpcCommand_t cmd, cJSON* params, int timeout, elcdRpcType_t *type, cJSON **result);

/** Makes asynchronous call to elcd server. Waiting has no time constraint.
 * Caller should take care of freeing params and result returned in callback.
 * 
 * @return Place in RPC pool, >= 0 on success
 *         Negative in case of error.
 */
int st_rpcAsync(elcdRpcCommand_t cmd, cJSON* params, rpcCallback_t callback, void *pArg);

/** Cancel queued asynchronous call.
 * 
 * @param[in] execute Call queued callback with type == invalid, empty result and saved pArg
 */
void st_cancelAsync(int index, int execute);

#ifdef ENABLE_DVB
int st_getDvbTuner(void);
void st_setTuneParams(int tuner, cJSON *params);

fe_type_t st_getDvbTunerType(int tuner);
#endif

/** Changes hdmi output mode. Reinitialize framebuffer if resolution is changed.
 * 
 * @param[in] selectedFormat Setted output mode.
 * @param[in] previousFormat Previous output mode. For detecting resolution change
 */
void st_changeOutputMode(char *selectedFormat, char *previousFormat);

/** Is need restart.
 * 
 * @return >0 if needed restart after hdmi output mode changes.
 */
int st_needRestart(void);

#ifdef __cplusplus
}
#endif

#endif // __STSDK_H
#endif // STSDK
