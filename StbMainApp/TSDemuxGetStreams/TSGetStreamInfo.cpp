
/*
 TSGetStreamInfo.cpp

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

#define MAX_PROGRAMS			256
#define MAX_STREAMS_IN_PROGRAM	MAX_PROGRAMS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#ifdef _WIN32
	#include <windows.h>
#else
typedef unsigned int    LONGLONG;
typedef unsigned int    DWORD;
typedef unsigned short  WORD;
typedef int             BOOL;
	#if !defined(TRUE) || !defined(FALSE)
		#define TRUE 1
		#define FALSE 0
	#endif

	#if !defined(min) || !defined(max)
		#define min(x,y) (x<y ? x:y)
		#define max(x,y) (x>y ? x:y)
	#endif
#endif	// end not win32 headers and difinitions

typedef struct
{
	int uiStreams;
	int transport_stream_id;
	int version_number;
	int current_next_indicator;
	int section_number;
	int last_section_number;
	struct
	{
		int program_number;
		int program_map_PID;
	} stream[MAX_STREAMS_IN_PROGRAM];
} *pAssociationSection, AssociationSection;

typedef struct
{
	int uiStreams;
	int program_number;
	int version_number;
	int PCR_PID;
	int current_next_indicator;
	struct
	{
		int stream_type;
		int elementary_PID;
	} stream[MAX_STREAMS_IN_PROGRAM];
} *pMapSection, MapSection;

typedef struct
{
	unsigned char*  pStart;
	unsigned char*  pEnd;
	long    Size;
	unsigned char*  p;
} strBufferDTS, *pstrBufferDTS;

typedef struct
{
	int         pid;
	int         started;
	int         ccount;
	unsigned int llCurTime;
} PidInfoStr, *pPidInfoStr;


typedef struct
{
	// Transport Stream and Program
	int             bSyncronized;
	PidInfoStr      pAudioInfo, pVideoInfo;
	unsigned char   *tp_packet;		// current stream poiter
	int             read_index;		// offset in tp_packet
	int             pcrDiscontinuity;

	pstrBufferDTS   pInputBuffer;

	void *vscaner_mem, *ascaner_mem;

	int PidsFound;
	pPidInfoStr pPidsInfo[MAX_PROGRAMS];

	int PAT_PIDS[MAX_PROGRAMS];
	int PAT_NBRS[MAX_PROGRAMS];
	int iPatCnt;
} strInstanceDTS, *pstrInstanceDTS;


#define TS_PACKET_LENGTH 	188

#define TS_SYNC_BYTE 		0x47
#define NULL_PACKETS_PID    0x1FFF
#define DEMUXER_INPUT_BUFFER	1024*100				/* max PES packet length = 65535, should be bigger */

#define CHECK_MARKER_BITS(x, y) if ((x)!=(y)) { printf("ERROR: wrong marker %s %d\n", __FILE__, __LINE__);return 0;}


extern "C" int DemuxTS_Open(void **ppInstance);
extern "C" int DemuxTS_Close(void *ppInstance);
extern "C" int DemuxTS_Parse(void *ivp_, char *pbBuffer, int iBufferLength, pStreamsPIDs pOutputPIDs);

// Allocate memory, initialize
int DemuxTS_Open(void **ppInstance)
{
	strInstanceDTS *ivp = (pstrInstanceDTS)     malloc(sizeof(strInstanceDTS));
	memset(ivp, 0, sizeof(strInstanceDTS));

	//printf("DemuxTS_Open\n");

	ivp->pInputBuffer   = (pstrBufferDTS)       malloc(sizeof(*ivp->pInputBuffer));
	ivp->pInputBuffer->p= ivp->pInputBuffer->pStart = (unsigned char *)malloc(DEMUXER_INPUT_BUFFER);
	ivp->pInputBuffer->Size = 0;
	ivp->pInputBuffer->pEnd = ivp->pInputBuffer->pStart + DEMUXER_INPUT_BUFFER;
	ivp->pVideoInfo.pid = ivp->pAudioInfo.pid =-1;

	*ppInstance = ivp;
	return 0;
}
// free memory
int DemuxTS_Close(void *ppInstance)
{
	//printf("DemuxTS_Close\n");

	pstrInstanceDTS ivp  = (pstrInstanceDTS)ppInstance;
	free(ivp->pInputBuffer->pStart);
	free(ivp->pInputBuffer);
	free(ppInstance);
	return 0;
}

int DemuxTS_AppendData(void *ivp, char *pPkBuffer, int iPkSize)
{
	int size;
	strBufferDTS* pBuffer = ((pstrInstanceDTS)ivp)->pInputBuffer;
	size = pBuffer->pEnd - pBuffer->p - pBuffer->Size;
	if ( size >= iPkSize )
	{
		memcpy(pBuffer->p + pBuffer->Size, pPkBuffer, iPkSize);
		pBuffer->Size += iPkSize;
		return 0;
	} else
	{
		if ( pBuffer->p != pBuffer->pStart )
		{
			memcpy(pBuffer->pStart, pBuffer->p, pBuffer->Size);
			pBuffer->p = pBuffer->pStart;
			return DemuxTS_AppendData(ivp, pPkBuffer, iPkSize);
		}
		printf("ERROR: BUFFER OVERFLOW in DemuxTS_AppendData << err start=0x%x, p=0x%x, end=0x%x, size=%d\n", pBuffer->pStart, pBuffer->p, pBuffer->pEnd, pBuffer->Size);
		return -1;
	}
}

//---------------------------------------------------------------------------
int program_association_section(pstrInstanceDTS ivp, pAssociationSection pPMT)
{
	unsigned char *tp_packet = ivp->tp_packet+ivp->read_index;
	unsigned char *tp_packet_bak = tp_packet;
	int section_length, section_length_bak;
	int program_info_length;
	pPMT->uiStreams = 0;

	tp_packet ++;
	if ( tp_packet[0]!=0 )
	{
		return 0;							// table_id
	}
	CHECK_MARKER_BITS((tp_packet[1]>>6) & 1, 0);				// section_syntax_indicator
	CHECK_MARKER_BITS((tp_packet[1]>>2)&3, 0);					// first 2 bits of section_length
	section_length_bak = section_length = (((tp_packet[1] & 3)<<8) | tp_packet[2]);
	pPMT->transport_stream_id  = (tp_packet[3] <<8) | tp_packet[4];
	pPMT->version_number = (tp_packet[5]>>1) & 0x1f;
	pPMT->current_next_indicator = tp_packet[5] & 1;
	pPMT->section_number = tp_packet[6];						// section_number
	pPMT->last_section_number = tp_packet[7];					// last_section_number
	tp_packet += 8;
	section_length -= 7 + 4;
	if ( section_length>188 )
	{
		printf("ERROR: SECTION LENGTH %d FORBIDDEN\n", section_length);
		return 0;
	}
	while ( section_length>0 )
	{
		if (pPMT->uiStreams < MAX_STREAMS_IN_PROGRAM)
		{
			pPMT->stream[pPMT->uiStreams].program_number = (tp_packet[0] <<8) | tp_packet[1];
			pPMT->stream[pPMT->uiStreams].program_map_PID= ((tp_packet[2]&0x1f)<<8) | tp_packet[3];
	//		printf("Program%d number %d, PID = %d\n", pPMT->uiStreams, pPMT->stream[pPMT->uiStreams].program_number,
//			pPMT->stream[pPMT->uiStreams].program_map_PID);
			pPMT->uiStreams++;
		}
		tp_packet += 4;
		section_length -= 4;
	}
	section_length = section_length_bak;
	if ( tp_packet-tp_packet_bak != section_length )
	{
		printf("Possible ERROR: PAS read %d, section length %d\n", tp_packet-tp_packet_bak, section_length);
		return section_length+4;
	}
	return tp_packet-tp_packet_bak+4;
}



int TS_program_map_section(pstrInstanceDTS ivp, pMapSection pPMT)
{
	unsigned char *tp_packet = ivp->tp_packet+ivp->read_index;
	unsigned char *tp_packet_bak = tp_packet;
	int section_length, section_length_bak;
	int program_info_length;
	int ES_info_length;
	pPMT->uiStreams = 0;
	tp_packet++;

	if ( tp_packet[0]!=2 )
	{
		printf("It's not a PMT\n");
		return 0;							// table_id
	}
	CHECK_MARKER_BITS((tp_packet[1]>>6) & 1, 0);				// section_syntax_indicator
	CHECK_MARKER_BITS((tp_packet[1]>>2) & 3, 0);				// first 2 bits of section_length
	section_length_bak = section_length = ((tp_packet[1] & 3)<<8) | tp_packet[2];
	pPMT->program_number = (tp_packet[3] <<8) | tp_packet[4];
	pPMT->version_number = (tp_packet[5]>>1) & 0x1f;
	pPMT->current_next_indicator = tp_packet[5] & 1;
//	printf("Program %d, version %d\n", pPMT->program_number, pPMT->version_number);
	CHECK_MARKER_BITS(tp_packet[6], 0);							// section_number
	CHECK_MARKER_BITS(tp_packet[7], 0);							// last_section_number
	pPMT->PCR_PID = ((tp_packet[8] & 0x1f)<<5) | tp_packet[9];
	CHECK_MARKER_BITS(tp_packet[10] & 0x0c, 0);				// first 2 bits of program_info_length
	program_info_length = ((tp_packet[10] & 0xf) <<4) | tp_packet[11];
	tp_packet+=program_info_length+12;
	section_length -= 11 + program_info_length + 4;
	if ( section_length>188 )
	{
		printf("ERROR: SECTION LENGTH %d FORBIDDEN\n", section_length);
		return 0;
	}
	while ( section_length>0 )
	{
		ES_info_length = ((tp_packet[3] & 3)<<8) | tp_packet[4];
		if ( tp_packet[0] )
		{
			if (pPMT->uiStreams < MAX_STREAMS_IN_PROGRAM)
			{
				pPMT->stream[pPMT->uiStreams].stream_type       = tp_packet[0];	// MPEG1 or MPEG2
				pPMT->stream[pPMT->uiStreams].elementary_PID    = ((tp_packet[1]&0x1f)<<8) | tp_packet[2];
	//			printf("stream_type = %d  PID = %d\n", pPMT->stream[pPMT->uiStreams].stream_type, pPMT->stream[pPMT->uiStreams].elementary_PID);
				if (tp_packet[0]==0x06) {	// possible value for DVB-T AC3
					pPMT->stream[pPMT->uiStreams].stream_type = 0xff;

					if (ES_info_length) {
						printf("minidmx: Private stream tag (%d)\n",tp_packet[5]);
						if (tp_packet[5]==0x6a || tp_packet[5]==0x7a) {	// Im not sure about 0x7a, but it was mentioned by someone when I searched in google
							pPMT->stream[pPMT->uiStreams].stream_type = 81;
						}
					}
				}
				pPMT->uiStreams++;
			}
			CHECK_MARKER_BITS(tp_packet[3] & 0x0c, 0);			// first 2 bits of ES_info_length
		}
		tp_packet += 5 + ES_info_length;
		section_length -= 5 + ES_info_length;
	}
	section_length = section_length_bak;
	if ( tp_packet-tp_packet_bak != section_length )
	{
		printf("Psssible ERROR: PMT read %d, section length %d\n", tp_packet-tp_packet_bak, section_length);
		return section_length+4;
	}
	return tp_packet-tp_packet_bak+4; // +4 - CRC
}
//////////////////////////////////////////////////////////////////////////////////////////////
////									DemuxTS_Parse									  ////
//////////////////////////////////////////////////////////////////////////////////////////////
//debug = debug
#ifdef _DEBUG
int iBytesRecve = 0;
#endif
int DemuxTS_Parse(void *ivp_, char *pbBuffer, int iBufferLength, pStreamsPIDs pOutputPIDs)
{

#define GOTO_NEXT_PACKET	pBufDTS->p	+=	TS_PACKET_LENGTH;\
							pBufDTS->Size	-=	TS_PACKET_LENGTH;\
							iCurPos	+=	TS_PACKET_LENGTH;

	pstrInstanceDTS ivp = (pstrInstanceDTS)ivp_;
	int iCurPos = 0;
	pstrBufferDTS pBufDTS = ivp->pInputBuffer;
	int     MaxOutStreams = MAX_STREAMS_IN_PROGRAM;
/*
	pOutputPIDs->pStream    = NULL;
	pOutputPIDs->ItemsCnt   = 0;
*/
//	printf("%d = %d\n", pOutputPIDs->pStream, pOutputPIDs->ItemsCnt);

#ifdef _DEBUG
	iBytesRecve += iBufferLength;
#endif
	DemuxTS_AppendData(ivp_, pbBuffer, iBufferLength);
	while ( pBufDTS->Size>3*TS_PACKET_LENGTH || (pBufDTS->p[0]==TS_SYNC_BYTE && ivp->bSyncronized && pBufDTS->Size>=TS_PACKET_LENGTH) )
	{
		if ( pBufDTS->p[0]!=TS_SYNC_BYTE )
		{
			//printf("Missing synch byte ! 0x47 != %d  offset = %d\n", pBufDTS->p[0], pBufDTS->p-pBufDTS->pStart);
			ivp->bSyncronized = 0;
		}
		// if 4 times synch byte found, assume that we have sequence of transport stream packets
		if ( !ivp->bSyncronized )
		{
			while ( pBufDTS->Size>3*TS_PACKET_LENGTH )
			{
				if ( (pBufDTS->p[0]==TS_SYNC_BYTE) &&
					 (pBufDTS->p[TS_PACKET_LENGTH]==TS_SYNC_BYTE) &&
					 (pBufDTS->p[TS_PACKET_LENGTH*2]==TS_SYNC_BYTE) &&
					 (pBufDTS->p[TS_PACKET_LENGTH*3]==TS_SYNC_BYTE) )
				{
					ivp->bSyncronized = 1; break;
				}
				pBufDTS->p++;
				pBufDTS->Size--;
				iCurPos++;
			}
		}
		if ( ivp->bSyncronized )
		{
			int     drop_packet = FALSE;
			int     transport_error_indicator;
			int     payload_unit_start_indicator;
			int     pid;
			int     adaptation_field_control;
			int     continuity_counter;
			int     first_section;
			int     two_sections;
			int     pidType=0;
			int     iTmp, iTmp1, iTmp2, i;
//	PESPacket	tmpPES;
			AssociationSection  pAS;
			MapSection          pPmt;
			PidInfoStr  *pPidInfo;

//	printf("Demux TS Packet offset = %d\n", pBufDTS->p-pBufDTS->pStart);
//	tmpPES.llPTS = 0;
			ivp->tp_packet      = pBufDTS->p;
			ivp->read_index     = 4;	// TS packet header

			// Get the transport packet header
			pid                          = ((pBufDTS->p[1] & 0x1F) << 8) + pBufDTS->p[2];

			transport_error_indicator    = ((int)(pBufDTS->p[1] & 0x80) >> 7);
			payload_unit_start_indicator = ((int)(pBufDTS->p[1] & 0x40) >> 6);
			adaptation_field_control     = ((int)(pBufDTS->p[3] & 0x30) >> 4);
			continuity_counter           = (pBufDTS->p[3] & 0x0f);

#if 0  // Unused
//	int  transport_priority;
//	int  transport_scrambling_control;

//	transport_priority           = ((int)(pBufDTS->p[1] & 0x08) >> 3);
//	transport_scrambling_control = ((int)(pBufDTS->p[3] & 0xc0) >> 6);
#endif
			for ( i=0;i<ivp->PidsFound;i++ )
				if ( pid==ivp->pPidsInfo[i]->pid )
				{
					pPidInfo    = ivp->pPidsInfo[i];
					break;
				}
			if ( i==ivp->PidsFound )
			{
				pPidInfo = ivp->pPidsInfo[i] = (pPidInfoStr)malloc(sizeof(*pPidInfo));
				memset(pPidInfo, 0, sizeof(*pPidInfo));
				pPidInfo->pid = pid;
				ivp->PidsFound++;
			}

			if ( transport_error_indicator )
			{
				printf("ERROR: Recieve error flag in a transport packet\n");
				GOTO_NEXT_PACKET
				continue;
			}
			if ( adaptation_field_control == 0 )
			{
				drop_packet = TRUE;
			}

			if ( pPidInfo->started == TRUE )
			{
				if ( pPidInfo->ccount == continuity_counter )
				{
					if ( (adaptation_field_control > 1) &&
						 (pBufDTS->p[4]      > 0) &&		 // Adapation field length
						 ((pBufDTS->p[5] & 0x80) == 0x80) )	 // Discontinuity_counter set
					{
						//				Discontinuity
						pPidInfo->ccount  = continuity_counter;	 // already have this value
					} else
					{	 //				printf("Duplicate packet counter = %d\n", continuity_counter);
						drop_packet = TRUE;
					}
				} else if ( ((int)(pPidInfo->ccount + 1) % 16) != continuity_counter )
				{
					//printf("Continuity counter mismatch: Expected=%d Got=%d\n", ((int)(pPidInfo->ccount + 1) % 16), continuity_counter);
					pPidInfo->ccount = continuity_counter;
				} else
				{
					pPidInfo->ccount = continuity_counter;	// OK
				}
			} else
			{
//		printf("New TS started %d, first TS packet counter = %d\n", pid, continuity_counter);
				pPidInfo->started = TRUE;
				pPidInfo->ccount  = continuity_counter;
			}

			if ( adaptation_field_control > 1 )
			{
//		getAdaptationField( ivp, pid, pPidInfo );
				iTmp=1+ivp->tp_packet[ivp->read_index];
				if ( iTmp>180 )
				{
					GOTO_NEXT_PACKET
					continue;
				}
				ivp->read_index += iTmp;
			}
			if ( (pid == NULL_PACKETS_PID) || drop_packet )
			{
				GOTO_NEXT_PACKET
				continue;
			}
//	if (ivp->iPatCnt && pid==ivp->PAT_PIDS[0] && !(adaptation_field_control & payload_unit_start_indicator))
//	printf("WARNING: PAT Table Skipped: Adaptation: %d, Playload %d\n", adaptation_field_control, payload_unit_start_indicator);

			if ( adaptation_field_control & 1 )
			{
				if ( payload_unit_start_indicator )
				{
					if ( pid == 0 )
					{
						ivp->read_index += iTmp = program_association_section(ivp, &pAS);
						// add new PID if found
						if ( iTmp )
						{
							for ( iTmp1=0;iTmp1<pAS.uiStreams;iTmp1++ )
							{
								iTmp = 1;
								for ( iTmp2=0;iTmp2<ivp->iPatCnt;iTmp2++ )
									if ( pAS.stream[iTmp1].program_map_PID == ivp->PAT_PIDS[iTmp2] ) iTmp = 0;
								if ( iTmp )
								{
									ivp->PAT_PIDS[ivp->iPatCnt] = pAS.stream[iTmp1].program_map_PID;
									ivp->PAT_NBRS[ivp->iPatCnt] = pAS.stream[iTmp1].program_number;
									ivp->iPatCnt++;
								}
							}
						}
					} else
						if ( ivp->iPatCnt )
					{
						for ( i=0;i<ivp->iPatCnt;i++ )
							if ( pid==ivp->PAT_PIDS[i] )
							{
								break;
							}
						if ( i!=ivp->iPatCnt )
						{
							void *tmpBuf;
							TS_program_map_section(ivp, &pPmt);
							if ( pOutputPIDs->pStream==NULL )
							{
								pOutputPIDs->pStream = (StreamsPIDs::StrmInfo *)malloc(sizeof(*pOutputPIDs->pStream) * MaxOutStreams);
								pOutputPIDs->TotalProgramCnt = pAS.uiStreams;
							}
							if ( pOutputPIDs->ItemsCnt+pPmt.uiStreams>=MaxOutStreams )
							{
								MaxOutStreams <<= 1;
								tmpBuf = malloc(sizeof(*pOutputPIDs->pStream) * MaxOutStreams);
								memcpy(tmpBuf, pOutputPIDs->pStream, sizeof(*pOutputPIDs->pStream) * pOutputPIDs->ItemsCnt);
								free(pOutputPIDs->pStream);
								pOutputPIDs->pStream = (StreamsPIDs::StrmInfo *)tmpBuf;
								pOutputPIDs->TotalProgramCnt = pAS.uiStreams;
							}

							{
								int idx1;
								int found;

								found = 0;
								for (idx1=0; idx1<pOutputPIDs->ItemsCnt; idx1++)
								{
									if (pOutputPIDs->pStream[idx1].ProgID			== ivp->PAT_PIDS[i])
									{
										if(pOutputPIDs->ItemsCnt == 0
												|| (pOutputPIDs->ItemsCnt == pPmt.uiStreams))
										{
											found = 1;
											break;
										}
									}
								}

								if (!found)
								{
									pOutputPIDs->ProgramCnt++;

									pOutputPIDs->ItemsCnt	=	0;

									for ( iTmp1=0; iTmp1<pPmt.uiStreams; iTmp1++ )
									{
										pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].PCR_PID		= pPmt.PCR_PID;
										pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].ProgID      = ivp->PAT_PIDS[i];
										pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].ProgNum     = ivp->PAT_NBRS[i];
										pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].elementary_PID = pPmt.stream[iTmp1].elementary_PID;
										pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].stream_type = pPmt.stream[iTmp1].stream_type;
										/*printf("%03d (%03d/%03d):\t%08X > %08X (%08X):\t%08X (%8X)\n",
												iTmp1, pOutputPIDs->ItemsCnt, pOutputPIDs->ProgramCnt,
												pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].ProgID,
												pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].ProgNum,
												pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].PCR_PID,
												pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].elementary_PID,
												pOutputPIDs->pStream[pOutputPIDs->ItemsCnt].stream_type);*/
										pOutputPIDs->ItemsCnt++;
									}
								}
							}
						}
					}
				}
			}

			GOTO_NEXT_PACKET;
//	printf("Decoded TS packet, new ptr: 0x%x. (offset=%d)\n", pBufDTS->p, iCurPos);
		}			// if was sunc byte (TS packet end
	}			// while not eof input buffer
	return 0;
}			//	func
/*

#define BUFFER_SIZE		4*1024

int main (int argc, char *argv[]) {
	FILE *fInput = fopen(argv[1],"rb");
	int iRead;
	char pbBuffer[BUFFER_SIZE];
	void *pDmxInst;
	StreamsPIDs OutputPIDs;
	DemuxTS_Open(&pDmxInst);
	do {
		iRead = fread(pbBuffer, 1, BUFFER_SIZE, fInput);
		DemuxTS_Parse(pDmxInst, pbBuffer, iRead, &OutputPIDs);
	} while (iRead=BUFFER_SIZE);
	DemuxTS_Close(pDmxInst);
	return 0;
}
*/
