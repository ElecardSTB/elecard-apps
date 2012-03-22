#ifndef __DOWNLOADER_H
#define __DOWNLOADER_H

/*
 downloader.h

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

#include <stdio.h>

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

/** Callback to be executed after download.
  First param is index of download in pool, second is user data specified in downloader_push
*/
typedef void (*downloadCallback)(int,void*);

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

void downloader_init();

/**
 * @brief Download a file in blocking mode. Downloaded files saved by default to /tmp/XXXXXXXX/<filename>.
 *
 * @param[in]   url
 * @param[in]   timeout   Download timeout in seconds
 * @param[out]  filename  Pointer to buffer to store downloaded file name. If buffer is initiated with some string, it will be used as filename as is (no autodetection)
 * @param[in]   fn_size   Size of filename buffer
 * @param[in]   quota     If greater than 0, downloaded file size will be limited to specified value
 *
 * @retval int	0 - success, non-zero - failure
 */
int  downloader_get(const char* url, int timeout, char *filename, size_t fn_size, size_t quota);

/**
 * @brief Remove downloaded file and temporal directory.
 *
 * @param[out] file Path to downloaded file. Will be modified during cleanup!
 */
void downloader_cleanupTempFile(char *file);

/**
 * @brief Check URL for being in download pool.
 *
 * @param[in]  url
 *
 * @retval int  Index of download in pool, -1 if not found
 */
int downloader_find(const char *url);

/**
 * @brief Add new URL to download pool. Not checking for same URL in pool.
 *
 * @param[in]   url
 * @param[out]  filename   Pointer to buffer to store downloaded file name. If buffer is initiated with some string, it will be used as filename as is (no autodetection).
 * @param[in]   fn_size    Size of filename buffer
 * @param[in]   quota      If greater than 0, downloaded file size will be limited to specified value
 * @param[in]   pCallback  Callback function to be called after download. Callback function may (and should) free buffers for URL and filename.
 * @param[in]   pArg       User data to be passed to callback function
 *
 * @retval int Index of download in pool, -1 on error
 */
int  downloader_push(const char *url, char *filename,  size_t fn_size, size_t quota, downloadCallback pCallback, void *pArg );

/**
 * @brief Terminate all downloads and free data.
 */
void downloader_cleanup();

/**
 * @brief Get download info from pool.
 *
 * @param[in]   index     Index of download in pool
 * @param[out]  url       URL of downloading file. Should not be modified.
 * @param[out]  filename  Pointer to filename buffer
 * @param[out]  fn_size   Size of filename buffer
 * @param[out]  quota     File size limit
 *
 * @retval int	0 - success, non-zero - failure
 */
int  downloader_getInfo( int index, char **url, char **filename, size_t *fn_size, size_t *quota);

#ifdef __cplusplus
}
#endif

#endif // __DOWNLOADER_H
