
/*
  SambaQuery
  Lists SMB network objects and resolves NetBIOS names.

  Copyright (C) Elecard Devices 2012

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <stdbool.h>

#include <libsmbclient.h>
#include <errno.h>

#if (defined SAMBA_VERSION) && (SAMBA_VERSION < 3)
#define samba_resolve_name resolve_name
extern int resolve_name(const char *name, struct in_addr *return_ip, int name_type);
#else
extern bool resolve_name(const char *name, struct sockaddr_storage *return_ss, int name_type);

static inline int samba_resolve_name(const char *name, struct in_addr *return_ip, int name_type)
{
	struct sockaddr_storage addr;
	int res = resolve_name(name, &addr, name_type);
	*return_ip = ((struct sockaddr_in*)&addr)->sin_addr;
	return res;
}
#endif

#define E_INVARGS       1
#define E_NAMENOTFOUND  2
#define E_OPENDIRFAILED 3
#define E_ACCESS        4
#define START_OUTPUT    "$$"

char *g_path = "";
char *g_user = "";
char *g_pass = "";

static void samba_auth_data_fn(const char *srv,
	const char *shr,
	char *wg, int wglen,
	char *un, int unlen,
	char *pw, int pwlen)
{
	(void)srv; (void)shr; (void)wg; (void)wglen;
	strncpy(un, g_user[0] != 0 ? g_user : "guest", unlen);
	strncpy(pw, g_pass, pwlen);
}

static void usage()
{
	puts("Usage:");
	puts("SambaQuery [options] path");
	puts("Browse options:");
	puts("  -u USER           Set the network username");
	puts("  -p PASS           Set user password");
	puts("SambaQuery -r name");
	puts("                    Resolve netbios name");
	exit(E_INVARGS);
}

int main(int argc, char **argv)
{
	if (argc <= 1)
	{
		usage();
	}

	smbc_init(samba_auth_data_fn, 0);

	if (argc==3 && argv[1][0]=='-' && argv[1][1]=='r')
	{
		struct in_addr ip;
		if (samba_resolve_name(argv[2], &ip, 0x20))
		{
			puts(START_OUTPUT);
			puts(inet_ntoa(ip));
			return 0;
		} else
			return E_NAMENOTFOUND;
	}

	int i;
	for (i = 1; i <= argc; i++)
	{
		if (argv[i][0]=='-' && argv[i][1]=='u')
		{
			i++;
			g_user = argv[i];
		} else
		if (argv[i][0]=='-' && argv[i][1]=='p')
		{
			i++;
			g_pass = argv[i];
		} else
		{
			g_path = argv[i];
			break;
		}
	}

	int dh = smbc_opendir(g_path);
	if (dh < 0)
		return errno == EACCES ? E_ACCESS : E_OPENDIRFAILED;

	puts(START_OUTPUT);
	struct smbc_dirent *pdirent;
	while ((pdirent = smbc_readdir(dh)) != NULL)
	{
		if (pdirent->name[strlen(pdirent->name)-1] != '$')
			puts(pdirent->name);
	}
	smbc_closedir(dh);

	return 0;
}
