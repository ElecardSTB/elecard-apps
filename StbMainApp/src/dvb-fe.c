/*
 src/dvb-fe.c

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

#ifdef ENABLE_DVB
/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <common.h>

#include "dvb-fe.h"
#include "sem.h"
#include "off_air.h"
#include "stsdk.h"
#include "helper.h"

/******************************************************************
* LOCAL MACROS                                                    *
*******************************************************************/
#define ioctl_loop(adapter, err, fd, request, ...) \
	do { \
		err = ioctl(fd, request, ##__VA_ARGS__); \
		if(err < 0) { \
			dprintf("%s[%d]: ioctl " #request " failed\n", __func__, adapter); \
		} \
	} while(err < 0)

#define ioctl_or_abort(adapter, fd, request, ...) \
	do { \
		int32_t ret = ioctl(fd, request, ##__VA_ARGS__); \
		if(ret < 0) { \
			eprintf("%s[%d]: ioctl " #request " failed\n", __func__, adapter); \
			return ret; \
		} \
	} while(0)

#define SLEEP_QUANTUM                  50000 /*us*/

#define TUNER_C_BAND_START           3000000 /*kHZ*/
#define TUNER_C_BAND_END             4200000 /*kHZ*/
#define TUNER_KU_LOW_BAND_START     10700000 /*kHZ*/
#define TUNER_KU_LOW_BAND_END       11700000 /*kHZ*/
#define TUNER_KU_HIGH_BAND_START    11700001 /*kHZ*/
#define TUNER_KU_HIGH_BAND_END      12750000 /*kHZ*/

#define SET_DTV_PRPERTY(prop, id, _cmd, val) \
		{ \
			prop[id].cmd = _cmd; \
			prop[id].u.data = val; \
			id++; \
 		}

// For front end signal tracking
// sec.s  between FE checks
#define TRACK_INTERVAL (2)


/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
typedef struct {
	fe_delivery_system_t    curDelSys;
	uint32_t                forceSetDelSys;

	uint32_t                frequency;
	union {
		stb810_dvbtInfo     dvbtInfo;
		stb810_dvbcInfo     dvbcInfo;
		stb810_dvbsInfo     dvbsInfo;
		stb810_atscInfo     atscInfo;
	};

	tunerState_t            fe_state;
} dvbfe_adapterState_t;

typedef enum {
	eTunerDriver_none = 0,
	eTunerDriver_linuxDVBapi,
	eTunerDriver_STAPISDK,
	eTunerDriver_streamerInput,
} stb_adapterDriverType_e;

/* DVB adapter information */
typedef struct {
	stb_adapterDriverType_e	driverType;
//	stb_adapterStatus		status;

	uint8_t					supportedDelSysCount;
	fe_delivery_system_t	supportedDelSys[8];

	dvbfe_adapterState_t	state;

	pthread_t				fe_tracker_Thread;
	int32_t					fd;
} stb_adapterInfo_t;

/******************************************************************
* EXPORTED DATA                                [for headers only] *
*******************************************************************/
static const table_IntStr_t delivery_system_desc[] = {
	{SYS_UNDEFINED,		"SYS_UNDEFINED"},
	{SYS_DVBC_ANNEX_A,	"SYS_DVBC_ANNEX_A"},//DVB-C
	{SYS_DVBC_ANNEX_B,	"SYS_DVBC_ANNEX_B"},
	{SYS_DVBT,			"SYS_DVBT"},
	{SYS_DSS,			"SYS_DSS"},
	{SYS_DVBS,			"SYS_DVBS"},
	{SYS_DVBS2,			"SYS_DVBS2"},
	{SYS_DVBH,			"SYS_DVBH"},
	{SYS_ISDBT,			"SYS_ISDBT"},
	{SYS_ISDBS,			"SYS_ISDBS"},
	{SYS_ISDBC,			"SYS_ISDBC"},
	{SYS_ATSC,			"SYS_ATSC"},
	{SYS_ATSCMH,		"SYS_ATSCMH"},
	{SYS_DMBTH,			"SYS_DMBTH"},
	{SYS_CMMB,			"SYS_CMMB"},
	{SYS_DAB,			"SYS_DAB"},
	{SYS_DVBT2,			"SYS_DVBT2"},
	{SYS_TURBO,			"SYS_TURBO"},
	{SYS_DVBC_ANNEX_C,	"SYS_DVBC_ANNEX_C"},

	TABLE_INT_STR_END_VALUE
};

const table_IntStr_t fe_modulationName[] = {
	{QPSK,		"QPSK"},     //  0
	{QAM_16,	"QAM_16"},   //  1
	{QAM_32,	"QAM_32"},   //  2
	{QAM_64,	"QAM_64"},   //  3
	{QAM_128,	"QAM_128"},  //  4
	{QAM_256,	"QAM_256"},  //  5
	{QAM_AUTO,	"QAM_AUTO"}, //  6
	{VSB_8,		"VSB_8"},    //  7
	{VSB_16,	"VSB_16"},   //  8
	{PSK_8,		"PSK_8"},    //  9
	{APSK_16,	"APSK_16"},  // 10
	{APSK_32,	"APSK_32"},  // 11
	{DQPSK,		"DQPSK"},    // 12
	{QAM_4_NR,	"QAM_4_NR"}, // 13

	TABLE_INT_STR_END_VALUE,
};

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static pmysem_t				dvbfe_semaphore;
static stb_adapterInfo_t	g_adapterInfo[MAX_ADAPTER_SUPPORTED];

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int32_t dvbfe_diseqcSend(uint32_t adapter, const uint8_t *tx, size_t tx_len);
static int32_t dvbfe_diseqcSetup(uint32_t adapter, uint32_t frequency, EIT_media_config_t *media);

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>_<Word>+    *
*******************************************************************/
int32_t dvbfe_hasTuner(uint32_t adapter)
{
	if((adapter < MAX_ADAPTER_SUPPORTED) && (g_adapterInfo[adapter].driverType != eTunerDriver_none)) {
		return 1;
	}
	return 0;
}

int32_t dvbfe_isLinuxAdapter(uint32_t adapter)
{
	if((adapter < MAX_ADAPTER_SUPPORTED) && (g_adapterInfo[adapter].driverType == eTunerDriver_linuxDVBapi)) {
		return 1;
	}
	return 0;
}

/**  @ingroup dvb
 *   @brief Returns the fe_type of the FrontEnd device
 *
 *   @param[in]  adapter               Tuner to use
 *
 *   @return fe_type or -1 if failed
 */
fe_delivery_system_t dvbfe_getType(uint32_t adapter)
{
	return g_adapterInfo[adapter].state.curDelSys;
}

static fe_delivery_system_t dvbfe_getDelSysFromMedia(EIT_media_config_t *media)
{
	fe_delivery_system_t typeAfter = SYS_UNDEFINED;
	serviceMediaType_t typeBefore;
	uint8_t modulation;

	if(media == NULL) {
		eprintf("%s: wrong arguments!\n", __FUNCTION__);
		return typeAfter;
	}

	typeBefore = media->type;
	switch(typeBefore) {
		case serviceMediaDVBT:
			if(media->dvb_t.generation == 2) {
				typeAfter = SYS_DVBT2;
			} else {
				typeAfter = SYS_DVBT;
			}
			break;
		case serviceMediaDVBC:
			typeAfter = SYS_DVBC_ANNEX_AC;
			break;
		case serviceMediaDVBS:
			typeAfter = SYS_DVBS;
			break;
		case serviceMediaATSC:
			typeAfter = SYS_ATSC;
			modulation = media->atsc.modulation;
			if((modulation == QAM_64) || (modulation == QAM_256)) {
				typeAfter = SYS_DVBC_ANNEX_B;
			}
			break;
//		case serviceMediaMulticast:	typeAfter = ; break;
		default:
			eprintf("%s: Undefined type: %s\n", __FUNCTION__, typeBefore);
	}
//	printf("%s[%d]: typeBefore=%d, typeAfter=%d!!!\n", __FILE__, __LINE__, typeBefore, typeAfter);
	return typeAfter;
}


static int32_t dvbfe_updateCurrentDelSys(uint32_t adapter)
{
	if(adapter >= MAX_ADAPTER_SUPPORTED) {
		return -1;
	}

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi:
			if(g_adapterInfo[adapter].fd >= 0) {
				struct dtv_property p = { .cmd = DTV_DELIVERY_SYSTEM, };
				struct dtv_properties cmdseq = { .num = 1, .props = &p, };
				if(ioctl(g_adapterInfo[adapter].fd, FE_GET_PROPERTY, &cmdseq) == 0) {
					g_adapterInfo[adapter].state.curDelSys = p.u.data;
				}
			}
			break;
		case eTunerDriver_STAPISDK:
		case eTunerDriver_streamerInput:
		default:
			break;
	}

	return 0;
}

int32_t dvbfe_fillMediaConfig(uint32_t adapter, uint32_t frequency, EIT_media_config_t *media)
{
	fe_delivery_system_t curDelSys = dvbfe_getType(adapter);
	if(media == NULL) {
		return -1;
	}

	memset(media, 0, sizeof(EIT_media_config_t));
	switch(curDelSys) {
		case SYS_DVBT:
		case SYS_DVBT2:
			media->type = serviceMediaDVBT;
			media->dvb_t.centre_frequency = frequency;
			media->dvb_t.inversion = appControlInfo.dvbtInfo.fe.inversion;
			media->dvb_t.bandwidth = appControlInfo.dvbtInfo.bandwidth;
			media->dvb_t.plp_id = 0;
			media->dvb_t.generation = (curDelSys == SYS_DVBT2) ? 2 : 1;
			break;
		case SYS_DVBC_ANNEX_AC:
			media->type = serviceMediaDVBC;
			media->dvb_c.frequency = frequency;
			media->dvb_c.inversion = appControlInfo.dvbcInfo.fe.inversion;
			media->dvb_c.symbol_rate = appControlInfo.dvbcInfo.symbolRate * 1000;
			media->dvb_c.modulation = appControlInfo.dvbcInfo.modulation;
			break;
		case SYS_DVBS:
			media->type = serviceMediaDVBS;
			media->dvb_s.frequency = frequency;
			//media->dvb_s.orbital_position
			//media->dvb_s.west_east_flag
			media->dvb_s.polarization = appControlInfo.dvbsInfo.polarization;
			//media->dvb_s.modulation
			media->dvb_s.symbol_rate = appControlInfo.dvbsInfo.symbolRate * 1000;
			//media->dvb_s.FEC_inner
			//media->dvb_s.inversion
			break;
		case SYS_ATSC:
		case SYS_DVBC_ANNEX_B:
			media->type = serviceMediaATSC;
			media->atsc.frequency = frequency;
			media->atsc.modulation = appControlInfo.atscInfo.modulation;
			break;
		default:
			media->type = serviceMediaNone;
			break;
	}

	dprintf("%s(): media type %s, freq %u\n", __func__, table_IntStrLookup(delivery_system_desc, curDelSys, "unknown "), media->frequency);
	return 0;
}

int32_t dvbfe_getDelSysCount(uint32_t adapter)
{
	return g_adapterInfo[adapter].supportedDelSysCount;
}

int32_t dvbfe_checkDelSysSupport(uint32_t adapter, fe_delivery_system_t delSys)
{
	int32_t i;
	for(i = 0; i < g_adapterInfo[adapter].supportedDelSysCount; i++) {
		if(g_adapterInfo[adapter].supportedDelSys[i] == delSys) {
			return 1;
		}
	}
	return 0;
}

const char *dvbfe_getTunerTypeName(uint32_t adapter)
{
	const table_IntStr_t fe_typeNameHuman[] = {
		{SYS_DVBS,			"DVB-S"},
		{SYS_DVBS2,			"DVB-S2"},
		{SYS_DVBC_ANNEX_AC,	"DVB-C"},
		{SYS_DVBT,			"DVB-T"},
		{SYS_DVBT2,			"DVB-T2"},
		{SYS_ATSC,			"ATSC"},
		{SYS_DVBC_ANNEX_B,	"ATSC"},

		TABLE_INT_STR_END_VALUE,
	};
	fe_delivery_system_t curDelSys = dvbfe_getType(adapter);

	if(((curDelSys == SYS_DVBT) || (curDelSys == SYS_DVBT2)) &&
		dvbfe_checkDelSysSupport(adapter, SYS_DVBT2) && dvbfe_checkDelSysSupport(adapter, SYS_DVBT)) {
		return "DVB-T/T2";
	}
	return table_IntStrLookup(fe_typeNameHuman, curDelSys, "DVB");
}

int32_t dvbfe_toggleType(uint32_t adapter)
{
	fe_delivery_system_t cur_type = dvbfe_getType(adapter);
	fe_delivery_system_t next_type;
	uint32_t i;

	for(i = 0; i < g_adapterInfo[adapter].supportedDelSysCount; i++) {
		if(g_adapterInfo[adapter].supportedDelSys[i] == cur_type) {
			break;
		}
	}
	if(i == g_adapterInfo[adapter].supportedDelSysCount) {
		return -1;
	}

	while(1) {
		i = (i + 1) % g_adapterInfo[adapter].supportedDelSysCount;
		next_type = g_adapterInfo[adapter].supportedDelSys[i];
		if(next_type == cur_type) {
			return 0;
		}

		// DVBT2 is supports, but don't process, because we need toggle between DVBT and DVBC type only.
		if(next_type != SYS_DVBT2) {
			break;
		}
	}

	return dvbfe_setFrontendType(adapter, next_type);
}

int32_t dvbfe_getTuner_freqs(uint32_t adapter, uint32_t * low_freq, uint32_t * high_freq, uint32_t * freq_step)
{
	switch(dvbfe_getType(adapter)) {
		case SYS_DVBT:
		case SYS_DVBT2:
			*low_freq  = (appControlInfo.dvbtInfo.fe.lowFrequency  * KHZ);
			*high_freq = (appControlInfo.dvbtInfo.fe.highFrequency * KHZ);
			*freq_step = (appControlInfo.dvbtInfo.fe.frequencyStep * KHZ);
			break;
		case SYS_DVBC_ANNEX_AC:
			*low_freq  = (appControlInfo.dvbcInfo.fe.lowFrequency  * KHZ);
			*high_freq = (appControlInfo.dvbcInfo.fe.highFrequency * KHZ);
			*freq_step = (appControlInfo.dvbcInfo.fe.frequencyStep * KHZ);
			break;
		case SYS_DVBS:
		case SYS_DVBS2:
			if(appControlInfo.dvbsInfo.band == dvbsBandC) {
				*low_freq  = (appControlInfo.dvbsInfo.c_band.lowFrequency  * KHZ);
				*high_freq = (appControlInfo.dvbsInfo.c_band.highFrequency * KHZ);
				*freq_step = (appControlInfo.dvbsInfo.c_band.frequencyStep * KHZ);
			} else {
				*low_freq  = (appControlInfo.dvbsInfo.k_band.lowFrequency  * KHZ);
				*high_freq = (appControlInfo.dvbsInfo.k_band.highFrequency * KHZ);
				*freq_step = (appControlInfo.dvbsInfo.k_band.frequencyStep * KHZ);
			}
			break;
		case SYS_ATSC:
		case SYS_DVBC_ANNEX_B:
			*low_freq  = (appControlInfo.atscInfo.fe.lowFrequency * KHZ);
			*high_freq = (appControlInfo.atscInfo.fe.highFrequency * KHZ);
			*freq_step = (appControlInfo.atscInfo.fe.frequencyStep * KHZ);
			break;
		default :
			break;
	}

	return 0;
}

int32_t dvbfe_getFrontendInfo(uint32_t adapter, struct dvb_frontend_info *fe_info)
{
	int32_t err;

	if(g_adapterInfo[adapter].fd < 0) {
		eprintf("%s[%d]: failed to open frontend\n", __func__, adapter);
		return -1;
	}
	ioctl_loop(adapter, err, g_adapterInfo[adapter].fd, FE_GET_INFO, fe_info);

	return 0;
}

// Low nibble of 4th (data) DiSEqC command byte
static inline uint8_t diseqc_data_lo(int satellite_position, int is_vertical, uint32_t f_khz)
{
	return (satellite_position & 0x03) << 2 | (is_vertical ? 0 : 1) << 1 | (f_khz > 11700000);
}

static int32_t dvbfe_diseqcSend(uint32_t adapter, const uint8_t* tx, size_t tx_len)
{
	struct dvb_diseqc_master_cmd cmd;
	size_t i;

	dprintf("%s: sending %d:\n", __func__, tx_len);
	for(i = 0; i < tx_len; i++) {
		dprintf(" 0x%02x", tx[i]);
	}
	dprintf("\n");

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi:
			cmd.msg_len = tx_len;
			memcpy(cmd.msg, tx, cmd.msg_len);
			ioctl_or_abort(adapter, g_adapterInfo[adapter].fd, FE_DISEQC_SEND_MASTER_CMD, &cmd);
			break;
		case eTunerDriver_STAPISDK:
			st_sendDiseqc(adapter, tx, tx_len);
			break;
		default:
			break;
	}

	return 0;
}

static int32_t dvbfe_diseqcSetup(uint32_t adapter, uint32_t frequency, EIT_media_config_t *media)
{
	if((dvbfe_getType(adapter) != SYS_DVBS) || (appControlInfo.dvbsInfo.diseqc.type == 0)) {
		return 0;
	}
	if(appControlInfo.dvbsInfo.diseqc.uncommited) {
		uint8_t ucmd[4] = { 0xe0, 0x10, 0x39, appControlInfo.dvbsInfo.diseqc.uncommited-1 };
		dvbfe_diseqcSend(adapter, ucmd, 4);
	}
	int32_t is_vertical = media ? media->dvb_s.polarization == 0x01 : appControlInfo.dvbsInfo.polarization != 0;
	int32_t port = appControlInfo.dvbsInfo.diseqc.type == diseqcSwitchMulti ?
					appControlInfo.dvbsInfo.diseqc.port & 1 :
					appControlInfo.dvbsInfo.diseqc.port;
	uint8_t data_hi = appControlInfo.dvbsInfo.diseqc.type == diseqcSwitchMulti ? 0x70 : 0xF0;
	uint8_t cmd[4] = { 0xe0, 0x10, 0x38, data_hi | diseqc_data_lo(port, is_vertical, frequency) };

	dvbfe_diseqcSend(adapter, cmd, 4);
	return 0;
}

static int32_t dvbfe_openLinuxDVBtuner(uint32_t adapter)
{
	char frontend_devname[32];
	int32_t frontend_fd = -1;
	snprintf(frontend_devname, sizeof(frontend_devname), "/dev/dvb/adapter%d/frontend0", adapter);

	dprintf("%s(): adapter%d/frontend%d\n", __func__, adapter, 0);
	frontend_fd = open(frontend_devname, O_RDWR);
	if(frontend_fd < 0) {
		snprintf(frontend_devname, sizeof(frontend_devname), "/dev/dvb%d.frontend%d", adapter, 0);
		frontend_fd = open(frontend_devname, O_RDWR);
		if(frontend_fd < 0) {
			eprintf("%s: failed to open adapter %d\n", __func__, adapter);
		}
	}
	return frontend_fd;
}

static int32_t dvbfe_scanLinuxDVBTuner(uint32_t adapter)
{
	int32_t fd;
	int32_t err;
	uint8_t i;
	struct dtv_properties		dtv_prop;
	struct dvb_frontend_info	info;
	fe_delivery_system_t		current_sys;
	struct dtv_property			dvbfe_prop[DTV_MAX_COMMAND];

	fd = dvbfe_openLinuxDVBtuner(adapter);
	if(fd < 0) {
	    return -1;
	}
	err = ioctl(fd, FE_GET_INFO, &info);
	if(err < 0) {
		eprintf("%s[%d]: ioctl FE_GET_INFO failed\n", __func__, adapter);
		close(fd);
		return -1;
	}
	dvbfe_prop[0].cmd = DTV_API_VERSION;
	dvbfe_prop[1].cmd = DTV_DELIVERY_SYSTEM;

	dtv_prop.num = 2;
	dtv_prop.props = dvbfe_prop;

	/* Detect a DVBv3 device */
	if(ioctl(fd, FE_GET_PROPERTY, &dtv_prop) == -1) {
		dvbfe_prop[0].u.data = 0x300;
		dvbfe_prop[1].u.data = SYS_UNDEFINED;
	}

	appControlInfo.dvbApiVersion = dvbfe_prop[0].u.data;
	current_sys = dvbfe_prop[1].u.data;

	eprintf("DVB API Version %d.%d, Current tuner %d delSys: %s\n",
		appControlInfo.dvbApiVersion / 256, appControlInfo.dvbApiVersion % 256,
		adapter, table_IntStrLookup(delivery_system_desc, current_sys, "unknown"));

	if (appControlInfo.dvbApiVersion < 0x505) {
		if(strstr(info.name, "CXD2820R") ||
			strstr(info.name, "MN88472"))
		{
			g_adapterInfo[adapter].supportedDelSysCount = 3;
			g_adapterInfo[adapter].supportedDelSys[0] = SYS_DVBC_ANNEX_AC;
			g_adapterInfo[adapter].supportedDelSys[1] = SYS_DVBT;
			g_adapterInfo[adapter].supportedDelSys[2] = SYS_DVBT2;
		} else {
			const table_IntInt_t type_to_delsys[] = {
				{FE_OFDM,	SYS_DVBT},
//				{FE_OFDM,	SYS_DVBT2},
				{FE_QAM,	SYS_DVBC_ANNEX_AC},
				{FE_QPSK,	SYS_DVBS},
				{FE_ATSC,	SYS_ATSC},
//				{FE_ATSC,	SYS_DVBC_ANNEX_B},

				TABLE_INT_INT_END_VALUE,
			};

			g_adapterInfo[adapter].supportedDelSysCount = 1;
			g_adapterInfo[adapter].supportedDelSys[0] = table_IntIntLookup(type_to_delsys, info.type, SYS_DVBT);
		}
	} else {
#ifdef DTV_ENUM_DELSYS
		dvbfe_prop[0].cmd = DTV_ENUM_DELSYS;
		dtv_prop.num = 1;
		dtv_prop.props = dvbfe_prop;
		if (ioctl(fd, FE_GET_PROPERTY, &dtv_prop) == -1) {
			close(fd);
			return -1;
		}
		g_adapterInfo[adapter].supportedDelSysCount = dvbfe_prop[0].u.buffer.len;

		if(g_adapterInfo[adapter].supportedDelSysCount == 0) {
			close(fd);
			return -1;
		}
		for(i = 0; i < g_adapterInfo[adapter].supportedDelSysCount; i++) {
			g_adapterInfo[adapter].supportedDelSys[i] = dvbfe_prop[0].u.buffer.data[i];
		}

#endif
	}

	g_adapterInfo[adapter].state.curDelSys = current_sys;
	g_adapterInfo[adapter].driverType = eTunerDriver_linuxDVBapi;
//	dvbInstances[adapter].fe_type = current_sys;

	eprintf("%s(): Adapter=%d tuner, Model=%s, FineStepSize=%u. Supported types (%d):\n\t",
				__func__, adapter, info.name, info.frequency_stepsize, g_adapterInfo[adapter].supportedDelSysCount);
	for(i = 0; i < g_adapterInfo[adapter].supportedDelSysCount; i++) {
		printf("%s ", table_IntStrLookup(delivery_system_desc, g_adapterInfo[adapter].supportedDelSys[i], "unknown"));
	}
	printf("\n");

	close(fd);
	return 0;
}

static int32_t dvbfe_scanSTAPISDKTuners()
{
#if (defined STSDK)
	elcdRpcType_t type = elcdRpcInvalid;
	cJSON *result = NULL;
	if(st_rpcSync(elcmd_dvbtuners, NULL, &type, &result) != 0 || type != elcdRpcResult || result == NULL) {
		cJSON_Delete(result);
		return -1;
	}

	if(result->type == cJSON_Array) {
		uint32_t adapter;

		for(adapter = 0; adapter < MAX_ADAPTER_SUPPORTED; adapter++) {
			cJSON *t = cJSON_GetArrayItem(result, adapter);

			if(!t) {
				break;
			}
			if(dvbfe_hasTuner(adapter)) {//adapter busy
				continue;
			}
			dprintf("%s: adapter %d\n", __func__, adapter);

			if(t->type == cJSON_String) {
				fe_delivery_system_t type;
				if(strcasecmp(t->valuestring, "DVB-T") == 0) {
					type = SYS_DVBT;
				} else if(strcasecmp(t->valuestring, "DVB-S") == 0) {
					type = SYS_DVBS;
				} else if(strcasecmp(t->valuestring, "DVB-C") == 0) {
					type = SYS_DVBC_ANNEX_AC;
				} else {
					continue;
				}

				g_adapterInfo[adapter].supportedDelSys[0] = type;
				g_adapterInfo[adapter].supportedDelSysCount = 1;
				g_adapterInfo[adapter].state.curDelSys = type;
				g_adapterInfo[adapter].driverType = eTunerDriver_STAPISDK;
				eprintf("%s(): Adapter=%d tuner, Supported types: %s\n", __func__, adapter,
						table_IntStrLookup(delivery_system_desc, g_adapterInfo[adapter].supportedDelSys[0], "unknown"));
			}
		}
	}
	cJSON_Delete(result);
#endif //#if (defined STSDK)
	return 0;
}

static uint32_t dvbfe_getBandwidthHz(fe_bandwidth_t bw)
{
	table_IntInt_t bands[] = {
		{BANDWIDTH_8_MHZ, 8000000},
		{BANDWIDTH_7_MHZ, 7000000},
		{BANDWIDTH_6_MHZ, 6000000},
		{BANDWIDTH_5_MHZ, 5000000},
		{BANDWIDTH_10_MHZ, 10000000},
		{BANDWIDTH_1_712_MHZ, 1712000},
		{BANDWIDTH_AUTO, 0},
		TABLE_INT_INT_END_VALUE
	};

	return table_IntIntLookup(bands, bw, 0);
}


static int32_t dvbfe_setParamLinuxDVBapi(uint32_t adapter, fe_delivery_system_t delSys, uint32_t frequency, EIT_media_config_t *media)
{
	uint32_t propCount = 0;
	struct dtv_property dtv[16];//check if it enough
	struct dtv_properties cmdseq;

	if(appControlInfo.dvbCommonInfo.streamerInput) {
		return 0;
	}
	if(g_adapterInfo[adapter].fd < 0) {
		eprintf("%s(): ERROR: Adapter=%d not opened!!!\n", __func__, adapter);
		return -1;
	}

	if(media && media->type == serviceMediaNone) {
		eprintf("%s(): ERROR: Adapter=%d bad params!!!\n", __func__, adapter);
		return -1;
	}

	if(((delSys == SYS_DVBT) || (delSys == SYS_DVBT2)) && (media->type == serviceMediaDVBT)) {
		fe_bandwidth_t bandwidth = media->dvb_t.bandwidth;
		uint32_t bandwidthHz = dvbfe_getBandwidthHz(bandwidth);
		uint32_t inversion = appControlInfo.dvbtInfo.fe.inversion;

		if(bandwidthHz == 0) {
			bandwidthHz = 8000000;
		}
		SET_DTV_PRPERTY(dtv, propCount, DTV_FREQUENCY, frequency);
		SET_DTV_PRPERTY(dtv, propCount, DTV_INVERSION, inversion);
		SET_DTV_PRPERTY(dtv, propCount, DTV_BANDWIDTH_HZ, bandwidthHz ? bandwidthHz : 8000000);
		SET_DTV_PRPERTY(dtv, propCount, DTV_CODE_RATE_HP, FEC_AUTO);
		SET_DTV_PRPERTY(dtv, propCount, DTV_CODE_RATE_LP, FEC_AUTO);
		SET_DTV_PRPERTY(dtv, propCount, DTV_MODULATION, QAM_AUTO);
		SET_DTV_PRPERTY(dtv, propCount, DTV_TRANSMISSION_MODE, TRANSMISSION_MODE_AUTO);
		SET_DTV_PRPERTY(dtv, propCount, DTV_GUARD_INTERVAL, GUARD_INTERVAL_AUTO);
		SET_DTV_PRPERTY(dtv, propCount, DTV_HIERARCHY, HIERARCHY_AUTO);
		if(delSys == SYS_DVBT2) {
			uint8_t plp_id = media->dvb_t.plp_id;

			SET_DTV_PRPERTY(dtv, propCount, DTV_STREAM_ID, plp_id);
			eprintf("   T2: plp %u\n", plp_id);
		}

		eprintf("   T: bandwidth %u, invertion %u\n", bandwidthHz, inversion);
	} else if((delSys == SYS_DVBC_ANNEX_AC) && (media->type == serviceMediaDVBC)) {
		uint32_t modulation;
		uint32_t symbol_rate;
		uint32_t inversion = appControlInfo.dvbcInfo.fe.inversion;
		modulation  = media->dvb_c.modulation;
		symbol_rate = media->dvb_c.symbol_rate;

		SET_DTV_PRPERTY(dtv, propCount, DTV_FREQUENCY, frequency);
		SET_DTV_PRPERTY(dtv, propCount, DTV_MODULATION, modulation);
		SET_DTV_PRPERTY(dtv, propCount, DTV_SYMBOL_RATE, symbol_rate);
		SET_DTV_PRPERTY(dtv, propCount, DTV_INVERSION, inversion);
		SET_DTV_PRPERTY(dtv, propCount, DTV_INNER_FEC, FEC_NONE);

		eprintf("   C: Symbol rate %u, modulation %u invertion %u\n",
			symbol_rate, modulation, inversion);
	} else if(((delSys == SYS_DVBS) || (delSys == SYS_DVBS2)) && (media->type == serviceMediaDVBS)) {
		uint32_t inversion = 0;
		fe_modulation_t modulation = QPSK;
		uint32_t polarization = media->dvb_s.polarization;
		uint32_t symbol_rate = media->dvb_s.symbol_rate;
		uint32_t freqLO = 0;
		uint32_t tone = SEC_TONE_OFF;

		if((TUNER_C_BAND_START <= frequency) && (frequency <= TUNER_C_BAND_END)) {
			freqLO = 5150000;
			tone = SEC_TONE_OFF;
		} else if((TUNER_KU_LOW_BAND_START <= frequency) && (frequency <= TUNER_KU_LOW_BAND_END)) {
			freqLO = 9750000;
			tone = SEC_TONE_OFF;
		} else if((TUNER_KU_HIGH_BAND_START <= frequency) && (frequency <= TUNER_KU_HIGH_BAND_END)) {
			freqLO = 10600000;
			tone = SEC_TONE_ON;
		} else {
			printf("%s()[%d]: !!!!!!\n", __func__, __LINE__);
		}
		//north america: freqLO = 11250000
		frequency -= freqLO;

		SET_DTV_PRPERTY(dtv, propCount, DTV_FREQUENCY, frequency);
		SET_DTV_PRPERTY(dtv, propCount, DTV_INVERSION, inversion);
		SET_DTV_PRPERTY(dtv, propCount, DTV_MODULATION, modulation);
		SET_DTV_PRPERTY(dtv, propCount, DTV_SYMBOL_RATE, symbol_rate);
		//SEC_VOLTAGE_13 - vertical, SEC_VOLTAGE_18 - horizontal
		SET_DTV_PRPERTY(dtv, propCount, DTV_VOLTAGE, polarization ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18);
		SET_DTV_PRPERTY(dtv, propCount, DTV_TONE, tone);

		dvbfe_diseqcSetup(adapter, frequency, media);

		eprintf("   S: Symbol rate %u\n", symbol_rate);
	} else if(((delSys == SYS_ATSC) || (delSys == SYS_DVBC_ANNEX_B)) /*&& (media == NULL || media->type == serviceMediaATSC)*/) {
		uint32_t modulation = media->atsc.modulation;
		if(modulation == 0) {
			modulation = VSB_8;
		}

		SET_DTV_PRPERTY(dtv, propCount, DTV_FREQUENCY, frequency);
		SET_DTV_PRPERTY(dtv, propCount, DTV_MODULATION, modulation);

	} else {
		eprintf("%s[%d]: ERROR: Unsupported frontend delSys=%s (media %d).\n", __func__, adapter,
			table_IntStrLookup(delivery_system_desc, delSys, "unknown"), (int32_t)media->type);
		return -1;
	}

	dtv[propCount].cmd = DTV_TUNE;
	propCount++;
	cmdseq.num = propCount;
	cmdseq.props = dtv;

	ioctl_or_abort(adapter, g_adapterInfo[adapter].fd, FE_SET_PROPERTY, &cmdseq);

	return 0;
}

static int32_t dvbfe_setParamSTAPISDK(uint32_t adapter, fe_delivery_system_t delSys, uint32_t frequency, EIT_media_config_t *media)
{
#if (defined STSDK)
	uint32_t		freqKHz = dvbfe_frequencyKHz(adapter, frequency);
	elcdRpcType_t	type = elcdRpcInvalid;
	cJSON			*result = NULL;
	cJSON			*params = NULL;

	dvbfe_diseqcSetup(adapter, frequency, media);

	params = cJSON_CreateObject();
	if(!params) {
		eprintf("%s[%d]: out of memory\n", __func__, adapter);
		return -1;
	}
	st_setTuneParams(adapter, params, media);
	cJSON_AddItemToObject(params, "frequency", cJSON_CreateNumber(freqKHz));
	st_rpcSync(elcmd_dvbtune, params, &type, &result);
	if(result && result->valuestring && (strcmp(result->valuestring, "ok") == 0)) {
		dprintf("%s(): Has lock\n", __func__);
	}
	cJSON_Delete(params);
	cJSON_Delete(result);
	return 0;
#else
	return -1;
#endif
}

int32_t dvbfe_setParam(uint32_t adapter, int32_t wait_for_lock,
						EIT_media_config_t *media, dvbfe_cancelFunctionDef* pFunction)
{
    uint8_t needTune = 0;
    fe_delivery_system_t delSys;
    uint32_t frequency;
    int32_t ret;
    int32_t hasLock;
    int32_t hasSignal;
    int32_t timeout;


	if(media == NULL) {
		eprintf("%s(): Error on tuning adapter%d, bad params!\n", __func__, adapter);
		return -1;
	}
	delSys = dvbfe_getDelSysFromMedia(media);
	frequency = media->frequency;

	if(g_adapterInfo[adapter].state.forceSetDelSys || (g_adapterInfo[adapter].state.curDelSys != delSys)) {
		if(dvbfe_setFrontendType(adapter, delSys) != 0) {
			eprintf("%s[%d]: Failed to set frontend delSys for current service\n", __func__, adapter);
			return -1;
		}
		g_adapterInfo[adapter].state.forceSetDelSys = 0;
		needTune = 1;
	}
	if(g_adapterInfo[adapter].state.frequency != frequency) {
		g_adapterInfo[adapter].state.frequency = frequency;
		needTune = 1;
	}

	switch(delSys) {
		case SYS_DVBT2: {
			uint8_t plp_id = media->dvb_t.plp_id;

			//check if tune needed
			if(g_adapterInfo[adapter].state.dvbtInfo.plp_id != plp_id) {
				g_adapterInfo[adapter].state.dvbtInfo.plp_id = plp_id;
				needTune = 1;
			}
			//no break!!!
		}
		case SYS_DVBT:
//			g_adapterInfo[adapter].state.dvbtInfo.generation = generation;
//			g_adapterInfo[adapter].state.dvbtInfo.bandwidth = bandwidth;
			break;
		case SYS_DVBC_ANNEX_AC:
			if((g_adapterInfo[adapter].state.dvbcInfo.symbolRate != media->dvb_c.symbol_rate)
			|| (g_adapterInfo[adapter].state.dvbcInfo.modulation != media->dvb_c.modulation)) {
				g_adapterInfo[adapter].state.dvbcInfo.symbolRate = media->dvb_c.symbol_rate;
				g_adapterInfo[adapter].state.dvbcInfo.modulation = media->dvb_c.modulation;
				needTune = 1;
			}
			break;
		case SYS_DVBS:
		case SYS_DVBS2: {
			uint32_t polarization = media->dvb_s.polarization;
			if(g_adapterInfo[adapter].state.dvbsInfo.polarization != polarization) {
				g_adapterInfo[adapter].state.dvbsInfo.polarization = polarization;
				needTune = 1;
			}
// 			g_adapterInfo[adapter].state.dvbsInfo.band = appControlInfo.dvbsInfo.band;
// 			g_adapterInfo[adapter].state.dvbsInfo.diseqc = appControlInfo.dvbsInfo.diseqc;
// 			g_adapterInfo[adapter].state.dvbsInfo.symbolRate = symbol_rate;
			break;
		}
		case SYS_ATSC:
		case SYS_DVBC_ANNEX_B:
// 			g_adapterInfo[adapter].state.atscInfo.modulation = modulation;
			break;
		default:
			needTune = 1;
			break;
	}

    if(needTune == 0) {
        //check if tuner locked
        tunerState_t state;

        memset(&state, 0, sizeof(state));
        dvbfe_getSignalInfo(adapter, &state);
        if(state.fe_status & FE_HAS_LOCK) {
            //no need to reconfigure tuner
            return 0;
        }
    }

    ret = -1;
    eprintf("%s(): Tune adapter=%d tuner, frequency=%uHz\n", __func__, adapter, frequency);
    switch(g_adapterInfo[adapter].driverType) {
        case eTunerDriver_linuxDVBapi:
            ret = dvbfe_setParamLinuxDVBapi(adapter, delSys, frequency, media);
            break;
        case eTunerDriver_STAPISDK:
            ret = dvbfe_setParamSTAPISDK(adapter, delSys, frequency, media);
            break;
        default:
            break;
    }
    if(ret != 0) {
        eprintf("%s(): Tuner %d not tuned on %u\n", __func__, adapter, frequency);
    }

    if(appControlInfo.dvbCommonInfo.adapterSpeed < 0) {//is this can happen?
        wait_for_lock = 0;
    }
    if(wait_for_lock == 0) {
//        eprintf("%s(): g_adapterInfo[%d].fd=%d\n", __func__, adapter, g_adapterInfo[adapter].fd);
        return 1;//1 mean 'no lock'
    }

    hasLock = 0;
    hasSignal = 0;
    timeout = (appControlInfo.dvbCommonInfo.adapterSpeed + 1);
#ifdef STBTI
    timeout *= 10;
#endif

    do {
        tunerState_t state;
        uint32_t us;
        uint32_t i;

        memset(&state, 0, sizeof(state));
        state.ber = BER_THRESHOLD;
        dvbfe_getSignalInfo(adapter, &state);

        if(state.fe_status & FE_HAS_LOCK) {
            if(!hasLock) {
                // locked, give adapter even more time... 
                dprintf("%s()[%d]: L\n", __func__, adapter);
            } else {
                break;
            }
            hasLock = 1;
        } else if(state.fe_status & FE_HAS_SIGNAL) {
            if(hasSignal == 0) {
                eprintf("%s()[%d]: Has signal\n", __func__, adapter);
                // found something above the noise level, increase timeout time 
                timeout += appControlInfo.dvbCommonInfo.adapterSpeed;
                hasSignal = 1;
            }
            dprintf("%s()[%d]: S (%d)\n", __func__, adapter, timeout);
        } else {
            dprintf("%s()[%d]: N (%d)\n", __func__, adapter, timeout);
            // there's no and never was any signal, reach timeout faster 
            if(hasSignal == 0) {
                eprintf("%s()[%d]: Skip\n", __func__, adapter);
                --timeout;
            }
        }

        // If ber is not -1, then wait a bit more 
//         if(state.ber == 0xffffffff) {
            dprintf("%s()[%d]: All clear...\n", __func__, adapter);
            us = 100000;
//         } else {
//             eprintf("%s()[%d]: Something is out there... (ber %u)\n", __func__, adapter, state.ber);
//             us = 500000;
//         }
        usleep(appControlInfo.dvbCommonInfo.adapterSpeed*10000);

        for(i = 0; i < us; i += SLEEP_QUANTUM) {
            if(pFunction && (pFunction() == -1)) {
                return -1;
            }
            if(dvbfe_getSignalInfo(adapter, NULL) == 1) {
                break;
            }
            usleep(SLEEP_QUANTUM);
        }

//         if((state.ber > 0) && (state.ber < BER_THRESHOLD)) {
//             break;
//         }
    } while(--timeout > 0);

    dprintf("%s[%d]: %u timeout %d, ber %u\n", __func__, adapter, frequency, timeout, ber);
    eprintf("%s(): Frequency set: adapter=%d, frequency=%ud, hasLock=%d, wait_for_lock=%d\n",
            __func__, adapter, frequency, hasLock, wait_for_lock);

    return !hasLock;
}

int32_t dvbfe_checkFrequency(fe_delivery_system_t type, uint32_t frequency, uint32_t adapter,
								uint32_t* ber, uint32_t fe_step_size, EIT_media_config_t *media,
								dvbfe_cancelFunctionDef* pFunction)
{
	int32_t res;
	int32_t frontend_fd = g_adapterInfo[adapter].fd;
	EIT_media_config_t local_media;

	if(media == NULL) {
		dvbfe_fillMediaConfig(adapter, frequency, &local_media);
		media = &local_media;
	}

	if((res = dvbfe_setParam(adapter, 1, media, pFunction)) == 0) {
		int32_t i;
		for(i = 0; i < 10; i++) {
			fe_status_t s;

			struct dtv_property dtv = {.cmd = DTV_FREQUENCY, };
			struct dtv_properties cmdseq = {.num = 1, .props = &dtv};

			while(1) {
				// Get the front-end state including current freq. setting.
				// Some drivers may need to be modified to update this field.
				ioctl_or_abort(adapter, frontend_fd, FE_GET_PROPERTY, &cmdseq);
				ioctl_or_abort(adapter, frontend_fd, FE_READ_STATUS,  &s);

				dprintf("%s[%d]: f_orig=%u   f_read=%u status=%02x\n", __func__, adapter,
				        frequency, (long) dtv.u.data, s);
				// in theory a clever FE could adjust the freq. to give a perfect freq. needed !
				if(dtv.u.data == frequency) {
					break;
				} else {
#ifdef DEBUG
					long gap = (dtv.u.data > frequency) ?
								dtv.u.data - frequency  :
								frequency - dtv.u.data;
					(void)gap;
					dprintf("%s[%d]: gap = %06ld fe_step_size=%06ld\n", __func__, adapter, gap, fe_step_size);
					// if (gap < fe_step_size) // may lead to hangs, so disabled
						// FE thinks its done a better job than this app. !
#endif
					break;
				}
				usleep (20000);
			}

			if(s & FE_HAS_LOCK) {
				ioctl_or_abort(adapter, frontend_fd, FE_READ_BER, ber);
				dprintf("%s[%d]: Got LOCK f=%u\n", __func__, adapter, frequency);
				return 1;
			}
		}
		dprintf("%s[%d]: No Lock f=%u\n", __func__, adapter, frequency);
	} else if(res == 1) {
		dprintf("%s[%d]: Trust LOCK f=%u\n", __func__, adapter, frequency);

		return 1;
	} else if(res == -1) {
		dprintf("%s[%d]: Fail or user abort f=%u\n", __func__, adapter, frequency);
		return -1;
	}

	return 0;
}

/* Thread to track frontend signal status */
static void *dvbfe_tracker_Thread(void *pArg)
{
	int err;
	uint32_t adapter = (uint32_t)pArg;

	for(;;) {
		sleep(TRACK_INTERVAL); // two sec.
		pthread_testcancel(); // just in case
		if(!appControlInfo.dvbCommonInfo.streamerInput) {
			ioctl_loop(adapter, err, g_adapterInfo[adapter].fd, FE_READ_STATUS,             &g_adapterInfo[adapter].state.fe_state.fe_status);
			ioctl_loop(adapter, err, g_adapterInfo[adapter].fd, FE_READ_BER,                &g_adapterInfo[adapter].state.fe_state.ber);
			ioctl_loop(adapter, err, g_adapterInfo[adapter].fd, FE_READ_SIGNAL_STRENGTH,    &g_adapterInfo[adapter].state.fe_state.signal_strength);
			ioctl_loop(adapter, err, g_adapterInfo[adapter].fd, FE_READ_SNR,                &g_adapterInfo[adapter].state.fe_state.snr);
			ioctl_loop(adapter, err, g_adapterInfo[adapter].fd, FE_READ_UNCORRECTED_BLOCKS, &g_adapterInfo[adapter].state.fe_state.uncorrected_blocks);
		}
	}
	return NULL;
}

int32_t dvbfe_trackerStart(uint32_t adapter)
{
	if(adapter >= MAX_ADAPTER_SUPPORTED) {
		return -1;
	}

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi:
			if(!g_adapterInfo[adapter].fe_tracker_Thread) {
				int32_t ret;
				pthread_attr_t attr;
				struct sched_param param = { 90 };

				pthread_attr_init(&attr);
				pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
				pthread_attr_setschedparam (&attr, &param);

				ret = pthread_create(&(g_adapterInfo[adapter].fe_tracker_Thread), &attr, dvbfe_tracker_Thread, (void *)adapter);
				pthread_attr_destroy(&attr);
				if(ret != 0) {
					eprintf("%s[%d]: pthread_create (fe track) err=%d\n", __FUNCTION__, adapter, ret);
					return -1;
				}
				pthread_detach(g_adapterInfo[adapter].fe_tracker_Thread);
			}
			break;
		case eTunerDriver_STAPISDK:
//			ret = dvbfe_setParamSTAPISDK(frequency, adapter, media);
			break;
		case eTunerDriver_streamerInput:
		default:
			break;
	}
	
	return 0;
}

int32_t dvbfe_trackerStop(uint32_t adapter)
{
	if(adapter >= MAX_ADAPTER_SUPPORTED) {
		return -1;
	}

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi:
			//dprintf("%s[%d]: in\n", __FUNCTION__, dvb->adapter);
			if(g_adapterInfo[adapter].fe_tracker_Thread) {
				pthread_cancel(g_adapterInfo[adapter].fe_tracker_Thread);
				g_adapterInfo[adapter].fe_tracker_Thread = 0;
			}
		case eTunerDriver_STAPISDK:
			break;
		case eTunerDriver_streamerInput:
		default:
			break;
	}
	return 0;
}

int32_t dvbfe_getSignalInfo(uint32_t adapter, tunerState_t *state)
{
	int32_t ret = -1;
	if(adapter >= MAX_ADAPTER_SUPPORTED) {
		return -1;
	}

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi: {
			fe_status_t status = 0;
			int32_t frontend_fd = g_adapterInfo[adapter].fd;

			if(frontend_fd >= 0) {
				mysem_get(dvbfe_semaphore);
				ioctl(frontend_fd, FE_READ_STATUS, &status);
				if(state) {
					state->fe_status = status;
					ioctl(frontend_fd, FE_READ_SIGNAL_STRENGTH, &(state->signal_strength));
					ioctl(frontend_fd, FE_READ_SNR, &(state->snr));
					ioctl(frontend_fd, FE_READ_BER, &(state->ber));
					ioctl(frontend_fd, FE_READ_UNCORRECTED_BLOCKS, &(state->uncorrected_blocks));
				}
				mysem_release(dvbfe_semaphore);
				ret = (status & FE_HAS_LOCK) == FE_HAS_LOCK;
			} else {
				eprintf("%s: failed opening frontend %d\n", __func__, adapter);

#if !(defined ENABLE_DVB_PVR)
				ret = -1;
#endif
			}
			break;
		}
		case eTunerDriver_STAPISDK: {
#if (defined STSDK)
			elcdRpcType_t type;
			cJSON *res = NULL;
			cJSON *param = cJSON_CreateObject();
			cJSON_AddItemToObject(param, "tuner", cJSON_CreateNumber(adapter));

			st_rpcSyncTimeout(elcmd_getDvbTunerStatus, param, 1, &type, &res);
			if((type != elcdRpcResult) || !res) {
				eprintf("%s(): failed call rpc %s\n", __func__, rpc_getCmdName(elcmd_getDvbTunerStatus));
				return 0;
			}
			cJSON_Delete(param);

			if(res->type == cJSON_Object) {
				ret = (objGetInt(res, "state", 0) == 5) ? 1 : 0;//has lock

				if(state) {
					state->fe_status = ret ? FE_HAS_LOCK : 0;
					state->signal_strength = objGetInt(res, "RFLevel", 0);
					if(dvbfe_getType(adapter) == SYS_DVBS
						|| dvbfe_getType(adapter) == SYS_DVBC_ANNEX_AC
						|| dvbfe_getType(adapter) == SYS_DVBC_ANNEX_B
					) {//use fake signal strength based on snr, coz RFLevel allways 0
						state->signal_strength = objGetInt(res, "signalQuality", 0) * 0xffff / 20;//Let 20dB as 100%
					}
					state->ber = objGetInt(res, "bitErrorRate", 0);
					state->snr = objGetInt(res, "signalQuality", 0);
					state->uncorrected_blocks = 0;
				}
			}
			cJSON_Delete(res);
#endif //#if (defined STSDK)
			break;
		}
		case eTunerDriver_streamerInput:
		default:
			break;
	}

	return ret;
}

int32_t dvbfe_setFrontendType(uint32_t adapter, fe_delivery_system_t type)
{
	int32_t ret = 0;
  
	if(adapter >= MAX_ADAPTER_SUPPORTED) {
		return -1;
	}

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi: {
			int32_t local_fd = g_adapterInfo[adapter].fd;
			struct dtv_property p = { .cmd = DTV_DELIVERY_SYSTEM, };
			struct dtv_properties cmdseq = {
				.num = 1,
				.props = &p
			};

			if(local_fd < 0) {
				local_fd = dvbfe_openLinuxDVBtuner(adapter);
				if(local_fd < 0) {
					return -1;
				}
			}

			p.u.data = type;
			ret = ioctl(local_fd, FE_SET_PROPERTY, &cmdseq);
			if(g_adapterInfo[adapter].fd < 0) {
				close(local_fd);
			}
			if(ret < 0) {
				eprintf("%s: set property failed: %m\n", __func__);
				return -1;
			}
		}
		case eTunerDriver_STAPISDK:
		case eTunerDriver_streamerInput:
		default:
			break;
	}
	g_adapterInfo[adapter].state.curDelSys = type;

	return 0;
}

int32_t dvbfe_updateMediaToCurentState(uint32_t adapter, EIT_media_config_t *media)
{
	dvbfe_updateCurrentDelSys(adapter);
	if(g_adapterInfo[adapter].state.curDelSys == SYS_DVBT2) {
		printf("%s:%s()[%d]: is dvb-t2, plp_id=%d\n", __FILE__, __func__, __LINE__, media->dvb_t.plp_id);
//		if(media->dvb_t.plp_id == 0) {
			media->dvb_t.generation = 2;
//		}
	}
	return 0;
}

int32_t dvbfe_isMultistreamMux(uint32_t adapter, EIT_media_config_t *media)
{
	if(g_adapterInfo[adapter].state.curDelSys == SYS_DVBT2) {
		return 1;
	}
	return 0;
}

int32_t dvbfe_updateMediaToNextStream(uint32_t adapter, EIT_media_config_t *media)
{
	if(g_adapterInfo[adapter].state.curDelSys == SYS_DVBT2) {
		media->dvb_t.plp_id++;
		if(media->dvb_t.plp_id > 3) {
			return -1;
		}
		return 0;
	}

	return 0;
}

int32_t dvbfe_open(uint32_t adapter)
{
	if(adapter >= MAX_ADAPTER_SUPPORTED) {
		return -1;
	}

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi:
			if(g_adapterInfo[adapter].fd < 0) {
				g_adapterInfo[adapter].state.forceSetDelSys = 1;
				g_adapterInfo[adapter].fd = dvbfe_openLinuxDVBtuner(adapter);
				if(g_adapterInfo[adapter].fd < 0) {
					return -1;
				}
			}
		case eTunerDriver_STAPISDK:
		case eTunerDriver_streamerInput:
		default:
			break;
	}

	return 0;
}

int32_t dvbfe_close(uint32_t adapter)
{
	if(adapter >= MAX_ADAPTER_SUPPORTED) {
		return -1;
	}

	dvbfe_trackerStop(adapter);

	switch(g_adapterInfo[adapter].driverType) {
		case eTunerDriver_linuxDVBapi:
			if(g_adapterInfo[adapter].fd >= 0) {
				close(g_adapterInfo[adapter].fd);
				g_adapterInfo[adapter].fd = -1;
			}
		case eTunerDriver_STAPISDK:
		case eTunerDriver_streamerInput:
		default:
			break;
	}

	return 0;
}

int32_t dvbfe_init(void)
{
	uint32_t i;

	memset(g_adapterInfo, 0, sizeof(g_adapterInfo));
	mysem_create(&dvbfe_semaphore);

	for(i = 0; i < MAX_ADAPTER_SUPPORTED; i++) {
		g_adapterInfo[i].driverType = eTunerDriver_none;
		g_adapterInfo[i].fd = -1;
	}

	for(i = 0; i < MAX_ADAPTER_SUPPORTED; i++) {
		dvbfe_scanLinuxDVBTuner(i);
	}
	dvbfe_scanSTAPISDKTuners();

	return 0;
}

int32_t dvbfe_terminate(void)
{
	mysem_destroy(dvbfe_semaphore);
	return 0;
}

#endif //#ifdef ENABLE_DVB
