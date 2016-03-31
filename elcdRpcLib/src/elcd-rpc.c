
/*
 elcd-rpc.c

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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "elcd-rpc.h"

/***********************************************
* LOCAL MACROS                                 *
************************************************/
//#define CMD_VALID(cmd) ((cmd) >= 0 && (cmd) <= elcmd_cmd_count)
#define CMD_NAME_NONE		"none"
#define CMD_NAME_UNKNOWN	"unknown"

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/
typedef struct {
	int32_t value;
	const char *name;
} uintLookup_t;

/***********************************************
* STATIC DATA                                  *
************************************************/

static uintLookup_t rpc_commands[] = {
	{elcmd_none,				CMD_NAME_NONE},
	{elcmd_state,				"state"},
	{elcmd_quit,				"quit"},
	{elcmd_setstream,			"setstream"},
	{elcmd_getstream,			"getstream"},
	{elcmd_setspeed,			"setspeed"},
	{elcmd_getspeed,			"getspeed"},
	{elcmd_closestream,			"closestream"},
	{elcmd_play,				"play"},
	{elcmd_pause,				"pause"},
	{elcmd_stop,				"stop"},
	{elcmd_times,				"times"},
	{elcmd_setpos,				"setpos"},
	{elcmd_setvol,				"setvol"},
	{elcmd_getvol,				"getvol"},
	{elcmd_setvmode,			"setvmode"},
	{elcmd_listvmode,			"listvmode"},
	{elcmd_listvoutput,			"listvoutput"},
	{elcmd_listvinput,			"listvinput"},
	{elcmd_setvinput,			"setvinput"},
	{elcmd_disablevinput,		"disablevinput"},
	{elcmd_getaudio,			"getaudio"},
	{elcmd_setaudio,			"setaudio"},
	{elcmd_setzoom,				"setzoom"},
	{elcmd_dvbtuners,			"dvbtuners"},
	{elcmd_dvbclearservices,	"dvbclearservices"},
	{elcmd_dvbscan,				"dvbscan"},
	{elcmd_dvbtune,				"dvbtune"},
	{elcmd_dvbdiseqc,			"dvbdiseqc"},
	{elcmd_tvscan,				"tvscan"},
	{elcmd_tvtune,				"tvtune"},
	{elcmd_subtitle,			"subtitle"},
	{elcmd_stopsubtitle,		"stopsubtitle"},
	{elcmd_recstart,			"recstart"},
	{elcmd_recstop,				"recstop"},
	{elcmd_reclist,				"reclist"},
	{elcmd_ttxStart,			"ttxStart"},
	{elcmd_ttxStop,				"ttxStop"},
	{elcmd_TSsectionStreamOn,	"TSsectionStreamOn"},
	{elcmd_TSsectionStreamOff,	"TSsectionStreamOff"},
	{elcmd_setOutputWnd,		"setOutputWnd"},
	{elcmd_getDvbTunerStatus,	"getDvbTunerStatus"},
	{elcmd_TSstreamerOn,		"TSstreamerOn"},
	{elcmd_TSstreamerOff,		"TSstreamerOff"},
	{elcmd_env,					"env"},
	{elcmd_ddvol,				"ddvol"},
	{elcmd_audparam,			"audparam"},
	{elcmd_demuxCcErrorCount,	"demuxCcErrorCount"},
	{elcmd_demuxTsErrorCount,	"demuxTsErrorCount"},
	{elcmd_videoSkippedPictures,"videoSkippedPictures"},
	{elcmd_videoInvalidStartCode,"videoInvalidStartCode"},

	// todo: make sure this array match elcdRpcCommand_t enum in rpc.h
	{elcmd_cmd_count,			NULL},
};

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/
static uint32_t helperLookup(const uintLookup_t table[], const char *key, int32_t defaultValue)
{
	int32_t i;
	if(!key) {
		return defaultValue;
	}
	for(i = 0; table[i].value != elcmd_cmd_count; i++) {
		if(!strcasecmp(table[i].name, key)) {
			return table[i].value;
		}
	}
	return defaultValue;
}

static const char *helperLookupR(const uintLookup_t table[], int32_t key, const char *defaultValue)
{
	int32_t i;
	if(!key) {
		return defaultValue;
	}
	for(i = 0; table[i].value != elcmd_cmd_count; i++) {
		if(table[i].value == key) {
			return table[i].name;
		}
	}
	return defaultValue;
}


const char *rpc_getCmdName(elcdRpcCommand_t cmd)
{
	return helperLookupR(rpc_commands, cmd, CMD_NAME_UNKNOWN);
}

elcdRpcCommand_t rpc_getCmd(const char *name)
{
	return helperLookup(rpc_commands, name, elcmd_none);
}

char *rpc_request(const char *cmd, int id, cJSON *value)
{
	cJSON *array = NULL;

	if(cmd == NULL) {
		printf("%s:%s(): ERROR cmd=NULL!!!\n", __FILE__, __func__);
		cmd = CMD_NAME_NONE;
	}
	if(!value || value->type != cJSON_Array) {
		array = cJSON_CreateArray();
		if(value) {
			cJSON_AddItemReferenceToArray(array, value);
		}
	} else {
		array = value;
	}

	cJSON *req = cJSON_CreateObject();
	cJSON_AddItemToObject(req, "method", cJSON_CreateString(cmd));
	cJSON_AddItemReferenceToObject(req, "params", array);
	cJSON_AddItemToObject(req, "id",     cJSON_CreateNumber(id));

	char *str = cJSON_PrintUnformatted(req);

	if(array != value) {
		cJSON_Delete(array);
	}
	cJSON_Delete(req);

	return str;
}

char *rpc_result(int id, cJSON *result)
{
	cJSON *res = cJSON_CreateObject();
	cJSON_AddItemToObject(res, "error",  NULL);
	cJSON_AddItemReferenceToObject(res, "result", result);
	cJSON_AddItemToObject(res, "id",     cJSON_CreateNumber(id));

	char *str = cJSON_PrintUnformatted(res);

	cJSON_Delete(res);

	return str;
}

char *rpc_error(int id, cJSON *err)
{
	cJSON *res = cJSON_CreateObject();
	cJSON_AddItemReferenceToObject(res, "error",  err);
	cJSON_AddItemToObject(res, "result", NULL);
	cJSON_AddItemToObject(res, "id",     cJSON_CreateNumber(id));

	char *str = cJSON_PrintUnformatted(res);

	cJSON_Delete(res);

	return str;
}
