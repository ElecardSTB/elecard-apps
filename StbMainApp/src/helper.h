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
#include <sys/time.h>

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

#define strList_release(commonList)                  commonList_release(commonList)
#define strList_add(commonList, str)                 commonList_add(commonList, str)
#define strList_add_head(commonList, str)            (const char *)commonList_add_head(commonList, str)
#define strList_remove(commonList, str)              commonList_remove(commonList, str)
#define strList_isExist(commonList, str)             commonList_isExist(commonList, str)
#define strList_find(commonList, str)                (const char *)commonList_find(commonList, str)
#define strList_get(commonList, number)              (const char *)commonList_get(commonList, number)
#define strList_remove_last(commonList)              commonList_remove_last(commonList)
#define strList_count(commonList)                    commonList_count(commonList)

#define commonList_getObj(commonList, number, type)  (type *)commonList_get(commonList, number)


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
int32_t stripEnterInStr(char *str);
char   *skipSpacesInStr(char *str);


//Common list API
int32_t commonList_init       (listHead_t *commonList, compareFunc_t *compar, void *pArg, size_t objSize, getLengthFunc_t *len);
int32_t commonList_release    (listHead_t *commonList);

const void *commonList_add        (listHead_t *commonList, const void *pArg);
const void *commonList_add_head   (listHead_t *commonList, const void *pArg);
int32_t     commonList_remove     (listHead_t *commonList, const void *pArg);
int32_t     commonList_remove_last(listHead_t *commonList);
int32_t     commonList_isExist    (listHead_t *commonList, const void *pArg);
int32_t     commonList_count      (listHead_t *commonList);
const void *commonList_find       (listHead_t *commonList, const void *pArg);
int32_t     commonList_findId     (listHead_t *commonList, const void *pArg);
const void *commonList_get        (listHead_t *commonList, uint32_t number);
int32_t     commonList_sort       (listHead_t *commonList);

//String list API
int32_t strList_init(listHead_t *commonList, int32_t isCaseSensivity);


int32_t Helper_IsTimeGreater(struct timeval t1, struct timeval t2);
/** Copy src string to new dest buffer.
 * @param[out] dest If already allocated and have enough space, will be used without changing pointer. If buffer will not be big enough, realloc would be used on *dest.
 * @param[in]  stc  If null, *dest will be freed and nulled.
 * @return 0 if src was copied successfully
 */
int32_t helperSafeStrCpy(char **dest, const char *src);
/** strcpy without characters unsupported by FAT filesystem
 * @param[out] dst Destination pointer
 * @param[in]  src Source stream
 * @return     dst
 */
char   *helperStrCpyTrimSystem(char *dst, char *src);
int32_t helperParseLine(const char *path, const char *cmd, const char *pattern, char *out, char stopChar);
int32_t helperReadLine(int32_t file, char* buffer);


#ifdef __cplusplus
}
#endif

#endif //#if !(define __HELPER_H__)
