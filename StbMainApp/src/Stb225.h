
/*
 Stb225.h

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

#ifdef STB225

#if !defined(__STB225_H)
	#define __STB225_H

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include <stdint.h>

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif


/**
 *   @brief Function used to change maping frame buffers to output screen
 *
 *   @param  path          I     Path to device (e.g /dev/fb[0|1]) that will be mapping
 *   @param  dest_x        I     X location of mapping device on output screen
 *   @param  dest_y        I     Y location of mapping device on output screen
 *   @param  dest_width    I     Width of mapping device on output screen
 *   @param  dest_height   I     Height of mapping device on output screen
 *
 *   @retval not 0 if error occure
 *           0 if not.
 */
int32_t Stb225ChangeDestRect(char *path, uint32_t dest_x, uint32_t dest_y, uint32_t dest_width, uint32_t dest_height);

/**
 *   @brief Function used to initialize ir receiver and leds on stb830(stb225)
 *
 *   @retval not 0 if error occure
 *           0 if not.
 */
int32_t Stb225initIR(void);


#ifdef __cplusplus
}
#endif


#endif //#if !defined(__STB225_H)
#endif // STB225
