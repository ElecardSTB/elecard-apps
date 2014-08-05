/*
 src/server.h

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
#include <stdint.h>

/******************************************************************
* EXPORTED MACROS                              [for headers only] *
*******************************************************************/

/******************************************************************
* EXPORTED TYPEDEFS                            [for headers only] *
*******************************************************************/
typedef int32_t getFromServer_t(const char *remoteFile, const char *localFile, void *pArg);
typedef int32_t sendToServer_t(const char *localFile, const char *remoteFile, void *pArg);
typedef int32_t runSFTPBatchOnServer_t(const char *fileName, void *pArg);

typedef struct {
	//API
	getFromServer_t        *pGetFromServer;
	sendToServer_t         *pSendToServer;
	runSFTPBatchOnServer_t *pRunSFTPBatchOnServer;

	void *pArg;
} registerServerAPI_t;

/******************************************************************
* EXPORTED DATA                                [for headers only] *
*******************************************************************/

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
int32_t server_get(const char *remoteFile, const char *localFile);
int32_t server_send(const char *localFile, const char *remoteFile);
int32_t server_runSFTPBatchFile(const char *fileName);

int32_t server_setWorkDir(const char *workDir);
const char *server_getWorkDir(void);
int32_t server_isAPIregistered(void);

int32_t server_registerAPI(const registerServerAPI_t *pArg);


