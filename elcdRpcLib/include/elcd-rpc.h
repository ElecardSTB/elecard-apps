#ifndef __ELCD_RPC_H
#define __ELCD_RPC_H

/*
 elcd-rpc.h

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

#include <stdlib.h>
#include <cJSON.h>

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef enum
{
	elcdRpcInvalid = -1,
	elcdRpcRequest = 0,
	elcdRpcResult,
	elcdRpcError,
} elcdRpcType_t;

typedef enum
{
	// Synchronous commands goes first
	elcmd_none = 0,
	elcmd_state,
	elcmd_quit,
	elcmd_times,
	elcmd_setvol,
	elcmd_getvol,
	elcmd_getspeed,
	elcmd_setvmode,
	elcmd_listvmode,
	elcmd_listvoutput,
	elcmd_listvinput,
	elcmd_setvinput,
	elcmd_disablevinput,
	elcmd_getaudio,
	elcmd_setaudio,
	elcmd_setzoom,
	elcmd_dvbtuners,
	elcmd_dvbclearservices,
	elcmd_dvbscan,
	elcmd_dvbtune,
	elcmd_dvbdiseqc,
	elcmd_tvscan,
	elcmd_tvtune,
	elcmd_subtitle,
	elcmd_reclist,
	elcmd_getstream,
	elcmd_TSsectionStreamOn,
	elcmd_TSsectionStreamOff,
	elcmd_setOutputWnd,
	elcmd_getDvbTunerStatus,
	elcmd_TSstreamerOn,
	elcmd_TSstreamerOff,
	elcmd_env,

	// All following commands are asynchronous
	elcmd_async = 0x1000, //should be enough for synchronous commands
	elcmd_closestream = elcmd_async,
	elcmd_setstream,
	elcmd_setpos,
	elcmd_setspeed,
	elcmd_play,
	elcmd_pause,
	elcmd_stop,
	elcmd_recstart,
	elcmd_recstop,
	elcmd_ttxStart,
	elcmd_ttxStop,

// todo: put new commands before this line,
// then add matching entry to rpc_cmd_names array in rpc.c
	elcmd_cmd_count,
} elcdRpcCommand_t;

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define case_all_sync \
	case elcmd_state: \
	case elcmd_quit: \
	case elcmd_times: \
	case elcmd_setvol: \
	case elcmd_getvol: \
	case elcmd_getspeed: \
	case elcmd_setvmode: \
	case elcmd_listvmode: \
	case elcmd_listvoutput: \
	case elcmd_listvinput: \
	case elcmd_setvinput: \
	case elcmd_disablevinput: \
	case elcmd_getaudio: \
	case elcmd_setaudio: \
	case elcmd_setzoom: \
	case elcmd_getstream: \
	case elcmd_dvbtuners: \
	case elcmd_dvbclearservices: \
	case elcmd_dvbscan: \
	case elcmd_dvbtune: \
	case elcmd_dvbdiseqc: \
	case elcmd_tvscan: \
	case elcmd_tvtune: \
	case elcmd_subtitle: \
	case elcmd_reclist: \
	case elcmd_TSsectionStreamOn: \
	case elcmd_TSsectionStreamOff: \
	case elcmd_setOutputWnd: \
	case elcmd_getDvbTunerStatus: \
	case elcmd_TSstreamerOn: \
	case elcmd_TSstreamerOff: \
	case elcmd_env

#define case_all_async \
	case elcmd_closestream: \
	case elcmd_setspeed: \
	case elcmd_setstream: \
	case elcmd_setpos: \
	case elcmd_play: \
	case elcmd_pause: \
	case elcmd_stop: \
	case elcmd_recstart: \
	case elcmd_recstop: \
	case elcmd_ttxStart: \
	case elcmd_ttxStop

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/** Return read-only static string */
const char *rpc_getCmdName(elcdRpcCommand_t cmd);
elcdRpcCommand_t rpc_getCmd(const char *name);

/** All functions return malloc'ated string, which needs to be freed after usage
 */
char* rpc_request( const char *cmd, int id, cJSON *value);
char* rpc_result ( int id, cJSON *result);
char* rpc_error  ( int id, cJSON *err );

#ifdef __cplusplus
}
#endif

static inline int32_t jsonGetInt(cJSON *param, int32_t defaultValue)
{
	return (param && param->type == cJSON_Number) ? param->valueint : defaultValue;
}
static inline int32_t objGetInt(cJSON *params, const char *name, int32_t defaultValue)
{
	return params ? jsonGetInt(cJSON_GetObjectItem(params, name), defaultValue) : defaultValue;
}

static inline double jsonGetDouble(cJSON *param, double defaultValue)
{
	return (param && param->type == cJSON_Number) ? param->valuedouble : defaultValue;
}
static inline double objGetDouble(cJSON *params, const char *name, double defaultValue)
{
	return params ? jsonGetDouble(cJSON_GetObjectItem(params, name), defaultValue) : defaultValue;
}

static inline char* jsonGetString(cJSON *param, char *defaultValue)
{
	return (param && param->type == cJSON_String) ? param->valuestring : defaultValue;
}
static inline char* objGetString(cJSON *params, const char *name, char *defaultValue)
{
	return params ? jsonGetString(cJSON_GetObjectItem(params, name), defaultValue) : defaultValue;
}

static inline int jsonCheckIfTrue(cJSON *param)
{
	return (param && param->type == cJSON_True) ? 1 : 0;
}
static inline int objCheckIfTrue(cJSON *params, const char *name)
{
	return params ? jsonCheckIfTrue(cJSON_GetObjectItem(params, name)) : 0;
}

#endif // __ELCD_RPC_H
