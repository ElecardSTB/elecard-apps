/*
 fusion.h

Copyright (C) 2015  Elecard Devices

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
 
#ifndef _FUSION_H_
#define _FUSION_H_

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <wait.h>
#include <sys/ioctl.h>
#include <resolv.h>

#include "md5.h"
#include "gfx.h"
#include "cJSON.h"
#include "media.h"
#include "interface.h"
#include "stsdk.h"

#define NO  0
#define YES 1

#define USB_ROOT "/mnt/"
#define FUSION_PLAYLIST_NAME  "fusion.playlist"

#define FUSION_MODIFIED 1
#define FUSION_NOT_MODIFIED 0
#define FUSION_ERR_FILETIME (-1)

#define FUSION_DEFAULT_CHECKTIME     (300)
#define FUSION_DEFAULT_SERVER_PATH   "http://public.tv/api"
#define FUSION_CFGDIR         "/var/etc/elecard/StbMainApp"
#define FUSION_HWCONFIG       FUSION_CFGDIR"/fusion.hwconf"

#define FUSION_REFRESH_DTMF_MS 5
#define MAX_SAVED_DIGITS 65

#define FUSION_STUB "fusion://stub"
#define FUSION_MIN_USLEEP 40

#define FUSION_LOCK_FILE    "/tmp/fusion_adverts.lock"

#define FUSION_DEFAULT_BANDLIM_KBYTE (2048)

typedef struct
{
    int ErrorCode;
    unsigned int Handle;
    char digits[MAX_SAVED_DIGITS];
    int count;
} STAUD_Ioctl_GetDtmf_t;

void fusion_startup();
void fusion_cleanup();
void fusion_fakeRestart();

#endif //_FUSION_H_
