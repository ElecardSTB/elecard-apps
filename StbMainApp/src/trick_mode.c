
/*
 trick_mode.c

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

#ifdef STB82

#include "trick_mode.h"

#include "debug.h"
#include "app_info.h"
#include "StbMainApp.h"

#include <phStbMpegTsTrickMode.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define MAX_ERROR_RETRIES       (100)

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static int* gpCurrentFileIndex;
static char* gpCurrentDirectory;
static int gOutputFile;

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

static int trickMode_endOfFileBackwards(int file, int* pNewFile, int userData)
{
    char filename[PATH_MAX];
    int index;
    int retVal = 0;

    (void)userData;

    close(file);

    index = (*gpCurrentFileIndex)-1;
    if(index >= 0)
    {
        sprintf(filename, "%s/part%02d.spts", gpCurrentDirectory, index);

        *pNewFile = open(filename, O_RDONLY);
        if(*pNewFile<0) 
        {
            dprintf("%s: Unable to open next file backwards\n", __FUNCTION__);
            retVal = -1;
            goto FuncReturn;
        }
        else
        {
            dprintf("%s: Opened next file backwards %s\n", __FUNCTION__, filename);
            *gpCurrentFileIndex = index;
        }
    }
    else
    {
        retVal = -1;
        goto FuncReturn;
    }

FuncReturn:
    return retVal;
}

static int trickMode_endOfFileForwards(int file, int* pNewFile, int userData)
{
    char filename[PATH_MAX];
    int retVal = 0;

    (void)userData;

    close(file);

    sprintf(filename, "%s/part%02d.spts", gpCurrentDirectory, ((*gpCurrentFileIndex)+1));

    *pNewFile = open(filename, O_RDONLY);
    if(*pNewFile<0) 
    {
        dprintf("%s: Unable to open next file forwards\n", __FUNCTION__);
        retVal = -1;
        goto FuncReturn;
    }
    else
    {
        dprintf("%s: Opened next file forwards %s\n", __FUNCTION__, filename);
        (*gpCurrentFileIndex)++;
    }

FuncReturn:
    return retVal;
}

static int trickMode_dataAvailable(unsigned char* pData, int size, int userData)
{
    int totalBytesWritten = 0;
    int bytesWritten = 0;
    int bytesToWrite = 0;

    dprintf("%s: pData:%p size:%d userData:%d to output:%d\n", __FUNCTION__,
        pData, size, userData, gOutputFile);

    (void)userData;
    bytesToWrite = size;
    do
    {
        bytesWritten = write(gOutputFile, &pData[totalBytesWritten], bytesToWrite);
        if(bytesWritten > 0)
        {
            totalBytesWritten += bytesWritten;
        }
        else
        {
            return -1;
        }
        bytesToWrite -= bytesWritten;
    }
    while(totalBytesWritten < size);

    dprintf("%s: done\n", __FUNCTION__);

    return 0;
}

void trickMode_stop(void)
{
    dprintf("%s: in\n", __FUNCTION__);
    phStbMpegTsTrickMode_Stop();
    dprintf("%s: out\n", __FUNCTION__);
}

int trickMode_play(char* directory, int *pFileIn, int *pWhichIndex, int *pOffset, int fileOut,
                   stb810_trickModeDirection direction, stb810_trickModeSpeed speed, int videoPid)
{
    int retValue;
    phStbMpegTsTrickMode_EndOfFileCb_t endOfFileCb;
    int complete = 0;
    int numErrorRetries = 0;
    int errorOffset = 0;

    dprintf("%s: speed:%d fileIndex:%d\n", __FUNCTION__, speed, *pWhichIndex);

    /* Assign the globals. */
    gpCurrentFileIndex = pWhichIndex;
    gOutputFile = fileOut;
    gpCurrentDirectory = directory;

    if(direction == direction_forwards)
    {
        endOfFileCb = trickMode_endOfFileForwards;
    }
    else
    {
        endOfFileCb = trickMode_endOfFileBackwards;
    }

    while(!complete)
    {
        retValue = phStbMpegTsTrickMode_Play(pFileIn,
                                             pOffset,
                                             (phStbMpegTsTrickMode_Direction_t)direction,
                                             (phStbMpegTsTrickMode_Speed_t)speed,
                                             videoPid,
                                             trickMode_dataAvailable,
                                             endOfFileCb,
                                             0);
        if(retValue == -2)
        {
            if(errorOffset == *pOffset)
            {
                numErrorRetries++;
            }
            else
            {
                numErrorRetries = 0;
            }
            errorOffset = *pOffset;
            if(numErrorRetries <= MAX_ERROR_RETRIES)
            {
                char filename[PATH_MAX];

                close(*pFileIn);
                sprintf(filename, "%s/part%02d.spts", gpCurrentDirectory, *gpCurrentFileIndex);
                *pFileIn = open(filename, O_RDONLY);
                if(*pFileIn < 0)
                {
                    /* Just return. */
                    complete = 1;
                }
                else
                {
                    int offset;
                    
                    /* Seek to the current position +- 1 TS 188 byte packet. */
                    if(direction == direction_forwards)
                    {
                        (*pOffset) += 188;
                    }
                    else
                    {
                        (*pOffset) -= 188;
                        if(*pOffset < 0)
                        {
                            *pOffset = 0;
                        }
                    }
                    offset = (int)lseek(*pFileIn, *pOffset, SEEK_SET);
                    eprintf("Trick Mode: Trick Mode data error, reopening %s at %d and retry\n",
                        filename, *pOffset);
                }
            }
            else
            {
                eprintf("Trick Mode: Trick Mode hit maximum retries, exiting\n");
                complete = 1;
            }
        }
        else
        {
            complete = 1;
        }
    }

    dprintf("%s: returned:%d offset:%d fileIndex:%d\n", __FUNCTION__, retValue, *pOffset, *pWhichIndex);
    return retValue;
}

void trickMode_setSpeed(stb810_trickModeSpeed speed)
{
    dprintf("%s: speed:%d\n", __FUNCTION__, speed);
    phStbMpegTsTrickMode_SetSpeed((phStbMpegTsTrickMode_Speed_t)speed);
    dprintf("%s: done\n", __FUNCTION__);
}

#endif // #ifdef STB82
