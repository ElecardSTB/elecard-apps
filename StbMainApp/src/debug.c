
/*
 debug.c

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

#include "debug.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#ifdef ALLOC_DEBUG

#define MAX_ALLOCS (256)

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct _alloc_info_t {
	void *addr;
	unsigned int present;
	char source[32];
} alloc_info_t;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static int initialized = 0;
static alloc_info_t allocs[MAX_ALLOCS];

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

static void dbg_cat_alloc(void)
{
	int i = 0;
	int count = 0;

	if (initialized == 0)
	{
		memset(allocs, 0, sizeof(allocs));
		initialized = 1;
	}

	printf("==========ALLOCS==============\n");

	for (i=0; i<MAX_ALLOCS; i++)
	{
		if (allocs[i].present != 0)
		{
			printf(" [%p]%s", allocs[i].addr, allocs[i].source);
			count++;
			if (count > 0 && count % 2 == 0)
			{
				printf("\n");
			}
		}
	}

	printf("\n------------------------------\n");
}

static void dbg_add_alloc(void *ptr, const char *source)
{
	int found = 0;
	int available = -1;
	int i;

	if (initialized == 0)
	{
		memset(allocs, 0, sizeof(allocs));
		initialized = 1;
	}

	for (i=0; i<MAX_ALLOCS; i++)
	{
		if (allocs[i].addr == ptr && allocs[i].present != 0)
		{
			found = 1;
			eprintf("already found %p in alloc map\n", ptr);
			return;
		}
		if (available == -1 && allocs[i].present == 0)
		{
			available = i;
		}
	}

	if (available >= 0)
	{
		allocs[available].addr = ptr;
		allocs[available].present = 1;
		strncpy(allocs[available].source, source, sizeof(allocs[available].source)-1);
		allocs[available].source[sizeof(allocs[available].source)-1]=0;
	} else {
		eprintf("too many allocs\n");
	}
}

static void dbg_del_alloc(void *ptr)
{
	int found = 0;
	int i;

	if (initialized == 0)
	{
		memset(allocs, 0, sizeof(allocs));
		initialized = 1;
	}

	for (i=0; i<MAX_ALLOCS; i++)
	{
		if (allocs[i].addr == ptr && allocs[i].present != 0)
		{
			found = 1;
			allocs[i].present = 0;
			allocs[i].addr = 0;
			return;
		}
	}

	if (found == 0)
	{
		eprintf("can't find %p in alloc map\n", ptr);
	}
}

void *dbg_calloc(size_t nmemb, size_t size, const char *location)
{
	void *p = calloc(nmemb, size);

	dbg_add_alloc(p, location);
	printf("%p CALLOC by %s\n", p, location);
	fflush(stdout);

	dbg_cat_alloc();

	return p;
}

void *dbg_malloc(size_t size, const char *location)
{
	void *p = malloc(size);

	dbg_add_alloc(p, location);
	printf("%p MALLOC by %s\n", p, location);
	fflush(stdout);

	dbg_cat_alloc();

	return p;
}

void dbg_free(void *ptr, const char *location)
{
	dbg_del_alloc(ptr);
	printf("%p FREE by %s\n", ptr, location);
	fflush(stdout);

	free(ptr);

	dbg_cat_alloc();
}

void *dbg_realloc(void *ptr, size_t size, const char *location)
{
	void *p = realloc(ptr, size);

	dbg_del_alloc(ptr);
	printf("%p FREE-REALLOC by %s\n", ptr, location);
	dbg_add_alloc(p, location);
	printf("%p MALLOC-REALLOC by %s\n", p, location);
	fflush(stdout);

	dbg_cat_alloc();

	return p;
}

#endif // #ifdef ALLOC_DEBUG
