/*
 src/dvb-fe.h

Copyright (C) 2014  Elecard Devices
Anton Sergeev <Anton.Sergeev@elecard.ru>

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

#if !(defined __DVB_FE_H__)
#define __DVB_FE_H__
/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include <stdint.h>

#include <service.h>
#include "frontend.h"
#include "dvb_types.h"
#include "helper.h"

/******************************************************************
* EXPORTED MACROS                              [for headers only] *
*******************************************************************/
#define MIN_FREQUENCY_KHZ	(  40000)
#define MAX_FREQUENCY_KHZ	( 860000)
#define FREQUENCY_STEP_KHZ	(   8000)
#define KHZ					(   1000)
#define MHZ					(1000000)

/******************************************************************
* EXPORTED TYPEDEFS                            [for headers only] *
*******************************************************************/
typedef struct {
	uint32_t	lowFrequency;
	uint32_t	highFrequency;
	uint32_t	frequencyStep;
	int32_t		inversion;
} stb810_dvbfeInfo;

typedef struct {
	stb810_dvbfeInfo	fe;
	fe_bandwidth_t		bandwidth;
	uint8_t				plp_id;
	uint8_t				generation;
} stb810_dvbtInfo;

typedef struct {
	stb810_dvbfeInfo	fe;
	fe_modulation_t		modulation;
	uint32_t			symbolRate;
} stb810_dvbcInfo;

typedef struct {
	stb810_dvbfeInfo	fe;
	fe_modulation_t		modulation;
} stb810_atscInfo;

typedef enum {
	dvbsBandK = 0,
	dvbsBandC,
} stb810_dvbsBand_t;

typedef enum {
	diseqcSwitchNone = 0,
	diseqcSwitchSimple,
	diseqcSwitchMulti,
	diseqcSwitchTypeCount,
} diseqcSwitchType_t;

#define DISEQC_SWITCH_NAMES { "NONE", "Simple", "Multi" }

typedef struct {
	diseqcSwitchType_t	type;
	uint8_t	port;       // 0..3
	uint8_t	uncommited; // 0 - off, 1..16
} stb810_diseqcInfo_t;

typedef struct  {
	// NB: DVB-S treat this values as MHz
	stb810_dvbfeInfo	c_band;
	stb810_dvbfeInfo	k_band;
	uint32_t			symbolRate;
	fe_sec_voltage_t	polarization;
	stb810_dvbsBand_t	band;
	stb810_diseqcInfo_t	diseqc;
} stb810_dvbsInfo;

typedef struct  {
	fe_status_t  fe_status;
	uint32_t     ber;
	uint32_t     uncorrected_blocks;
	uint16_t     signal_strength;
	uint16_t     snr;
} tunerState_t;
	
typedef int32_t dvbfe_cancelFunctionDef(void);

/******************************************************************
* EXPORTED DATA                                                   *
*******************************************************************/
extern const table_IntStr_t fe_modulationName[];

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

int32_t dvbfe_hasTuner(uint32_t adapter);

int32_t dvbfe_open(uint32_t adapter);
int32_t dvbfe_close(uint32_t adapter);
int32_t dvbfe_getFrontendInfo(uint32_t adapter, struct dvb_frontend_info *fe_info);
int32_t dvbfe_getDelSysCount(uint32_t adapter);
int32_t dvbfe_toggleType(uint32_t adapter);
int32_t dvbfe_checkDelSysSupport(uint32_t adapter, fe_delivery_system_t delSys);

int32_t dvbfe_updateMediaToCurentState(uint32_t adapter, EIT_media_config_t *media);
int32_t dvbfe_updateMediaToNextStream(uint32_t adapter, EIT_media_config_t *media);
int32_t dvbfe_isMultistreamMux(uint32_t adapter, EIT_media_config_t *media);

const char *dvbfe_getTunerTypeName(uint32_t adapter);


fe_delivery_system_t dvbfe_getType(uint32_t adapter);

int32_t dvbfe_isLinuxAdapter(uint32_t adapter);

static inline uint32_t dvbfe_frequencyKHz(uint32_t adapter, uint32_t frequency)
{
	return (dvbfe_getType(adapter) == SYS_DVBS) ? frequency : (frequency / KHZ);
}

int32_t dvbfe_checkFrequency(fe_delivery_system_t type, uint32_t frequency, uint32_t adapter,
								__u32* ber, __u32 fe_step_size, EIT_media_config_t *media,
								dvbfe_cancelFunctionDef* pFunction);
int32_t dvbfe_setParam(uint32_t adapter, int32_t wait_for_lock,
						EIT_media_config_t *media, dvbfe_cancelFunctionDef* pFunction);

int32_t dvbfe_fillMediaConfig(uint32_t adapter, uint32_t frequency, EIT_media_config_t *media);

/**  @ingroup dvb
 *   @brief For the given adapter returns the Frontend's freq. limits
 *
 *   @param[in]  adapter   Tuner adapter to use
 *   @param[out] low_freq  Initial search frequency
 *   @param[out] high_freq Final search frequency
 *   @param[out] freq_step Frequency step size
 *
 *   @return 0 or -1 if failed
 */
int32_t dvbfe_getTuner_freqs(uint32_t adapter, __u32 * low_freq, __u32 * high_freq, __u32 * freq_step);

/**  @ingroup dvb_instance
 *   @brief Get signal info from specified adapter
 *
 *   @param[in]  adapter    Adapter tuner to be used
 *   @param[out] state
 *
 *   @return 0 on success
 */
int32_t dvbfe_getSignalInfo(uint32_t adapter, tunerState_t *state);

int32_t dvbfe_setFrontendType(uint32_t adapter, fe_delivery_system_t type);


int32_t dvbfe_trackerStart(uint32_t adapter);
int32_t dvbfe_trackerStop(uint32_t adapter);


int32_t dvbfe_init(void);
int32_t dvbfe_terminate(void);


#ifdef __cplusplus
}
#endif

#endif //#if !(defined __DVB_FE_H__)
