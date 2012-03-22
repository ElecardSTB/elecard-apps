#ifndef __XMLCONFIG_H
#define __XMLCONFIG_H

/*
 xmlconfig.h

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
* EXPORTED TYPEDEFS                            *
************************************************/

typedef void *xmlConfigHandle_t;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif
	xmlConfigHandle_t xmlConfigOpen(const char *path);
	xmlConfigHandle_t xmlConfigParse(const char *data);
	xmlConfigHandle_t xmlConfigGetElement(xmlConfigHandle_t parent, const char *name, int index);
	xmlConfigHandle_t xmlConfigGetNextElement(xmlConfigHandle_t element, const char *name);
	const char *xmlConfigGetElementText(xmlConfigHandle_t element);
	const char *xmlConfigGetText(xmlConfigHandle_t parent, const char *name);
	const char *xmlConfigGetAttribute(xmlConfigHandle_t parent, const char *name);
	void xmlConfigClose(xmlConfigHandle_t config);
#ifdef __cplusplus
}
#endif

#endif  /* __XMLCONFIG_H      Do not add any thing below this line */
