#if !defined(__STBPVR_H)
#define __STBPVR_H

/*

Elecard STB820 Demo Application
Copyright (C) 2007  Elecard Devices

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA

*/

/*******************
* INCLUDE FILES    *
********************/

#include <time.h>

#include "sdp.h"
#include "service.h"

/*******************
* EXPORTED MACROS  *
********************/

#define STBPVR_PIDFILE     "/var/run/StbPvr.pid"
#define STBPVR_JOBLIST     CONFIG_DIR "/jobs.conf"
#define STBPVR_STATUSLIST  "/tmp/stbpvrStatus.conf"
#define STBPVR_SOCKET_FILE "/tmp/pvr.socket"
#define STBPVR_PIPE_FILE   "/tmp/pipe_pvr"
#define STBPVR_FOLDER      "/pvr"
#define STBELCD_SOCKET_FILE    "/var/run/elcd.sock"

#define STBPVR_DVB_CHANNEL_NONE (-1)

#define STBPVR_UDP_PORT   (1234)

/*********************
* EXPORTED TYPEDEFS  *
**********************/

typedef enum
{
	pvrJobTypeUnknown = 0,
	pvrJobTypeDVB,
	pvrJobTypeRTP,
	pvrJobTypeHTTP,
} pvrJobType_t;

typedef struct
{
	int            channel;
	unsigned short event_id;
	EIT_service_t *service;
} dvbJobInfo_t;

typedef struct
{
	media_desc     desc;
	struct in_addr ip;
	char           session_name[SDP_SHORT_FIELD_LENGTH];
} rtpJobInfo_t;

typedef struct
{
	char          *url;
	char           session_name[SDP_SHORT_FIELD_LENGTH];
} httpJobInfo_t;

typedef struct
{
	time_t         start_time;
	time_t         end_time;
	pvrJobType_t   type;
	union {
	  dvbJobInfo_t dvb;
	  rtpJobInfo_t rtp;
	  httpJobInfo_t http;
	} info;
} pvrJob_t;

/*******************
* EXPORTED DATA    *
********************/

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

#endif /* __STBPVR_H      Do not add any thing below this line */
