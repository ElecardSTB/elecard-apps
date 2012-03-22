/*
 TSDemuxGetStreams.cpp

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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_BUF_LEN 187
//#define MAX_BUF_LEN 64*1024

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

int DemuxTS_Open(void **ppInstance);
int DemuxTS_Close(void *ppInstance);
int DemuxTS_Parse(void *ivp_, char *pbBuffer, int iBufferLength, pStreamsPIDs pOutputPIDs);

int main(int argc, char * argv[])
{
	void *hDemuxer;
	FILE *fInput;
	char pbBuf[MAX_BUF_LEN];
	int rez, iBufNum = 0, i;
	pStreamsPIDs pTsStreams = (pStreamsPIDs)malloc(sizeof(StreamsPIDs));

	printf("\n");
	if ( argc!=2 )
	{
		printf("Program shows programs and PIDs of a transport stream\nNeed transport stream file as a parameter\n\n");
		return -1;
	}

	fInput = fopen(argv[1], "rb");
	memset(pTsStreams, 0, sizeof(StreamsPIDs));
	DemuxTS_Open(&hDemuxer);
	while ( !feof(fInput) )
	{
		if ( 0!=(rez=DemuxTS_Parse(hDemuxer, pbBuf, fread(pbBuf, 1, MAX_BUF_LEN, fInput), pTsStreams)) )
		{
			if ( rez != 0xabc )	printf("The error occure in searching transport streams, number %d\n", rez);
			else
			{
				break;
			}
		};
		iBufNum++;
		/*if ( pTsStreams->ItemsCnt )
		{
			printf("Program%d (ID 0x%x), PCR=0x%x\n",pTsStreams->ProgNum,pTsStreams->ProgID, pTsStreams->PCR_PID);
			for ( i=0;i<pTsStreams->ItemsCnt;i++ )
			{
				printf("\tstream_type 0x%x, ID=0x%x\n",pTsStreams->pStream[i].stream_type,pTsStreams->pStream[i].elementary_PID);
			}
			free(pTsStreams->pStream);
			pTsStreams->ItemsCnt = 0;
		}*/
	}   
	DemuxTS_Close(hDemuxer);
	return 0;
}

