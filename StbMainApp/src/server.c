/*
 src/server.c

Copyright (C) 2013  Elecard Devices
Anton Sergeev <Anton.Sergeev@elecard.ru>

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

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "server.h"

/******************************************************************
* LOCAL MACROS                                                    *
*******************************************************************/

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static registerServerAPI_t serverAPI;
static int32_t registered = 0;
static const char *workDir = NULL;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>_<Word>+    *
*******************************************************************/
int32_t server_get(const char *remoteFile, const char *localFile)
{
	if(serverAPI.pGetFromServer == NULL) {
		return -1;
	}

	return serverAPI.pGetFromServer(remoteFile, localFile, serverAPI.pArg);
}

int32_t server_send(const char *localFile, const char *remoteFile)
{
	if(serverAPI.pSendToServer == NULL) {
		return -1;
	}

	return serverAPI.pSendToServer(localFile, remoteFile, serverAPI.pArg);
}

int32_t server_runSFTPBatchFile(const char *fileName)
{
	if(serverAPI.pRunSFTPBatchOnServer == NULL) {
		return -1;
	}
	return serverAPI.pRunSFTPBatchOnServer(fileName, serverAPI.pArg);
}

int32_t server_setWorkDir(const char *str)
{
	workDir = str;
	return 0;
}

const char *server_getWorkDir(void)
{
	return workDir ? workDir : ".";
}

int32_t server_isAPIregistered(void)
{
	return registered;
}

int32_t server_registerAPI(const registerServerAPI_t *pArg)
{
	if(pArg == NULL) {
		return -1;
	}
	serverAPI = *pArg;
	registered = 1;

	return 0;
}

