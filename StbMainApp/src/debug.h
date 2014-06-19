
/*
 debug.h

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

#include "defines.h"

#include <common.h>

/***********************************************
* EXPORTED MACROS                              *
************************************************/
#define DEBUG_MESSAGE         "DEBUG"
#define DEBUG_BOUQUET "BOUQUET"

#define dprintf(...)        DPRINT(errorLevelDebug, __VA_ARGS__)
#define dbg_cmdSystem(cmd)  dbg_cmdSystem2(cmd, __func__, __LINE__)

#ifdef TRACE
	#define TRACEE printf("ENTER: %s\n", __FUNCTION__);
	#define TRACEL printf("LEAVE: %s\n", __FUNCTION__);
	#define TRACEF printf("FAILE: %s\n", __FUNCTION__);
#else
	#define TRACEE
	#define TRACEL
	#define TRACEF
#endif

#ifdef ALLOC_DEBUG
	#define dcalloc(x,y)	dbg_calloc(x, y, __FUNCTION__)
	#define dmalloc(x)		dbg_malloc(x, __FUNCTION__)
	#define	dfree(x)		dbg_free(x, __FUNCTION__)
	#define drealloc(x,y)	dbg_realloc(x, y, __FUNCTION__)
#else
	#define dcalloc(x,y)	calloc(x, y)
	#define dmalloc(x)		malloc(x)
	#define	dfree(x)		free(x)
	#define drealloc(x,y)	realloc(x, y)
#endif

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/

int32_t gdbDebug;
int32_t gdbBouquet;

#ifdef __cplusplus
extern "C" {
#endif

void *dbg_calloc(size_t nmemb, size_t size, const char *location);
void *dbg_malloc(size_t size, const char *location);
void  dbg_free   (void *ptr,  const char *location);
void *dbg_realloc(void *ptr, size_t size, const char *location);
int  dbg_ThreadInit(void);
void  dbg_ThreadStop(void);
int  dbg_getDebag(char *);
int  dbg_cmdSystem2(const char *cmd, const char *func, int32_t line);

#ifdef __cplusplus
};
#endif
