/*
 * Copyright (C) 2014 by Elecard-STB.
 * Written by Anton Sergeev <Anton.Sergeev@elecard.ru>
 *
 */

#if !(defined __HELPER_H__)
#define __HELPER_H__

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include "list.h"
#include <stdint.h>

/******************************************************************
* EXPORTED MACROS                              [for headers only] *
*******************************************************************/
#define ARRAY_SIZE(arr)	(sizeof(arr)/sizeof(*arr))

#define TABLE_INT_END_VALUE			0xdeadbeaf
#define TABLE_STR_END_VALUE			NULL

#define TABLE_INT_INT_END_VALUE		{ TABLE_INT_END_VALUE, TABLE_INT_END_VALUE }
#define TABLE_INT_STR_END_VALUE		{ TABLE_INT_END_VALUE, TABLE_STR_END_VALUE }


#define dbg_printf(fmt, args...) \
	{ \
		struct timeval tv; \
		if(gettimeofday(&tv, NULL) == 0) { \
			fprintf(stderr, "%09d.%06d: ", (int32_t)tv.tv_sec, (int32_t)tv.tv_usec); \
		} \
		fprintf(stderr, "%s:%s()[%d]: " fmt, __FILE__, __func__, __LINE__, ##args); \
	}


/******************************************************************
* EXPORTED TYPEDEFS                            [for headers only] *
*******************************************************************/
typedef struct {
	int32_t key;
	const char *value;
} table_IntStr_t;

typedef struct {
	int32_t key;
	int32_t value;
} table_IntInt_t;


/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

int32_t helperFileExists(const char* filename);
int32_t helperCheckDirectoryExsists(const char *path);

int32_t	getParam(const char *path, const char *param, const char *defaultValue, char *output);
int32_t	setParam(const char *path, const char *param, const char *value);

const char	*table_IntStrLookup(const table_IntStr_t table[], int32_t key, char *defaultValue);
int32_t	table_IntStrLookupR(const table_IntStr_t table[], char *value, int32_t defaultValue);
int32_t	table_IntIntLookup(const table_IntInt_t table[], int32_t key, int32_t defaultValue);
int32_t	table_IntIntLookupR(const table_IntInt_t table[], int32_t value, int32_t defaultValue);

//String list API
int32_t strList_add    (struct list_head *listHead, const char *str);
int32_t strList_remove (struct list_head *listHead, const char *str);
int32_t strList_isExist(struct list_head *listHead, const char *str);
int32_t strList_release(struct list_head *listHead);
const char *strList_get(struct list_head *listHead, uint32_t number);

#ifdef __cplusplus
}
#endif

#endif //#if !(define __HELPER_H__)
