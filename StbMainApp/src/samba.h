#if !defined(__SAMBA_H)
#define __SAMBA_H

/*
 samba.h

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

/*******************
* INCLUDE FILES    *
********************/

#include "defines.h"

#ifdef ENABLE_SAMBA
#include "interface.h"

/*******************
* EXPORTED MACROS  *
********************/

#define SAMBA_CONFIG SYSTEM_CONFIG_DIR "/samba.auto"

/*********************
* EXPORTED TYPEDEFS  *
**********************/

/*******************
* EXPORTED DATA    *
********************/

extern interfaceListMenu_t SambaMenu;
extern const  char sambaRoot[];

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

void samba_buildMenu(interfaceMenu_t *pParent);
int  samba_unmountShare(const char *mountPoint);
void samba_cleanup();
void *samba_readMounts(void);
void *samba_nextMount(void *mount);
int samba_manualBrowse(interfaceMenu_t *pMenu, void *pIgnored);
int samba_enterLogin(interfaceMenu_t *pMenu, void *pIgnored);

int samba_browseShare(interfaceMenu_t *pMenu, void *pArg);
void* samba_readShares(void);
void* samba_nextShare(void);
const char *samba_shareGetName(void *share);
int samba_shareGetIcon(void *share);

#endif // ENABLE_SAMBA

#endif /* __SAMBA_H      Do not add any thing below this line */
