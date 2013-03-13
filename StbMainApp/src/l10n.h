#if !defined(__L10N_H)
#define __L10N_H

/*
 l10n.h

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

/****************
* INCLUDE FILES *
*****************/

#include "interface.h"

/*******************
* EXPORTED MACROS  *
********************/

#define MAX_LANG_NAME_LENGTH (128)
#define _T(String)           l10n_getText( (String) )

/******************************************************************
* EXPORTED TYPEDEFS                                               *
*******************************************************************/

/*******************************************************************
* EXPORTED DATA                                                    *
********************************************************************/

extern const char          l10n_languageDir[];
extern char                l10n_currentLanguage[MAX_LANG_NAME_LENGTH];
extern interfaceListMenu_t LanguageMenu;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

#ifdef __cplusplus
extern "C" {
#endif

char *l10n_getText(const char* key);
int   l10n_init(const char* languageName);
void  l10n_cleanup(void);
int   l10n_switchLanguage(const char* newLanguage);
int   l10n_initLanguageMenu(interfaceMenu_t *pMenu, void* pArg);

#ifdef __cplusplus
}
#endif

#endif /* __L10N_H      Do not add any thing below this line */
