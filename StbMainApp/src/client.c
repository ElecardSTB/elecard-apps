
/*
 client.c

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

#include "client.h"
#include "debug.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define CLIENT_READ_TIMEOUT (5)

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int client_connect   (socketClient_t *s);
static int client_disconnect(socketClient_t *s);

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int client_create(socketClient_t *s, const char *socket_name,
                  client_handler before_connect, client_handler  after_connect,
                  client_handler on_disconnect, void *pArg)
{
	strncpy( s->remote.sun_path, socket_name, sizeof(s->remote.sun_path) );
	s->remote.sun_family = AF_UNIX;
	s->pArg = pArg;
	s->before_connect = before_connect;
	s->after_connect  = after_connect;
	s->on_disconnect  = on_disconnect;
	s->socket_fd      = INVALID_SOCKET;

	return client_connect(s);
}

int client_connect(socketClient_t *s)
{
	size_t len;
	struct stat st;

	if( s->before_connect )
		s->before_connect(s);

	dprintf("%s: %s\n", __FUNCTION__, s->remote.sun_path);

	if (s->socket_fd != INVALID_SOCKET)
	{
		eprintf("%s: close socket %s\n", s->remote.sun_path);
		close(s->socket_fd);
		s->socket_fd = INVALID_SOCKET;
	}

	if( stat( s->remote.sun_path, &st) < 0)
	{
		dprintf("%s: Can't stat %s\n", __FUNCTION__, s->remote.sun_path);
		goto connect_failed;
	}

	if ((s->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		eprintf("%s: Can't create socket %s\n", __FUNCTION__, s->remote.sun_path);
		goto connect_failed;
	}

	len = strlen(s->remote.sun_path) + sizeof(s->remote.sun_family);
	if (connect(s->socket_fd, (struct sockaddr *)&s->remote, len) == -1) {
		eprintf("%s: Can't connect to %s\n", __FUNCTION__, s->remote.sun_path);
		close(s->socket_fd);
		goto connect_failed;
	}

	if( s->after_connect )
		s->after_connect(s);

	return 0;

connect_failed:
	s->socket_fd = INVALID_SOCKET;
	return -1;
}

int client_destroy(socketClient_t *s)
{
	return client_disconnect(s);
}

int client_disconnect(socketClient_t* s)
{
	if (s->socket_fd != INVALID_SOCKET)
	{
		dprintf("%s: Disconnecting from %s\n", __FUNCTION__, s->remote.sun_path);
		close(s->socket_fd);
		s->socket_fd = INVALID_SOCKET;

		if( s->on_disconnect )
			s->on_disconnect(s);
	}
	return 0;
}

ssize_t client_read (socketClient_t *s, char *buf, size_t len)
{
	ssize_t res = -1;
	//dprintf("%s: Read %d bytes from %s\n", __FUNCTION__, len. s->remote.sun_path);
	if (s->socket_fd == INVALID_SOCKET && client_connect(s) != 0 )
	{
		//dprintf("%s: Invalid socket %s\n", __FUNCTION__, s->remote.sun_path);
		return -1;
	}

	fd_set rfds;
	struct timeval tv;
	FD_ZERO(&rfds);
	FD_SET(s->socket_fd, &rfds);
	tv.tv_sec = CLIENT_READ_TIMEOUT;
	tv.tv_usec = 0;
	res = select(s->socket_fd+1, &rfds, NULL, NULL, &tv);
	if( res <= 0 )
	{
		dprintf("%s: res = %d, timeout %d on %s\n", __FUNCTION__, res, CLIENT_READ_TIMEOUT, s->remote.sun_path);
		return res;
	}

	res = recv(s->socket_fd, buf, len, 0);
	//dprintf("%s: Read %d bytes\n", __FUNCTION__, res);
	if (res <= 0)
	{
		client_disconnect(s);
	}
	return res;
}


ssize_t client_write(socketClient_t *s, char *buf, size_t len)
{
	int res = -1;
	//dprintf("%s: Write '%s' %d bytes to %s\n", __FUNCTION__, buf, len, s->remote.sun_path);
	if (s->socket_fd == INVALID_SOCKET && client_connect(s) != 0)
	{
		//dprintf("%s: Invalid socket %s\n", __FUNCTION__, s->remote.sun_path);
		return -1;
	}
	res = send(s->socket_fd, buf, len, 0);
	if (res <= 0)
	{
		eprintf("%s: can't write to %s, res %d: %s\n", __FUNCTION__, s->remote.sun_path, res, strerror(errno));
		client_disconnect(s);
	}
	return res;
}
