
/*
 l10n.c

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

#include <directfb.h>
#include "app_info.h"
#include "debug.h"
#include "sem.h"
#include "gfx.h"
#include "interface.h"
#include "l10n.h"
#include "output.h"
#include "StbMainApp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#ifndef LANGUAGE_DIR
	#define LANGUAGE_DIR "/opt/elecard/share/StbMainApp/languages/"
#endif

#define TEXT_ENTRY_BUFFER_SIZE     (2048)
#define ENTRIES_CAPACITY_INCREMENT (20)

#define LANG_RUSSIAN	"Русский"
#define LANG_ENGLISH	"English"
#define LANG_GEORGIAN	"Georgian"
#define LANG_KAZAH		"Казақша"

#ifdef LANG
	#define DEFAULT_LANGUAGE LANG
#else
	#define DEFAULT_LANGUAGE LANG_ENGLISH
#endif

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

typedef struct
{
	char *key;
	char *value;
} l10n_textEntry;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int   l10n_readAndInitTextEntry(char* entrySource, int entryIndex);
static int   l10n_setTextEntry(const char* entryText, int entryIndex);
static int   lang_select(const struct dirent * de);
static int   l10n_findKey(const char* key);
static int   l10n_enumLanguageFiles(struct dirent ***langEntries);
static FILE *l10n_findLanguageFile(const char* language);
static int   l10n_fillLanguageMenu(interfaceMenu_t *pMenu, void* pArg);
static int   l10n_menuSelectLanguage(interfaceMenu_t *pMenu, void* pArg);
static int   l10n_confirmLanguageChange(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int   free_language_list(interfaceMenu_t *pMenu, void* pArg);
static void  parseEscapeCharacters(char *str);
static int   part(int l, int r);
static void  quicksort(int l, int t);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static l10n_textEntry *l10n_textEntries;
static int             l10n_textEntriesCount;
static char            l10n_languageKey[] = "LANG_NAME";
static l10n_textEntry *l10n_languages;
static int             l10n_languageCount = 0;
static int            *l10n_sortedIndexes;

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

const char          l10n_languageDir[] = LANGUAGE_DIR;
char                l10n_currentLanguage[MAX_LANG_NAME_LENGTH] = DEFAULT_LANGUAGE;
interfaceListMenu_t LanguageMenu;

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/


int l10n_init(const char* languageName)
{
	l10n_textEntry*  newEntries;
	/* Entries will be loaded by portions of constant size to decrease usage of realloc */
	int currentEntriesCapacity = ENTRIES_CAPACITY_INCREMENT;
	FILE* lang_file = l10n_findLanguageFile(languageName);
	if(lang_file == NULL)
	{
		eprintf("l10n: Can't find file '%s' language file!\n",languageName);
		if((lang_file = l10n_findLanguageFile(DEFAULT_LANGUAGE)) == NULL && (lang_file = l10n_findLanguageFile(NULL)) == NULL)
		{
			eprintf("l10n: Error: Can't load default language " DEFAULT_LANGUAGE "!\n");
			return -1;
		}
	}
	else
	{
		dprintf("l10n: Initializing %s language\n",languageName);
	}

	char buffer[TEXT_ENTRY_BUFFER_SIZE];
	int  bufferLength;
	int i;

	l10n_textEntries = (l10n_textEntry*)dmalloc( sizeof(l10n_textEntry) * ENTRIES_CAPACITY_INCREMENT );
	l10n_textEntriesCount    = 0;
	while(fgets(buffer, TEXT_ENTRY_BUFFER_SIZE, lang_file) != NULL)
	{
		bufferLength = strlen(buffer);
		if(buffer[bufferLength-1] == '\n')
			buffer[bufferLength-1] = 0;
		/* Here we can make some checks on input string */
		if(l10n_readAndInitTextEntry(buffer,l10n_textEntriesCount) == 0)
		{
			l10n_textEntriesCount++;
			if(l10n_textEntriesCount % ENTRIES_CAPACITY_INCREMENT == 0)
			{
				newEntries = (l10n_textEntry*)drealloc( l10n_textEntries, sizeof(l10n_textEntry) * (l10n_textEntriesCount + ENTRIES_CAPACITY_INCREMENT) );
				if(newEntries == NULL)
				{
					dprintf("l10n: Can't increment storage capacity for text entries! Current capacity=%d\n",currentEntriesCapacity);
					break;
				}
				else
				{
					currentEntriesCapacity = l10n_textEntriesCount + ENTRIES_CAPACITY_INCREMENT;
					l10n_textEntries = newEntries;
				}
			}
		}
	}
	fclose(lang_file);
	if(l10n_textEntriesCount % ENTRIES_CAPACITY_INCREMENT != 0)
	{
		newEntries = (l10n_textEntry*)drealloc( l10n_textEntries, sizeof(l10n_textEntry) * l10n_textEntriesCount );
		if(newEntries == NULL)
		{
			dprintf("l10n: Can't adjust storage size for %d text entries! Current capacity=%d\n",l10n_textEntriesCount, currentEntriesCapacity);
			for( i = l10n_textEntriesCount; i < currentEntriesCapacity ; ++i )
			{
				l10n_textEntries[i].key = NULL; l10n_textEntries[i].value = NULL;
			}
			l10n_textEntriesCount = currentEntriesCapacity;
		}
		else
			l10n_textEntries = newEntries;
	}

	l10n_sortedIndexes = (int*)dmalloc( sizeof(int) * l10n_textEntriesCount );
	for( i = 0; i < l10n_textEntriesCount; ++i )
		l10n_sortedIndexes[i] = i;
	quicksort(0,l10n_textEntriesCount-1);
/*#ifdef DEBUG
	for( i = 0; i < l10n_textEntriesCount; ++i )
		dprintf("%s: key[%3d]=%s\n",__FUNCTION__,i,l10n_textEntries[l10n_sortedIndexes[i]].key);
#endif*/
	return 0;
}

void l10n_cleanup()
{
	dprintf("l10n: cleaning %d text entries\n",l10n_textEntriesCount);
	int i;
	for( i = 0; i < l10n_textEntriesCount; ++i)
	{
		dfree(l10n_textEntries[i].key);
		dfree(l10n_textEntries[i].value);
	}
	dfree(l10n_textEntries);
	dfree(l10n_sortedIndexes);
}

static void  quicksort(int l, int t)
{
	int i;
	if(l<t)
	{
		i = part(l, t);
		quicksort(l,i-1);
		quicksort(i+1,t);
	}
}

static int part(int l, int r)
{
	int  v, i, j, b;
	v = l10n_sortedIndexes[r];
	i = l-1;
	j = r;
	do
	{
		do
		{
			j--;
		}
		while (strcmp(l10n_textEntries[l10n_sortedIndexes[j]].key,l10n_textEntries[v].key) > 0 && j != i+1);
		do
		{
			i++;
		}
		while (strcmp(l10n_textEntries[l10n_sortedIndexes[i]].key,l10n_textEntries[v].key) < 0 && i != j-1);
		b = l10n_sortedIndexes[i];
		l10n_sortedIndexes[i] = l10n_sortedIndexes[j];
		l10n_sortedIndexes[j] = b;
	}
	while (i<j);
	l10n_sortedIndexes[j] = l10n_sortedIndexes[i];
	l10n_sortedIndexes[i] = l10n_sortedIndexes[r];
	l10n_sortedIndexes[r] = b;
	return i;
}

static void  parseEscapeCharacters(char *str)
{
	char *writing, *reading;
	/* Parsing source string for \n,\r,\t */
	reading = writing = str;
	for( ; reading[0] ; ++reading, ++writing)
	{
		if(reading[0] == '\\')
			switch(reading[1])
			{
				case 'n':
					reading = &reading[1];
					writing[0] = '\n';
					continue;
				case 't':
					reading = &reading[1];
					writing[0] = '\t';
					continue;
				case 'r':
					reading = &reading[1];
					writing[0] = '\r';
					continue;
				default:
					dprintf("l10n: Unknown escape character '%с'\n",reading[1]);
			}
		writing[0] = reading[0];
	}
	writing[0] = reading[0];
}

static int l10n_readAndInitTextEntry(char* entrySource, int entryIndex)
{
	//dprintf("%s: %s\n", __FUNCTION__,entrySource);
	char *delimeter = index(entrySource, '=');
	if (delimeter == NULL)
		return -1;
	size_t key_len = delimeter-entrySource;
	l10n_textEntries[entryIndex].key = (char*)dmalloc( key_len+1 );
	memcpy(l10n_textEntries[entryIndex].key, entrySource, key_len);
	l10n_textEntries[entryIndex].key[key_len] = 0;
	parseEscapeCharacters(&delimeter[1]);
	l10n_textEntries[entryIndex].value = strdup(&delimeter[1]);
	//dprintf("%s: Added %s=%s\n", __FUNCTION__,l10n_textEntries[entryIndex].key,l10n_textEntries[entryIndex].value);
	return 0;
}

static int l10n_setTextEntry(const char* entryText, int entryIndex)
{
	size_t textLength = strlen(entryText);
	if(strlen(l10n_textEntries[entryIndex].value)!=textLength)
	{
		char* newText = (char*)drealloc(l10n_textEntries[entryIndex].value, sizeof(char) * (textLength+1));
		if(newText == NULL)
		{
			dprintf("%s: Can't realloc memory for %d entriy. Current text length=%d, asked=%d\n", __FUNCTION__,entryIndex,strlen(l10n_textEntries[entryIndex].value), textLength );
			textLength = strlen(l10n_textEntries[entryIndex].value);
		}
		else
		{
			l10n_textEntries[entryIndex].value = newText;
		}
	}
	strncpy(l10n_textEntries[entryIndex].value,entryText,textLength);
	l10n_textEntries[entryIndex].value[textLength]=0;
	return textLength;
}

static int lang_select(const struct dirent * de)
{
	char full_path[PATH_MAX];
	sprintf(full_path,"%s%s",l10n_languageDir,de->d_name);
	int status;
	struct stat stat_info;
	status = stat( full_path, &stat_info);
	if(status<0)
		return 0;
	if(!(stat_info.st_mode & (S_IFREG | S_IFLNK)))
		return 0;
	return (strcasecmp(&de->d_name[strlen(de->d_name)-4], ".lng")==0);
}

static int  l10n_enumLanguageFiles(struct dirent ***langEntries)
{
	return scandir(l10n_languageDir, langEntries, lang_select, alphasort);
}

static FILE* l10n_findLanguageFile(const char* language)
{
	struct dirent **langEntries;
	int languageCount;
	if((languageCount = l10n_enumLanguageFiles(&langEntries)) < 0)
	{
		return NULL;
	}
	char buffer[PATH_MAX];
	char query_string[PATH_MAX];
	if (language != NULL)
	{
		sprintf(query_string,"%s=%s",l10n_languageKey,language);
	}
	FILE* lang_file;
	int i;
	int bufferLength;
	for( i = 0; i< languageCount; ++i)
	{
		sprintf(buffer,"%s%s",l10n_languageDir,langEntries[i]->d_name);
		dprintf("l10n: Opened language file %s\n",langEntries[i]->d_name);
		dfree(langEntries[i]);
		lang_file = fopen(buffer, "r");
		fgets(buffer, PATH_MAX, lang_file);
		bufferLength = strlen(buffer);
		if(buffer[bufferLength-1] == '\n')
			buffer[bufferLength-1] = 0;
		dprintf("l10n: Identification: %s\n",buffer);
		if(language == NULL || strcasecmp(buffer,query_string)==0)
		{
			int j;
			for( j = i+1; j < languageCount; ++j )
				dfree(langEntries[j]);
			dfree(langEntries);
			return lang_file;
		}
		fclose(lang_file);
	}
	dfree(langEntries);
	return NULL;
}

int l10n_switchLanguage(const char* newLanguage)
{
	if( strcasecmp(l10n_currentLanguage,newLanguage) == 0)
		return 0;
	char        buffer[PATH_MAX];
	int         bufferLength;
	int         i;
	FILE*       lang_file = NULL;
	for( i = 0; i < l10n_languageCount; ++i )
		if(strcasecmp(l10n_languages[i].key, newLanguage)==0)
		{
			sprintf(buffer, "%s%s", l10n_languageDir, l10n_languages[i].value );
			lang_file = fopen(buffer, "r");
			if(lang_file != NULL)
			{
				fgets(buffer,PATH_MAX,lang_file); /* LANG_NAME= */
			}
			break;
		}
	/*if(lang_file == NULL)
		lang_file = l10n_findLanguageFile(newLanguage);*/
	if(lang_file == NULL)
		return -1;
	strcpy(l10n_currentLanguage, newLanguage);
	char       *delimeter;
	int         entryIndex;
	while( fgets(buffer, PATH_MAX, lang_file) != NULL )
	{
		delimeter = index(buffer,'=');
		if(delimeter == NULL)
			continue;
		bufferLength = strlen(buffer);
		if(buffer[bufferLength-1] == '\n')
			buffer[bufferLength-1] = 0;
		*delimeter = 0;
/*#ifdef DEBUG
		char dbg[4096];
		sprintf(dbg,"echo 'buffer=%s newText=%s' >> l10n.log",buffer,&delimeter[1]);
		system(dbg);
#endif*/
		entryIndex = l10n_findKey(buffer);
		if(entryIndex < 0)
		{
			dprintf("l10n: Can't find existing key = %s\n",buffer);
			/*
			l10n_textEntry* newEntries = (l10n_textEntry*)drealloc( l10n_textEntries, sizeof(l10n_textEntry) * (l10n_textEntriesCount + 1) );
			if(newEntries == NULL)
			{
				dprintf("l10n: Can't increment storage capacity for new entry! Current capacity=%d\n",l10n_textEntriesCount);
				break;
			}
			else
			{
				*delimeter = '=';
				if(l10n_readAndInitTextEntry(buffer,l10n_textEntriesCount) != 0)
				{
					dprintf("l10n: Can't init new entry! Input string=%s",buffer);
				}
				else
				{
					entryIndex = l10n_textEntriesCount;
					l10n_textEntriesCount++;
				}
				*delimeter = 0;
			}*/
		}
		else /* entryIndex is valid */
		{
			parseEscapeCharacters(&delimeter[1]);
			l10n_setTextEntry(&delimeter[1],entryIndex);
		}
	}
	fclose(lang_file);
	return 1;
}

static int l10n_findKey(const char* key)
{
	int l = 0, r = l10n_textEntriesCount-1, i, cmp_result;
	i = (l + r) / 2;
	while( (cmp_result = strcmp(l10n_textEntries[l10n_sortedIndexes[i]].key, key)) != 0 && (r - l > 1))
	{
		if( cmp_result < 0)
			l = i;
		else
			r = i;
		i = (l + r) / 2;
	}
	if(cmp_result == 0)
		return l10n_sortedIndexes[i];
	if(cmp_result > 0 && strcmp(l10n_textEntries[l10n_sortedIndexes[l]].key, key)==0)
		return l10n_sortedIndexes[l];
	if(cmp_result < 0 && strcmp(l10n_textEntries[l10n_sortedIndexes[r]].key, key)==0)
		return l10n_sortedIndexes[r];
	return -1;
}

char* l10n_getText(const char* key)
{
	//dprintf("%s: _T(\"%s\")\n", __FUNCTION__,key);
	int entryIndex = l10n_findKey(key);
	if(entryIndex < 0)
	{
		dprintf("l10n: Can't find text entry for '%s' in %s language file\n",key, l10n_currentLanguage);
		return (char*)key;
	}
	else
		return &(l10n_textEntries[entryIndex].value[0]);
}

static int free_language_list(interfaceMenu_t *pMenu, void* pArg)
{
	dprintf("l10n: cleaning language list\n");
	int i;
	for( i = 0; i< l10n_languageCount; ++i)
	{
		dfree(l10n_languages[i].key);
		dfree(l10n_languages[i].value);
	}
	dfree(l10n_languages);
	l10n_languages = NULL;
	return 0;
}

int l10n_initLanguageMenu(interfaceMenu_t *pMenu, void* pArg)
{
	static int built = 0;
	if ( !built )
	{
		createListMenu(&LanguageMenu, _T("LANGUAGE"), settings_language, NULL, (interfaceMenu_t*)&OutputMenu,
					   /* interfaceInfo.clientX, interfaceInfo.clientY,
					   interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
					   NULL, free_language_list, NULL);
		built = 1;
	}
	struct dirent **langEntries;
	char buffer[PATH_MAX];
	int bufferLength;
	FILE *lang_file;
	int i;
	int keyLength=strlen(l10n_languageKey);
	int langEntriesCount = l10n_enumLanguageFiles(&langEntries);
	l10n_languageCount = 0;
	l10n_languages = (l10n_textEntry*)dmalloc( sizeof(l10n_textEntry) * langEntriesCount );
	for( i = 0; i < langEntriesCount; ++i )
	{
		sprintf(buffer,"%s%s", l10n_languageDir, langEntries[i]->d_name);
		lang_file = fopen(buffer,"r");
		fgets(buffer, PATH_MAX, lang_file);
		bufferLength = strlen(buffer);
		if(buffer[bufferLength-1] == '\n')
			buffer[bufferLength-1] = 0;
		dprintf("l10n: Identification: %s\n",buffer);
		if(strncasecmp(buffer,l10n_languageKey,keyLength)==0)
		{
			l10n_languages[l10n_languageCount].key = strdup(&buffer[keyLength+1]);
			l10n_languages[l10n_languageCount].value = strdup(langEntries[i]->d_name);
			dprintf("l10n: Added language description: %s=%s\n",l10n_languages[l10n_languageCount].key,l10n_languages[l10n_languageCount].value);
			l10n_languageCount++;
		}
		else
			eprintf("l10n: %s is not a valid language file!\n",langEntries[i]->d_name);
		dfree(langEntries[i]);
		fclose(lang_file);
	}
	dfree(langEntries);
	if(l10n_languageCount < langEntriesCount)
	{
		l10n_textEntry* new_languages = (l10n_textEntry*)drealloc( l10n_languages , sizeof(l10n_textEntry) * l10n_languageCount );
		if(new_languages != NULL)
			l10n_languages = new_languages;
		else
			eprintf("l10n: Can't realloc memory for language desriptions! Current size=%d, requested=%d\n",langEntriesCount,l10n_languageCount);
	}
	return l10n_fillLanguageMenu(pMenu, NULL);
}

static int l10n_fillLanguageMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int i;
	int selectedItem = MENU_ITEM_BACK;
	int strcmpResult;
	interface_clearMenuEntries((interfaceMenu_t *)&LanguageMenu);
	char* test_text = _T("test");
	interface_addMenuEntryDisabled((interfaceMenu_t *)&LanguageMenu, test_text, thumbnail_info );
	for( i = 0; i < l10n_languageCount; ++i )
	{
		strcmpResult = strcasecmp(l10n_currentLanguage, l10n_languages[i].key);
		if(strcmpResult == 0)
			selectedItem = interface_getMenuEntryCount((interfaceMenu_t *)&LanguageMenu);
		interface_addMenuEntry((interfaceMenu_t *)&LanguageMenu, l10n_languages[i].key, l10n_menuSelectLanguage, NULL, strcmpResult == 0 ? radiobtn_filled : radiobtn_empty );
	}
	interface_setSelectedItem((interfaceMenu_t *)&LanguageMenu, selectedItem);
	interface_menuActionShowMenu(pMenu, (void*)&LanguageMenu);
	return 0;
}

static int l10n_menuSelectLanguage(interfaceMenu_t *pMenu, void* pArg)
{
	char buf[BUFFER_SIZE];
	interface_getMenuEntryInfo(pMenu, interface_getSelectedItem(pMenu), buf, MENU_ENTRY_INFO_LENGTH);
	if(strcasecmp(buf,l10n_currentLanguage) != 0)
	{
		interface_showConfirmationBox(_T("CHANGE_LANGUAGE_CONFIRM"), thumbnail_question, l10n_confirmLanguageChange, pArg);
	}
	/*
	int result = l10n_switchLanguage(newLanguage);
	if( result < 0)
	{
		eprintf("l10n: Error: Can't set language to %s!",newLanguage);
	}
	else
	if(result > 0)
		return l10n_fillLanguageMenu(pMenu,pArg);
	return result;
	*/
	return 0;
}

static int l10n_confirmLanguageChange(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		interface_getMenuEntryInfo((interfaceMenu_t *)&LanguageMenu, interface_getSelectedItem((interfaceMenu_t *)&LanguageMenu), l10n_currentLanguage, MENU_ENTRY_INFO_LENGTH);
		if (saveAppSettings() != 0)
		{
			interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
		}
		else
			signal_handler(9);
		return 0;
	}

	return 1;
}
