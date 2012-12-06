#if !defined(__PVR_H)
#define __PVR_H

/*
 pvr.h

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

#include "defines.h"

#ifdef ENABLE_PVR

#include "interface.h"

// NETLib
#include <service.h>

#include <StbPvr.h>

#include <time.h>

/*******************
* EXPORTED MACROS  *
********************/

#define PVR_MENU_ICON_WIDTH    (22)
#define PVR_MENU_ICON_HEIGHT   (22)
#define PVR_CELL_BORDER_WIDTH  (1)
#define PVR_CELL_BORDER_HEIGHT (1)

/* Table colors */

#define PVR_CELL_BORDER_RED   175
#define PVR_CELL_BORDER_GREEN 175
#define PVR_CELL_BORDER_BLUE  175
#define PVR_CELL_BORDER_ALPHA 0x88

#if (defined STBPNX) || (defined STSDK)
#define STBPVR
#endif

#define PVR_RES_ADDED     (0)
#define PVR_RES_COLLISION (1)
#define PVR_RES_MATCH     (2)

/*******************
* EXPORTED DATA    *
********************/

extern interfaceListMenu_t PvrMenu;
extern int pvr_jobCount;
extern list_element_t *pvr_jobs;

/********************************
* EXPORTED FUNCTIONS PROTOTYPES *
*********************************/

void pvr_init(void);

void pvr_cleanup(void);

/**
*   @brief Build the Pvr menu data structures
*
*   @retval void
*/
void pvr_buildPvrMenu(interfaceMenu_t* pParent);

/**
*   @brief Init the Pvr menu for specified recording mode
*
*   @param pMenu I
*   @param pArg  I Pvr recording mode of type pvrJobType_t casted to void*
*
*   @retval void
*/
int  pvr_initPvrMenu(interfaceMenu_t *pMenu, void *pArg);

/**
*   @brief Function used to stop Pvr playback on a given screen
* 
*   @param  which   I       Screen to be used
*
*   @retval void
*/
void pvr_stopPlayback(int which);

/**
*   @brief Function used to record the currently displayed channel
*
*   @retval void
*/
void pvr_recordNow(void);

/**
*   @brief Function used to start recording given stream
*
*   @param  which I  Screen to be used
*   @param  url   I  Stream URL
*   @param  desc  I  Optional description of recording stream
*
*   @retval int 0 - success, non-zero otherwise
*/
int  pvr_record(int which, char *url, char *desc );

/**
*   @brief Function used to add recording job for given stream
*
*   @param  which I  Screen to be used
*   @param  url   I  Stream URL
*   @param  desc  I  Optional description of recording stream
*
*   @retval int 0 - success, non-zero otherwise
*/
int  pvr_manageRecord(int which, char *url, char *desc );

#ifdef ENABLE_DVB
/**
*   @brief Start simultanious playback and recording of a DVB channel on a given screen
* 
*   @param  which       I   Screen to be used
*   @param  fromStart   I   Flag indicating if playback is from start of file
*
*   @retval void
*/
void pvr_startPlaybackDVB(int which);

/** Function used to determine status of playback of recording DVB stream
 *  @return 1 if STB is playing DVB stream which is recording right now, 0 otherwise
 */
int  pvr_isPlayingDVB(int which);

/** Function used to determine presence of DVB recordings
 *  @return 1 if STB is recording DVB now or has scheduled DVB records, 0 otherwise
 */
int  pvr_hasDVBRecords(void);

/** Cancel current and all scheduled DVB records and update job list
 */
void pvr_purgeDVBRecords(void);

/**
*   @brief Function used to stop Pvr recording from a given tuner
*
*   @param  which   I       Tuner to be used
*
*   @retval void
*/
void pvr_stopRecordingDVB(int which);

int  pvr_isRecordingDVB(void);

void pvr_toogleRecordingDVB(void);
#endif // ENABLE_DVB

int  pvr_showStopPvr( interfaceMenu_t *pMenu, void* pArg );

int  pvr_isRecordingRTP(void);

void pvr_stopRecordingRTP(int which);

int  pvr_isRecordingHTTP(void);

void pvr_stopRecordingHTTP(int which);

void pvr_toggleRecording(void);

/**
*   @brief Function used to find existing job or insert new job with given parameters
*
*   @param  job          I
*   @param  jobListEntry O Pointer to added or found job as list element
*
*   @retval int          PVR_RES_ADDED     No existing job found, new job added
*                        PVR_RES_COLLISION Collision found
*                        PVR_RES_MATCH     Existing job with same settings found
*                        -1                NULL jobListEntry pointer
*/
int  pvr_findOrInsertJob(pvrJob_t *job, list_element_t **jobListEntry);

void pvr_storagesChanged(void);

int  pvr_getActive(void);

/** Print string describing record job to a given buffer
 * @param[out] buf      Should be 24 bytes at minimum
 * @param[in]  buf_size
 * @param[in]  job
 * @return Count of printed characters (including leading zero) or -1 on insufficient buffer capacity
 */
ssize_t pvr_jobprint(char *buf, size_t buf_size, pvrJob_t *job);

int  pvr_deleteJob(list_element_t* job);

int  pvr_clearJobList(void);

int  pvr_exportJobList(void);

int  pvr_importJobList(void);

int  pvr_updateSettings(void);

#endif /* ENABLE_PVR */

#endif /* __PVR_H      Do not add any thing below this line */
