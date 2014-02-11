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
#include <stdint.h>

/******************************************************************
* EXPORTED MACROS                              [for headers only] *
*******************************************************************/
#define ARRAY_SIZE(arr)	(sizeof(arr)/sizeof(*arr))

#define TABLE_INT_END_VALUE	0xdeadbeaf
#define TABLE_STR_END_VALUE	NULL

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
int32_t helperFileExists(const char* filename);
int32_t helperCheckDirectoryExsists(const char *path);

int32_t	getParam(const char *path, const char *param, const char *defaultValue, char *output);
int32_t	setParam(const char *path, const char *param, const char *value);

const char	*table_IntStrLookup(const table_IntStr_t table[], int32_t key, char *defaultValue);
int32_t	table_IntStrLookupR(const table_IntStr_t table[], char *value, int32_t defaultValue);
int32_t	table_IntIntLookup(const table_IntInt_t table[], int32_t key, int32_t defaultValue);
int32_t	table_IntIntLookupR(const table_IntInt_t table[], int32_t value, int32_t defaultValue);


#endif //#if !(define __HELPER_H__)
