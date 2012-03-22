/*
 StbCommandClient.c

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

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

#include <signal.h>
#include <bits/signum.h>
#include <stdlib.h>

#define INVALID_SOCKET -1
#define CMD_SOCKET     "/tmp/cmd.socket"

static int cmd_socket = INVALID_SOCKET;

int cmd_pipe_connect(const char *socket_path)
{
	int len;
	struct stat st;
	struct sockaddr_un remote;

	if (cmd_socket != INVALID_SOCKET)
	{
		//printf("cmd_pipe_connect: Close socket\n");
		close(cmd_socket);
		cmd_socket = INVALID_SOCKET;
	}

	if( stat( socket_path, &st) < 0)
	{
		//printf("CMD Can't stat %s\n", socket_path);
		cmd_socket = INVALID_SOCKET;
		return -1;
	}

	if ((cmd_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		//printf("cmd_pipe_connect: Can't create socket\n");
		cmd_socket = INVALID_SOCKET;
		return -1;
	}

	remote.sun_family = AF_UNIX;
	strcpy(remote.sun_path, socket_path);
	len = strlen(remote.sun_path) + sizeof(remote.sun_family);
	if (connect(cmd_socket, (struct sockaddr *)&remote, len) == -1) {
		//printf("CMD Can't open %s\n", socket_path);
		close(cmd_socket);
		cmd_socket = INVALID_SOCKET;
		return -1;
	}

	return 0;
}

int cmd_pipe_read(char *buf, int len)
{
	int res = -1;

	//eprintf("Read %d bytes\n", len);

	res = recv(cmd_socket, buf, len-1, 0);

	if (res > 0)
	{
    	    buf[res] = 0;
	}

	//printf("Read res %d %s\n", res, buf);

	return res;
}

int cmd_pipe_write(char *buf, int len)
{
	int res = -1;

	//eprintf("Write '%s' %d bytes\n", buf, len);

	res = send(cmd_socket, buf, len, 0);

	//eprintf("Write res %d\n", res);

	return res;
}

int main(int argc, char *argv[])
{
	int i, firsti, dowait, doprint;
	char buffer[4096];
	char *src, *dst;
	char *server_path;

	if (argc < 2)
	{
		printf("Usage: StbCommandClient [-nf socket] cmd arg1 arg2 ...\n");
		return 1;
	}

	firsti = 1;
	dowait = 1;
	doprint = 0;
	server_path = CMD_SOCKET;

	if (argv[1][0] == '-')
	{
	    firsti = 2;
	    i = 1;
	    while (argv[1][i] != 0)
	    {
		switch (argv[1][i])
		{
		    case 'n':
			dowait = 0;
			break;
		    case 'p':
			doprint = 1;
			break;
		    case 'f':
			firsti = 3;
			server_path = argv[2];
			break;
		    default:
			return -1;
		}
		i++;
	    }
	}

	if (cmd_pipe_connect(server_path) != 0)
	{
		//printf("Failed to connect to server\n");
		return -1;
	}

	dst = buffer;
	for (i=firsti; i<argc; i++)
	{
		src = argv[i];
		if (i != firsti)
		{
			*dst = ' ';
			dst++;
		}
		while (*src != 0 && dst-buffer < sizeof(buffer)-1)
		{
			*dst = *src;
			dst++;
			src++;
		}
	}
	*dst++ = '\r';
	*dst++ = '\n';
	*dst++ = 0;

	//printf("'%s'\n", buffer);

	cmd_pipe_write(buffer, strlen(buffer)+1);
	if (dowait)
	{
		cmd_pipe_read(buffer, sizeof(buffer));
		if (doprint)
		{
		    fwrite(buffer, 1, strlen(buffer), stdout);
		}
		return *((int*)buffer);
	}

	return 0;
}
