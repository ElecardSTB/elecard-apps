

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
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>

#include "debug.h"
#include "app_info.h"
#include "helper.h"

/***********************************************
* LOCAL MACROS                                 *
************************************************/
#define MAX_ALLOCS (256)
#define GDB_PIPE "/tmp/pipe_gdg"
#define SIZE_BUFFER  256

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/
typedef struct {
	void *addr;
	unsigned int present;
	char source[32];
} alloc_info_t;

/******************************************************************
* GLOBAL DATA                                                     *
*******************************************************************/

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
#ifdef ALLOC_DEBUG
static int initialized = 0;
static alloc_info_t allocs[MAX_ALLOCS];
#endif //#ifdef ALLOC_DEBUG

static pthread_t dbgThread;
static int32_t debugGroupValues[eDebug_count] = { [0 ... (eDebug_count - 1)] = 0 };

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/
#ifdef ALLOC_DEBUG
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

static void *dbg_thread(void *pArg)
{
	struct pollfd pfd[1];
	char buff[SIZE_BUFFER];
	int32_t len = 0;
	int32_t fd = (int32_t)pArg;

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	while(1) {
		if(poll(pfd, 1, 1000) > 0) {
			if(pfd[0].revents & POLLIN) {
				static const table_IntStr_t debugGroupNames[] = {
					{eDebug_gloabal, "DEBUG"},
					{eDebug_bouquet, "BOUQUET"},
					TABLE_INT_STR_END_VALUE
				};
				int32_t i;
				
				memset(buff, 0, sizeof(buff));
				len = read(fd, buff, SIZE_BUFFER);
				if(len < 0) {
					eprintf("%s: %d: errno=%d: %s\n", __func__, __LINE__, errno, strerror(errno));
					continue;
				}
				buff[len] = '\0';
				if(strncasecmp(buff, "?", 1) == 0 ) {
					printf("Debug help:\n");
					for(i = 0; i < eDebug_count; i++) {
						const char *cmdName = table_IntStrLookup(debugGroupNames, i, NULL);
						if(cmdName == NULL) {
							continue;
						}
						printf("  %s=%d\n", cmdName, dbg_getDebug(i));
					}
				} else {
					for(i = 0; i < eDebug_count; i++) {
						const char *cmdName = table_IntStrLookup(debugGroupNames, i, NULL);
						size_t len;
						if(cmdName == NULL) {
							continue;
						}
						len = strlen(cmdName);
						if(strncasecmp(buff, cmdName, len) == 0) {
							int32_t tmp;
							sscanf(buff + len + 1, "%d\n", &tmp);
							dbg_setDebug(i, tmp);
							saveAppSettings();
						}
					}
				}
			}
		}
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
	}
	return NULL;
}

int32_t dbg_init(void)
{
	int32_t dbg_pipe;
	int32_t i;

	/* By default send all output to stdout, except for error and warning output */
	for(i = 0; i < errorLevelCount; i++) {
		setoutput((errorLevel_t)i, (i == errorLevelError || i == errorLevelWarning) ? stderr : stdout);
	}
	setoutputlevel(errorLevelNormal);

	unlink(GDB_PIPE);
	if(mkfifo(GDB_PIPE, 0666) < 0) {
		eprintf("ttx: Unable to create a fifo buffer\n");
		return -1;
	}
	dbg_pipe = open(GDB_PIPE, O_RDONLY | O_NONBLOCK);
	if(dbg_pipe < 0) {
		unlink(GDB_PIPE);
		eprintf("Error in opening file %s\n", GDB_PIPE);
		return -2;
	}
	if(pthread_create(&dbgThread, NULL, dbg_thread, (void *)dbg_pipe) != 0) {
		eprintf("%s: ERROR not create thread\n", __func__);
		return -4;
	}

	return 0;
}

int32_t dbg_term(void)
{
	if(dbgThread) {
		pthread_cancel(dbgThread);
		pthread_join(dbgThread, NULL);
		dbgThread = 0;
	}
	return 0;
}

int32_t dbg_getDebug(debugGroup_t group)
{
	if(group >= eDebug_count) {
		return 0;
	}

	return debugGroupValues[group];
}

int32_t dbg_setDebug(debugGroup_t group, int32_t value)
{
	if(group >= eDebug_count) {
		return -1;
	}

	debugGroupValues[group] = value;
	if(group == eDebug_gloabal) {
		if(value) {
			setoutputlevel(errorLevelDebug);
		} else {
			setoutputlevel(errorLevelNormal);
		}
	}

	return 0;
}

int dbg_cmdSystem2(const char *cmd, const char *func, int32_t line)
{
	dprintf("%s()[%d]: cmd: '%s'\n", func, line, cmd);
	return system(cmd);
}
