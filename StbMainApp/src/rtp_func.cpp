
/*
 rtp_func.cpp

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

#include "rtp_func.h"

#include "debug.h"
#include "StbMainApp.h"
#include "sem.h"

// NETLib
#include <sdp.h>
#include <sap.h>
#include <smrtp.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef INCLUDE_CCRTP
#include <cstdlib>
#include <ccrtp/rtp.h>

using namespace ost;
#endif // INCLUDE_CCRTP

#ifdef ENABLE_VERIMATRIX
#include "VMClient.h"
#endif

#ifdef ENABLE_SECUREMEDIA
#include <smplatform.h>
#include <smmsg.h>
#include <smargs.h>
#include <smudp.h>
#include <smclient.h>
#include <smhttp.h>
#include <smurl.h>
#include <smtime64.h>
#include <smfaces.h>
#include <tkimports.h>
#include <smrandom.h>
#include <smhttpsrv.h>
#include <smmisc.h>
#include <smps.h>

#include <smm2.h>
#include <smm2dec.h>

SM_HTTPGET_IMPL();
SML_USE();
#endif // ENABLE_SECUREMEDIA

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define BUFFER_SIZE (2*1024)
#define GET_STREAMS_TIMEOUT (5)

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

typedef struct rtp_session_t
{

#ifdef INCLUDE_CCRTP
	RTPSession *rtp_session;
#endif

	pthread_t recv_thread;
	bool cancel_thread;
	int engine;
	int size_addon;

	udp_sap_session_t *sapSession;

	int dvrfd;
	int dmxfd;

	struct timeval last_data_timestamp;

	unsigned char buffer[BUFFER_SIZE];

	void *hDemuxer;
	pStreamsPIDs pTsStreams;

	pmysem_t confirm_sem;

#ifdef ENABLE_VERIMATRIX
	int             vmEnable;
	void           *vmContext;
	uint16_t        iOperatorId;
	uint8_t         iRatingLevel;
#endif

#ifdef ENABLE_SECUREMEDIA
	int             smEnable;
	SmM2TsDecProcessor *smContext;
#endif

	int pktcounter;

	struct smrtp_session_t *smrtp_session[4];
} rtp_session;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

extern "C" int DemuxTS_Open(void **ppInstance);
extern "C" int DemuxTS_Close(void *ppInstance);
extern "C" int DemuxTS_Parse(void *ivp_, char *pbBuffer, int iBufferLength, pStreamsPIDs pOutputPIDs);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

/**************************************************************************
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

int rtp_engine_supports_transport(int eng, int transport)
{

	dprintf("%s: transport: %d == %s\n", __FUNCTION__, transport, transport == mediaProtoRTP ? "RTP" : "UDP");

	if ( eng == 0 )
	{ // elecard
		return transport == mediaProtoRTP || transport == mediaProtoUDP;
	} else if ( eng == 1 )
	{ // ccRTP
		return transport == mediaProtoRTP;
	} else
	{ // smallRTP
		return transport == mediaProtoRTP || transport == mediaProtoUDP;
	}
	return 0;
}


void rtp_change_eng(rtp_session *session, int eng)
{
	if ( eng == 0 )
	{ // elecard
		session->engine = eng;
		session->size_addon = -4;
	} else
	{ // ccRTP, smallRTP
		session->engine = eng;
		session->size_addon = 0;
	}
	//dprintf("%s: RTP engine changed to %d, size_addon = %d\n", __FUNCTION__, session->engine, session->size_addon);
}

void rtp_flush_receive_buffers(rtp_session *session)
{
	dprintf("%s: FLUSH buffers\n", __FUNCTION__);

	if ( session->engine == 2 )
	{
		// smallRTP
		int m = 0;

		while (session->smrtp_session[m] != NULL)
		{
			small_rtp_flush(session->smrtp_session[m]);
			m++;
		}
	} else
	{
		// elecard, ccRTP
	}
}

void rtp_get_last_data_timestamp( rtp_session *session, struct timeval *ts )
{
	memcpy(ts, &session->last_data_timestamp, sizeof(struct timeval));
}

int rtp_get_found_streams(rtp_session *session, rtp_list *found_streams)
{
	if (session->sapSession != NULL)
	{
		if (session->sapSession->started > 0)
		{
			found_streams->count = udp_sap_get_sdp(session->sapSession, found_streams->items, RTP_MAX_STREAM_COUNT);
		} else if (session->sapSession->started == 0)
		{
			int rc;
			/* try starting receiver */
			rc = udp_sap_start(session->sapSession);
			if ( rc != 0 )
			{
				eprintf("rtp_func: failed to start sap receiver == %i\n", rc);
				return -1;
			}
		}
	}

	return found_streams->count;
}

int rtp_sdp_start_collecting(rtp_session *session)
{
	int rc;

	rc = udp_sap_init( &session->sapSession );
	if ( rc != 0 )
	{
		eprintf("rtp_func: failed to init sap receiver == %i\n", rc);
		return -1;
	}

	rc = udp_sap_start(session->sapSession);
	if ( rc != 0 )
    {
		eprintf("rtp_func: failed to start sap receiver == %i\n", rc);
		return -1;
    }

	return 0;
}

int rtp_sdp_set_collecting_state(rtp_session *session, int startFlag)
{
	// unused now...

	return 0;
}

int rtp_sdp_stop_collecting(rtp_session *session)
{
	if (session->sapSession != NULL)
	{
		udp_sap_stop(session->sapSession);
		session->sapSession = NULL;
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// receiver
//
//#ifndef USE_CCRTP
//------------------------------------------------------------------------

int set_nonblock_flag (int desc, int value)
{
  int oldflags = fcntl (desc, F_GETFL, 0);
  /* If reading the flags failed, return error indication now. */
  if (oldflags == -1)
    return -1;
  /* Set just the flag we want to set. */
  if (value != 0)
    oldflags |= O_NONBLOCK;
  else
    oldflags &= ~O_NONBLOCK;
  /* Store modified flag word in the descriptor. */
  return fcntl (desc, F_SETFL, oldflags);
}

//#else // #ifndef USE_CCRTP
// Returns 0 if DemuxTS_Parse didn't find any PIDs
unsigned long _confirm(void *arg, const unsigned char *buffer, unsigned long numbytes)
//#endif // #ifndef USE_CCRTP
{
	int *outfd;
	//dprintf("%s: Enter confirm(); numbytes=%d\r\n", __FUNCTION__, numbytes);
	rtp_session *session = (rtp_session *)arg;

	//static int fd = open("/dump/rtp_dump.ts", O_CREAT|O_WRONLY|O_TRUNC);

	//write(fd, buffer, numbytes+session->size_addon);

	/*
	static FILE *fd = fopen("/rtp_dump.ts", "wb");

	if (fd != NULL) {
		fwrite(buffer, 1, numbytes, fd);
	}
	*/

	session->pktcounter++;

	outfd = &session->dvrfd;

#ifdef ENABLE_VERIMATRIX
	if (session->vmEnable != 0 && session->vmContext != NULL)
	{
		int iBytesRead, iResult;
		iResult = VMDecryptStreamData(session->vmContext,(unsigned char*)buffer,numbytes+session->size_addon,&iBytesRead);
		if (iResult != 0 || iBytesRead != numbytes+session->size_addon)
		{
			eprintf("rtp_func: Verimatrix decrypt result: %d, processed: %d\n", iResult, iBytesRead);
		}
	}
#endif

#ifdef ENABLE_SECUREMEDIA
	if (session->smEnable != 0 && session->smContext != NULL)
	{
		size_t iBytesRead;
		SM_RESULT iResult;

		iResult = SmM2TsDecProcessor_Decrypt(session->smContext, (void*)buffer, numbytes+session->size_addon, &iBytesRead);
		if (iResult != 0 || iBytesRead != numbytes+session->size_addon)
		{
			dprintf("rtp_func: SecureMedia decrypt result: %d, processed: %d\n", iResult, iBytesRead);
		}
	}
#endif

	if ( *outfd == 0 && session->hDemuxer != NULL )
	{
		//dprintf("%s: demux pkt %d bytes\n", __FUNCTION__, numbytes+session->size_addon);
		DemuxTS_Parse(session->hDemuxer, (char*)buffer, numbytes+session->size_addon, session->pTsStreams);
		//if ( session->pTsStreams->ItemsCnt )
		if ( session->pTsStreams->ItemsCnt > 0 &&
			 session->pTsStreams->ProgramCnt > 0 &&
			 (!(session->pktcounter< 10
				&& session->pTsStreams->ItemsCnt == 1))
			 )
			 //session->pTsStreams->ProgramCnt == session->pTsStreams->TotalProgramCnt)
		{

			dprintf("%s: found streams %d\n", __FUNCTION__, session->pTsStreams->ItemsCnt);
			//dfree(session->pTsStreams->pStream);
			//session->pTsStreams->ItemsCnt = 0;
			DemuxTS_Close(session->hDemuxer);
			eprintf("rtp_func: Found %d streams in %d/%d programs of TS on buffer %d\n", session->pTsStreams->ItemsCnt, session->pTsStreams->ProgramCnt, session->pTsStreams->TotalProgramCnt, session->pktcounter);
			session->hDemuxer = NULL;
		} else
		{
			//dprintf("%s: Not found PIDs\n", __FUNCTION__);
			return 0;
		}
	} else if ( *outfd >= 0 )
	{

		while (*outfd == 0)
		{
			//dprintf("%s: Waiting for fd to start output on packet %d\n", __FUNCTION__, pktcounter);
			usleep(4000);
		}

		if (*outfd > 0)
		{
			mysem_get(session->confirm_sem);

			gettimeofday(&session->last_data_timestamp, 0);

			//dprintf("%s: write %d bytes to %d\n", __FUNCTION__, numbytes+session->size_addon, *outfd);

	#ifndef DISABLE_OUTPUT
			set_nonblock_flag(*outfd, 1);

			//static int fd = open("/rtp_dump.h264", O_CREAT|O_TRUNC|O_WRONLY);
			int res = 0;
			int offset = 0;
			int size = numbytes+session->size_addon;
			fd_set wfd;
			struct timeval tout;
			while (size > 0)
			{
				FD_ZERO(&wfd);
				FD_SET(*outfd, &wfd);
				tout.tv_sec = 1;
				tout.tv_usec = 0;
				res = select((*outfd)+1, NULL, &wfd, NULL, &tout);
				if (res == 0) // timeout
				{
					eprintf("rtp_func: select timeout %d bytes to %d\n", size, *outfd);
					mysem_release(session->confirm_sem);
					return numbytes+session->size_addon-size;
				}
				if (res == -1)
				{
					eprintf("rtp_func: select error %d bytes to %d: errno = %d\n", size, *outfd, errno);
					mysem_release(session->confirm_sem);
					return 0;
				}
				res = write(*outfd, &buffer[offset], size);
				if (res == size)
				{
					break;
				}
				//dprintf("%s: write %d bytes to %d: res = %d\n", __FUNCTION__, size, *outfd, res);
				if (res > 0)
				{
					offset += res;
					size -= res;
				} else if (res == -1 && errno == EWOULDBLOCK)
				{
					eprintf("rtp_func: write will block %d bytes to %d: res = %d\n", size, *outfd, res);
				} else
				{
					eprintf("rtp_func: error write %d bytes to %d: res = %d, errno %d\n", size, *outfd, res, errno);
					break;
				}
			}
			//dprintf("%s: final write %d bytes to %d: res = %d\n", __FUNCTION__, size, *outfd, res);
	#endif

			mysem_release(session->confirm_sem);
		}
	}

	return numbytes+session->size_addon;
}


#ifdef INCLUDE_CCRTP
void *recv_main(void *pArg)
{
	rtp_session *session = (rtp_session *)pArg;

	dprintf("%s: in\n", __FUNCTION__);

	defaultApplication().setSDESItem(SDESItemTypeTOOL,
									 "rtplisten demo app.");
	session->rtp_session->setExpireTimeout(1000000);

	session->rtp_session->setPayloadFormat(StaticPayloadFormat(sptPCMU));

	session->rtp_session->startRunning();

	//dprintf("%s: session running=%d, cancelled=%d\n", __FUNCTION__, session->rtp_session->isRunning(), session->rtp_session->isCancelled());

	while ( session->cancel_thread == false )
	{
		const AppDataUnit* adu;
		while ( (adu = session->rtp_session->getData(session->rtp_session->getFirstTimestamp())) )
		{
			//dprintf("%s: got packet\n", __FUNCTION__);
			_confirm(session, (unsigned char*)adu->getData(), adu->getSize());
			delete adu;
		}
		Thread::sleep(7);
	}

	delete session->rtp_session;

	dprintf("%s: out\n", __FUNCTION__);
}
#endif // #ifdef INCLUDE_CCRTP

//------------------------------------------------------------------------


//#endif // #ifndef USE_CCRTP


#ifdef ENABLE_VERIMATRIX
extern "C" int rtp_enable_verimatrix(struct rtp_session_t *session, char *inipath)
{
	session->vmEnable = 1;
	eprintf("rtp_func: Create Verimatrix CA Context\n");
	session->vmContext =  VMCreateContext(NULL,true);
	/* Load custom INI file */
	eprintf("rtp_func: Load Verimatrix INI file from %s\n", inipath);
	VMSetINIFile( session->vmContext, inipath );
	VMLoadINIFile( session->vmContext );
	eprintf("rtp_func: Update Verimatrix Channel Keys\n");
	VMUpdateChannelKeys( session->vmContext );
	eprintf("rtp_func: Set Verimatrix Operator Id to %d\n", session->iOperatorId);
	VMSetOperatorId( session->vmContext, session->iOperatorId );
	/* decrypt in place */
	eprintf("rtp_func: Set Verimatrix Callback to decrypt in place\n");
	VMSetCallback( session->vmContext, NULL, 188, NULL);
	/* prepare for new movie */
	eprintf("rtp_func: Reset Verimatrix Stream\n");
	VMResetStream( session->vmContext );

	return 0;
}
#endif

#ifdef ENABLE_SECUREMEDIA
extern "C" int rtp_enable_securemedia(struct rtp_session_t *session)
{
	session->smEnable = 1;
	eprintf("rtp_func: Create SecureMedia \n");
	session->smContext = SmClientPlugin_Mpeg2Processor();
}
#endif

int rtp_start_receiver(rtp_session *session, sdp_desc *desc, int media_no, int delayFeeding, int verimatrix) {
	// demux
	session->pTsStreams = (pStreamsPIDs)dmalloc(sizeof(StreamsPIDs));
	memset(session->pTsStreams, 0, sizeof(StreamsPIDs));
	if (desc->media[media_no].fmt == payloadTypeMpegTS)
	{
		DemuxTS_Open(&session->hDemuxer);
	} else
	{
		session->hDemuxer = NULL;
	}

	session->dmxfd = 0;
	session->dvrfd = 0;

	mysem_create(&session->confirm_sem);

#ifdef ENABLE_VERIMATRIX
	if (verimatrix != 0)
	{
		eprintf("rtp_func: Enable Verimatrix\n");
		rtp_enable_verimatrix(session, VERIMATRIX_INI_FILE);
	}
#endif
#ifdef ENABLE_SECUREMEDIA
	if (verimatrix != 0)
	{
		eprintf("rtp_func: Enable SecureMedia\n");
		rtp_enable_securemedia(session);
	}
#endif

	dprintf("%s: start_receiver %s:%d (%s)\n", __FUNCTION__, inet_ntoa(desc->connection.address.IPv4), desc->media[media_no].port, desc->session_info);

//#ifndef USE_CCRTP
	if ( session->engine == 0 )
	{
		eprintf("rtp_func: NWSource engine is no longer used\n");
		return -1;
//#else // #ifndef USE_CCRTP

	} else if ( session->engine == 1 )
	{ // ccRTP
#ifdef INCLUDE_CCRTP
		dprintf("%s: !!! create RTPSession %s:%d !!!\n", __FUNCTION__, inet_ntoa(desc->connection.address.IPv4), desc->media[media_no].port);

		InetMcastAddress ima;

		try
		{
			ima = InetMcastAddress(desc->connection.address.IPv4);
		} catch ( ... )
		{
			eprintf("rtp_func: bad multicast address 0x%04x\n", desc->connection.address.IPv4);
		}

		dprintf("%s: resolved address\n", __FUNCTION__);

		// ccRTP expects dataPort to be even if controlPort is 0 or unspecified
		session->rtp_session = new RTPSession(ima,desc->media[media_no].port, desc->media[media_no].port+1);

		dprintf("%s: start receiving thread\n", __FUNCTION__);

		session->cancel_thread = false;

		pthread_create(&session->recv_thread, NULL, recv_main, (void*) session);
		pthread_detach(session->recv_thread);
#else // #ifdef INCLUDE_CCRTP
		return -1;
#endif // #ifdef INCLUDE_CCRTP

	} else
	{ // smallRTP

		dprintf("%s: !!! start smRTP !!!\n", __FUNCTION__);

		char buf[64];

		strcpy(buf, inet_ntoa(desc->connection.address.IPv4));

		if (media_no == -1)
		{
			int m = 0;

			while (desc->media[m].fmt != payloadTypeUnknown && m < 4)
			{
				eprintf("rtp_func: Creating %d smRTP receiver for %s:%d proto %d fmt %d\n", m, buf, desc->media[m].port, desc->media[m].proto, desc->media[m].fmt);
				if (small_rtp_init(&session->smrtp_session[m], buf, desc->media[m].port, desc->media[m].proto, desc->media[m].fmt, 1, 1) != 0)
				{
					rtp_stop_receiver(session);
					return -1;
				}
				if (small_rtp_start(session->smrtp_session[m], _confirm, (void*)session, delayFeeding) != 0)
				{
					rtp_stop_receiver(session);
					return -1;
				}
				m++;
			}
		} else
		{
			eprintf("rtp_func: Creating one smRTP receiver for %s:%d proto %d fmt %d\n", buf, desc->media[media_no].port, desc->media[media_no].proto, desc->media[media_no].fmt);
			if (small_rtp_init(&session->smrtp_session[0], buf, desc->media[media_no].port, desc->media[media_no].proto, desc->media[media_no].fmt, 0, 0) != 0)
			{
				rtp_stop_receiver(session);
				return -1;
			}
			if (small_rtp_start(session->smrtp_session[0], _confirm, (void*)session, delayFeeding) != 0)
			{
				rtp_stop_receiver(session);
				return -1;
			}
		}
	}

	gettimeofday(&session->last_data_timestamp, 0);

	return 0;
}

int rtp_stop_receiver(rtp_session *session)
{
//#ifndef USE_CCRTP

	session->dvrfd = -1;
	session->dmxfd = -1;

	if ( session->engine == 0 )
	{ // elecard


//#else // #ifndef USE_CCRTP

	} else if ( session->engine == 1 )
	{ // ccRTP
		dprintf("%s: stop receiver\n", __FUNCTION__);
		session->cancel_thread = true;
		mysem_destroy(session->confirm_sem);
		//pthread_kill(session->recv_thread, SIGKILL);
//#endif // #ifndef USE_CCRTP

	} else
	{ // smallRTP
		int m = 0;

		while (session->smrtp_session[m] != NULL)
		{
			eprintf("rtp_func: Stopping smRTP receiver %d\n", m);
			small_rtp_stop(session->smrtp_session[m]);
			small_rtp_destroy(session->smrtp_session[m]);
			session->smrtp_session[m] = NULL;
			m++;
		}
		mysem_destroy(session->confirm_sem);
	}
#ifdef ENABLE_VERIMATRIX
	if (session->vmContext != NULL)
	{
		eprintf("rtp_func: Destroy Verimatrix context\n");
		session->vmEnable = 0;
		VMDestroyContext(session->vmContext);
		session->vmContext = NULL;
	}
#endif
#ifdef ENABLE_SECUREMEDIA
	if (session->smContext != NULL)
	{
		eprintf("rtp_func: Destroy SecureMedia context\n");
		session->smEnable = 0;
		session->smContext = NULL;
	}
#endif
}

void rtp_clear_streams(rtp_session *session)
{
	dprintf("%s: clear streams\n", __FUNCTION__);
	if ( session->pTsStreams != NULL )
	{
		if ( session->pTsStreams->pStream != NULL )
		{
			dfree(session->pTsStreams->pStream);
		}
		dfree(session->pTsStreams);
	}
	session->pTsStreams = NULL;
}

pStreamsPIDs rtp_get_streams(rtp_session *session)
{
	int count = 0;

	dprintf("%s: get streams from demuxer %08X\n", __FUNCTION__, session->hDemuxer);

	while ( session->hDemuxer != NULL )
	{
		count++;
		if ( count > GET_STREAMS_TIMEOUT )
		{
			eprintf("rtp_func: get_streams timed out\n");
			DemuxTS_Close(session->hDemuxer);
			session->hDemuxer = NULL;
			if (session->pTsStreams->ItemsCnt > 0)
			{
				eprintf("rtp_func: Found %d streams in %d/%d programs of TS with timeout\n", session->pTsStreams->ItemsCnt, session->pTsStreams->ProgramCnt, session->pTsStreams->TotalProgramCnt);
				break;
			}
			rtp_clear_streams(session);
			return NULL;
		}
		sleep(1);
	}

	if ( session->pTsStreams->ItemsCnt <= 0 )
	{
		rtp_clear_streams(session);
		return NULL;
	}

	return session->pTsStreams;
}

int rtp_start_output( rtp_session *session, int fd, int dmx )
{
	int count = 0;

	dprintf("%s: wait demuxer %08X\n", __FUNCTION__, session->hDemuxer);

	while ( session->hDemuxer != NULL )
	{
		count++;
		sleep(1);
		dprintf("%s: wait demuxer %d\n", __FUNCTION__, count);

		if ( count > 3 )
		{
			eprintf("rtp_func: demuxer timed out\n");
			return -1;
		}
	}

	dprintf("%s: start receiving\n", __FUNCTION__);

	session->dvrfd = fd;
	session->dmxfd = dmx;

	return 0;
}

int rtp_session_init(struct rtp_session_t **psession)
{
	//dprintf("%s: in\n", __FUNCTION__);

	rtp_session *session = (rtp_session *)dmalloc(sizeof(rtp_session));

	session->dmxfd = 0;
	session->dvrfd = 0;
	session->hDemuxer = NULL;
	session->cancel_thread = 0;
	session->pTsStreams = NULL;
	session->pktcounter = 0;

	rtp_change_eng(session, 0);

#ifdef ENABLE_VERIMATRIX
	eprintf("rtp_func: Reset Verimatrix variables\n");
	session->vmEnable = 0;
	session->vmContext = NULL;
	session->iOperatorId = 1;
    session->iRatingLevel = 10;
#endif
#ifdef ENABLE_SECUREMEDIA
	eprintf("rtp_func: Reset SecureMedia variables\n");
	session->smEnable = 0;
	session->smContext = NULL;
#endif

	*psession = session;

	return 0;
}

void rtp_session_destroy(struct rtp_session_t *session)
{

#ifdef ENABLE_VERIMATRIX
	if (session->vmContext != NULL)
	{
		eprintf("rtp_func: Destroy Verimatrix context\n");
		session->vmEnable = 0;
		VMDestroyContext(session->vmContext);
		session->vmContext = NULL;
	}
#endif

#ifdef ENABLE_SECUREMEDIA
	eprintf("rtp_func: Reset SecureMedia variables\n");
	session->smEnable = 0;
	session->smContext = NULL;
#endif

	dfree(session);
}

int rtp_set_rtcp(struct rtp_session_t *session, unsigned long server_address, int server_port, unsigned long client_address, int client_port)
{
	return small_rtp_set_rtcp(session->smrtp_session[0], server_address, server_port, client_address, client_port);
}

#ifdef ENABLE_SECUREMEDIA
/* ----------------------------------------------------------------------------
 * Called once when module is loaded.
 */
int SmPlugin_Init(const char *args)
{
    SM_RESULT   result = SM_OK;

    SMLI("Initializing SecureMedia plugin\n");

    /* === Integation Point 1 (Set up) Start === */
    SmPs_EnableStorage(SM_FALSE);
    SMC(Sm_Init());
    SMC(SmClientPlugin_Init());
    SMC(SmClientHttpPluginServer_Init());
    /* === Integation Point 1 (Set up) End === */

    SMLI("SecureMedia plugin started\n");

Finally:
    SMLE_ERROR("Failed to initialize SecureMedia plugin");
    return result;
}

/* ----------------------------------------------------------------------------
 * Called once when the module is unloaded.
 * All resources must be released.
 */
void SmPlugin_Finit(void)
{
    /* Put the finalization code here, exit pthreads (if any) free resources and so on. */
    SMLI("Unloading SecureMedia plugin\n");

    /* === Integation Point 3 (Clean up) Start === */
    SmClientPlugin_Finit();
    SmClientHttpPluginServer_Finit();
    Sm_Finit();
    /* === Integation Point 3 (Clean up) End === */
}    
#endif

//////////////////////////////////////////////////////////////////////////
