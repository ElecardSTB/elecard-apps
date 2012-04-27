
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

//#undef dprintf
//#define dprintf printf

#define fatal eprintf
#define errorn eprintf
#define verbose dprintf
#define verbosedebug dprintf
#define debug dprintf
#define error eprintf
#define info dprintf
#define warning eprintf

#define ERROR(x...)                                                     \
        do {                                                            \
                fprintf(stderr, "ERROR: ");                             \
                fprintf(stderr, x);                                     \
                fprintf (stderr, "\n");                                 \
        } while (0)

#define PERROR(x...)                                                    \
        do {                                                            \
                fprintf(stderr, "ERROR: ");                             \
                fprintf(stderr, x);                                     \
                fprintf (stderr, " (%s)\n", strerror(errno));       \
        } while (0)

#define AUDIO_CHAN_MAX (8)

#define MAX_OFFSETS   (1)
#define FE_TYPE_COUNT (4)

#define MAX_RUNNING   (32)

#define CHANNEL_FILE_FORMAT "Frequency %ld Video %04X H264 %04X PCR %04X ServiceID %04X PMT %04X SCRAMBLED %d LCN %d Audio "
#define CHANNEL_NAME_LENGTH (32)

#define FILE_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)


// For front end signal tracking
// sec.s  between FE checks
#define TRACK_INTERVAL (2)

/* SD bit rate ~ 1.5GB / hour -> ~ 450KB / second */
#define DEFAULT_PVR_RATE (450*1024)

#define MEDIA_ID         (unsigned long)frequency

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

// defines one chain of devices which together make up a DVB receiver/player
struct dvb_instance {
	// file descriptors for frontend, demux-video, demux-audio
	int fdf;
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
	int fdout;

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

	__u32 setFrequency;

	char directory[PATH_MAX];
	int fileIndex;
	int pvr_position;
	int pvr_last_position;
	int pvr_last_index;
	int pvr_rate;
	int pvr_exit;
	int *pEndOfFile;
	pthread_t pvr_thread;
#ifdef ENABLE_MULTI_VIEW
	int multi_exit;
	pthread_t multi_thread;
#endif
#ifdef ENABLE_TELETEXT
	pthread_t teletext_thread;
#endif
	int vmsp;

	DvbMode_t mode;
	fe_type_t fe_type;
	pthread_t thread;
	pthread_t fe_tracker_Thread;
	volatile int running;
	volatile int exit;
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

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static LIST_HEAD(running_filters);
static LIST_HEAD(waiting_filters);
static int n_running;
static struct pollfd poll_fds[MAX_RUNNING];
static struct section_buf* poll_section_bufs[MAX_RUNNING];
static struct dvb_instance dvbInstances[VMSP_COUNT];
static long currentFrequency[VMSP_COUNT];

static int lock_filters = 1;

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
			fatal("too many poll_fds\n");
		s = list_entry (p, struct section_buf, list);
		if (s->fd == -1)
			fatal("s->fd == -1 on running_filters\n");
		poll_fds[i].fd = s->fd;
		poll_fds[i].events = POLLIN;
		poll_fds[i].revents = 0;
		poll_section_bufs[i] = s;
		i++;
	}
	if (i != n_running)
		fatal("n_running is hosed\n");
}

static int dvb_filterStart (struct section_buf* s)
{
	struct dmx_sct_filter_params f;

	if (n_running >= MAX_RUNNING)
		goto err0;
	if ((s->fd = open (s->dmx_devname, O_RDWR | O_NONBLOCK)) < 0)
		goto err0;

	dprintf("DVB: start filter 0x%02x\n", s->pid);

	memset(&f, 0, sizeof(f));

	f.pid = (__u16) s->pid;

	if (s->table_id < 0x100 && s->table_id > 0) {
		f.filter.filter[0] = (unsigned char) s->table_id;
		f.filter.mask[0]   = 0xff;
	}

	f.timeout = 0;
	f.flags = DMX_IMMEDIATE_START | DMX_CHECK_CRC;

	if (ioctl(s->fd, DMX_SET_FILTER, &f) == -1) {
		errorn ("ioctl DMX_SET_FILTER failed");
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
	dprintf("DVB: stop filter %d\n", s->pid);
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
		dprintf("DVB: filters are locked\n");
		if (s->is_dynamic)
		{
			dprintf("DVB: free dynamic filter\n");
			dfree(s);
		}
	}
}


static void dvb_filterRemove (struct section_buf *s)
{
	dvb_filterStop (s);
	if (s->is_dynamic)
	{
		dprintf("DVB: free dynamic filter\n");
		dfree(s);
	}

	dprintf("DVB: start waiting filters\n");

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

	lock_filters = 1;

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
				fatal("poll_section_bufs[%d] is NULL\n", i);
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

	dprintf("DVB: parsing, media type %s, freq %lu\n", gFE_Type_Names[gFE_type], media.frequency);

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
			debug("DVB: section version_number or table_id_ext changed "
				"%d -> %d / %04x -> %04x\n",
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

		debug("DVB: pid 0x%02x tid 0x%02x table_id_ext 0x%04x, "
		    "%i/%i (version %i)\n",
		    s->pid, table_id, table_id_ext, section_number,
		    last_section_number, section_version_number);

		switch (table_id) {
		case 0x00:
			verbose("DVB: PAT\n");
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
						dprintf("DVB: new pmt filter ts %d, pid %d, service %d, media %d\n",
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
			verbose("DVB: PMT 0x%04x for service 0x%04x\n", s->pid, table_id_ext);
			dprintf("DVB: got pmt filter ts %d, pid %d\n", s->transport_stream_id, s->pid);
			mysem_get(dvb_semaphore);
			cur = *services;
			while (cur != NULL)
			{
				service = ((EIT_service_t*)cur->data);
				dprintf("DVB: Check pmt for: pid %d/%d, media %lu/%lu, tsid %d/%d, freqs %lu/%lu, sid %d\n",
				        service->program_map.program_map_PID, s->pid, service->common.media_id, MEDIA_ID,
				        service->common.transport_stream_id, s->transport_stream_id, service->media.frequency,
				        (unsigned long)frequency, service->common.service_id);
				if (service->program_map.program_map_PID == s->pid && 
				    service->common.media_id == MEDIA_ID &&
				    service->common.transport_stream_id == s->transport_stream_id &&
				    service->media.frequency == (unsigned long)frequency )
				{
					dprintf("DVB: parse pmt filter ts %d, pid %d, service %d, media %d\n", 
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
			verbose("DVB: NIT 0x%04x for service 0x%04x\n", s->pid, table_id_ext);
			if (s->nit != NULL)
			{
				mysem_get(dvb_semaphore);
				updated = parse_nit(services, (unsigned char *)s->buf, MEDIA_ID, &media, s->nit);
				mysem_release(dvb_semaphore);
			}
			break;

    case 0x42:
    case 0x46:
			verbose("DVB: SDT (%s TS)\n", table_id == 0x42 ? "actual":"other");
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
				verbose("DVB: EIT 0x%04x for service 0x%04x\n", s->pid, table_id_ext);
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
			dprintf("DVB: Unknown 0x%04x for service 0x%04x", s->pid, table_id_ext);
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
				dprintf("DVB: Finished pid 0x%04x for table 0x%04x\n", s->pid, s->table_id);
				s->sectionfilter_done = 1;
			}
		}
	}

	if (updated)
	{
		s->was_updated = 1;
	}

	dprintf("DVB: parsed media %d,  0x%04X table 0x%02X, updated %d, done %d\n",
	        MEDIA_ID, s->pid, table_id, updated, s->sectionfilter_done);

	if (s->sectionfilter_done)
		return 1;

	return 0;
}

static int dvb_sectionRead (long frequency, char * demux_devname, struct section_buf *s)
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


static void dvb_filtersRead (long frequency, char* demux_devname)
{
	struct section_buf *s;
	int i, n, done;

	mysem_get(dvb_filter_semaphore);

	n = poll(poll_fds, n_running, 1000);
	if (n == -1)
		errorn("DVB: filter poll failed");

	for (i = 0; i < n_running; i++) {
		s = poll_section_bufs[i];
		if (!s)
		{
			fatal("DVB: poll_section_bufs[%d] is NULL\n", i);
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
				debug("DVB: filter timeout pid 0x%04x\n", s->pid);
			}
			dvb_filterRemove (s);
		}
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

static void dvb_scanForServices (long frequency, char * demux_devname, NIT_table_t *nit)
{
	struct section_buf pat_filter;
	struct section_buf sdt_filter;
	//struct section_buf sdt1_filter;
	struct section_buf eit_filter;
	struct section_buf nit_filter;

	/**
	 *  filter timeouts > min repetition rates specified in ETR211
	 */
	dvb_filterSetup (&pat_filter, demux_devname, 0x00, 0x00, 5, nit, &dvb_services); /* PAT */
	dvb_filterSetup (&nit_filter, demux_devname, 0x10, 0x40, 5, nit, &dvb_services); /* NIT */
	dvb_filterSetup (&sdt_filter, demux_devname, 0x11, 0x42, 5, nit, &dvb_services); /* SDT actual */
	//dvb_filterSetup (&sdt1_filter, demux_devname, 0x11, 0x46, 5, nit, &dvb_services); /* SDT other */
	dvb_filterSetup (&eit_filter, demux_devname, 0x12, -1, 5, nit, &dvb_services); /* EIT */

	dvb_filterAdd (&pat_filter);
	dvb_filterAdd (&nit_filter);
	dvb_filterAdd (&sdt_filter);
	//dvb_filterAdd (&sdt1_filter);
	dvb_filterAdd (&eit_filter);

	do
	{
		dvb_filtersRead (frequency, demux_devname);
	} while (!(list_empty(&running_filters) && list_empty(&waiting_filters)));

	//dvb_filterServices(&dvb_services);
}

void dvb_scanForEPG( tunerFormat tuner, unsigned long frequency )
{
	struct section_buf eit_filter;
	char demux_devname[256];
	int counter = 0;
	
	sprintf(demux_devname, "/dev/dvb/adapter%d/demux0", tuner);

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
}

void dvb_scanForPSI( tunerFormat tuner, unsigned long frequency, list_element_t **out_list )
{
	struct section_buf pat_filter;
	char demux_devname[256];
	int counter = 0;

	sprintf(demux_devname, "/dev/dvb/adapter%d/demux0", tuner);

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
				eprintf("DVB: FE_READ_STATUS failed\n");				\
				return -1;										\
			}													\
			if (s & FE_HAS_LOCK) break;							\
			usleep(500000);										\
		}														\
	}															\

static int dvb_setFrequency(fe_type_t  type, long frequency, int frontend_fd, int tuner,
                            int wait_for_lock, EIT_media_config_t *media, dvb_cancelFunctionDef* pFunction)
{
	struct dvb_frontend_parameters p;

	if (appControlInfo.dvbCommonInfo.streamerInput)
	{
		return 0;
	}
	
	memset(&p, 0, sizeof(p));

	eprintf("DVB: Current frequency %ld, new frequency %ld\n", currentFrequency[tuner], frequency);
/*
	if (currentFrequency[tuner] == frequency)
	{
		fe_status_t s;
		__u32 ber = BER_THRESHOLD;

		if (ioctl(frontend_fd, FE_READ_STATUS, &s) == -1)
		{
			eprintf("DVB: FE_READ_STATUS failed\n");
			return -1;
		}
		if (ioctl(frontend_fd, FE_READ_BER, &ber) == -1)
		{
			eprintf("DVB: FE_READ_BER failed\n");
			return -1;
		}

		dprintf("DVB: Check lock = %d, check ber = %lu\n", s & FE_HAS_LOCK, ber);

		if ((s & FE_HAS_LOCK) == 0 || ber >= BER_THRESHOLD)
		{
			eprintf("DVB: Force retune to %d\n", frequency);
			currentFrequency[tuner] = 0;
		}
	}

	// Only change the tuner frequency when absolutely necessary
	if (currentFrequency[tuner] != frequency)*/

#ifdef STSDK
	if (type == FE_QAM)
	{
		p.frequency=frequency;
		p.u.qam.fec_inner=FEC_AUTO;
		p.u.qam.symbol_rate=6750000;
		p.u.qam.modulation=QAM_64;

		if (ioctl(frontend_fd, FE_SET_FRONTEND, &p) < 0) {
			perror("FRONTEND FE_SET_FRONTEND: ");
			return -1;
		}

		int lock_tries;
		int32_t status = 0;

		for (lock_tries = 20; lock_tries>0; lock_tries--)
		{
			ioctl(frontend_fd, FE_READ_STATUS, &status);
			/*
			int32_t ber;
			u_int16_t snr, str;
			int32_t uncorrected_blocks;

			ioctl(frontend_fd, FE_READ_SNR, &snr);
			ioctl(frontend_fd, FE_READ_SIGNAL_STRENGTH, &str);
			ioctl(frontend_fd, FE_READ_BER, &ber);
			ioctl(frontend_fd, FE_READ_UNCORRECTED_BLOCKS, &uncorrected_blocks);

			printf("status=0x%02x: %s%s%s%s%s%s%s\n", status,
							(status & FE_HAS_SIGNAL)?"FE_HAS_SIGNAL ":"",
							(status & FE_HAS_CARRIER)?"FE_HAS_CARRIER ":"",
							(status & FE_HAS_VITERBI)?"FE_HAS_VITERBI ":"",
							(status & FE_HAS_SYNC)?"FE_HAS_SYNC ":"",
							(status & FE_HAS_LOCK)?"FE_HAS_LOCK ":"",
							(status & FE_TIMEDOUT)?"FE_TIMEDOUT ":"",
							(status & FE_REINIT)?"FE_REINIT ":""
				);
			printf("snr=%d%%\nstr=%d%%\nber=%d\nuncorrected_blocks=%d\n",
			       snr*100/65535, str*100/65535, ber, uncorrected_blocks);*/
			if ((status & FE_HAS_LOCK) == FE_HAS_LOCK)
			{
				currentFrequency[tuner] = frequency;
				return 1;
			}
			usleep(500000);
		}
	} else
#endif // STSDK
	{
		fe_status_t s;
		int lockedBeforeChange = 0;

		eprintf("DVB: Setting frequency to %ld\n", frequency);

		if (type == FE_OFDM && (media == NULL || media->type == serviceMediaDVBT))
		{
			if (media != NULL)
			{
				p.u.ofdm.bandwidth = media->dvb_t.bandwidth;
			} else
			{
				p.u.ofdm.bandwidth = appControlInfo.dvbtInfo.bandwidth;
			}
			p.u.ofdm.code_rate_HP = FEC_AUTO;
			p.u.ofdm.code_rate_LP = FEC_AUTO;
			p.u.ofdm.constellation = QAM_AUTO;
			p.u.ofdm.hierarchy_information = HIERARCHY_AUTO;
			p.u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
			p.u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
			p.inversion = appControlInfo.dvbtInfo.fe.inversion == 2 ?
				INVERSION_AUTO : (appControlInfo.dvbtInfo.fe.inversion == 1 ? INVERSION_ON : INVERSION_OFF);
		}
		else if (type == FE_QAM && (media == NULL || media->type == serviceMediaDVBC))
		{
			if (media != NULL)
			{
				p.u.qam.modulation  = media->dvb_c.modulation;
				p.u.qam.symbol_rate = media->dvb_c.symbol_rate;
			} else
			{
				p.u.qam.modulation  = appControlInfo.dvbcInfo.modulation;
				p.u.qam.symbol_rate = appControlInfo.dvbcInfo.symbolRate*1000;
			}
			p.inversion = appControlInfo.dvbcInfo.fe.inversion == 2 ? 
				INVERSION_AUTO : (appControlInfo.dvbcInfo.fe.inversion == 1 ? INVERSION_ON : INVERSION_OFF);
			p.u.qam.fec_inner   = FEC_NONE;
			eprintf("DVB: Symbol rate %ld, modulation %ld invertion %d\n",
			        p.u.qam.symbol_rate, p.u.qam.modulation, p.inversion);
		}
		else
		{
			eprintf("DVB: ERROR: Unsupported frontend type=%s (media %d).\n",
			        gFE_Type_Names[type], media != NULL ? (int)media->type : -1);
			return -1;
		}

		p.frequency = frequency;

		if (ioctl(frontend_fd, FE_READ_STATUS, &s) == -1)
		{
			eprintf("DVB: FE_READ_STATUS failed\n");
			return -1;
		}
		if (s & FE_HAS_LOCK)
		{
			lockedBeforeChange = 1;
		}

		if (ioctl(frontend_fd, FE_SET_FRONTEND, &p) == -1) {
			eprintf("DVB: Setting frontend parameters failed\n");
			return -1;
		}

		if (wait_for_lock)
		{
			if (lockedBeforeChange)
			{
				//eprintf("DVB: Was locked, wait 1 sec\n");
				/* This is faster than wait for PID filters timeout... */
				//BREAKABLE_SLEEP(1000000);
			}

			if (appControlInfo.dvbCommonInfo.adapterSpeed >= 0)
			{
				int hasSignal = 0;
				int hasLock = 0;
				__u32 ber = BER_THRESHOLD;
#ifdef STBTI
				int timeout = (appControlInfo.dvbCommonInfo.adapterSpeed+1)*10;
#else
				int timeout = (appControlInfo.dvbCommonInfo.adapterSpeed+1);
#endif
				do
				{
					if (ioctl(frontend_fd, FE_READ_BER, &ber) == -1)
					{
						eprintf("DVB: FE_READ_BER failed\n");
						return -1;
					}
					/* If ber is not -1, then wait a bit more */
					if (ber == 0xffffffff)
					{
						dprintf("DVB: All clear...\n");
						BREAKABLE_SLEEP(1000000);
					} else
					{
						eprintf("DVB: Something is out there...\n");
						BREAKABLE_SLEEP(3000000);
					}
					if (ioctl(frontend_fd, FE_READ_STATUS, &s) == -1)
					{
						eprintf("DVB: FE_READ_STATUS failed\n");
						return -1;
					}
					if (s & FE_HAS_LOCK)
					{
						if (hasLock == 0)
						{
							//timeout += appControlInfo.dvbCommonInfo.adapterSpeed*10;
							hasLock = 1;
						}
						dprintf("DVB: L");
						/* locked, give adapter even more time... */
						//usleep (appControlInfo.dvbCommonInfo.adapterSpeed*10000);
					} else if (s & FE_HAS_SIGNAL)
					{
						if (hasSignal == 0)
						{
							eprintf("DVB: Has signal\n");
							timeout += appControlInfo.dvbCommonInfo.adapterSpeed;
							hasSignal = 1;
						}
						dprintf("DVB: S");
						/* found something above the noise level, increase timeout time */
						//usleep (appControlInfo.dvbCommonInfo.adapterSpeed*10000);
					} else
					{
						dprintf("DVB: N");
						/* there's no and never was any signal, reach timeout faster */
						if (hasSignal == 0)
						{
							eprintf("DVB: Skip\n");
							--timeout;
						}
					}
				} while ( (ber >= BER_THRESHOLD) && (--timeout > 0));
				dprintf("DVB: %lu timeout %d, ber %d\n", frequency, timeout, ber);
				currentFrequency[tuner] = frequency;
				eprintf("DVB: Frequency set, lock: %d\n", hasLock);
				return hasLock;
			}
		}
		currentFrequency[tuner] = frequency;
	}
	return 0;
}

static int dvb_checkFrequency(fe_type_t  type, __u32 * frequency, int frontend_fd, int tuner, 
                              __u32* ber, long fe_step_size, EIT_media_config_t *media,
                              dvb_cancelFunctionDef* pFunction)
{
	int res;

	currentFrequency[tuner] = 0;

	if ((res = dvb_setFrequency(type, *frequency, frontend_fd, tuner, 1, media, pFunction)) == 0)
	{
		int i;

		currentFrequency[tuner] = 0;

		for (i = 0; i < 10; i++)
		{
			struct dvb_frontend_parameters fe;
			fe_status_t s;

			while (1)
			{
				// Get the front-end state including current freq. setting.
				// Some drivers may need to be modified to update this field.
				if (ioctl(frontend_fd, FE_GET_FRONTEND, &fe) == -1)
				{
					eprintf("DVB: FE_GET_FRONTEND failed\n");
					return -1;
				}
				if (ioctl(frontend_fd, FE_READ_STATUS, &s) == -1)
				{
					eprintf("DVB: FE_READ_STATUS failed\n");
					return -1;
				}
				dprintf("DVB: f_orig=%ld   f_read=%ld status=%d\n",
				        *frequency, (long) fe.frequency, s);
				// in theory a clever FE could adjust the freq. to give a perfect freq. needed !
				if (fe.frequency == *frequency)
				{
					break;
				}
				else
				{
#ifdef DEBUG
					long gap;

					if (fe.frequency > *frequency)
						gap = fe.frequency - *frequency;
					else
						gap = *frequency - fe.frequency;

					dprintf("%s: gap = %06ld fe_step_size=%06ld\n", __func__, gap, fe_step_size);
					// if (gap < fe_step_size) // may lead to hangs, so disabled
						// FE thinks its done a better job than this app. !
#endif
					break;
				}
				usleep (20000);
			}

			if (s & FE_HAS_LOCK)
			{
				if (ioctl(frontend_fd, FE_READ_BER, ber) == -1)
				{
					eprintf("DVB: FE_READ_BER failed\n");
					return -1;
				}
				dprintf("DVB: Got LOCK f=%ld\n", *frequency);
				return 1;
			}
		}
		dprintf("DVB: No Lock f=%ld\n", *frequency);
	} else if (res == 1)
	{
		dprintf("DVB: Trust LOCK f=%ld\n", *frequency);

		currentFrequency[tuner] = 0;
		return 1;
	} else if (res == -1)
	{
		dprintf("DVB: Fail or user abort f=%ld\n", *frequency);
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
	dprintf("DVB: Export services\n");
	mysem_get(dvb_semaphore);
	dump_services(dvb_services, filename );
	mysem_release(dvb_semaphore);
	/*appControlInfo.dvbInfo[screenMain].channel =
	appControlInfo.dvbInfo[screenPip].channel = 0;*/

	dprintf("DVB: Services exported\n");
}

void dvb_clearNIT(NIT_table_t *nit)
{
	free_transport_streams(&nit->transport_streams);
	memset(nit, 0, sizeof(NIT_table_t));
}

void dvb_clearServiceList(int permanent)
{
	dprintf("DVB: Clearing service list\n");
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

	/*// Goes in the way of auto-loading last selected channel
	appControlInfo.pvrInfo.recordInfo[screenMain].channel =
	appControlInfo.pvrInfo.recordInfo[screenPip].channel =
	appControlInfo.dvbInfo[screenMain].channel =
	appControlInfo.dvbInfo[screenPip].channel = 0;
	*/
	return res;
}

int dvb_getType(tunerFormat tuner)
{
#ifdef STSDK
	if (appControlInfo.tunerInfo[tuner].status == tunerNotPresent)
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
			/*
			*low_freq  = (101500 * KHZ);
			*high_freq = (150000 * KHZ);
			*freq_step = (  8000 * KHZ);
			*/
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
			eprintf("DVB: ATSC FEs not yet supported");
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

int dvb_getSignalInfo(tunerFormat tuner,
                      uint16_t *snr, uint16_t *signal, uint32_t *ber, uint32_t *uncorrected_blocks)
{
	int frontend_fd;
	fe_status_t status;

	mysem_get(dvb_fe_semaphore);

	if ((frontend_fd = dvb_openFrontend(tuner, O_RDONLY | O_NONBLOCK)) < 0)
	{
		eprintf("%s: failed opening frontend %d\n", __FUNCTION__, tuner);
#ifndef ENABLE_PVR
		mysem_release(dvb_fe_semaphore);
		return -1;
	}
#else
	} else
	{
#endif
	ioctl(frontend_fd, FE_READ_STATUS, &status);
	ioctl(frontend_fd, FE_READ_SIGNAL_STRENGTH, signal);
	ioctl(frontend_fd, FE_READ_SNR, snr);
	ioctl(frontend_fd, FE_READ_BER, ber);
	ioctl(frontend_fd, FE_READ_UNCORRECTED_BLOCKS, uncorrected_blocks);

	close(frontend_fd);
#ifdef ENABLE_PVR
	}
#endif
	if (status & FE_HAS_LOCK)
	{
		mysem_release(dvb_fe_semaphore);
		return 1;
	}

	mysem_release(dvb_fe_semaphore);
	return 0;
}

int dvb_serviceScan( tunerFormat tuner, dvb_displayFunctionDef* pFunction)
{
	char demux_devname[32];
	int err;
	int frontend_fd;
	__u32 frequency;
	__u32 low_freq = 0, high_freq = 0, freq_step = 0;
	struct dvb_frontend_info fe_info;

	dvb_getTuner_freqs(tuner, &low_freq, &high_freq, &freq_step);

#ifdef STSDK
	cJSON *params = cJSON_CreateObject();
	if (!params)
	{
		eprintf("%s: out of memory\n", __FUNCTION__);
		return -1;
	}
	cJSON *result = NULL;
	elcdRpcType_t type = elcdRpcInvalid;
	int res = -1;

	if (tuner >= VMSP_COUNT)
	{
		cJSON_AddItemToObject(params, "tuner", cJSON_CreateNumber( tuner-VMSP_COUNT ) );
		cJSON_AddItemToObject(params, "start", cJSON_CreateNumber(  low_freq/KHZ ) );
		cJSON_AddItemToObject(params, "stop" , cJSON_CreateNumber( high_freq/KHZ ) );

		st_setTuneParams(tuner-VMSP_COUNT, params);
		eprintf("%s: scanning %6u-%6u\n", __FUNCTION__, low_freq/KHZ, high_freq/KHZ);
		res = st_rpcSyncTimeout(elcmd_dvbscan, params, 30, &type, &result );
		cJSON_Delete(params);
		cJSON_Delete(result);
		if (res != 0 || type != elcdRpcResult)
		{
			eprintf("%s: scan failed\n", __FUNCTION__ );
			return -1;
		}

		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);

		return 0;
	}

	cJSON *p_freq = cJSON_CreateNumber(0);
	if (!p_freq)
	{
		eprintf("%s: out of memory\n", __FUNCTION__);
		goto service_scan_failed;
	}
	cJSON_AddItemToObject(params, "frequency", p_freq);
#endif

	sprintf(demux_devname, "/dev/dvb/adapter%d/demux0", tuner);

	dprintf("DVB: Scan Adapter /dev/dvb/adapter%d/\n", tuner);

	/* Clear any existing channel list */
	//dvb_clearServiceList();

	if(!appControlInfo.dvbCommonInfo.streamerInput)
	{
		if ((frontend_fd = dvb_openFrontend(tuner, O_RDWR)) < 0)
		{
			eprintf("%s: failed opening frontend %d\n", __FUNCTION__, tuner);
			goto service_scan_failed;
		}

		do
		{
			err = ioctl(frontend_fd, FE_GET_INFO, &fe_info);
			if (err < 0) {
				eprintf("DVB: ioctl FE_GET_INFO failed ... trying again\n");
			}
		} while (err<0);

		if ((fe_info.type == FE_OFDM) || (fe_info.type == FE_QAM))   // DVB-T or DVB-C
		{
			eprintf("DVB: Scanning DVB FrontEnd-%d  Model=%s,  Type=%s ; ", 
			        tuner, fe_info.name, gFE_Type_Names[fe_info.type]);
			eprintf("DVB: FineStepSize=%ld, StepCount=%d\n",
			        (long)fe_info.frequency_stepsize, MAX_OFFSETS);
		}
		else if ((fe_info.type == FE_QPSK) || (fe_info.type == FE_ATSC)) // DVB-S or ATSC
		{
			eprintf("DVB: Scanning DVB FrontEnd-%d  Model=%s,  Type=%s\n, NOT SUPPORTED",
			        tuner, fe_info.name, gFE_Type_Names[fe_info.type]);

			goto service_scan_failed;
		}
		else
		{
			eprintf("DVB: Scanning DVB FrontEnd-%d  Model=%s,  INVALID FE_TYPE ERROR\n", tuner, fe_info.name);
			goto service_scan_failed;
		}

		// Use the FE's own start/stop freq. for searching (if not explicitly app. defined).
		if (!low_freq)
		{
			low_freq = fe_info.frequency_min;
		}
		if (!high_freq)
		{
			high_freq = fe_info.frequency_max;
		}

		ZERO_SCAN_MESAGE();

		SCAN_MESSAGE("DVB: Scan: StartFreq=%lu, StopFreq=%lu, StepSize=%lu\n",
		             (unsigned long)low_freq, (unsigned long)high_freq, (unsigned long)freq_step);
		SCAN_MESSAGE("==================================================================================\n");

		eprintf     ("DVB: Scan: StartFreq=%lu, StopFreq=%lu, StepSize=%lu\n",
		             (unsigned long)low_freq, (unsigned long)high_freq, (unsigned long)freq_step);
		eprintf     ("==================================================================================\n");

		/* Go through the range of frequencies */
		for(frequency = low_freq; frequency <= high_freq; frequency += freq_step)
		{
			__u32 match_ber;
			__u32 f;
			__u32 i;
			int found=0;

			f = frequency;

			if (1== dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
			                           &match_ber, fe_info.frequency_stepsize, NULL, NULL))
			{
					found = 1;
			}

			dprintf("DVB: Check main freq: %ld\n", f);

			if (appControlInfo.dvbCommonInfo.extendedScan)
			{
				for (i=1; !found && (i<=MAX_OFFSETS); i++)
				{
					__u32 offset = i * fe_info.frequency_stepsize;

					f = frequency - offset;
					//dprintf("DVB: Check lower freq: %ld\n", f);
					if (1== dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
					                           &match_ber, fe_info.frequency_stepsize, NULL, NULL))
					{
					   found = 1;
					   break;
					}
					f = frequency + offset;
					//dprintf("DVB: Check higher freq: %ld\n", f);
					if (1== dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
					                           &match_ber, fe_info.frequency_stepsize, NULL, NULL))
					{
					   found = 1;
					   break;
					}
				}
			}

			if (found)
			{
				dprintf("DVB: Found something on %ld, search channels\n!", f);
				SCAN_MESSAGE("DVB: Found something on %ld, search channels\n!", f);
				eprintf("DVB: @@@@@@@@@@@@@@@@@@@@@@ LOCKED f= %ld \n", f);
				/* Scan for channels within this frequency / transport stream */
#ifdef STSDK
				p_freq->valueint = f/KHZ;
				res = st_rpcSync(elcmd_dvbtune, params, &type, &result );
				cJSON_Delete(result); // ignore
				result = NULL;
				eprintf("%s: scanning %6u\n", __FUNCTION__, f);
				res = st_rpcSync(elcmd_dvbscan, NULL, &type, &result );
				cJSON_Delete(result);
				if (res != 0 || type != elcdRpcResult)
				{
					eprintf("%s: scan failed\n", __FUNCTION__ );
				} else
					dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
#else
				lock_filters = 0;
				dvb_scanForServices (f, demux_devname, NULL);
				lock_filters = 1;
#endif // STSDK
				SCAN_MESSAGE("DVB: Found %d channels ...\n", dvb_getNumberOfServices());
				dprintf("DVB: Found %d channels ...\n", dvb_getNumberOfServices());
			} else
			{
				eprintf("DVB: ... NOTHING f= %ld \n", f);
			}
			if (pFunction != NULL && pFunction(f, dvb_getNumberOfServices(), tuner, 0, 0) == -1)
			{
				dprintf("DVB: BREAK: pFunction(f, serviceCount, tuner) == -1\n");
				break;
			}
		}

		close(frontend_fd);
	}
	else
	{
		lock_filters = 0;
		dvb_scanForServices (0, demux_devname, NULL);
		lock_filters = 1;
		dprintf("DVB: Found %d channels ...\n", dvb_getNumberOfServices());
	}

	/* Write the channel list to the root file system */
	dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);

	dprintf("DVB: Tuning complete\n");

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
	int err;
	int frontend_fd = 0;
	struct dvb_frontend_info fe_info;
	long current_frequency_number = 1, max_frequency_number = 0;

#ifdef STSDK
	cJSON *params = cJSON_CreateObject();
	if (!params)
	{
		eprintf("%s: out of memory\n", __FUNCTION__);
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
			eprintf("%s: failed opening frontend %d\n", tuner);

			return -1;
		}

		if (!dvb_setFrequency(dvb_getType(tuner), frequency, frontend_fd, tuner, 1, NULL, NULL))
		{
			eprintf("%s: failed to set frequency to %.3f MHz\n", __FUNCTION__, (float)frequency/MHZ);
			//close(frontend_fd);
			//return -1;
		}
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
		eprintf("%s: scanning %6u\n", __FUNCTION__, frequency / KHZ);
		int res = st_rpcSyncTimeout(elcmd_dvbscan, params, 20, &type, &result );
		cJSON_Delete(params);
		cJSON_Delete(result);
		if (frontend_fd != 0) close(frontend_fd);
		if (res != 0 || type != elcdRpcResult)
		{
			eprintf("%s: failed to scan %6u\n", __FUNCTION__, frequency / KHZ );
			return -1;
		}
		dvb_readServicesFromDump(appControlInfo.dvbCommonInfo.channelConfigFile);
		return 0;
	}
#endif

	sprintf(demux_devname, "/dev/dvb/adapter%d/demux0", tuner);

	dprintf("%s: Scan Adapter %d\n", __FUNCTION__, tuner);

	//INIT_LIST_HEAD(&services);
	//INIT_LIST_HEAD(&lcns);

	if (!appControlInfo.dvbCommonInfo.streamerInput)
	{
		if ((frontend_fd = dvb_openFrontend(tuner, O_RDWR)) < 0)
		{
			eprintf("%s: failed opening frontend %d\n", __FUNCTION__, tuner);

			return -1;
		}

		do
		{
			err = ioctl(frontend_fd, FE_GET_INFO, &fe_info);
			if (err < 0) {
				eprintf("DVB: ioctl FE_GET_INFO failed ... trying again\n");
			}
		} while (err<0);

		if ((fe_info.type == FE_OFDM) || (fe_info.type == FE_QAM))   // DVB-T or DVB-C
		{
			eprintf("DVB: Scanning DVB FrontEnd-%d  Model=%s,  Type=%s ; ",
			        tuner, fe_info.name, gFE_Type_Names[fe_info.type]);
			eprintf("DVB: FineStepSize=%ld, StepCount=%d\n",
			        (long)fe_info.frequency_stepsize, MAX_OFFSETS);
		}
		else if ((fe_info.type == FE_QPSK) || (fe_info.type == FE_ATSC)) // DVB-S or ATSC
		{
			eprintf("DVB: Scanning DVB FrontEnd-%d  Model=%s,  Type=%s\n, NOT SUPPORTED",
			        tuner, fe_info.name, gFE_Type_Names[fe_info.type]);
			close(frontend_fd);
			return -1;
		}
		else
		{
			eprintf("DVB: Scanning DVB FrontEnd-%d  Model=%s,  INVALID FE_TYPE ERROR\n", tuner, fe_info.name);
			close(frontend_fd);
			return -1;
		}

		// Use the FE's own start/stop freq. for searching (if not explicitly app. defined).
		if( frequency < fe_info.frequency_min || frequency > fe_info.frequency_max )
		{
			eprintf("DVB: Frequency is out of range!\n");
			close(frontend_fd);
			return 1;
		}

		ZERO_SCAN_MESAGE();

		SCAN_MESSAGE("DVB: Scan: Freq=%lu\n", (unsigned long)frequency);
		SCAN_MESSAGE("==================================================================================\n");

		eprintf     ("DVB: Scan: Freq=%lu\n", (unsigned long)frequency);
		eprintf     ("==================================================================================\n");

		__u32 match_ber;
		__u32 f;
		long i;
		int found=0;

		f = frequency;

		if (1== (err = dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner, 
		                                  &match_ber, fe_info.frequency_stepsize, media, pCancelFunction)))
		{
			found = 1;
		}
		if (err == -1)
		{
			printf("DVB: BREAK: pCancelFunction() == -1\n");
			close(frontend_fd);
			return -1;
		}

		/* Just keep tuner on frequency */
		if (save_service_list == -1)
		{
			for(;;)
			{
				if (pFunction == NULL ||
				    pFunction(frequency, 0, tuner, current_frequency_number, max_frequency_number) == -1)
				{
					printf("DVB: BREAK: pFunction(f, serviceCount, tuner) == -1\n");
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

		dprintf("DVB: Check main freq: %ld\n", f);

		if (appControlInfo.dvbCommonInfo.extendedScan)
		{
			for (i=1; !found && (i<=MAX_OFFSETS); i++)
			{
				long offset = i * fe_info.frequency_stepsize;

				f = frequency - offset;
				//dprintf("DVB: Check lower freq: %ld\n", f);
				if (1== (err = dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
				                 &match_ber, fe_info.frequency_stepsize, media, pCancelFunction)))
				{
					found = 1;
					break;
				}
				if (err == -1)
				{
					dprintf("DVB: BREAK: pCancelFunction() == -1\n");
					close(frontend_fd);
					return -1;
				}
				f = frequency + offset;
				//dprintf("DVB: Check higher freq: %ld\n", f);
				if (1== (err = dvb_checkFrequency(fe_info.type, &f, frontend_fd, tuner,
				                 &match_ber, fe_info.frequency_stepsize, media, pCancelFunction)))
				{
					found = 1;
					break;
				}
				if (err == -1)
				{
					dprintf("DVB: BREAK: pCancelFunction() == -1\n");
					close(frontend_fd);
					return -1;
				}
			}
		}

		if (found)
		{
			dprintf("DVB: Found something on %ld, search channels\n!", f);
			SCAN_MESSAGE("DVB: Found something on %ld, search channels\n!", f);
			dprintf("DVB: @@@@@@@@@@@@@@@@@@@@@@ LOCKED f= %ld \n", f);
			/* Scan for channels within this frequency / transport stream */
			lock_filters = 0;
			dvb_scanForServices (f, demux_devname, scan_network);
			lock_filters = 1;
			SCAN_MESSAGE("DVB: Found %d channels ...\n", dvb_getNumberOfServices());
			dprintf("DVB: Found %d channels ...\n", dvb_getNumberOfServices());
		}

		close(frontend_fd);
	}
	else
	{
		lock_filters = 0;
		dvb_scanForServices (frequency, demux_devname, scan_network);
		lock_filters = 1;
		dprintf("DVB: Found %d channels ...\n", dvb_getNumberOfServices());
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
					dprintf("DVB: BREAK: pFunction(f, serviceCount, tuner) == -1\n");
					break;
				}
				eprintf("DVB: Network Scan: %lu Hz\n", tsrtream->media.frequency);
				dvb_frequencyScan(tuner, tsrtream->media.frequency,
				                  &tsrtream->media, pFunction, NULL, 0, pCancelFunction);
				current_frequency_number++;
			}
			tstream_element = tstream_element->next;
		}
	}

	if (save_service_list)
	{
		/* Write the channel list to the root file system */
		dvb_exportServiceList(appControlInfo.dvbCommonInfo.channelConfigFile);
	}

	dprintf("DVB: Frequency scan completed\n");

	return 0;
}

/*
   clear and inits the given instance.

   @param mode @b IN The DVB mode to set up
*/

static void dvb_instanceInit(struct dvb_instance * dvb)
{
	memset( dvb, 0, sizeof(struct dvb_instance));

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
	dvb->fdf = -1;
	dvb->fdin = -1;
	dvb->fdout = -1;
}

static int dvb_instanceSetMode (struct dvb_instance * dvb, DvbMode_t mode)
{
	dvb_instanceInit(dvb);

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
	
	dvb->mode = mode;
	dvb->running = 1;

	return 0;
}

static int dvb_serviceChange(struct dvb_instance * dvb, EIT_media_config_t *media)
{
	int ret;

#ifdef ENABLE_PVR
	if (dvb->mode != DvbMode_Play && dvb->setFrequency > 0)
#else
	if (dvb->mode != DvbMode_Play)
#endif
	{
		ret = dvb_setFrequency(dvb->fe_type, dvb->setFrequency, dvb->fdf, dvb->vmsp, 1, media, NULL);
		if (ret < 0)
		{
			PERROR("DVB: Set frequency failed");
			return ret;
		}
	}

#ifdef STSDK
	return 0;
#else
	dprintf("%s: setting filterv=%d filtera=%d filtert=%d filterp=%d\n", __FUNCTION__,
	        dvb->filterv.pid, dvb->filtera.pid, dvb->filtert.pid, dvb->filterp.pid );

	ret = ioctl(dvb->fdv, DMX_SET_PES_FILTER, &dvb->filterv);
	if (ret != 0)
	{
		PERROR("DVB: ioctl DMX_SET_PES_FILTER fdv failed");
		return ret;
	}

#ifdef ENABLE_MULTI_VIEW
	if( dvb->mode == DvbMode_Multi )
	{
		ret = ioctl(dvb->fdv1, DMX_SET_PES_FILTER, &dvb->filterv1);
		if (ret != 0)
		{
				PERROR("DVB: ioctl DMX_SET_PES_FILTER fdv1 failed");
				return ret;
		}
	
		ret = ioctl(dvb->fdv2, DMX_SET_PES_FILTER, &dvb->filterv2);
		if (ret != 0)
		{
				PERROR("DVB: ioctl DMX_SET_PES_FILTER fdv2 failed");
				return ret;
		}
	
		ret = ioctl(dvb->fdv3, DMX_SET_PES_FILTER, &dvb->filterv3);
		if (ret != 0)
		{
				PERROR("DVB: ioctl DMX_SET_PES_FILTER fdv3 failed");
				return ret;
		}
		dprintf("%s: filterv1=%d filterv2=%d filterv3=%d\n", __FUNCTION__,
		        dvb->filterv1.pid,dvb->filterv2.pid,dvb->filterv3.pid);
	} else
#endif // ENABLE_MULTI_VIEW
	{
		ret = ioctl(dvb->fda, DMX_SET_PES_FILTER, &dvb->filtera);
		if (ret != 0)
		{
			PERROR("DVB: ioctl DMX_SET_PES_FILTER fda failed");
			return ret;
		}

		ret = ioctl(dvb->fdp, DMX_SET_PES_FILTER, &dvb->filterp);
		if (ret != 0)
		{
			PERROR("DVB: ioctl DMX_SET_PES_FILTER fdp failed");
			return ret;
		}

		ret = ioctl(dvb->fdt, DMX_SET_PES_FILTER, &dvb->filtert);
		if (ret != 0)
		{
			PERROR("DVB: ioctl DMX_SET_PES_FILTER fdt failed");
			return ret;
		}
	}

	dprintf("%s: starting PID filters\n", __FUNCTION__);

	if (dvb->filterv.pid != 0)
	{
		ret = ioctl(dvb->fdv, DMX_START);
		if (ret != 0)
		{
			PERROR("DVB: ioctl DMX_START fdv failed");
			//return ret;
		}
	}

#ifdef ENABLE_MULTI_VIEW
	if( dvb->mode == DvbMode_Multi )
	{
		if (dvb->filterv1.pid != 0)
		{
			ret = ioctl(dvb->fdv1, DMX_START);
			if (ret != 0)
			{
				PERROR("DVB: ioctl DMX_START fdv1 failed");
				return ret;
			}
		}
	
		if (dvb->filterv2.pid != 0)
		{
			ret = ioctl(dvb->fdv2, DMX_START);
			if (ret != 0)
			{
				PERROR("DVB: ioctl DMX_START fdv2 failed");
				return ret;
			}
		}
	
		if (dvb->filterv3.pid != 0)
		{
			ret = ioctl(dvb->fdv3, DMX_START);
			if (ret != 0)
			{
				PERROR("DVB: ioctl DMX_START fdv3 failed");
				return ret;
			}
		}
	} else
#endif // ENABLE_MULTI_VIEW
	{
		if (dvb->filtera.pid != 0)
		{
			ret = ioctl(dvb->fda, DMX_START);
			if (ret != 0)
			{
				PERROR("DVB: ioctl DMX_START fda failed");
				//return ret;
			}
		}
		if (dvb->filtert.pid != 0)
		{
			ret = ioctl(dvb->fdt, DMX_START);
			if (ret != 0)
			{
				PERROR("DVB: ioctl DMX_START fdt failed");
				//return ret;
			}
		}
#ifdef STBTI
		if (dvb->filterp.pid != dvb->filtera.pid &&
		    dvb->filterp.pid != dvb->filterv.pid &&
		    dvb->filterp.pid != dvb->filtert.pid)
		{
			dprintf("DVB: INIT PCR!\n");
			ret = ioctl(dvb->fdp, DMX_START);
			if (ret != 0)
			{
				PERROR("DVB: ioctl DMX_START fdp failed");
				return ret;
			}
		}
#else
		if (dvb->filterp.pid != 0)
		{
			ret = ioctl(dvb->fdp, DMX_START);
			if (ret != 0)
			{
				PERROR("DVB: ioctl DMX_START fdp failed");
				return ret;
			}
		}
#endif // STBTI
	}
	return ret;
#endif // STSDK
}
/*
   create a dvb player instance by chaining the basic devices.
   Uses given device setup data (params) & device paths.
*/
static int open_dvb_instance (struct dvb_instance * dvb, int vmsp, char *pFilename)
{
	char path[MAX_PATH_LENGTH];
	int err;

	dprintf("%s: adapter%d/frontend0\n", __FUNCTION__, vmsp);

	dvb->vmsp = vmsp;
	dvb->fe_tracker_Thread = 0;

	if (!appControlInfo.dvbCommonInfo.streamerInput && (dvb->mode != DvbMode_Play))
	{
		struct dvb_frontend_info fe_info;

		if ((dvb->fdf = dvb_openFrontend(vmsp, O_RDWR)) < 0)
		{
			PERROR ("DVB: failed opening '%s'", path);
#ifndef ENABLE_PVR
			return -1;
		}
#else
			dvb->fdf = -1;
		} else
#endif
		{
			do
			{
				err = ioctl(dvb->fdf, FE_GET_INFO, &fe_info);
				if (err < 0) {
					info("ioctl FE_GET_INFO failed ... trying again\n");
				}
			} while (err!=0);
	
			dvb->fe_type = fe_info.type;
	
			if ((fe_info.type == FE_OFDM) || (fe_info.type == FE_QAM))   // DVB-T or DVB-C
			{
				dprintf("DVB: FrontEnd-%d  Model=%s\n", vmsp, fe_info.name);
			}
			else if ((fe_info.type == FE_QPSK) || (fe_info.type == FE_ATSC)) // DVB-S or ATSC
			{
				eprintf("DVB: FrontEnd-%d  Model=%s,  Type=%s\n, NOT SUPPORTED by this app.",
				        vmsp, fe_info.name, gFE_Type_Names[fe_info.type]);
				return -1;
			}
			else
			{
				eprintf("DVB: FrontEnd-%d  Model=%s,  INVALID FE_TYPE ERROR\n", vmsp, fe_info.name);
				return -1;
			}
		}
	}

#ifndef STSDK
	dprintf("%s: adapter%d/demux0\n", __FUNCTION__, vmsp);

	sprintf(path, "/dev/dvb/adapter%i/demux0", vmsp);
	if ((dvb->fdv = open(path, O_RDWR)) <0)
	{
		PERROR ("DVB: failed opening '%s' for video", path );
		return -1;
	}

#ifdef ENABLE_MULTI_VIEW
	if ( dvb->mode == DvbMode_Multi )
	{
		if ((dvb->fdv1 = open(path, O_RDWR)) <0)
		{
			PERROR ("DVB: failed opening '%s' for video1", path );
			return -1;
		}

		if ((dvb->fdv2 = open(path, O_RDWR)) <0)
		{
			PERROR ("DVB: failed opening '%s' for video2", path );
			return -1;
		}

		if ((dvb->fdv3 = open(path, O_RDWR)) <0)
		{
			PERROR ("DVB: failed opening '%s' for video3", path );
			return -1;
		}
	} else
#endif
	{
		if ((dvb->fda = open(path, O_RDWR)) <0)
		{
			PERROR ("DVB: failed opening '%s' for audio", path );
			return -1;
		}

		if ((dvb->fdp = open(path, O_RDWR)) <0)
		{
			PERROR ("DVB: failed opening '%s' for pcr", path );
			return -1;
		}

		if ((dvb->fdt = open(path, O_RDWR)) <0)
		{
			PERROR ("DVB: failed opening '%s' for text", path );
			return -1;
		}
	}

	if (dvb->mode == DvbMode_Record
	 || dvb->filtert.pid
#ifdef ENABLE_MULTI_VIEW
	 || dvb->mode == DvbMode_Multi
#endif
	   )
	{
		dprintf("%s: adapter%i/dvr0\n", __FUNCTION__, vmsp);

		sprintf(path, "/dev/dvb/adapter%i/dvr0", vmsp);
		if ((dvb->fdin = open(path, O_RDONLY)) <0)
		{
			PERROR ("DVB: failed opening '%s' for read", path);
			return -1;
		}
#ifdef ENABLE_MULTI_VIEW
		if( dvb->mode == DvbMode_Record )
#endif
		{
			strcpy(dvb->directory, pFilename);
			sprintf(path, "%s/part%02d.spts", dvb->directory, dvb->fileIndex);
			if ((dvb->fdout = open(path, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) <0)
			{
				PERROR ("DVB: failed opening '%s' for write", path);
				return -1;
			}
		}
	}
	else if (dvb->mode == DvbMode_Play)
	{
		dprintf("%s: %s/part%02d.spts\n", __FUNCTION__, dvb->directory, dvb->fileIndex);

		strcpy(dvb->directory, pFilename);
		dvb->pvr_rate = DEFAULT_PVR_RATE;
		sprintf(path, "%s/part%02d.spts", dvb->directory, dvb->fileIndex);
		if ((dvb->fdin = open(path, O_RDONLY)) <0)
		{
			PERROR ("DVB: failed opening '%s' for read", path);
			return -1;
		}

		/* Set the current read position */
		lseek(dvb->fdin, (dvb->pvr_position/TS_PACKET_SIZE)*TS_PACKET_SIZE, 0);
		dvb->pvr_last_position = dvb->pvr_position;
		dvb->pvr_last_index = dvb->fileIndex;

		dprintf("%s: adapter%i/dvr0\n", __FUNCTION__, vmsp);

		/* Since the input DVRs correponding to VMSP 0/1 are on adapter 2/3 */
		sprintf(path, "/dev/dvb/adapter%i/dvr0", vmsp);
		if ((dvb->fdout = open(path, O_WRONLY)) <0)
		{
			PERROR ("DVB: failed opening '%s' for write", path);
			return -1;
		}
	}

#endif // !STSDK

	dprintf("%s: out\n", __FUNCTION__);

	return 0;
}

static int dvb_instanceClose (struct dvb_instance * dvb)
{
	int ret;

	dprintf("%s: in\n", __FUNCTION__);

	if( 0 != dvb->fe_tracker_Thread )
		pthread_cancel (dvb->fe_tracker_Thread);
#ifdef ENABLE_TELETEXT
	if(dvb->teletext_thread)
	{
		pthread_cancel (dvb->teletext_thread);
		appControlInfo.teletextInfo.status = teletextStatus_disabled;
		appControlInfo.teletextInfo.exists = 0;
		interfaceInfo.teletext.show = 0;
	}
#endif
	usleep(10000);
	dvb->fe_tracker_Thread = 0;
#ifdef ENABLE_TELETEXT
	dvb->teletext_thread   = 0;
#endif

	if (dvb->fdv >= 0)
	{
		ret = close (dvb->fdv);
		if (ret !=0) eprintf("DVB: video filter closed with error %d\n", ret);
	}

#ifdef ENABLE_MULTI_VIEW
	if (dvb->fdv1 >= 0)
	{
		dprintf("%s: CLOSING dvb->fdv1\n", __FUNCTION__);
		ret = close(dvb->fdv1);
		if (ret != 0)
			eprintf("DVB: video1 filter closed with error %d\n", ret);
	}

	if (dvb->fdv2 >= 0)
	{
		ret = close(dvb->fdv2);
		if (ret != 0)
			eprintf("DVB: video2 filter closed with error %d\n", ret);
	}

	if (dvb->fdv3 >= 0)
	{
		ret = close(dvb->fdv3);
		if (ret != 0)
			eprintf("DVB: video3 filter closed with error %d\n", ret);
	}
#endif
	if (dvb->fda >= 0)
	{
		ret = close (dvb->fda);
		if (ret !=0)
			eprintf("DVB: audio filter closed with error %d\n", ret);
	}

	if (dvb->fdp >= 0)
	{
		ret = close (dvb->fdp);
		if (ret !=0)
			eprintf("DVB: pcr filter closed with error %d\n", ret);
	}

	if (dvb->fdt >= 0)
	{
		ret = close (dvb->fdt);
		if (ret !=0)
			eprintf("DVB: teletext filter closed with error %d\n", ret);
	}

	if (dvb->fdf >= 0)
	{
		ret = close (dvb->fdf);
		if (ret !=0)
			eprintf("DVB: front end closed with error %d\n", ret);
	}

	if (dvb->fdin >= 0)
	{
		ret = close (dvb->fdin);
		if (ret !=0)
			eprintf("DVB: input closed with error %d\n", ret);
	}

	if (dvb->fdout >= 0)
	{
		ret = close (dvb->fdout);
		if (ret !=0)
			eprintf("DVB: output closed with error %d\n", ret);
	}

	dprintf("%s: out\n", __FUNCTION__);

	return 0;
}

/* Thread to track frontend signal status */
static void * dvb_fe_tracker_Thread(void *pArg)
{
	int err;
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;

	do
	{
		sleep(TRACK_INTERVAL); // two sec.
		pthread_testcancel(); // just in case

		if (!appControlInfo.dvbCommonInfo.streamerInput)
		{
			do
			{
				err = ioctl(dvb->fdf, FE_READ_STATUS, &appControlInfo.tunerInfo[dvb->vmsp].fe_status);
				if (err < 0) {
					eprintf("DVB: ioctl FE_READ_STATUS failed ... trying again\n");
				}
			} while (err<0);
			do
			{
				err = ioctl(dvb->fdf, FE_READ_BER, &appControlInfo.tunerInfo[dvb->vmsp].ber);
				if (err < 0) {
					eprintf("DVB: ioctl FE_READ_BER failed ... trying again\n");
				}
			} while (err<0);
			do
			{
				err = ioctl(dvb->fdf, FE_READ_SIGNAL_STRENGTH, &appControlInfo.tunerInfo[dvb->vmsp].signal_strength);
				if (err < 0) {
					eprintf("DVB: ioctl FE_READ_SIGNAL_STRENGTH failed ... trying again\n");
				}
			} while (err<0);
			do
			{
				err = ioctl(dvb->fdf, FE_READ_SNR, &appControlInfo.tunerInfo[dvb->vmsp].snr);
				if (err < 0) {
					eprintf("DVB: ioctl FE_READ_SNR failed ... trying again\n");
				}
			} while (err<0);
			do
			{
				err = ioctl(dvb->fdf, FE_READ_UNCORRECTED_BLOCKS, &appControlInfo.tunerInfo[dvb->vmsp].uncorrected_blocks);
				if (err < 0) {
					eprintf("DVB: ioctl FE_READ_UNCORRECTED_BLOCKS failed ... trying again\n");
				}
			} while (err<0);
		}
	} while (1);
	return NULL;
}

static void dvb_pvrThreadTerm(void* pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;

	dprintf("DVB: Exiting DVB pvr Thread\n");

	dvb->pvr_exit = 1;
}

#define PVR_BUFFER_SIZE (TS_PACKET_SIZE * 100)

/* Thread that deals with asynchronous recording and playback of PVR files */
static void *dvb_pvrThread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	static struct timeval time[2];
	int which = 1;
	gettimeofday(&time[which], NULL);

	pthread_cleanup_push(dvb_pvrThreadTerm, pArg);

	do
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
			if (dvb->fileIndex > dvb->pvr_last_index)
			{
				difference = FILESIZE_THRESHOLD - dvb->pvr_last_position;
				dvb->pvr_last_position = 0;
			}
			difference += (dvb->pvr_position - dvb->pvr_last_position);
			if (difference)
			{
				which = last;
				dvb->pvr_last_position = dvb->pvr_position;
				dvb->pvr_rate += (((difference*1000)/timeGap) - dvb->pvr_rate)/2;
				dprintf("DVB: Time gap : %d ms Difference : %d PVR rate : %d Bps\n",
				        timeGap, difference, dvb->pvr_rate);
			}
		}
	} while (dvb->pvr_exit == 0);

	dprintf("DVB: Exiting dvb_pvrThread\n");
	pthread_cleanup_pop(1);

	return NULL;
}

#ifdef ENABLE_MULTI_VIEW

static void dvb_multiThreadTerm(void* pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;

	dprintf("DVB: Exiting DVB multiview Thread\n");

	dvb->multi_exit = 1;
}

static void *dvb_multiThread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	int length;
	int dvrout;
	struct pollfd pfd[1];
	unsigned char buf[PVR_BUFFER_SIZE];

	usleep(100000); // wait for pipe to be opened by multiview provider
	dvrout = open( "/tmp/dvb.ts", O_WRONLY );
	if( dvrout < 0 )
	{
		eprintf("DVB: dvb_multiThread::Can't open file fo writing\n");
		return NULL;
	}
	pfd[0].fd = dvb->fdin;
	pfd[0].events = POLLIN;
	pthread_cleanup_push(dvb_multiThreadTerm, pArg);
	do
	{
		pthread_testcancel();
		if (poll(pfd,1,1))
		{
			length = -1;
			if (pfd[0].revents & POLLIN)
			{
				length = read(dvb->fdin, buf, PVR_BUFFER_SIZE);
				if (length < 0)
				{
					perror("DVB: dvb_multiThread::Error reading dvr");
				}
				if (length > 0)
				{
					write(dvrout, buf, length);
				}
			}
			if( length < 0 )
			{
				memset(buf, 0xff, TS_PACKET_SIZE);
				write(dvrout, buf, TS_PACKET_SIZE);
			}
		}
		pthread_testcancel();
	} while (dvb->multi_exit == 0);

	dprintf("DVB: Exiting dvb_multiThread\n");
	pthread_cleanup_pop(1);
	return NULL;
}

#endif

#ifdef ENABLE_TELETEXT
static void *dvb_teletextThread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	struct pollfd pfd[1];
	unsigned char buf[PVR_BUFFER_SIZE];

	pfd[0].fd = dvb->fdin;
	pfd[0].events = POLLIN;

	do
	{
		pthread_testcancel();
		if (poll(pfd,1,1))
		{
			if (pfd[0].revents & POLLIN)
			{
				ssize_t length = read(dvb->fdin, buf, TELETEXT_PACKET_BUFFER_SIZE);
				if (length < 0)
				{
					perror("DVB: dvb_teletextThread::Error reading dvr");
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

static void dvb_ThreadTerm(void* pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;

	dprintf("DVB: Exiting DVB Thread\n");
	if (dvb->mode == DvbMode_Record)
	{
		fsync(dvb->fdout);
	}
	if (dvb->mode == DvbMode_Play)
	{
		pthread_cancel (dvb->pvr_thread);
		while(dvb->pvr_exit == 0)
		{
			usleep(10000);
		}
	}
#ifdef ENABLE_MULTI_VIEW
	if (dvb->mode == DvbMode_Multi)
	{
		pthread_cancel (dvb->multi_thread);
		while(dvb->multi_exit == 0)
		{
			usleep(1000);
		}
	}
#endif

	dvb->running = 0;
	dvb->exit = 1;
}

/* Thread that deals with asynchronous recording and playback of PVR files */
static void *dvb_Thread(void *pArg)
{
	struct dvb_instance *dvb = (struct dvb_instance *)pArg;
	int length, write_length;
	char filename[PATH_MAX];

	if (dvb->mode == DvbMode_Play)
	{
		int st;
		st = pthread_create(&dvb->pvr_thread, NULL,  dvb_pvrThread,  dvb);
		if (st == 0)
		{
			pthread_detach(dvb->pvr_thread);
		}
	}

	dprintf("DVB: THREAD!!!\n");


	pthread_cleanup_push(dvb_ThreadTerm, pArg);
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
				sprintf(filename, "%s/part%02d.spts", dvb->directory, dvb->fileIndex+1);
				if ((dvb->fdin = open(filename, O_RDONLY)) >= 0)
				{
					dvb->fileIndex++;
					dvb->pvr_position = 0;
					length = read(dvb->fdin, buffer, PVR_BUFFER_SIZE);
				}
				else
				{
					*dvb->pEndOfFile = 1;
				}
			}
		}

		if (length < 0)
		{
			PERROR("DVB: Read error");
		}
		else
		{
			/* Update the current PVR playback position */
			if (dvb->mode == DvbMode_Record)
			{
				if (dvb->pvr_position > FILESIZE_THRESHOLD)
				{
					close(dvb->fdout);
					sprintf(filename, "%s/part%02d.spts", dvb->directory, dvb->fileIndex+1);
					if ((dvb->fdout = open(filename, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) >= 0)
					{
						dvb->fileIndex++;
						dvb->pvr_position = 0;
					}
				}
			}
			dvb->pvr_position += length;
			pthread_testcancel();
			write_length = write(dvb->fdout, buffer, length);
			if (write_length < 0)
			{
				PERROR("DVB: Write error");
			}
		}
		pthread_testcancel();
	} while ((length > 0) && (write_length > 0) && (dvb->running != 0));

	dprintf("DVB: Exiting dvb_Thread\n");
	pthread_cleanup_pop(1);
	return NULL;
}

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

int dvb_getPIDs(EIT_service_t *service, int audio, int* pVideo, int* pAudio, int* pText, int* pPcr)
{
	list_element_t *stream_element;
	PID_info_t* stream;
	int type;
	if(service == NULL)
	{
		return -1;
	}
	audio = audio < 0 ? 1 : audio + 1;
	mysem_get(dvb_semaphore);
	if( pPcr != NULL )
		*pPcr = service->program_map.map.PCR_PID;
	stream_element = service->program_map.map.streams;
	while( stream_element != NULL)
	{
		stream = (PID_info_t*)stream_element->data;
		type = dvb_getStreamType( stream );
		switch( type )
		{
			case payloadTypeMpeg2:
			case payloadTypeH264:
				if( pVideo != NULL )
					*pVideo = stream->elementary_PID;
				break;
			case payloadTypeMpegAudio:
			case payloadTypeAAC:
			case payloadTypeAC3:
				if( pAudio != NULL )
				{
					audio--;
					if( audio == 0 )
					{
						*pAudio = stream->elementary_PID;
					}
				}
				break;
			case payloadTypeText:
				if( pText != NULL )
				{
					*pText = stream->elementary_PID;
				}
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
	if(service == NULL)
	{
		return -1;
	}
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
		dprintf("DVB: dvb_getServiceURL service is NULL\n");
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
	int ret;
	struct dmx_pes_filter_params pesfilter;
	struct dvb_instance *dvb;
	dvb = &dvbInstances[vmsp];

	//printf("Change aduio PID for vmsp %d to %d\n", vmsp, aPID);

	if (dvb->fda >= 0)
	{
		// Stop demuxing old audio pid
		ret = ioctl(dvb->fda, DMX_STOP);
		if ( ret != 0 )
		{
			eprintf("DVB: ioctl DMX_STOP fda failed\n");
			return ret;
		}
		// reconfigure audio decoder
		// ...
		// reconfigure demux
		pesfilter.input = DMX_IN_DVR;
		pesfilter.output = DMX_OUT_DECODER;
		pesfilter.pes_type = DMX_PES_AUDIO;
		pesfilter.flags = 0;
		pesfilter.pid = aPID;

		ret = ioctl(dvb->fda, DMX_SET_PES_FILTER, &pesfilter);
		if ( ret != 0 )
		{
			eprintf("DVB: ioctl DMX_SET_PES_FILTER fda failed\n");
			return ret;
		}
		// start demuxing new audio pid
		ret = ioctl(dvb->fda, DMX_START);
		if ( ret != 0 )
		{
			eprintf("DVB: ioctl DMX_START fda failed\n");
			return ret;
		}
	}

	return 0;
}

int dvb_startDVB(DvbParam_t *pParam)
{
	dprintf("%s: pParam->vmsp=%d\n", __FUNCTION__, pParam->vmsp);

	if ((pParam->vmsp < 0) || (pParam->vmsp >= VMSP_COUNT))
	{
		eprintf("DVB: Invalid vmsp - use range 0-%d\n", VMSP_COUNT-1);
		return -1;
	}

	struct dvb_instance *dvb;
	
	dvb_filtersFlush();

	lock_filters = 0;

	dvb = &dvbInstances[pParam->vmsp];

	dprintf("%s: dvb_instanceSetMode %s\n", __FUNCTION__, pParam->mode == DvbMode_Play ? "play" : "watch" );

	if (dvb_instanceSetMode(dvb, pParam->mode) != 0)
	{
		eprintf("DVB: Can't set instance defaults\n");
		return -1;
	}
	if (pParam->mode == DvbMode_Play || pParam->frequency > 0)
	{
#ifdef ENABLE_MULTI_VIEW
		if ( dvb->mode == DvbMode_Multi )
		{
			eprintf("%s: Multi %d %d %d %d\n", __FUNCTION__,
			        pParam->param.multiParam.channels[0],
			        pParam->param.multiParam.channels[1],
			        pParam->param.multiParam.channels[2],
			        pParam->param.multiParam.channels[3]);
			dvb->filterv.pid = pParam->param.multiParam.channels[0];
			dvb->filterv1.pid = pParam->param.multiParam.channels[1];
			dvb->filterv2.pid = pParam->param.multiParam.channels[2];
			dvb->filterv3.pid = pParam->param.multiParam.channels[3];
		} else
#endif
		{
			eprintf("%s: Play Video pid %d and Audio pid %d Text pid %d Pcr %d\n", __FUNCTION__,
			        pParam->param.playParam.videoPid,
			        pParam->param.playParam.audioPid,
			        pParam->param.playParam.textPid,
			        pParam->param.playParam.pcrPid);
			dvb->filterv.pid  = pParam->param.playParam.videoPid;
			dvb->filtera.pid  = pParam->param.playParam.audioPid;
			dvb->filtert.pid  = pParam->param.playParam.textPid;
			dvb->filterp.pid  = pParam->param.playParam.pcrPid;
		}
		if( pParam->frequency <= 0 )
		{
			dvb->fileIndex    = pParam->param.playParam.position.index;
			dvb->pvr_position = pParam->param.playParam.position.offset;
			dvb->pEndOfFile   = pParam->param.playParam.pEndOfFile;
			*dvb->pEndOfFile  = 0;
		}
		else
		{
			dvb->setFrequency = pParam->frequency;
		}
	}
	else
	{
		int vpid = 0, apid = 0, tpid = 0, ppid = 0;
		if (dvb_getPIDs(dvb_getService( pParam->param.liveParam.channelIndex ),
			pParam->param.liveParam.audioIndex, &vpid, &apid, &tpid, &ppid) != 0)
		{
			eprintf("DVB: Watch failed to get stream pids for %d service\n",
			        pParam->param.liveParam.channelIndex);
			return 0;
		}
		dvb->filterv.pid = vpid;
		dvb->filtera.pid = apid;
		dvb->filtert.pid = tpid;
		dvb->filterp.pid = ppid;
		eprintf("DVB: Watch Video pid %hu Audio pid %hu Text pid %hu Pcr %hu\n",
		        dvb->filterv.pid, dvb->filtera.pid, dvb->filtert.pid, dvb->filterp.pid);
#ifndef ENABLE_PVR
		if (dvb_getServiceFrequency( dvb_getService( pParam->param.liveParam.channelIndex ),
		                             &dvb->setFrequency) != 0)
#else
		if( (s32)pParam->frequency < 0 )
		{
			dvb->setFrequency = -1;
		} else
		if (dvb_getServiceFrequency( dvb_getService( pParam->param.liveParam.channelIndex ),
		                             &dvb->setFrequency) != 0)
#endif
		{
			eprintf("DVB: Watch failed to get frequency for %d service\n",
			        pParam->param.liveParam.channelIndex);
			return 0;
		}
	}

	if (open_dvb_instance(dvb, pParam->vmsp, pParam->directory) != 0)
	{
		eprintf("DVB: Failed to open dvb instance\n");
		return -1;
	}
	/*if (pParam->mode != DvbMode_Play && pParam->frequency <= 0)
	{
		if (dvb_getScrambled(dvb_getService( pParam->param.liveParam.channelIndex )))
		{
			eprintf("DVB: Channel %d scrambled - not starting!\n", pParam->param.liveParam.channelIndex);
			return 0;
		}
	}*/

	dprintf("%s: dvb_serviceChange\n", __FUNCTION__);

	if (dvb_serviceChange(dvb, pParam->media) != 0)
	{
		eprintf("DVB: Failed to change service!\n");
		return 0;
	}

	int st;
	pthread_attr_t attr;
	struct sched_param param = { 90 };

	st = pthread_attr_init(&attr);
	if (st==-1)
	{
		eprintf("DVB: Error during pthread_attr_init (%d)\n", st);
	}
	st = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	if (st==-1)
	{
		eprintf("DVB: Error during pthread_attr_setschedpolicy (%d)\n", st);
	}
	st = pthread_attr_setschedparam(&attr, &param);
	if (st==-1)
	{
		eprintf("DVB: Error during pthread_attr_setschedparam (%d)\n", st);
	}

	if (dvb->mode != DvbMode_Watch
#ifdef ENABLE_MULTI_VIEW
		&& dvb->mode != DvbMode_Multi
#endif
	)
	{
		dprintf("%s: !DvbMode_Watch\n", __FUNCTION__);

		st = pthread_create (&dvb->thread, &attr, dvb_Thread, dvb);
		if (st != 0)
		{
			eprintf("DVB: error %d starting dvb_Thread", st);
			return 1;
		}
		pthread_detach(dvb->thread);
	}
#ifdef ENABLE_PVR
	if ( (dvb->mode == DvbMode_Watch
#ifdef ENABLE_MULTI_VIEW
		|| dvb->mode == DvbMode_Multi
#endif
		|| dvb->mode == DvbMode_Record) && dvb->fdf > 0 )
#else
	else
#endif
	{
		dprintf("DVB: DvbMode_Watch\n");
		if (!dvb->fe_tracker_Thread)
		{
			st = pthread_create(&dvb->fe_tracker_Thread, &attr,  dvb_fe_tracker_Thread,  dvb);
			if (st != 0)
			{
				eprintf("DVB: pthread_create (fe track) err=%d, Adp-%d", st, dvb->vmsp);
				return 1;
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
					eprintf("DVB: pthread_create (teletext) err=%d, Adp-%d", st, dvb->vmsp);
					return 1;
				}
				pthread_detach(dvb->teletext_thread);
			}
		}
#endif /* ENABLE_TELETEXT */
#ifdef ENABLE_MULTI_VIEW
		if( dvb->mode == DvbMode_Multi )
		{
			st = pthread_create(&dvb->multi_thread, &attr,  dvb_multiThread,  dvb);
			if (st != 0)
			{
				eprintf("DVB: pthread_create (multiview) err=%d, Adp-%d", st, dvb->vmsp);
				return 1;
			}
			pthread_detach(dvb->multi_thread);
		}
#endif
	}
	//dprintf("%s: done start\n", __FUNCTION__);

	return 0;
}

int dvb_stopDVB(int vmsp, int reset)
{
	int retval = -1;

	dprintf("%s: in vmsp %d reset %d\n", __FUNCTION__, vmsp, reset);

	if (vmsp >= 0 && vmsp < VMSP_COUNT)
	{
		struct dvb_instance *dvb = &dvbInstances[vmsp];

		if (dvb->thread != 0)
		{
			pthread_cancel (dvb->thread);
			while(dvb->exit != 1)
			{
				usleep(10000);
			}
		}
#ifdef ENABLE_MULTI_VIEW
		if (dvb->multi_thread != 0)
		{
			pthread_cancel (dvb->multi_thread);
			while(dvb->multi_exit != 1)
			{
				usleep(10000);
			}
		}
#endif

		dvb_filtersFlush();

		retval = dvb_instanceClose(dvb);

		dvb_instanceInit(dvb);
		if (reset)
		{
			/* Reset the stored frequency as the driver    */
			/* puts the tuner into standby so it needs to  */
			/* be woken up when we next do a set frequency */
			currentFrequency[vmsp] = 0;
		}
	}

	dprintf("%s: out %d\n", __FUNCTION__, retval);

	return retval;
}

void dvb_init(void)
{
	int i;

	dprintf("%s: in\n", __FUNCTION__);

	/* Got through the possible tuners */
	for ( i=0; i<inputTuners; i++ )
	{
		int fdf;
		struct dvb_frontend_info fe_info;
		int err;

		if ((fdf = dvb_openFrontend(i, O_RDONLY)) < 0) {
			appControlInfo.tunerInfo[i].status = tunerNotPresent;
			continue;
		}

		appControlInfo.tunerInfo[i].status = tunerInactive;
		do
		{
			err = ioctl(fdf, FE_GET_INFO, &fe_info);
			if (err < 0) {
				info("ioctl FE_GET_INFO failed ... trying again\n");
			}
		} while (err<0);
		close(fdf);
		gFE_type = fe_info.type; // Assume all FEs are the same type !

		char *type_str = "unknown";
		switch (fe_info.type)
		{
			case FE_QPSK: type_str = "DVB-S"; break;
			case FE_QAM:  type_str = "DVB-C"; break;
			case FE_OFDM: type_str = "DVB-T"; break;
			case FE_ATSC: type_str = "ATSC";  break;
		}
		eprintf("%s[%d]: %s (%s)\n", __FUNCTION__, i, fe_info.name, type_str);
	}

	mysem_create(&dvb_semaphore);
	mysem_create(&dvb_filter_semaphore);
	mysem_create(&dvb_fe_semaphore);

	for(i=0; i<VMSP_COUNT; i++)
	{
		dvb_instanceInit(&dvbInstances[i]);
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
	{
		dvb_stopDVB(i, 1);
	}

	free_services(&dvb_services);

	mysem_destroy(dvb_semaphore);
	mysem_destroy(dvb_filter_semaphore);
	mysem_destroy(dvb_fe_semaphore);
}

static int dvb_get_numPartPvrFiles(struct dvb_instance *dvb, int low, int high)
{
	char filename[PATH_MAX];
	if ((high - low) < 2)
	{
		return low;
	}
	sprintf(filename, "%s/part%02d.spts", dvb->directory, (low+high)/2);
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
	sprintf(filename, "%s/part%02d.spts", dvb->directory, file);

	if ((filePtr = open(filename, O_RDONLY)) <0)
	{
		return 0;
	}

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
		pPosition->offset = dvb->pvr_position;
	}
}

int dvb_getPvrRate(int which)
{
	if (which >= 0 && which < 2)
	{
		struct dvb_instance *dvb = &dvbInstances[which+2];
		return dvb->pvr_rate;
	}
	return DEFAULT_PVR_RATE;
}

#endif /* ENABLE_DVB */
