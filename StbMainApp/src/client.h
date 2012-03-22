#ifndef __CLIENT_H
#define __CLIENT_H

/*
 client.h

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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

struct __socketClient_t;

typedef int (*client_handler)(struct __socketClient_t*);

typedef struct __socketClient_t {
	int             socket_fd;
	struct sockaddr_un remote;
	void           *pArg;
	client_handler  before_connect;
	client_handler  after_connect;
	client_handler  on_disconnect;
} socketClient_t;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

int   client_create (socketClient_t *s, const char *socket_name,
                     client_handler before_connect, client_handler after_connect,
                     client_handler on_disconnect, void *pArg);
int   client_destroy(socketClient_t *s);
ssize_t client_read (socketClient_t *s, char *buf, size_t len);
ssize_t client_write(socketClient_t *s, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // __CLIENT_H
