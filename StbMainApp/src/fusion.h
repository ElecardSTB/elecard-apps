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

typedef struct
{
    int ErrorCode;
    unsigned int Handle;
    char digits[MAX_SAVED_DIGITS];
    int count;
} STAUD_Ioctl_GetDtmf_t;


void fusion_startup();
void fusion_cleanup();

#endif //_FUSION_H_
