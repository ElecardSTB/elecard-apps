
/*
 stb820.c

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

#include "stb820.h"

#ifdef STB82

#include <common.h>
#include <dlfcn.h>

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

#if (defined ENABLE_EXPAT) && (ENABLE_EXPAT == 2)
static void *expat_handle = NULL;
XML_Parser (*expat_ParserCreate)(const XML_Char *);
#endif

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

#if (defined ENABLE_EXPAT) && (ENABLE_EXPAT == 2)
XML_Parser XML_ParserCreate(const XML_Char *encoding)
{
	if (!expat_handle)
	{
		expat_handle = dlopen("/usr/local/webkit/libexpat.so", RTLD_LAZY);
		if (!expat_handle)
		{
			eprintf ("%s: failed to init libexpat: %s\n", __FUNCTION__, dlerror());
			return NULL;
		}
		expat_Parse = dlsym(expat_handle, "XML_Parse");
		expat_ParserCreate = dlsym(expat_handle, "XML_ParserCreate");
		expat_SetCharacterDataHandler = dlsym(expat_handle, "XML_SetCharacterDataHandler");
		expat_SetUserData = dlsym(expat_handle, "XML_SetUserData");
		expat_SetElementHandler = dlsym(expat_handle, "XML_SetElementHandler");
		expat_ParserFree = dlsym(expat_handle, "XML_ParserFree");
		if (expat_ParserCreate == NULL || 
			expat_SetCharacterDataHandler == NULL ||
			expat_SetUserData == NULL ||
			expat_SetElementHandler == NULL ||
			expat_ParserFree == NULL)
		{
			eprintf("%s: failed to init expat\n", __FUNCTION__);
			dlclose(expat_handle);
			expat_handle = NULL;
			return NULL;
		}
	}

	return (*expat_ParserCreate)(encoding);
}
#endif // ENABLE_EXPAT == 2

void stb820_terminate(void )
{
#if (defined ENABLE_EXPAT) && (ENABLE_EXPAT == 2)
	if (expat_handle)
	{
		dlclose(expat_handle);
		expat_ParserCreate = NULL;
	}
#endif
}

#endif // STB82
