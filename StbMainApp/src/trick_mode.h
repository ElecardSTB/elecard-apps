#if !defined(__TRICK_MODE_H)
#define __TRICK_MODE_H

/*
 trick_mode.h

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

#ifdef STB82

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "app_info.h"

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

/**
*   @brief Function used to stop trick mode playback
*
*   @retval void
*/
extern void trickMode_stop(void);

/**
*   @brief Function used to start trick mode playback
*
*   @param directory    I    Path to directory where the PVR files are held
*   @param pFileIn      I/O  Pointer to current input file
*   @param pWhichIndex  I/O  Pointer to PVR file index
*   @param pOffset      I/O  Pointer to offset within the current PVR file
*   @param fileOut      I    Output file (demux)
*   @param direction    I    Play direction
*   @param speed        I    Play speed
*   @param videoPid     I    The PID for the video stream
*
*   @retval void
*/
extern int trickMode_play(char* directory, int *pFileIn, int *pWhichIndex, int *pOffset, int fileOut,
                          stb810_trickModeDirection direction, stb810_trickModeSpeed speed, int videoPid);

/**
*   @brief Function used to change the speed of a trick mode
*
*   @param speed        I    Play speed
*
*   @retval void
*/
extern void trickMode_setSpeed(stb810_trickModeSpeed speed);

#endif // STB82

#endif /* __TRICK_MODE_H      Do not add any thing below this line */
