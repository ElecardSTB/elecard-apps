
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

#ifdef ENABLE_DVB

#include "debug.h"
#include "list.h"
#include "app_info.h"
#include "StbMainApp.h"
#include "interface.h"
#include "l10n.h"
#include "playlist.h"
#include "sem.h"
#include "teletext.h"
#include "stsdk.h"

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

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define fatal   eprintf
#define info    dprintf
#define verbose(...)
#define debug(...)

#ifndef STSDK
#define LINUX_DVB_API_DEMUX
#endif

#define PERROR(fmt, ...) eprintf(fmt " (%s)\n", ##__VA_ARGS__, strerror(errno))

#define AUDIO_CHAN_MAX (8)

#define MAX_OFFSETS   (1)
#define FE_TYPE_COUNT (4)
#define FE_MAX_SUPPORTED (FE_OFDM)

#define MAX_RUNNING   (32)

#define FILE_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

// For front end signal tracking
// sec.s  between FE checks
#define TRACK_INTERVAL (2)

/* SD bit rate ~ 1.5GB / hour -> ~ 450KB / second */
#define DEFAULT_PVR_RATE (450*1024)

#define MEDIA_ID         (unsigned long)frequency

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

#define ioctl_loop(tuner, err, fd, request, ...)                                    \
	do {                                                                       \
		err = ioctl(fd, request, ##__VA_ARGS__);                               \
		if (err < 0) {                                                         \
			info("%s[%d]: ioctl " #request " failed", __FUNCTION__, tuner);    \
		}                                                                      \
	} while (err<0)                                                            \

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

// defines one chain of devices which together make up a DVB receiver/player
struct dvb_instance {
	// file descriptors for frontend, demux-video, demux-audio
	int fdf;
	__u32 setFrequency;
	int vmsp;

	DvbMode_t mode;
	fe_type_t fe_type;
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
	int fdin;

	// device setup data
	struct dmx_pes_filter_params filterv;
#ifdef ENABLE_MULTI_VIEW
	struct dmx_pes_filter_params filterv1;
	struct dmx_pes_filter_params filterv2;
	struct dmx_pes_filter_params filterv3;
#endif
	struct dmx_pes_filter_params filtera;
	struct dmx_pes_filter_params filterp;
	struct dmx_pes_filter_params filtert;
	struct dvb_frontend_parameters tuner;

#ifdef ENABLE_PVR
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
#ifdef ENABLE_TELETEXT
	pthread_t teletext_thread;
#endif
#endif // LINUX_DVB_API_DEMUX
};

enum table_type {
	PAT,
	PMT,
	SDT,
	NIT
};

enum running_mode {
	RM_NOT_RUNNING = 0x01,
	RM_STARTS_SOON = 0x02,
	RM_PAUSING     = 0x03,
	RM_RUNNING     = 0x04
};

struct section_buf {
	struct list_head list;
	const char *dmx_devname;
	int fd;
	int pid;
	int table_id;
	int table_id_ext;
	int section_version_number;
	unsigned char section_done[32];
	int sectionfilter_done;
	unsigned char buf[BUFFER_SIZE];
	time_t timeout;
	time_t start_time;
	time_t running_time;
	unsigned long transport_stream_id;
	unsigned long is_dynamic;
	unsigned long was_updated;
	NIT_table_t *nit;
	list_element_t **service_list;
	int *running_counter; // if not null, used as reference counter by this and child (PAT->PMT) filters
	struct section_buf *next_seg; /* this is used to handle
	                               * segmented tables (like NIT-other)
	                               */
};

/***********************************************
* EXPORTED DATA                                *
************************************************/

list_element_t *dvb_services = NULL;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int dvb_openFrontend(tunerFormat tuner, int flags);
static int dvb_getFrontendInfo(tunerFormat tuner, int flags, struct dvb_frontend_info *fe_info);
static inline int dvb_isSupported(fe_type_t  type)
{
	return type <= FE_MAX_SUPPORTED;
}

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

#ifdef LINUX_DVB_API_DEMUX
static LIST_HEAD(running_filters);
static LIST_HEAD(waiting_filters);
static int n_running;
static struct pollfd poll_fds[MAX_RUNNING];
static struct section_buf* poll_section_bufs[MAX_RUNNING];
static int lock_filters = 1;
static inline void dvb_filtersLock(void)   { lock_filters = 1; }
static inline void dvb_filtersUnlock(void) { lock_filters = 0; }
#else
#define dvb_filtersUnlock()
#define dvb_filtersLock()
#endif

static struct dvb_instance dvbInstances[VMSP_COUNT];
static __u32 currentFrequency[VMSP_COUNT];
static char * gFE_Type_Names[FE_TYPE_COUNT] = {
	"QPSK",
	"FE_QAM",
	"FE_OFDM",
	"FE_ATSC" };

static fe_type_t gFE_type=FE_OFDM;    //will be used as if all FEs are of identical type.

static pmysem_t dvb_semaphore;
static pmysem_t dvb_fe_semaphore;
static pmysem_t dvb_filter_semaphore;

//char scan_messages[64*1024];
#define ZERO_SCAN_MESAGE()	//scan_messages[0] = 0
#define SCAN_MESSAGE(...)	//sprintf(&scan_messages[strlen(scan_messages)], __VA_ARGS__)

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

#ifdef LINUX_DVB_API_DEMUX
static void dvb_update_poll_fds(void)
{
	struct list_head *p;
	struct section_buf* s;
	int i;

	memset(poll_section_bufs, 0, sizeof(poll_section_bufs));
	for (i = 0; i < MAX_RUNNING; i++)
		poll_fds[i].fd = -1;
	i = 0;
	list_for_each (p, &running_filters) {
		if (i >= MAX_RUNNING)
			fatal("%s: too many poll_fds\n", __FUNCTION__);
		s = list_entry (p, struct section_buf, list);
		if (s->fd == -1)
			fatal("%s: s->fd == -1 on running_filters\n", __FUNCTION__);
		poll_fds[i].fd = s->fd;
		poll_fds[i].events = POLLIN;
		poll_fds[i].revents = 0;
		poll_section_bufs[i] = s;
		i++;
	}
	if (i != n_running)
		fatal("%s: n_running is hosed\n", __FUNCTION__);
}

static int dvb_filterStart (struct section_buf* s)
{
	struct dmx_sct_filter_params f;

	if (n_running >= MAX_RUNNING)
		goto err0;
	if ((s->fd = open (s->dmx_devname, O_RDWR | O_NONBLOCK)) < 0)
		goto err0;

	debug("%s: start filter 0x%02x\n", __FUNCTION__, s->pid);

	memset(&f, 0, sizeof(f));

	f.pid = (__u16) s->pid;

	if (s->table_id < 0x100 && s->table_id > 0) {
		f.filter.filter[0] = (unsigned char) s->table_id;
		f.filter.mask[0]   = 0xff;
	}

	f.timeout = 0;
	f.flags = DMX_IMMEDIATE_START | DMX_CHECK_CRC;

	if (ioctl(s->fd, DMX_SET_FILTER, &f) == -1) {
		PERROR("%s: ioctl DMX_SET_FILTER failed", __FUNCTION__);
		goto err1;
	}

	s->sectionfilter_done = 0;
	time(&s->start_time);

	if (s->running_counter != NULL)
	{
		(*s->running_counter)++;
	}

	list_del_init (&s->list);  /* might be in waiting filter list */
	list_add (&s->list, &running_filters);

	n_running++;
	dvb_update_poll_fds();

	return 0;

err1:
	ioctl (s->fd, DMX_STOP);
	close (s->fd);
err0:
	return -1;
}

static void dvb_filterStop (struct section_buf *s)
{
	debug("%s: stop filter %d\n", __FUNCTION__, s->pid);
	ioctl (s->fd, DMX_STOP);
	close (s->fd);
	s->fd = -1;
	list_del (&s->list);
	s->running_time += time(NULL) - s->start_time;
	
	if (s->running_counter != NULL)
	{
		(*s->running_counter)--;
	}

	n_running--;
	dvb_update_poll_fds();
}

static void dvb_filterAdd (struct section_buf *s)
{
	if (!lock_filters)
	{
		if (dvb_filterStart (s))
			list_add_tail (&s->list, &waiting_filters);
	} else
	{
		debug("%s: filters are locked\n", __FUNCTION__);
		if (s->is_dynamic)
		{
			debug("%s: free dynamic filter\n", __FUNCTION__);
			dfree(s);
		}
	}
}

static void dvb_filterRemove (struct section_buf *s)
{
	dvb_filterStop (s);
	if (s->is_dynamic)
	{
		debug("%s: free dynamic filter\n", __FUNCTION__);
		dfree(s);
	}

	debug("%s: start waiting filters\n", __FUNCTION__);

	while (!list_empty(&waiting_filters)) {
		struct list_head *next = waiting_filters.next;
		s = list_entry (next, struct section_buf, list);
		if (dvb_filterStart (s))
			break;
	};
}

static void dvb_filterSetup (struct section_buf* s, const char *dmx_devname,
			  int pid, int tid, int timeout, NIT_table_t *nit, list_element_t **outServices)
{
	memset (s, 0, sizeof(struct section_buf));

	s->fd = -1;
	s->dmx_devname = dmx_devname;
	s->pid = pid;
	s->table_id = tid;
	s->timeout = timeout;
	s->table_id_ext = -1;
	s->section_version_number = -1;
	s->transport_stream_id = -1;
	s->nit = nit;
	s->service_list = outServices;

	INIT_LIST_HEAD (&s->list);
}

static void dvb_filtersFlush()
{
	int i;
	struct section_buf *s;

	dvb_filtersLock();

	/* Flush waiting filter pool */
	while (!list_empty(&waiting_filters)) {
		struct list_head *next = waiting_filters.next;
		s = list_entry (next, struct section_buf, list);
		list_del (&s->list);
		if (s->is_dynamic)
		{
			dfree(s);
		}
	};
	/* Flush running filter pool */
	while (n_running > 0)
	{
		for (i = 0; i < n_running; i++)
		{
			s = poll_section_bufs[i];
			if (!s)
				fatal("%s: poll_section_bufs[%d] is NULL\n", __FUNCTION__, i);
			dvb_filterRemove (s);
		}
	}
}

static int get_bit (unsigned char *bitfield, int bit)
{
	return (bitfield[bit/8] >> (bit % 8)) & 1;
}

static void set_bit (unsigned char *bitfield, int bit)
{
	bitfield[bit/8] |= 1 << (bit % 8);
}

/**
 *   returns 0 when more sections are expected
 *	   1 when all sections are read on this pid
 *	   -1 on invalid table id
 */
static int dvb_sectionParse (long frequency, char* demux_devname, struct section_buf *s)
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

	if (s->service_list != NULL)
	{
		services = s->service_list;
	} else
	{
		services = &dvb_services;
	}

	switch(gFE_type)
	{
		case FE_OFDM:
			media.type = serviceMediaDVBT;
			media.dvb_t.centre_frequency = frequency;
			media.dvb_t.inversion = appControlInfo.dvbtInfo.fe.inversion;
			media.dvb_t.bandwidth = appControlInfo.dvbtInfo.bandwidth;
			break;
		case FE_QAM:
			media.type = serviceMediaDVBC;
			media.dvb_c.frequency = frequency;
			media.dvb_c.inversion = appControlInfo.dvbcInfo.fe.inversion;
			media.dvb_c.symbol_rate = appControlInfo.dvbcInfo.symbolRate*1000;
			media.dvb_c.modulation = appControlInfo.dvbcInfo.modulation;
			break;
		default:
			media.type = serviceMediaNone;
	}

	dprintf("%s: media type %s, freq %u\n", __FUNCTION__, gFE_Type_Names[gFE_type], media.frequency);

	table_id = s->buf[0];

	if (s->table_id >= 0 && table_id != s->table_id)
		return -1;

	//unsigned int section_length = (((buf[1] & 0x0f) << 8) | buf[2]) - 11;
	table_id_ext = (s->buf[3] << 8) | s->buf[4];
	section_version_number = (s->buf[5] >> 1) & 0x1f;
	section_number = s->buf[6];
	last_section_number = s->buf[7];

	if (s->section_version_number != section_version_number ||
			s->table_id_ext != table_id_ext) {
		struct section_buf *next_seg = s->next_seg;

		if (s->section_version_number != -1 && s->table_id_ext != -1)
		{
			verbose("%s: section version_number or table_id_ext changed "
				"%d -> %d / %04x -> %04x\n", __FUNCTION__,
				s->section_version_number, section_version_number,
				s->table_id_ext, table_id_ext);
		}
		s->table_id_ext = table_id_ext;
		s->section_version_number = section_version_number;
		s->sectionfilter_done = 0;
		memset (s->section_done, 0, sizeof(s->section_done));
		s->next_seg = next_seg;
	}

	if (!get_bit(s->section_done, section_number))
	{
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
			while ( cur != NULL)
			{
				service = (EIT_service_t*)cur->data;
				if( (service->flags & serviceFlagHasPAT) &&
				     service->media.frequency == (unsigned long)frequency )
				{
					pmt_filter = dmalloc(sizeof(struct section_buf));
					if( pmt_filter != NULL )
					{
						dprintf("%s: new pmt filter ts %d, pid %d, service %d, media %d\n", __FUNCTION__,
						        service->common.transport_stream_id,
						        service->program_map.program_map_PID,
						        service->common.service_id,
						        service->common.media_id);
						dvb_filterSetup(pmt_filter, demux_devname,
						                service->program_map.program_map_PID, 0x02, 2, s->nit, services);
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
			while (cur != NULL)
			{
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
			if (s->nit != NULL)
			{
				mysem_get(dvb_semaphore);
				updated = parse_nit(services, (unsigned char *)s->buf, MEDIA_ID, &media, s->nit);
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
				if (updated)
				{
					s->timeout = 5;
				}
				mysem_release(dvb_semaphore);
				can_count_sections = 0;
				break;
		default:
			dprintf("%s: Unknown 0x%04x for service 0x%04x", __FUNCTION__, s->pid, table_id_ext);
		};

		// FIXME: This is the only quick solution I see now, because each service in EIT has its own last_section_number
		// we need to count services and track each of them...

		if (can_count_sections)
		{
			int i;

			for (i = 0; i <= last_section_number; i++)
				if (get_bit (s->section_done, i) == 0)
					break;

			if (i > last_section_number)
			{
				dprintf("%s: Finished pid 0x%04x for table 0x%04x\n", __FUNCTION__, s->pid, s->table_id);
				s->sectionfilter_done = 1;
			}
		}
	}

	if (updated)
	{
		s->was_updated = 1;
	}

	dprintf("%s: parsed media %d,  0x%04X table 0x%02X, updated %d, done %d\n", __FUNCTION__,
	        MEDIA_ID, s->pid, table_id, updated, s->sectionfilter_done);

	if (s->sectionfilter_done)
		return 1;

	return 0;
}

static int dvb_sectionRead (__u32 frequency, char * demux_devname, struct section_buf *s)
{
	int section_length, count;

	if (s->sectionfilter_done)
		return 1;

	/* 	the section filter API guarantess that we get one full section
	 * per read(), provided that the buffer is large enough (it is)
	 */
	if (((count = read (s->fd, s->buf, sizeof(s->buf))) < 0) && errno == EOVERFLOW)
		count = read (s->fd, s->buf, sizeof(s->buf));
	if (count < 0 && errno != EAGAIN) {
		dprintf("%s: Read error %d (errno %d) pid 0x%04x\n", __FUNCTION__, count, errno, s->pid);
		return -1;
	}

	//dprintf("%s: READ %d bytes!!!\n", __FUNCTION__, count);

	if (count < 4)
		return -1;

	section_length = ((s->buf[1] & 0x0f) << 8) | s->buf[2];

	//dprintf("%s: READ %d bytes, section length %d!!!\n", __FUNCTION__, count, section_length);

	if (count != section_length + 3)
		return -1;

	if (dvb_sectionParse(frequency, demux_devname, s) == 1)
		return 1;

	return 0;
}

static void dvb_filtersRead (__u32 frequency, char* demux_devname)
{
	struct section_buf *s;
	int i, n, done;

	mysem_get(dvb_filter_semaphore);

	n = poll(poll_fds, n_running, 1000);
	if (n == -1)
		PERROR("%s: filter poll failed", __FUNCTION__);

	for (i = 0; i < n_running; i++) {
		s = poll_section_bufs[i];
		if (!s)
		{
			fatal("%s: poll_section_bufs[%d] is NULL\n", __FUNCTION__, i);
			continue;
		}
		if (poll_fds[i].revents)
			done = dvb_sectionRead (frequency, demux_devname, s) == 1;
		else
			done = 0; /* timeout */
		if (done || time(NULL) > s->start_time + s->timeout) {
			if (!done)
			{
				SCAN_MESSAGE("DVB: filter timeout pid 0x%04x\n", s->pid);
				debug("%s: filter timeout pid 0x%04x\n", __FUNCTION__, s->pid);
			}
			dvb_filterRemove (s);
		}
	}

	mysem_release(dvb_filter_semaphore);
}
#else
#define dvb_filtersFlush()
#endif // LINUX_DVB_API_DEMUX

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

static void dvb_scanForServices (long frequency, char * demux_devname, NIT_table_t *nit)
{
#ifdef LINUX_DVB_API_DEMUX
	struct section_buf pat_filter;
	struct section_buf sdt_filter;
	//struct section_buf sdt1_filter;
	struct section_buf eit_filter;
	struct section_buf nit_filter;

	dvb_filtersUnlock();
	/**
	 *  filter timeouts > min repetition rates specified in ETR211
	 */
	dvb_filterSetup (&pat_filter, demux_devname, 0x00, 0x00, 5, nit, &dvb_services); /* PAT */
	dvb_filterSetup (&nit_filter, demux_devname, 0x10, 0x40, 5, nit, &dvb_services); /* NIT */
	dvb_filterSetup (&sdt_filter, demux_devname, 0x11, 0x42, 5, nit, &dvb_services); /* SDT actual */
	//dvb_filterSetup (&sdt1_filter, demux_devname, 0x11, 0x46, 5, nit, &dvb_services); /* SDT other */
	dvb_filterSetup (&eit_filter, demux_devname, 0x12,   -1, 5, nit, &dvb_services); /* EIT */

	dvb_filterAdd (&pat_filter);
	dvb_filterAdd (&nit_filter);
	dvb_filterAdd (&sdt_filter);
	//dvb_filterAdd (&sdt1_filter);
	dvb_filterAdd (&eit_filter);

	do
	{
		dvb_filtersRead (frequency, demux_devname);
	} while (!(list_empty(&running_filters) && list_empty(&waiting_filters)));
	dvb_filtersLock();
#endif // LINUX_DVB_API_DEMUX
	//dvb_filterServices(&dvb_services);
}

void dvb_scanForEPG( tunerFormat tuner, unsigned long frequency )
{
#ifdef LINUX_DVB_API_DEMUX
	struct section_buf eit_filter;
	char demux_devname[32];
	int counter = 0;
	
	snprintf(demux_devname, sizeof(demux_devname), "/dev/dvb/adapter%d/demux0", tuner);

	dvb_filterSetup (&eit_filter, demux_devname, 0x12, -1, 2, NULL, &dvb_services); /* EIT */
	eit_filter.running_counter = &counter;
	dvb_filterAdd (&eit_filter);
	do
	{
		dvb_filtersRead (frequency, demux_devname);
		//dprintf("DVB: eit %d, counter %d\n", eit_filter.start_time, counter);
		usleep(1); // pass control to other threads that may want to call dvb_filtersRead
	} while (eit_filter.start_time == 0 || counter > 0);

	//dvb_filterServices(&dvb_services);

	if (eit_filter.was_updated)
	{
		/* Write the channel list to the root file system */
		dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
	}
#endif // LINUX_DVB_API_DEMUX
}

void dvb_scanForPSI( tunerFormat tuner, unsigned long frequency, list_element_t **out_list )
{
#ifdef LINUX_DVB_API_DEMUX
	struct section_buf pat_filter;
	char demux_devname[32];
	int counter = 0;

	snprintf(demux_devname, sizeof(demux_devname), "/dev/dvb/adapter%d/demux0", tuner);

	if (out_list == NULL)
	{
		out_list = &dvb_services;
	}

	dvb_filterSetup (&pat_filter, demux_devname, 0x00, 0x00, 2, NULL, out_list); /* PAT */
	pat_filter.running_counter = &counter;
	dvb_filterAdd (&pat_filter);
	do
	{
		dvb_filtersRead (frequency, demux_devname);
		//dprintf("DVB: pat %d, counter %d\n", pat_filter.start_time, counter);
		usleep(1); // pass control to other threads that may want to call dvb_filtersRead
	} while (pat_filter.start_time == 0 || counter > 0);
#endif // LINUX_DVB_API_DEMUX
	//dvb_filterServices(out_list);
}

#define BREAKABLE_SLEEP(us)										\
	{															\
		int _s;													\
		for (_s=0;_s<us;_s+=500000)								\
		{														\
			if (pFunction != NULL && pFunction() == -1)			\
			{													\
				return -1;										\
			}													\
			if (ioctl(frontend_fd, FE_READ_STATUS, &s) == -1)	\
			{													\
				eprintf("%s[%d]: FE_READ_STATUS failed: %s\n", __FUNCTION__, tuner, strerror(errno)); \
				return -1;										\
			}													\
			if (s & FE_HAS_LOCK) break;							\
			usleep(500000);										\
		}														\
	}															\

static int dvb_setFrequency(fe_type_t  type, __u32 frequency, int frontend_fd, int tuner,
                            int wait_for_lock, EIT_media_config_t *media, dvb_cancelFunctionDef* pFunction)
{
	if (appControlInfo.dvbCommonInfo.streamerInput)
		return 0;

	eprintf("%s[%d]: Current frequency %u, new frequency %u\n", __FUNCTION__, tuner, currentFrequency[tuner], frequency);
/*
	if (currentFrequency[tuner] == frequency) {
		fe_status_t s;
		__u32 ber = BER_THRESHOLD;

		ioctl_or_abort(tuner, frontend_fd, FE_READ_STATUS, &s);
		ioctl_or_abort(tuner, frontend_fd, FE_READ_BER, &ber);
		dprintf("%s[%d]: Check lock = %d, check ber = %u\n", __FUNCTION__, tuner, s & FE_HAS_LOCK, ber);
		if ((s & FE_HAS_LOCK) == 0 || ber >= BER_THRESHOLD) {
			eprintf("%s[%d]: Force retune to %u\n", __FUNCTION__, tuner, frequency);
			currentFrequency[tuner] = 0;
		}
	}

	// Only change the tuner frequency when absolutely necessary
	if (currentFrequency[tuner] == frequency)
		return 0;
*/
	struct dvb_frontend_parameters p;
	memset(&p, 0, sizeof(p));

	p.frequency = frequency;
	if (media && media->type == serviceMediaNone)
		media = NULL;
	if (type == FE_OFDM && (media == NULL || media->type == serviceMediaDVBT))
	{
		p.u.ofdm.bandwidth = media != NULL ? media->dvb_t.bandwidth :
		                     appControlInfo.dvbtInfo.bandwidth;
		p.u.ofdm.code_rate_HP = FEC_AUTO;
		p.u.ofdm.code_rate_LP = FEC_AUTO;
		p.u.ofdm.constellation = QAM_AUTO;
		p.u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
		p.u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
		p.u.ofdm.hierarchy_information = HIERARCHY_AUTO;
		p.inversion = appControlInfo.dvbtInfo.fe.inversion;
	} else
	if (type == FE_QAM && (media == NULL || media->type == serviceMediaDVBC))
	{
		if (media != NULL) {
			p.u.qam.modulation  = media->dvb_c.modulation;
			p.u.qam.symbol_rate = media->dvb_c.symbol_rate;
		} else {
			p.u.qam.modulation  = appControlInfo.dvbcInfo.modulation;
			p.u.qam.symbol_rate = appControlInfo.dvbcInfo.symbolRate*1000;
		}
		p.inversion = appControlInfo.dvbcInfo.fe.inversion;
		p.u.qam.fec_inner   = FEC_NONE;
		eprintf("   C: Symbol rate %u, modulation %u invertion %u\n",
				p.u.qam.symbol_rate, p.u.qam.modulation, p.inversion);
	} else
	if (type == FE_QPSK && (media == NULL || media->type == serviceMediaDVBS))
	{
		p.u.qpsk.symbol_rate = media != NULL ? media->dvb_s.symbol_rate :
		                       appControlInfo.dvbsInfo.symbolRate*1000;
		p.u.qpsk.fec_inner   = FEC_NONE;
		eprintf("   S: Symbol rate %u\n", p.u.qpsk.symbol_rate);
	} else
	{
		eprintf("%s[%d]: ERROR: Unsupported frontend type=%s (media %d).\n", __FUNCTION__, tuner,
			gFE_Type_Names[type], media ? (int)media->type : -1);
		return -1;
	}

	ioctl_or_abort(tuner, frontend_fd, FE_SET_FRONTEND, &p);

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
		/* If ber is not -1, then wait a bit more */
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
			/* locked, give adapter even more time... */
			//usleep (appControlInfo.dvbCommonInfo.adapterSpeed*10000);
		} else
		if (s & FE_HAS_SIGNAL) {
			if (hasSignal == 0) {
				eprintf("%s[%d]: Has signal\n", __FUNCTION__, tuner);
				timeout += appControlInfo.dvbCommonInfo.adapterSpeed;
				hasSignal = 1;
			}
			dprintf("%s[%d]: S (%d)\n", __FUNCTION__, tuner, timeout);
			/* found something above the noise level, increase timeout time */
			//usleep (appControlInfo.dvbCommonInfo.adapterSpeed*10000);
		} else
		{
			dprintf("%s[%d]: N (%d)\n", __FUNCTION__, tuner, timeout);
			/* there's no and never was any signal, reach timeout faster */
			if (hasSignal == 0)
			{
				eprintf("%s[%d]: Skip\n", __FUNCTION__, tuner);
				--timeout;
			}
		}
	} while (--timeout > 0 && ber >= BER_THRESHOLD);
	dprintf("%s[%d]: %u timeout %d, ber %u\n", __FUNCTION__, tuner, frequency, timeout, ber);
	currentFrequency[tuner] = frequency;
	eprintf("%s[%d]: Frequency set, lock: %d\n", __FUNCTION__, tuner, hasLock);
	return hasLock;
}
#undef BREAKABLE_SLEEP

static int dvb_checkFrequency(fe_type_t  type, __u32 * frequency, int frontend_fd, int tuner, 
                              __u32* ber, __u32 fe_step_size, EIT_media_config_t *media,
                              dvb_cancelFunctionDef* pFunction)
{
	int res;

	currentFrequency[tuner] = 0;

	if ((res = dvb_setFrequency(type, *frequency, frontend_fd, tuner, 1, media, pFunction)) == 0)
	{
		int i;
		for (i = 0; i < 10; i++)
		{
			struct dvb_frontend_parameters fe;
			fe_status_t s;

			while (1)
			{
				// Get the front-end state including current freq. setting.
				// Some drivers may need to be modified to update this field.
				ioctl_or_abort(tuner, frontend_fd, FE_GET_FRONTEND, &fe);
				ioctl_or_abort(tuner, frontend_fd, FE_READ_STATUS,  &s);

				dprintf("%s[%d]: f_orig=%u   f_read=%u status=%02x\n", __FUNCTION__, tuner,
				        *frequency, (long) fe.frequency, s);
				// in theory a clever FE could adjust the freq. to give a perfect freq. needed !
				if (fe.frequency == *frequency)
				{
					break;
				}
				else
				{
#ifdef DEBUG
					long gap = (fe.frequency > *frequency) ?
					            fe.frequency - *frequency  :
					              *frequency - fe.frequency;
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
	if (permanent)
	{
#ifdef STSDK
		elcdRpcType_t type;
		cJSON *result = NULL;
		st_rpcSync( elcmd_dvbclearservices, NULL, &type, &result );
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

tunerFormat dvb_getTuner(void)
{
	tunerFormat tuner;
	for ( tuner = inputTuner0; tuner < inputTuners; tuner++ )
		if (appControlInfo.tunerInfo[tuner].status > tunerNotPresent)
			return tuner;
#ifdef STSDK
	tuner = st_getDvbTuner();
	if ((signed)tuner < 0)
		tuner = inputTuner0;
	else
		tuner += VMSP_COUNT;
#endif
	return tuner;
}

int dvb_getType(tunerFormat tuner)
{
#ifdef STSDK
	if ((tuner >= inputTuners) || (appControlInfo.tunerInfo[tuner].status == tunerNotPresent))
		return st_getDvbTunerType(-1 /* current tuner */);
#endif
	return (gFE_type);
}

int dvb_getTuner_freqs(tunerFormat tuner, __u32 * low_freq, __u32 * high_freq, __u32 * freq_step)
{
	switch (dvb_getType(tuner)) {
		case FE_OFDM:
			*low_freq  = ( appControlInfo.dvbtInfo.fe.lowFrequency * KHZ);
			*high_freq = (appControlInfo.dvbtInfo.fe.highFrequency * KHZ);
			*freq_step = (appControlInfo.dvbtInfo.fe.frequencyStep * KHZ);
			break;
		case FE_QAM:
			*low_freq  = ( appControlInfo.dvbcInfo.fe.lowFrequency * KHZ);
			*high_freq = (appControlInfo.dvbcInfo.fe.highFrequency * KHZ);
			*freq_step = (appControlInfo.dvbcInfo.fe.frequencyStep * KHZ);
			break;
		case FE_QPSK:
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
		case FE_ATSC:
			eprintf("%s: ATSC FEs not yet supported", __FUNCTION__);
			return -1;
			break;
		default :
			break;
	}

	return 0;
}

int dvb_openFrontend(tunerFormat tuner, int flags)
{
	char frontend_devname[32];
	snprintf(frontend_devname, sizeof(frontend_devname), "/dev/dvb/adapter%d/frontend0", tuner);
	int frontend_fd = open(frontend_devname, flags);
	if (frontend_fd < 0)
	{
		snprintf(frontend_devname, sizeof(frontend_devname), "/dev/dvb%d.frontend0", tuner);
		frontend_fd = open(frontend_devname, flags);
	}
	return frontend_fd;
}

int dvb_getFrontendInfo(tunerFormat tuner, int flags, struct dvb_frontend_info *fe_info)
{
	int frontend_fd = dvb_openFrontend(tuner, flags);
	if (frontend_fd < 0) {
		eprintf("%s: failed to open frontend %d\n", __FUNCTION__, tuner);
		return -1;
	}

	int err;
	ioctl_loop(tuner, err, frontend_fd, FE_GET_INFO, fe_info);

	if (!dvb_isSupported(fe_info->type)) {
		eprintf("%s[%d]: Model=%s,  Type=%u is not supported!\n",
			__FUNCTION__, tuner, fe_info->name, fe_info->type);
		close(frontend_fd);
		return -1;
	}

	eprintf("%s[%d]: DVB Model=%s,  Type=%s FineStepSize=%u\n", __FUNCTION__,
		tuner, fe_info->name, gFE_Type_Names[fe_info->type], fe_info->frequency_stepsize);
	return frontend_fd;
}

int dvb_getSignalInfo(tunerFormat tuner,
                      uint16_t *snr, uint16_t *signal, uint32_t *ber, uint32_t *uncorrected_blocks)
{
	int frontend_fd;
	fe_status_t status = 0;

	mysem_get(dvb_fe_semaphore);

	if ((frontend_fd = dvb_openFrontend(tuner, O_RDONLY | O_NONBLOCK)) > 0)
	{
		ioctl(frontend_fd, FE_READ_STATUS, &status);
		ioctl(frontend_fd, FE_READ_SIGNAL_STRENGTH, signal);
		ioctl(frontend_fd, FE_READ_SNR, snr);
		ioctl(frontend_fd, FE_READ_BER, ber);
		ioctl(frontend_fd, FE_READ_UNCORRECTED_BLOCKS, uncorrected_blocks);
		close(frontend_fd);
		frontend_fd = (status & FE_HAS_LOCK) == FE_HAS_LOCK;
	} else
	{
		eprintf("%s: failed opening frontend %d\n", __FUNCTION__, tuner);
#ifdef ENABLE_PVR
		frontend_fd = 0; // don't return error
#endif
	}
	mysem_release(dvb_fe_semaphore);
	return frontend_fd;
}

int dvb_serviceScan( tunerFormat tuner, dvb_displayFunctionDef* pFunction)
{
	__u32 frequency;
	__u32 low_freq = 0, high_freq = 0, freq_step = 0;
	struct dvb_frontend_info fe_info;

	dvb_getTuner_freqs(tuner, &low_freq, &high_freq, &freq_step);

#ifdef STSDK
	cJSON *params = cJSON_CreateObject();
	if (!params)
	{
		eprintf("%s[%d]: out of memory\n", __FUNCTION__, tuner);
		return -1;
	}
	cJSON *result = NULL;
	elcdRpcType_t type = elcdRpcInvalid;
	int res = -1;

	if (tuner >= VMSP_COUNT)
	{
#if 0
		// FIXME: Avit DVB-C tuner has problems with range scanning

		cJSON_AddItemToObject(params, "tuner", cJSON_CreateNumber( tuner-VMSP_COUNT ) );
		cJSON_AddItemToObject(params, "start", cJSON_CreateNumber(  low_freq/KHZ ) );
		cJSON_AddItemToObject(params, "stop" , cJSON_CreateNumber( high_freq/KHZ ) );

		st_setTuneParams(tuner-VMSP_COUNT, params);
		eprintf("%s: scanning %6u-%6u\n", __FUNCTION__, low_freq/KHZ, high_freq/KHZ);
		res = st_rpcSyncTimeout(elcmd_dvbscan, params, RPC_SCAN_TIMEOUT, &type, &result );
		cJSON_Delete(params);
		cJSON_Delete(result);
		if (res != 0 || type != elcdRpcResult)
		{
			eprintf("%s: scan failed\n", __FUNCTION__ );
			return -1;
		}
#else
		result = NULL;
		params = cJSON_CreateObject();
		cJSON *p_freq = cJSON_CreateNumber(0);

		cJSON_AddItemToObject(params, "tuner", cJSON_CreateNumber( tuner-VMSP_COUNT ) );
		cJSON_AddItemToObject(params, "frequency", p_freq);
		st_setTuneParams(tuner-VMSP_COUNT, params);

		for (frequency = low_freq; frequency <= high_freq; frequency += freq_step)
		{
			p_freq->valueint = frequency/KHZ;
			p_freq->valuedouble = p_freq->valueint;
			dprintf("%s[%d]: Check main freq: %u\n", __FUNCTION__, tuner, frequency);
			if (pFunction != NULL && pFunction(frequency, dvb_getNumberOfServices(), tuner, 0, 0) == -1)
			{
				dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
				break;
			}
			res = st_rpcSync(elcmd_dvbtune, params, &type, &result );
			if (result && result->valuestring != NULL && strcmp (result->valuestring, "ok") == 0)
			{
				cJSON_Delete(result);
				result = NULL;
				dprintf("%s[%d]: Found something on %u, search channels!\n", __FUNCTION__, tuner, frequency);
				res = st_rpcSyncTimeout(elcmd_dvbscan, NULL, RPC_SCAN_TIMEOUT, &type, &result );
				dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
			}
			cJSON_Delete(result);
			result = NULL;
		}
		cJSON_Delete(params);
#endif
		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);

		return 0;
	}

	cJSON *p_freq = cJSON_CreateNumber(0);
	if (!p_freq)
	{
		eprintf("%s[%d]: out of memory\n", __FUNCTION__, tuner);
		goto service_scan_failed;
	}
	cJSON_AddItemToObject(params, "tuner", cJSON_CreateNumber(tuner));
	cJSON_AddItemToObject(params, "frequency", p_freq);
#endif

	char demux_devname[32];
	snprintf(demux_devname, sizeof(demux_devname), "/dev/dvb/adapter%d/demux0", tuner);
	dprintf("%s[%d]: Scan Adapter /dev/dvb/adapter%d/\n", __FUNCTION__, tuner, tuner);

	/* Clear any existing channel list */
	//dvb_clearServiceList();

	if(!appControlInfo.dvbCommonInfo.streamerInput)
	{
		int frontend_fd = dvb_getFrontendInfo(tuner, O_RDWR, &fe_info);
		if (frontend_fd < 0) {
			eprintf("%s[%d]: failed to open frontend\n", __FUNCTION__, tuner);
			goto service_scan_failed;
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
			dprintf("%s[%d]: Check main freq: %u\n", __FUNCTION__, tuner, f);
			int found = (1== dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
			                           &match_ber, fe_info.frequency_stepsize, NULL, NULL));

			if (appControlInfo.dvbCommonInfo.extendedScan)
			{
				int i;
				for (i=1; !found && (i<=MAX_OFFSETS); i++) 
				{
					__u32 offset = i * fe_info.frequency_stepsize;

					f = frequency - offset;
					verbose("%s[%d]: Check lower freq %u\n", __FUNCTION__, tuner, f);
					found = (1== dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
					                           &match_ber, fe_info.frequency_stepsize, NULL, NULL));
					if (found)
						break;
					f = frequency + offset;
					verbose("%s[%d]: Check higher freq %u\n", __FUNCTION__, tuner, f);
					found = (1== dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
					                           &match_ber, fe_info.frequency_stepsize, NULL, NULL));
				}
			}

			if (found)
			{
				eprintf("%s[%d]: Found something on %u, search channels!\n", __FUNCTION__, tuner, f);
				SCAN_MESSAGE("DVB[%d]: Found something on %u, search channels!\n", tuner, f);
				/* Scan for channels within this frequency / transport stream */
#ifdef STSDK
				p_freq->valueint = f/KHZ;
				p_freq->valuedouble = (double)p_freq->valueint;
				res = st_rpcSync(elcmd_dvbtune, params, &type, &result );
				cJSON_Delete(result); // ignore
				result = NULL;
				eprintf("%s[%d]: scanning %6u\n", __FUNCTION__, tuner, f);
				res = st_rpcSyncTimeout(elcmd_dvbscan, params, RPC_SCAN_TIMEOUT, &type, &result );
				cJSON_Delete(result);
				if (res != 0 || type != elcdRpcResult)
					eprintf("%s[%d]: scan failed\n", __FUNCTION__, tuner);
				else
					dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
#else
				dvb_scanForServices (f, demux_devname, NULL);
#endif // STSDK
				SCAN_MESSAGE("DVB[%d]: Found %d channels ...\n", tuner, dvb_getNumberOfServices());
				dprintf("%s[%d]: Found %d channels ...\n", __FUNCTION__, tuner, dvb_getNumberOfServices());
			} else
			{
				eprintf("%s[%d]: ... NOTHING f= %u \n", __FUNCTION__, tuner, f);
			}
			if (pFunction != NULL && pFunction(f, dvb_getNumberOfServices(), tuner, 0, 0) == -1)
			{
				dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
				break;
			}
		}

		close(frontend_fd);
	}
	else // streamer input
	{
		dvb_scanForServices (0, demux_devname, NULL);
		dprintf("%s[%d]: Found %d channels ...\n", __FUNCTION__, tuner, dvb_getNumberOfServices());
	}

	/* Write the channel list to the root file system */
	dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);

	dprintf("%s[%d]: Tuning complete\n", __FUNCTION__, tuner);

#ifdef STSDK
	cJSON_Delete(params);
#endif
	return 0;

service_scan_failed:
#ifdef STSDK
	cJSON_Delete(params);
#endif
	return -1;
}

int dvb_frequencyScan( tunerFormat tuner, __u32 frequency, EIT_media_config_t *media,
                       dvb_displayFunctionDef* pFunction, NIT_table_t *scan_network,
                       int save_service_list, dvb_cancelFunctionDef* pCancelFunction)
{
	char demux_devname[32];
	int frontend_fd = 0;
	struct dvb_frontend_info fe_info;
	long current_frequency_number = 1, max_frequency_number = 0;

#ifdef STSDK
	cJSON *params = cJSON_CreateObject();
	if (!params)
	{
		eprintf("%s[%d]: out of memory\n", __FUNCTION__, tuner);
		return -1;
	}
	cJSON *result = NULL;
	elcdRpcType_t type = elcdRpcInvalid;

	if (tuner >= VMSP_COUNT)
	{
		cJSON_AddItemToObject(params, "tuner", cJSON_CreateNumber( tuner-VMSP_COUNT ) );
		if (st_getDvbTunerType(tuner-VMSP_COUNT) == FE_QPSK)
		{
			cJSON_AddItemToObject(params, "start", cJSON_CreateNumber( frequency ) );
			//cJSON_AddItemToObject(params, "stop" , cJSON_CreateNumber( frequency ) );
		} else
		{
			cJSON_AddItemToObject(params, "start", cJSON_CreateNumber( frequency/KHZ ) );
			cJSON_AddItemToObject(params, "stop" , cJSON_CreateNumber( frequency/KHZ ) );
		}
		st_setTuneParams(tuner-VMSP_COUNT, params);
	} else
	{
		if ((frontend_fd = dvb_openFrontend(tuner, O_RDWR)) < 0)
		{
			cJSON_Delete(params);
			eprintf("%s[%d]: failed to open frontend\n", tuner);
			return -1;
		}

		if (dvb_setFrequency(dvb_getType(tuner), frequency, frontend_fd, tuner, 1, NULL, pCancelFunction) != 1)
		{
			eprintf("%s[%d]: failed to set frequency to %.3f MHz\n", __FUNCTION__, tuner, (float)frequency/MHZ);
			close(frontend_fd);
			return -1;
		}
		cJSON_AddItemToObject(params, "tuner", cJSON_CreateNumber(tuner) );
		cJSON_AddItemToObject(params, "frequency", cJSON_CreateNumber( frequency/KHZ ) );
		st_rpcSync(elcmd_dvbtune, params, &type, &result ); // ignore
		cJSON_Delete(params);
		cJSON_Delete(result);

		// reset rpc variables
		params = NULL;
		result = NULL;
		type = elcdRpcInvalid;
	}

	{
		eprintf("%s[%d]: scanning %6u\n", __FUNCTION__, tuner, frequency / KHZ);
		int res = st_rpcSyncTimeout(elcmd_dvbscan, params, RPC_SCAN_TIMEOUT, &type, &result );
		cJSON_Delete(params);
		cJSON_Delete(result);
		if (frontend_fd != 0) close(frontend_fd);
		if (res != 0 || type != elcdRpcResult)
		{
			eprintf("%s[%d]: failed to scan %6u\n", __FUNCTION__, tuner, frequency / KHZ );
			return -1;
		}
		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
		return 0;
	}
#endif

	dprintf("%s: Scan Adapter %d\n", __FUNCTION__, tuner);
	snprintf(demux_devname, sizeof(demux_devname), "/dev/dvb/adapter%d/demux0", tuner);

	if (!appControlInfo.dvbCommonInfo.streamerInput)
	{
		if ((frontend_fd = dvb_getFrontendInfo(tuner, O_RDWR, &fe_info)) < 0) {
			eprintf("%s[%d]: failed to open frontend %d\n", __FUNCTION__, tuner);
			return -1;
		}

		// Use the FE's own start/stop freq. for searching (if not explicitly app. defined).
		if( frequency < fe_info.frequency_min || frequency > fe_info.frequency_max )
		{
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

		int found = dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
			&match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
		if (found == -1)
		{
			dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
			goto frequency_scan_failed;
		}

		/* Just keep tuner on frequency */
		if (save_service_list == -1)
		{
			for(;;)
			{
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
		}
		/* Or just return found value */
		else if (save_service_list == -2)
		{
			close(frontend_fd);
			return found;
		}

		if (appControlInfo.dvbCommonInfo.extendedScan)
		{
			for (i=1; !found && (i<=MAX_OFFSETS); i++)
			{
				long offset = i * fe_info.frequency_stepsize;

				f = frequency - offset;
				dprintf("%s[%d]: Check lower freq: %u\n", __FUNCTION__, tuner, f);
				found = dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
				                 &match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
				if (found == -1) {
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
					goto frequency_scan_failed;
				}
				if (found)
					break;
				f = frequency + offset;
				dprintf("%s[%d]: Check higher freq: %u\n", __FUNCTION__, tuner, f);
				found = dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
				                 &match_ber, fe_info.frequency_stepsize, media, pCancelFunction);
				if (found == -1) {
					dprintf("%s[%d]: aborted by user\n", __FUNCTION__, tuner);
					goto frequency_scan_failed;
				}
			}
		}

		if (found)
		{
			dprintf("%s[%d]: Found something on %u, search channels!\n", __FUNCTION__, tuner, f);
			SCAN_MESSAGE("DVB[%d]: Found something on %u, search channels!\n", tuner, f);
			/* Scan for channels within this frequency / transport stream */
			dvb_scanForServices (f, demux_devname, scan_network);
			SCAN_MESSAGE("DVB[%d]: Found %d channels ...\n", tuner, dvb_getNumberOfServices());
			dprintf("%s[%d]: Found %d channels ...\n", __FUNCTION__, tuner, dvb_getNumberOfServices());
		}

		close(frontend_fd);
	}
	else
	{
		dvb_scanForServices (frequency, demux_devname, scan_network);
		dprintf("%s[%d]: Found %d channels on %u\n", __FUNCTION__, tuner, dvb_getNumberOfServices(), frequency);
	}

	if (scan_network != NULL)
	{
		list_element_t *tstream_element;
		NIT_transport_stream_t *tsrtream;

		/* Count frequencies in NIT */
		tstream_element = scan_network->transport_streams;
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
		tstream_element = scan_network->transport_streams;
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
				                  &tsrtream->media, pFunction, NULL, 0, pCancelFunction);
				current_frequency_number++;
			}
			tstream_element = tstream_element->next;
		}
	}

	if (save_service_list)
		dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);

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
static void dvb_instanceReset(struct dvb_instance * dvb, int vmsp)
{
	memset( dvb, 0, sizeof(struct dvb_instance));

	dvb->vmsp = vmsp;
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
#ifdef ENABLE_PVR
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
			eprintf("%s[%d]: Multi %d %d %d %d\n", __FUNCTION__, pParam->vmsp,
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
#ifdef ENABLE_PVR
		case DvbMode_Play:
			eprintf("%s[%d]: Play Video pid %d and Audio pid %d Text pid %d Pcr %d\n", __FUNCTION__, pParam->vmsp,
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
					eprintf("%s[%d]: Watch failed to get stream pids for %d service\n", __FUNCTION__, pParam->vmsp,
							pParam->param.liveParam.channelIndex);
					return -1;
				}
			} else {
				dvb->filterv.pid = pParam->param.playParam.videoPid;
				dvb->filtera.pid = pParam->param.playParam.audioPid;
				dvb->filtert.pid = pParam->param.playParam.textPid;
				dvb->filterp.pid = pParam->param.playParam.pcrPid;
			}
			eprintf("%s[%d]: Watch channel %d Video pid %hu Audio pid %hu Text pid %hu Pcr %hu\n", __FUNCTION__, pParam->vmsp,
				pParam->param.liveParam.channelIndex,
				dvb->filterv.pid, dvb->filtera.pid, dvb->filtert.pid, dvb->filterp.pid);
			return 0;
		default:;
	}
	return -1;
}

static int dvb_demuxerStart(struct dvb_instance * dvb, EIT_media_config_t *media)
{
	dprintf("%s[%d]: setting filterv=%hu filtera=%hu filtert=%hu filterp=%hu\n", __FUNCTION__, dvb->vmsp,
	        dvb->filterv.pid, dvb->filtera.pid, dvb->filtert.pid, dvb->filterp.pid );

	ioctl_or_abort(dvb->vmsp, dvb->fdv, DMX_SET_PES_FILTER, &dvb->filterv);

#ifdef ENABLE_MULTI_VIEW
	if (dvb->mode == DvbMode_Multi)
	{
		dprintf("%s[%d]: filterv1=%hu filterv2=%hu filterv3=%hu\n", __FUNCTION__, dvb->vmsp,
		        dvb->filterv1.pid, dvb->filterv2.pid, dvb->filterv3.pid);

		ioctl_or_abort(dvb->vmsp, dvb->fdv1, DMX_SET_PES_FILTER, &dvb->filterv1);
		ioctl_or_abort(dvb->vmsp, dvb->fdv2, DMX_SET_PES_FILTER, &dvb->filterv2);
		ioctl_or_abort(dvb->vmsp, dvb->fdv3, DMX_SET_PES_FILTER, &dvb->filterv3);
	} else
#endif // ENABLE_MULTI_VIEW
	{
		ioctl_or_abort(dvb->vmsp, dvb->fda,  DMX_SET_PES_FILTER, &dvb->filtera);
		ioctl_or_abort(dvb->vmsp, dvb->fdp,  DMX_SET_PES_FILTER, &dvb->filterp);
		ioctl_or_abort(dvb->vmsp, dvb->fdt,  DMX_SET_PES_FILTER, &dvb->filtert);
	}

	dprintf("%s[%d]: starting PID filters\n", __FUNCTION__, dvb->vmsp);

	if (dvb->filterv.pid != 0)
		ioctl_or_warn (dvb->vmsp, dvb->fdv, DMX_START);
#ifdef ENABLE_MULTI_VIEW
	if (dvb->mode == DvbMode_Multi)
	{
		if (dvb->filterv1.pid != 0)
			ioctl_or_abort(dvb->vmsp, dvb->fdv1, DMX_START);
		if (dvb->filterv2.pid != 0)
			ioctl_or_abort(dvb->vmsp, dvb->fdv2, DMX_START);
		if (dvb->filterv3.pid != 0)
			ioctl_or_abort(dvb->vmsp, dvb->fdv3, DMX_START);
	} else
#endif // ENABLE_MULTI_VIEW
	{
		if (dvb->filtera.pid != 0)
			ioctl_or_warn (dvb->vmsp, dvb->fda,  DMX_START);
		if (dvb->filtert.pid != 0)
			ioctl_or_warn (dvb->vmsp, dvb->fdt,  DMX_START);
#ifdef STBTI
		if (dvb->filterp.pid != dvb->filtera.pid &&
		    dvb->filterp.pid != dvb->filterv.pid &&
		    dvb->filterp.pid != dvb->filtert.pid)
		{
			dprintf("%s[%d]: INIT PCR %hu\n", tuner, dvb->vmsp, dvb->filterp.pid);
			ioctl_or_abort(dvb->vmsp, dvb->fdp,  DMX_START);
		}
#else
		if (dvb->filterp.pid != 0)
			ioctl_or_abort(dvb->vmsp, dvb->fdp,  DMX_START);
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
	int vmsp = dvb->vmsp;
	dprintf("%s[%d]: adapter%d/frontend0\n", __FUNCTION__, vmsp, vmsp);

	dvb->fe_tracker_Thread = 0;
	dvb->fe_type = gFE_type;

	if (!appControlInfo.dvbCommonInfo.streamerInput && (dvb->mode != DvbMode_Play))
	{
		struct dvb_frontend_info fe_info;
		dvb->fdf = dvb_getFrontendInfo(vmsp, O_RDWR, &fe_info);
		if (dvb->fdf < 0) {
			PERROR("%s[%d]: failed to open frontend", __FUNCTION__, vmsp);
#ifndef ENABLE_PVR
			return -1;
#endif
		} else
			dvb->fe_type = fe_info.type;
	}

#ifdef LINUX_DVB_API_DEMUX
	char path[32];
	dprintf("%s[%d]: adapter%d/demux0\n", __FUNCTION__, vmsp, vmsp);

	snprintf(path, sizeof(path), "/dev/dvb/adapter%i/demux0", vmsp);
	open_or_abort(vmsp, "video", path, O_RDWR, dvb->fdv);

#ifdef ENABLE_MULTI_VIEW
	if ( dvb->mode == DvbMode_Multi )
	{
		open_or_abort(vmsp, "video1", path, O_RDWR, dvb->fdv1);
		open_or_abort(vmsp, "video2", path, O_RDWR, dvb->fdv2);
		open_or_abort(vmsp, "video3", path, O_RDWR, dvb->fdv3);
	} else
#endif
	{
		open_or_abort(vmsp, "audio",  path, O_RDWR, dvb->fda);
		open_or_abort(vmsp, "pcr",    path, O_RDWR, dvb->fdp);
		open_or_abort(vmsp, "text" ,  path, O_RDWR, dvb->fdt);
	}

	if (dvb->mode == DvbMode_Record
	 || dvb->filtert.pid
#ifdef ENABLE_MULTI_VIEW
	 || dvb->mode == DvbMode_Multi
#endif
	   )
	{
		dprintf("%s[%d]: adapter%i/dvr0\n", __FUNCTION__, vmsp, vmsp);

		snprintf(path, sizeof(path), "/dev/dvb/adapter%i/dvr0", vmsp);
		open_or_abort(vmsp, "read",   path, O_RDONLY, dvb->fdin);
#ifdef ENABLE_PVR
#ifdef ENABLE_MULTI_VIEW
		if( dvb->mode == DvbMode_Record )
#endif
		{
			char out_path[PATH_MAX];
			snprintf(dvb->directory, sizeof(dvb->directory), "%s", pFilename);
			snprintf(out_path, sizeof(out_path), "%s/part%02d.spts", dvb->directory, dvb->fileIndex);
			open_or_abort(vmsp, "write", out_path, O_WRONLY | O_CREAT | O_TRUNC, dvb->fdout);
		}
#endif
	}
#ifdef ENABLE_PVR
	else if (dvb->mode == DvbMode_Play)
	{
		char in_path[PATH_MAX];
		dprintf("%s[%d]: %s/part%02d.spts\n", __FUNCTION__, vmsp, dvb->directory, dvb->fileIndex);

		snprintf(dvb->directory, sizeof(dvb->directory), "%s", pFilename);
		dvb->pvr.rate = DEFAULT_PVR_RATE;
		snprintf(in_path, sizeof(in_path), "%s/part%02d.spts", dvb->directory, dvb->fileIndex);
		open_or_abort(vmsp, "read", in_path, O_RDONLY, dvb->fdin);

		/* Set the current read position */
		lseek(dvb->fdin, (dvb->pvr.position/TS_PACKET_SIZE)*TS_PACKET_SIZE, 0);
		dvb->pvr.last_position = dvb->pvr.position;
		dvb->pvr.last_index = dvb->fileIndex;

		dprintf("%s[%d]: adapter%i/dvr0\n", __FUNCTION__, vmsp, vmsp);

		/* Since the input DVRs correponding to VMSP 0/1 are on adapter 2/3 */
		snprintf(path, sizeof(path), "/dev/dvb/adapter%i/dvr0", vmsp);
		open_or_abort(vmsp, "write", path, O_WRONLY, dvb->fdout);
	}
#endif // ENABLE_PVR
#endif // LINUX_DVB_API_DEMUX
	dprintf("%s[%d]: ok\n", __FUNCTION__, vmsp);
	return 0;
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
	//dprintf("%s[%d]: in\n", __FUNCTION__, dvb->vmsp);
	if (0 != dvb->fe_tracker_Thread) {
		pthread_cancel (dvb->fe_tracker_Thread);
		dvb->fe_tracker_Thread = 0;
	}
#ifdef ENABLE_TELETEXT
	if (dvb->teletext_thread) {
		pthread_cancel (dvb->teletext_thread);
		pthread_join (dvb->teletext_thread, NULL);
		dvb->teletext_thread = 0;
		appControlInfo.teletextInfo.status = teletextStatus_disabled;
		appControlInfo.teletextInfo.exists = 0;
		interfaceInfo.teletext.show = 0;
	}
#endif
	CLOSE_FD(dvb->vmsp, "frontend",       dvb->fdf);

#ifdef LINUX_DVB_API_DEMUX
	CLOSE_FD(dvb->vmsp, "video filter",   dvb->fdv);
#ifdef ENABLE_MULTI_VIEW
	CLOSE_FD(dvb->vmsp, "video1 filter",  dvb->fdv1);
	CLOSE_FD(dvb->vmsp, "video2 filter",  dvb->fdv2);
	CLOSE_FD(dvb->vmsp, "video3 filter",  dvb->fdv3);
#endif
	CLOSE_FD(dvb->vmsp, "audio filter",   dvb->fda);
	CLOSE_FD(dvb->vmsp, "pcr filter",     dvb->fdp);
	CLOSE_FD(dvb->vmsp, "teletext filter",dvb->fdt);
	CLOSE_FD(dvb->vmsp, "input",          dvb->fdin);
#ifdef ENABLE_PVR
	CLOSE_FD(dvb->vmsp, "output",         dvb->fdout);
#endif
#endif // LINUX_DVB_API_DEMUX
	dprintf("%s[%d]: ok\n", __FUNCTION__, dvb->vmsp);
}
#undef CLOSE_FD

/* Thread to track frontend signal status */
static void * dvb_fe_tracker_Thread(void *pArg)
{
	int err;
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;

	for (;;) {
		sleep(TRACK_INTERVAL); // two sec.
		pthread_testcancel(); // just in case
		if (!appControlInfo.dvbCommonInfo.streamerInput)
		{
			ioctl_loop(dvb->vmsp, err, dvb->fdf, FE_READ_STATUS,
				&appControlInfo.tunerInfo[dvb->vmsp].fe_status);
			ioctl_loop(dvb->vmsp, err, dvb->fdf, FE_READ_BER,
				&appControlInfo.tunerInfo[dvb->vmsp].ber);
			ioctl_loop(dvb->vmsp, err, dvb->fdf, FE_READ_SIGNAL_STRENGTH,
				&appControlInfo.tunerInfo[dvb->vmsp].signal_strength);
			ioctl_loop(dvb->vmsp, err, dvb->fdf, FE_READ_SNR,
				&appControlInfo.tunerInfo[dvb->vmsp].snr);
			ioctl_loop(dvb->vmsp, err, dvb->fdf, FE_READ_UNCORRECTED_BLOCKS,
				&appControlInfo.tunerInfo[dvb->vmsp].uncorrected_blocks);
		}
	}
	return NULL;
}

#define PVR_BUFFER_SIZE (TS_PACKET_SIZE * 100)
#ifdef ENABLE_PVR
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
					__FUNCTION__, dvb->vmsp, timeGap, difference, dvb->pvr.rate);
			}
		}
	}
	return NULL;
}
#endif // ENABLE_PVR

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
		eprintf("%s[%d]: Failed to open %s fo writing: %s\n", __FUNCTION__, dvb->vmsp, path, strerror(errno));
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
					PERROR("%s[%d]: Failed to read DVR", __FUNCTION__, dvb->vmsp);
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
	dprintf("%s[%d]: Exiting dvb_multiThread\n", __FUNCTION__, dvb->vmsp);
	return NULL;
}
#endif // ENABLE_MULTI_VIEW

#ifdef ENABLE_TELETEXT
static void *dvb_teletextThread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	struct pollfd pfd[1];
	unsigned char buf[TELETEXT_PACKET_BUFFER_SIZE];

	pfd[0].fd = dvb->fdin;
	pfd[0].events = POLLIN;

	do
	{
		pthread_testcancel();
		if (poll(pfd,1,1))
		{
			if (pfd[0].revents & POLLIN)
			{
				ssize_t length = read(dvb->fdin, buf, sizeof(buf));
				if (length < 0)
				{
					PERROR("%s[%d]: Failed to read dvr", __FUNCTION__, dvb->vmsp);
				}
				if (length > 0)
				{
					teletext_readPESPacket(buf, length);
				}
			}
		}
	} while (1);

	return NULL;
}
#endif

#ifdef ENABLE_PVR
static void dvb_pvrThreadTerm(void* pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	dprintf("%s[%d]: Exiting DVB Thread\n", __FUNCTION__, dvb->vmsp);
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
	dprintf("%s[%d]: running\n", __FUNCTION__, dvb->vmsp);

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
			PERROR("%s[%d]: Read error", __FUNCTION__, dvb->vmsp);
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
				PERROR("%s[%d]: Write error", __FUNCTION__, dvb->vmsp);
		}
		pthread_testcancel();
	} while ((length > 0) && (write_length > 0));

	dprintf("%s[%d]: out\n", __FUNCTION__, dvb->vmsp);
	pthread_cleanup_pop(1);
	return NULL;
}
#endif // ENABLE_PVR

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

int dvb_getServiceIndex( EIT_service_t* service)
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
	switch( stream->stream_type )
	{
		case 0x01:
		case 0x02:
			return payloadTypeMpeg2;
		case 0x1b:
			return payloadTypeH264;
		case 0x03:
		case 0x04:
			return payloadTypeMpegAudio;
		case 0x12:
			return payloadTypeAC3;
		case 0x06:
			return payloadTypeText;
//	case 0x06:
// 			if (find_descriptor(0x56, buf + 5, ES_info_len, NULL, NULL)) {
// 				info("  TELETEXT  : PID 0x%04x\n", elementary_pid);
// 				s->teletext_pid = elementary_pid;
// 				break;
// 			}
// 			else if (find_descriptor(0x59, buf + 5, ES_info_len, NULL, NULL)) {
// 				/* Note: The subtitling descriptor can also signal
// 				 * teletext subtitling, but then the teletext descriptor
// 				 * will also be present; so we can be quite confident
// 				 * that we catch DVB subtitling streams only here, w/o
// 				 * parsing the descriptor. */
// 				info("  SUBTITLING: PID 0x%04x\n", elementary_pid);
// 				s->subtitling_pid = elementary_pid;
// 				break;
// 			}
// 			else if (find_descriptor(0x6a, buf + 5, ES_info_len, NULL, NULL)) {
// 				info("  AC3       : PID 0x%04x\n", elementary_pid);
// 				if (s->audio_num < AUDIO_CHAN_MAX) {
// 					s->audio_tracks[s->audio_num].pid = elementary_pid;
// 					s->audio_tracks[s->audio_num].type = AC3;
// 					s->audio_tracks[s->audio_num].lang[0] = 0;
// 					s->audio_num++;
// 				}
// 				else
// 				{
// 					info("more than %i audio channels, truncating\n",
// 						 AUDIO_CHAN_MAX);
// 				}
// 				break;
// 	}
		default:
			return payloadTypeUnknown;
	}
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

int dvb_getAudioTypeForService(EIT_service_t *service, int audio)
{
	PID_info_t* stream = dvb_getAudioStream(service, audio);
	if( stream == NULL )
	{
		return -1;
	}
	return dvb_getStreamType(stream);
}

short unsigned int dvb_getAudioPidForService(EIT_service_t *service, int audio)
{
	PID_info_t* stream = dvb_getAudioStream(service, audio);
	if( stream == NULL )
	{
		return -1;
	}
	return stream->elementary_PID;
}

int dvb_getAudioCountForService(EIT_service_t *service)
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
		 dvb_hasMediaTypeNB(service, mediaTypeAudio) ||
		 dvb_hasMediaTypeNB(service, (media_type)payloadTypeText)) )
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
		mysem_release(dvb_semaphore);
		type = dvb_getStreamType( stream );
		mysem_get(dvb_semaphore);
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
	int type;
	if( service == NULL )
	{
		dprintf("%s: service is NULL\n", __FUNCTION__);
		buf[0] = 0;
		return -1;
	}
	mysem_get(dvb_semaphore);
	sprintf(buf,"%s\n%s: %s\n",
		service->service_descriptor.service_name,
		_T("DVB_PROVIDER"), service->service_descriptor.service_provider_name);
	buf += strlen(buf);

	switch(service->media.type)
	{
		case serviceMediaDVBT:
			sprintf(buf,"DVB-T\n %s: %lu\n",
			_T("DVB_FREQUENCY"), service->media.dvb_t.centre_frequency);
			buf += strlen(buf);
			break;
		case serviceMediaDVBC:
			sprintf(buf,"DVB-C\n %s: %lu\n",
			_T("DVB_FREQUENCY"), service->media.dvb_c.frequency);
			buf += strlen(buf);
			break;
		default: ;
	}

	sprintf(buf,"Service ID: %hu\nPCR: %hu\n%s\n",
		service->common.service_id,
		service->program_map.map.PCR_PID, _T("DVB_STREAMS"));
	buf += strlen(buf);
	stream_element = service->program_map.map.streams;
	while( stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		mysem_release(dvb_semaphore);
		type = dvb_getStreamType( stream );
		mysem_get(dvb_semaphore);
		switch( type )
		{
			case payloadTypeMpegAudio:
			case payloadTypeMpeg2:
			case payloadTypeAAC:
			case payloadTypeAC3:
				switch(type)
				{
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
				buf+=strlen(buf);
				if( stream->ISO_639_language_code[0] )
				{
					eprintf("%s: '%s'\n", __FUNCTION__, stream->ISO_639_language_code);
					sprintf( buf, " (%3s)", stream->ISO_639_language_code );
					buf+=strlen(buf);
				}
				*buf++ = '\n';
				break;
			case payloadTypeMpeg4:
				sprintf( buf, "  PID: %hu MPEG4 %s\n", stream->elementary_PID, _T("VIDEO") );
				buf+=strlen(buf);
				break;
			case payloadTypeH264:
				sprintf( buf, "  PID: %hu H264  %s\n", stream->elementary_PID, _T("VIDEO") );
				buf+=strlen(buf);
				break;
			case payloadTypeText:
				sprintf( buf, "  PID: %hu %s\n", stream->elementary_PID , _T("TELETEXT") );
				buf+=strlen(buf);
				break;
			default: ;
		}
		stream_element = stream_element->next;
	}
	mysem_release(dvb_semaphore);
	buf[-1] = 0;
	return 0;
}

char* dvb_getEventName( EIT_service_t* service, short int event_id )
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

int dvb_changeAudioPid(int vmsp, short unsigned int aPID)
{
#ifdef LINUX_DVB_API_DEMUX
	struct dmx_pes_filter_params pesfilter;
	struct dvb_instance *dvb;
	dvb = &dvbInstances[vmsp];

	//printf("Change aduio PID for vmsp %d to %d\n", vmsp, aPID);

	if (dvb->fda >= 0)
	{
		// Stop demuxing old audio pid
		ioctl_or_abort(vmsp, dvb->fda, DMX_STOP);
		// reconfigure audio decoder
		// ...
		// reconfigure demux
		pesfilter.input = DMX_IN_DVR;
		pesfilter.output = DMX_OUT_DECODER;
		pesfilter.pes_type = DMX_PES_AUDIO;
		pesfilter.flags = 0;
		pesfilter.pid = aPID;

		ioctl_or_abort(vmsp, dvb->fda, DMX_SET_PES_FILTER, &pesfilter);
		// start demuxing new audio pid
		ioctl_or_abort(vmsp, dvb->fda, DMX_START);
	}
#endif // LINUX_DVB_API_DEMUX
	return 0;
}

int dvb_startDVB(DvbParam_t *pParam)
{
	if ((pParam->vmsp < 0) || (pParam->vmsp >= VMSP_COUNT))
	{
		eprintf("%s: Invalid vmsp - use range 0-%d\n", __FUNCTION__, VMSP_COUNT-1);
		return -1;
	}

	dvb_filtersFlush();
	dvb_filtersUnlock();

	struct dvb_instance *dvb = &dvbInstances[pParam->vmsp];
	dvb_instanceReset(dvb, pParam->vmsp);

	dprintf("%s[%d]: set mode %s\n", __FUNCTION__, pParam->vmsp, pParam->mode == DvbMode_Watch ? "Watch" :
		(pParam->mode == DvbMode_Multi ? "Multi" : (pParam->mode == DvbMode_Record ? "Record" : "Play")));
	dvb->mode = pParam->mode;

	dvb->setFrequency = pParam->frequency;
	if (dvb->setFrequency == 0 && pParam->mode == DvbMode_Watch &&
		dvb_getServiceFrequency( dvb_getService( pParam->param.liveParam.channelIndex ), &dvb->setFrequency) != 0)
	{
		eprintf("%s[%d]: Watch failed to get frequency for %d service\n", __FUNCTION__, pParam->vmsp, pParam->param.liveParam.channelIndex);
		return -1;
	}
	dprintf("%s[%d]: set frequency %u\n", __FUNCTION__, dvb->setFrequency);

#ifdef LINUX_DVB_API_DEMUX
	dvb_demuxerInit(dvb, pParam->mode);
	if (dvb_demuxerSetup(dvb, pParam) != 0) {
		eprintf("%s[%d]: unknown mode %d!\n", __FUNCTION__, dvb->vmsp, pParam->mode);
		return -1;
	}
#endif

	if (dvb_instanceOpen(dvb, pParam->directory) != 0)
	{
		eprintf("%s[%d]: Failed to open dvb instance\n", __FUNCTION__, pParam->vmsp);
		return -1;
	}

	if (dvb->mode != DvbMode_Play &&
#ifdef ENABLE_PVR
	    (signed)dvb->setFrequency > 0 &&
#endif
	    dvb_setFrequency(dvb->fe_type, dvb->setFrequency, dvb->fdf, dvb->vmsp, 1, pParam->media, NULL) < 0)
	{
		eprintf("%s[%d]: Failed to set frequency %u\n", __FUNCTION__, dvb->vmsp, dvb->setFrequency);
		return -1;
	}

#ifdef LINUX_DVB_API_DEMUX
	dprintf("%s[%d]: start demuxer\n", __FUNCTION__, pParam->vmsp);
	if (dvb_demuxerStart(dvb, pParam->media) != 0)
	{
		eprintf("%s[%d]: Failed to change service!\n", __FUNCTION__, pParam->vmsp);
		return 0;
	}
#endif // LINUX_DVB_API_DEMUX

	int st;
	pthread_attr_t attr;
	struct sched_param param = { 90 };

	pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam (&attr, &param);

#ifdef ENABLE_PVR
	if (dvb->mode == DvbMode_Play || dvb->mode == DvbMode_Record)
	{
		dprintf("%s[%d]: DvbMode_%s\n", __FUNCTION__, dvb->vmsp, dvb->mode == DvbMode_Play ? "Play" : "Record");

		st = pthread_create (&dvb->pvr.thread, &attr, dvb_pvrThread, dvb);
		if (st != 0)
		{
			eprintf("%s[%d]: error %d starting dvb_pvrThread\n", __FUNCTION__, dvb->vmsp, st);
			goto failure;
		}
	} else if (dvb->fdf > 0)
#endif // ENABLE_PVR
	{
		dprintf("%s[%d]: DvbMode_Watch\n", __FUNCTION__, dvb->vmsp);
		if (!dvb->fe_tracker_Thread)
		{
			st = pthread_create(&dvb->fe_tracker_Thread, &attr,  dvb_fe_tracker_Thread,  dvb);
			if (st != 0)
			{
				eprintf("%s[%d]: pthread_create (fe track) err=%d\n", __FUNCTION__, dvb->vmsp, st);
				goto failure;
			}
			pthread_detach(dvb->fe_tracker_Thread);
		}
#ifdef ENABLE_TELETEXT
#ifdef ENABLE_MULTI_VIEW
        if( dvb->mode != DvbMode_Multi )
#endif
		{
			if (dvb->filtert.pid>0)
			{
				teletext_init();
				appControlInfo.teletextInfo.exists = 1;
				st = pthread_create(&dvb->teletext_thread, &attr,  dvb_teletextThread,  dvb);
				if (st != 0)
				{
					eprintf("%s[%d]: pthread_create (teletext) err=%d\n", __FUNCTION__, dvb->vmsp, st);
					goto failure;
				}
			}
		}
#endif /* ENABLE_TELETEXT */
#ifdef ENABLE_MULTI_VIEW
		if( dvb->mode == DvbMode_Multi )
		{
			st = pthread_create(&dvb->multi_thread, &attr,  dvb_multiThread,  dvb);
			if (st != 0)
			{
				dvb->multi_thread = 0;
				eprintf("%s[%d]: pthread_create (multiview) err=%d\n", __FUNCTION__, dvb->vmsp, st);
				goto failure;
			}
		}
#endif
	}
	pthread_attr_destroy(&attr);
	dprintf("%s[%d]: ok\n", __FUNCTION__, pParam->vmsp);
	return 0;

failure:
	pthread_attr_destroy(&attr);
	return -1;
}

void dvb_stopDVB(int vmsp, int reset)
{
	if (vmsp >= 0 && vmsp < VMSP_COUNT)
	{
		dprintf("%s[%d]: reset %d\n", __FUNCTION__, vmsp, reset);

		struct dvb_instance *dvb = &dvbInstances[vmsp];

#ifdef ENABLE_PVR
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
		dvb_instanceReset(dvb, vmsp);
		if (reset)
		{
			/* Reset the stored frequency as the driver    */
			/* puts the tuner into standby so it needs to  */
			/* be woken up when we next do a set frequency */
			currentFrequency[vmsp] = 0;
		}
		dprintf("%s[%d]: out\n", __FUNCTION__, vmsp);
	}
}

void dvb_init(void)
{
	int i;
	struct dvb_frontend_info fe_info;
	int fdf;

	dprintf("%s: in\n", __FUNCTION__);

	/* Got through the possible tuners */
	for (i=0; i<inputTuners; i++)
	{
		appControlInfo.tunerInfo[i].status = tunerNotPresent;

		if ((fdf = dvb_getFrontendInfo(i, O_RDONLY, &fe_info)) < 0)
			continue;
		close(fdf);

		gFE_type = fe_info.type; // Assume all FEs are the same type !
		appControlInfo.tunerInfo[i].status = tunerInactive;
		eprintf("%s[%d]: %s (%s)\n", __FUNCTION__, i, fe_info.name, gFE_Type_Names[fe_info.type]);
	}

	mysem_create(&dvb_semaphore);
	mysem_create(&dvb_filter_semaphore);
	mysem_create(&dvb_fe_semaphore);

	for (i=0; i<VMSP_COUNT; i++)
	{
		dvb_instanceReset(&dvbInstances[i], i);
		currentFrequency[i] = 0;
	}

	if (helperFileExists(appControlInfo.dvbCommonInfo.channelConfigFile))
	{
		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
		dprintf("%s: loaded %d services\n", __FUNCTION__, dvb_getNumberOfServices());
	}

	dprintf("%s: out\n", __FUNCTION__);
}

void dvb_terminate(void)
{
	int i;
	for(i=0; i<VMSP_COUNT; i++)
		dvb_stopDVB(i, 1);

	free_services(&dvb_services);

	mysem_destroy(dvb_semaphore);
	mysem_destroy(dvb_filter_semaphore);
	mysem_destroy(dvb_fe_semaphore);
}

#ifdef ENABLE_PVR
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
#endif // ENABLE_PVR

#endif /* ENABLE_DVB */
