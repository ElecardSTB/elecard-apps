
/*
 m3u.c

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

#include "m3u.h"
#include "defines.h"
#include "debug.h"

#include <stdio.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define TMPFILE "/tmp/m3u.tmp"

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

char m3u_description[MENU_ENTRY_INFO_LENGTH];
char m3u_url[MAX_URL];

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

int m3u_readEntry(FILE *f)
{
	int state = 2;
	char* str;
	if( f == NULL )
	{
		return -1;
	}
	
	while( state > 0 && fgets( m3u_url, sizeof(m3u_url), f ) != NULL )
	{
		switch( m3u_url[0] )
		{
			case '#':
				if( state == 1)
				{
					break; // comment?
				}
				if(strncmp(m3u_url, EXTINF, 7) == 0)
				{
					str = index(m3u_url, ',');
					if(str != NULL)
					{
						strncpy( m3u_description, &str[1], sizeof(m3u_description) );
						m3u_description[sizeof(m3u_description)-1] = 0;
						str = index(m3u_description, '\n');
						if( str != NULL)
						{
							*str = 0;
						}
						state = 1;
					} else
					{
						dprintf("%s: Invalid EXTINF format '%s'\n", __FUNCTION__, m3u_url);
					}
				}
				break;
			case '\t':
			case '\n':
			case '\r':
				break; // empty string
			default:
				if(state != 1)
				{
					//dprintf(%s: EXTINF " section not found!\n", __FUNCTION__);
					strncpy( m3u_description, m3u_url, sizeof(m3u_description) );
					m3u_description[sizeof(m3u_description)-1] = 0;
					str = index(m3u_description, '\n');
					if( str != NULL)
					{
						*str = 0;
					}
				}
				str = index(m3u_url, '\n');
				if( str != NULL)
				{
					*str = 0;
				}
				state = 0;
		}
	}
	if( state > 0)
	{
		fclose(f);
	}
	return state;
}

FILE*  m3u_initFile(const const char *m3u_filename, const char* mode)
{
	FILE* f;
	f = fopen( m3u_filename, mode);
	if( f == NULL)
	{
		eprintf("M3U: Can't fopen '%s'\n", m3u_filename);
		return NULL;
	}
	
	if( fgets(m3u_url, MAX_URL, f) == NULL || strncmp( m3u_url, EXTM3U, 7 ) != 0 )
	{
		eprintf("M3U: '%s' is not a valid M3U file\n", m3u_filename);
		fclose(f);
		return NULL;
	}
	
	return f;
}

int m3u_createFile(const char *m3u_filename)
{
	FILE *f = fopen( m3u_filename, "w");
	if( f == NULL)
	{
		eprintf("M3U: Can't create m3u file at '%s'\n", m3u_filename);
		return -1;
	}
	fputs(EXTM3U "\n", f);
	fclose(f);
	return 0;
}

int m3u_getEntry(const char *m3u_filename, int selected)
{
	FILE* f;
	int i = 0;

	f = m3u_initFile(m3u_filename, "r");
	if( f == NULL )
	{
		return -1;
	}
	
	while ( m3u_readEntry(f) == 0 )
	{
		if (i == selected)
		{
			fclose(f);
			return 0;
		}
		i++;
	}
	
	return 1;
}

int m3u_findUrl(const char *m3u_filename, const char *url)
{
	FILE* f;

	f = m3u_initFile(m3u_filename, "r");
	if( f == NULL )
	{
		return -1;
	}
	
	while ( m3u_readEntry(f) == 0 )
	{
		if (strcasecmp(m3u_url, url) == 0)
		{
			fclose(f);
			return 0;
		}
	}
	
	return 1;
}

int m3u_findDescription(const char *m3u_filename, char *description)
{
	FILE* f;

	f = m3u_initFile(m3u_filename, "r");
	if( f == NULL )
	{
		return -1;
	}
	
	while ( m3u_readEntry(f) == 0 )
	{
		if (strcasecmp(m3u_description, description) == 0)
		{
			fclose(f);
			return 0;
		}
	}
	
	return 1;
}

int m3u_addEntry(const char *m3u_filename, const char *url, const char *description)
{
	FILE* f;

	if(url == NULL)
	{
		return -2;
	}

	f = fopen(m3u_filename, "a");
	if( f == NULL )
	{
		return -1;
	}
	if(description != NULL)
	{
		char *ptr;
		ptr = index( description, '\n' );
		if( ptr )
		{
			char tmp[MENU_ENTRY_INFO_LENGTH];
			size_t tmp_length;

			if( (tmp_length = ptr-description) >= MENU_ENTRY_INFO_LENGTH )
				tmp_length = MENU_ENTRY_INFO_LENGTH-1;
			strncpy(tmp, description, tmp_length);
			tmp[tmp_length] = 0;
			fprintf(f, EXTINF ":-1,%s\n", tmp);
		} else
			fprintf(f, EXTINF ":-1,%s\n", description);
	}
	fprintf(f, "%s\n", url);
	fclose(f);
	return 0;
}

int m3u_deleteEntryByIndex(const char *m3u_filename, int index)
{
	FILE *old_file;
	int i, res;
	char cmd[PATH_MAX];
	if( index < 0)
	{
		return -2;
	}
	old_file = m3u_initFile(m3u_filename, "r");
	if(old_file == NULL)
	{
		return 1;
	}
	if( m3u_createFile( TMPFILE ) != 0)
	{
		fclose(old_file);
		return 1;
	}
	i = 0; res = 0;
	while ( m3u_readEntry(old_file) == 0 && res == 0 )
	{
		if( i != index)
		{
			res = m3u_addEntry(TMPFILE, m3u_url, m3u_description);
		}
		i++;
	}
	if( res == 0)
	{
		sprintf(cmd, "mv -f \"" TMPFILE "\" \"%s\"", m3u_filename );
		system(cmd);
	}
	return res;
}

int m3u_deleteEntryByUrl(const char *m3u_filename, const char *url)
{
	FILE *old_file;
	int i, res;
	char cmd[PATH_MAX];
	if( url == NULL)
	{
		return -2;
	}
	old_file = m3u_initFile(m3u_filename, "r");
	if(old_file == NULL)
	{
		return 1;
	}
	if( m3u_createFile( TMPFILE ) != 0)
	{
		fclose(old_file);
		return 1;
	}
	i = 0; res = 0;
	while ( m3u_readEntry(old_file) == 0 && res == 0 )
	{
		if( strcasecmp(m3u_url, url) != 0)
		{
			res = m3u_addEntry(TMPFILE, m3u_url, m3u_description);
		}
		i++;
	}
	if( res == 0)
	{
		sprintf(cmd, "mv -f \"" TMPFILE "\" \"%s\"", m3u_filename );
		system(cmd);
	}
	return res;
}

int m3u_replaceEntryByIndex(const char *m3u_filename, int index, const char *url, const char *description)
{
	FILE *old_file;
	int i, res;
	char cmd[PATH_MAX];
	if( index < 0 || url == NULL)
	{
		return -2;
	}
	old_file = m3u_initFile(m3u_filename, "r");
	if(old_file == NULL)
	{
		return 1;
	}
	if( m3u_createFile( TMPFILE ) != 0)
	{
		fclose(old_file);
		return 1;
	}
	i = 0; res = 0;
	while ( m3u_readEntry(old_file) == 0 && res == 0 )
	{
		if( i != index)
		{
			res = m3u_addEntry(TMPFILE, m3u_url, m3u_description);
		} else {
			res = m3u_addEntry(TMPFILE, url, description[0] != 0 ? description : NULL );
		}
		i++;
	}
	if( res == 0)
	{
		sprintf(cmd, "mv -f \"" TMPFILE "\" \"%s\"", m3u_filename );
		system(cmd);
	}
	return res;
}

int m3u_readEntryFromBuffer(char **pData, int *length)
{
	char *ptr, *ptr2, *str;
	int state = 2, ptr_length;

	if( pData == NULL || length == NULL)
	{
		return -2;
	}
	ptr = *pData;
	while( *length > 0 && state > 0 )
	{
		if ( (ptr2 = strchr(ptr, '\r')) != NULL || (ptr2 = strchr(ptr, '\n')) != NULL )
		{
			/* Skip line ends and whitespaces */
			for(; *ptr2 && *ptr2 <= ' '; ptr2++ )
			{
				ptr2[0] = 0;
			}
			ptr_length = strlen(ptr);
		} else
		{
			ptr_length = strlen(ptr);
			ptr2 = ptr+ptr_length;
		}
		if ( ptr_length > 0 )
		{
			//dprintf("%s: Got string '%s' (%d)\n", __FUNCTION__, ptr, state);
			switch( *ptr )
			{
				case '#':
					if(strncmp(ptr, EXTINF, 7) == 0)
					{
						str = index(ptr, ',');
						if(str != NULL)
						{
							strncpy( m3u_description, &str[1], MENU_ENTRY_INFO_LENGTH );
							m3u_description[MENU_ENTRY_INFO_LENGTH-1] = 0;
							state = 1;
						} else
						{
							dprintf("%s: Invalid EXTINF format '%s'\n", __FUNCTION__, ptr);
							m3u_description[0] = 0;
						}
					} else
					{
						dprintf("%s: #EXTINF expected but '%s' found\n", __FUNCTION__, ptr);
					}
					break;
				default:
					if( ptr_length < MAX_URL )
					{
						strcpy( m3u_url, ptr );
					} else
					{
						strncpy( m3u_url, ptr, MAX_URL );
						m3u_url[MAX_URL-1] = 0;
					}
					if(state != 1)
					{
						//dprintf("%s: " EXTINF " section not found!\n", __FUNCTION__);
						strncpy( m3u_description, m3u_url, MENU_ENTRY_INFO_LENGTH );
						m3u_description[MENU_ENTRY_INFO_LENGTH-1] = 0;
					}
					state = 0;
			}
		}
		*length -= ptr2-ptr;
		ptr = ptr2;
	}// while ( state > 0 && ptr2 != NULL && *length-(ptr - *pData) > 0 );
	*pData = ptr;
	return state;
}
