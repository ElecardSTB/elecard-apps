
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

#include "elcd-rpc.h"

#include "cJSON.h"
#include <stdio.h>
#include <string.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define CMD_VALID(cmd) ((cmd) >= 0 && (cmd) <= elcmd_cmd_count)

/***********************************************
* STATIC DATA                                  *
************************************************/

const char* rpc_cmd_names[] = {
[elcmd_none]        = "none",
[elcmd_state]       = "state",
[elcmd_quit]        = "quit",
[elcmd_setstream]   = "setstream",
[elcmd_getstream]   = "getstream",
[elcmd_setspeed]    = "setspeed",
[elcmd_getspeed]    = "getspeed",
[elcmd_closestream] = "closestream",
[elcmd_play]        = "play",
[elcmd_pause]       = "pause",
[elcmd_stop]        = "stop",
[elcmd_times]       = "times",
[elcmd_setpos]      = "setpos",
[elcmd_setvol]      = "setvol",
[elcmd_getvol]      = "getvol",
[elcmd_setvmode]    = "setvmode",
[elcmd_listvmode]   = "listvmode",
[elcmd_listvoutput] = "listvoutput",
[elcmd_listvinput]  = "listvinput",
[elcmd_setvinput]   = "setvinput",
[elcmd_getaudio]    = "getaudio",
[elcmd_setaudio]    = "setaudio",
[elcmd_setzoom]     = "setzoom",
[elcmd_dvbtuners]   = "dvbtuners",
[elcmd_dvbclearservices] = "dvbclearservices",
[elcmd_dvbscan]     = "dvbscan",
[elcmd_dvbtune]     = "dvbtune",
[elcmd_dvbdiseqc]   = "dvbdiseqc",
[elcmd_tvscan]      = "tvscan",
[elcmd_subtitle]    = "subtitle",
[elcmd_recstart]    = "recstart",
[elcmd_recstop]     = "recstop",
[elcmd_reclist]     = "reclist",
// todo: make sure this array match elcdRpcCommand_t enum in rpc.h
[elcmd_cmd_count]   = NULL,
};

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

const char* rpc_cmd_name(elcdRpcCommand_t cmd)
{
	/// assert( CMD_VALID(cmd) )
	return rpc_cmd_names[cmd];
}

char* rpc_request( const char *cmd, int id, cJSON *value )
{
	/// assert( CMD_VALID(cmd) && value != NULL );
	cJSON *array  = NULL;

	if( !value || value->type != cJSON_Array )
	{
		array = cJSON_CreateArray();
		if( value )
			cJSON_AddItemReferenceToArray(array, value);
	} else
		array = value;

	cJSON *req    = cJSON_CreateObject();
	cJSON_AddItemToObject( req, "method", cJSON_CreateString( cmd ) );
	cJSON_AddItemReferenceToObject(req, "params", array );
	cJSON_AddItemToObject( req, "id",     cJSON_CreateNumber( id ) );

	char *str = cJSON_PrintUnformatted(req);

	if( array != value )
		cJSON_Delete( array );
	cJSON_Delete(req);

	return str;
}

char* rpc_result ( int id, cJSON *result )
{
	cJSON *res = cJSON_CreateObject();
	cJSON_AddItemToObject( res, "error",  NULL );
	cJSON_AddItemReferenceToObject( res, "result", result );
	cJSON_AddItemToObject( res, "id",     cJSON_CreateNumber( id ) );

	char *str = cJSON_PrintUnformatted(res);

	cJSON_Delete(res);

	return str;
}

char* rpc_error  ( int id, cJSON *err )
{
	cJSON *res = cJSON_CreateObject();
	cJSON_AddItemReferenceToObject( res, "error",  err );
	cJSON_AddItemToObject( res, "result", NULL );
	cJSON_AddItemToObject( res, "id",     cJSON_CreateNumber( id ) );

	char *str = cJSON_PrintUnformatted(res);

	cJSON_Delete(res);

	return str;
}
