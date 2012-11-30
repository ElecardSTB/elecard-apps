/*
 mdevmonitor.c

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

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

#define MONITOR_PATH "/org/kernel/udev/monitor"

int main(int argc, char *argv[])
{
	if (argc < 2) {
		puts("Usage: mdevmonitor <message>");
		return 1;
	}

	int socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (socket_fd == -1) {
		fprintf(stderr, "failed to create socket: %s\n", strerror(errno));
		return -1;
	}

	struct sockaddr_un remote;
	memset(&remote, 0, sizeof(remote));
	remote.sun_family = AF_UNIX;
	strncpy(&remote.sun_path[1], MONITOR_PATH, sizeof(remote.sun_path)-1);
	if (connect(socket_fd, (struct sockaddr *)&remote,
	    sizeof(remote.sun_family)+1+strlen(&remote.sun_path[1])) == -1)
	{
		fprintf(stderr, "failed to connect socket: %s\n", strerror(errno));
		close(socket_fd);
		return -1;
	}
	ssize_t ret = send(socket_fd, argv[1], strlen(argv[1])+1, 0);
	if (ret <= 0)
		fprintf(stderr, "failed to write to socket: %s\n", strerror(errno));
	close(socket_fd);
	return ret > 0 ? 0 : -1;
}
