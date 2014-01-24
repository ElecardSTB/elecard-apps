
/*
 stats.c

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

#include "stats.h"
#ifdef ENABLE_STATS
#include "debug.h"
#include "app_info.h"
#include "off_air.h"
#include "xmlconfig.h"
#include "StbMainApp.h"
#include "media.h"
#include "l10n.h"

#include <service.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>

char *strptime(const char *s, const char *format, struct tm *tm);

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

enum statsParseState_t {
	parserReady = 0,
	parserReadDate,
	parserReadTimezone,
	parserReadChannelcount,
	parserReadHeader,
	parserFinished,
	parserError
};

/***********************************************
* EXPORTED DATA                                *
************************************************/

statsInfo_t statsInfo;
interfaceListMenu_t StatsMenu;
time_t stats_lastTime;

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

static int offair_select_usb(const struct dirent * de)
{
	struct stat stat_info;
	int         status;
	char        full_path[PATH_MAX];
	if (strncmp(de->d_name, "Drive ", sizeof("Drive ")-1) == 0)
	{
		// Skip CD/DVD Drives since we add them manually
		return 0;
	}
	sprintf(full_path,"%s%s",usbRoot,de->d_name);
	status = stat( full_path, &stat_info);
	if(status<0)
		return 0;
	return (stat_info.st_mode & S_IFDIR) && (de->d_name[0] != '.');
}

static int stats_selectStorage(interfaceMenu_t *pMenu, void* pArg)
{
	char storagePath[BUFFER_SIZE];
	int storageIndex = (int)pArg;
	int res;

	if(storageIndex >= 0)
	{
		sprintf(storagePath, "mv '" STATS_FILE "' '%s",usbRoot);
		interface_getMenuEntryInfo((interfaceMenu_t*)&StatsMenu,storageIndex,&storagePath[strlen(storagePath)],MENU_ENTRY_INFO_LENGTH);
		strcpy(&storagePath[strlen(storagePath)],"/stats.txt'");
		res = system( storagePath );
		if( WIFEXITED(res) == 1 && WEXITSTATUS(res) == 0 )
		{
			interface_showMessageBox(_T("PROFILE_SAVED"), thumbnail_info, 3000);
		} else
		{
			interface_showMessageBox(_T("ERR_PROFILE_SAVE"), thumbnail_error, 0);
		}
	}
	return 0;
}

static int stats_fillLocationMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int i,storageCount;
	struct dirent **usb_storages;
	char *str;

	interface_clearMenuEntries((interfaceMenu_t *)&StatsMenu);

	if( media_scanStorages() <= 0 )
	{
		str = _T("USB_NOTFOUND");
		interface_addMenuEntryDisabled((interfaceMenu_t *)&StatsMenu, str, thumbnail_info);
	}
	
	storageCount = scandir(usbRoot, &usb_storages, offair_select_usb, alphasort);
	for( i = 0 ; i < storageCount; ++i )
	{
		interface_addMenuEntry((interfaceMenu_t *)&StatsMenu, usb_storages[i]->d_name, 
		                       stats_selectStorage,
		                       SET_NUMBER(interface_getMenuEntryCount((interfaceMenu_t *)&StatsMenu)), 
		                       thumbnail_usb);
		free(usb_storages[i]);
	}
	free(usb_storages);

	return 0;
}

int stats_buildMenu(interfaceMenu_t* pParent)
{
	createListMenu(&StatsMenu, _T("RECORDED_LIST"), thumbnail_usb, NULL, pParent,
	               interfaceListMenuIconThumbnail, stats_fillLocationMenu, NULL, NULL);
	return 0;
}

int stats_load()
{
	FILE *f;
	char  buf[BUFFER_SIZE];
	char *str, *ptr;
	int   index;
	struct tm temp_date;
	time_t temp_timezone;
	int   channelCount;
	int   channelMap[MAX_MEMORIZED_SERVICES];
	int   i;
	enum statsParseState_t state = parserReady;
	EIT_common_t header;

	f = fopen(STATS_TMP_FILE, "r");
	if( f != NULL )
	{
		while( state != parserError && fgets(buf, BUFFER_SIZE, f) != NULL )
		{
#if 0
			switch(state)
			{
				case parserReady:            dprintf("%s: import: rd ", __FUNCTION__);break;
				case parserReadDate:         dprintf("%s: import: dt ", __FUNCTION__);break;
				case parserReadTimezone:     dprintf("%s: import: tz ", __FUNCTION__);break;
				case parserReadChannelcount: dprintf("%s: import: cc ", __FUNCTION__);break;
				case parserReadHeader:       dprintf("%s: import: hd ", __FUNCTION__);break;
				case parserFinished:         dprintf("%s: import: fi ", __FUNCTION__);break;
				default:                     dprintf("%s: import: er ", __FUNCTION__);
			}
			puts(buf);
#endif
			str = buf;
			switch(state)
			{
				case parserReady:
					if(strncasecmp(buf, "DATE=", 5 ) == 0)
					{
						if (strptime(&buf[5], "%F", &temp_date) == NULL )
						{
							state = parserError;
						} else
						{
							state = parserReadDate;
						}
					} else
					{
						dprintf("%s: DATE expected but '%s' found\n", __FUNCTION__, buf);
						state = parserError;
					}
					break;
				case parserReadDate:
					if(strncasecmp(buf, "TIMEZONE=", 9 ) == 0)
					{
						temp_timezone = atol(&buf[9]);
						state = parserReadTimezone;
					} else
					{
						dprintf("%s: TIMEZONE expected but '%s' found\n", __FUNCTION__, buf);
						state = parserError;
					}
					break;
				case parserReadTimezone:
					if(strncasecmp(buf, "CHANNELCOUNT=", 13 ) == 0)
					{
						channelCount = atoi(&buf[13]);
						state = parserReadChannelcount;
						memset(channelMap, 0, sizeof(channelMap));
					} else
					{
						dprintf("%s: CHANNELCOUNT expected but '%s' found\n", __FUNCTION__, buf);
						state = parserError;
					}
					break;
				case parserReadChannelcount:
					if(sscanf(buf, "CHANNEL%03d=%lu;%hu;%hu", &index, &header.media_id, &header.service_id, &header.transport_stream_id) == 4)
					{
						if( index < 0 || index > channelCount )
						{
							dprintf("%s: wrong channel index %d (%s)\n", __FUNCTION__, index, buf);
							state = parserError;
						} else {
							int32_t serviceCount = dvbChannel_getCount();
							i = dvbChannel_getServiceId(&header);
							if(i >= 0) {
								channelMap[index] = i;
							} else if(serviceCount < MAX_MEMORIZED_SERVICES) {
								EIT_service_t *service = offair_getService(serviceCount - 1);

								if(service) {
									service->service = NULL;
									service->common = header;
									channelMap[index] = serviceCount;
									serviceCount++;
								}
							}
						}
						break;
					} else if ( strncasecmp(buf, "STATS=", 6) != 0 )
					{
						dprintf("%s: CHANNEL### expected but '%s' found\n", __FUNCTION__, buf);
						state = parserError;
						break;
					} else
					{
						state = parserReadHeader;
						index = 0;
						for( str = &buf[6]; *str && *str <= ' '; ++str );
					}
				case parserReadHeader:
					while( *str && index < STATS_SAMPLE_COUNT )
					{
						i = strtol(str, &ptr, 10);
						statsInfo.watched[index++] = channelMap[i];
						for( str = ptr; *str && *str <= ' '; ++str );
					}
					if( index == STATS_SAMPLE_COUNT )
					{
						state = parserFinished;
					}
					break;
				case parserFinished:
					dprintf("%s: EOF expected, but '%s' found\n", __FUNCTION__, buf);
				default: ; //parserError
			}
		}
		if( state == parserReadHeader && index < STATS_SAMPLE_COUNT )
		{
			state = parserFinished;
			eprintf("%s: has read %d samples, still need %d. Maybe incomplete file?\n", __FUNCTION__, index, STATS_SAMPLE_COUNT - index - 1);
		}
		fclose(f);
	}

	return state != parserFinished;
}

int stats_save()
{
	FILE *f;
	char  buf[32];
	int i;
	int32_t serviceCount;

	f = fopen(STATS_TMP_FILE, "w");
	if(f == NULL) {
		eprintf("Stats: Can't open temp file\n");
		return 1;
	}
	serviceCount = dvbChannel_getCount();
	strftime(buf,32,"DATE=%F\n", gmtime(&statsInfo.today));
	fputs(buf, f);
	fprintf(f,"TIMEZONE=%ld\n", timezone);
	fprintf(f,"CHANNELCOUNT=%d\n", serviceCount);
	for(i = 1; i < serviceCount; ++i) {
		EIT_service_t *service = offair_getService(i);
		if(service) {
			fprintf(f,"CHANNEL%03d=%lu;%hu;%hu\n", i,
					service->common.media_id,
					service->common.service_id,
					service->common.transport_stream_id);
		}
	}
	fputs("STATS=", f);
	for(i = 0; i < STATS_SAMPLE_COUNT; ++i) {
		if(i % 30 == 0) {
			fputc('\n', f);
		}
		fprintf(f, "%d ", statsInfo.watched[i]);
	}
	fputs("\n\n", f);
	fclose(f);
	return 0;
}

/*
 * Trying to read header of current stat file.
 * If date and timezone is matching current - load file and continue collecting data.
 * If date is in past, export file and start new temp file.
 * If file has incorrect format, start new temp file.
 */
int stats_init()
{
	struct tm *t, temp_date;
	FILE *f;
	char buf[BUFFER_SIZE];
	time_t temp_timezone;
	int res;

	memset(statsInfo.watched, 0, sizeof(statsInfo.watched));
	statsInfo.today = time(NULL);
	statsInfo.today -= statsInfo.today % (24*60*60);
	dprintf("%s: today is %ld (%s UTC)\n", __FUNCTION__, statsInfo.today, ctime(&statsInfo.today));
	t = gmtime(&statsInfo.today);
	res = 0; // ignore
	f = fopen(STATS_TMP_FILE, "r");
	if( f != NULL )
	{
		if( fgets( buf, BUFFER_SIZE, f ) != NULL )
		{
			if( strncasecmp(buf, "DATE=", 5 ) == 0 && strptime(&buf[5], "%F", &temp_date) != NULL )
			{
				if( fgets( buf, BUFFER_SIZE, f ) != NULL )
				{
					if( strncasecmp(buf, "TIMEZONE=", 9 ) == 0 )
					{
						temp_timezone = atol(&buf[9]);
						if( temp_timezone == timezone && t->tm_year == temp_date.tm_year &&
							t->tm_mon == temp_date.tm_mon && t->tm_mday == temp_date.tm_mday )
						{
							res = 2; // load
						} else
						{
							res = 1; // export
						}
					}
				} else
				{
					dprintf("%s: wrong file format in current stat file:\n'%s' found, 'TIMEZONE=' expected. Ignoring\n", __FUNCTION__, buf);
				}
			} else
			{
				dprintf("%s: wrong file format in current stat file:\n'%s' found, 'DATE=' expected. Ignoring\n", __FUNCTION__, buf);
			}
		}
		fclose(f);
	}
	switch( res )
	{
		case 2:    // load
			dprintf("%s: loading temp file\n", __FUNCTION__);
			stats_load();
			break;
		case 1:    // export
			dprintf("%s: saving temp file\n", __FUNCTION__);
			system( "cat " STATS_TMP_FILE " >> " STATS_FILE);
			break;
		default: ; // ignore
			//dprintf("%s: starting temp file\n", __FUNCTION__);
	}
	return 0;
}

#endif // #ifdef ENABLE_STATS
