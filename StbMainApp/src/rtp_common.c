
/*
 rtp_common.c

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

#include "rtp_common.h"
#include "debug.h"

#include <sdp.h>
#include <service.h>

#ifdef STB225
#include <phStbSbmSink.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

void rtp_common_get_pids(pStreamsPIDs streamPIDs, int *vFormat, int *vPID, int *aFormat, int *aPID, int *pPID, struct list_head *audio)
{
	int i;
	int progid = 0;

	struct list_head *p1;
    struct audio_track *atrack;
    struct list_head *tmp;

	if (audio != NULL)
	{
		if (!list_empty(audio))
		{
			dprintf("%s: Free audio tracks...\n", __FUNCTION__);
			list_for_each_safe(p1, tmp, audio)
			{
				list_del(p1);
				atrack = list_entry(p1, struct audio_track, list);
				dfree(atrack);
			}
		}
	}

	*aPID = *vPID = *pPID = *aFormat = *vFormat = 0;

	for ( i=0; i<streamPIDs->ItemsCnt; i++ )
	{
		dprintf("%s: \t%d: (0x%x) stream_type = 0x%x, ID = 0x%x\n", __FUNCTION__,streamPIDs->pStream[i].ProgNum,streamPIDs->pStream[i].ProgID,streamPIDs->pStream[i].stream_type,streamPIDs->pStream[i].elementary_PID);
		if ( streamPIDs->pStream[i].stream_type == 0x2 )
		{ //mpeg2
			dprintf("%s: got mpeg2 with pid 0x%X\n", __FUNCTION__, streamPIDs->pStream[i].elementary_PID);
			*vFormat = streamTypeVideoMPEG2;
			*vPID = streamPIDs->pStream[i].elementary_PID;
			progid = streamPIDs->pStream[i].ProgID;
			*pPID = streamPIDs->pStream[i].PCR_PID;
			break;
		} else if ( streamPIDs->pStream[i].stream_type == 0x1b )
		{ //h264
			dprintf("%s: got h264 with pid 0x%X\n", __FUNCTION__, streamPIDs->pStream[i].elementary_PID);
			*vFormat = streamTypeVideoH264;
			*vPID = streamPIDs->pStream[i].elementary_PID;
			progid = streamPIDs->pStream[i].ProgID;
			*pPID = streamPIDs->pStream[i].PCR_PID;
			break;
		}
	}

	for ( i=0; i<streamPIDs->ItemsCnt; i++ )
	{
		dprintf("%s: \t%d: (0x%x) stream_type = 0x%x, ID = 0x%x\n", __FUNCTION__,streamPIDs->pStream[i].ProgNum,streamPIDs->pStream[i].ProgID,streamPIDs->pStream[i].stream_type,streamPIDs->pStream[i].elementary_PID);
		if ( (streamPIDs->pStream[i].stream_type == 0x3 || streamPIDs->pStream[i].stream_type == 0x4) && (progid == 0 || progid == streamPIDs->pStream[i].ProgID) )
		{ //mp3
			dprintf("%s: got mp3 with pid 0x%X\n", __FUNCTION__, streamPIDs->pStream[i].elementary_PID);
			if (*aPID == 0)
			{
				*aFormat = streamTypeAudioMPEG1;
				*aPID = streamPIDs->pStream[i].elementary_PID;
			}
			if (audio != NULL)
			{
				p1 = dmalloc(sizeof(struct audio_track));
				atrack = list_entry(p1, struct audio_track, list);
				atrack->pid = streamPIDs->pStream[i].elementary_PID;
				list_add(p1, audio);
			}
		}
#ifdef ALLOW_AAC
		else if ( streamPIDs->pStream[i].stream_type == 0xf && (progid == 0 || progid == streamPIDs->pStream[i].ProgID) )
		{ //aac
			dprintf("%s: got aac with pid 0x%X\n", __FUNCTION__, streamPIDs->pStream[i].elementary_PID);
			if (*aPID == 0)
			{
				*aFormat = streamTypeAudioAAC;
				*aPID = streamPIDs->pStream[i].elementary_PID;
			}
			if (audio != NULL)
			{
				p1 = dmalloc(sizeof(struct audio_track));
				atrack = list_entry(p1, struct audio_track, list);
				atrack->pid = streamPIDs->pStream[i].elementary_PID;
				list_add(p1, audio);
			}
		}
#endif
		else if ( (streamPIDs->pStream[i].stream_type == 0x6 || streamPIDs->pStream[i].stream_type == 0x81)
				&& (progid == 0 || progid == streamPIDs->pStream[i].ProgID) )
		{ //ac3
			if (*aPID == 0)
			{
				*aFormat = streamTypeAudioAC3;
				*aPID = streamPIDs->pStream[i].elementary_PID;
			}
			if (audio != NULL)
			{
				p1 = dmalloc(sizeof(struct audio_track));
				atrack = list_entry(p1, struct audio_track, list);
				atrack->pid = streamPIDs->pStream[i].elementary_PID;
				list_add(p1, audio);
			}
		}

	}
/*
	if (audio != NULL)
	{
		if (!list_empty(audio))
		{
			list_for_each_safe(p1, tmp, audio)
			{
				atrack = list_entry(p1, struct audio_track, list);
				dprintf("%s: Audio %d (%08X=%08X->%08X)\n", __FUNCTION__, atrack->pid, p1, tmp, audio);
			}
		}
	}
*/
}

int rtp_common_change_audio(rtp_common_instance *pInst, int aPID)
{
	int ret;
	struct dmx_pes_filter_params pesfilter;

	if (pInst->fda >= 0)
	{
		// Stop demuxing old audio pid
		ret = ioctl(pInst->fda, DMX_STOP);
		if ( ret != 0 )
		{
			eprintf("rtp_common: ioctl DMX_STOP fda failed\n");
			return ret;
		}
		// reconfigure audio decoder
		// ...
		// reconfigure demux
		pesfilter.input = DMX_IN_DVR;
		pesfilter.output = DMX_OUT_DECODER;
		pesfilter.pes_type = DMX_PES_AUDIO;
		pesfilter.flags = 0;
		pesfilter.pid = aPID;

		ret = ioctl(pInst->fda, DMX_SET_PES_FILTER, &pesfilter);
		if ( ret != 0 )
		{
			eprintf("rtp_common: ioctl DMX_SET_PES_FILTER fda failed\n");
			return ret;
		}
		// start demuxing new audio pid
		ret = ioctl(pInst->fda, DMX_START);
		if ( ret != 0 )
		{
			eprintf("rtp_common: ioctl DMX_START fda failed\n");
			return ret;
		}
	}

	return 0;
}

void rtp_common_init_instance(rtp_common_instance *pInst)
{
	pInst->dvrfd = -1;
	pInst->fdv = -1;
	pInst->fda = -1;
	pInst->fdp = -1;
#ifdef STB225
	pInst->sbmSink = (unsigned int)-1;
#endif
	pInst->pipeString[0] = 0;

	INIT_LIST_HEAD(&pInst->audio_tracks);
}

void rtp_common_close(rtp_common_instance *pInst)
{
	int ret;
	struct list_head *p1;
    struct audio_track *atrack;
    struct list_head *tmp;

	if ( pInst->dvrfd >= 0 )
	{
		ret = close(pInst->dvrfd);
		if ( ret !=0 )
		{
			eprintf("rtp_common: DVR closed with error %d, errno %d\n", ret, errno);
		}
		pInst->dvrfd = -1;
	}

	if ( pInst->fdv >= 0 )
	{
		ret = close(pInst->fdv);
		if ( ret !=0 )
		{
			eprintf("rtp_common: Video filter closed with error %d, errno %d\n", ret, errno);
		}
		pInst->fdv = -1;
	}

	if ( pInst->fda >= 0 )
	{
		ret = close(pInst->fda);
		if ( ret !=0 )
		{
			eprintf("rtp_common: Audio filter closed with error %d, errno %d\n", ret, errno);
		}
		pInst->fda = -1;
	}

	if ( pInst->fdp >= 0 )
	{
		ret = close(pInst->fdp);
		if ( ret !=0 )
		{
			eprintf("rtp_common: PCR closed with error %d, errno %d\n", ret, errno);
		}
		pInst->fdp = -1;
	}

#ifdef STB225
	if (pInst->sbmSink != (unsigned int)-1)
	{
		//phStbSbmSink_Destroy(pInst->sbmSink);
		pInst->sbmSink = (unsigned int)-1;
		pInst->pipeString[0] = 0;
	} else
#endif
	{
		if (pInst->pipeString[0] != 0)
		{
			char pipeString[PATH_MAX];

			sprintf(pipeString, "rm -f %s", pInst->pipeString);
			system(pipeString);

			pInst->pipeString[0] = 0;
		}
	}

	if (!list_empty(&pInst->audio_tracks))
	{
		list_for_each_safe(p1, tmp, &pInst->audio_tracks)
		{
			list_del(p1);
			atrack = list_entry(p1, struct audio_track, list);
			dfree(atrack);
		}
	}
}

int rtp_common_open(/* IN */	char *demuxer, char *pipe, char *pipedev, char *dvrdev, int videopid, int audiopid, int pcrpid,
					/* OUT */	int *dvrfd, int *videofd, int *audiofd, int *pcrfd)
{
	int ret=0;
	struct dmx_pes_filter_params pesfilter;

	if (demuxer != NULL)
	{
		//dprintf("%s: open DEMUXER\n", __FUNCTION__);

		if (videofd != NULL)
		{
			if ( (*videofd = open(demuxer, O_NONBLOCK)) < 0 )
			{
				eprintf("rtp_common: Video filter open failed %d, errno %d\n", *videofd, errno);
				return -1;
			}

			pesfilter.input = DMX_IN_DVR;
			pesfilter.output = DMX_OUT_DECODER;
			pesfilter.pes_type = DMX_PES_VIDEO;
			pesfilter.flags = 0;
			pesfilter.pid = videopid;

			if ( (ret = ioctl(*videofd, DMX_SET_PES_FILTER, &pesfilter)) != 0 )
			{
				eprintf("rtp_common: ioctl DMX_SET_PES_FILTER for video filter failed %d, errno %d\n", ret, errno);
				return ret;
			}

			if ( (ret = ioctl(*videofd, DMX_START)) != 0 )
			{
				eprintf("rtp_common: ioctl DMX_START for video filter failed %d, errno %d\n", ret, errno);
				return ret;
			}
		}

		if (audiofd != NULL)
		{
			if ( (*audiofd = open(demuxer, O_NONBLOCK)) < 0 )
			{
				eprintf("rtp_common: Audio filter open failed %d, errno %d\n", *audiofd, errno);
				return -1;
			}

			pesfilter.input = DMX_IN_DVR;
			pesfilter.output = DMX_OUT_DECODER;
			pesfilter.pes_type = DMX_PES_AUDIO;
			pesfilter.flags = 0;
			pesfilter.pid = audiopid;

			if ( (ret = ioctl(*audiofd, DMX_SET_PES_FILTER, &pesfilter)) != 0 )
			{
				eprintf("rtp_common: ioctl DMX_SET_PES_FILTER for audio filter failed %d, errno %d\n", ret, errno);
				return ret;
			}

			if ( (ret = ioctl(*audiofd, DMX_START)) != 0 )
			{
				eprintf("rtp_common: ioctl DMX_START for Audio filter failed %d, errno %d\n", ret, errno);
				return ret;
			}
		}

		if (pcrfd != NULL)
		{
			if ( (*pcrfd = open(demuxer, O_NONBLOCK)) < 0 )
			{
				eprintf("rtp_common: PCR filter open failed %d, errno %d\n", *pcrfd, errno);
				return -1;
			}

			pesfilter.input = DMX_IN_DVR;
			pesfilter.output = DMX_OUT_DECODER;
			pesfilter.pes_type = DMX_PES_PCR;
			pesfilter.flags = 0;
			pesfilter.pid = pcrpid;

			if ( (ret = ioctl(*pcrfd, DMX_SET_PES_FILTER, &pesfilter)) != 0 )
			{
				eprintf("rtp_common: ioctl DMX_SET_PES_FILTER for PCR filter failed %d, errno %d\n", ret, errno);
				return ret;
			}

			if ( (ret = ioctl(*pcrfd, DMX_START)) != 0 )
			{
				eprintf("rtp_common: ioctl DMX_START for PCR filter failed %d, errno %d\n", ret, errno);
				return ret;
			}
		}
	}

	if (pipe != NULL)
	{
		char pipeString[PATH_MAX];

		dprintf("%s: open PIPE\n", __FUNCTION__);

		if (pipedev != NULL)
		{
			sprintf(pipeString, "[ -e %s ] || ln -s %s %s", pipe, pipedev, pipe);
			system(pipeString);
		} else
		{
			sprintf(pipeString, "mkfifo %s", pipe);
			system(pipeString);
		}
	}

	if (dvrfd != NULL && dvrdev != NULL)
	{
		//dprintf("%s: open DVR\n", __FUNCTION__);

		if ( (*dvrfd = open(dvrdev, O_WRONLY)) < 0 )
		{
			eprintf("rtp_common: DVR device open failed %d, errno %d\n", *dvrfd, errno);
			return -1;
		}

		//dprintf("%s: opened DVR %s: %d\n", __FUNCTION__, dvrdev, *dvrfd);
	}

	//dprintf("%s: open succeeded\n", __FUNCTION__);

	return 0;
}

#ifdef STB225
int rtp_common_sink_prepare(rtp_common_instance *pInst)
{
	char resourceName[128];
	tmErrorCode_t status;
	uint32_t pinFlags = 0;

	status = phStbSbmSink_Create(pinFlags, &pInst->sbmSink, resourceName);

	dprintf("%s: phStbSbmSink_Create: %d\n", __FUNCTION__, status);

	if (status != 0)
	{
		eprintf("rtp_common: DVR device sink creation failed\n");
		return -1;
	}

	sprintf(pInst->pipeString, "/dev/%s", resourceName);

	return 0;
}

int rtp_common_sink_open(rtp_common_instance *pInst)
{
	if ( (pInst->dvrfd = open(pInst->pipeString, O_WRONLY)) < 0 )
	{
		eprintf("rtp_common: DVR device open failed %d, errno %d\n", pInst->dvrfd, errno);
		return -1;
	}

	return 0;
}
#endif
