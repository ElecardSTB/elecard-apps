
/*
 messages.c

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

#include "messages.h"

#ifdef ENABLE_MESSAGES

#include "debug.h"
#include "l10n.h"
#include "app_info.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

/******************************************************************
* EXPORTED DATA                                                   *
*******************************************************************/

interfaceListMenu_t MessagesMenu;

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

int messages_count;
static struct dirent **messages_entries = NULL;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int messages_leaving_menu(interfaceMenu_t *pMenu, void* pArg);
static int messages_showMessage(interfaceMenu_t *pMenu, void *pArg);
static void messages_freeBrowseInfo();
static int messages_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

int messages_displayFile(const char *filename)
{
	int f;
	int status;
	struct stat stat_info;
	char buf[MAX_MESSAGE_BOX_LENGTH];

	status = lstat( filename, &stat_info);
	if(status<0 || !(stat_info.st_mode & S_IFREG) || S_ISLNK(stat_info.st_mode) )
	{
		interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);
		return 1;
	}
	if( stat_info.st_size > MAX_MESSAGE_BOX_LENGTH )
	{
		interface_showMessageBox(_T("ERR_FILE_TOO_BIG"), thumbnail_error, 3000);
		return 1;
	}
	f = open(filename, O_RDONLY);
	if( f < 0 )
	{
		interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);
		return 1;
	}
	status = read(f, buf, MAX_MESSAGE_BOX_LENGTH);
	if( status >= 0 )
	{
		if( status > MAX_MESSAGE_BOX_LENGTH-3 )
		{
			status = MAX_MESSAGE_BOX_LENGTH-3;
		}
		buf[status] = 0;
		buf[status+1] = 0;
	}
	close(f);
	interface_showScrollingBox(buf, 0, NULL, NULL);
	return 0;
}

int messages_select(const struct dirent * de)
{
	struct stat stat_info;
	int         status;
	int         nameLength;
	static char full_path[PATH_MAX];
	sprintf(full_path, "%s%s", MESSAGES_DIR, de->d_name);
	status = lstat( full_path, &stat_info);
	/* Files with size > 2GB will give overflow error */
	if( status<0 && errno != EOVERFLOW )
	{
		return 0;
	} else if ( status>=0 && (!(stat_info.st_mode & S_IFREG) || S_ISLNK(stat_info.st_mode)) )
	{
		return 0;
	}
	nameLength = strlen(full_path);
	if( strncasecmp( &full_path[nameLength-4], ".txt", 4 ) == 0 )
	{
		return 1;
	}
	return 0;
}

static int messages_leaving_menu(interfaceMenu_t *pMenu, void* pArg)
{
	dprintf("%s: in\n", __FUNCTION__);
	messages_freeBrowseInfo();
	return 0;
}

static void messages_freeBrowseInfo()
{
	int i;

	if(messages_entries != NULL)
	{
		for( i = 0 ; i < messages_count; ++i )
			free(messages_entries[i]);
		free(messages_entries);
		messages_entries = NULL;
		messages_count = 0;
	}
}

void messages_buildMenu(interfaceMenu_t *pParent)
{
	int messages_icons[4] = {0,0,0,0};
	createListMenu(&MessagesMenu, _T("MESSAGES"), thumbnail_messages, messages_icons, pParent,
		interfaceListMenuIconThumbnail, messages_fillMenu, messages_leaving_menu, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&MessagesMenu, messages_keyCallback);
}

int messages_fillMenu(interfaceMenu_t *pMenu, void *pArg)
{
	int i;
	struct stat stat_info;
	int         status;
	char full_path[PATH_MAX];
	char *str;

	interface_clearMenuEntries((interfaceMenu_t *)&MessagesMenu);
	messages_freeBrowseInfo();

	messages_count = scandir(MESSAGES_DIR, &messages_entries, messages_select, alphasort);
	if( messages_count <= 0 )
	{
		str = _T("NO_FILES");
		interface_addMenuEntryDisabled((interfaceMenu_t *)&MessagesMenu, str, 0);
		return 0;
	}

	for( i = 0; i < messages_count; i++ )
	{
		sprintf(full_path, "%s%s", MESSAGES_DIR, messages_entries[i]->d_name);
		status = lstat( full_path, &stat_info);
		if( status >= 0 && stat_info.st_mtime <= appControlInfo.messagesInfo.lastWatched )
		{
			status = thumbnail_message_open;
		} else
			status = thumbnail_message_new;
		str = &full_path[strlen(MESSAGES_DIR)];
		str[strlen(str)-4] = 0;
		interface_addMenuEntry((interfaceMenu_t *)&MessagesMenu, str, messages_showMessage, (void*)i, status);
	}
	if( interface_getSelectedItem((interfaceMenu_t*)&MessagesMenu) >= messages_count )
	{
		interface_setSelectedItem((interfaceMenu_t*)&MessagesMenu, messages_count-1 );
	}

	return 0;
}

static int messages_showMessage(interfaceMenu_t *pMenu, void *pArg)
{
	char full_path[PATH_MAX];
	int messageIndex = (int)pArg;
	int res;
	struct stat stat_info;
	int         status;

	sprintf(full_path, "%s%s", MESSAGES_DIR, messages_entries[messageIndex]->d_name);
	if( (res = messages_displayFile(full_path)) == 0 )
	{
		if( MessagesMenu.baseMenu.menuEntry[messageIndex].thumbnail == thumbnail_message_new && (messageIndex == 0 || MessagesMenu.baseMenu.menuEntry[messageIndex-1].thumbnail == thumbnail_message_open ) )
		{
			status = lstat( full_path, &stat_info);
			if( status >= 0 && stat_info.st_mtime > appControlInfo.messagesInfo.lastWatched )
			{
				appControlInfo.messagesInfo.lastWatched = stat_info.st_mtime;
				appControlInfo.messagesInfo.newMessages = messages_checkNew();
				saveAppSettings();
				messages_fillMenu(pMenu, pArg);
			}
		}
	}
	return res;
}

static int messages_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int messageIndex = interface_getSelectedItem((interfaceMenu_t*)&MessagesMenu);
	char full_path[PATH_MAX];

	if( messages_count <= 0 || messageIndex < 0 )
		return 1;

	switch( cmd->command )
	{
		case interfaceCommandRed:
			sprintf(full_path, "%s%s", MESSAGES_DIR, messages_entries[messageIndex]->d_name);
			unlink(full_path);
			messages_fillMenu(pMenu, pArg);
			interface_displayMenu(1);
			return 0;
		default: ;
	}

	return 1;
}

int messages_checkNew()
{
	struct dirent *de;
	DIR *dir;
	struct stat stat_info;
	char full_path[PATH_MAX];
	int status;

	dir = opendir(MESSAGES_DIR);
	if (dir != NULL)
	{
		while((de = readdir(dir))!=NULL)
		{
			if(de->d_type == DT_REG || de->d_type == DT_UNKNOWN )
			{
				sprintf(full_path, "%s%s", MESSAGES_DIR, de->d_name);
				status = lstat( full_path, &stat_info);
				if( status >= 0 && stat_info.st_mtime > appControlInfo.messagesInfo.lastWatched )
				{
					closedir(dir);
					return 1;
				}
			}
		}
		closedir(dir);
	}
	return 0;
}

#endif // ENABLE_MESSAGES
