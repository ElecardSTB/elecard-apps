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
#include "dvb-fe.h"
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


#if (defined ENABLE_USE_DVB_APPS)
#include <libucsi/atsc/section.h>
#include <libucsi/atsc/descriptor.h>
#endif

/***********************************************
* LOCAL MACROS                                 *
************************************************/
#define fatal   eprintf
#define info    dprintf
#define verbose(...) //printf(__VA_ARGS__)
#define debug(...) //printf(__VA_ARGS__)

#define PERROR(fmt, ...) eprintf(fmt " (%s)\n", ##__VA_ARGS__, strerror(errno))

#define AUDIO_CHAN_MAX (8)

#define MAX_OFFSETS   (1)

#define MAX_RUNNING   (32)

#define FILE_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

/* SD bit rate ~ 1.5GB / hour -> ~ 450KB / second */
#define DEFAULT_PVR_RATE (450*1024)

#define ioctl_or_abort(adapter, fd, request, ...)                                \
	do {                                                                       \
		int ret = ioctl(fd, request, ##__VA_ARGS__);                           \
		if (ret < 0) {                                                         \
			PERROR("%s[%d]: ioctl " #request " failed", __FUNCTION__, adapter);  \
			return ret;                                                        \
		}                                                                      \
	} while(0)

#define ioctl_or_warn(adapter, fd, request, ...)                                 \
	do {                                                                       \
		if (ioctl(fd, request, ##__VA_ARGS__) < 0)                             \
			PERROR("%s[%d]: ioctl " #request " failed", __FUNCTION__, adapter);  \
	} while(0)

#define ioctl_loop(adapter, err, fd, request, ...)                               \
	do {                                                                       \
		err = ioctl(fd, request, ##__VA_ARGS__);                               \
		if (err < 0) {                                                         \
			info("%s[%d]: ioctl " #request " failed", __FUNCTION__, adapter);    \
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

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

// defines one chain of devices which together make up a DVB receiver/player
struct dvb_instance {
	// file descriptors for frontend, demux-video, demux-audio
	uint32_t adapter;

	DvbMode_t mode;

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


struct section_buf {
	struct list_head list;
	uint32_t adapter;
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

/***********************************************
* EXPORTED DATA                                *
************************************************/
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

static struct dvb_instance dvbInstances[MAX_ADAPTER_SUPPORTED];

static pmysem_t dvb_semaphore;
static pmysem_t dvb_filter_semaphore;
static NIT_table_t dvb_scan_network;

//char scan_messages[64*1024];

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int dvb_hasPayloadTypeNB(EIT_service_t *service, payload_type p_type);
static int dvb_hasMediaTypeNB(EIT_service_t *service, media_type m_type);
static int dvb_hasMediaNB(EIT_service_t *service);

static inline int isSubtitle( PID_info_t* stream)
{
	return stream->component_descriptor.stream_content == 0x03 &&
	       stream->component_descriptor.component_type >= 0x10 &&
	       stream->component_descriptor.component_type <= 0x15;
}

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

	snprintf(demux_devname, sizeof(demux_devname), "/dev/dvb/adapter%d/demux0", s->adapter);
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
		elcdRpcType_t type;
		cJSON *result = NULL;
		cJSON *params;

		params = cJSON_CreateObject();
		if(params) {
			cJSON_AddStringToObject(params, "uri", "all");
			st_rpcSync(elcmd_TSsectionStreamOff, params, &type, &result);
			if(result && (result->valuestring != NULL) &&
				(strcmp(result->valuestring, "ok") == 0)) {
				//nothing
			}

			cJSON_Delete(result);
			cJSON_Delete(params);
		} else {
			eprintf("%s(): Error in creating params\n", __func__);
		}

		memset(g_dvb_pipe, 0, sizeof(g_dvb_pipe));
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
	cJSON_AddNumberToObject(params, "tuner", s->adapter);
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

static void dvb_filterSetup(struct section_buf* s, uint32_t adapter,
			  int pid, int tid, int timeout, list_element_t **outServices)
{
	memset (s, 0, sizeof(struct section_buf));

	s->fd = -1;
	s->adapter = adapter;
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

#if (defined ENABLE_USE_DVB_APPS)
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
#endif //#if (defined ENABLE_USE_DVB_APPS)


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
	uint32_t network_id;

	service_index_t *srvIdx = dvbChannel_getServiceIndex(appControlInfo.dvbInfo.channel);
	if (srvIdx != NULL) {
		network_id = srvIdx->service->original_network_id;
	} else {
		network_id = 0x80000000 + (uint32_t)frequency;
	}
	if(s->service_list != NULL) {
		services = s->service_list;
	} else {
		services = &dvb_services;
	}

	dvbfe_fillMediaConfig(s->adapter, frequency, &media);

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
				updated = parse_pat(services, (unsigned char *)s->buf, network_id, &media);
				cur = *services;
				while(cur != NULL) {
					service = (EIT_service_t*)cur->data;
					if( ((srvIdx == NULL) || (service->common.service_id == srvIdx->service->common.service_id)) &&
						(service->flags & serviceFlagHasPAT) &&
						service->media.frequency == (unsigned long)frequency )
					{
						pmt_filter = dmalloc(sizeof(struct section_buf));
						if(pmt_filter != NULL) {
							dprintf("%s: new pmt filter ts %d, pid %d, service %d, media %d\n", __FUNCTION__,
									service->common.transport_stream_id,
									service->program_map.program_map_PID,
									service->common.service_id,
									service->common.media_id);
							dvb_filterSetup(pmt_filter, s->adapter, service->program_map.program_map_PID, 0x02, 2, services);
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
							service->program_map.program_map_PID, s->pid, service->common.media_id, network_id,
							service->common.transport_stream_id, s->transport_stream_id, service->media.frequency,
							(unsigned long)frequency, service->common.service_id);
					if (service->program_map.program_map_PID == s->pid &&
						service->common.media_id == network_id &&
						service->common.transport_stream_id == s->transport_stream_id &&
						service->media.frequency == (unsigned long)frequency )
					{
						dprintf("%s: parse pmt filter ts %d, pid %d, service %d, media %d\n", __FUNCTION__,
								service->common.transport_stream_id,
								service->program_map.program_map_PID,
								service->common.service_id,
								service->common.media_id);
						updated = parse_pmt(services, (unsigned char *)s->buf,
											service->common.transport_stream_id, network_id, &media);
					}
					cur = cur->next;
				}
				mysem_release(dvb_semaphore);
				break;

			case 0x40:
				verbose("%s: NIT 0x%04x for service 0x%04x\n", __FUNCTION__, s->pid, table_id_ext);
				if(appControlInfo.dvbCommonInfo.networkScan) {
					mysem_get(dvb_semaphore);
					updated = parse_nit(services, (unsigned char *)s->buf, network_id, &media, &dvb_scan_network);
					mysem_release(dvb_semaphore);
				}
				break;

			case 0x42:
			case 0x46:
				verbose("%s: SDT (%s TS)\n", __FUNCTION__, table_id == 0x42 ? "actual":"other");
				mysem_get(dvb_semaphore);
				//parse_sdt(services, (unsigned char *)s->buf, network_id, &media);
				updated = parse_sdt_with_nit(services, (unsigned char *)s->buf, network_id, &media, NULL);
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
				updated = parse_eit(services, (unsigned char *)s->buf, network_id, &media);
				if(updated) {
					s->timeout = 5;
				}
				mysem_release(dvb_semaphore);
				can_count_sections = 0;
				break;
#if (defined ENABLE_USE_DVB_APPS)
			case stag_atsc_terrestrial_virtual_channel:
			case stag_atsc_cable_virtual_channel:
				mysem_get(dvb_semaphore);
				parse_atsc_section(services, network_id, s->buf, ((s->buf[1] & 0x0f) << 8) | s->buf[2], s->pid);
				mysem_release(dvb_semaphore);
				break;
#endif
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
			network_id, s->pid, table_id, updated, s->sectionfilter_done);

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

static void dvb_scanForServices(long frequency, uint32_t adapter, uint32_t enableNit)
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
	dvb_filterSetup(&pat_filter, adapter, 0x00, 0x00, 5, &dvb_services); /* PAT */
	//dvb_filterSetup(&sdt1_filter, adapter, 0x11, 0x46, 5, &dvb_services); /* SDT other */
	dvb_filterSetup(&eit_filter, adapter, 0x12,   -1, 5, &dvb_services); /* EIT */

	dvb_filterAdd(&pat_filter);
	//dvb_filterAdd(&sdt1_filter);
	dvb_filterAdd(&eit_filter);

	if((dvbfe_getType(adapter) == SYS_ATSC) || (dvbfe_getType(adapter) == SYS_DVBC_ANNEX_B)) {
#if (defined ENABLE_USE_DVB_APPS)
		dvb_filterSetup(&tvct_filter, adapter, 0x1ffb, stag_atsc_terrestrial_virtual_channel, 5, &dvb_services); //Terrestrial Virtual Channel Table (TVCT)
		dvb_filterAdd(&tvct_filter);
		dvb_filterSetup(&cvct_filter, adapter, 0x1ffb, stag_atsc_cable_virtual_channel, 5, &dvb_services); //Cable Virtual Channel Table (CVCT)
		dvb_filterAdd(&cvct_filter);
#else
		(void)tvct_filter;
		(void)cvct_filter;
#endif
	} else {
		dvb_filterSetup(&sdt_filter, adapter, 0x11, 0x42, 5, &dvb_services); /* SDT actual */
		dvb_filterAdd(&sdt_filter);
	}

	if(enableNit) {
		dvb_clearNIT(&dvb_scan_network);
		dvb_filterSetup(&nit_filter, adapter, 0x10, 0x40, 5, &dvb_services); /* NIT */
		dvb_filterAdd(&nit_filter);
	}

	do {
		dvb_filtersRead(frequency);
	} while(!list_empty(&running_filters) || !list_empty(&waiting_filters));
	dvb_filtersLock();
	dvb_filtersFlush();

	//dvb_filterServices(&dvb_services);
}

static void dvb_scanForBouquet_t(uint32_t frequency, uint32_t adapter, EIT_service_t *service)
{
    struct section_buf pat_filter;
    dvb_filtersUnlock();
    do {
        if (service != NULL &&
            service->flags & serviceFlagHasPAT &&
            service->media.frequency == frequency) {
            struct section_buf pmt_filter;
            dprintf("%s: new pmt filter ts %d, pid %d, service %d, media %d\n", __FUNCTION__,
                    service->common.transport_stream_id,
                    service->program_map.program_map_PID,
                    service->common.service_id,
                    service->common.media_id);
            dvb_filterSetup(&pmt_filter, adapter, service->program_map.program_map_PID, 0x02, 2, &dvb_services);   /* PMT */
            pmt_filter.transport_stream_id = service->common.transport_stream_id;
            dvb_filterAdd(&pmt_filter);
            break;
        }
        dvb_filterSetup(&pat_filter, adapter, 0x00, 0x00, 5, &dvb_services); /* PAT */
        dvb_filterAdd(&pat_filter);
    } while(0);

    do {
		dvb_filtersRead(frequency);
    } while(!list_empty(&running_filters) || !list_empty(&waiting_filters));
    dvb_filtersLock();
    dvb_filtersFlush();
    dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
}

int32_t dvb_scanForBouquet(uint32_t adapter, EIT_service_t *service)
{
	uint32_t frequency;
	if(service == NULL) {
		eprintf("%s(): unknown service\n", __func__);
		return -1;
	}
	frequency = service->media.frequency;
	if(dvbfe_open(adapter) != 0) {
		eprintf("%s(): failed to open adapter%d\n", __func__, adapter);
		return -1;
	}
	if(dvbfe_setParam(adapter, 1, &service->media, NULL) != 0) {
		float freqMHz = (float)dvbfe_frequencyKHz(adapter, frequency) / (float)KHZ;
		eprintf("%s(): adapter=%d, failed to set frequency to %.3f MHz\n", __func__, adapter, freqMHz);
		return -1;
	}

	dvb_scanForBouquet_t(frequency, adapter, service);
	dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);

	return 0;
}

void dvb_scanForEPG( uint32_t adapter, uint32_t frequency )
{
	struct section_buf eit_filter;
	int counter = 0;

	if(!dvbfe_isLinuxAdapter(adapter)) {
		dvb_filtersUnlock();
	}

	dvb_filterSetup(&eit_filter, adapter, 0x12, -1, 2, &dvb_services); /* EIT */
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

void dvb_scanForPSI( uint32_t adapter, uint32_t frequency, list_element_t **out_list )
{
	struct section_buf pat_filter;
	int counter = 0;

	if(out_list == NULL) {
		out_list = &dvb_services;
	}

	dvb_filterSetup(&pat_filter, adapter, 0x00, 0x00, 2, out_list); /* PAT */
	pat_filter.running_counter = &counter;
	dvb_filterAdd(&pat_filter);
	do {
		dvb_filtersRead(frequency);
		//dprintf("DVB: pat %d, counter %d\n", pat_filter.start_time, counter);
		usleep(1); // pass control to other threads that may want to call dvb_filtersRead
	} while((pat_filter.start_time == 0) || (counter > 0));

	//dvb_filterServices(out_list);
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


#ifdef STSDK
static int32_t dvb_frequencyScanOne(uint32_t adapter, uint32_t frequency, EIT_media_config_t *media,
						dvb_displayFunctionDef* pFunction, dvbfe_cancelFunctionDef* pCancelFunction, uint32_t enableNit)
{
	float freqMHz = (float)dvbfe_frequencyKHz(adapter, frequency) / (float)KHZ;
	EIT_media_config_t local_media;

	if(media == NULL) {
		dvbfe_fillMediaConfig(adapter, frequency, &local_media);
		media = &local_media;
	}

	media->dvb_t.plp_id = 0;
	do {
		if(dvbfe_setParam(adapter, 1, media, pCancelFunction) != 0) {
			eprintf("%s(): adapter=%d, failed to set frequency to %.3f MHz\n", __func__, adapter, freqMHz);
			return -1;
		}

		if(dvbfe_isCurrentDelSys_dvbt2(adapter)) {
			printf("%s:%s()[%d]: is dvb-t2=%d, plp_id=%d\n", __FILE__, __func__, __LINE__, dvbfe_isCurrentDelSys_dvbt2(adapter), media->dvb_t.plp_id);
			appControlInfo.dvbtInfo.plp_id = media->dvb_t.plp_id;
			if(media->dvb_t.plp_id == 0) {
				media->dvb_t.generation = 2;
			}
		}
		eprintf("%s(): adapter=%d, scanning %.3f MHz\n", __func__, adapter, freqMHz);
		dvb_scanForServices(frequency, adapter, enableNit);

		media->dvb_t.plp_id++;

	} while((dvbfe_isCurrentDelSys_dvbt2(adapter) == 1) && (media->dvb_t.plp_id <= 3));


	return 0;
}
#endif

static int32_t dvb_isFrequencyesEqual(uint32_t adapter, uint32_t freq1, uint32_t freq2)
{
	uint32_t diff;
	uint32_t range = 0;
	switch(dvbfe_getType(adapter)) {
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

int dvb_frequencyScan(uint32_t adapter, __u32 frequency, EIT_media_config_t *media,
						dvb_displayFunctionDef* pFunction,
						int save_service_list, dvbfe_cancelFunctionDef* pCancelFunction)
{
	int current_frequency_number = 1, max_frequency_number = 0;

	if(!appControlInfo.dvbCommonInfo.streamerInput) {
		struct dvb_frontend_info fe_info;
		if(dvbfe_open(adapter) < 0) {
			eprintf("%s(): Failed to open adapter=%d frontend\n", __func__, adapter);
			return -1;
		}

		dprintf("%s(): Scan adapter %d\n", __func__, adapter);

		if(dvbfe_isLinuxAdapter(adapter)) {
			// Use the FE's own start/stop freq. for searching (if not explicitly app. defined).
			dvbfe_getFrontendInfo(adapter, &fe_info);
			if((frequency < fe_info.frequency_min) || (frequency > fe_info.frequency_max)) {
				eprintf("%s(): Adapter=%d, frequency %u is out of range [%u:%u]!\n", __func__, adapter,
					frequency, fe_info.frequency_min, fe_info.frequency_max);
				dvbfe_close(adapter);
				return 1;
			}
		}

#ifdef STSDK
		dvb_frequencyScanOne(adapter, frequency, media, pFunction, pCancelFunction, appControlInfo.dvbCommonInfo.networkScan);

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
					!dvb_isFrequencyesEqual(adapter, tsrtream->media.frequency, frequency) )
				{
					dvb_frequencyScanOne(adapter, tsrtream->media.frequency, &tsrtream->media, pFunction, pCancelFunction, 0);
	//				max_frequency_number++;
				}
				tstream_element = tstream_element->next;
			}
		}

		if(save_service_list) {
			dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
		}

		dvbfe_close(adapter);
		return 0;
#endif

		ZERO_SCAN_MESAGE();

		SCAN_MESSAGE("DVB[%d]: Scan: Freq=%u\n", adapter, frequency);
		SCAN_MESSAGE("==================================================================================\n");

		eprintf     ("DVB[%d]: Scan: Freq=%u\n", adapter, frequency);
		eprintf     ("==================================================================================\n");

		__u32 f = frequency;
		__u32 match_ber;
		int i;

		dprintf("%s[%d]: Check main freq: %u\n", __FUNCTION__, adapter, f);

		int found = dvbfe_checkFrequency(dvbfe_getType(adapter), f, adapter,
										&match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
		if(found == -1) {
			dprintf("%s[%d]: aborted by user\n", __FUNCTION__, adapter);
			goto frequency_scan_failed;
		}

		/* Just keep adapter on frequency */
		if(save_service_list == -1) {
			for(;;) {
				if (pFunction == NULL ||
				    pFunction(frequency, 0, adapter, current_frequency_number, max_frequency_number) == -1)
				{
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, adapter);
					break;
				}
				sleepMilli(500);
			}
			dvbfe_close(adapter);
			return 1;
		} else if(save_service_list == -2) {/* Or just return found value */
			dvbfe_close(adapter);
			return found;
		}

		if(appControlInfo.dvbCommonInfo.extendedScan) {
			for(i=1; !found && (i<=MAX_OFFSETS); i++) {
				long offset = i * fe_info.frequency_stepsize;

				f = frequency - offset;
				dprintf("%s[%d]: Check lower freq: %u\n", __FUNCTION__, adapter, f);
				found = dvbfe_checkFrequency(dvbfe_getType(adapter), f, adapter,
											&match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
				if(found == -1) {
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, adapter);
					goto frequency_scan_failed;
				}
				if(found)
					break;
				f = frequency + offset;
				dprintf("%s[%d]: Check higher freq: %u\n", __FUNCTION__, adapter, f);
				found = dvbfe_checkFrequency(dvbfe_getType(adapter), f, adapter,
									&match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
				if (found == -1) {
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, adapter);
					goto frequency_scan_failed;
				}
			}
		}

		if(found) {
			dprintf("%s[%d]: Found something on %u, search channels!\n", __FUNCTION__, adapter, f);
			SCAN_MESSAGE("DVB[%d]: Found something on %u, search channels!\n", adapter, f);
			/* Scan for channels within this frequency / transport stream */
			dvb_scanForServices(f, adapter, 0);
			SCAN_MESSAGE("DVB[%d]: Found %d channels ...\n", adapter, dvb_getNumberOfServices());
			dprintf("%s[%d]: Found %d channels ...\n", __FUNCTION__, adapter, dvb_getNumberOfServices());
		}

	} else {
		dvb_scanForServices(frequency, adapter, 0);
		dprintf("%s[%d]: Found %d channels on %u\n", __FUNCTION__, adapter, dvb_getNumberOfServices(), frequency);
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
					          adapter, current_frequency_number, max_frequency_number) == -1)
				{
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, adapter);
					break;
				}
				eprintf("%s[%d]: Network Scan: %u Hz\n", __FUNCTION__, adapter, tsrtream->media.frequency);
				dvb_frequencyScan(adapter, tsrtream->media.frequency,
				                  &tsrtream->media, pFunction, 0, pCancelFunction);
				current_frequency_number++;
			}
			tstream_element = tstream_element->next;
		}
	}

	if(save_service_list) {
		dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
	}

	dvbfe_close(adapter);
	dprintf("%s[%d]: Frequency %u scan completed\n", __FUNCTION__, adapter, frequency);
	return 0;

frequency_scan_failed:
	dvbfe_close(adapter);
	return -1;
}

/*
   clear and inits the given instance.
   @param mode @b IN The DVB mode to set up
*/
static void dvb_instanceReset(struct dvb_instance * dvb, uint32_t adapter)
{
	memset(dvb, 0, sizeof(struct dvb_instance));

	dvb->adapter = adapter;
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

static int dvb_demuxerStart(struct dvb_instance *dvb, EIT_media_config_t *media)
{
	dprintf("%s[%d]: setting filterv=%hu filtera=%hu filtert=%hu filterp=%hu\n", __FUNCTION__, dvb->adapter,
	        dvb->filterv.pid, dvb->filtera.pid, dvb->filtert.pid, dvb->filterp.pid );

	ioctl_or_abort(dvb->adapter, dvb->fdv, DMX_SET_PES_FILTER, &dvb->filterv);

#ifdef ENABLE_MULTI_VIEW
	if(dvb->mode == DvbMode_Multi) {
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
			dprintf("%s(): Adapter=%d INIT PCR %hu\n", __func__, dvb->adapter, dvb->filterp.pid);
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
#define open_or_abort(adapter, name, path, mode, fd)                             \
	do {                                                                       \
		if ((fd = open(path, mode)) < 0) {                                     \
			PERROR("%s[%d]: failed to open %s for %s: %s\n",                   \
				__FUNCTION__, adapter, path, name, strerror(errno));             \
			return -1;                                                         \
		}                                                                      \
	} while(0)

static int dvb_instanceOpen(struct dvb_instance * dvb, char *pFilename)
{
#ifdef LINUX_DVB_API_DEMUX
	uint32_t adapter = dvb->adapter;

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

	dprintf("%s[%d]: ok\n", __FUNCTION__, adapter);
#endif //#ifdef LINUX_DVB_API_DEMUX
	return 0;
}

int32_t dvb_getTeletextFD(uint32_t adapter, int32_t *dvr_fd)
{
	struct dvb_instance *dvb = &(dvbInstances[adapter]);

	if(dvb->filtert.pid > 0) {
		*dvr_fd = dvb->fdin;
		return 0;
	}

	return -1;
}
#undef open_or_abort

#define CLOSE_FD(adapter, name, fd)                                              \
	do {                                                                       \
		if (fd > 0 && close(fd) < 0)                                           \
			eprintf("%s[%d]: " name " closed with error: %s\n",                \
				__FUNCTION__, adapter, strerror(errno));                         \
	} while(0)

static void dvb_instanceClose (struct dvb_instance * dvb)
{
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

int32_t dvb_getCountOfServices(void)
{
    list_element_t *service_element;
    int serviceCount = 0;
    mysem_get(dvb_semaphore);
    for( service_element = dvb_services; service_element != NULL; service_element = service_element->next )
    {
        serviceCount++;
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
	if(service != NULL) {
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

int dvb_getServiceFrequency(EIT_service_t *service, uint32_t *pFrequency)
{
	int32_t ret = -1;
	if(service == NULL) {
		return -1;
	}

	mysem_get(dvb_semaphore);
	switch(service->media.type) {
		case serviceMediaDVBT:
		case serviceMediaDVBC:
		case serviceMediaDVBS:
		case serviceMediaATSC:
			*pFrequency = service->media.frequency;
			ret = 0;
			break;
		default:
			break;
	}
	mysem_release(dvb_semaphore);
	return ret;
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
	const table_IntStr_t service_typeName[] = {
		{serviceMediaDVBT,	"DVB-T"},
		{serviceMediaDVBC,	"DVB-C"},
		{serviceMediaDVBS,	"DVB-S"},
		{serviceMediaATSC,	"ATSC"},

		TABLE_INT_STR_END_VALUE,
	};

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

	typeName = table_IntStrLookup(service_typeName, service->media.type, NULL);
	if(typeName) {
		uint8_t generation = 0;
		if(service->media.type == serviceMediaDVBT) {
			generation = service->media.dvb_t.generation;
		}
		if(generation <= 1) {
			sprintf(buf, "%s:\n", typeName);
		} else {
			sprintf(buf, "%s%d:\n", typeName, generation);
		}
		buf += strlen(buf);
	}
	sprintf(buf,"  %s: %u MHz\n", _T("DVB_FREQUENCY"),
			(service->media.type == serviceMediaDVBS) ? service->media.frequency / 1000 : service->media.frequency / 1000000);
	buf += strlen(buf);

	switch(service->media.type) {
		case serviceMediaDVBT:
			if(service->media.dvb_t.generation == 2) {
				sprintf(buf,"  plp id: %d\n", service->media.dvb_t.plp_id);
				buf += strlen(buf);
			}
			break;
		case serviceMediaDVBC:
			sprintf(buf,"  %s: %u KBd\n", _T("DVB_SYMBOL_RATE"), service->media.dvb_c.symbol_rate / 1000);
			buf += strlen(buf);
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

int dvb_changeAudioPid(uint32_t adapter, uint16_t aPID)
{
	if(!dvbfe_isLinuxAdapter(adapter)) {
		eprintf("%s[%d]: unsupported\n", __FUNCTION__, adapter);
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

int dvb_startDVB(DvbParam_t *pParam)
{
	if(!dvbfe_isLinuxAdapter(pParam->adapter)) {
		eprintf("%s: Invalid adapter - use range 0-%d\n", __FUNCTION__, MAX_ADAPTER_SUPPORTED - 1);
		return -1;
	}

	dvb_filtersFlush();
	dvb_filtersUnlock();

	struct dvb_instance *dvb = &dvbInstances[pParam->adapter];
	dvb_instanceReset(dvb, pParam->adapter);

	dprintf("%s[%d]: set mode %s\n", __FUNCTION__, pParam->adapter, pParam->mode == DvbMode_Watch ? "Watch" :
		(pParam->mode == DvbMode_Multi ? "Multi" : (pParam->mode == DvbMode_Record ? "Record" : "Play")));
	dvb->mode = pParam->mode;

#ifdef LINUX_DVB_API_DEMUX
	dvb_demuxerInit(dvb, pParam->mode);
	if(dvb_demuxerSetup(dvb, pParam) != 0) {
		eprintf("%s[%d]: unknown mode %d!\n", __FUNCTION__, pParam->adapter, pParam->mode);
		return -1;
	}
#endif

	if(dvb_instanceOpen(dvb, pParam->directory) != 0) {
		eprintf("%s[%d]: Failed to open dvb instance\n", __FUNCTION__, pParam->adapter);
		return -1;
	}

	if(dvb->mode != DvbMode_Play) {
		if(!appControlInfo.dvbCommonInfo.streamerInput) {
			if(dvbfe_open(pParam->adapter) < 0) {
				PERROR("%s[%d]: failed to open frontend", __FUNCTION__, pParam->adapter);
#ifndef ENABLE_DVB_PVR
				return -1;
#endif
			}
		}

		if(dvbfe_setParam(pParam->adapter, 0, pParam->media, NULL) != 0) {
			eprintf("%s(): Failed to set adapter%d params\n", __FUNCTION__, pParam->adapter);
			return -1;
		}
	}

#ifdef LINUX_DVB_API_DEMUX
	dprintf("%s[%d]: start demuxer\n", __FUNCTION__, pParam->adapter);
	if(dvb_demuxerStart(dvb, pParam->media) != 0) {
		eprintf("%s[%d]: Failed to change service!\n", __FUNCTION__, pParam->adapter);
		return 0;
	}
#endif // LINUX_DVB_API_DEMUX

	int ret = 0;
	pthread_attr_t attr;
	struct sched_param param = { 90 };

	pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam (&attr, &param);

#ifdef ENABLE_DVB_PVR
	if(dvb->mode == DvbMode_Play || dvb->mode == DvbMode_Record) {
		dprintf("%s[%d]: DvbMode_%s\n", __FUNCTION__, pParam->adapter, dvb->mode == DvbMode_Play ? "Play" : "Record");

		ret = pthread_create(&dvb->pvr.thread, &attr, dvb_pvrThread, dvb);
		if(ret != 0) {
			eprintf("%s[%d]: error %d starting dvb_pvrThread\n", __FUNCTION__, pParam->adapter, ret);
		}
	} else //if (dvb->fdf > 0)
#endif // ENABLE_DVB_PVR
	{
		dprintf("%s[%d]: DvbMode_Watch\n", __FUNCTION__, pParam->adapter);
		dvbfe_trackerStart(pParam->adapter);

#ifdef ENABLE_MULTI_VIEW
		if(dvb->mode == DvbMode_Multi) {
			ret = pthread_create(&dvb->multi_thread, &attr, dvb_multiThread, dvb);
			if(ret != 0) {
				dvb->multi_thread = 0;
				eprintf("%s[%d]: pthread_create (multiview) err=%d\n", __FUNCTION__, pParam->adapter, ret);
			}
		}
#endif
	}
	pthread_attr_destroy(&attr);
	if(ret != 0) {
		dprintf("%s[%d]: ok\n", __FUNCTION__, pParam->adapter);
		return -1;
	}
	return 0;
}

void dvb_stopDVB(uint32_t adapter, int reset)
{
	if(dvbfe_isLinuxAdapter(adapter)) {
		dprintf("%s[%d]: reset %d\n", __FUNCTION__, adapter, reset);

		struct dvb_instance *dvb = &dvbInstances[adapter];

#ifdef ENABLE_DVB_PVR
		if(dvb->pvr.thread != 0) {
			pthread_cancel (dvb->pvr.thread);
			pthread_join (dvb->pvr.thread, NULL);
			dvb->pvr.thread = 0;
		}
#endif
#ifdef ENABLE_MULTI_VIEW
		if(dvb->multi_thread != 0) {
			pthread_cancel (dvb->multi_thread);
			pthread_join (dvb->multi_thread, NULL);
			dvb->multi_thread = 0;
		}
#endif
		dvb_filtersFlush();
		dvb_instanceClose(dvb);
		dvb_instanceReset(dvb, adapter);
		if(reset) {
			/* Reset the stored frequency as the driver    */
			/* puts the adapter into standby so it needs to  */
			/* be woken up when we next do a set frequency */
			dvbfe_trackerStop(adapter);
			dvbfe_close(adapter);
		}
		dprintf("%s[%d]: out\n", __FUNCTION__, adapter);
	}
}

void dvb_init(void)
{
	dprintf("%s: in\n", __FUNCTION__);

	dvbfe_init();

	mysem_create(&dvb_semaphore);
	mysem_create(&dvb_filter_semaphore);

	if(helperFileExists(appControlInfo.dvbCommonInfo.channelConfigFile)){
		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
		dprintf("%s: loaded %d services\n", __FUNCTION__, dvb_getNumberOfServices());
	}

	dprintf("%s: out\n", __FUNCTION__);
}

void dvb_terminate(void)
{
	int i;
	for(i = 0; i < MAX_ADAPTER_SUPPORTED; i++) {
		dvb_stopDVB(i, 1);
	}

	free_services(&dvb_services);

	mysem_destroy(dvb_semaphore);
	mysem_destroy(dvb_filter_semaphore);
	
	dvbfe_terminate();
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
	if(which >= 0 && which < 2) {
		struct dvb_instance *dvb = &dvbInstances[which+2];

		/* Calculate the number of pvr 'part' files */
		pPosition->index = dvb_get_numPartPvrFiles(dvb, 0, 99);
		pPosition->offset = dvb_getPvrFileLength(dvb, pPosition->index);
	}
}

void dvb_getPvrPosition(int which, DvbFilePosition_t *pPosition)
{
	if(which >= 0 && which < 2) {
		struct dvb_instance *dvb = &dvbInstances[which+2];
		pPosition->index = dvb->fileIndex;
		pPosition->offset = dvb->pvr.position;
	}
}

int dvb_getPvrRate(int which)
{
	if(which >= 0 && which < 2) {
		struct dvb_instance *dvb = &dvbInstances[which+2];
		return dvb->pvr.rate;
	}
	return DEFAULT_PVR_RATE;
}
#endif // ENABLE_DVB_PVR

#endif /* ENABLE_DVB */
