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
#define TEMP_CONFIG_FILE             "/var/tmp/cfg.tmp"
#define commonList_getPtrToObj(pos)  (((void *)pos) + sizeof(struct list_head))
#define commonList_getPtrToList(obj) (((void *)obj) - sizeof(struct list_head))
#define commonList_isValid(listHead) ((listHead == NULL) ? 0 : 1)

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

int32_t helperFileIsSymlink(const char* filename)
{
	struct stat st;

	if(filename && (lstat(filename, &st) == 0) && S_ISLNK(st.st_mode)) {
		return 1;
	}

	return 0;
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

int32_t stripEnterInStr(char *str)
{
	char *ch;
	if(!str) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return -1;
	}

	if((ch = strchr(str, '\r')) != NULL) {
		ch[0] = '\0';
	}
	if((ch = strchr(str, '\n')) != NULL) {
		ch[0] = '\0';
	}

	return 0;
}

char *skipSpacesInStr(char *str)
{
	if(str == NULL) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return NULL;
	}
	while((*str != 0)
			&& ((*str == ' ') || (*str == '\t'))) {
		str++;
	}
	return str;
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

static int32_t strList_compareFunc(const void *str1, const void *str2, void *pArg)
{
	if((int32_t)pArg == 0) {//ignore case
		return strcasecmp((const char *)str1, (const char *)str2);
	}
	return strcmp((const char *)str1, (const char *)str2);
}

static size_t strList_getLengthFunc(const void *str, void *pArg)
{
	(void)pArg;
	return strlen((const char *)str) + 1;//calculate with '\0' symbol
}

int32_t strList_init(listHead_t *commonList, int32_t isCaseSensivity)
{
	return commonList_init(commonList, strList_compareFunc, (void *)isCaseSensivity, 0, strList_getLengthFunc);
}

static void commonList_resetLast(listHead_t *commonList)
{
	commonList->last.pos = NULL;
	commonList->last.id = 0;
}

static void commonList_removeElement(listHead_t *commonList, struct list_head *pos)
{
	list_del(pos);
	free((void *)pos);
}

/* Asume the next struct for constant objects size:
 * struct {
 *     struct list_head  list;
 *     char              object[objSize];
 * };
 * and for strings (objSize=0):
 * struct {
 *     struct list_head  list;
 *     char              str[strlen];
 * };
 */
int32_t commonList_init(listHead_t *commonList, compareFunc_t *compar, void *pArg, size_t objSize, getLengthFunc_t *len)
{
	memset(commonList, 0, sizeof(listHead_t));
	INIT_LIST_HEAD(&commonList->head);
	commonList->compar = compar;
	commonList->len = len;
	commonList->pArg = pArg;
	commonList->objSize = objSize;
	commonList_resetLast(commonList);
	if((commonList->objSize == 0) && (commonList->len == NULL)) {
		eprintf("%s(): Error, objSize=0 and len() function not defined!", __func__);
		return -1;
	}
	return 0;
}

int32_t commonList_release(listHead_t *commonList)
{
	struct list_head *pos;
	struct list_head *n;
	if(!commonList_isValid(commonList)) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return -1;
	}
	list_for_each_safe(pos, n, &commonList->head) {
		commonList_removeElement(commonList, pos);
	}
	commonList_resetLast(commonList);

	return 0;
}

static struct list_head *commonList_createListItem(listHead_t *commonList, const void *pArg)
{
	void *newItem;
	size_t len;

	if(!commonList_isValid(commonList) || !pArg) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return NULL;
	}

	if(commonList->objSize) {
		len = commonList->objSize;
	} else {
		if(commonList->len) {
			len = commonList->len(pArg, commonList->pArg);
			if(len == 0) {
				eprintf("%s(): Error, len() return 0!", __func__);
				return NULL;
			}
		} else {
			eprintf("%s(): Error, objSize=0 and len() function not defined!", __func__);
			return NULL;
		}
	}

	newItem = malloc(len + sizeof(struct list_head));
	if(!newItem) {
		eprintf("%s(): Allocation error!\n", __func__);
		return NULL;
	}
	memcpy(newItem + sizeof(struct list_head), pArg, len);

	return (struct list_head *)newItem;
}

const void *commonList_add_head(listHead_t *commonList, const void *pArg)
{
	struct list_head *listItem;
	listItem = commonList_createListItem(commonList, pArg);
	if(listItem == NULL) {
		return NULL;
	}

	list_add(listItem, &commonList->head);
	commonList_resetLast(commonList);
	return commonList_getPtrToObj(listItem);
}

const void *commonList_add(listHead_t *commonList, const void *pArg)
{
	struct list_head *listItem;
	listItem = commonList_createListItem(commonList, pArg);
	if(listItem == NULL) {
		return NULL;
	}

	list_add_tail(listItem, &commonList->head);
	commonList_resetLast(commonList);
	return commonList_getPtrToObj(listItem);
}

int32_t commonList_remove(listHead_t *commonList, const void *pArg)
{
	struct list_head *pos;
	if(!commonList_isValid(commonList) || !pArg) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -1;
	}
	if(commonList->compar == NULL) {
		eprintf("%s(): Comparison function not setted!\n", __func__);
		return -2;
	}
	list_for_each(pos, &commonList->head) {
		if(commonList->compar(commonList_getPtrToObj(pos), pArg, commonList->pArg) == 0) {
			commonList_removeElement(commonList, pos);
			commonList_resetLast(commonList);
			return 1;
		}
	}

	return 0;
}

int32_t commonList_remove_last(listHead_t *commonList)
{
	struct list_head *pos;

	if(!commonList_isValid(commonList)) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -1;
	}
	if(list_empty(&commonList->head)) {
		return 0;
	}

	pos = commonList->head.prev;
	commonList_removeElement(commonList, pos);

//	if(commonList.last.pos == pos) {
		commonList_resetLast(commonList);
//	}

	return 0;
}

int32_t commonList_isExist(listHead_t *commonList, const void *pArg)
{
	struct list_head *pos;
	if(!commonList_isValid(commonList) || !pArg) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return 0;
	}
	if(commonList->compar == NULL) {
		eprintf("%s(): Comparison function not setted!\n", __func__);
		return 0;
	}
	list_for_each(pos, &commonList->head) {
		if(commonList->compar(commonList_getPtrToObj(pos), pArg, commonList->pArg) == 0) {
			return 1;
		}
	}

	return 0;
}

int32_t commonList_count(listHead_t *commonList)
{
	struct list_head *pos;
	uint32_t id = 0;
	if(!commonList_isValid(commonList)) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -1;
	}
	list_for_each(pos, &commonList->head) {
		id++;
	}
	return id;
}

const void *commonList_find(listHead_t *commonList, const void *pArg)
{
	struct list_head *pos;
	if(!commonList_isValid(commonList) || !pArg) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return NULL;
	}
	if(commonList->compar == NULL) {
		eprintf("%s(): Comparison function not setted!\n", __func__);
		return NULL;
	}
	list_for_each(pos, &commonList->head) {
		if(commonList->compar(commonList_getPtrToObj(pos), pArg, commonList->pArg) == 0) {
			return commonList_getPtrToObj(pos);
		}
	}

	return NULL;
}

int32_t commonList_findId(listHead_t *commonList, const void *pArg)
{
	struct list_head *pos;
	int32_t id = 0;
	if(!commonList_isValid(commonList) || !pArg) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return -2;
	}
	if(commonList->compar == NULL) {
		eprintf("%s(): Comparison function not setted!\n", __func__);
		return -3;
	}
	list_for_each(pos, &commonList->head) {
		if(commonList->compar(commonList_getPtrToObj(pos), pArg, commonList->pArg) == 0) {
			return id;
		}
		id++;
	}

	return -1;
}

const void *commonList_get(listHead_t *commonList, uint32_t number)
{
	struct list_head *pos = NULL;
	uint32_t id = 0;
	if(!commonList_isValid(commonList)) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return NULL;
	}
	if(commonList->last.pos) {
		if((commonList->last.id + 1) == number) {
			pos = commonList->last.pos->next;
		} else if(commonList->last.id == number) {
			pos = commonList->last.pos;
		} else if((commonList->last.id - 1) == number) {
			pos = commonList->last.pos->prev;
		}
		if(pos && (pos != &commonList->head)) {
			commonList->last.id = number;
			commonList->last.pos = pos;
			return commonList_getPtrToObj(pos);
		}
	}
	list_for_each(pos, &commonList->head) {
		if(id == number) {
			commonList->last.id = id;
			commonList->last.pos = pos;
			return commonList_getPtrToObj(pos);
		}
		id++;
	}
	return NULL;
}

static int commonList_cmp_qsort_r(const void *p1, const void *p2, void *pArg)
{
	listHead_t *commonList = (listHead_t *)pArg;

	if(commonList->compar == NULL) {
		return 0;//is this right?
	}
	return commonList->compar(*((const void **)p1), *((const void **)p2), commonList->pArg);
}


int32_t commonList_sort(listHead_t *commonList)
{
	const void **array;
	uint32_t count = commonList_count(commonList);
	uint32_t i;
	struct list_head *pos;

	if(commonList->compar == NULL) {
		eprintf("%s(): Comparison function not setted!\n", __func__);
		return -3;
	}

	array = malloc(count * sizeof(void *));
	if(array == NULL) {
		eprintf("%s(): Error allocating memory %dB!", __func__, sizeof(void *) * count);
		return -1;
	}

	i = 0;
	list_for_each(pos, &commonList->head) {
		array[i] = commonList_getPtrToObj(pos);
		i++;
	}
	qsort_r(array, count, sizeof(void *), commonList_cmp_qsort_r, (void *)commonList);

	for(i = 0; i < count; i++) {
		pos = commonList_getPtrToList(array[i]);
		list_del(pos);
		list_add_tail(pos, &commonList->head);
	}
	commonList_resetLast(commonList);
	free(array);
	return 0;
}
