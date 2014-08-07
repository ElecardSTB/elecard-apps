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
#include <stddef.h>

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

#define strList_release(commonHead)                  commonList_release(commonHead)
#define strList_add(commonHead, str)                 commonList_add(commonHead, str)
#define strList_add_head(commonHead, str)            commonList_add_head(commonHead, str)
#define strList_remove(commonHead, str)              commonList_remove(commonHead, str)
#define strList_isExist(commonHead, str)             commonList_isExist(commonHead, str)
#define strList_find(commonHead, str)                commonList_find(commonHead, str)
#define strList_get(commonHead, number)              (const char *)commonList_get(commonHead, number)
#define strList_remove_last(commonHead)              commonList_remove_last(commonHead)
#define strList_count(commonHead)                    commonList_count(commonHead)

#define commonList_getObj(commonHead, number, type)  (type *)commonList_get(commonHead, number)


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

typedef int32_t compareFunc_t(const void *, const void *, void *);
typedef size_t  getLengthFunc_t(const void *, void *);

typedef struct {
	compareFunc_t    *compar;
	getLengthFunc_t  *len; //this matter if objSize=0
	void             *pArg;
	size_t            objSize;
	struct list_head  head;
	uint32_t          count;
	struct {
		struct list_head *pos;
		uint32_t          id;
	} last;
} listHead_t;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

int32_t helperFileExists(const char* filename);
int32_t helperFileIsSymlink(const char* filename);
int32_t helperCheckDirectoryExsists(const char *path);

int32_t	getParam(const char *path, const char *param, const char *defaultValue, char *output);
int32_t	setParam(const char *path, const char *param, const char *value);

const char	*table_IntStrLookup(const table_IntStr_t table[], int32_t key, char *defaultValue);
int32_t	table_IntStrLookupR(const table_IntStr_t table[], char *value, int32_t defaultValue);
int32_t	table_IntIntLookup(const table_IntInt_t table[], int32_t key, int32_t defaultValue);
int32_t	table_IntIntLookupR(const table_IntInt_t table[], int32_t value, int32_t defaultValue);

//Cut "Enters" from string
int32_t	stripEnterInStr(const char *str);


//Common list API
int32_t commonList_init       (listHead_t *commonHead, compareFunc_t *compar, void *pArg, size_t objSize, getLengthFunc_t *len);
int32_t commonList_release    (listHead_t *commonHead);

int32_t commonList_add        (listHead_t *commonHead, const void *pArg);
int32_t commonList_add_head   (listHead_t *commonHead, const void *pArg);
int32_t commonList_remove     (listHead_t *commonHead, const void *pArg);
int32_t commonList_remove_last(listHead_t *commonHead);
int32_t commonList_isExist    (listHead_t *commonHead, const void *pArg);
int32_t commonList_count      (listHead_t *commonHead);
int32_t commonList_find       (listHead_t *commonHead, const void *pArg);
const void *commonList_get    (listHead_t *commonHead, uint32_t number);

//String list API
int32_t strList_init(listHead_t *commonList, int32_t isCaseSensivity);

#ifdef __cplusplus
}
#endif

#endif //#if !(define __HELPER_H__)
