/*
 * Copyright (C) 2014 by Elecard-STB.
 * Written by Anton Sergeev <Anton.Sergeev@elecard.ru>
 *
 */

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
//#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include <common.h>

#include "helper.h"
#include "defines.h"
#include "interface.h"
#include "l10n.h"

/******************************************************************
* LOCAL MACROS                                                    *
*******************************************************************/
#define TEMP_CONFIG_FILE            "/var/tmp/cfg.tmp"


/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
typedef struct {
	char               *str;
	struct list_head    list;
} strList_t;

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>_<Word>+    *
*******************************************************************/
int helperFileExists(const char* filename)
{
	int file;
	int ret = 0;
#if (defined STB225)
	const char *filename_t = filename;
	const char *prefix = "file://";
	if(strncmp(filename, prefix, strlen(prefix)) == 0) {
		filename_t = filename + strlen(prefix);
	}
	file = open(filename_t, O_RDONLY);
//printf("%s[%d]: file=%d, filename_t=%s\n", __FILE__, __LINE__, file, filename_t);
#else
	file = open(filename, O_RDONLY);
#endif
	if(file >= 0) {
		close(file);
		ret = 1;
	}

	return ret;
}

int32_t helperCheckDirectoryExsists(const char *path)
{
	struct stat st;

	if(path && (stat(path, &st) == 0) && S_ISDIR(st.st_mode)) {
		return 1;
	}

	return 0;
}

int32_t getParam(const char *path, const char *param, const char *defaultValue, char *output)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	FILE *fd;
	int32_t found = 0;
	int32_t plen, vlen;

	fd = fopen(path, "r");
	if(fd != NULL) {
		while(fgets(buf, sizeof(buf), fd) != NULL) {
			plen = strlen(param);
			vlen = strlen(buf)-1;
			if(strncmp(buf, param, plen) == 0 && buf[plen] == '=') {
				while(buf[vlen] == '\r' || buf[vlen] == '\n' || buf[vlen] == ' ') {
					buf[vlen] = 0;
					vlen--;
				}
				if(vlen-plen > 0) {
					if(output != NULL) {
						strcpy(output, &buf[plen+1]);
					}
					found = 1;
				}
				break;
			}
		}
		fclose(fd);
	}

	if(!found && defaultValue && output) {
		strcpy(output, defaultValue);
	}
	return found;
}

int32_t setParam(const char *path, const char *param, const char *value)
{
	FILE *fdi, *fdo;
	char buf[MENU_ENTRY_INFO_LENGTH];
	int32_t found = 0;

	//dprintf("%s: %s: %s -> %s\n", __FUNCTION__, path, param, value);

#ifdef STB225
//	system("mount -o rw,remount /");
#endif

	fdo = fopen(TEMP_CONFIG_FILE, "w");

	if(fdo == NULL) {
		eprintf("output: Failed to open out file '%s'\n", TEMP_CONFIG_FILE);
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
#ifdef STB225
//		system("mount -o ro,remount /");
#endif
		return -1;
	}

	fdi = fopen(path, "r");

	if(fdi != NULL) {
		while(fgets(buf, sizeof(buf), fdi) != NULL) {
			//dprintf("%s: line %s\n", __FUNCTION__, buf);
			if (strncasecmp(param, buf, strlen(param)) == 0 ||
				(buf[0] == '#' && strncasecmp(param, &buf[1], strlen(param)) == 0))
			{
				//dprintf("%s: line matched param %s\n", __FUNCTION__, param);
				if(!found) {
					if(value != NULL) {
						fprintf(fdo, "%s=%s\n", param, value);
					} else {
						fprintf(fdo, "#%s=\n", param);
					}
					found = 1;
				}
			} else {
				fwrite(buf, strlen(buf), 1, fdo);
			}
		}
		fclose(fdi);
	}

	if(found == 0) {
		if(value != NULL) {
			fprintf(fdo, "%s=%s\n", param, value);
		} else {
			fprintf(fdo, "#%s=\n", param);
		}
	}

	fclose(fdo);

	//dprintf("%s: replace!\n", __FUNCTION__);
	sprintf(buf, "mv -f '%s' '%s'", TEMP_CONFIG_FILE, path);
	system(buf);

#ifdef STB225
//	system("mount -o ro,remount /");
#endif

	return 0;
}

const char *table_IntStrLookup(const table_IntStr_t table[], int32_t key, char *defaultValue)
{
	int32_t i;
/*	if(key < 0) {
		return defaultValue;
	}*/
	for(i = 0; table[i].value != TABLE_STR_END_VALUE; i++) {
		if(table[i].key == key) {
			return table[i].value;
		}
	}
	return defaultValue;
}

int32_t table_IntStrLookupR(const table_IntStr_t table[], char *value, int32_t defaultValue)
{
	int32_t i;
	if(!value) {
		return defaultValue;
	}
	for(i = 0; table[i].value != TABLE_STR_END_VALUE; i++) {
		if(!strcasecmp(table[i].value, value)) {
			return table[i].key;
		}
	}
	return defaultValue;
}

int32_t table_IntIntLookup(const table_IntInt_t table[], int32_t key, int32_t defaultValue)
{
	uint32_t i;

	for(i = 0; table[i].value != (int32_t)TABLE_INT_END_VALUE; i++) {
		if(table[i].key == key) {
			return table[i].value;
		}
	}
	return defaultValue;
}

int32_t table_IntIntLookupR(const table_IntInt_t table[], int32_t value, int32_t defaultValue)
{
	uint32_t i;

	for(i = 0; table[i].value != (int32_t)TABLE_INT_END_VALUE; i++) {
		if(table[i].value == value) {
			return table[i].key;
		}
	}
	return defaultValue;
}


int32_t strList_add(struct list_head *listHead, const char *str)
{
	strList_t *newName;
	if(!listHead || !str) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -1;
	}

	newName = malloc(sizeof(strList_t));
	if(!newName) {
		eprintf("%s(): Allocation error!\n", __func__);
		return -2;
	}
	newName->str = strdup(str);
	if(!newName->str) {
		eprintf("%s(): Cat duplicate str=%s!\n", __func__, str);
		free(newName);
		return -3;
	}
	dprintf("%s: %s\n", __func__, str);
	list_add_tail(&newName->list, listHead);

	return 0;
}

int32_t strList_remove(struct list_head *listHead, const char *str)
{
	struct list_head *pos;
	if(!listHead || !str) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -1;
	}
	list_for_each(pos, listHead) {
		strList_t *el = list_entry(pos, strList_t, list);
		if(strcasecmp(el->str, str) == 0) {
			dprintf("%s: %s\n", __func__, str);
			list_del(pos);
			if(el->str) {
				free(el->str);
			} else {
				eprintf("%s(): Something wrong, element has no str!\n", __func__);
			}
			free(el);
			return 1;
		}
	}

	return 0;
}

int32_t strList_isExist(struct list_head *listHead, const char *str)
{
	struct list_head *pos;
	if(!listHead || !str) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return 0;
	}
	list_for_each(pos, listHead) {
		strList_t *el = list_entry(pos, strList_t, list);
		//CHECK: is there need to ignore case???
		if(strcasecmp(el->str, str) == 0) {
			dprintf("%s: %s\n", __func__, str);
			return 1;
		}
	}

	return 0;
}

int32_t strList_release(struct list_head *listHead)
{
	struct list_head *pos;
	struct list_head *n;
	list_for_each_safe(pos, n, listHead) {
		strList_t *el = list_entry(pos, strList_t, list);

		list_del(pos);
		if(el->str) {
			free(el->str);
		} else {
			eprintf("%s(): Something wrong, element has no str!\n", __func__);
		}
		free(el);
	}
	return 0;
}

const char *strList_get(struct list_head *listHead, uint32_t number)
{
	struct list_head *pos;
	uint32_t id = 0;
	if(!listHead) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return NULL;
	}
	list_for_each(pos, listHead) {
		if(id == number) {
			strList_t *el = list_entry(pos, strList_t, list);
			return el->str;
		}
		id++;
	}
	return NULL;
}
