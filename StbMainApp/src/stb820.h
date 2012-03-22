#ifdef STB82

#ifndef __STB820_H
#define __STB820_H

/*
 stb820.h

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

#if (defined ENABLE_EXPAT) && (ENABLE_EXPAT == 2)
#include <expat.h>
#endif

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#if (defined ENABLE_EXPAT) && (ENABLE_EXPAT == 2)
#define XML_Parse                   (*expat_Parse)
#define XML_SetCharacterDataHandler (*expat_SetCharacterDataHandler)
#define XML_SetUserData             (*expat_SetUserData)
#define XML_SetElementHandler       (*expat_SetElementHandler)
#define XML_ParserFree              (*expat_ParserFree)
#endif

/***********************************************
* EXPORTED DATA                                *
************************************************/

#if (defined ENABLE_EXPAT) && (ENABLE_EXPAT == 2)
enum XML_Status (*expat_Parse)(XML_Parser parser, const char *s, int len, int isFinal);
void (*expat_SetUserData)(XML_Parser parser, void *userData);
void (*expat_SetElementHandler)(XML_Parser parser,
                      XML_StartElementHandler start,
                      XML_EndElementHandler end);
void (*expat_SetCharacterDataHandler)(XML_Parser parser,
                            XML_CharacterDataHandler handler);
void (*expat_ParserFree)(XML_Parser parser);
#endif

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#if (defined ENABLE_EXPAT) && (ENABLE_EXPAT == 2)
XML_Parser XML_ParserCreate(const XML_Char *encoding);
#endif

void stb820_terminate(void);

#ifdef __cplusplus
}
#endif

#endif // __STB820_H
#endif // STB82
