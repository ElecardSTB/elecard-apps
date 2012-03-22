#if !defined(__RTP_FUNC_H)
#define __RTP_FUNC_H

/*
 rtp_func.h

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


#include <sdp.h>

#define RTP_MAX_STREAM_COUNT (256)
#define RTP_SDP_TTL          (7000)

struct rtp_list
{
	int count;
	sdp_desc items[RTP_MAX_STREAM_COUNT];
};

typedef struct
{
	int ItemsCnt;
	int ProgramCnt;
	int TotalProgramCnt;
	struct StrmInfo
	{
		int ProgNum;
		int ProgID;
		int PCR_PID;
		int stream_type;
		int elementary_PID;
	} *pStream;
} StreamsPIDs, *pStreamsPIDs;

struct rtp_session_t;

#ifdef __cplusplus
extern "C" {
#endif
void rtp_change_eng(struct rtp_session_t *session, int eng);
void rtp_get_last_data_timestamp(struct rtp_session_t *session, struct timeval *ts);
int rtp_engine_supports_transport(int eng, int transport);
void rtp_flush_receive_buffers(struct rtp_session_t *session);
int rtp_start_output(struct rtp_session_t *session, int fd, int dmx);
pStreamsPIDs rtp_get_streams(struct rtp_session_t *session);
void rtp_clear_streams(struct rtp_session_t *session);
int rtp_start_receiver(struct rtp_session_t *session, sdp_desc *desc, int media_no, int delayFeeding, int verimatrix);
int rtp_stop_receiver(struct rtp_session_t *session);
int rtp_sdp_start_collecting(struct rtp_session_t *session);
int rtp_sdp_set_collecting_state(struct rtp_session_t *session, int startFlag);
int rtp_sdp_stop_collecting(struct rtp_session_t *session);
int rtp_get_found_streams(struct rtp_session_t *session, struct rtp_list *found_streams);
int rtp_session_init(struct rtp_session_t **psession);
void rtp_session_destroy(struct rtp_session_t *psession);
int rtp_set_rtcp(struct rtp_session_t *session, unsigned long server_address, int server_port, unsigned long client_address, int client_port);
#ifdef ENABLE_VERIMATRIX
int rtp_enable_verimatrix(struct rtp_session_t *session, char *inipath);
#endif
#ifdef ENABLE_SECUREMEDIA
int rtp_enable_securemedia(struct rtp_session_t *session);
int SmPlugin_Init(const char *args);
void SmPlugin_Finit(void);
#endif
#ifdef __cplusplus
}
#endif

#endif //__RTP_FUNC_H
