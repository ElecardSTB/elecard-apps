
/*
 dvb.c

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

#include "dvb.h"
#include "off_air.h"

#ifdef ENABLE_DVB

#include "debug.h"
#include "list.h"
#include "app_info.h"
#include "interface.h"
#include "l10n.h"
#include "playlist.h"
#include "sem.h"
#include "teletext.h"
#include "stsdk.h"
#include "helper.h"
//#include "elcd-rpc.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>
#include <libgen.h>
#include <signal.h>
#include <pthread.h>

#include <poll.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include <libucsi/atsc/section.h>
#include <libucsi/atsc/descriptor.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define fatal   eprintf
#define info    dprintf
#define verbose(...)
#define debug(...)

#define PERROR(fmt, ...) eprintf(fmt " (%s)\n", ##__VA_ARGS__, strerror(errno))

#define AUDIO_CHAN_MAX (8)

#define MAX_OFFSETS   (1)

#define MAX_RUNNING   (32)

#define FILE_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

// For front end signal tracking
// sec.s  between FE checks
#define TRACK_INTERVAL (2)

/* SD bit rate ~ 1.5GB / hour -> ~ 450KB / second */
#define DEFAULT_PVR_RATE (450*1024)

#define MEDIA_ID         (uint32_t)frequency

#define ioctl_or_abort(tuner, fd, request, ...)                                \
	do {                                                                       \
		int ret = ioctl(fd, request, ##__VA_ARGS__);                           \
		if (ret < 0) {                                                         \
			PERROR("%s[%d]: ioctl " #request " failed", __FUNCTION__, tuner);  \
			return ret;                                                        \
		}                                                                      \
	} while(0)

#define ioctl_or_warn(tuner, fd, request, ...)                                 \
	do {                                                                       \
		if (ioctl(fd, request, ##__VA_ARGS__) < 0)                             \
			PERROR("%s[%d]: ioctl " #request " failed", __FUNCTION__, tuner);  \
	} while(0)

#define ioctl_loop(tuner, err, fd, request, ...)                               \
	do {                                                                       \
		err = ioctl(fd, request, ##__VA_ARGS__);                               \
		if (err < 0) {                                                         \
			info("%s[%d]: ioctl " #request " failed", __FUNCTION__, tuner);    \
		}                                                                      \
	} while (err<0)                                                            \

#if 0
#define mysem_get(sem) do {                                                    \
	eprintf("%s: mysem_get\n", __FUNCTION__);                                  \
	mysem_get(sem);                                                            \
} while (0)

#define mysem_release(sem) do {                                                \
	eprintf("%s: mysem_release\n", __FUNCTION__);                              \
	mysem_release(sem);                                                        \
} while (0)
#endif

#define ZERO_SCAN_MESAGE()	//scan_messages[0] = 0
#define SCAN_MESSAGE(...)	//sprintf(&scan_messages[strlen(scan_messages)], __VA_ARGS__)

#define TUNER_C_BAND_START			 3000000 /*kHZ*/
#define TUNER_C_BAND_END			 4200000 /*kHZ*/
#define TUNER_KU_LOW_BAND_START		10700000 /*kHZ*/
#define TUNER_KU_LOW_BAND_END		11700000 /*kHZ*/
#define TUNER_KU_HIGH_BAND_START	11700001 /*kHZ*/
#define TUNER_KU_HIGH_BAND_END		12750000 /*kHZ*/

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

// defines one chain of devices which together make up a DVB receiver/player
struct dvb_instance {
	// file descriptors for frontend, demux-video, demux-audio
	int fdf;
	__u32 setFrequency;
	int adapter;

	DvbMode_t mode;
	fe_delivery_system_t fe_type;
	pthread_t fe_tracker_Thread;

#ifdef LINUX_DVB_API_DEMUX
	int fdv;
#ifdef ENABLE_MULTI_VIEW
	int fdv1;
	int fdv2;
	int fdv3;
#endif
	int fda;
	int fdp;
	int fdt;

	// device setup data
	struct dmx_pes_filter_params filterv;
#ifdef ENABLE_MULTI_VIEW
	struct dmx_pes_filter_params filterv1;
	struct dmx_pes_filter_params filterv2;
	struct dmx_pes_filter_params filterv3;
#endif
	struct dmx_pes_filter_params filtera;
	struct dmx_pes_filter_params filterp;
	struct dvb_frontend_parameters tuner;

#ifdef ENABLE_PVR
#define ENABLE_DVB_PVR // = ENABLE_DVB && LINUX_DVB_API_DEMUX && ENABLE_PVR
	int fdout;

	char directory[PATH_MAX];
	int fileIndex;
	struct {
		int position;
		int last_position;
		int last_index;
		int rate;
		pthread_t thread;
		pthread_t rate_thread;
	} pvr;
#endif
#ifdef ENABLE_MULTI_VIEW
	pthread_t multi_thread;
#endif

#endif // LINUX_DVB_API_DEMUX

	int fdin;
	//teletext
	struct dmx_pes_filter_params filtert;
};

enum running_mode {
	RM_NOT_RUNNING = 0x01,
	RM_STARTS_SOON = 0x02,
	RM_PAUSING     = 0x03,
	RM_RUNNING     = 0x04
};

struct section_buf {
	struct list_head list;
	tunerFormat tuner;
	int fd;
	int pid;
	int table_id;
	int table_id_ext;
	int section_version_number;
	uint8_t section_done[32];
	int sectionfilter_done;
	uint8_t buf[BUFFER_SIZE];
	time_t timeout;
	time_t start_time;
	time_t running_time;
	uint16_t transport_stream_id;
	int is_dynamic;
	int was_updated;
	list_element_t **service_list;
	int *running_counter; // if not null, used as reference counter by this and child (PAT->PMT) filters
	struct section_buf *next_seg;  /* this is used to handle
									* segmented tables (like NIT-other)
									*/

	struct pollfd	*poll_fd;
#if (defined STSDK)
	int32_t	pipeId;
#endif
};

struct dvb_paramState {
	fe_delivery_system_t delSys;
	uint32_t frequency;
	union {
		stb810_dvbtInfo			dvbtInfo;
		stb810_dvbcInfo			dvbcInfo;
		stb810_dvbsInfo			dvbsInfo;
		stb810_atscInfo			atscInfo;
	};
};

/***********************************************
* EXPORTED DATA                                *
************************************************/
table_IntStr_t fe_typeName[] = {
	{serviceMediaDVBT,	"DVB-T"},
	{serviceMediaDVBC,	"DVB-C"},
	{serviceMediaDVBS,	"DVB-S"},
	{serviceMediaATSC,	"ATSC"},
	TABLE_INT_STR_END_VALUE,
};

table_IntStr_t fe_modulationName[] = {
	{QPSK,		"QPSK"},
	{QAM_16,	"QAM_16"},
	{QAM_32,	"QAM_32"},
	{QAM_64,	"QAM_64"},
	{QAM_128,	"QAM_128"},
	{QAM_256,	"QAM_256"},
	{QAM_AUTO,	"QAM_AUTO"},
	{VSB_8,		"VSB_8"},
	{VSB_16,	"VSB_16"},
	{PSK_8,		"PSK_8"},
	{APSK_16,	"APSK_16"},
	{APSK_32,	"APSK_32"},
	{DQPSK,		"DQPSK"},
	{QAM_4_NR,	"QAM_4_NR"},
	TABLE_INT_STR_END_VALUE,
};

table_IntInt_t type_to_delsys[] = {
	{FE_OFDM,	SYS_DVBT},
	{FE_OFDM,	SYS_DVBT2},
	{FE_QAM,	SYS_DVBC_ANNEX_AC},
	{FE_QPSK,	SYS_DVBS},
	{FE_ATSC,	SYS_ATSC},
	{FE_ATSC,	SYS_DVBC_ANNEX_B},

	TABLE_INT_INT_END_VALUE,
};


list_element_t *dvb_services = NULL;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static LIST_HEAD(running_filters);
static LIST_HEAD(waiting_filters);
static int n_running = 0;
static int lock_filters = 1;
static inline void dvb_filtersLock(void)   { lock_filters = 1; }
static inline void dvb_filtersUnlock(void) { lock_filters = 0; }

static struct dvb_instance dvbInstances[ADAPTER_COUNT];
static __u32 currentFrequency[ADAPTER_COUNT];
static char * fe_typeNames[] = {
	"DVB-S",
	"DVB-C",
	"DVB-T",
	"ATSC"
};

static pmysem_t dvb_semaphore;
static pmysem_t dvb_fe_semaphore;
static pmysem_t dvb_filter_semaphore;
static NIT_table_t dvb_scan_network;
static struct dvb_paramState dvbParamState;

//char scan_messages[64*1024];

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int dvb_hasPayloadTypeNB(EIT_service_t *service, payload_type p_type);
static int dvb_hasMediaTypeNB(EIT_service_t *service, media_type m_type);
static int dvb_hasMediaNB(EIT_service_t *service);

static int dvb_openFrontend(int adapter, int flags);
static int dvb_getFrontendInfo(int adapter, int flags, struct dvb_frontend_info *fe_info);

#ifdef STSDK
static inline int dvb_isLinuxAdapter(int adapter)
{
	return adapter >= 0 && adapter < ADAPTER_COUNT;
}
#else
#define dvb_isLinuxAdapter(x) 1
#endif

// Low nibble of 4th (data) DiSEqC command byte
static inline uint8_t diseqc_data_lo(int satellite_position, int is_vertical, uint32_t f_khz)
{
	return (satellite_position & 0x03) << 2 | (is_vertical ? 0 : 1) << 1 | (f_khz > 11700000);
}

static int dvb_diseqcSend (tunerFormat tuner, int frontend_fd, const uint8_t *tx, size_t tx_len);

static inline int isSubtitle( PID_info_t* stream)
{
	return stream->component_descriptor.stream_content == 0x03 &&
	       stream->component_descriptor.component_type >= 0x10 &&
	       stream->component_descriptor.component_type <= 0x15;
}

fe_delivery_system_t dvb_getDelSysFromService(EIT_service_t *service);

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

static int32_t dvb_getPollFds(struct pollfd **fds, nfds_t *count)
{
	struct list_head *pos;
	struct section_buf* s;
	int32_t i;
	int32_t ret = 0;
	static struct pollfd poll_fds[MAX_RUNNING];

/*	for(i = 0; i < MAX_RUNNING; i++) {
		poll_fds[i].fd = -1;
	}*/
	i = 0;
	list_for_each(pos, &running_filters) {
		if(i >= MAX_RUNNING) {
			fatal("%s: too many poll_fds\n", __FUNCTION__);
		}
		s = list_entry(pos, struct section_buf, list);
		if(s->fd == -1) {
			fatal("%s: s->fd == -1 on running_filters\n", __FUNCTION__);
		}
		poll_fds[i].fd = s->fd;
		poll_fds[i].events = POLLIN;
		poll_fds[i].revents = 0;
		s->poll_fd = poll_fds + i;
		i++;
	}

	if(i != n_running) {
		fatal("%s: n_running is hosed: i=%d, n_running=%d\n", __FUNCTION__, i, n_running);
		n_running = i;
		ret = -1;
	}
	*fds = poll_fds;
	*count = n_running;

	return ret;
}

#if (defined LINUX_DVB_API_DEMUX)
static int32_t dvb_filterStart_arch(struct section_buf* s)
{
	struct dmx_sct_filter_params f;
	char demux_devname[32];

	snprintf(demux_devname, sizeof(demux_devname), "/dev/dvb/adapter%d/demux0", dvb_getAdapter(s->tuner));
	s->fd = open(demux_devname, O_RDWR | O_NONBLOCK);
	if(s->fd < 0) {
		return -1;
	}

	memset(&f, 0, sizeof(f));
	f.pid = (__u16)s->pid;

	if(s->table_id < 0x100 && s->table_id > 0) {
		f.filter.filter[0]	= (unsigned char) s->table_id;
		f.filter.mask[0]	= 0xff;
	}
	f.timeout = 0;
	f.flags = DMX_IMMEDIATE_START | DMX_CHECK_CRC;

	if(ioctl(s->fd, DMX_SET_FILTER, &f) == -1) {
		PERROR("%s: ioctl DMX_SET_FILTER failed", __FUNCTION__);
		ioctl(s->fd, DMX_STOP);
		close(s->fd);
		return -1;
	}

	return 0;
}

static int32_t dvb_filterStop_arch(struct section_buf *s)
{
	ioctl(s->fd, DMX_STOP);
	close(s->fd);
	s->fd = -1;

	return 0;
}
#elif (defined STSDK)
static char *dvb_getSectionPipeName(int32_t id)
{
	static char pipeNameBuf[32];

	snprintf(pipeNameBuf, sizeof(pipeNameBuf), "/tmp/section_pipe_%02d", id);
	return pipeNameBuf;
}

static int32_t g_dvb_pipe[16];

static int32_t dvb_sectionPipeStop(int32_t pipeId)
{
	elcdRpcType_t type;
	cJSON *result = NULL;
	cJSON *params;
	char *pipeName;
	int32_t ret = -1;

	if(pipeId < 0) {
		eprintf("%s(): Wrong argument: pipeId=%d\n", __func__, pipeId);
		return -1;
	}

	params = cJSON_CreateObject();
	if(params == NULL) {
		eprintf("%s(): Error in creating params\n", __func__);
		return -4;
	}

	pipeName = dvb_getSectionPipeName(pipeId);
	cJSON_AddStringToObject(params, "uri", pipeName);
	st_rpcSync(elcmd_TSsectionStreamOff, params, &type, &result);
	if(result && (result->valuestring != NULL) &&
		(strcmp(result->valuestring, "ok") == 0)) {
		ret = 0;
	}

	cJSON_Delete(result);
	cJSON_Delete(params);

	unlink(pipeName);
	g_dvb_pipe[pipeId] = 0;
	return ret;
}

static int32_t dvb_lockSectionPipeId(void)
{
	static int32_t init = 0;
	uint32_t i;

	if(!init) {
		for(i = 0; i < ARRAY_SIZE(g_dvb_pipe); i++) {
			dvb_sectionPipeStop(i);
		}
		init = 1;
	}

	for(i = 0; i < ARRAY_SIZE(g_dvb_pipe); i++) {
		if(g_dvb_pipe[i] == 0) {
			g_dvb_pipe[i] = 1;
			return i;
		}
	}

	return -1;
}

static int32_t dvb_filterStart_arch(struct section_buf* s)
{
	elcdRpcType_t type;
	cJSON *result = NULL;
	cJSON *params;
	char *pipeName;
	int32_t ret = -1;

	s->pipeId = dvb_lockSectionPipeId();
	if(s->pipeId < 0) {
		eprintf("%s(): All slots are busy!\n", __func__);
		return -1;
	}
	pipeName = dvb_getSectionPipeName(s->pipeId);
	unlink(pipeName);
	if(mkfifo(pipeName, 0640) < 0) {
		eprintf("%s(): Unable to create a fifo file\n", __func__);
		return -2;
	}
	s->fd = open(pipeName, O_RDONLY | O_NONBLOCK);
	if(s->fd < 0) {
		unlink(pipeName);
		eprintf("%s(): Error in opening file %s\n", __func__, pipeName);
		return -3;
	}

	params = cJSON_CreateObject();
	if(params == NULL) {
		eprintf("%s(): Error in creating params\n", __func__);
		return -4;
	}

	cJSON_AddStringToObject(params, "uri", pipeName);
	cJSON_AddNumberToObject(params, "tuner", s->tuner);
	cJSON_AddNumberToObject(params, "pid", (uint16_t)s->pid);
	if(s->table_id < 0x100 && s->table_id > 0) {
		cJSON_AddNumberToObject(params, "tid", (uint8_t)s->table_id);
	}

	st_rpcSync(elcmd_TSsectionStreamOn, params, &type, &result);
	if(result && (result->valuestring != NULL) &&
		(strcmp(result->valuestring, "ok") == 0)) {
		ret = 0;
	}

	cJSON_Delete(result);
	cJSON_Delete(params);
	return ret;
}

static int32_t dvb_filterStop_arch(struct section_buf *s)
{
	int32_t ret;
	ret = dvb_sectionPipeStop(s->pipeId);

	close(s->fd);
	return ret;
}
#else
#define dvb_filterStart_arch(args...) -1
#define dvb_filterStop_arch(args...) -1

#warning "dvb_filterStart_arch() and dvb_filterStop_arch() not defined!!!"
#endif

static int dvb_filterStart(struct section_buf* s)
{
	int32_t ret = 0;
	if(n_running >= MAX_RUNNING) {
		return -1;
	}

	debug("%s: start filter 0x%02x\n", __FUNCTION__, s->pid);
	ret = dvb_filterStart_arch(s);
	if(ret != 0) {
		eprintf("%s(): Cant start collecting section with pid=0x%04x, table_id=0x%02x\n", __func__, (uint16_t)s->pid, (uint8_t)s->table_id);
		return ret;
	}

	s->sectionfilter_done = 0;
	time(&s->start_time);
	if(s->running_counter != NULL) {
		(*s->running_counter)++;
	}
	s->poll_fd = NULL;

	n_running++;

	return 0;
}

static int32_t dvb_filterStop(struct section_buf *s)
{
	int32_t ret = 0;
	debug("%s: stop filter %d\n", __FUNCTION__, s->pid);

	if(s->start_time == 0) {
		return -1;
	}
	ret = dvb_filterStop_arch(s);
	(void)ret;

	s->running_time += time(NULL) - s->start_time;
	if(s->running_counter != NULL) {
		(*s->running_counter)--;
	}

	n_running--;

	return 0;
}

static int32_t dvb_filterRemove(struct section_buf *s)
{
	dvb_filterStop(s);
	list_del(&s->list);
	if(s->is_dynamic) {
		debug("%s: free dynamic filter\n", __FUNCTION__);
		dfree(s);
	}
	debug("%s: start waiting filters\n", __FUNCTION__);

	return 0;
}

static void dvb_filterAdd(struct section_buf *s)
{
	if(!lock_filters) {
		if(dvb_filterStart(s) == 0) {
			list_add(&s->list, &running_filters);
		} else {
			if(!list_empty(&running_filters)) {
				//add to waiting queue if there are any filter started
				//to prevent infinty loop
				list_add_tail(&s->list, &waiting_filters);
			}
		}
	} else {
		dvb_filterRemove(s);
	}
}

static int32_t dvb_filterPushWaiters(void)
{
	struct list_head *pos;
	struct list_head *n;

	list_for_each_safe(pos, n, &waiting_filters) {
		struct section_buf *s;
		s = list_entry(pos, struct section_buf, list);

		//dvb_filterAdd(s);
		if(dvb_filterStart(s) == 0) {
			list_del(pos);//remove from waiting_filters
			list_add(&s->list, &running_filters);
		} else {
			if(list_empty(&running_filters)) {
				printf("%s:%s()[%d]: Dropping: s->pid=0x%04x, s->table_id=0x%02x, table_id_ext=0x%04x\n", __FILE__, __func__, __LINE__, s->pid, s->table_id, s->table_id_ext);
				dvb_filterRemove(s);
			}
		}
	}

	return 0;
}

static void dvb_filterSetup(struct section_buf* s, tunerFormat tuner,
			  int pid, int tid, int timeout, list_element_t **outServices)
{
	memset (s, 0, sizeof(struct section_buf));

	s->fd = -1;
	s->tuner = tuner;
	s->pid = pid;
	s->table_id = tid;
	s->timeout = timeout;
	s->table_id_ext = -1;
	s->section_version_number = -1;
	s->transport_stream_id = -1;
	s->service_list = outServices;

	INIT_LIST_HEAD (&s->list);
}

static int32_t dvb_filtersFlush(void)
{
	struct list_head *pos;
	struct list_head *n;

	dvb_filtersLock();

	/* Flush waiting filter pool */
	list_for_each_safe(pos, n, &waiting_filters) {
		struct section_buf *s = list_entry(pos, struct section_buf, list);
		dvb_filterRemove(s);
	};
	/* Flush running filter pool */
	list_for_each_safe(pos, n, &running_filters) {
		struct section_buf *s = list_entry(pos, struct section_buf, list);
		dvb_filterRemove(s);
	};
	return 0;
}

static int get_bit (unsigned char *bitfield, int bit)
{
	return (bitfield[bit/8] >> (bit % 8)) & 1;
}

static void set_bit (unsigned char *bitfield, int bit)
{
	bitfield[bit/8] |= 1 << (bit % 8);
}

static char *atsc_getText(struct atsc_text *atext, int len)
{
	struct atsc_text_string *cur_string;
	struct atsc_text_string_segment *cur_segment;
	int str_idx;
	int seg_idx;
	char *str = NULL;

	if(len == 0) {
		return NULL;
	}

	atsc_text_strings_for_each(atext, cur_string, str_idx) {
		atsc_text_string_segments_for_each(cur_string, cur_segment, seg_idx) {
			if(cur_segment->compression_type < 0x3e) {
				uint8_t *decoded = NULL;
				size_t decodedlen = 0;
				size_t decodedpos = 0;

				if(atsc_text_segment_decode(cur_segment, &decoded, &decodedlen, &decodedpos) < 0) {
					printf("\t\t" "%s()[%d]: Decode error\n", __func__, __LINE__);
				} else {
					if(str) {
						printf("%s()[%d]: Unused str=%s, str_idx=%d, seg_idx=%d\n", __func__, __LINE__, str, str_idx, seg_idx);
						free(str);
					}
					str = strndup((char *)decoded, decodedpos);
					printf("%s()[%d]: *** str=%s, strlen(str)=%d\n", __func__, __LINE__, str, strlen(str));
				}
				if(decoded) {
					free(decoded);
				}
			}
		}
	}
	return str;
}

static int32_t parse_descriptor(EIT_service_t *service, struct descriptor *d)
{
	switch(d->tag) {
	case dtag_atsc_extended_channel_name:
	{
		struct atsc_extended_channel_name_descriptor *dx;
		char *service_name = NULL;

//		printf("\t\t" "%s()[%d]: DSC Decode atsc_extended_channel_name_descriptor\n", __func__, __LINE__);
		dx = atsc_extended_channel_name_descriptor_codec(d);
		if(dx == NULL) {
			fprintf(stderr, "%s()[%d]: DSC XXXX atsc_extended_channel_name_descriptor decode error\n", __func__, __LINE__);
			return -1;
		}

		service_name = atsc_getText(atsc_extended_channel_name_descriptor_text(dx), atsc_extended_channel_name_descriptor_text_length(dx));
		if(service_name) {
			if(service) {
//				printf("%s()[%d]: *** service_name=%s, sizeof(service->service_descriptor.service_name)=%d\n",
//							__func__, __LINE__, service_name, sizeof(service->service_descriptor.service_name));
//				strcpy_iso((char *)service->service_descriptor.service_name, (char *)service_name);
				strncpy((char *)service->service_descriptor.service_name, service_name, sizeof(service->service_descriptor.service_name));
			}
			free(service_name);
		}
		break;
	}
	default:
		printf("\t\t" "%s()[%d]: *** d->tag=%d\n", __func__, __LINE__, d->tag);
		break;
	}
	return 0;
}

static int32_t convert_UTF16_UTF8(uint16_t *src, uint8_t *dst, uint32_t src_len, uint32_t dst_len)
{
	int32_t i;
	uint8_t *src_u8 = (uint8_t *)src;
	//TODO: Add common UTF16->UTF8 converter
	for(i = 0; i < 7; i++) {
		dst[i] = src_u8[i * 2 + 1];
	}
	dst[7] = 0;
	return 0;
}

static int32_t parse_atsc_section(list_element_t **head, uint32_t media_id, uint8_t *buf, int len, int pid)
{
	struct section_ext *section_ext = NULL;
	struct atsc_section_psip *section_psip = NULL;
	struct section *section = NULL;

	if((section = section_codec(buf, len)) == NULL) {
		return -1;
	}
	if((section_ext = section_ext_decode(section, 1)) == NULL) {
		return -2;
	}
	if((section_psip = atsc_section_psip_decode(section_ext)) == NULL) {
		return -3;
	}
	switch(section->table_id) {

	case stag_atsc_terrestrial_virtual_channel:
	{
		struct atsc_tvct_section *tvct;
		struct atsc_tvct_channel *cur_channel;
		struct atsc_tvct_section_part2 *part2;
		struct descriptor *curd;
		int idx;
		list_element_t *service_element = NULL;
		EIT_service_t *service = NULL;

		printf("SCT Decode TVCT (pid:0x%04x) (table:0x%02x)\n", pid, section->table_id);
		if((tvct = atsc_tvct_section_codec(section_psip)) == NULL) {
			fprintf(stderr, "SCT XXXX TVCT section decode error\n");
			return -4;
		}

		atsc_tvct_section_channels_for_each(tvct, cur_channel, idx) {
			service_element = find_or_allocate_service(head, cur_channel->program_number, cur_channel->channel_TSID, media_id, 1);
			service = service_element ? (EIT_service_t *)service_element->data : NULL;

//			printf("%s()[%d]: *** service=%p\n", __func__, __LINE__, service);
			atsc_tvct_channel_descriptors_for_each(cur_channel, curd) {
				parse_descriptor(service, curd);
			}

			if(service && (service->service_descriptor.service_name[0] == 0)) {
				convert_UTF16_UTF8(cur_channel->short_name, service->service_descriptor.service_name, 7, sizeof(service->service_descriptor.service_name));
			}
		}

		part2 = atsc_tvct_section_part2(tvct);
		atsc_tvct_section_part2_descriptors_for_each(part2, curd) {
//			parse_descriptor(service, curd);
		}
		break;
	}

	case stag_atsc_cable_virtual_channel:
	{
		struct atsc_cvct_section *cvct;
		struct atsc_cvct_channel *cur_channel;
		struct atsc_cvct_section_part2 *part2;
		struct descriptor *curd;
		int idx;
		list_element_t *service_element = NULL;
		EIT_service_t *service = NULL;

		printf("SCT Decode CVCT (pid:0x%04x) (table:0x%02x)\n", pid, section->table_id);
		if ((cvct = atsc_cvct_section_codec(section_psip)) == NULL) {
			fprintf(stderr, "SCT XXXX CVCT section decode error\n");
			return -4;
		}

		atsc_cvct_section_channels_for_each(cvct, cur_channel, idx) {
			service_element = find_or_allocate_service(head, cur_channel->program_number, cur_channel->channel_TSID, media_id, 1);
			service = service_element ? (EIT_service_t *)service_element->data : NULL;

//			printf("%s()[%d]: *** service=%p\n", __func__, __LINE__, service);
			atsc_cvct_channel_descriptors_for_each(cur_channel, curd) {
				parse_descriptor(service, curd);
			}

			if(service && (service->service_descriptor.service_name[0] == 0)) {
				convert_UTF16_UTF8(cur_channel->short_name, service->service_descriptor.service_name, 7, sizeof(service->service_descriptor.service_name));
			}
		}

		part2 = atsc_cvct_section_part2(cvct);
		atsc_cvct_section_part2_descriptors_for_each(part2, curd) {
//			parse_descriptor(service, curd);
		}
		break;
	}

	default:
		printf("\t\t" "%s()[%d]: *** section->table_id=%d\n", __func__, __LINE__, section->table_id);
		break;
	}

	return 0;
}


/**
 *   returns 0 when more sections are expected
 *	   1 when all sections are read on this pid
 *	   -1 on invalid table id
 */
static int dvb_sectionParse(long frequency, struct section_buf *s)
{
	int table_id;
	int table_id_ext;
	int section_version_number;
	int section_number;
	int last_section_number;
	list_element_t *cur;
	EIT_service_t *service;
	struct section_buf *pmt_filter;
	EIT_media_config_t media;
	list_element_t **services;
	int can_count_sections = 1;
	int updated = 0;

	if(s->service_list != NULL) {
		services = s->service_list;
	} else {
		services = &dvb_services;
	}

	switch(dvb_getType(s->tuner)) {
		case SYS_DVBT:
		case SYS_DVBT2:
			media.type = serviceMediaDVBT;
			media.dvb_t.centre_frequency = frequency;
			media.dvb_t.inversion = appControlInfo.dvbtInfo.fe.inversion;
			media.dvb_t.bandwidth = appControlInfo.dvbtInfo.bandwidth;
			media.dvb_t.plp_id = appControlInfo.dvbtInfo.plp_id;
			media.dvb_t.generation = (dvb_isCurrentDelSys_dvbt2(appControlInfo.dvbInfo.tuner) == 1) ? 2 : 1;
			break;
		case SYS_DVBC_ANNEX_AC:
			media.type = serviceMediaDVBC;
			media.dvb_c.frequency = frequency;
			media.dvb_c.inversion = appControlInfo.dvbcInfo.fe.inversion;
			media.dvb_c.symbol_rate = appControlInfo.dvbcInfo.symbolRate*1000;
			media.dvb_c.modulation = appControlInfo.dvbcInfo.modulation;
			break;
		case SYS_DVBS:
			media.type = serviceMediaDVBS;
			media.dvb_s.frequency = frequency;
			//media.dvb_s.orbital_position
			//media.dvb_s.west_east_flag
			media.dvb_s.polarization = appControlInfo.dvbsInfo.polarization;
			//media.dvb_s.modulation
			media.dvb_s.symbol_rate = appControlInfo.dvbsInfo.symbolRate * 1000;
			//media.dvb_s.FEC_inner
			//media.dvb_s.inversion
			break;
		case SYS_ATSC:
		case SYS_DVBC_ANNEX_B:
			media.type = serviceMediaATSC;
			media.atsc.frequency = frequency;
			media.atsc.modulation = appControlInfo.atscInfo.modulation;
			break;
		default:
			media.type = serviceMediaNone;
	}

	dprintf("%s: media type %s, freq %u\n", __FUNCTION__, dvb_getTypeName(appControlInfo.dvbInfo.tuner), media.frequency);

	table_id = s->buf[0];

	if((s->table_id >= 0) && (table_id != s->table_id)) {
		return -1;
	}

	//unsigned int section_length = (((buf[1] & 0x0f) << 8) | buf[2]) - 11;
	table_id_ext = (s->buf[3] << 8) | s->buf[4];
	section_version_number = (s->buf[5] >> 1) & 0x1f;
	section_number = s->buf[6];
	last_section_number = s->buf[7];

	if(s->section_version_number != section_version_number ||
			s->table_id_ext != table_id_ext) {
		struct section_buf *next_seg = s->next_seg;

		if((s->section_version_number != -1) && (s->table_id_ext != -1)) {
			verbose("%s: section version_number or table_id_ext changed "
				"%d -> %d / %04x -> %04x\n", __FUNCTION__,
				s->section_version_number, section_version_number,
				s->table_id_ext, table_id_ext);
		}
		s->table_id_ext = table_id_ext;
		s->section_version_number = section_version_number;
		s->sectionfilter_done = 0;
		memset(s->section_done, 0, sizeof(s->section_done));
		s->next_seg = next_seg;
	}

	if(!get_bit(s->section_done, section_number)) {
		set_bit (s->section_done, section_number);

		verbose("%s: pid 0x%02x tid 0x%02x table_id_ext 0x%04x, "
				"%i/%i (version %i)\n", __FUNCTION__,
				s->pid, table_id, table_id_ext, section_number,
				last_section_number, section_version_number);

		switch (table_id) {
			case 0x00:
				verbose("%s: PAT\n", __FUNCTION__);
				mysem_get(dvb_semaphore);
				updated = parse_pat(services, (unsigned char *)s->buf, MEDIA_ID, &media);
				cur = *services;
				while(cur != NULL) {
					service = (EIT_service_t*)cur->data;
					if( (service->flags & serviceFlagHasPAT) &&
						service->media.frequency == (unsigned long)frequency )
					{
						pmt_filter = dmalloc(sizeof(struct section_buf));
						if(pmt_filter != NULL) {
							dprintf("%s: new pmt filter ts %d, pid %d, service %d, media %d\n", __FUNCTION__,
									service->common.transport_stream_id,
									service->program_map.program_map_PID,
									service->common.service_id,
									service->common.media_id);
							dvb_filterSetup(pmt_filter, s->tuner, service->program_map.program_map_PID, 0x02, 2, services);
							pmt_filter->transport_stream_id = service->common.transport_stream_id;
							pmt_filter->is_dynamic = 1;
							pmt_filter->running_counter = s->running_counter;
							dvb_filterAdd(pmt_filter);
						}
					}
					cur = cur->next;
				}
				mysem_release(dvb_semaphore);
				break;

			case 0x02:
				dprintf("%s: got pmt filter ts %d, pid %d for service 0x%04x\n", __FUNCTION__, s->transport_stream_id, s->pid, table_id_ext);
				mysem_get(dvb_semaphore);
				cur = *services;
				while(cur != NULL) {
					service = ((EIT_service_t*)cur->data);
					dprintf("%s: Check pmt for: pid %d/%d, media %lu/%lu, tsid %d/%d, freqs %lu/%lu, sid %d\n", __FUNCTION__,
							service->program_map.program_map_PID, s->pid, service->common.media_id, MEDIA_ID,
							service->common.transport_stream_id, s->transport_stream_id, service->media.frequency,
							(unsigned long)frequency, service->common.service_id);
					if (service->program_map.program_map_PID == s->pid &&
						service->common.media_id == MEDIA_ID &&
						service->common.transport_stream_id == s->transport_stream_id &&
						service->media.frequency == (unsigned long)frequency )
					{
						dprintf("%s: parse pmt filter ts %d, pid %d, service %d, media %d\n", __FUNCTION__,
								service->common.transport_stream_id,
								service->program_map.program_map_PID,
								service->common.service_id,
								service->common.media_id);
						updated = parse_pmt(services, (unsigned char *)s->buf,
											service->common.transport_stream_id, MEDIA_ID, &media);
					}
					cur = cur->next;
				}
				mysem_release(dvb_semaphore);
				break;

			case 0x40:
				verbose("%s: NIT 0x%04x for service 0x%04x\n", __FUNCTION__, s->pid, table_id_ext);
				if(appControlInfo.dvbCommonInfo.networkScan) {
					mysem_get(dvb_semaphore);
					updated = parse_nit(services, (unsigned char *)s->buf, MEDIA_ID, &media, &dvb_scan_network);
					mysem_release(dvb_semaphore);
				}
				break;

			case 0x42:
			case 0x46:
				verbose("%s: SDT (%s TS)\n", __FUNCTION__, table_id == 0x42 ? "actual":"other");
				mysem_get(dvb_semaphore);
				//parse_sdt(services, (unsigned char *)s->buf, MEDIA_ID, &media);
				updated = parse_sdt_with_nit(services, (unsigned char *)s->buf, MEDIA_ID, &media, NULL);
				mysem_release(dvb_semaphore);
				break;
			case 0x4E:
			case 0x50:
			case 0x51:
			case 0x52:
			case 0x53:
			case 0x54:
			case 0x55:
			case 0x56:
			case 0x57:
			case 0x58:
			case 0x59:
			case 0x5A:
			case 0x5B:
			case 0x5C:
			case 0x5D:
			case 0x5E:
			case 0x5F:
				verbose("%s: EIT 0x%04x for service 0x%04x\n", __FUNCTION__, s->pid, table_id_ext);
				mysem_get(dvb_semaphore);
				updated = parse_eit(services, (unsigned char *)s->buf, MEDIA_ID, &media);
				if(updated) {
					s->timeout = 5;
				}
				mysem_release(dvb_semaphore);
				can_count_sections = 0;
				break;
			case stag_atsc_terrestrial_virtual_channel:
			case stag_atsc_cable_virtual_channel:
				mysem_get(dvb_semaphore);
				parse_atsc_section(services, MEDIA_ID, s->buf, ((s->buf[1] & 0x0f) << 8) | s->buf[2], s->pid);
				mysem_release(dvb_semaphore);
			default:
				dprintf("%s: Unknown 0x%04x for service 0x%04x", __FUNCTION__, s->pid, table_id_ext);
		};

		// FIXME: This is the only quick solution I see now, because each service in EIT has its own last_section_number
		// we need to count services and track each of them...
		if(can_count_sections) {
			int i;

			for(i = 0; i <= last_section_number; i++) {
				if(get_bit (s->section_done, i) == 0) {
					break;
				}
			}

			if(i > last_section_number) {
				dprintf("%s: Finished pid 0x%04x for table 0x%04x\n", __FUNCTION__, s->pid, s->table_id);
				s->sectionfilter_done = 1;
			}
		}
	}

	if(updated) {
		s->was_updated = 1;
	}

	dprintf("%s: parsed media %d,  0x%04X table 0x%02X, updated %d, done %d\n", __FUNCTION__,
			MEDIA_ID, s->pid, table_id, updated, s->sectionfilter_done);

	if(s->sectionfilter_done) {
		return 1;
	}

	return 0;
}

static int32_t dvb_sectionRead(__u32 frequency, struct section_buf *s)
{
	int section_length, count;

	if(s->sectionfilter_done) {
		return 1;
	}

	/* the section filter API guarantess that we get one full section
	 * per read(), provided that the buffer is large enough (it is)
	 */
	if(((count = read(s->fd, s->buf, sizeof(s->buf))) < 0) && (errno == EOVERFLOW)) {
		count = read(s->fd, s->buf, sizeof(s->buf));
	}
	if(count < 0 && errno != EAGAIN) {
		dprintf("%s: Read error %d (errno %d) pid 0x%04x\n", __FUNCTION__, count, errno, s->pid);
		return -1;
	}

	//dprintf("%s: READ %d bytes!!!\n", __FUNCTION__, count);
	if(count < 4) {
		return -1;
	}

	section_length = ((s->buf[1] & 0x0f) << 8) | s->buf[2];
	//dprintf("%s: READ %d bytes, section length %d!!!\n", __FUNCTION__, count, section_length);
	if(count != section_length + 3) {
#warning TODO: Handle if we read more data (several sections)
		return -1;
	}

	if(dvb_sectionParse(frequency, s) == 1) {
		return 1;
	}

	return 0;
}

static void dvb_filtersRead(__u32 frequency)
{
	struct list_head *pos;
	struct list_head *n;
	struct pollfd *fds;
	nfds_t nfds;
//	int32_t ret;

	mysem_get(dvb_filter_semaphore);

	dvb_getPollFds(&fds, &nfds);
	if(poll(fds, nfds, 1000) == -1) {
		PERROR("%s: filter poll failed", __FUNCTION__);
	}

	list_for_each_safe(pos, n, &running_filters) {
		int32_t done = 0;
		struct section_buf *s = list_entry(pos, struct section_buf, list);

		if(!s) {
			fatal("%s: s is NULL\n", __FUNCTION__);
			continue;
		}

		if(s->poll_fd && s->poll_fd->revents && (dvb_sectionRead(frequency, s) == 1)) {
			done = 1;
		}

		if(done) {
			dvb_filterRemove(s);
		} else if(time(NULL) > (s->start_time + s->timeout)) {
			SCAN_MESSAGE("DVB: filter timeout pid 0x%04x\n", s->pid);
			debug("%s: filter timeout pid 0x%04x\n", __FUNCTION__, s->pid);
			dvb_filterRemove(s);
		}
		dvb_filterPushWaiters();
	}

	mysem_release(dvb_filter_semaphore);
}

/*
static void dvb_filterServices(list_element_t **head)
{
	list_element_t *service_element;
	mysem_get(dvb_semaphore);
	service_element = *head;
	while (service_element != NULL)
	{
		EIT_service_t *service = (EIT_service_t *)service_element->data;
		if (!dvb_hasMediaNB(service))
		{
			dprintf("DVB: Service without media: id 0x%04X, name '%s'\n",
			        service->common.service_id, service->service_descriptor.service_name);
			service_element = remove_element(head, service_element);
		} else
		{
			service_element = service_element->next;
		}
	}
	mysem_release(dvb_semaphore);
}
*/

static void dvb_scanForServices(long frequency, tunerFormat tuner, uint32_t enableNit)
{
	struct section_buf pat_filter;
	struct section_buf sdt_filter;
	struct section_buf tvct_filter;
	struct section_buf cvct_filter;
	//struct section_buf sdt1_filter;
	struct section_buf eit_filter;
	struct section_buf nit_filter;

	dvb_filtersUnlock();
	/**
	 *  filter timeouts > min repetition rates specified in ETR211
	 */
	dvb_filterSetup(&pat_filter, tuner, 0x00, 0x00, 5, &dvb_services); /* PAT */
	//dvb_filterSetup(&sdt1_filter, tuner, 0x11, 0x46, 5, &dvb_services); /* SDT other */
	dvb_filterSetup(&eit_filter, tuner, 0x12,   -1, 5, &dvb_services); /* EIT */

	dvb_filterAdd(&pat_filter);
	//dvb_filterAdd(&sdt1_filter);
	dvb_filterAdd(&eit_filter);

	if((dvb_getType(tuner) == SYS_ATSC) || (dvb_getType(tuner) == SYS_DVBC_ANNEX_B)) {
		dvb_filterSetup(&tvct_filter, tuner, 0x1ffb, stag_atsc_terrestrial_virtual_channel, 5, &dvb_services); //Terrestrial Virtual Channel Table (TVCT)
		dvb_filterAdd(&tvct_filter);
		dvb_filterSetup(&cvct_filter, tuner, 0x1ffb, stag_atsc_cable_virtual_channel, 5, &dvb_services); //Cable Virtual Channel Table (CVCT)
		dvb_filterAdd(&cvct_filter);
	} else {
		dvb_filterSetup(&sdt_filter, tuner, 0x11, 0x42, 5, &dvb_services); /* SDT actual */
		dvb_filterAdd(&sdt_filter);
	}

	if(enableNit) {
		dvb_clearNIT(&dvb_scan_network);
		dvb_filterSetup(&nit_filter, tuner, 0x10, 0x40, 5, &dvb_services); /* NIT */
		dvb_filterAdd(&nit_filter);
	}

	do {
		dvb_filtersRead(frequency);
	} while(!list_empty(&running_filters) || !list_empty(&waiting_filters));
	dvb_filtersLock();
	dvb_filtersFlush();

	//dvb_filterServices(&dvb_services);
}

int dvb_checkDelSysSupport(tunerFormat tuner, fe_delivery_system_t delSys)
{
	int i;
	for(i = 0; i < appControlInfo.tunerInfo[tuner].delSysCount; i++) {
		if(appControlInfo.tunerInfo[tuner].delSys[i] == delSys) {
			return 1;
		}
	}
	return 0;
}

int dvb_isCurrentDelSys_dvbt2(tunerFormat tuner)
{
#ifdef DTV_DELIVERY_SYSTEM
	if(!dvb_checkDelSysSupport(tuner, SYS_DVBT2)) {
		return 0;
	}

	int fdf = dvb_openFrontend(dvb_getAdapter(tuner), O_RDONLY);
	if (fdf >= 0) {
		struct dtv_property p = { .cmd = DTV_DELIVERY_SYSTEM, };
		struct dtv_properties cmdseq = { .num = 1, .props = &p, };
		if(ioctl(fdf, FE_GET_PROPERTY, &cmdseq) >= 0 && p.u.data == SYS_DVBT2) {
			close(fdf);
			return 1;
		}
		close(fdf);
	}
#endif
	return 0;
}

void dvb_scanForEPG( tunerFormat tuner, uint32_t frequency )
{
	int adapter = dvb_getAdapter(tuner);
	struct section_buf eit_filter;
	int counter = 0;

	if(!dvb_isLinuxAdapter(adapter)) {
		dvb_filtersUnlock();
	}

	dvb_filterSetup(&eit_filter, tuner, 0x12, -1, 2, &dvb_services); /* EIT */
	eit_filter.running_counter = &counter;
	dvb_filterAdd(&eit_filter);
	do {
		dvb_filtersRead(frequency);
		//dprintf("DVB: eit %d, counter %d\n", eit_filter.start_time, counter);
		usleep(1); // pass control to other threads that may want to call dvb_filtersRead
	} while((eit_filter.start_time == 0) || (counter > 0));
	//dvb_filterServices(&dvb_services);

	if(eit_filter.was_updated) {
		/* Write the channel list to the root file system */
		dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
	}
}

void dvb_scanForPSI( tunerFormat tuner, uint32_t frequency, list_element_t **out_list )
{
	int adapter = dvb_getAdapter(tuner);

	if(!dvb_isLinuxAdapter(adapter)) {
		eprintf("%s[%d]: unsupported\n", __FUNCTION__, tuner);
		return;
	}

#ifdef LINUX_DVB_API_DEMUX
	struct section_buf pat_filter;
	int counter = 0;

	if(out_list == NULL) {
		out_list = &dvb_services;
	}

	dvb_filterSetup(&pat_filter, tuner, 0x00, 0x00, 2, out_list); /* PAT */
	pat_filter.running_counter = &counter;
	dvb_filterAdd(&pat_filter);
	do {
		dvb_filtersRead(frequency);
		//dprintf("DVB: pat %d, counter %d\n", pat_filter.start_time, counter);
		usleep(1); // pass control to other threads that may want to call dvb_filtersRead
	} while (pat_filter.start_time == 0 || counter > 0);
#endif // LINUX_DVB_API_DEMUX
	//dvb_filterServices(out_list);
}

int dvb_diseqcSetup(tunerFormat tuner, int frontend_fd, uint32_t frequency, EIT_media_config_t *media)
{
	if(appControlInfo.tunerInfo[tuner].type != SYS_DVBS ||
		appControlInfo.dvbsInfo.diseqc.type == 0)
	{
		return 0;
	}
	if (appControlInfo.dvbsInfo.diseqc.uncommited) {
		uint8_t ucmd[4] = { 0xe0, 0x10, 0x39, appControlInfo.dvbsInfo.diseqc.uncommited-1 };
		dvb_diseqcSend(tuner, frontend_fd, ucmd, 4);
	}
	int is_vertical = media ? media->dvb_s.polarization == 0x01 : appControlInfo.dvbsInfo.polarization != 0;
	int port = appControlInfo.dvbsInfo.diseqc.type == diseqcSwitchMulti ?
	           appControlInfo.dvbsInfo.diseqc.port & 1 :
	           appControlInfo.dvbsInfo.diseqc.port;
	uint8_t data_hi = appControlInfo.dvbsInfo.diseqc.type == diseqcSwitchMulti ? 0x70 : 0xF0;
	uint8_t cmd[4] = { 0xe0, 0x10, 0x38, data_hi | diseqc_data_lo(port, is_vertical, frequency) };

	dvb_diseqcSend(tuner, frontend_fd, cmd, 4);
	return 0;
}

int dvb_diseqcSend(tunerFormat tuner, int frontend_fd, const uint8_t* tx, size_t tx_len)
{
	struct dvb_diseqc_master_cmd cmd;

	dprintf("%s: sending %d: %02x %02x %02x %02x %02x %02x\n", __FUNCTION__, tx_len, tx[0], tx[1], tx[2],
		tx_len > 3 ? tx[3] : 0, tx_len > 4 ? tx[4] : 0, tx_len > 5 ? tx[5] : 0);

	if(dvb_isLinuxTuner(tuner)) {
		cmd.msg_len = tx_len;
		memcpy(cmd.msg, tx, cmd.msg_len);
		ioctl_or_abort(tuner, frontend_fd, FE_DISEQC_SEND_MASTER_CMD, &cmd);
	} else {
		st_sendDiseqc(tuner, tx, tx_len);
	}

	return 0;
}

#define SLEEP_QUANTUM 50000
#define BREAKABLE_SLEEP(us)										\
	do {														\
		for (int _s=0;_s<us;_s+=SLEEP_QUANTUM) {				\
			if (pFunction != NULL && pFunction() == -1)			\
				return -1;										\
			ioctl_or_abort(tuner, frontend_fd, FE_READ_STATUS, &s); \
			if (s & FE_HAS_LOCK) break;							\
			usleep(SLEEP_QUANTUM);								\
		}														\
	} while(0)													\

int dvb_initFrontend(tunerFormat tuner)
{
	int fd;
	struct dtv_properties		dtv_prop;
	struct dvb_frontend_info	info;
	fe_delivery_system_t		current_sys;
	struct dtv_property			dvb_prop[DTV_MAX_COMMAND];
	uint8_t i;

	if((fd = dvb_getFrontendInfo(dvb_getAdapter(tuner), O_RDONLY, &info)) < 0) {
	    close(fd);
	    return -1;
	}

	dvb_prop[0].cmd = DTV_API_VERSION;
	dvb_prop[1].cmd = DTV_DELIVERY_SYSTEM;

	dtv_prop.num = 2;
	dtv_prop.props = dvb_prop;

	/* Detect a DVBv3 device */
	if (ioctl(fd, FE_GET_PROPERTY, &dtv_prop) == -1) {
		dvb_prop[0].u.data = 0x300;
		dvb_prop[1].u.data = SYS_UNDEFINED;
	}

	appControlInfo.dvbApiVersion = dvb_prop[0].u.data;
	current_sys = dvb_prop[1].u.data;

	eprintf("DVB API Version %d.%d, Current v5 delivery system: %d\n",
		appControlInfo.dvbApiVersion / 256,
		appControlInfo.dvbApiVersion % 256,
		current_sys);

	if (appControlInfo.dvbApiVersion < 0x505) {
		if(strstr(info.name, "CXD2820R") ||
			strstr(info.name, "MN88472"))
		{
			appControlInfo.tunerInfo[tuner].delSysCount = 3;
			appControlInfo.tunerInfo[tuner].delSys[0] = SYS_DVBC_ANNEX_AC;
			appControlInfo.tunerInfo[tuner].delSys[1] = SYS_DVBT;
			appControlInfo.tunerInfo[tuner].delSys[2] = SYS_DVBT2;
		} else {
			appControlInfo.tunerInfo[tuner].delSysCount = 1;
			appControlInfo.tunerInfo[tuner].delSys[0] = table_IntIntLookup(type_to_delsys, info.type, SYS_DVBT);
		}
	} else {
#ifdef DTV_ENUM_DELSYS
		dvb_prop[0].cmd = DTV_ENUM_DELSYS;
		dtv_prop.num = 1;
		dtv_prop.props = dvb_prop;
		if (ioctl(fd, FE_GET_PROPERTY, &dtv_prop) == -1) {
			close(fd);
			return -1;
		}
		appControlInfo.tunerInfo[tuner].delSysCount = dvb_prop[0].u.buffer.len;

		if(appControlInfo.tunerInfo[tuner].delSysCount == 0) {
			close(fd);
			return -1;
		}
		for(i = 0; i < appControlInfo.tunerInfo[tuner].delSysCount; i++) {
			appControlInfo.tunerInfo[tuner].delSys[i] = dvb_prop[0].u.buffer.data[i];
		}

#endif
	}

//	appControlInfo.tunerInfo[tuner].type = table_IntIntLookup(type_to_delsys, info.type, SYS_UNDEFINED);
	appControlInfo.tunerInfo[tuner].type = current_sys;
	dvbInstances[dvb_getAdapter(tuner)].fe_type = current_sys;

	appControlInfo.tunerInfo[tuner].status = tunerInactive;
	eprintf("%s[%d]: %s (%s) supported types: ", __FUNCTION__, dvb_getAdapter(tuner),
		info.name, fe_typeNames[info.type]);
	for(i = 0; i < appControlInfo.tunerInfo[tuner].delSysCount; i++) {
		printf("%u ", appControlInfo.tunerInfo[tuner].delSys[i]);
	}
	printf("\n");

	close(fd);
	return 0;
}

uint32_t dvb_getBandwidth_kHz(fe_bandwidth_t bw)
{
	switch(bw)
	{
	  case BANDWIDTH_8_MHZ:
	    return 8000000;
	  case BANDWIDTH_7_MHZ:
	    return 7000000;
	  case BANDWIDTH_6_MHZ:
	    return 6000000;
	  case BANDWIDTH_5_MHZ:
	    return 5000000;
	  case BANDWIDTH_10_MHZ:
	    return 10000000;
	  case BANDWIDTH_1_712_MHZ:
	    return 1712000;
	  default:
	    return 0;//AUTO
	}
}

static int dvb_setParam(fe_delivery_system_t type, __u32 frequency, tunerFormat tuner, int frontend_fd,
                            int wait_for_lock, EIT_media_config_t *media, dvb_cancelFunctionDef* pFunction)
{
	if(dvb_isLinuxTuner(tuner)) {
		uint8_t makeTune = 1;

		fe_delivery_system_t currentType = dvbParamState.delSys;//appControlInfo.tunerInfo[tuner].type;

		if((type != currentType) && (dvb_setFrontendType(dvb_getAdapter(tuner), type, frontend_fd) != 0)) {
			eprintf("%s[%d]: Watch failed to set frontend type for current service\n", __FUNCTION__, dvb_getAdapter(tuner));
			return -1;
		}
		if(appControlInfo.dvbCommonInfo.streamerInput)
			return 0;

		eprintf("%s[%d]: Current frequency %u, new frequency %u\n", __FUNCTION__, tuner, currentFrequency[tuner], frequency);

		struct dtv_property dtv[16];//check if it enough
		struct dtv_properties cmdseq;

		if(media && media->type == serviceMediaNone) {
			media = NULL;
		}
		if(((type == SYS_DVBT) || (type == SYS_DVBT2)) && (media == NULL || media->type == serviceMediaDVBT)) {
			fe_bandwidth_t bandwidth = (media != NULL) ? media->dvb_t.bandwidth : appControlInfo.dvbtInfo.bandwidth;
			uint32_t int_bandwidth = (dvb_getBandwidth_kHz(bandwidth) == 0) ? 3 : dvb_getBandwidth_kHz(bandwidth);
			uint32_t inversion = appControlInfo.dvbtInfo.fe.inversion;

			dvbParamState.dvbtInfo.bandwidth = bandwidth;

#ifdef DTV_STREAM_ID
			uint8_t plp_id = media ? media->dvb_t.plp_id : appControlInfo.dvbtInfo.plp_id;
			uint8_t generation = media ? media->dvb_t.generation : appControlInfo.dvbtInfo.generation;
			if((plp_id == dvbParamState.dvbtInfo.plp_id) && (frequency == dvbParamState.frequency)) {
				makeTune = 0;
			}

			dvbParamState.dvbtInfo.plp_id = plp_id;
			dvbParamState.dvbtInfo.generation = generation;

			struct dtv_property dtv_plp = { .cmd = DTV_STREAM_ID, .u.data = plp_id,};

			struct dtv_properties cmdseq_plp = { .num = 1, .props = &dtv_plp, };
			if (ioctl(frontend_fd, FE_SET_PROPERTY, &cmdseq_plp) == -1) {
				eprintf("%s[%d]: set %u plp failed: %s\n", __FUNCTION__, tuner, plp_id, strerror(errno));
				appControlInfo.dvbtInfo.plp_id = 0;
				return -1;
			}
			eprintf("   T: plp %u, generation %u\n", plp_id, generation);
#endif

			dtv[0].cmd = DTV_FREQUENCY; 		dtv[0].u.data = frequency;
			dtv[1].cmd = DTV_INVERSION; 		dtv[1].u.data = inversion;
			dtv[2].cmd = DTV_BANDWIDTH_HZ; 		dtv[2].u.data = int_bandwidth;
			dtv[3].cmd = DTV_CODE_RATE_HP; 		dtv[3].u.data = FEC_AUTO;
			dtv[4].cmd = DTV_CODE_RATE_LP; 		dtv[4].u.data = FEC_AUTO;
			dtv[5].cmd = DTV_MODULATION; 		dtv[5].u.data = QAM_AUTO;
			dtv[6].cmd = DTV_TRANSMISSION_MODE;	dtv[6].u.data = TRANSMISSION_MODE_AUTO;
			dtv[7].cmd = DTV_GUARD_INTERVAL; 	dtv[7].u.data = GUARD_INTERVAL_AUTO;
			dtv[8].cmd = DTV_HIERARCHY; 		dtv[8].u.data = HIERARCHY_AUTO;
			dtv[9].cmd = DTV_TUNE;

			cmdseq.num = 10;

			eprintf("   T: bandwidth %u, invertion %u\n", int_bandwidth, inversion);
		} else if((type == SYS_DVBC_ANNEX_AC) && (media == NULL || media->type == serviceMediaDVBC)) {
			uint32_t modulation;
			uint32_t symbol_rate;
			uint32_t inversion = appControlInfo.dvbcInfo.fe.inversion;
			if(media != NULL) {
				modulation  = media->dvb_c.modulation;
				symbol_rate = media->dvb_c.symbol_rate;
			} else {
				modulation  = appControlInfo.dvbcInfo.modulation;
				symbol_rate = appControlInfo.dvbcInfo.symbolRate * KHZ;
			}

			dvbParamState.dvbcInfo.modulation = modulation;
			dvbParamState.dvbcInfo.symbolRate = symbol_rate;

			dtv[0].cmd = DTV_FREQUENCY; 		dtv[0].u.data = frequency;
			dtv[1].cmd = DTV_MODULATION; 		dtv[1].u.data = modulation;
			dtv[2].cmd = DTV_SYMBOL_RATE; 		dtv[2].u.data = symbol_rate;
			dtv[3].cmd = DTV_INVERSION; 		dtv[3].u.data = inversion;
			dtv[4].cmd = DTV_INNER_FEC; 		dtv[4].u.data = FEC_NONE;
			dtv[5].cmd = DTV_TUNE;

			cmdseq.num = 6;
			eprintf("   C: Symbol rate %u, modulation %u invertion %u\n",
				symbol_rate, modulation, inversion);
		} else if((type == SYS_DVBS) && (media == NULL || media->type == serviceMediaDVBS)) {
			uint32_t inversion = 0;
			fe_modulation_t modulation = QPSK;
			uint32_t polarization = (media != NULL) ? media->dvb_s.polarization : appControlInfo.dvbsInfo.polarization;
			uint32_t symbol_rate = (media != NULL) ? media->dvb_s.symbol_rate : appControlInfo.dvbsInfo.symbolRate * KHZ;
			uint32_t freqLO;
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

			if(polarization == dvbParamState.dvbsInfo.polarization) {
				makeTune = 0;
			}

			dvbParamState.dvbsInfo.band = appControlInfo.dvbsInfo.band;
			dvbParamState.dvbsInfo.diseqc = appControlInfo.dvbsInfo.diseqc;
			dvbParamState.dvbsInfo.polarization = polarization;
			dvbParamState.dvbsInfo.symbolRate = symbol_rate;

			dtv[0].cmd = DTV_FREQUENCY; 		dtv[0].u.data = frequency;
			dtv[1].cmd = DTV_INVERSION; 		dtv[1].u.data = inversion;
			dtv[2].cmd = DTV_MODULATION; 		dtv[2].u.data = modulation;
			dtv[3].cmd = DTV_SYMBOL_RATE; 		dtv[3].u.data = symbol_rate;
			dtv[4].cmd = DTV_VOLTAGE;			dtv[4].u.data = polarization;
			dtv[5].cmd = DTV_TONE;				dtv[5].u.data = tone;
			dtv[6].cmd = DTV_TUNE;

			cmdseq.num = 4;
			eprintf("   S: Symbol rate %u\n", symbol_rate);

			dvb_diseqcSetup(tuner, frontend_fd, frequency, media);
		} else if(((type == SYS_ATSC) || (type == SYS_DVBC_ANNEX_B)) /*&& (media == NULL || media->type == serviceMediaATSC)*/) {
			uint32_t modulation = media ? media->atsc.modulation : appControlInfo.atscInfo.modulation;
			if(modulation == 0) {
				modulation = VSB_8;
			}

			dvbParamState.atscInfo.modulation = modulation;

			dtv[0].cmd = DTV_FREQUENCY; 		dtv[0].u.data = frequency;
			dtv[1].cmd = DTV_MODULATION; 		dtv[1].u.data = modulation;
			dtv[2].cmd = DTV_TUNE;

			cmdseq.num = 3;
		} else {
			eprintf("%s[%d]: ERROR: Unsupported frontend type=%s (media %d).\n", __FUNCTION__, tuner,
				fe_typeNames[table_IntIntLookupR(type_to_delsys, type, FE_QPSK)], media ? (int)media->type : -1);
			return -1;
		}
		cmdseq.props = dtv;

		if (makeTune) {
			ioctl_or_abort(tuner, frontend_fd, FE_SET_PROPERTY, &cmdseq);
		}

		dvbParamState.frequency = frequency;

		if (!wait_for_lock || appControlInfo.dvbCommonInfo.adapterSpeed < 0) {
			currentFrequency[tuner] = frequency;
			return 0;
		}

		fe_status_t s;
		int hasSignal = 0;
		int hasLock   = 0;
		__u32 ber = BER_THRESHOLD;
#ifdef STBTI
		int timeout = (appControlInfo.dvbCommonInfo.adapterSpeed+1)*10;
#else
		int timeout = (appControlInfo.dvbCommonInfo.adapterSpeed+1);
#endif
		do {
			ioctl_or_abort(tuner, frontend_fd, FE_READ_BER, &ber);
			// If ber is not -1, then wait a bit more 
			if (ber == 0xffffffff) {
				dprintf("%s[%d]: All clear...\n", __FUNCTION__, tuner);
				BREAKABLE_SLEEP(100000);
			} else
			{
				eprintf("%s[%d]: Something is out there... (ber %u)\n", __FUNCTION__, tuner, ber);
				BREAKABLE_SLEEP(500000);
			}

			ioctl_or_abort(tuner, frontend_fd, FE_READ_STATUS, &s);
			if (s & FE_HAS_LOCK) {
				if (!hasLock)
					dprintf("%s[%d]: L\n", __FUNCTION__, tuner);
				else
					break;
				hasLock = 1;
				// locked, give adapter even more time... 
				//usleep (appControlInfo.dvbCommonInfo.adapterSpeed*10000);
			} else
			if (s & FE_HAS_SIGNAL) {
				if (hasSignal == 0) {
					eprintf("%s[%d]: Has signal\n", __FUNCTION__, tuner);
					timeout += appControlInfo.dvbCommonInfo.adapterSpeed;
					hasSignal = 1;
				}
				dprintf("%s[%d]: S (%d)\n", __FUNCTION__, tuner, timeout);
				// found something above the noise level, increase timeout time 
				//usleep (appControlInfo.dvbCommonInfo.adapterSpeed*10000);
			} else
			{
				dprintf("%s[%d]: N (%d)\n", __FUNCTION__, tuner, timeout);
				// there's no and never was any signal, reach timeout faster 
				if (hasSignal == 0)
				{
					eprintf("%s[%d]: Skip\n", __FUNCTION__, tuner);
					--timeout;
				}
			}
		} while (--timeout > 0 && (!ber || ber >= BER_THRESHOLD));
		dprintf("%s[%d]: %u timeout %d, ber %u\n", __FUNCTION__, tuner, frequency, timeout, ber);
		currentFrequency[tuner] = frequency;
		eprintf("%s[%d]: Frequency set, lock: %d\n", __FUNCTION__, tuner, hasLock);
		return hasLock;
	} else {
		uint32_t	freqKHz = st_frequency(tuner, frequency);
		elcdRpcType_t	type = elcdRpcInvalid;
		int32_t			tuneSuccess = 0;
		cJSON			*result = NULL;
		cJSON			*params = NULL;

		dvb_diseqcSetup(tuner, -1, frequency, media);

		params = cJSON_CreateObject();
		if(!params) {
			eprintf("%s[%d]: out of memory\n", __FUNCTION__, tuner);
			return -1;
		}
		st_setTuneParams(tuner, params, media);
		cJSON_AddItemToObject(params, "frequency", cJSON_CreateNumber(freqKHz));
		st_rpcSync(elcmd_dvbtune, params, &type, &result);
		if(result && result->valuestring && (strcmp(result->valuestring, "ok") == 0)) {
			tuneSuccess = 1;
		}
		cJSON_Delete(params);
		cJSON_Delete(result);
		return tuneSuccess;
	}
	return 0;
}
#undef BREAKABLE_SLEEP

static int dvb_checkFrequency(fe_delivery_system_t type, __u32 * frequency, int frontend_fd, tunerFormat tuner,
                              __u32* ber, __u32 fe_step_size, EIT_media_config_t *media,
                              dvb_cancelFunctionDef* pFunction)
{
	int res;

	currentFrequency[tuner] = 0;

	if ((res = dvb_setParam(type, *frequency, tuner, 1, frontend_fd, media, pFunction)) == 0)
	{
		int i;
		for (i = 0; i < 10; i++)
		{
			fe_status_t s;

			struct dtv_property dtv = {.cmd = DTV_FREQUENCY, };
			struct dtv_properties cmdseq = {.num = 1 , .props = &dtv};

			while (1)
			{
				// Get the front-end state including current freq. setting.
				// Some drivers may need to be modified to update this field.
				ioctl_or_abort(tuner, frontend_fd, FE_GET_PROPERTY, &cmdseq);
				ioctl_or_abort(tuner, frontend_fd, FE_READ_STATUS,  &s);

				dprintf("%s[%d]: f_orig=%u   f_read=%u status=%02x\n", __FUNCTION__, tuner,
				        *frequency, (long) dtv.u.data, s);
				// in theory a clever FE could adjust the freq. to give a perfect freq. needed !
				if (dtv.u.data == *frequency)
				{
					break;
				}
				else
				{
#ifdef DEBUG
					long gap = (dtv.u.data > *frequency) ?
					            dtv.u.data - *frequency  :
					              *frequency - dtv.u.data;
					dprintf("%s[%d]: gap = %06ld fe_step_size=%06ld\n", __func__, tuner, gap, fe_step_size);
					// if (gap < fe_step_size) // may lead to hangs, so disabled
						// FE thinks its done a better job than this app. !
#endif
					break;
				}
				usleep (20000);
			}

			if (s & FE_HAS_LOCK)
			{
				ioctl_or_abort(tuner, frontend_fd, FE_READ_BER, ber);
				dprintf("%s[%d]: Got LOCK f=%u\n", __FUNCTION__, tuner, *frequency);
				return 1;
			}
		}
		dprintf("%s[%d]: No Lock f=%u\n", __FUNCTION__, tuner, *frequency);
	} else if (res == 1)
	{
		dprintf("%s[%d]: Trust LOCK f=%u\n", __FUNCTION__, tuner, *frequency);

		currentFrequency[tuner] = 0;
		return 1;
	} else if (res == -1)
	{
		dprintf("%s[%d]: Fail or user abort f=%u\n", __FUNCTION__, tuner, *frequency);
		return -1;
	}

	currentFrequency[tuner] = 0;
	return 0;
}

char *dvb_getTempServiceName(int index)
{
	list_element_t *service_element;
	EIT_service_t *service;
	int i = 0;

	service_element = dvb_services;
	while( service_element != NULL )
	{
		if (dvb_hasMedia(service_element->data))
		{
			if (i == index)
			{
				service = (EIT_service_t *)service_element->data;
				if (service->common.service_id)
				{
					if (service->service_descriptor.service_name[0])
					{
						return (char *)&service->service_descriptor.service_name[0];
					}
					return "N/A";
				}
				return NULL;
			}
			i++;
		}
		service_element = service_element->next;
	}
	return NULL;
}

void dvb_exportServiceList(char* filename)
{
	dprintf("%s: in\n", __FUNCTION__);
	mysem_get(dvb_semaphore);
	dump_services(dvb_services, filename );
	mysem_release(dvb_semaphore);
	dprintf("%s: out\n", __FUNCTION__);
}

void dvb_clearNIT(NIT_table_t *nit)
{
	free_transport_streams(&nit->transport_streams);
	memset(nit, 0, sizeof(NIT_table_t));
}

void dvb_clearServiceList(int permanent)
{
	dprintf("%s: %d\n", __FUNCTION__, permanent);
	mysem_get(dvb_semaphore);
	free_services(&dvb_services);
	mysem_release(dvb_semaphore);
	if(permanent) {
#ifdef STSDK
		elcdRpcType_t type;
		cJSON *result = NULL;
		st_rpcSync(elcmd_dvbclearservices, NULL, &type, &result);
		cJSON_Delete(result);
#endif
		remove(appControlInfo.dvbCommonInfo.channelConfigFile);
	}
}

int dvb_readServicesFromDump(char* filename)
{
	int res;
	mysem_get(dvb_semaphore);
	free_services(&dvb_services);
	res = services_load_from_dump(&dvb_services, filename);
	mysem_release(dvb_semaphore);
	return res;
}

const char *dvb_getTypeName(tunerFormat tuner)
{
	if ((appControlInfo.tunerInfo[tuner].type == SYS_DVBT) ||
		(appControlInfo.tunerInfo[tuner].type == SYS_DVBT2))
		return "DVB-T/T2";
	return fe_typeNames[table_IntIntLookupR(type_to_delsys, appControlInfo.tunerInfo[tuner].type, FE_QPSK)];
}

int dvb_setFrontendType(int adapter, fe_delivery_system_t type, int frontend_fd)
{
#ifdef DTV_DELIVERY_SYSTEM
//	int fdf;
	tunerFormat tuner = offair_getTuner();
	struct dtv_property p = { .cmd = DTV_DELIVERY_SYSTEM, };
	struct dtv_properties cmdseq = {
		.num = 1,
		.props = &p
	};
	int local_fd = frontend_fd;

	p.u.data = type;

	if(local_fd < 0) {
		local_fd = dvb_openFrontend(adapter, O_RDWR);
		if(local_fd < 0) {
			return -1;
		}
	}

	if(ioctl(local_fd, FE_SET_PROPERTY, &cmdseq) == -1) {
		eprintf("%s: set property failed: %s\n", __FUNCTION__, strerror(errno));
//		close(local_fd);
		return -1;
	}

	if(frontend_fd < 0) {
		close(local_fd);
	}
	appControlInfo.tunerInfo[tuner].type = type;
	dvbInstances[adapter].fe_type = type;
	dvbParamState.delSys = type;

	return 0;
#else
	return -1;
#endif
}

int dvb_toggleType(tunerFormat tuner)
{
	fe_delivery_system_t cur_type = dvb_getType(tuner);
	fe_delivery_system_t next_type;
	uint32_t i;

	for(i = 0; i < appControlInfo.tunerInfo[tuner].delSysCount; i++) {
		if(appControlInfo.tunerInfo[tuner].delSys[i] == cur_type) {
			break;
		}
	}
	if(i == appControlInfo.tunerInfo[tuner].delSysCount) {
		return -1;
	}

	while(1) {
		i = (i + 1) % appControlInfo.tunerInfo[tuner].delSysCount;
		next_type = appControlInfo.tunerInfo[tuner].delSys[i];
		if(next_type == cur_type) {
			return 0;
		}

		// DVBT2 is supports, but don't process, because we need toggle between DVBT and DVBC type only.
		if(next_type != SYS_DVBT2) {
			break;
		}
	}

	return dvb_setFrontendType(dvb_getAdapter(tuner), next_type, -1);
}

int dvb_getTuner_freqs(tunerFormat tuner, __u32 * low_freq, __u32 * high_freq, __u32 * freq_step)
{
	switch (dvb_getType(tuner)) {
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
			if (appControlInfo.dvbsInfo.band == dvbsBandC)
			{
				*low_freq  = (appControlInfo.dvbsInfo.c_band.lowFrequency  * KHZ);
				*high_freq = (appControlInfo.dvbsInfo.c_band.highFrequency * KHZ);
				*freq_step = (appControlInfo.dvbsInfo.c_band.frequencyStep * KHZ);
			} else
			{
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

static int dvb_openFrontend(int adapter, int flags)
{
	char frontend_devname[32];
	int frontend_fd;
	snprintf(frontend_devname, sizeof(frontend_devname), "/dev/dvb/adapter%d/frontend0", adapter);

	frontend_fd = open(frontend_devname, flags);
	if(frontend_fd < 0) {
		snprintf(frontend_devname, sizeof(frontend_devname), "/dev/dvb%d.frontend0", adapter);
		frontend_fd = open(frontend_devname, flags);
		if(frontend_fd < 0) {
//			printf("%s[%d]: %m\n", __FILE__, __LINE__);
			eprintf("%s: failed to open adapter %d\n", __FUNCTION__, adapter);
		}
	}

	return frontend_fd;
}

int dvb_getFrontendInfo(int adapter, int flags, struct dvb_frontend_info *fe_info)
{
	int frontend_fd = dvb_openFrontend(adapter, flags);
	if (frontend_fd < 0) {
		eprintf("%s[%d]: failed to open frontend\n", __FUNCTION__, adapter);
		return -1;
	}
	int err;
	ioctl_loop(adapter, err, frontend_fd, FE_GET_INFO, fe_info);

	eprintf("%s[%d]: DVB Model=%s, Type=%s, FineStepSize=%u\n", __FUNCTION__,
		adapter, fe_info->name, fe_typeNames[fe_info->type], fe_info->frequency_stepsize);
	return frontend_fd;
}

int dvb_getSignalInfo(tunerFormat tuner,
                      uint16_t *snr, uint16_t *signal, uint32_t *ber, uint32_t *uncorrected_blocks)
{
	int adapter = dvb_getAdapter(tuner);
	int frontend_fd = 0;

	if(!dvb_isLinuxAdapter(adapter)) {
		eprintf("%s[%d]: unsupported\n", __FUNCTION__, tuner);
		return 0;
	}

	fe_status_t status = 0;
	mysem_get(dvb_fe_semaphore);

	if((frontend_fd = dvb_openFrontend(adapter, O_RDONLY | O_NONBLOCK)) >= 0) {
		ioctl(frontend_fd, FE_READ_STATUS, &status);
		ioctl(frontend_fd, FE_READ_SIGNAL_STRENGTH, signal);
		ioctl(frontend_fd, FE_READ_SNR, snr);
		ioctl(frontend_fd, FE_READ_BER, ber);
		ioctl(frontend_fd, FE_READ_UNCORRECTED_BLOCKS, uncorrected_blocks);
		close(frontend_fd);
		frontend_fd = (status & FE_HAS_LOCK) == FE_HAS_LOCK;
	} else {
		eprintf("%s: failed opening frontend %d\n", __FUNCTION__, adapter);
#ifdef ENABLE_DVB_PVR
		frontend_fd = 0; // don't return error
#endif
	}
	mysem_release(dvb_fe_semaphore);
	return frontend_fd;
}

int32_t dvb_serviceScan(tunerFormat tuner, dvb_displayFunctionDef* pFunction)
{
	__u32 frequency;
	__u32 low_freq = 0, high_freq = 0, freq_step = 0;
	struct dvb_frontend_info fe_info;
	int32_t ret = 0;

	dvb_getTuner_freqs(tuner, &low_freq, &high_freq, &freq_step);
#ifdef STSDK
	cJSON *params = cJSON_CreateObject();
	if(!params) {
		eprintf("%s[%d]: out of memory\n", __FUNCTION__, tuner);
		return -1;
	}
	st_setTuneParams(tuner, params, NULL);

	cJSON *result = NULL;
	elcdRpcType_t type = elcdRpcInvalid;
	int res = -1;
	if(!dvb_isLinuxTuner(tuner)) {
		cJSON *p_freq = cJSON_CreateNumber(0);
		cJSON_AddItemToObject(params, "frequency", p_freq);

		for(frequency = low_freq; frequency <= high_freq; frequency += freq_step) {
			dvb_diseqcSetup(tuner, -1, frequency, NULL);
			p_freq->valueint = st_frequency(tuner, frequency);
			p_freq->valuedouble = p_freq->valueint;
			dprintf("%s[%d]: Check main freq: %u\n", __FUNCTION__, tuner, frequency);
			if(pFunction != NULL && pFunction(frequency, dvb_getNumberOfServices(), tuner, 0, 0) == -1) {
				dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
				break;
			}
			res = st_rpcSync(elcmd_dvbtune, params, &type, &result);
			if(result && result->valuestring != NULL && strcmp (result->valuestring, "ok") == 0) {
				cJSON_Delete(result);
				result = NULL;

				cJSON_Delete(params);
				params = cJSON_CreateObject();
				st_setTuneParams(tuner, params, NULL);

				dprintf("%s[%d]: Found something on %u, search channels!\n", __FUNCTION__, tuner, frequency);
				res = st_rpcSyncTimeout(elcmd_dvbscan, params, RPC_SCAN_TIMEOUT, &type, &result );
			}
			dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
			cJSON_Delete(result);
			result = NULL;
		}
		cJSON_Delete(params);

		return 0;
	}

	cJSON *p_freq = cJSON_CreateNumber(0);
	if(!p_freq) {
		eprintf("%s[%d]: out of memory\n", __FUNCTION__, tuner);
		ret = -1;
		goto service_scan_end;
	}
	cJSON_AddItemToObject(params, "frequency", p_freq);
#endif //defined STSDK

	dprintf("%s[%d]: Scan tuner %d\n", __FUNCTION__, tuner);

	/* Clear any existing channel list */
	//dvb_clearServiceList();

	if(!appControlInfo.dvbCommonInfo.streamerInput)
	{
		int frontend_fd = dvb_getFrontendInfo(dvb_getAdapter(tuner), O_RDWR, &fe_info);
		if(frontend_fd < 0) {
			eprintf("%s[%d]: failed to open frontend %d\n", __FUNCTION__, tuner, dvb_getAdapter(tuner));
			ret = -1;
			goto service_scan_end;
		}

		// Use the FE's own start/stop freq. for searching (if not explicitly app. defined).
		if (!low_freq)
			low_freq  = fe_info.frequency_min;
		if (!high_freq)
			high_freq = fe_info.frequency_max;

		ZERO_SCAN_MESAGE();

		SCAN_MESSAGE("DVB[%d]: Scan: StartFreq=%u, StopFreq=%u, StepSize=%u\n", tuner,
		             low_freq, high_freq, freq_step);
		SCAN_MESSAGE("==================================================================================\n");

		eprintf     ("%s[%d]: Scan: StartFreq=%u, StopFreq=%u, StepSize=%u\n", __FUNCTION__, tuner,
		             low_freq, high_freq, freq_step);
		eprintf     ("==================================================================================\n");

		/* Go through the range of frequencies */
		for(frequency = low_freq; frequency <= high_freq; frequency += freq_step)
		{
			__u32 match_ber;
			__u32 f = frequency;
			int found;
			dprintf("%s[%d]: Check main freq: %u\n", __FUNCTION__, tuner, f);
			do {
				found = (1 == dvb_checkFrequency(dvb_getType(tuner), &f, frontend_fd, tuner,
								&match_ber, fe_info.frequency_stepsize, NULL, NULL));
				if(appControlInfo.dvbCommonInfo.extendedScan) {
					for(int i=1; !found && (i<=MAX_OFFSETS); i++) {
						__u32 offset = i * fe_info.frequency_stepsize;

						f = frequency - offset;
						verbose("%s[%d]: Check lower freq %u\n", __FUNCTION__, tuner, f);
						found = (1== dvb_checkFrequency(dvb_getType(tuner), &f, frontend_fd, tuner,
									  &match_ber, fe_info.frequency_stepsize, NULL, NULL));
						if(found)
							break;
						f = frequency + offset;
						verbose("%s[%d]: Check higher freq %u\n", __FUNCTION__, tuner, f);
						found = (1 == dvb_checkFrequency(dvb_getType(tuner), &f, frontend_fd, tuner,
									  &match_ber, fe_info.frequency_stepsize, NULL, NULL));
					}
				}
				if(found) {
					eprintf("%s[%d]: Found something on %u, search channels!\n", __FUNCTION__, tuner, f);
					SCAN_MESSAGE("DVB[%d]: Found something on %u, search channels!\n", tuner, f);
					/* Scan for channels within this frequency / transport stream */
#ifdef STSDK
					p_freq->valueint = st_frequency(tuner, f);
					p_freq->valuedouble = (double)p_freq->valueint;
					res = st_rpcSync(elcmd_dvbtune, params, &type, &result );
					cJSON_Delete(result); // ignore
					result = NULL;

					eprintf("%s[%d]: scanning %6u\n", __FUNCTION__, tuner, f);
					res = st_rpcSyncTimeout(elcmd_dvbscan, params, RPC_SCAN_TIMEOUT, &type, &result );
					cJSON_Delete(result);
					if((res == 0) && (type == elcdRpcResult)) {
						dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
					} else {
						eprintf("%s[%d]: scan failed\n", __FUNCTION__, tuner);
					}
#else
					dvb_scanForServices(f, tuner, 0);
#endif // STSDK
					SCAN_MESSAGE("DVB[%d]: Found %d channels ...\n", tuner, dvb_getNumberOfServices());
					dprintf("%s[%d]: Found %d channels ...\n", __FUNCTION__, tuner, dvb_getNumberOfServices());
				} else {
					eprintf("%s[%d]: ... NOTHING f= %u \n", __FUNCTION__, tuner, f);
				}
				if(pFunction != NULL && pFunction(f, dvb_getNumberOfServices(), tuner, 0, 0) == -1) {
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
					break;
				}

				if(	found &&
					(dvb_isCurrentDelSys_dvbt2(tuner) == 1) &&
					(appControlInfo.dvbtInfo.plp_id < 3))
				{
					appControlInfo.dvbtInfo.plp_id++;
					dprintf("%s[%u]: scan plp %u\n", __FUNCTION__, tuner, appControlInfo.dvbtInfo.plp_id);
				} else {
					found = 0;
					appControlInfo.dvbtInfo.plp_id = 0;
				}
			} while(found);
		}

		close(frontend_fd);
	} else { // streamer input
		dvb_scanForServices(0, tuner, 0);
		dprintf("%s[%d]: Found %d channels ...\n", __FUNCTION__, tuner, dvb_getNumberOfServices());
	}
	/* Write the channel list to the root file system */
	//dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);

	dprintf("%s[%d]: Tuning complete\n", __FUNCTION__, tuner);

service_scan_end:
#ifdef STSDK
	cJSON_Delete(params);
#endif
	return ret;
}

static int32_t dvb_frequencyScanOne(tunerFormat tuner, __u32 frequency, EIT_media_config_t *media,
						dvb_displayFunctionDef* pFunction, dvb_cancelFunctionDef* pCancelFunction, uint32_t enableNit, int frontend_fd)
{
	uint32_t	freqKHz = st_frequency(tuner, frequency);
	float		freqMHz = (float)freqKHz / (float)KHZ;
	int32_t		tuneSuccess = 0;

	tuneSuccess = dvb_setParam(appControlInfo.tunerInfo[tuner].type, frequency, tuner, frontend_fd, 1, NULL, NULL);
	if(tuneSuccess) {
		eprintf("%s(): tuner=%d, scanning %.3f MHz\n", __FUNCTION__, tuner, freqMHz);

		dvb_scanForServices(frequency, tuner, enableNit);
	} else {
		eprintf("%s[%d]: failed to set frequency to %.3f MHz\n", __FUNCTION__, tuner, freqMHz);
	}

	if(frontend_fd >= 0) {
		close(frontend_fd);
	}

	return 0;
}

static int32_t dvb_isFrequencyesEqual(tunerFormat tuner, uint32_t freq1, uint32_t freq2)
{
	uint32_t diff;
	uint32_t range = 0;
	switch(dvb_getType(tuner)) {
		case SYS_DVBT:
		case SYS_DVBT2:
		case SYS_DVBC_ANNEX_AC:
		case SYS_ATSC:
		case SYS_DVBC_ANNEX_B:
			range = 500000;//0,5MHz
			break;
		case SYS_DVBS:
			//for dvb-s frequencies are in KHz
			range = 1000;//1MHz
			break;
		default :
			break;
	}
	if(freq1 > freq2) {
		diff = freq1 - freq2;
	} else {
		diff = freq2 - freq1;
	}
	if(diff <= range) {
		return 1;
	}
	return 0;
}

int dvb_frequencyScan(tunerFormat tuner, __u32 frequency, EIT_media_config_t *media,
						dvb_displayFunctionDef* pFunction,
						int save_service_list, dvb_cancelFunctionDef* pCancelFunction)
{
	int frontend_fd = -1;
	struct dvb_frontend_info fe_info;
	int current_frequency_number = 1, max_frequency_number = 0;

	if(!appControlInfo.dvbCommonInfo.streamerInput) {
		if((frontend_fd = dvb_getFrontendInfo(dvb_getAdapter(tuner), O_RDWR, &fe_info)) < 0) {
			eprintf("%s[%d]: failed to open frontend %d\n", __FUNCTION__, tuner, dvb_getAdapter(tuner));
			return -1;
		}

#ifdef STSDK
		dvb_frequencyScanOne(tuner, frequency, media, pFunction, pCancelFunction, appControlInfo.dvbCommonInfo.networkScan, frontend_fd);

		if(appControlInfo.dvbCommonInfo.networkScan) {
			list_element_t *tstream_element;
			NIT_transport_stream_t *tsrtream;

			/* Count frequencies in NIT */
			tstream_element = dvb_scan_network.transport_streams;

			while(tstream_element != NULL) {
				tsrtream = (NIT_transport_stream_t *)tstream_element->data;

				if( tsrtream != NULL &&
					tsrtream->media.type > serviceMediaNone &&
					tsrtream->media.frequency > 0 &&
					!dvb_isFrequencyesEqual(tuner, tsrtream->media.frequency, frequency) )
				{
					dvb_frequencyScanOne(tuner, tsrtream->media.frequency, &tsrtream->media, pFunction, pCancelFunction, 0, frontend_fd);
	//				max_frequency_number++;
				}
				tstream_element = tstream_element->next;
			}
		}

		if(save_service_list) {
			dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
		}
		close(frontend_fd);
		return 0;
#endif

		dprintf("%s: Scan tuner %d\n", __FUNCTION__, tuner);

		// Use the FE's own start/stop freq. for searching (if not explicitly app. defined).
		if(frequency < fe_info.frequency_min || frequency > fe_info.frequency_max) {
			eprintf("%s[%d]: Frequency %u is out of range [%u:%u]!\n", __FUNCTION__, tuner,
				frequency, fe_info.frequency_min, fe_info.frequency_max);
			close(frontend_fd);
			return 1;
		}

		ZERO_SCAN_MESAGE();

		SCAN_MESSAGE("DVB[%d]: Scan: Freq=%u\n", tuner, frequency);
		SCAN_MESSAGE("==================================================================================\n");

		eprintf     ("DVB[%d]: Scan: Freq=%u\n", tuner, frequency);
		eprintf     ("==================================================================================\n");

		__u32 f = frequency;
		__u32 match_ber;
		int i;

		dprintf("%s[%d]: Check main freq: %u\n", __FUNCTION__, tuner, f);

		int found = dvb_checkFrequency(dvb_getType(tuner), &f, frontend_fd, dvb_getAdapter(tuner),
			&match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
		if(found == -1) {
			dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
			goto frequency_scan_failed;
		}

		/* Just keep tuner on frequency */
		if(save_service_list == -1) {
			for(;;) {
				if (pFunction == NULL ||
				    pFunction(frequency, 0, tuner, current_frequency_number, max_frequency_number) == -1)
				{
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
					break;
				}
				sleepMilli(500);
			}
			close(frontend_fd);
			return 1;
		} else if(save_service_list == -2) {/* Or just return found value */
			close(frontend_fd);
			return found;
		}

		if(appControlInfo.dvbCommonInfo.extendedScan) {
			for(i=1; !found && (i<=MAX_OFFSETS); i++) {
				long offset = i * fe_info.frequency_stepsize;

				f = frequency - offset;
				dprintf("%s[%d]: Check lower freq: %u\n", __FUNCTION__, tuner, f);
				found = dvb_checkFrequency(dvb_getType(tuner), &f, frontend_fd, dvb_getAdapter(tuner),
				                 &match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
				if(found == -1) {
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
					goto frequency_scan_failed;
				}
				if(found)
					break;
				f = frequency + offset;
				dprintf("%s[%d]: Check higher freq: %u\n", __FUNCTION__, tuner, f);
				found = dvb_checkFrequency(dvb_getType(tuner), &f, frontend_fd, dvb_getAdapter(tuner),
				                 &match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
				if (found == -1) {
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
					goto frequency_scan_failed;
				}
			}
		}

		if(found) {
			dprintf("%s[%d]: Found something on %u, search channels!\n", __FUNCTION__, tuner, f);
			SCAN_MESSAGE("DVB[%d]: Found something on %u, search channels!\n", tuner, f);
			/* Scan for channels within this frequency / transport stream */
			dvb_scanForServices(f, tuner, 0);
			SCAN_MESSAGE("DVB[%d]: Found %d channels ...\n", tuner, dvb_getNumberOfServices());
			dprintf("%s[%d]: Found %d channels ...\n", __FUNCTION__, tuner, dvb_getNumberOfServices());
		}

		close(frontend_fd);
	} else {
		dvb_scanForServices(frequency, tuner, 0);
		dprintf("%s[%d]: Found %d channels on %u\n", __FUNCTION__, tuner, dvb_getNumberOfServices(), frequency);
	}

	if(appControlInfo.dvbCommonInfo.networkScan) {
		list_element_t *tstream_element;
		NIT_transport_stream_t *tsrtream;

		/* Count frequencies in NIT */
		tstream_element = dvb_scan_network.transport_streams;
		while (tstream_element != NULL)
		{
			tsrtream = (NIT_transport_stream_t *)tstream_element->data;
			if (tsrtream != NULL &&
			    tsrtream->media.type      > serviceMediaNone &&
			    tsrtream->media.frequency > 0 &&
			    tsrtream->media.frequency != frequency)
			{
				max_frequency_number++;
			}
			tstream_element = tstream_element->next;
		}

		/* Scan frequencies in NIT */
		tstream_element = dvb_scan_network.transport_streams;
		while (tstream_element != NULL)
		{
			tsrtream = (NIT_transport_stream_t *)tstream_element->data;
			if (tsrtream != NULL &&
			    tsrtream->media.type > serviceMediaNone &&
			    tsrtream->media.frequency > 0 &&
			    tsrtream->media.frequency != frequency)
			{
				if (pFunction != NULL &&
				    pFunction(tsrtream->media.frequency, dvb_getNumberOfServices(),
					          tuner, current_frequency_number, max_frequency_number) == -1)
				{
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
					break;
				}
				eprintf("%s[%d]: Network Scan: %u Hz\n", __FUNCTION__, tuner, tsrtream->media.frequency);
				dvb_frequencyScan(tuner, tsrtream->media.frequency,
				                  &tsrtream->media, pFunction, 0, pCancelFunction);
				current_frequency_number++;
			}
			tstream_element = tstream_element->next;
		}
	}

	if(save_service_list) {
		dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
	}

	dprintf("%s[%d]: Frequency %u scan completed\n", __FUNCTION__, tuner, frequency);
	return 0;

frequency_scan_failed:
	close(frontend_fd);
	return -1;
}

/*
   clear and inits the given instance.
   @param mode @b IN The DVB mode to set up
*/
static void dvb_instanceReset(struct dvb_instance * dvb, int adapter)
{
	memset(dvb, 0, sizeof(struct dvb_instance));

	dvb->adapter = adapter;
	dvb->fdf = -1;
#ifdef LINUX_DVB_API_DEMUX
	/* Initialise file handles to invalid values! */
	dvb->fdv = -1;
#ifdef ENABLE_MULTI_VIEW
	dvb->fdv1 = -1;
	dvb->fdv2 = -1;
	dvb->fdv3 = -1;
#endif
	dvb->fda = -1;
	dvb->fdp = -1;
	dvb->fdt = -1;
	dvb->fdin = -1;
#ifdef ENABLE_DVB_PVR
	dvb->fdout = -1;
#endif
#endif // LINUX_DVB_API_DEMUX
}

#ifdef LINUX_DVB_API_DEMUX
static void dvb_demuxerInit (struct dvb_instance * dvb, DvbMode_t mode)
{
#ifdef STBTI
	dvb->filterv.input    = DMX_IN_FRONTEND;
	dvb->filterv.output   = DMX_OUT_TS_TAP;
	dvb->filterv.pes_type = DMX_PES_VIDEO;
	dvb->filterv.flags    = 0;

	dvb->filtera.input    = DMX_IN_FRONTEND;
	dvb->filtera.output   = DMX_OUT_TS_TAP;
	dvb->filtera.pes_type = DMX_PES_AUDIO;
	dvb->filtera.flags    = 0;

	dvb->filterp.input    = DMX_IN_FRONTEND;
	dvb->filterp.output   = DMX_OUT_TS_TAP;
	dvb->filterp.pes_type = DMX_PES_PCR;
	dvb->filterp.flags    = 0;
#else
	dvb->filterv.input    = (mode == DvbMode_Play) ? DMX_IN_DVR : DMX_IN_FRONTEND;
	dvb->filterv.output   = (mode == DvbMode_Record
#ifdef ENABLE_MULTI_VIEW
		|| mode == DvbMode_Multi
#endif
	) ? DMX_OUT_TS_TAP : DMX_OUT_DECODER;
	dvb->filterv.pes_type = DMX_PES_VIDEO;
	dvb->filterv.flags    = 0;

	dvb->filtera.input    = (mode == DvbMode_Play) ? DMX_IN_DVR : DMX_IN_FRONTEND;
	dvb->filtera.output   = (mode == DvbMode_Record) ? DMX_OUT_TS_TAP : DMX_OUT_DECODER;
	dvb->filtera.pes_type = DMX_PES_AUDIO;
	dvb->filtera.flags    = 0;

	dvb->filterp.input    = (mode == DvbMode_Play) ? DMX_IN_DVR : DMX_IN_FRONTEND;
	dvb->filterp.output   = (mode == DvbMode_Record) ? DMX_OUT_TS_TAP : DMX_OUT_DECODER;
	dvb->filterp.pes_type = DMX_PES_PCR;
	dvb->filterp.flags    = 0;

#ifdef ENABLE_MULTI_VIEW
	dvb->filterv1.input    = DMX_IN_FRONTEND;
	dvb->filterv1.output   = DMX_OUT_TS_TAP;
	dvb->filterv1.pes_type = DMX_PES_VIDEO;
	dvb->filterv1.flags    = 0;

	dvb->filterv2.input    = DMX_IN_FRONTEND;
	dvb->filterv2.output   = DMX_OUT_TS_TAP;
	dvb->filterv2.pes_type = DMX_PES_VIDEO;
	dvb->filterv2.flags    = 0;

	dvb->filterv3.input    = DMX_IN_FRONTEND;
	dvb->filterv3.output   = DMX_OUT_TS_TAP;
	dvb->filterv3.pes_type = DMX_PES_VIDEO;
	dvb->filterv3.flags    = 0;
#endif

#endif /* !defined STBTI */

	dvb->filtert.input    = DMX_IN_FRONTEND;
	dvb->filtert.output   = DMX_OUT_TS_TAP;
	dvb->filtert.pes_type = DMX_PES_TELETEXT;
	dvb->filtert.flags    = 0;
}

static int dvb_demuxerSetup (struct dvb_instance * dvb, DvbParam_t *pParam)
{
	switch (pParam->mode) {
#ifdef ENABLE_MULTI_VIEW
		case DvbMode_Multi:
			eprintf("%s[%d]: Multi %d %d %d %d\n", __FUNCTION__, pParam->adapter,
			        pParam->param.multiParam.channels[0],
			        pParam->param.multiParam.channels[1],
			        pParam->param.multiParam.channels[2],
			        pParam->param.multiParam.channels[3]);
			dvb->filterv.pid = pParam->param.multiParam.channels[0];
			dvb->filterv1.pid = pParam->param.multiParam.channels[1];
			dvb->filterv2.pid = pParam->param.multiParam.channels[2];
			dvb->filterv3.pid = pParam->param.multiParam.channels[3];
			return 0;
#endif
#ifdef ENABLE_DVB_PVR
		case DvbMode_Play:
			eprintf("%s[%d]: Play Video pid %d and Audio pid %d Text pid %d Pcr %d\n", __FUNCTION__, pParam->adapter,
			        pParam->param.playParam.videoPid,
			        pParam->param.playParam.audioPid,
			        pParam->param.playParam.textPid,
			        pParam->param.playParam.pcrPid);
			dvb->filterv.pid  = pParam->param.playParam.videoPid;
			dvb->filtera.pid  = pParam->param.playParam.audioPid;
			dvb->filtert.pid  = pParam->param.playParam.textPid;
			dvb->filterp.pid  = pParam->param.playParam.pcrPid;
			dvb->fileIndex    = pParam->param.playParam.position.index;
			dvb->pvr.position = pParam->param.playParam.position.offset;
			return 0;
#endif
		case DvbMode_Watch:
			if (pParam->frequency == 0) {
				if (dvb_getPIDs(dvb_getService( pParam->param.liveParam.channelIndex ), pParam->param.liveParam.audioIndex,
					&dvb->filterv.pid,
					&dvb->filtera.pid,
					&dvb->filtert.pid,
					&dvb->filterp.pid) != 0)
				{
					eprintf("%s[%d]: Watch failed to get stream pids for %d service\n", __FUNCTION__, pParam->adapter,
							pParam->param.liveParam.channelIndex);
					return -1;
				}
			} else {
				dvb->filterv.pid = pParam->param.playParam.videoPid;
				dvb->filtera.pid = pParam->param.playParam.audioPid;
				dvb->filtert.pid = pParam->param.playParam.textPid;
				dvb->filterp.pid = pParam->param.playParam.pcrPid;
			}
			eprintf("%s[%d]: Watch channel %d Video pid %hu Audio pid %hu Text pid %hu Pcr %hu\n", __FUNCTION__, pParam->adapter,
				pParam->param.liveParam.channelIndex,
				dvb->filterv.pid, dvb->filtera.pid, dvb->filtert.pid, dvb->filterp.pid);
			return 0;
		default:;
	}
	return -1;
}

static int dvb_demuxerStart(struct dvb_instance * dvb, EIT_media_config_t *media)
{
	dprintf("%s[%d]: setting filterv=%hu filtera=%hu filtert=%hu filterp=%hu\n", __FUNCTION__, dvb->adapter,
	        dvb->filterv.pid, dvb->filtera.pid, dvb->filtert.pid, dvb->filterp.pid );

	ioctl_or_abort(dvb->adapter, dvb->fdv, DMX_SET_PES_FILTER, &dvb->filterv);

#ifdef ENABLE_MULTI_VIEW
	if (dvb->mode == DvbMode_Multi)
	{
		dprintf("%s[%d]: filterv1=%hu filterv2=%hu filterv3=%hu\n", __FUNCTION__, dvb->adapter,
		        dvb->filterv1.pid, dvb->filterv2.pid, dvb->filterv3.pid);

		ioctl_or_abort(dvb->adapter, dvb->fdv1, DMX_SET_PES_FILTER, &dvb->filterv1);
		ioctl_or_abort(dvb->adapter, dvb->fdv2, DMX_SET_PES_FILTER, &dvb->filterv2);
		ioctl_or_abort(dvb->adapter, dvb->fdv3, DMX_SET_PES_FILTER, &dvb->filterv3);
	} else
#endif // ENABLE_MULTI_VIEW
	{
		ioctl_or_abort(dvb->adapter, dvb->fda,  DMX_SET_PES_FILTER, &dvb->filtera);
		ioctl_or_abort(dvb->adapter, dvb->fdp,  DMX_SET_PES_FILTER, &dvb->filterp);
		ioctl_or_abort(dvb->adapter, dvb->fdt,  DMX_SET_PES_FILTER, &dvb->filtert);
	}

	dprintf("%s[%d]: starting PID filters\n", __FUNCTION__, dvb->adapter);

	if (dvb->filterv.pid != 0)
		ioctl_or_warn (dvb->adapter, dvb->fdv, DMX_START);
#ifdef ENABLE_MULTI_VIEW
	if (dvb->mode == DvbMode_Multi)
	{
		if (dvb->filterv1.pid != 0)
			ioctl_or_abort(dvb->adapter, dvb->fdv1, DMX_START);
		if (dvb->filterv2.pid != 0)
			ioctl_or_abort(dvb->adapter, dvb->fdv2, DMX_START);
		if (dvb->filterv3.pid != 0)
			ioctl_or_abort(dvb->adapter, dvb->fdv3, DMX_START);
	} else
#endif // ENABLE_MULTI_VIEW
	{
		if (dvb->filtera.pid != 0)
			ioctl_or_warn (dvb->adapter, dvb->fda,  DMX_START);
		if (dvb->filtert.pid != 0)
			ioctl_or_warn (dvb->adapter, dvb->fdt,  DMX_START);
#ifdef STBTI
		if (dvb->filterp.pid != dvb->filtera.pid &&
		    dvb->filterp.pid != dvb->filterv.pid &&
		    dvb->filterp.pid != dvb->filtert.pid)
		{
			dprintf("%s[%d]: INIT PCR %hu\n", tuner, dvb->adapter, dvb->filterp.pid);
			ioctl_or_abort(dvb->adapter, dvb->fdp,  DMX_START);
		}
#else
		if (dvb->filterp.pid != 0)
			ioctl_or_abort(dvb->adapter, dvb->fdp,  DMX_START);
#endif // STBTI
	}
	return 0;
}
#endif // LINUX_DVB_API_DEMUX

/*
   create a dvb player instance by chaining the basic devices.
   Uses given device setup data (params) & device paths.
*/
#define open_or_abort(tuner, name, path, mode, fd)                             \
	do {                                                                       \
		if ((fd = open(path, mode)) < 0) {                                     \
			PERROR("%s[%d]: failed to open %s for %s: %s\n",                   \
				__FUNCTION__, tuner, path, name, strerror(errno));             \
			return -1;                                                         \
		}                                                                      \
	} while(0)

static int dvb_instanceOpen (struct dvb_instance * dvb, char *pFilename)
{
	int adapter = dvb->adapter;
	dprintf("%s[%d]: adapter%d/frontend0\n", __FUNCTION__, adapter, adapter);

	dvb->fe_tracker_Thread = 0;

	if(!appControlInfo.dvbCommonInfo.streamerInput && (dvb->mode != DvbMode_Play)) {
		dvb->fdf = dvb_openFrontend(adapter, O_RDWR);

		if (dvb->fdf < 0) {
			PERROR("%s[%d]: failed to open frontend", __FUNCTION__, adapter);
#ifndef ENABLE_DVB_PVR
			return -1;
#endif
		}
	}

#ifdef LINUX_DVB_API_DEMUX
	char path[32];
	dprintf("%s[%d]: adapter%d/demux0\n", __FUNCTION__, adapter, adapter);

	snprintf(path, sizeof(path), "/dev/dvb/adapter%i/demux0", adapter);
	open_or_abort(adapter, "video", path, O_RDWR, dvb->fdv);

#ifdef ENABLE_MULTI_VIEW
	if(dvb->mode == DvbMode_Multi) {
		open_or_abort(adapter, "video1", path, O_RDWR, dvb->fdv1);
		open_or_abort(adapter, "video2", path, O_RDWR, dvb->fdv2);
		open_or_abort(adapter, "video3", path, O_RDWR, dvb->fdv3);
	} else
#endif
	{
		open_or_abort(adapter, "audio",  path, O_RDWR, dvb->fda);
		open_or_abort(adapter, "pcr",    path, O_RDWR, dvb->fdp);
		open_or_abort(adapter, "text" ,  path, O_RDWR, dvb->fdt);
	}

	if (dvb->mode == DvbMode_Record
	 || dvb->filtert.pid
#ifdef ENABLE_MULTI_VIEW
	 || dvb->mode == DvbMode_Multi
#endif
	   )
	{
		dprintf("%s[%d]: adapter%i/dvr0\n", __FUNCTION__, adapter, adapter);

		snprintf(path, sizeof(path), "/dev/dvb/adapter%i/dvr0", adapter);
		open_or_abort(adapter, "read",   path, O_RDONLY, dvb->fdin);
#ifdef ENABLE_DVB_PVR
#ifdef ENABLE_MULTI_VIEW
		if( dvb->mode == DvbMode_Record )
#endif
		{
			char out_path[PATH_MAX];
			snprintf(dvb->directory, sizeof(dvb->directory), "%s", pFilename);
			snprintf(out_path, sizeof(out_path), "%s/part%02d.spts", dvb->directory, dvb->fileIndex);
			open_or_abort(adapter, "write", out_path, O_WRONLY | O_CREAT | O_TRUNC, dvb->fdout);
		}
#endif
	}
#ifdef ENABLE_DVB_PVR
	else if (dvb->mode == DvbMode_Play)
	{
		char in_path[PATH_MAX];
		dprintf("%s[%d]: %s/part%02d.spts\n", __FUNCTION__, adapter, dvb->directory, dvb->fileIndex);

		snprintf(dvb->directory, sizeof(dvb->directory), "%s", pFilename);
		dvb->pvr.rate = DEFAULT_PVR_RATE;
		snprintf(in_path, sizeof(in_path), "%s/part%02d.spts", dvb->directory, dvb->fileIndex);
		open_or_abort(adapter, "read", in_path, O_RDONLY, dvb->fdin);

		/* Set the current read position */
		lseek(dvb->fdin, (dvb->pvr.position/TS_PACKET_SIZE)*TS_PACKET_SIZE, 0);
		dvb->pvr.last_position = dvb->pvr.position;
		dvb->pvr.last_index = dvb->fileIndex;

		dprintf("%s[%d]: adapter%i/dvr0\n", __FUNCTION__, adapter, adapter);

		/* Since the input DVRs correponding to VMSP 0/1 are on adapter 2/3 */
		snprintf(path, sizeof(path), "/dev/dvb/adapter%i/dvr0", adapter);
		open_or_abort(adapter, "write", path, O_WRONLY, dvb->fdout);
	}
#endif // ENABLE_DVB_PVR
#endif // LINUX_DVB_API_DEMUX
	dprintf("%s[%d]: ok\n", __FUNCTION__, adapter);
	return 0;
}

int32_t dvb_getTeletextFD(int adapter, int32_t *dvr_fd)
{
	struct dvb_instance *dvb = &(dvbInstances[adapter]);

	if(dvb->filtert.pid > 0) {
		*dvr_fd = dvb->fdin;
		return 0;
	}

	return -1;
}
#undef open_or_abort

#define CLOSE_FD(tuner, name, fd)                                              \
	do {                                                                       \
		if (fd > 0 && close(fd) < 0)                                           \
			eprintf("%s[%d]: " name " closed with error: %s\n",                \
				__FUNCTION__, tuner, strerror(errno));                         \
	} while(0)

static void dvb_instanceClose (struct dvb_instance * dvb)
{
	//dprintf("%s[%d]: in\n", __FUNCTION__, dvb->adapter);
	if (0 != dvb->fe_tracker_Thread) {
		pthread_cancel (dvb->fe_tracker_Thread);
		dvb->fe_tracker_Thread = 0;
	}

	CLOSE_FD(dvb->adapter, "frontend",       dvb->fdf);
#ifdef LINUX_DVB_API_DEMUX
	CLOSE_FD(dvb->adapter, "video filter",   dvb->fdv);
#ifdef ENABLE_MULTI_VIEW
	CLOSE_FD(dvb->adapter, "video1 filter",  dvb->fdv1);
	CLOSE_FD(dvb->adapter, "video2 filter",  dvb->fdv2);
	CLOSE_FD(dvb->adapter, "video3 filter",  dvb->fdv3);
#endif
	CLOSE_FD(dvb->adapter, "audio filter",   dvb->fda);
	CLOSE_FD(dvb->adapter, "pcr filter",     dvb->fdp);
	CLOSE_FD(dvb->adapter, "teletext filter",dvb->fdt);
	CLOSE_FD(dvb->adapter, "input",          dvb->fdin);
#ifdef ENABLE_DVB_PVR
	CLOSE_FD(dvb->adapter, "output",         dvb->fdout);
#endif
#endif // LINUX_DVB_API_DEMUX
	dprintf("%s[%d]: ok\n", __FUNCTION__, dvb->adapter);
}
#undef CLOSE_FD

/* Thread to track frontend signal status */
static void * dvb_fe_tracker_Thread(void *pArg)
{
	int err;
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;

	for(;;) {
		sleep(TRACK_INTERVAL); // two sec.
		pthread_testcancel(); // just in case
		if(!appControlInfo.dvbCommonInfo.streamerInput) {
			ioctl_loop(dvb->adapter, err, dvb->fdf, FE_READ_STATUS,
				&appControlInfo.tunerInfo[dvb->adapter].fe_status);
			ioctl_loop(dvb->adapter, err, dvb->fdf, FE_READ_BER,
				&appControlInfo.tunerInfo[dvb->adapter].ber);
			ioctl_loop(dvb->adapter, err, dvb->fdf, FE_READ_SIGNAL_STRENGTH,
				&appControlInfo.tunerInfo[dvb->adapter].signal_strength);
			ioctl_loop(dvb->adapter, err, dvb->fdf, FE_READ_SNR,
				&appControlInfo.tunerInfo[dvb->adapter].snr);
			ioctl_loop(dvb->adapter, err, dvb->fdf, FE_READ_UNCORRECTED_BLOCKS,
				&appControlInfo.tunerInfo[dvb->adapter].uncorrected_blocks);
		}
	}
	return NULL;
}

#define PVR_BUFFER_SIZE (TS_PACKET_SIZE * 100)
#ifdef ENABLE_DVB_PVR
/* Thread that deals with asynchronous recording and playback of PVR files */
static void *dvb_pvrRateThread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	static struct timeval time[2];
	int which = 1;
	gettimeofday(&time[which], NULL);

	for(;;)
	{
		int difference = 0;
		int timeGap;
		int last;

		pthread_testcancel();
		usleep(100000);
		pthread_testcancel();

		gettimeofday(&time[which], NULL);
		last = (which+1)%2;

		/* Get the gap in ms */
		timeGap = ((time[which].tv_sec  - time[last].tv_sec) *1000) +
		          ((time[which].tv_usec - time[last].tv_usec)/1000);
		if (timeGap != 0)
		{
			if (dvb->fileIndex > dvb->pvr.last_index)
			{
				difference = FILESIZE_THRESHOLD - dvb->pvr.last_position;
				dvb->pvr.last_position = 0;
			}
			difference += (dvb->pvr.position - dvb->pvr.last_position);
			if (difference)
			{
				which = last;
				dvb->pvr.last_position = dvb->pvr.position;
				dvb->pvr.rate += (((difference*1000)/timeGap) - dvb->pvr.rate)/2;
				dprintf("%s[%d]: Time gap : %d ms Difference : %d PVR rate : %d Bps\n",
					__FUNCTION__, dvb->adapter, timeGap, difference, dvb->pvr.rate);
			}
		}
	}
	return NULL;
}
#endif // ENABLE_DVB_PVR

#ifdef ENABLE_MULTI_VIEW
static void *dvb_multiThread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	int length;
	int dvrout;
	struct pollfd pfd[1];
	const char *path = "/tmp/dvb.ts";
	unsigned char buf[PVR_BUFFER_SIZE];

	usleep(100000); // wait for pipe to be opened by multiview provider
	dvrout = open( path, O_WRONLY );
	if (dvrout < 0)
	{
		eprintf("%s[%d]: Failed to open %s fo writing: %s\n", __FUNCTION__, dvb->adapter, path, strerror(errno));
		return NULL;
	}
	pfd[0].fd = dvb->fdin;
	pfd[0].events = POLLIN;
	for(;;)
	{
		pthread_testcancel();
		if (poll(pfd,1,1))
		{
			length = -1;
			if (pfd[0].revents & POLLIN)
			{
				length = read(dvb->fdin, buf, sizeof(buf));
				if (length < 0)
					PERROR("%s[%d]: Failed to read DVR", __FUNCTION__, dvb->adapter);
				if (length > 0)
					write(dvrout, buf, length);
			}
			if (length < 0)
			{
				memset(buf, 0xff, TS_PACKET_SIZE);
				write(dvrout, buf, TS_PACKET_SIZE);
			}
		}
		pthread_testcancel();
	};
	dprintf("%s[%d]: Exiting dvb_multiThread\n", __FUNCTION__, dvb->adapter);
	return NULL;
}
#endif // ENABLE_MULTI_VIEW


#ifdef ENABLE_DVB_PVR
static void dvb_pvrThreadTerm(void* pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	dprintf("%s[%d]: Exiting DVB Thread\n", __FUNCTION__, dvb->adapter);
	if (dvb->mode == DvbMode_Record)
		fsync(dvb->fdout);
	if (dvb->pvr.rate_thread != 0) {
		pthread_cancel (dvb->pvr.rate_thread);
		pthread_join (dvb->pvr.rate_thread, NULL);
		dvb->pvr.rate_thread = 0;
	}
}

/* Thread that deals with asynchronous recording and playback of PVR files */
static void *dvb_pvrThread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	int length, write_length;
	char filename[PATH_MAX];

	if (dvb->mode == DvbMode_Play)
	{
		int st;
		st = pthread_create(&dvb->pvr.rate_thread, NULL,  dvb_pvrRateThread,  dvb);
		if (st != 0)
			dvb->pvr.rate_thread = 0;
	}
	dprintf("%s[%d]: running\n", __FUNCTION__, dvb->adapter);

	pthread_cleanup_push(dvb_pvrThreadTerm, pArg);
	do
	{
		unsigned char buffer[PVR_BUFFER_SIZE];

		pthread_testcancel();
		length = read(dvb->fdin, buffer, PVR_BUFFER_SIZE);
		if (length == 0)
		{
			if (dvb->mode == DvbMode_Play)
			{
				close(dvb->fdin);
				snprintf(filename, sizeof(filename), "%s/part%02d.spts", dvb->directory, dvb->fileIndex+1);
				if ((dvb->fdin = open(filename, O_RDONLY)) >= 0)
				{
					dvb->fileIndex++;
					dvb->pvr.position = 0;
					length = read(dvb->fdin, buffer, PVR_BUFFER_SIZE);
				}
			}
		}

		if (length < 0)
		{
			PERROR("%s[%d]: Read error", __FUNCTION__, dvb->adapter);
		}
		else
		{
			/* Update the current PVR playback position */
			if (dvb->mode == DvbMode_Record)
			{
				if (dvb->pvr.position > FILESIZE_THRESHOLD)
				{
					close(dvb->fdout);
					snprintf(filename, sizeof(filename), "%s/part%02d.spts", dvb->directory, dvb->fileIndex+1);
					if ((dvb->fdout = open(filename, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) >= 0)
					{
						dvb->fileIndex++;
						dvb->pvr.position = 0;
					}
				}
			}
			dvb->pvr.position += length;
			pthread_testcancel();
			write_length = write(dvb->fdout, buffer, length);
			if (write_length < 0)
				PERROR("%s[%d]: Write error", __FUNCTION__, dvb->adapter);
		}
		pthread_testcancel();
	} while ((length > 0) && (write_length > 0));

	dprintf("%s[%d]: out\n", __FUNCTION__, dvb->adapter);
	pthread_cleanup_pop(1);
	return NULL;
}
#endif // ENABLE_DVB_PVR

int dvb_getNumberOfServices(void)
{
	list_element_t *service_element;
	int serviceCount = 0;
	mysem_get(dvb_semaphore);
	for( service_element = dvb_services; service_element != NULL; service_element = service_element->next )
	{
		if (dvb_hasMediaNB(service_element->data))
		{
			serviceCount++;
		}
	}
	mysem_release(dvb_semaphore);
	return serviceCount;
}


EIT_service_t* dvb_getService(int which)
{
	list_element_t *service_element;
	if( which < 0 )
		return NULL;
	mysem_get(dvb_semaphore);
	for( service_element = dvb_services; service_element != NULL; service_element = service_element->next )
	{
		if(which == 0)
		{
			mysem_release(dvb_semaphore);
			return (EIT_service_t*)service_element->data;
		}
		which--;
	}
	mysem_release(dvb_semaphore);
	return NULL;
}

int dvb_getServiceIndex( EIT_service_t* service )
{
	list_element_t *service_element;
	int i;
	mysem_get(dvb_semaphore);

	for( service_element = dvb_services, i = 0;
		service_element != NULL;
		service_element = service_element->next, i++ )
	{
		if( (EIT_service_t*)service_element->data == service )
		{
			mysem_release(dvb_semaphore);
			return i;
		}
	}
	mysem_release(dvb_semaphore);
	return -1;
}

char* dvb_getServiceName(EIT_service_t *service)
{
	if(service)
	{
		return (char*)&service->service_descriptor.service_name[0];
	}
	return NULL;
}

payload_type dvb_getStreamType( PID_info_t* stream )
{
	switch (stream->stream_type)
	{
		case 0x01: // streamTypeVideoMPEG1
		case 0x02: // streamTypeVideoMPEG2
			return payloadTypeMpeg2;
		case 0x1b: // streamTypeVideoH264
			return payloadTypeH264;
		case 0x03: // streamTypeAudioMPEG1
		case 0x04: // streamTypeAudioMPEG2
			return payloadTypeMpegAudio;
		case 0x81: // streamTypeAudioAC3
			return payloadTypeAC3;
		case 0x06:
			return payloadTypeText;
		default:
			return payloadTypeUnknown;
	}
}

static PID_info_t* dvb_getVideoStream(EIT_service_t *service)
{
	if (service == NULL)
		return NULL;
	PID_info_t* stream = NULL;
	mysem_get(dvb_semaphore);
	for (list_element_t *stream_element = service->program_map.map.streams;
	     stream_element;
	     stream_element = stream_element->next)
	{
		stream = stream_element->data;
		switch (stream->stream_type)
		{
			case streamTypeVideoMPEG1:
			case streamTypeVideoMPEG2:
			case streamTypeVideoMPEG4:
			case streamTypeVideoH264:
			case streamTypeVideoVC1:
				mysem_release(dvb_semaphore);
				return stream;
		}
	}
	mysem_release(dvb_semaphore);
	return NULL;
}

PMT_stream_type_t dvb_getVideoType(EIT_service_t *service)
{
	PID_info_t* stream = dvb_getVideoStream(service);
	if (stream == NULL)
		return 0;
	return stream->stream_type;
}

static PID_info_t* dvb_getAudioStream(EIT_service_t *service, int audio)
{
	PID_info_t* stream;
	list_element_t *stream_element;
	int result;

	if(service == NULL)
	{
		return NULL;
	}
	audio = audio < 0 ? 1 : audio + 1;
	mysem_get(dvb_semaphore);
	stream_element = service->program_map.map.streams;
	while( stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		result = dvb_getStreamType( stream );
		switch( result )
		{
			case payloadTypeMpegAudio:
			case payloadTypeAC3:
			case payloadTypeAAC:
				audio--;
				break;
			default: ;
		}
		if( audio == 0 )
		{
			mysem_release(dvb_semaphore);
			return stream;
		}
		stream_element = stream_element->next;
	}
	mysem_release(dvb_semaphore);
	return NULL;
}

PMT_stream_type_t dvb_getAudioType(EIT_service_t *service, int audio)
{
	PID_info_t* stream = dvb_getAudioStream(service, audio);
	if( stream == NULL )
	{
		return -1;
	}
	return stream->stream_type;
}

uint16_t dvb_getAudioPid(EIT_service_t *service, int audio)
{
	PID_info_t* stream = dvb_getAudioStream(service, audio);
	if( stream == NULL )
	{
		return -1;
	}
	return stream->elementary_PID;
}

int dvb_getAudioCount(EIT_service_t *service)
{
	PID_info_t* stream;
	list_element_t *stream_element;
	int result, audioCount;

	if(service == NULL)
	{
		return -1;
	}
	stream_element = service->program_map.map.streams;
	audioCount = 0;
	while( stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		result = dvb_getStreamType( stream );
		switch( result )
		{
			case payloadTypeMpegAudio:
			case payloadTypeAC3:
			case payloadTypeAAC:
				audioCount++;
				break;
			default: ;
		}
		stream_element = stream_element->next;
	}
	return audioCount;
}

int dvb_getSubtitleCount(EIT_service_t *service)
{
	int count = 0;
	mysem_get(dvb_semaphore);
	for (list_element_t *s = service->program_map.map.streams; s; s = s->next) {
		PID_info_t* stream = s->data;
		if (isSubtitle(stream))
			count++;
	}
	mysem_release(dvb_semaphore);
	return count;
}

list_element_t* dvb_getNextSubtitleStream(EIT_service_t *service, list_element_t *subtitle)
{
	list_element_t *stream = subtitle ? subtitle->next : service->program_map.map.streams;
	while (stream) {
		PID_info_t* info = stream->data;
		if (isSubtitle(info))
			break;
		stream = stream->next;
	}
	return stream;
}

uint16_t dvb_getNextSubtitle(EIT_service_t *service, uint16_t subtitle_pid)
{
	assert (service);
	mysem_get(dvb_semaphore);
	for (list_element_t *s = service->program_map.map.streams; s; s = s->next) {
		PID_info_t* stream = s->data;
		if ((isSubtitle(stream)) && (subtitle_pid == 0 ||
						(stream->elementary_PID == subtitle_pid))) {
			mysem_release(dvb_semaphore);
			return stream->elementary_PID;
		}
	}
	mysem_release(dvb_semaphore);
	return 0;
}

int dvb_getServiceID(EIT_service_t *service)
{
	if(service != NULL)
	{
		return service->common.service_id;
	}
	return -1;
}

int dvb_getScrambled(EIT_service_t *service)
{
	if(service != NULL)
	{
		return service->service_descriptor.free_CA_mode;
	}
	return -1;
}

int dvb_hasPayloadType(EIT_service_t *service, payload_type p_type)
{
	int res;
	mysem_get(dvb_semaphore);
	res = dvb_hasPayloadTypeNB(service, p_type);
	mysem_release(dvb_semaphore);
	return res;
}

int dvb_hasPayloadTypeNB(EIT_service_t *service, payload_type p_type)
{
	PID_info_t *stream;
	list_element_t *stream_element;
	if(service == NULL)
	{
		return 0;
	}
	stream_element = service->program_map.map.streams;
	while( stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		if( dvb_getStreamType( stream ) == p_type )
		{
			return 1;
		}
		stream_element = stream_element->next;
	}
	return 0;
}

int dvb_hasMedia(EIT_service_t *service)
{
	int res;
	mysem_get(dvb_semaphore);
	res = dvb_hasMediaNB(service);
	mysem_release(dvb_semaphore);
	return res;
}

int dvb_hasMediaNB(EIT_service_t *service)
{
	if ((service->flags & (serviceFlagHasPAT|serviceFlagHasPMT)) == (serviceFlagHasPAT|serviceFlagHasPMT) &&
		(dvb_hasMediaTypeNB(service, mediaTypeVideo) ||
		 dvb_hasMediaTypeNB(service, mediaTypeAudio)) )
	{
		return 1;
	}
	return 0;
}

int dvb_hasMediaType(EIT_service_t *service, media_type m_type)
{
	int res;
	mysem_get(dvb_semaphore);
	res = dvb_hasMediaTypeNB(service, m_type);
	mysem_release(dvb_semaphore);
	return res;
}

int dvb_hasMediaTypeNB(EIT_service_t *service, media_type m_type)
{
	PID_info_t* stream;
	list_element_t *stream_element;
	int type;
	if(service == NULL)
	{
		return 0;
	}
	stream_element = service->program_map.map.streams;
	while( stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		type = dvb_getStreamType( stream );
		if( (m_type == mediaTypeVideo && (type == payloadTypeH264  ||
			                              type == payloadTypeMpeg2 ||
			                              type == payloadTypeMpeg4)) ||
			(m_type == mediaTypeAudio && (type == payloadTypeMpegAudio ||
			                              type == payloadTypeAAC   ||
			                              type == payloadTypeAC3)) ||
			type == payloadTypeText)
		{
			return 1;
		}
		stream_element = stream_element->next;
	}
	return 0;
}

int dvb_getPIDs(EIT_service_t *service, int audio, uint16_t* pVideo, uint16_t* pAudio, uint16_t* pText, uint16_t* pPcr)
{
	list_element_t *stream_element;
	PID_info_t* stream;
	int type;

	if (service == NULL)
		return -1;

	audio = audio < 0 ? 1 : audio + 1;
	mysem_get(dvb_semaphore);
	if (pPcr != NULL)
		*pPcr = service->program_map.map.PCR_PID;
	stream_element = service->program_map.map.streams;
	while (stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		type = dvb_getStreamType( stream );
		switch (type) {
			case payloadTypeMpeg2:
			case payloadTypeH264:
				if (pVideo != NULL)
					*pVideo = stream->elementary_PID;
				break;
			case payloadTypeMpegAudio:
			case payloadTypeAAC:
			case payloadTypeAC3:
				if (pAudio != NULL) {
					audio--;
					if (audio == 0)
						*pAudio = stream->elementary_PID;
				}
				break;
			case payloadTypeText:
				if (pText != NULL)
					*pText = stream->elementary_PID;
				break;
			default: ;
		}
		stream_element = stream_element->next;
	}
	mysem_release(dvb_semaphore);
	return 0;
}

int dvb_getServiceFrequency(EIT_service_t *service, __u32* pFrequency)
{
	if (service == NULL)
		return -1;

	mysem_get(dvb_semaphore);
	switch(service->media.type)
	{
		case serviceMediaDVBT:
			*pFrequency = service->media.dvb_t.centre_frequency;
			break;
		case serviceMediaDVBC:
			*pFrequency = service->media.dvb_c.frequency;
			break;
		default:
			*pFrequency = service->common.media_id;
	}
	mysem_release(dvb_semaphore);
	return 0;
}

int dvb_getServiceURL(EIT_service_t *service, char* URL)
{
	list_element_t *stream_element;
	PID_info_t* stream;
	__u32 frequency;
	int type, audio_printed = 0;
	if( service == NULL )
	{
		dprintf("%s: dvb_getServiceURL service is NULL\n", __FUNCTION__);
		URL[0]=0;
		return -1;
	}
	mysem_get(dvb_semaphore);
	switch(service->media.type)
	{
		case serviceMediaDVBT:
			frequency = service->media.dvb_t.centre_frequency;
			break;
		case serviceMediaDVBC:
			frequency = service->media.dvb_c.frequency;
			break;
		default:
			frequency = service->common.media_id;
	}
	sprintf(URL,"dvb://%lu:%hu/?pcr=%hu",
	        (unsigned long)frequency,service->common.service_id,service->program_map.map.PCR_PID);
	URL+=strlen(URL);
	stream_element = service->program_map.map.streams;
	while( stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		type = dvb_getStreamType( stream );
		switch( type )
		{
			case payloadTypeMpegAudio:
				if( audio_printed == 0)
				{
					sprintf( URL, "&at=MP3&ap=%hu", stream->elementary_PID );
					URL+=strlen(URL);
					audio_printed = 1;
				}
				break;
			case payloadTypeMpeg2:
				sprintf( URL, "&vt=MPEG2&vp=%hu", stream->elementary_PID );
				URL+=strlen(URL);
				break;
			case payloadTypeAAC:
				if( audio_printed == 0)
				{
					sprintf( URL, "&at=AAC&ap=%hu", stream->elementary_PID );
					URL+=strlen(URL);
					audio_printed = 1;
				}
				break;
			case payloadTypeText:
				sprintf( URL, "&tp=%hu", stream->elementary_PID );
				URL+=strlen(URL);
				break;
			case payloadTypeMpeg4:
				sprintf( URL, "&vt=MPEG4&vp=%hu", stream->elementary_PID );
				URL+=strlen(URL);
				break;
			case payloadTypeH264:
				sprintf( URL, "&vt=H264&vp=%hu", stream->elementary_PID );
				URL+=strlen(URL);
				break;
			case payloadTypeAC3:
				if( audio_printed == 0)
				{
					sprintf( URL, "&at=AC3&ap=%hu", stream->elementary_PID );
					URL+=strlen(URL);
					audio_printed = 1;
				}
				break;
			default: ;
		}
		stream_element = stream_element->next;
	}
	mysem_release(dvb_semaphore);
	return 0;
}

int dvb_getServiceDescription(EIT_service_t *service, char* buf)
{
	list_element_t *stream_element;
	PID_info_t* stream;
	int32_t	type;
	const char	*typeName;
	const char	*modName;

	if(service == NULL) {
		dprintf("%s: service is NULL\n", __FUNCTION__);
		buf[0] = 0;
		return -1;
	}
	mysem_get(dvb_semaphore);
	sprintf(buf, "\"%s\"\n%s: %s\n",
		service->service_descriptor.service_name,
		_T("DVB_PROVIDER"), service->service_descriptor.service_provider_name);
	buf += strlen(buf);

	typeName = table_IntStrLookup(fe_typeName, service->media.type, NULL);
	if(typeName) {
		sprintf(buf, "%s:\n", typeName);
		buf += strlen(buf);
	}
	sprintf(buf,"  %s: %u MHz\n", _T("DVB_FREQUENCY"),
			(service->media.type == serviceMediaDVBS) ? service->media.frequency / 1000 : service->media.frequency / 1000000);
	buf += strlen(buf);

	switch(service->media.type) {
		case serviceMediaDVBT:
			//TODO: show plp id for dvb-t2
			break;
		case serviceMediaDVBC:
			sprintf(buf,"  %s: %u KBd\n", _T("DVB_SYMBOL_RATE"), service->media.dvb_c.symbol_rate / 1000);
			modName = table_IntStrLookup(fe_modulationName, service->media.dvb_c.modulation, NULL);
			if(modName) {
				sprintf(buf, "  %s: %s\n", _T("DVB_MODULATION"), modName);
				buf += strlen(buf);
			}
			break;
		case serviceMediaDVBS:
			sprintf(buf,"  %s: %u KBd\n", _T("DVB_SYMBOL_RATE"), service->media.dvb_s.symbol_rate / 1000);
			buf += strlen(buf);
			sprintf(buf,"  %s: %s\n", _T("DVB_POLARIZATION"), service->media.dvb_s.polarization ? "V" : "H");
			buf += strlen(buf);
			break;
		case serviceMediaATSC:
			modName = table_IntStrLookup(fe_modulationName, service->media.atsc.modulation, NULL);
			if(modName) {
				sprintf(buf, "  %s: %s\n", _T("DVB_MODULATION"), modName);
				buf += strlen(buf);
			}
			break;
		default: ;
	}

	sprintf(buf,"Service ID: %hu\nPCR: %hu\n%s\n",
		service->common.service_id,
		service->program_map.map.PCR_PID, _T("DVB_STREAMS"));
	buf += strlen(buf);
	stream_element = service->program_map.map.streams;
	while(stream_element != NULL) {
		stream = (PID_info_t*)stream_element->data;
		type = dvb_getStreamType( stream );
		switch(type) {
			case payloadTypeMpegAudio:
			case payloadTypeMpeg2:
			case payloadTypeAAC:
			case payloadTypeAC3:
				switch(type) {
					case payloadTypeMpegAudio:
						sprintf( buf, "  PID: %hu MP3 %s", stream->elementary_PID, _T("AUDIO") );
						break;
					case payloadTypeMpeg2:
						sprintf( buf, "  PID: %hu MPEG2 %s", stream->elementary_PID, _T("VIDEO") );
						break;
					case payloadTypeAAC:
						sprintf( buf, "  PID: %hu AAC %s", stream->elementary_PID, _T("AUDIO") );
						break;
					case payloadTypeAC3:
						sprintf( buf, "  PID: %hu AC3 %s", stream->elementary_PID , _T("AUDIO") );
						break;
				}
				buf += strlen(buf);
				if(stream->ISO_639_language_code[0]) {
					eprintf("%s: '%s'\n", __FUNCTION__, stream->ISO_639_language_code);
					sprintf( buf, " (%3s)", stream->ISO_639_language_code);
					buf += strlen(buf);
				}
				*buf++ = '\n';
				break;
			case payloadTypeMpeg4:
				sprintf( buf, "  PID: %hu MPEG4 %s\n", stream->elementary_PID, _T("VIDEO") );
				buf += strlen(buf);
				break;
			case payloadTypeH264:
				sprintf( buf, "  PID: %hu H264  %s\n", stream->elementary_PID, _T("VIDEO") );
				buf += strlen(buf);
				break;
			case payloadTypeText:
				sprintf(buf, "  PID: %hu %s\n", stream->elementary_PID , isSubtitle(stream) ? _T("SUBTITLES") : _T("TELETEXT") );
				buf += strlen(buf);
				break;
			default: ;
		}
		stream_element = stream_element->next;
	}
	mysem_release(dvb_semaphore);
	buf[-1] = 0;
	return 0;
}

char* dvb_getEventName( EIT_service_t* service, uint16_t event_id )
{
	list_element_t *event_element;
	EIT_event_t* event;
	if( service == NULL || event_id == 0 )
		return NULL;
	if( service->schedule != NULL )
	{
		for (event_element = service->schedule;
		     event_element != NULL && (event = (EIT_event_t*)event_element->data) != NULL;
		     event_element = event_element->next )
		{
			if( event->event_id == event_id )
			{
				return (char *)event->description.event_name;
			}
		}
	}
	return NULL;
}

int dvb_changeAudioPid(tunerFormat tuner, uint16_t aPID)
{
	int adapter = dvb_getAdapter(tuner);
	if(!dvb_isLinuxAdapter(adapter)) {
		eprintf("%s[%d]: unsupported\n", __FUNCTION__, tuner);
		return -1;
	}
#ifdef LINUX_DVB_API_DEMUX
	struct dmx_pes_filter_params pesfilter;
	struct dvb_instance *dvb;
	dvb = &dvbInstances[adapter];

	//printf("Change aduio PID for adapter %d to %d\n", adapter, aPID);

	if (dvb->fda >= 0)
	{
		// Stop demuxing old audio pid
		ioctl_or_abort(adapter, dvb->fda, DMX_STOP);
		// reconfigure audio decoder
		// ...
		// reconfigure demux
		pesfilter.input = DMX_IN_DVR;
		pesfilter.output = DMX_OUT_DECODER;
		pesfilter.pes_type = DMX_PES_AUDIO;
		pesfilter.flags = 0;
		pesfilter.pid = aPID;

		ioctl_or_abort(adapter, dvb->fda, DMX_SET_PES_FILTER, &pesfilter);
		// start demuxing new audio pid
		ioctl_or_abort(adapter, dvb->fda, DMX_START);
	}
#endif // LINUX_DVB_API_DEMUX
	return 0;
}

fe_delivery_system_t dvb_getDelSysFromService(EIT_service_t *service)
{
	fe_delivery_system_t typeAfter = 0;
	serviceMediaType_t typeBefore;
	uint8_t generation;
	uint8_t modulation;

	if(service == NULL) {
		eprintf("%s: wrong arguments!\n", __FUNCTION__);
		return SYS_UNDEFINED;
	}

	typeBefore = service->media.type;
	switch (typeBefore) {
		case serviceMediaDVBT:
			generation = service->media.dvb_t.generation;
			if(generation == 2) {
				typeAfter = SYS_DVBT2;
			} else {
				typeAfter = SYS_DVBT;
			}
			break;
		case serviceMediaDVBC:	typeAfter = SYS_DVBC_ANNEX_AC; break;
		case serviceMediaDVBS:	typeAfter = SYS_DVBS; break;
//		case serviceMediaMulticast:	typeAfter = ; break;
		case serviceMediaATSC:
			modulation = service->media.atsc.modulation;
			if((modulation == QAM_64) || (modulation == QAM_256)) {
				typeAfter = SYS_DVBC_ANNEX_B;
				printf("%s[%d]: SYS_DVBC_ANNEX_B!!!\n", __FILE__, __LINE__);
			} else {
				typeAfter = SYS_ATSC;
				printf("%s[%d]: SYS_ATSC!!!\n", __FILE__, __LINE__);
			}
			break;
		default:
			typeAfter = SYS_UNDEFINED;
			eprintf("%s: Undefined type: %s\n", __FUNCTION__, typeBefore);
	}
	return typeAfter;
}

int dvb_startDVB(DvbParam_t *pParam)
{
	if(!dvb_isLinuxAdapter(pParam->adapter)) {
		eprintf("%s: Invalid adapter - use range 0-%d\n", __FUNCTION__, ADAPTER_COUNT-1);
		return -1;
	}

	dvb_filtersFlush();
	dvb_filtersUnlock();

	struct dvb_instance *dvb = &dvbInstances[pParam->adapter];
	dvb_instanceReset(dvb, pParam->adapter);

	dprintf("%s[%d]: set mode %s\n", __FUNCTION__, pParam->adapter, pParam->mode == DvbMode_Watch ? "Watch" :
		(pParam->mode == DvbMode_Multi ? "Multi" : (pParam->mode == DvbMode_Record ? "Record" : "Play")));
	dvb->mode = pParam->mode;
	dvb->setFrequency = pParam->frequency;

	if (pParam->mode == DvbMode_Watch) {
		EIT_service_t *service = dvb_getService( pParam->param.liveParam.channelIndex );
		if (dvb->setFrequency == 0 && dvb_getServiceFrequency( service, &dvb->setFrequency) != 0) {
			eprintf("%s[%d]: Watch failed to get frequency for %d service\n", __FUNCTION__, pParam->adapter, pParam->param.liveParam.channelIndex);
			return -1;
		}
	}

	dprintf("%s[%d]: set frequency %u\n", __FUNCTION__, dvb->setFrequency);

#ifdef LINUX_DVB_API_DEMUX
	dvb_demuxerInit(dvb, pParam->mode);
	if(dvb_demuxerSetup(dvb, pParam) != 0) {
		eprintf("%s[%d]: unknown mode %d!\n", __FUNCTION__, dvb->adapter, pParam->mode);
		return -1;
	}
#endif

	if (dvb_instanceOpen(dvb, pParam->directory) != 0)
	{
		eprintf("%s[%d]: Failed to open dvb instance\n", __FUNCTION__, pParam->adapter);
		return -1;
	}

	if (dvb->mode != DvbMode_Play &&
#ifdef ENABLE_DVB_PVR
	    (signed)dvb->setFrequency > 0 &&
#endif
	    dvb_setParam(dvb_getDelSysFromService(service), dvb->setFrequency, dvb->adapter, dvb->fdf, 0, pParam->media, NULL) < 0)
	{
		eprintf("%s[%d]: Failed to set frequency %u\n", __FUNCTION__, dvb->adapter, dvb->setFrequency);
		return -1;
	}

#ifdef LINUX_DVB_API_DEMUX
	dprintf("%s[%d]: start demuxer\n", __FUNCTION__, pParam->adapter);
	if(dvb_demuxerStart(dvb, pParam->media) != 0) {
		eprintf("%s[%d]: Failed to change service!\n", __FUNCTION__, pParam->adapter);
		return 0;
	}
#endif // LINUX_DVB_API_DEMUX

	int st;
	pthread_attr_t attr;
	struct sched_param param = { 90 };

	pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam (&attr, &param);

#ifdef ENABLE_DVB_PVR
	if (dvb->mode == DvbMode_Play || dvb->mode == DvbMode_Record)
	{
		dprintf("%s[%d]: DvbMode_%s\n", __FUNCTION__, dvb->adapter, dvb->mode == DvbMode_Play ? "Play" : "Record");

		st = pthread_create (&dvb->pvr.thread, &attr, dvb_pvrThread, dvb);
		if (st != 0)
		{
			eprintf("%s[%d]: error %d starting dvb_pvrThread\n", __FUNCTION__, dvb->adapter, st);
			goto failure;
		}
	} else if (dvb->fdf > 0)
#endif // ENABLE_DVB_PVR
	{
		dprintf("%s[%d]: DvbMode_Watch\n", __FUNCTION__, dvb->adapter);
		if (!dvb->fe_tracker_Thread)
		{
			st = pthread_create(&dvb->fe_tracker_Thread, &attr,  dvb_fe_tracker_Thread,  dvb);
			if (st != 0)
			{
				eprintf("%s[%d]: pthread_create (fe track) err=%d\n", __FUNCTION__, dvb->adapter, st);
				goto failure;
			}
			pthread_detach(dvb->fe_tracker_Thread);
		}

#ifdef ENABLE_MULTI_VIEW
		if( dvb->mode == DvbMode_Multi )
		{
			st = pthread_create(&dvb->multi_thread, &attr,  dvb_multiThread,  dvb);
			if (st != 0)
			{
				dvb->multi_thread = 0;
				eprintf("%s[%d]: pthread_create (multiview) err=%d\n", __FUNCTION__, dvb->adapter, st);
				goto failure;
			}
		}
#endif
	}
	pthread_attr_destroy(&attr);
	dprintf("%s[%d]: ok\n", __FUNCTION__, pParam->adapter);
	return 0;

failure:
	pthread_attr_destroy(&attr);
	return -1;
}

void dvb_stopDVB(int adapter, int reset)
{
	if(dvb_isLinuxAdapter(adapter)) {
		dprintf("%s[%d]: reset %d\n", __FUNCTION__, adapter, reset);

		struct dvb_instance *dvb = &dvbInstances[adapter];

#ifdef ENABLE_DVB_PVR
		if (dvb->pvr.thread != 0) {
			pthread_cancel (dvb->pvr.thread);
			pthread_join (dvb->pvr.thread, NULL);
			dvb->pvr.thread = 0;
		}
#endif
#ifdef ENABLE_MULTI_VIEW
		if (dvb->multi_thread != 0) {
			pthread_cancel (dvb->multi_thread);
			pthread_join (dvb->multi_thread, NULL);
			dvb->multi_thread = 0;
		}
#endif
		dvb_filtersFlush();
		dvb_instanceClose(dvb);
		dvb_instanceReset(dvb, adapter);
		if (reset)
		{
			/* Reset the stored frequency as the driver    */
			/* puts the tuner into standby so it needs to  */
			/* be woken up when we next do a set frequency */
			currentFrequency[adapter] = 0;
		}
		dprintf("%s[%d]: out\n", __FUNCTION__, adapter);
	}
}

void dvb_init(void)
{
	int i;

	dprintf("%s: in\n", __FUNCTION__);

	for(i = 0; i < inputTuners; i++) {
		appControlInfo.tunerInfo[i].status = tunerNotPresent;
	}

	tunerFormat tuner = 0;
	for(i = 0; (i < ADAPTER_COUNT) && (tuner < inputTuners); i++) {
		dvb_instanceReset(&dvbInstances[i], i);
		currentFrequency[i] = 0;
		appControlInfo.tunerInfo[tuner].adapter = i;

		if(dvb_initFrontend(tuner) < 0) {
			continue;
		}

		tuner++;
	}

	mysem_create(&dvb_semaphore);
	mysem_create(&dvb_filter_semaphore);
	mysem_create(&dvb_fe_semaphore);

	if(helperFileExists(appControlInfo.dvbCommonInfo.channelConfigFile)){
		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
		dprintf("%s: loaded %d services\n", __FUNCTION__, dvb_getNumberOfServices());
	}

	dprintf("%s: out\n", __FUNCTION__);
}

void dvb_terminate(void)
{
	int i;
	for(i=0; i<ADAPTER_COUNT; i++)
		dvb_stopDVB(i, 1);

	free_services(&dvb_services);

	mysem_destroy(dvb_semaphore);
	mysem_destroy(dvb_filter_semaphore);
	mysem_destroy(dvb_fe_semaphore);
}

#ifdef ENABLE_DVB_PVR
static int dvb_get_numPartPvrFiles(struct dvb_instance *dvb, int low, int high)
{
	char filename[PATH_MAX];
	if ((high - low) < 2)
	{
		return low;
	}
	snprintf(filename, sizeof(filename), "%s/part%02d.spts", dvb->directory, (low+high)/2);
	if (helperFileExists(filename))
	{
		return dvb_get_numPartPvrFiles(dvb, (low+high)/2, high);
	}
	return dvb_get_numPartPvrFiles(dvb, low, (low+high)/2);
}

static int dvb_getPvrFileLength(struct dvb_instance *dvb, int file)
{
	char filename[PATH_MAX];
	int filePtr;
	int offset;
	snprintf(filename, sizeof(filename), "%s/part%02d.spts", dvb->directory, file);

	if ((filePtr = open(filename, O_RDONLY)) <0)
		return 0;

	/* Set the current read position */
	offset = lseek(filePtr, 0, 2);
	close(filePtr);

	return offset;
}

void dvb_getPvrLength(int which, DvbFilePosition_t *pPosition)
{
	if (which >= 0 && which < 2)
	{
		struct dvb_instance *dvb = &dvbInstances[which+2];

		/* Calculate the number of pvr 'part' files */
		pPosition->index = dvb_get_numPartPvrFiles(dvb, 0, 99);
		pPosition->offset = dvb_getPvrFileLength(dvb, pPosition->index);
	}
}

void dvb_getPvrPosition(int which, DvbFilePosition_t *pPosition)
{
	if (which >= 0 && which < 2)
	{
		struct dvb_instance *dvb = &dvbInstances[which+2];
		pPosition->index = dvb->fileIndex;
		pPosition->offset = dvb->pvr.position;
	}
}

int dvb_getPvrRate(int which)
{
	if (which >= 0 && which < 2)
	{
		struct dvb_instance *dvb = &dvbInstances[which+2];
		return dvb->pvr.rate;
	}
	return DEFAULT_PVR_RATE;
}
#endif // ENABLE_DVB_PVR

#endif /* ENABLE_DVB */
