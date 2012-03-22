#if !defined(__RTP_COMMON_H)
#define __RTP_COMMON_H

/*
 rtp_common.h

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

#include "rtp_func.h"
#include "rtsp.h"
#include "list.h"

/*******************
* EXPORTED MACROS  *
********************/

#if (defined STB82)
#include <phStbDFBVideoProviderCommonTypes.h>
#else
#if (defined STB225)
#include <phStbDFBVideoProviderCommonElcTypes.h>
#include <phStbSbmSink.h>
#else
#define SET_PAUSE(f, w)
#define SET_NORMAL_SPEED(f, w)
#define SET_FAST_SPEED(f, w)
#define FLUSH_PROVIDER(f, w)
#define SET_AUDIO_PID(f, w, p)
#define SET_VIDEO_PID(f, w, p)
#endif
#endif

/*********************
* EXPORTED TYPEDEFS  *
**********************/

typedef struct
{
	// file descriptors for frontend, demux-video, demux-audio
	int fdv;
	int fda;
	int fdp;
	int dvrfd;
#ifdef STB225
	phStbSbmSink_t sbmSink;
#endif

	sdp_desc selectedDesc;
	rtsp_stream_info stream_info;

	/* indicates if current stream is playing (not previewing) */
	int playingFlag;

	char pipeString[PATH_MAX];

	pthread_t thread;
	pthread_t collectThread;

	int collectFlag;

	int data_timeout;

	struct list_head audio_tracks;

	struct rtp_session_t *rtp_session;
} rtp_common_instance;


struct audio_track 
{
	struct list_head list;
	int pid;
};

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

void rtp_common_init_instance(rtp_common_instance *pInst);

void rtp_common_close(rtp_common_instance *pInst);

int rtp_common_open(/* IN */	char *demuxer, char *pipe, char *pipedev, char *dvrdev, int videopid, int audiopid, int pcrpid,
					/* OUT */	int *dvrfd, int *videofd, int *audiofd, int *pcrfd);

void rtp_common_get_pids(pStreamsPIDs streamPIDs, int *vFormat, int *vPID, int *aFormat, int *aPID, int *pPID, struct list_head *audio);

#ifdef STB225
int rtp_common_sink_open(rtp_common_instance *pInst);
int rtp_common_sink_prepare(rtp_common_instance *pInst);
#endif

int rtp_common_change_audio(rtp_common_instance *pInst, int aPID);

#endif //__RTP_COMMON_H
