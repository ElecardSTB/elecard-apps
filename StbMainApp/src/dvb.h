#if !defined(__DVB_H)
#define __DVB_H

/*
 dvb.h

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

/** @file dvb.h DVB playback backend
 */
/** @defgroup dvb DVB features
 *  @ingroup StbMainApp
 *  In DVB service and channels are synonymous and freely interchanged
 */
/** @defgroup dvb_instance DVB instance features
 *  @ingroup dvb
 */
/** @defgroup dvb_service DVB service features
 *  @ingroup dvb
 */

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "defines.h"

#include "app_info.h"
#include "dvb_types.h"

#include <platform.h>
#include <sdp.h>
#include <service.h>

#ifdef ENABLE_DVB

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define ADAPTER_COUNT      (2)

#define DVBC FE_QAM
#define DVBT FE_OFDM
#define DVBS FE_QPSK

#define MIN_FREQUENCY_KHZ  (  40000)
#define MAX_FREQUENCY_KHZ  ( 860000)
#define FREQUENCY_STEP_KHZ (   8000)
#define KHZ                (   1000)
#define MHZ                (1000000)

#define SCALE_FACTOR       (10000)

#ifdef STBPNX
#define SIGNAL_MASK        (0xff)
#define MAX_SIGNAL         (180)
#define AVG_SIGNAL         (150)
#define BAD_SIGNAL         (70)
#else
#define SIGNAL_MASK        (0xffff)
#define MAX_SIGNAL         (0xffff)
#define AVG_SIGNAL         (0x7fff)
#define BAD_SIGNAL         (0x4fff)
#endif

#define MAX_BER            (50000)
#define BAD_BER            (20000)

#define MAX_UNC            (100)
#define BAD_UNC            (0)

#define BAD_CC             (0)

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef int dvb_displayFunctionDef(uint32_t, int, tunerFormat, int, int);

typedef int dvb_cancelFunctionDef(void);

typedef struct dvb_filePosition {
	int index;
	int offset;
} DvbFilePosition_t;

typedef struct __dvb_playParam {
	int audioPid;
	int videoPid;
	int textPid;
	int pcrPid;
	DvbFilePosition_t position;
} DvbPlayParam_t;

typedef struct __dvb_liveParam {
	int channelIndex;
	int audioIndex;
} DvbLiveParam_t;

#ifdef ENABLE_MULTI_VIEW
typedef struct __dvb_multiParam {
	uint16_t channels[4];
} DvbMultiParam_t;
#endif

/** @ingroup dvb_instance
 * DVB watch settings used to initialize tuner for specified frequency and PID filters
 */
typedef struct __dvb_param {
	DvbMode_t mode;
	int adapter;
	char *directory;
	union 
	{
		DvbLiveParam_t liveParam;
		DvbPlayParam_t playParam;
#ifdef ENABLE_MULTI_VIEW
		DvbMultiParam_t multiParam;
#endif
	} param;
	EIT_media_config_t *media;
	__u32 frequency;
} DvbParam_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

extern list_element_t *dvb_services;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/

/**  @ingroup dvb
 *   @brief Function used to initialise the DVB component
 *
 *   Must be called before initiating offair
 */
void dvb_init(void);

/**  @ingroup dvb
 *   @brief Function used to terminate the DVB component
 *
 *   Must be called after terminating offair
 */
void dvb_terminate(void);

/**  @ingroup dvb
 *   @brief Returns the fe_type of the FrontEnd device
 *
 *   @param[in]  tuner               Tuner to use
 *
 *   @return fe_type or -1 if failed
 */
static inline fe_type_t dvb_getType(tunerFormat tuner)
{
	return appControlInfo.tunerInfo[tuner].type;
}

const char *dvb_getTypeName(tunerFormat tuner);

int dvb_toggleType(tunerFormat tuner);

/**  @ingroup dvb
 *   @brief For the given tuner returns the Frontend's freq. limits
 *
 *   @param[out] low_freq  Initial search frequency
 *   @param[out] high_freq Final search frequency
 *   @param[out] freq_step Frequency step size
 *   @param[in]  tuner     Tuner to use
 *
 *   @return fe_type or -1 if failed
 */
int dvb_getTuner_freqs(tunerFormat tuner, __u32 * low_freq, __u32 * high_freq, __u32 * freq_step);

/**  @ingroup dvb_instance
 *   @brief Function used to start DVB decoding of a given channel
 *
 *   @param[in]  pParam    DVB startup parameters
 *
 *   @return 0 on success
 *   @sa dvb_stopDVB()
 */
int dvb_startDVB(DvbParam_t *pParam);

/**  @ingroup dvb_instance
 *   @brief Function used to stop DVB decoding from a given tuner
 *
 *   @param[in]  adapter   DVB adapter to be used
 *   @param[in]  reset     Flag to indicate if the tuner will be put into sleep mode
 *
 *   @sa dvb_startDVB()
 */
void dvb_stopDVB(int adapter, int force);

/**  @ingroup dvb_instance
 *   @brief Change audio PID of currently running channel
 *
 *   @param[in]  adapter  DVB adapter to be used
 *   @param[in]  aPID     New audio PID to be used
 *
 *   @return 0 on success
 */
int dvb_changeAudioPid(tunerFormat tuner, uint16_t aPID);

/**  @ingroup dvb_instance
 *   @brief Get signal info from specified tuner
 *
 *   @param[in]  tuner     Tuner to be used
 *   @param[out] snr
 *   @param[out] signal
 *   @param[out] ber
 *   @param[out] uncorrected_blocks
 *
 *   @return 0 on success
 */
int dvb_getSignalInfo(tunerFormat tuner,
                      uint16_t *snr, uint16_t *signal, uint32_t *ber, uint32_t *uncorrected_blocks);

/**  @ingroup dvb_instance
 *   @ingroup dvb_service
 *   @brief Function used to scan for DVB-T channels
 *
 *   @param[in]  tuner     Tuner to use
 *   @param[in]  pFunction Callback function
 *
 *   @return 0 on success
 */
int dvb_serviceScan( tunerFormat tuner, dvb_displayFunctionDef* pFunction);

/**  @ingroup dvb_instance
 *   @ingroup dvb_service
 *   @brief Function used to scan for DVBT channels on custom frequency
 *
 *   @param[in]  tuner             Tuner to use
 *   @param[in]  frequency         Frequency to scan
 *   @param[in]  media             Tune settings
 *   @param[in]  pFunction         Callback function to display progress and check for user cancel
 *   @param[in]  scan_network      NIT table to scan
 *   @param[in]  save_service_list Allow saving channel list to permanent storage
 *   @param[in]  pCancelFunction   Function used to cancel scanning during frequency check
 *
 *   @return 0 on success
 */
int dvb_frequencyScan( tunerFormat tuner, __u32 frequency,
                       EIT_media_config_t *media, 
                       dvb_displayFunctionDef* pFunction,
                       NIT_table_t *scan_network,
                       int save_service_list,
                       dvb_cancelFunctionDef* pCancelFunction);

/**  @ingroup dvb_instance
 *   @ingroup dvb_service
 *   @brief Function used to scan for EPG during playback
 *
 *   @param[in]  tuner             Tuner to use
 *   @param[in]  frequency         Frequency to scan
 */
void dvb_scanForEPG( tunerFormat tuner, uint32_t frequency );

/**  @ingroup dvb_instance
 *   @ingroup dvb_service
 *   @brief Function used to scan for PAT+PMT during playback
 *   @param[in]  tuner             Tuner to use
 *   @param[in]  frequency         Frequency to scan
 *   @param[out] out_list          Service list to store PAT+PMT data
 */
void dvb_scanForPSI( tunerFormat tuner, uint32_t frequency, list_element_t **out_list );

/**  @ingroup dvb_service
 *   @brief Function used to return the number of available DVB channels
 *
 *   @return Number of available DVB channels
 */
int dvb_getNumberOfServices(void);

/**  @ingroup dvb_service
 *   @brief Get channel at specified index
 *
 *   @param[in]  which     Channel index
 *
 *   @return NULL if index is out of range
 */
EIT_service_t* dvb_getService(int which);

/**  @ingroup dvb_service
 *   @brief Get channel name at specified index
 *
 *   @param[in]  which     Channel index
 *
 *   @return NULL if service is out of range, translated N/A if name is not specified
 */
char *dvb_getTempServiceName(int index);

/**  @ingroup dvb_service
 *   @brief Get index of specified channel
 *
 *   @param[in]  service   Channel to be used
 *
 *   @return Channel index starting from 0 or -1 if service not found
 */
int dvb_getServiceIndex( EIT_service_t* service);

/**  @ingroup dvb_service
 *   @brief Function used to return the name of a given DVB channel
 *
 *   @param[in]  service   Channel to be used
 *
 *   @return Channel name or NULL if not available
 */
char* dvb_getServiceName(EIT_service_t *service);

/**  @ingroup dvb_service
 *   @brief Function used to get textual description of a given DVB channel
 *
 *   @param[in]  service   Channel to be used
 *   @param[out] buf       Channel description
 *
 *   @return 0 on success
 */
int dvb_getServiceDescription(EIT_service_t *service, char* buf);

/**  @ingroup dvb_service
 *   @brief Function used to return the service ID of a given DVB channel
 *
 *   @param[in]  service   Service ID to be returned
 *
 *   @return Service ID or -1 if not available
 */
int dvb_getServiceID(EIT_service_t *service);

/**  @ingroup dvb_service
 *   @brief Function used to get the service frequency of a given DVB channel
 *
 *   @param[in]  service     Channel to be used
 *   @param[out] pFrequency  Channel frequency
 *
 *   @return 0 on success
 */
int dvb_getServiceFrequency(EIT_service_t *service, __u32* pFrequency);

/**  @ingroup dvb_service
 *   @brief Function used to return the scrambled state of a given DVB channel
 *
 *   @param[in]  service   Channel to be checked
 *
 *   @return 1 if scrambled
 */
int dvb_getScrambled(EIT_service_t *service);

/**  @ingroup dvb_service
 *   @brief Function used to get the pseudo url of DVB channel
 *
 *   @param[in]  service   Channel to be used
 *   @param[out] URL       DVB channel's pseudo URL
 *
 *   @return 1 if scrambled
 */
int dvb_getServiceURL(EIT_service_t *service, char* URL);

/**  @ingroup dvb_service
 *   @brief Function used to return video and audio PIDs for the given DVB channel
 *
 *   @param[in]  service   Channel to be used
 *   @param[in]  audio     Audio track number
 *   @param[out] pVideo    Video PID for channel
 *   @param[out] pAudio    Audio PID for channel
 *   @param[out] pText     Teletext PID for channel
 *   @param[out] pPcr      Pcr PID for channel
 *
 *   @return 0 on success
 */
int dvb_getPIDs(EIT_service_t *service, int audio, uint16_t* pVideo, uint16_t* pAudio, uint16_t* pText, uint16_t* pPcr);

/**  @ingroup dvb_service
 *   @brief Function checks stream has specified payload type
 *
 *   @param[in]  serivce   Channel to be used
 *   @param[in]  p_type    Payload type to check
 *
 *   @return 1 if found
 *   @sa dvb_hasPayloadTypeNB()
 *   @sa dvb_hasMediaType()
 */
int dvb_hasPayloadType(EIT_service_t *service, payload_type p_type);

/**  @ingroup dvb_service
 *   @brief Function checks stream has specified media type (video or audio)
 *
 *   @param[in]  serivce   Channel to be used
 *   @param[in]  m_type    Media type to check
 *
 *   @return 1 if found
 *   @sa dvb_hasMedia()
 *   @sa dvb_hasMediaTypeNB()
 */
int dvb_hasMediaType(EIT_service_t *service, media_type m_type);

/**  @ingroup dvb_service
 *   @brief Function checks stream has any supported media
 *
 *   @param[in]  serivce   Channel to be used
 *
 *   @return 1 if found
 *   @sa dvb_hasMediaType()
 */
int dvb_hasMedia(EIT_service_t *service);

/**  @ingroup dvb_service
 *   @brief Function used to get number of audio tracks of a given channel
 *   @param[in]  serivce   Channel to be used
 *   @return Audio track count or -1 on error
 */
int dvb_getAudioCount(EIT_service_t *service);

/**  @ingroup dvb_service
 *   @brief Function used to get audio PID of specified track of a given channel
 *   @param[in]  serivce   Channel to be used
 *   @param[in]  audio     Audio track number
 *   @return Audio track PID or -1 on error
 */
uint16_t dvb_getAudioPid(EIT_service_t *service, int audio);

/**  @ingroup dvb_service
 *   @brief Function used to get audio type of specified track of a given channel
 *   @param[in]  serivce   Channel to be used
 *   @param[in]  audio     Audio track number
 *   @return Audio track type or 0 on error
 */
PMT_stream_type_t dvb_getAudioType(EIT_service_t *service, int audio);

PMT_stream_type_t dvb_getVideoType(EIT_service_t *service);

/**  @ingroup dvb_service
 *   @brief Function used to get name of event with specified ID from a given channel
 *   @param[in]  serivce   Channel to be used
 *   @param[in]  event_id  Event ID
 *   @return NULL if event not found
 */
char* dvb_getEventName( EIT_service_t* service, uint16_t event_id );

/**  @ingroup dvb_service
 *   @brief Cleanup NIT structure
 */
void dvb_clearNIT(NIT_table_t *nit);

/**  @ingroup dvb_service
 *   @brief Function exports current channel list to specified file
 *   @param[in]  filename  Channel file name
 */
void dvb_exportServiceList(char* filename);

/**  @ingroup dvb_service
 *   @brief Function imports current channel list from specified file
 *   @param[in]  filename  Channel file name
 *   @return 0 on success
 */
int dvb_readServicesFromDump(char* filename);

/**  @ingroup dvb_service
 *   @brief Function cleans current channel list
 *   @param[in]  permanent Save changes to disk
 */
void dvb_clearServiceList(int permanent);

/** @defgroup dvb_pvr DVB PVR features
 *  @ingroup dvb
 *  @deprecated PVR functions moved to StbPvr project
 *  @{
 */

#ifdef LINUX_DVB_API_DEMUX
/** Function used to obtain the length of the given PVR stream
 *   @param[in]  which     PVR stream being played
 *   @param[out] pPosition Pointer to variable to hold length
 */
void dvb_getPvrLength(int which, DvbFilePosition_t *pPosition);

/** Function used to obtain the current playback position of the given PVR stream
 *   @param[in]  which     PVR stream being played
 *   @param[out] pPosition Pointer to variable to hold current position
 */
void dvb_getPvrPosition(int which, DvbFilePosition_t *pPosition);

/** Function used to return the bit rate for the given pvr playback
 *   @param[in]  which     PVR playback session
 *   @return Bit rate in bytes per second
 */
int dvb_getPvrRate(int which);
#endif // LINUX_DVB_API_DEMUX

static inline int dvb_getAdapter(tunerFormat tuner)
{
	return appControlInfo.tunerInfo[tuner].adapter;
}

static inline int dvb_isLinuxTuner(tunerFormat tuner)
{
	return dvb_getAdapter(tuner) < ADAPTER_COUNT;
}

/** @} */

#endif /* ENABLE_DVB */

#endif /* __DVB_H      Do not add any thing below this line */
