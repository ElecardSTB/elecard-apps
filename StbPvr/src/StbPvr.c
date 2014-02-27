
/*

Elecard STB820 Demo Application
Copyright (C) 2007  Elecard Devices

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA

*/

/***********************************************
* INCLUDE FILES*
************************************************/

/* StbMainApp */
#include <defines.h>
#include <dvb_types.h>

#include "StbPvr.h"

/* NETLib */
#include <platform.h>
#include <smrtp.h>
#include <sdp.h>
#include <tools.h>
#include <service.h>
/* Linux DVB API */
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
/* HTTP support */
#include <curl/curl.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <libgen.h>
#include <signal.h>
#include <limits.h>
#include <time.h>
#include "cJSON.h"
#include <elcd-rpc.h>

/***********************************************
* LOCAL MACROS *
************************************************/

#define APP_NAME "StbPvr"

#define INFO(x...) \
do {\
	time_t ts; \
	struct tm *tsm; \
	ts = time(NULL); \
	tsm = localtime(&ts); \
	printf("%s %02d:%02d:%02d: ", APP_NAME, tsm->tm_hour, tsm->tm_min, tsm->tm_sec); \
	printf(x); \
} while (0)

#define ERROR(x...) \
do {\
	time_t ts; \
	struct tm *tsm; \
	ts = time(NULL); \
	tsm = localtime(&ts); \
	fprintf(stderr, "%s %02d:%02d:%02d: ", APP_NAME, tsm->tm_hour, tsm->tm_min, tsm->tm_sec); \
	fprintf(stderr, x); \
	fprintf(stderr, "\n"); \
} while (0)

#define PERROR(x...)\
do {\
	time_t ts; \
	struct tm *tsm; \
	ts = time(NULL); \
	tsm = localtime(&ts); \
	fprintf(stderr, "%s %02d:%02d:%02d: ", APP_NAME, tsm->tm_hour, tsm->tm_min, tsm->tm_sec); \
	fprintf(stderr, x); \
	fprintf(stderr, " (%s)\n", strerror(errno));   \
} while (0)

//#define MAX_PATH_LENGTH 80

#define HTTP_CONNECT_TIMEOUT (10)

#define MAX_URL (PATH_MAX+7)

#define LIST_SIZE(x) sizeof(x)/sizeof(Param)

#define BUFFER_SIZE 1024

// Each 0.3 seconds
#define PATPMT_PERIOD 300000

#define FILE_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
#define PVR_BUFFER_SIZE (TS_PACKET_SIZE * 100)
#define PVR_CHUNK_SIZE  (TS_PACKET_SIZE * 7)
#define PVR_WRITE_COUNT (3)

#define MAX_CLIENTS	4

/******************************************************************
* LOCAL TYPEDEFS  *
*******************************************************************/

typedef enum
{
	dvbRecord_free = 0,
	dvbRecord_active,
	dvbRecord_closed,
    dvbRecord_error,
} dvb_status_rec;

typedef enum
{
	dvbRecord_start = 0,
	dvbRecord_stop,
} dvb_cmd;

typedef struct
{
	int               inversion;
	long              bandwidth;
} stb810_dvbtInfo;

typedef struct
{
	int               inversion;
	long              modulation;
	long              symbolRate;
} stb810_dvbcInfo;

typedef struct
{
	stb810_dvbtInfo   dvbtInfo;
	stb810_dvbcInfo   dvbcInfo;
	int               dvbAdapterSpeed;
} stb810_dvbInfo;

typedef struct {
	int               fd;
	char              directory[PATH_MAX];
	int               part;
	off_t             position;
} fileRecordInfo_t;

// defines one chain of devices which together make up a DVB receiver/playter
typedef struct {
	int               channel;

	// file descriptors for frontend, demux-video, demux-audio
	int               fdf;
	int               fdv;
	int               fda;
	int               fdp;

	// device setup data
	struct dmx_pes_filter_params filterv;
	struct dmx_pes_filter_params filtera;
	struct dmx_pes_filter_params filterp;
	struct dvb_frontend_parameters tuner;

	int               vmsp;
	fe_type_t         fe_type;
	//fe_delivery_system_t	fe_type;

	int               fdin;
	int               fdplay;
	int               play_only;
	fileRecordInfo_t  out;
	pthread_t         thread;

	stb810_dvbInfo    info;
	long              currentFrequency;

	int               running;
} dvbRecordInfo_t;

typedef struct {
	media_desc        desc;
	struct in_addr    ip;
	struct smrtp_session_t *RTPSession;

	fileRecordInfo_t  out;
} rtpRecordInfo_t;

typedef struct {
	char title[SDP_FIELD_LENGTH];
	char url[MAX_URL];
	char proxy[32];
	char login[512];

	volatile int need_stop;
	pthread_t thread;
	CURL *curl;
	fileRecordInfo_t  out;
} httpRecordInfo_t;

typedef struct
{
	dvbRecordInfo_t   dvb;
	rtpRecordInfo_t   rtp;
	httpRecordInfo_t  http;
	char              path[PATH_MAX];
	list_element_t   *current_job;
	time_t            current_job_end; /**< running job end time (needed when job is deleted from outside when already started) */
} pvrInfo_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES  <Module>_<Word>+*
*******************************************************************/

static EIT_service_t* dvb_getService(int which);
static payload_type   dvb_getStreamType( PID_info_t* stream );
static int   dvb_getPIDs(EIT_service_t *service, int audio, int *videoType, int *audioType, int* pVideo, int* pAudio, int* pPcr);
static char *dvb_getTempServiceName(int index);
static int   dvb_setTuner(dvbRecordInfo_t *dvb, long frequency);

static int   dvb_instance_set_defaults (dvbRecordInfo_t * dvb);
static int   dvb_instance_open (dvbRecordInfo_t * dvb);
static int   dvb_instance_setup (dvbRecordInfo_t * dvb);
static int   dvb_instance_close (dvbRecordInfo_t * dvb);
static int   dvb_recording_stop(pvrInfo_t *pvr);
static int   dvb_recording_start(pvrInfo_t *pvr, int channel, EIT_service_t *service);
static void *dvb_recording_thread(void *pArg);
static int   dvb_write_status(pvrInfo_t *pvr);

static int   rtp_recording_start(pvrInfo_t *pvr, media_desc *desc, struct in_addr *ip, char *channelName);
static int   rtp_recording_stop(pvrInfo_t *pvr);
static unsigned long rtp_recv_callback(void *arg, const unsigned char *buffer, unsigned long numbytes);
static int   rtp_write_status(pvrInfo_t *pvr);

static int   http_recording_stop(pvrInfo_t *pvr);
static int   http_recording_start(pvrInfo_t *pvr, const char *url, const char *channelName);
static int   http_write_status(pvrInfo_t *pvr);

static int   pvr_recording_stop(pvrInfo_t *pvr);

static int   pvr_importJobList(void);
static int   pvr_clearJobList(void);
static void  pvr_freeJob(list_element_t* job_element);
static int   pvr_deleteJob(list_element_t* job);
static void  pvr_cancelCurrentJob(pvrInfo_t *pvr);

static inline void pvr_write_status(pvrInfo_t *pvr);
static void* pvr_socket_thread(void *arg);
static int   write_chunk (char * buf, int out_len);
static int   read_chunk (char * buf, int len);

/******************************************************************
* STATIC DATA  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static volatile int     exit_app = 0;
static volatile int     update_required = 1;
static time_t           notifyTimeout = 10;
static dvb_status_rec   dvb_status_rec_t = 0;

static struct clientSockets
{
	int count;
	int sockets[MAX_CLIENTS];
} clients;

/**************************************************************************
* EXPORTED DATA  g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

list_element_t *dvb_services = NULL;
list_element_t *pvr_jobs = NULL;

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions *
*  tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

static int mkdirs( char *path )
{
	int res = -1;
	char *dir = path;

	if( path[0] == 0 )
		return res;
	do
	{
		dir = index( dir+1, '/' );
		if( dir != NULL )
			*dir = 0;

		res = mkdir( path, S_IRWXU | S_IRWXG | S_IRWXO );
		if( res == -1 )
		{
			if (errno != EEXIST )
			{
				PERROR("Failed to create '%s'\n", path);
				return res;
			}
			else
				res = 0;
		}
		if( dir != NULL )
			*dir = '/';
	} while( dir != NULL );

	return res;
}

static char *helperStrCpyTrimSystem(char *dst, const char *src)
{
	char *ptr = dst;
	while( *src )
	{
		if( (unsigned char)(*src) > 127 )
		{
			*ptr++ = tolower(*src);
		} else if( *src >= ' ' )
			switch(*src)
			{
				case '"': case '*': case '/': case ':': case '|':
				case '<': case '>': case '?': case '\\': case 127: break;
				default: *ptr++ = tolower(*src);
			}
		src++;
	}
	*ptr = 0;
	/* Trim ending spaces and dots */
	for( ptr = ptr-1; ptr >= dst && (*ptr == ' ' || *ptr == '.'); ptr-- )
		*ptr = 0;
	return dst;
}

/** Trim following spaces */
static char *helperStrTrim( char *value )
{
	char *str;
	for( str = &value[strlen(value)-1];
	     str >= value && *str > 0 && *str <= ' ';
	     str-- )
		*str = 0;
	return value;
}

/*
   clear and inits the given instance.
*/
static int dvb_instance_set_defaults (dvbRecordInfo_t * dvb)
{
	//memset( dvb, 0, sizeof(dvbRecordInfo_t));

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

	return 0;
}

/*
   create a dvb player instance by chaning the basic devices.
   Uses given device setup data (params) & device paths.
*/
static int dvb_instance_setup (dvbRecordInfo_t * dvb)
{
	int ret;

	INFO("%s: f=%ld\n", __FUNCTION__, (long int)dvb->tuner.frequency);

#ifndef NO_FRONTEND
	ret = ioctl(dvb->fdf, FE_SET_FRONTEND, &dvb->tuner);
	if (ret < 0)
	{
		PERROR("ioctl FE_SET_FRONTEND failed");
		return ret;
	}
	usleep(500000);
#endif

	//INFO("(dvb_instance_setup: filterv pid=%d filtera pid=%d filterp pid=%d)\n",dvb->filterv.pid,dvb->filtera.pid,dvb->filterp.pid);

	ret = ioctl(dvb->fdv, DMX_SET_PES_FILTER, &dvb->filterv);
	if (ret != 0)
	{
		PERROR("ioctl DMX_SET_PES_FILTER fdv failed");
		return ret;
	}

	ret = ioctl(dvb->fda, DMX_SET_PES_FILTER, &dvb->filtera);
	if (ret != 0)
	{
		PERROR("ioctl DMX_SET_PES_FILTER fda failed");
		return ret;
	}

	ret = ioctl(dvb->fdp, DMX_SET_PES_FILTER, &dvb->filterp);
	if (ret != 0)
	{
		PERROR("ioctl DMX_SET_PES_FILTER fdp failed");
		return ret;
	}

	ret = ioctl(dvb->fdv, DMX_START);
	if (ret != 0)
	{
		PERROR("ioctl DMX_START fdv failed");
		return ret;
	}

	ret = ioctl(dvb->fda, DMX_START);
	if (ret != 0)
	{
		PERROR("ioctl DMX_START fda failed");
		return ret;
	}

	ret = ioctl(dvb->fdp, DMX_START);
	if (ret != 0)
	{
		PERROR("ioctl DMX_START fdp failed");
		return ret;
	}

	return ret;
}
/*
   create a dvb player instance by chaining the basic devices.
   Uses given device setup data (params) & device paths.
*/
static int dvb_instance_open (dvbRecordInfo_t * dvb)
{
	char path[MAX_PATH_LENGTH];
	int error;

#ifndef NO_FRONTEND
	struct dvb_frontend_info fe_info;

	sprintf(path, "/dev/dvb/adapter%i/frontend0", dvb->vmsp);
	if ((dvb->fdf = open(path, O_RDWR)) < 0) {
		PERROR ("failed opening '%s'", path );

		return -1;
	}

	do
	{
		error = ioctl(dvb->fdf, FE_GET_INFO, &fe_info);
		if (error < 0) {
			INFO("ioctl FE_GET_INFO failed ... trying again\n");
		}
	} while (error!=0);

	if (fe_info.type != FE_OFDM) {
		ERROR ("frontend device is not a OFDM (DVB-T) device");
		//return -1;
	}

	dvb->fe_type = fe_info.type; //dvb_typeConversionForwardOld(fe_info.type);
#endif

	sprintf(path, "/dev/dvb/adapter%i/demux0", dvb->vmsp);
	if ((dvb->fdv = open(path, O_RDWR)) <0)
	{
		PERROR ("failed opening '%s' for video", path );
		return -1;
	}

	if ((dvb->fda = open(path, O_RDWR)) <0)
	{
		PERROR ("failed opening '%s' for audio", path );
		return -1;
	}

	if ((dvb->fdp = open(path, O_RDWR)) <0)
	{
		PERROR ("failed opening '%s' for pcr", path );
		return -1;
	}

	
	sprintf(path, "/dev/dvb/adapter%i/dvr0", dvb->vmsp);
	if ((dvb->fdin = open(path, O_RDONLY)) <0)
	{
		PERROR ("failed opening '%s' for read", path);
		return -1;
	}

	dvb->out.position  = 0;
	dvb->out.part = 1;

	if (!dvb->play_only)
	{
		sprintf(path, "%s/part%02d.ts", dvb->out.directory, dvb->out.part);
		if ((dvb->out.fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) <0)
		{
			PERROR ("failed opening '%s' for write", path);
			return -1;
		}
	}

	return 0;
}

static int dvb_instance_close (dvbRecordInfo_t * dvb)
{
	int ret;

#define CLOSE_FD(fd, msg) \
do { \
	if (fd > 0) { \
		ret = close(fd); \
		if (ret !=0) PERROR("%s closed with error %d", msg, ret); \
		fd = -1; \
	} \
} while(0)

	CLOSE_FD(dvb->fdv, "video filter");
	CLOSE_FD(dvb->fda, "audio filter");
	CLOSE_FD(dvb->fdp, "pcr filter");
#ifndef NO_FRONTEND
	CLOSE_FD(dvb->fdf, "frontend");
#endif
	CLOSE_FD(dvb->fdin,"input");
	CLOSE_FD(dvb->out.fd, "output");
	CLOSE_FD(dvb->fdplay, "pipe");
	dvb->play_only = 0;

#undef CLOSE_FD
	return 0;
}

static int dvb_setTuner(dvbRecordInfo_t *dvb, long frequency)
{
	INFO("Current frequency %ld, new frequency %ld\n", dvb->currentFrequency, frequency);

	if (dvb->currentFrequency == frequency)
	{
		fe_status_t s;
		__u32 ber = BER_THRESHOLD;

		if (ioctl(dvb->fdf, FE_READ_STATUS, &s) == -1)
		{
			PERROR("FE_READ_STATUS failed");
			return -1;
		}
		if (ioctl(dvb->fdf, FE_READ_BER, &ber) == -1)
		{
			PERROR("FE_READ_BER failed");
			return -1;
		}

		INFO("Check lock = %d, check ber = %lu\n", s & FE_HAS_LOCK, (long unsigned) ber);

		if ((s & FE_HAS_LOCK) == 0 || ber >= BER_THRESHOLD)
		{
			ERROR( "Force retune to %ld", frequency);
			dvb->currentFrequency = 0;
		}
	}
	if ((dvb->fe_type == FE_OFDM/*SYS_DVBT*/))
	{
		INFO("fe_type=FE_OFDM\n");
		//INFO("fe_type=DVBT/DVBT2\n");
		dvb->tuner.u.ofdm.bandwidth = dvb->info.dvbtInfo.bandwidth;
		dvb->tuner.u.ofdm.code_rate_HP = FEC_AUTO;
		dvb->tuner.u.ofdm.code_rate_LP = FEC_AUTO;
		dvb->tuner.u.ofdm.constellation = QAM_AUTO;
		dvb->tuner.u.ofdm.hierarchy_information = HIERARCHY_AUTO;
		dvb->tuner.u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
		dvb->tuner.u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
		dvb->tuner.inversion = dvb->info.dvbtInfo.inversion;
	}
	else if (dvb->fe_type == FE_QAM/*SYS_DVBC_ANNEX_AC*/)
	{
		INFO("fe_type=FE_QAM\n");
		//INFO("fe_type=SYS_DVBC_ANNEX_AC\n");
		dvb->tuner.u.qam.modulation  = dvb->info.dvbcInfo.modulation;
		dvb->tuner.u.qam.symbol_rate = dvb->info.dvbcInfo.symbolRate*1000;
		dvb->tuner.u.qam.fec_inner   = FEC_NONE;
		dvb->tuner.inversion = dvb->info.dvbcInfo.inversion;
	}
	else
	{
		ERROR( "Unsupported frontend type");
		return -1;
	}

	dvb->tuner.frequency = frequency;

	return 0;
}

static EIT_service_t* dvb_getService(int which)
{
	list_element_t *service_element;
	for( service_element = dvb_services; service_element != NULL; service_element = service_element->next )
	{
		if(which == 0)
		{
			return (EIT_service_t*)service_element->data;
		}
		which--;
	}
	return NULL;
}

static char *dvb_getTempServiceName(int index)
{
	list_element_t *service_element;
	EIT_service_t *service;
	int i = 0;

	service_element = dvb_services;
	while( service_element != NULL )
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
		service_element = service_element->next;
	}
	return "";
}

static payload_type dvb_getStreamType( PID_info_t* stream )
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
		default:
			return payloadTypeUnknown;
	}
}

int dvb_hasMediaType(EIT_service_t *service)
{
	list_element_t *stream_element;

	for (stream_element = service->program_map.map.streams;
	     stream_element != NULL;
	     stream_element = stream_element->next)
	{
		if (dvb_getStreamType((PID_info_t*)stream_element->data) != payloadTypeUnknown)
			return 1;
	}

	return 0;
}

static int dvb_getPIDs(EIT_service_t *service, int audio, int *videoType, int *audioType, int* pVideo, int* pAudio, int* pPcr)
{
	list_element_t *stream_element;
	PID_info_t* stream;
	int type;
	if(service == NULL)
	{
		return -1;
	}
	audio = audio < 0 ? 1 : audio + 1;
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
				if(videoType != NULL)
					*videoType = type == payloadTypeH264 ? streamTypeVideoH264 : streamTypeVideoMPEG2;
				*pVideo = stream->elementary_PID;
				break;
			case payloadTypeMpegAudio:
			case payloadTypeAAC:
			case payloadTypeAC3:
				audio--;
				if( audio == 0 )
				{
					*pAudio = stream->elementary_PID;
					if( audioType != NULL )
					{
						switch( type )
						{
							case payloadTypeMpegAudio: *audioType = streamTypeAudioMPEG1; break;
							case payloadTypeAAC:       *audioType = streamTypeAudioAAC; break;
							case payloadTypeAC3:       *audioType = streamTypeAudioAC3; break;
						}
					}
				}
				break;
				default: ;
		}
		stream_element = stream_element->next;
	}
	return 0;
}

long dvb_getFrequency(EIT_service_t *service)
{
	if(service == NULL)
	{
		return 0;
	}
	return service->common.media_id;
}

static int dvb_recording_stop(pvrInfo_t *pvr)
{
	if( pvr->dvb.channel != STBPVR_DVB_CHANNEL_NONE )
	{
		INFO("Stopping DVB recording\n");

		pvr->current_job_end = 0;
		if (pvr->dvb.thread != 0)
		{
			pthread_cancel (pvr->dvb.thread);
			pthread_join   (pvr->dvb.thread, NULL);
			pvr->dvb.thread = 0;
		}
		pvr->dvb.channel = STBPVR_DVB_CHANNEL_NONE;

		dvb_instance_close(&pvr->dvb);

		if( pvr->current_job && ((pvrJob_t*)(pvr->current_job->data))->type == pvrJobTypeDVB )
			pvr_cancelCurrentJob(pvr);
	}
	dvb_write_status(pvr);
	return 0;
}

static int dvb_recording_start(pvrInfo_t *pvr, int channel, EIT_service_t *service)
{
	dvbRecordInfo_t *dvb = &pvr->dvb;
	int vpid = 0, apid = 0, pcr = 0;

	if (!dvb->play_only)
	{

	char *str;
	//int infoFile;
	time_t rawtime;
	struct tm *t;
	struct stat stat_info;

	if (stat( pvr->path, &stat_info ) < 0 || (stat_info.st_mode & S_IFDIR) != S_IFDIR)
	{
		if (dvb->fdplay > 0)
		{
			close(dvb->fdplay);
			dvb->fdplay = -1;
			dvb->play_only = 0;
		}
		write_chunk( "ed", 3 );
		ERROR("Record path '%s' not accessible", pvr->path);
		return -1;
	}

	sprintf( dvb->out.directory, "d%c%d", dvb->fdplay > 0 ? 'p' : 'r', channel);
	write_chunk( dvb->out.directory, strlen(dvb->out.directory)+1 );

	time( &rawtime );
	t = localtime(&rawtime);

	sprintf(dvb->out.directory, "%s" STBPVR_FOLDER "/", pvr->path);
	str = &dvb->out.directory[strlen(dvb->out.directory)];
	if (service != NULL)
	{
		size_t name_length;
		helperStrCpyTrimSystem( str, (char*)service->service_descriptor.service_name );
		name_length = strlen(str);
		if (name_length > 0)
		{
			str += name_length;
			*str = '/';
			str++;
		}
	}
	strftime(str, 20, "%Y-%m-%d/%H-%M", t);
	INFO("Recording to '%s'\n", dvb->out.directory);

	mkdirs(dvb->out.directory);
	} // !dvb->play_only

	INFO( "Starting recording\n" );

	if (0 == dvb_instance_set_defaults(dvb) &&
	    0 == dvb_instance_open(dvb) &&
	    0 == dvb_getPIDs( service, 0, NULL, NULL, &vpid, &apid, &pcr))
	{
		dvb_setTuner(dvb, dvb_getFrequency(service));

		dvb->filterv.pid = vpid;
		dvb->filtera.pid = apid;
		dvb->filterp.pid = pcr;

		/* Set up the PVR info filename *//*
		sprintf(mkdirString, "%s/info.pvr", dvb->out.directory);
		infoFile = open(mkdirString, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS);

		if (infoFile >= 0)
		{
			char outputString[256];
			sprintf(outputString, "VideoPid %04X\n", vpid);
			write(infoFile, outputString, strlen(outputString));
			sprintf(outputString, "AudioPid %04X\n", apid);
			write(infoFile, outputString, strlen(outputString));
			sprintf(outputString, "PcrPid %04X\n", pcr);
			write(infoFile, outputString, strlen(outputString));
			*//*
			if (dvb_hasAC3Audio(offair_getService(appControlInfo.pvrInfo.recordInfo[which].channel)) )
			{
				sprintf(outputString, "VideoType :MPEG2\n");
				write(infoFile, outputString, strlen(outputString));
				sprintf(outputString, "AudioType :AC3\n");
				write(infoFile, outputString, strlen(outputString));
			}
			*//*
			close(infoFile);
		}
		*/
		if (dvb_instance_setup(dvb) == 0)
		{
			pthread_attr_t attr;
			struct sched_param param = { 90 };
			int st;

			ERROR( "DVB configured for Recording" );

			st = pthread_attr_init(&attr);
			if (st==-1)
			{
				ERROR( "Error during pthread_attr_init (%d)", st);
			}
			st = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
			if (st==-1)
			{
				ERROR("Error during pthread_attr_setschedpolicy (%d)", st);
			}
			st = pthread_attr_setschedparam(&attr, &param);
			if (st==-1)
			{
				ERROR("Error during pthread_attr_setschedparam (%d)", st);
			}

			dvb->channel = channel;
			st = pthread_create (&dvb->thread, &attr, dvb_recording_thread, dvb);
			if (st != 0)
			{
				ERROR("Can't create DVB record thread: Error %d", st);
				dvb->channel = STBPVR_DVB_CHANNEL_NONE;
			} else
			{
				dvb_write_status(pvr);
				return 0;
			}
		} else
		{
			ERROR("Can't configure DVB for recording");
		}
	} else
	{
		ERROR("Can't open DVB instance");
	}
	write_chunk( "e", 2 );

	dvb_instance_close(dvb);

	return 1;
}

static int rtp_recording_stop(pvrInfo_t *pvr)
{
	if( pvr->rtp.desc.fmt != payloadTypeUnknown )
	{
		pvr->current_job_end = 0;

		small_rtp_stop(pvr->rtp.RTPSession);
		small_rtp_destroy(pvr->rtp.RTPSession);
		close(pvr->rtp.out.fd);

		pvr->rtp.desc.fmt = payloadTypeUnknown;

		if( pvr->current_job && ((pvrJob_t*)(pvr->current_job->data))->type == pvrJobTypeRTP )
			pvr_cancelCurrentJob(pvr);

		rtp_write_status(pvr);
	}
    return 0;
}

static unsigned long rtp_recv_callback(void *arg, const unsigned char *buffer, unsigned long numbytes)
{
	pvrInfo_t *pvr = (pvrInfo_t *)arg;
	rtpRecordInfo_t * rtp = &pvr->rtp;
	static char filename[PATH_MAX];
	int res = 0;

	if (rtp->out.fd <= 0)
		return 0;

    if (rtp->out.position + numbytes > FILESIZE_THRESHOLD)
	{
		close(rtp->out.fd);
		sprintf(filename, "%s/part%02d.ts", rtp->out.directory, rtp->out.part+1);
		if ((rtp->out.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) >= 0)
		{
			rtp->out.part++;
			rtp->out.position = 0;
		} else
		{
			PERROR("Failed to open '%s' for writing", filename);
			goto rtp_record_failed;
		}
	}

	res = write(rtp->out.fd, buffer, numbytes);
	if (res < 0)
	{
		close(pvr->rtp.out.fd);
		pvr->rtp.out.fd = -1;
		PERROR("Write error");
		switch (errno)
		{
			case ENOSPC: write_chunk("es", 3); break;
			default:     write_chunk("e", 2);
		}
		
		//small_rtp_destroy(pvr->rtp.RTPSession);

		//pvr->rtp.desc.fmt = payloadTypeUnknown;
		goto rtp_record_failed;
	} else
		rtp->out.position += res;
	//INFO("%s: written %d\n", __FUNCTION__, res);
	return res;

rtp_record_failed:
	pvr->current_job_end = 0;
	if (pvr->current_job && ((pvrJob_t*)(pvr->current_job->data))->type == pvrJobTypeRTP)
		pvr_cancelCurrentJob(pvr);

	return 0;
}

static int rtp_recording_start(pvrInfo_t *pvr, media_desc *desc, struct in_addr *ip, char *channelName)
{
	rtpRecordInfo_t *rtp = &pvr->rtp;
	char *str, filename[PATH_MAX];
	time_t rawtime;
	int st;
	struct tm *t;
	struct stat stat_info;

	memcpy( &rtp->desc, desc, sizeof(media_desc) );
	memcpy( &rtp->ip,   ip,   sizeof(struct in_addr) );

	INFO( "Recording %s://%s:%d (%s)\n", proto_toa(rtp->desc.proto), inet_ntoa(rtp->ip), rtp->desc.port, channelName);

	//sprintf( mkdirString, "ur%s://%s:%d", proto_toa(rtp->desc.proto), inet_ntoa(rtp->ip), rtp->desc.port);
	//write_chunk( mkdirString, strlen(mkdirString)+1 );

	st = stat( pvr->path, &stat_info );
	if( st < 0 || (stat_info.st_mode & S_IFDIR) != S_IFDIR )
	{
		PERROR("Can't access output path");
		write_chunk( "ed", 3 );
		goto failure;
	}

    if (small_rtp_init(&rtp->RTPSession, inet_ntoa(rtp->ip), rtp->desc.port, rtp->desc.proto, payloadTypeMpegTS, 0, 0) != 0)
	{
		ERROR("small_rtp_init failed");
		write_chunk( "e", 2 );
		goto failure;
	}

	sprintf(rtp->out.directory, "%s" STBPVR_FOLDER "/", pvr->path);
	str = &rtp->out.directory[strlen(rtp->out.directory)];
	if( channelName != NULL && channelName[0] != 0 )
	{
		size_t name_length;
		helperStrCpyTrimSystem( str, channelName );
		name_length = strlen(str);
		if( name_length > 0 )
		{
			str += name_length;
			*str = '/';
			str++;
		}
	}
	else
		sprintf( str, "%s-%s-%d", proto_toa(rtp->desc.proto), inet_ntoa(rtp->ip), rtp->desc.port);
	time( &rawtime );
	t = localtime(&rawtime);
	strftime(str, 20, "%Y-%m-%d/%H-%M", t);
	mkdirs(rtp->out.directory);

	INFO("Recording RTP to '%s'\n", rtp->out.directory);
	rtp->out.part = 1;
	rtp->out.position = 0;
	sprintf(filename, "%s/part%02d.ts", rtp->out.directory, rtp->out.part);
	if ((rtp->out.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) < 0)
	{
		PERROR("Output file create failed");
		write_chunk( "e", 2 );
        goto failure;
	}
	if (small_rtp_start(rtp->RTPSession, rtp_recv_callback, pvr, 1) != 0)
	{
		ERROR("small_rtp_start failed\n");
		write_chunk( "e", 2 );
		goto failure;
	}

	rtp_write_status(pvr);
	return 0;
failure:
	rtp->desc.fmt = payloadTypeUnknown;
	return -1;
}

static size_t http_write_callback(char *buffer, size_t size, size_t nmemb, void *userp)
{
	pvrInfo_t *pvr = (pvrInfo_t*)userp;
	httpRecordInfo_t *http = &pvr->http;
	ssize_t numbytes = size*nmemb;
	ssize_t offset, written;
	static char filename[PATH_MAX];

	if( http->out.fd <= 0 )
		return 0;

    if (http->out.position + numbytes > FILESIZE_THRESHOLD)
	{
		close(http->out.fd);
		sprintf(filename, "%s/part%02d.ts", http->out.directory, http->out.part+1);
		if ((http->out.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) >= 0)
		{
			http->out.part++;
			http->out.position = 0;
		} else
		{
			switch( errno )
			{
				case ENOSPC:
					write_chunk("es", 3);
					break;
				default:
					write_chunk("e", 2);
			}
			return 0;
		}
	}

	offset = 0;
	do
	{
		if( http->need_stop )
			break;
		written = write(http->out.fd, &buffer[offset], (numbytes-offset));
		//INFO("Written %d of %d (%d total)\n", written, size*nmemb, http->out.position + written);
		if (written > 0)
		{
			offset += written;
			http->out.position += written;
		} else
		{
			close(pvr->http.out.fd);
			pvr->http.out.fd = -1;
			pvr->current_job_end = 0;
			if( pvr->current_job && ((pvrJob_t*)(pvr->current_job->data))->type == pvrJobTypeHTTP )
				pvr_cancelCurrentJob(pvr);
			PERROR("Write error");
			switch( errno )
			{
				case ENOSPC:
					write_chunk("es", 3);
					break;
				default:
					write_chunk("e", 2);
			}
			break;
		}
	} while( offset < numbytes );
	return offset;
}

static void* http_record_thread(void *pArg)
{
	pvrInfo_t *pvr = (pvrInfo_t*)pArg;
	httpRecordInfo_t *http = &pvr->http;
	static char errbuff[CURL_ERROR_SIZE];
	CURLcode res;

	curl_easy_setopt(http->curl, CURLOPT_URL, http->url);
	curl_easy_setopt(http->curl, CURLOPT_ERRORBUFFER, errbuff);
	curl_easy_setopt(http->curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(http->curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(http->curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_TIMEOUT);
	curl_easy_setopt(http->curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(http->curl, CURLOPT_HEADER, 0L);
	curl_easy_setopt(http->curl, CURLOPT_NOBODY, 0L);
	curl_easy_setopt(http->curl, CURLOPT_WRITEDATA, pArg);
	curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, http_write_callback);
	curl_easy_setopt(http->curl, CURLOPT_PROXY, http->proxy);
	if( http->login[0] != 0 )
	{
		curl_easy_setopt(http->curl, CURLOPT_PROXYUSERPWD, http->login);
	}

	res = curl_easy_perform(http->curl);

	if(res != CURLE_OK && res != CURLE_WRITE_ERROR && http->proxy[0] != 0 )
	{
		curl_easy_setopt(http->curl, CURLOPT_URL, http->url);
		curl_easy_setopt(http->curl, CURLOPT_ERRORBUFFER, errbuff);
		curl_easy_setopt(http->curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(http->curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(http->curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_TIMEOUT);
		curl_easy_setopt(http->curl, CURLOPT_FOLLOWLOCATION, 1);
		curl_easy_setopt(http->curl, CURLOPT_HEADER, 0L);
		curl_easy_setopt(http->curl, CURLOPT_NOBODY, 0L);
		curl_easy_setopt(http->curl, CURLOPT_WRITEDATA, pArg);
		curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, http_write_callback);
		curl_easy_setopt(http->curl, CURLOPT_PROXY, "");

		res = curl_easy_perform(http->curl);
	}

	if( res != CURLE_OK )
	{
		ERROR("Failed to download '%s': %s\n", http->url, errbuff );
	}
	curl_easy_cleanup( http->curl );
	close(http->out.fd);
	http->curl = NULL;

	http_write_status(pvr);

	pthread_exit(NULL);
}

static int http_recording_start(pvrInfo_t *pvr, const char *url, const char *channelName)
{
	httpRecordInfo_t *http = &pvr->http;
	char *str, filename[PATH_MAX];
	time_t rawtime;
	struct tm *t;
	struct stat stat_info;
	size_t name_length = 0;

	http->curl = curl_easy_init();
	if(http->curl == NULL)
	{
		ERROR("Failed to init curl\n");
		return  -1;
	}

	http->need_stop = 0;
	strcpy( http->url, url );
	if (channelName != NULL && channelName[0] != 0)
	{
		if (strlen(channelName) < sizeof(http->title)) {
			helperStrCpyTrimSystem(http->title, channelName);
			name_length = strlen(http->title);
		}
		else
			name_length = 0;
	} else
	{
		str = strstr( http->url, "://" );
		if( str != NULL )
		{
			char *ptr;

			str+=3;
			ptr = index( str, '/' );
			if( ptr != NULL )
			{
				name_length = ptr - str;
				if( name_length < sizeof( http->title ) )
				{
					memcpy( http->title, str, name_length );
					http->title[name_length] = 0;
				} else
					name_length = 0;
			}
		}
	}
	if (name_length == 0)
		strcpy(http->title, "http");

	INFO( "Recording %s (%s)\n", http->url, http->title);

	//sprintf( mkdirString, "ur%s://%s:%d", proto_toa(rtp->desc.proto), inet_ntoa(rtp->ip), rtp->desc.port);
	//write_chunk( mkdirString, strlen(mkdirString)+1 );

	if( stat( pvr->path, &stat_info ) < 0 || (stat_info.st_mode & S_IFDIR) != S_IFDIR )
	{
		PERROR("Can't acces output path");
		write_chunk( "ed", 3 );
		goto failure;
	}

	sprintf(http->out.directory, "%s" STBPVR_FOLDER "/%s", pvr->path, http->title);
	str = &http->out.directory[strlen(http->out.directory)];
	*str = '/';
	str++;
	time( &rawtime );
	t = localtime(&rawtime);
	strftime(str, 20, "%Y-%m-%d/%H-%M", t);
	mkdirs(http->out.directory);

	INFO("Recording HTTP to '%s'\n", http->out.directory);
	http->out.part = 1;
	http->out.position = 0;
	sprintf(filename, "%s/part%02d.ts", http->out.directory, http->out.part);
	if ((http->out.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) < 0)
	{
		PERROR("Output file create failed");
		write_chunk( "e", 2 );
		goto failure;
	}

	if( pthread_create( &http->thread, NULL, http_record_thread, pvr)  != 0 )
	{
		http->thread = 0;
		ERROR("pthread_create(http_record_thread) failed\n");
		write_chunk( "e", 2 );
		goto failure;
	}

	http_write_status(pvr);
	return 0;
failure:
	curl_easy_cleanup( http->curl );
	return -1;
}

static int http_recording_stop(pvrInfo_t *pvr)
{
	httpRecordInfo_t *http = &pvr->http;
	if( http->thread != 0 )
	{
		http->need_stop = 1;
		//INFO("%s: Wait for http thread\n", __func__);
		pthread_join(http->thread, NULL);
		//INFO("%s: done\n", __func__);
		http->thread = 0;
		pvr->current_job_end = 0;
		if( pvr->current_job && ((pvrJob_t*)(pvr->current_job->data))->type == pvrJobTypeHTTP )
			pvr_cancelCurrentJob(pvr);
	}
	return 0;
}

static int  http_write_status(pvrInfo_t *pvr)
{
	char buf[MAX_URL];
	if( pvr->http.curl != NULL )
		sprintf(buf,"hr%s", pvr->http.url );
	else
		strcpy(buf,"hi");
	return write_chunk(buf, strlen(buf)+1);
}

static int pvr_recording_stop(pvrInfo_t *pvr)
{
	dvb_recording_stop(pvr);
	rtp_recording_stop(pvr);
	http_recording_stop(pvr);
	return 0;
}

static int pvr_clearJobList(void)
{
	list_element_t *cur_element, *del_element;
	cur_element = pvr_jobs;
	while (cur_element != NULL) {
		del_element = cur_element;
		cur_element = cur_element->next;
		pvr_freeJob(del_element);
	}
	pvr_jobs = NULL;
	return 0;
}

ssize_t pvr_jobprint(char *buf, size_t buf_size, pvrJob_t *job)
{
	char *str  = buf;
	char *name = NULL;
	struct tm *t;
	if (buf_size < 24)
		return -1;
	t = gmtime(&job->start_time);
	strftime( str, 25, "%F", t);
	str = &str[strlen(str)];
	strftime( str, 15, " %T", t);
	str = &str[strlen(str)];
	t = gmtime(&job->end_time);
	strftime( str, 15, "-%T ", t);
	str = &str[strlen(str)];
	switch (job->type)
	{
		case pvrJobTypeUnknown:
			break;
		case pvrJobTypeDVB:
			name = dvb_getTempServiceName(job->info.dvb.channel);
			break;
		case pvrJobTypeRTP:
			name = job->info.rtp.session_name;
			break;
		case pvrJobTypeHTTP:
			name = job->info.http.session_name;
			break;
	}
	if (name)
	strncpy(str, name, buf_size-(str-buf));
	buf[buf_size-1] = 0;
	return strlen(buf)+1;
}

static int pvr_importJobList(void)
{
	FILE* f;
	pvrJob_t *curJob;
	list_element_t *cur_element;
	char buf[MAX_URL+18]; //strlen("JOBCHANNELRTPNAME")
	char value[MAX_URL];
	time_t jobStart, jobEnd;
	int channel_dvb;
	media_desc desc;
	url_desc_t url;
	in_addr_t  rtp_ip = 0;
	char *url_str;
	size_t job_count = 0;

	f = fopen( STBPVR_JOBLIST, "r" );
	if ( f == NULL )
	{
		PERROR("Can't open job list!");
		return 1;
	}
	pvr_clearJobList();
	cur_element = pvr_jobs;
	buf[0] = 0; // buffer is empty and ready for reading next line from input file
	while(1)
	{
		if ( buf[0] == 0 && fgets( buf, sizeof(buf), f ) == NULL )
		{
			INFO("%s: imported %d jobs\n", __FUNCTION__, job_count);
			fclose(f);
			return 0;
		}
		channel_dvb = STBPVR_DVB_CHANNEL_NONE;
		jobStart    = 0;
		jobEnd      = 0;
		url_str     = NULL;
		if( sscanf(buf, "JOBSTART=%d", (int*)&jobStart) != 1 )
		{
			buf[0] = 0;
			continue;
		}
		if( fgets( buf, sizeof(buf), f ) == NULL)
			break;
		if( sscanf(buf, "JOBEND=%d", (int*)&jobEnd) != 1 )
		{
			buf[0] = 0;
			continue;
		}
		if( fgets( buf, sizeof(buf), f ) == NULL)
			break;
		if( sscanf(buf, "JOBCHANNELDVB=%d", &channel_dvb) != 1 )
		{
			channel_dvb = STBPVR_DVB_CHANNEL_NONE;

			if( sscanf(buf, "JOBCHANNELRTP=%[^\r\n]", value) != 1 )
			{
				if( sscanf(buf, "JOBURL=%[^\r\n]", value) != 1 )
				{
					ERROR("%s: failed to get job type for [%ld-%ld]", __FUNCTION__, jobStart, jobEnd);
					buf[0] = 0;
					continue;
				}
				if( strncmp( value, "http://",  7 ) != 0 &&
				    strncmp( value, "https://", 8 ) != 0 &&
				    strncmp( value, "ftp://",   6 ) != 0 )
				{
					ERROR("%s: wrong url proto %s", __FUNCTION__, value);
					buf[0] = 0;
					continue;
				}
				url_str = malloc( strlen(value)+1 );
				if( url_str == NULL )
				{
					ERROR("%s: failed to allocate memory!", __FUNCTION__);
					buf[0] = 0;
					continue;
				}
				strcpy( url_str, value );
				if( fgets( buf, sizeof(buf), f ) == NULL)
				{
					ERROR("%s: unexpected end of file (http title wanted)!", __FUNCTION__);
					buf[0] = 0;
				}
				if( strncmp( buf, "JOBNAME=", sizeof("JOBNAME=")-1 ) == 0 )
				{
					strcpy( value, &buf[sizeof("JOBNAME=")-1] );
					helperStrTrim( value );
				} else
				{
					ERROR("%s: failed to get http job name for [%ld-%ld]", __FUNCTION__, jobStart, jobEnd);
					value[0] = 0;
				}
			} else
			{
				memset(&desc, 0, sizeof(desc));
				memset(&url,  0, sizeof(url));

				if( parseURL( value, &url ) != 0 )
				{
					ERROR("%s: invalid RTP url %s!", __FUNCTION__, value);
					buf[0] = 0;
					continue;
				}
				switch(url.protocol)
				{
					case mediaProtoUDP:
					case mediaProtoRTP:
						desc.fmt   = payloadTypeMpegTS;
						desc.type  = mediaTypeVideo;
						desc.port  = url.port;
						desc.proto = url.protocol;
						rtp_ip = inet_addr(url.address);
					default:;
				}
				if(desc.fmt == 0 || rtp_ip == INADDR_NONE || rtp_ip == INADDR_ANY)
				{
					ERROR("%s: invalid proto %d!", __FUNCTION__, url.protocol);
					buf[0] = 0;
					continue;
				}
				if( fgets( buf, sizeof(buf), f ) == NULL)
				{
					ERROR("%s: unexpected end of file (rtp title wanted)!", __FUNCTION__);
					buf[0] = 0;
				}
				if( strncmp( buf, "JOBCHANNELRTPNAME=", sizeof("JOBCHANNELRTPNAME=")-1 ) == 0 )
				{
					strcpy( value, &buf[sizeof("JOBCHANNELRTPNAME=")-1] );
					helperStrTrim( value );
				} else
				{
					ERROR("%s: failed to get rtp job name for [%ld-%ld]", __FUNCTION__, jobStart, jobEnd);
					value[0] = 0;
				}
			}
		}
		if( pvr_jobs == NULL )
		{
			pvr_jobs = allocate_element(sizeof(pvrJob_t));
			cur_element = pvr_jobs;
		} else {
			cur_element = insert_new_element(cur_element, sizeof(pvrJob_t));
		}
		curJob = (pvrJob_t*)cur_element->data;
		curJob->start_time = jobStart;
		curJob->end_time = jobEnd;
		if( channel_dvb != STBPVR_DVB_CHANNEL_NONE )
		{
			//INFO("Importing job dvb %d %d %d\n", (int)jobStart, (int)jobEnd, channel_dvb);
			curJob->type = pvrJobTypeDVB;
			curJob->info.dvb.channel = channel_dvb;
			curJob->info.dvb.event_id = -1;
			curJob->info.dvb.service = NULL;
		} else if( url_str != NULL )
		{
			curJob->type = pvrJobTypeHTTP;
			curJob->info.http.url = url_str;
			strncpy( curJob->info.http.session_name, value, sizeof(curJob->info.http.session_name) );
		} else
		{
			//INFO("Importing job rtp %d %d %s://%s:%d %s\n", (int)jobStart, (int)jobEnd, proto_toa(curJob->info.rtp.desc.proto), inet_ntoa(curJob->info.rtp.ip), curJob->info.rtp.desc.port, curJob->info.rtp.session_name);
			curJob->type = pvrJobTypeRTP;
			memcpy(&curJob->info.rtp.desc, &desc, sizeof(desc));
			curJob->info.rtp.ip.s_addr = rtp_ip;
			strncpy(curJob->info.rtp.session_name, value, sizeof(curJob->info.rtp.session_name));
			curJob->info.rtp.session_name[sizeof(curJob->info.rtp.session_name)-1] = 0;
		}
		pvr_jobprint( value, sizeof(value), curJob );
		INFO("%s: imported %d %s\n", __FUNCTION__, job_count, value);
		job_count++;

		buf[0] = 0; // mark buffer ready for next job
	}
	fclose(f);
	ERROR("%s: failed, imported %d jobs", __FUNCTION__, job_count);
	return 1;
}

static void pvr_freeJob(list_element_t* job_element)
{
	pvrJob_t *job = job_element->data;
	if (job->type == pvrJobTypeHTTP)
		free(job->info.http.url);
	free_element(job_element);
}

static int pvr_deleteJob(list_element_t* job_element)
{
	list_element_t *prev_element = NULL;
	list_element_t *cur_element;

	if (pvr_jobs == NULL || job_element == NULL)
		return 0;
	for (cur_element = pvr_jobs; cur_element != NULL && cur_element != job_element; cur_element = cur_element->next)
		prev_element = cur_element;
	if (cur_element != job_element)
		return 1;

	if (prev_element == NULL)
		pvr_jobs = pvr_jobs->next;
	else
		prev_element->next = job_element->next;

	pvr_freeJob(job_element);
	return 0;
}

static int pvr_exportJobList(void)
{
	FILE* f;
	pvrJob_t *curJob;
	list_element_t *cur_element;

	f = fopen( STBPVR_JOBLIST, "w" );
	if ( f == NULL )
	{
		return 1;
	}
	for( cur_element = pvr_jobs; cur_element != NULL; cur_element = cur_element->next )
	{
		curJob = (pvrJob_t*)cur_element->data;
		fprintf(f,"JOBSTART=%d\nJOBEND=%d\n", (int)curJob->start_time, (int)curJob->end_time );
		switch( curJob->type )
		{
			case pvrJobTypeUnknown: break;
			case pvrJobTypeDVB:
				fprintf(f,"JOBCHANNELDVB=%d\n", curJob->info.dvb.channel);
				break;
			case pvrJobTypeRTP:
				fprintf(f,"JOBCHANNELRTP=%s://%s:%d/\nJOBCHANNELRTPNAME=%s\n", proto_toa(curJob->info.rtp.desc.proto), inet_ntoa(curJob->info.rtp.ip), curJob->info.rtp.desc.port, curJob->info.rtp.session_name);
				break;
			case pvrJobTypeHTTP:
				fprintf(f,"JOBURL=%s\nJOBNAME=%s\n", curJob->info.http.url, curJob->info.http.session_name);
				break;
		}
	}
	fclose(f);
	return 0;
}

static void pvr_cancelCurrentJob(pvrInfo_t *pvr)
{
	if( pvr->current_job )
	{
		pvrJob_t *job = (pvrJob_t *)pvr->current_job->data;
		char *name, *str;
		char buf[BUFFER_SIZE];
		struct tm *t;
		str = buf;
		t = gmtime(&job->start_time);
		strftime( str, 25, "%F", t);
		str = &str[strlen(str)];
		strftime( str, 15, " %T", t);
		str = &str[strlen(str)];
		t = gmtime(&job->end_time);
		strftime( str, 15, "-%T ", t);
		str = &str[strlen(str)];
		switch( job->type )
		{
			case pvrJobTypeUnknown:
				name = NULL;
				break;
			case pvrJobTypeDVB:
				name = dvb_getTempServiceName(job->info.dvb.channel);
				break;
			case pvrJobTypeRTP:
				name = job->info.rtp.session_name;
				break;
			case pvrJobTypeHTTP:
				name = job->info.http.session_name;
				break;
		}
		sprintf(str,"%s", name);
		INFO("Canceling job %s\n", buf);
		job->end_time = time(NULL)-1;
		//pvr_deleteJob(pvr->current_job);
		pvr_exportJobList();
		pvr->current_job = NULL;
	}
}

static int pvr_jobcmp(pvrJob_t *x, pvrJob_t *y)
{
	int res;
	if( (res = y->start_time - x->start_time) != 0 )
		return res;
	if( (res = y->end_time - x->end_time) != 0 )
		return res;
	if( (res = y->type - x->type ) != 0 )
		return res;
	switch( y->type )
	{
		case pvrJobTypeUnknown:
			return 0;
		case pvrJobTypeDVB:
			if( (res = y->info.dvb.channel - x->info.dvb.channel ) != 0 )
				return res;
			break;
		case pvrJobTypeRTP:
			if( (res = y->info.rtp.ip.s_addr - x->info.rtp.ip.s_addr ) != 0 )
				return res;
			if( (res = y->info.rtp.desc.port - x->info.rtp.desc.port ) != 0 )
				return res;
			break;
		case pvrJobTypeHTTP:
			return strcmp( y->info.http.url, x->info.http.url );
	}
	return 0;
}

list_element_t* pvr_findJob(pvrJob_t *job)
{
	list_element_t *jobListEntry;
	pvrJob_t *curJob;

	//INFO("%s: [%ld-%ld] \n", __FUNCTION__, job->start_time, job->end_time);

	for( jobListEntry = pvr_jobs; jobListEntry != NULL; jobListEntry = (jobListEntry)->next )
	{
		curJob = (pvrJob_t*)jobListEntry->data;
		//INFO("%s: test: [%ld-%ld] \n", __FUNCTION__, curJob->start_time, curJob->end_time);
		if( curJob->start_time >= job->end_time )
			break;
		if( 0 == pvr_jobcmp(job, curJob) )
			return jobListEntry;
	}
	return NULL;
}

static void dvb_recording_threadTerm(void* pArg)
{
	dvbRecordInfo_t *dvb = (dvbRecordInfo_t *)pArg;

	INFO("Exiting DVB Thread\n");
	dvb->channel = STBPVR_DVB_CHANNEL_NONE;
	if (dvb->out.fd > 0) fsync(dvb->out.fd);
	dvb_instance_close(dvb);

	dvb->running = 0;
}

/* Thread that deals with asynchronous recording and playback of PVR files */
static void *dvb_recording_thread(void *pArg)
{
	dvbRecordInfo_t *dvb = (dvbRecordInfo_t *)pArg;
	ssize_t read_length;
	int write_length;
	struct timeval pat_time, cur_time;
	char filename[PATH_MAX];
	unsigned char pat[2*188]; // PAT+PMT
	unsigned char *pmt = &pat[188];
	unsigned char packet_counter = 0;
	EIT_service_t *service;

	INFO("DVB THREAD START!\n");

	dvb->running = 1;

	service = dvb_getService( dvb->channel );
	if (service != NULL )
	{
		int length;

		memset( pat, -1, sizeof(pat));
		pat[0] = 0x47;
		pat[1] = 0x40; // 3 reserved; 13 PID
		pat[2] = 0x00;
		pat[3] = 0x30 | (packet_counter & 0xf); // adaptation bit + payload
		pat[4] = (unsigned char)188-(16)-6;
		pat[5] = 0x00;
		pat[(unsigned char)pat[4]+5] = 0x00; // adaptation field
		//INFO("Writing PAT section at %hhu\n",pat[4]+6);
		if ((length = make_pat_section(service, (char *)&pat[pat[4]+6], 16, &write_length)) != 1 )
		{
			INFO("Warning (%d): failed to make PAT for service %d\n", length, dvb->channel );
			service = NULL;
		}
		/*else
		{
			INFO("PAT table\n");
			for ( length = 0; length < 188; length++ )
			{
				printf("%02hhx ", (unsigned char)pat[length]);
				if( (length + 1) % 8 == 0 )
					printf("\n");
			}
			printf("\n");
		}*/
	}
	if (service != NULL)
	{
		int length;

		pmt[0] = 0x47;
		pmt[1] = 0x40 | ((service->program_map.program_map_PID >> 8) & 0x1F);
		pmt[2] = (service->program_map.program_map_PID) & 0xFF;
		pmt[3] = 0x30 | (packet_counter & 0xf);
		pmt[4] = (unsigned char)188-(12+ 2*5 + 4)-6;
		pmt[5] = 0x00;
		pmt[(unsigned char)pmt[4]+5] = 0x00;
		//INFO("Writing PMT section at %hhu\n",pmt[4]+6);
		if ((length = make_pmt_section(service, dvb->filterv.pid, dvb->filtera.pid, (char *)&pmt[pmt[4]+6], 12+ 2*5 + 4, &write_length)) != 1)
		{
			INFO("Warning (%d): failed to make PMT with PID %d for service %d\n", length, service->program_map.program_map_PID, dvb->channel );
			service = NULL;
		}
		/*else
		{
			INFO("PMT table (pid 0x%04hx)\n", service->program_map.program_map_PID);
			for ( length = 0; length < 188; length++ )
			{
				printf("%02hhx ", (unsigned char)pmt[length]);
				if( (length + 1) % 8 == 0 )
					printf("\n");
			}
			printf("\n");
			
		}*/
	}
	if (service != NULL)
	{
		INFO("Writing initial PAT/PMT\n");

		dvb->out.position += sizeof(pat);

		if (dvb->out.fd > 0 &&
		    write(dvb->out.fd, pat, sizeof(pat)) != sizeof(pat))
			PERROR("Failed to write PAT/PMT");
		if (dvb->fdplay > 0 &&
		    write(dvb->fdplay, pat, sizeof(pat)) != sizeof(pat))
			PERROR("Failed to write PAT/PMT to pipe");

		gettimeofday(&pat_time, NULL);
	} else
	{
		INFO( "Warning: failed to get info for service %d\n", dvb->channel );
	}

	pthread_cleanup_push(dvb_recording_threadTerm, pArg);
	do
	{
		unsigned char buffer[PVR_BUFFER_SIZE];

		pthread_testcancel();
		read_length = read(dvb->fdin, buffer, PVR_BUFFER_SIZE);

		if (read_length <= 0)
		{
			write_chunk("e", 2);
			PERROR("Read error");
			ERROR( "fdf=%d fdin=%d fdout=%d v=%d a=%d p=%d",dvb->fdf, dvb->fdin, dvb->out.fd, dvb->filterv.pid, dvb->filtera.pid, dvb->filterp.pid);
			break;
		}

		pthread_testcancel();

		if (dvb->out.fd > 0)
		{
			dvb->out.position += read_length;

			if (dvb->out.position > FILESIZE_THRESHOLD)
			{
				close(dvb->out.fd);

				sprintf(filename, "%s/part%02d.ts", dvb->out.directory, dvb->out.part+1);
				if ((dvb->out.fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS)) >= 0)
				{
					dvb->out.part++;
					dvb->out.position = read_length;
				} else
				{
					ERROR("Failed to open '%s' for writing", filename);
					break;
				}
			}

			write_length = write(dvb->out.fd, buffer, read_length);

			if (write_length < 0)
			{
				PERROR("Write error");
				switch (errno)
				{
					case ENOSPC: write_chunk("es", 3); break;
					default:     write_chunk("e",  2);
				}
				break;
			}
		}

		if (dvb->fdplay > 0)
		{
			size_t offset     = 0;
			size_t chunk_size = PVR_CHUNK_SIZE;

			while (offset < (size_t)read_length)
			{
				if (chunk_size > (read_length-offset))
					chunk_size = (read_length-offset);

				if (dvb->play_only)
				{
					// Ignore socket write errors
					write(dvb->fdplay, &buffer[offset], chunk_size);
					offset += chunk_size;
				} else
				{
					int write_tries  = 0;
					size_t chunk_offset = 0;
					ssize_t size_write;

					while (chunk_offset < chunk_size)
					{
						pthread_testcancel();
						size_write = write(dvb->fdplay, &buffer[offset], chunk_size-chunk_offset);
						if (size_write>0)
						{
							write_tries = 0;
							offset       += size_write;
							chunk_offset += size_write;
						} else
						{
							if (++write_tries > PVR_WRITE_COUNT)
							{
								PERROR("Failed to write %d bytes to pipe", chunk_size-chunk_offset);
								goto cancel_pipe_write;
							}
						}
					}
				}
			}
		}
cancel_pipe_write:
		if (service != NULL)
		{
			gettimeofday(&cur_time, NULL);
			__suseconds_t difftime = (cur_time.tv_sec-pat_time.tv_sec)*1000000+(cur_time.tv_usec-pat_time.tv_usec);
			if (difftime >= PATPMT_PERIOD)
			{
				// INFO("Inserting PAT/PMT\n");
				packet_counter++;
				pat[3] = 0x30 | (packet_counter & 0xf);
				pmt[3] = 0x30 | (packet_counter & 0xf);
				if (dvb->out.fd > 0 && write(dvb->out.fd, pat, sizeof(pat)) != sizeof(pat))
				{
					PERROR("PAT/PMT write error");
				}
				if (dvb->fdplay > 0 && write(dvb->fdplay, pat, sizeof(pat)) != sizeof(pat))
				{
					if (!dvb->play_only)
					PERROR("PAT/PMT pipe write error");
				}
				pat_time = cur_time;
			}
		}
		pthread_testcancel();
	} while (dvb->running != 0);

	INFO("Exiting %s\n", __FUNCTION__);
	pthread_cleanup_pop(1);
	return NULL;
}

static int loadAppSettings(pvrInfo_t *pvr)
{
	char buf[BUFFER_SIZE];
	char passwd[512];
	FILE *fd;
	fd = fopen(SETTINGS_FILE, "r");
	if (fd == NULL)
	{
		ERROR("Failed to open %s for reading", SETTINGS_FILE);
		return -1;
	}
	while (fgets(buf, sizeof(buf), fd) != NULL)
	{
		sscanf(buf, "DVBCINVERSION=%d",      &pvr->dvb.info.dvbcInfo.inversion);
		sscanf(buf, "DVBTINVERSION=%d",      &pvr->dvb.info.dvbtInfo.inversion);
		sscanf(buf, "TUNERSPEED=%d",         &pvr->dvb.info.dvbAdapterSpeed);
		sscanf(buf, "DVBTBANDWIDTH=%ld",     &pvr->dvb.info.dvbtInfo.bandwidth);
		sscanf(buf, "QAMMODULATION=%ld",     &pvr->dvb.info.dvbcInfo.modulation);
		sscanf(buf, "QAMSYMBOLRATE=%ld",     &pvr->dvb.info.dvbcInfo.symbolRate);
		if (strncasecmp(buf, "PVRDIRECTORY=", 13) == 0)
		{
			strcpy(pvr->path,&buf[13]);
			if(strlen(pvr->path)>0)
			{
				pvr->path[strlen(pvr->path)-1] = 0; // remove \n
			}
		}
	}
	fclose(fd);

	fd = fopen(BROWSER_CONFIG_FILE, "r");
	if (fd != NULL)
	{
		passwd[0] = 0;
		while (fgets(buf, sizeof(buf), fd) != NULL)
		{
			sscanf(buf, "HTTPProxyServer=%s", pvr->http.proxy);
			sscanf(buf, "HTTPProxyLogin=%[^\r\n]", pvr->http.login);
			sscanf(buf, "HTTPProxyPasswd=%[^\r\n]", passwd);
		}
		fclose(fd);

		if( pvr->http.login[0] != 0 && passwd[0] != 0 )
		{
			char *str = &pvr->http.login[strlen(pvr->http.login)];
			*str = ':';
			strncpy( str+1, passwd, sizeof(pvr->http.login)-(str - pvr->http.login)-1 );
		}
	}
	INFO("PVRDIRECTORY=%s\n", pvr->path);
	INFO("PROXY=%s\n", pvr->http.proxy);
	if( pvr->http.proxy[0] != 0 && pvr->http.login[0] != 0 )
		INFO("PROXY_LOGIN=%s\n", pvr->http.login);
	return 0;
}

/* Handle any registered signal by requesting a graceful exit */
static void signal_handler(int sig)
{
	INFO( "Got signal %d: Quiting\n", sig );
	exit_app = 1;
}

static void sigusr_handler(int sig)
{
	INFO( "Got signal %d: Update required\n", sig );
	update_required = 1;
	signal(SIGUSR1, sigusr_handler);
}

static int helperFileExists(char* filename)
{
	int file;

	file = open( filename, O_RDONLY);

	if ( file < 0 )
	{
		return 0;
	}

	close( file );

	return 1;
}

/*****************************************************************************
 * Socket in-out
 */

static int read_chunk (char * buf, int len)
{
	int t;
	int sz = -1;

	if (clients.count > 0)
	{
		for (t=0; t<MAX_CLIENTS; t++)
		{
			if (clients.sockets[t] != 0)
			{
				sz = recv(clients.sockets[t], buf, len, 0);
				if (sz == 0 || (sz == -1 && errno != EWOULDBLOCK))
				{
					INFO("client[%d]: %d disconnected\n", t, clients.sockets[t]);
					clients.sockets[t] = 0;
					clients.count--;
				} else if (sz > 0)
				{
					INFO("Received '%s' (%d bytes) from %d\n", buf, sz, clients.sockets[t]);
					break;
				}
			}
		}
	}

	return sz;
}

static int write_chunk (char * buf, int out_len)
{
	int t, len, res = -1;

	if(!buf) return -2;

	if (clients.count > 0)
	{
		for (t=0; t<MAX_CLIENTS; t++)
		{
			if (clients.sockets[t] != 0)
			{
				int total = 0;
				int n = 0;

				len = out_len;
				while (total < out_len)
				{
					n = send(clients.sockets[t],&buf[total],len, 0);
					INFO("written '%s' %d bytes to %d\n", &buf[total], n, clients.sockets[t]);
					if (n != len)
					{
						//INFO("n %d != len %d at total %d !!!\n", n, len, total);
						if (n == -1)
						{
							// Non-blocking output or broken socket? Just forget it.
							if (errno != EWOULDBLOCK)
							{
								INFO("client[%d]: %d has disconnected\n", t, clients.sockets[t]);
								clients.sockets[t] = 0;
								clients.count--;
							}
							break;
						}
					}
					if (n > 0)
					{
						len -= n;
						total += n;
					}
				}
				if (total == out_len)
				{
					res = out_len;
				}
			}
		}
	}

	return res;
}

static int dvb_write_status(pvrInfo_t *pvr)
{
	char buf[16];
	if( pvr->dvb.channel != STBPVR_DVB_CHANNEL_NONE )
		sprintf(buf,"d%c%d", (pvr->dvb.fdplay > 0 && !pvr->dvb.play_only) ? 'p' : 'r', pvr->dvb.channel );
	else
		strcpy(buf,"di");
	return write_chunk(buf, strlen(buf)+1);
}

static int rtp_write_status(pvrInfo_t *pvr)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	if( pvr->rtp.desc.fmt != payloadTypeUnknown )
		sprintf(buf,"ur%s://%s:%d", proto_toa(pvr->rtp.desc.proto), inet_ntoa( pvr->rtp.ip ), pvr->rtp.desc.port );
	else
		strcpy(buf,"ui");
	return write_chunk(buf, strlen(buf)+1);
}

static inline void pvr_write_status(pvrInfo_t *pvr)
{
	dvb_write_status(pvr);
	rtp_write_status(pvr);
	http_write_status(pvr);
}

static int setDvbRecStatus(const int setStatus, const char *URL,const int id)
{
    char status[20];
    FILE* f;
    f = fopen( STBPVR_STATUSLIST, "w" );
    if ( f == NULL )
            return 0;

    switch(setStatus)
    {
        case dvbRecord_free:
            strcpy(status,"free");
            break;
        case dvbRecord_active:
            strcpy(status,"active");
            break;
        case dvbRecord_closed:
            strcpy(status,"closed");
            break;
        case dvbRecord_error:
            strcpy(status,"error");
            break;
        default:
            return 0;
            break;
    }
    fprintf(f,"REC_STATUS=%s\nURL=%s\nID=%d\n", status, URL, id);
    fclose(f);
    return 1;
}

static dvb_status_rec getDvbRecStatus()
{
    FILE* f;
    errno = 0;
    f = fopen( STBPVR_STATUSLIST, "r" );

    if (errno != 0)
    {
        if (errno == ENOENT)
        {
            INFO("ERROR read file: %s\n",strerror( errno ));
            return dvbRecord_free;
        }
        return dvbRecord_error;
    }
    if ( f == NULL )
        return dvbRecord_error;

    char buf[MAX_URL];
    fgets( buf, sizeof(buf), f );
    fclose(f);

    char *bufStatus;
    bufStatus = strchr(buf,'=');
    bufStatus++;
    bufStatus[strlen(bufStatus)-1] = '\0';

    if (strncasecmp( bufStatus, "free", 4 ) == 0)
        return dvbRecord_free;
    else
    if (strncasecmp( bufStatus, "active", 6 ) == 0)
        return dvbRecord_active;
    else
    if (strncasecmp( bufStatus, "closed", 6 ) == 0)
        return dvbRecord_closed;
    else
    if (strncasecmp( bufStatus, "error", 5 ) == 0)
        return dvbRecord_error;

    return dvbRecord_error;
}

static void* pvr_socket_thread(void *arg)
{
    struct sockaddr_un sa;
    struct stat st;
    int sock = -1;

	int socket_fd = -1;
	int len, channel;
	struct sockaddr_un local, remote;
	char buf[PATH_MAX];
	char *type  =  buf;
	char *cmd   = &buf[1];
	char *value = &buf[2];
	pvrInfo_t *pvr = (pvrInfo_t *)arg;
	dvbRecordInfo_t *dvb = &pvr->dvb;
	EIT_service_t *service;
	FILE *dummy;
	int apid, vpid, pcr, videoType, audioType;

	if ((socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		INFO("Failed to create socket\n");
		goto on_exit;
	}
	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, STBPVR_SOCKET_FILE);
	unlink(local.sun_path);
	len = strlen(local.sun_path) + sizeof(local.sun_family);
	if (bind(socket_fd, (struct sockaddr *)&local, len) == -1)
	{
		INFO("Failed to bind socket\n");
		goto on_exit;
	}

	if (fcntl(socket_fd, F_SETFL, O_NONBLOCK) == -1)
	{
		INFO("Failed to set non-blocking mode\n");
		goto on_exit;
	}

	if (listen(socket_fd, 5) == -1)
	{
		INFO("Failed to listen socket\n");
		goto on_exit;
	}

	while ( exit_app == 0 )
	{
		if (clients.count < MAX_CLIENTS)
		{
			int s, t;
			len = sizeof(remote);
			if ((s = accept(socket_fd, (struct sockaddr *)&remote, (socklen_t*)&len)) == -1 && errno != EWOULDBLOCK)
			{
				INFO("Failed to accept socket\n");
			} else if (s != -1)
			{
				INFO("New client[%d]: %d\n", clients.count, s);
				if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
				{
					INFO("Failed to set client non-blocking mode\n");
					goto on_exit;
				}
				for (t=0; t<MAX_CLIENTS; t++)
				{
					if (clients.sockets[t] == 0)
					{
						clients.sockets[t] = s;
						clients.count++;
						break;
					}
				}
			}
		}

		if (read_chunk(buf, sizeof(buf)) <= 0)
		{
			usleep(50000);
			continue;
		}

		INFO("Got: '%s'\n", buf);

                cJSON *params = cJSON_Parse(buf);
                if (params != NULL && params->type != cJSON_NULL)
                {
                    cJSON *values = cJSON_GetObjectItem( params, "method" );
                    if (values != NULL && values->type != cJSON_NULL)
                    {
                        if (strncasecmp( values->valuestring, "recstart", 8 ) == 0)
                        {
                            char *url = NULL;
                            char *file = NULL;
                            int id = 0;
                            values = cJSON_GetObjectItem( params, "params" );

                            if (NULL == (url  = objGetString(values, "url", NULL)) ||
                                NULL == (file = objGetString(values, "filename", NULL)) ||
                                0    == (id = objGetInt(params, "id", 0)))
                            {
                                //error
                            }
                            else
                            {
                                dvb_status_rec_t = getDvbRecStatus();

                                cJSON *param = cJSON_CreateObject();

                                if (dvb_status_rec_t == dvbRecord_free)
                                {
                                    INFO("Rec Start\n");
                                }

                                if (dvb_status_rec_t == dvbRecord_active)
                                {
                                    INFO("Rec Stop\n");
                                    cJSON_AddItemToObject(param, "method", cJSON_CreateString("recstop"));
                                    cJSON_AddItemToObject(param, "id", cJSON_CreateNumber( id ));
                                    memset(buf, '\0', sizeof(buf));
                                    sprintf(buf,cJSON_PrintUnformatted(param));
                                }

                                strcpy(sa.sun_path, STBELCD_SOCKET_FILE);
                                sa.sun_family = AF_UNIX;

                                if( stat( sa.sun_path, &st) < 0)
                                {
                                        INFO("error stat %s\n",STBELCD_SOCKET_FILE);
                                        break;
                                }
                                sock = socket(AF_UNIX, SOCK_STREAM, 0);

                                if (sock == -1)
                                {
                                        INFO("Failed to create socket\n");
                                        break;
                                }

                                int Elcdlen = strlen(sa.sun_path) + sizeof(sa.sun_family);
                                if (connect(sock, (struct sockaddr *)&sa, Elcdlen) == -1)
                                {
                                        INFO("error connect\n");
                                        break;
                                }

                                if (send(sock, buf, sizeof(buf), 0) == -1)
                                {
                                        INFO("Error send message\n");
                                        close(sock);
                                        break;
                                }
                                memset(buf,'\0',sizeof(buf));
                                if (recv(sock, buf, sizeof(buf)-1, 0) == -1)
                                {
                                        INFO("Error recv message\n");
                                        close(sock);
                                        break;
                                }

                                INFO("Response: %s\n",buf);

                                param = cJSON_Parse(buf);
                                char *result = NULL;
                                result = objGetString(param, "result", NULL);

                                if ((dvb_status_rec_t == dvbRecord_free) && (strncasecmp( result, "ok", 2 ) == 0) )
                                {
                                    setDvbRecStatus(dvbRecord_active,url ,id );
                                }

                                if ((dvb_status_rec_t == dvbRecord_active) && (strncasecmp( result, "ok", 2 ) == 0) )
                                {
                                    setDvbRecStatus(dvbRecord_free ,url ,id );

                                }
                                close(sock);
                                cJSON_Delete(param);

                            }
                        } else
                        if (strncasecmp( values->valuestring, "recstop", 7 ) == 0)
                        {

                        }
                    }
               }
               cJSON_Delete(params);


		switch (*type) {
			case '?': // status?
				pvr_write_status(pvr);
				break;
			case 'd': //dvb
				switch(*cmd)
				{
					case 'p': // play
						if( pvr->path[0] == 0 )
						{
							write_chunk("ee", 3);
							break;
						}
						channel = *value != 0 ? atoi( value ) : dvb->channel;
						INFO("fdplay=%d ch=%d current=%d\n", dvb->fdplay, channel, dvb->channel);
						service = dvb_getService( channel );
						if (service == NULL)
						{
							ERROR("Failed to find channel %d!", channel);
							dvb_write_status(pvr);
							break;
						}
						dummy = fopen( STBPVR_PIPE_FILE ".dummy", "w" );
						INFO("service=%p dummy=%p\n", service, dummy);
						if (dummy != NULL)
						{
							dvb_getPIDs( service, 0, &videoType, &audioType, &vpid, &apid, &pcr);
							INFO("Recording DVB: vt=%d at=%d vpid=%d apid=%d pcr=%d\n", videoType, audioType, vpid, apid, pcr);
							fprintf(dummy, "%d %d %d %d %d\n", videoType, audioType, vpid, apid, pcr);
							fclose(dummy);
							if (dvb->fdplay > 0 && dvb->play_only)
							{
								INFO("Cancel DVB streaming\n");
								close(dvb->fdplay);
								dvb->fdplay = -1;
								dvb->play_only = 0;
							}
							if (dvb->fdplay <= 0)
							{
								remove( STBPVR_PIPE_FILE );
								system("ln -s /dev/dsppipe0 " STBPVR_PIPE_FILE);
								INFO("opening %s\n", STBPVR_PIPE_FILE);
								dvb->fdplay = open( STBPVR_PIPE_FILE , O_WRONLY );
							}
						} else
						{
							PERROR("Failed to open dummy file");
						}
						if (channel != dvb->channel)
						{
							INFO("Stopping previous DVB recording\n");
							dvb_recording_stop(pvr);
							if (dvb_recording_start( pvr, channel, service ) < 0)
								break;
						} else
						{
							dvb_write_status(pvr);
						}
						break;
					case 'n': // stop playiNg
					case 's': // stop recording
						if( dvb->fdplay > 0)
						{
							INFO("Stopping DVB playback - closing pipe\n");
							close(dvb->fdplay);
							dvb->fdplay = -1;
						}
						if(*cmd == 's')
						{
							dvb_recording_stop(pvr);
						} else
							dvb_write_status(pvr);
						break;
					case 'r': // record
						if( pvr->path[0] == 0 )
						{
							write_chunk("ee", 3);
							break;
						}
						channel = atoi( value );
						if( channel == dvb->channel )
							break;

						service = dvb_getService( channel );
						if ( service != NULL )
						{
							dvb_recording_stop(pvr);
							dvb_recording_start( pvr, channel, service );
						} else
						{
							ERROR( "PVR socket Thread - Wrong channel number %d for record", channel );
						}
						break;
					case 'u': // stream to udp
						channel = *value != 0 ? atoi( value ) : dvb->channel;
						INFO("Streaming ch=%d current=%d\n", channel, dvb->channel);
						service = dvb_getService( channel );
						if (service == NULL)
						{
							ERROR("Failed to find channel %d!", channel);
							dvb_write_status(pvr);
							break;
						}
						if (dvb->fdplay > 0)
						{
							close(dvb->fdplay);
							dvb->fdplay = -1;
							remove( STBPVR_PIPE_FILE );
						}
						if (channel != dvb->channel)
						{
							INFO("Stopping previous DVB recording\n");
							dvb_recording_stop(pvr);
						} else
							dvb_write_status(pvr);

						// Try to create UDP socket
						int sock = INVALID_SOCKET;
						do {
							struct sockaddr_in saddr;
							int value;

							sock = socket(AF_INET, SOCK_DGRAM, 0);
							if (sock == INVALID_SOCKET)
							{
								PERROR("Failed to create socket");
								break;
							}
							
							value = 1;
							if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) != 0)
							{
								PERROR("Failed to set socket options: SO_REUSEADDR");
								break;
							}
							value = 1;
							if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) != 0)
							{
								PERROR("Failed to set socket options: SO_BROADCAST");
								break;
							}

							memset(&saddr, 0, sizeof(struct sockaddr_in));
							saddr.sin_family      = AF_INET;
							saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
							saddr.sin_port        = 0;
							if (bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) != 0)
							{
								PERROR("Failed to bind socket");
								break;
							}
							memset(&saddr, 0, sizeof(struct sockaddr_in));
							saddr.sin_family      = AF_INET;
							saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
							saddr.sin_port        = htons(STBPVR_UDP_PORT);

							if (connect(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) != 0)
							{
								PERROR("Failed to connect socket");
								break;
							}
							dvb->fdplay = sock;
							INFO("Stream to %d (%s:%d)\n", dvb->play_only, inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
							dvb->play_only = 1;
						} while(0);
						if (dvb->play_only == 0)
						{
							closesocket(sock);
							write_chunk("eu", 3);
							break;
						}
						if (channel != dvb->channel)
						{
							if (dvb_recording_start( pvr, channel, service ) < 0)
								break;
						} else
						{
							dvb_write_status(pvr);
						}
						break;
				}
				break;
			case 'u': // IPTV record
				switch (*cmd)
				{
					case 'r':
					{
						url_desc_t url;
						struct in_addr ip;
						media_desc desc;
						char *str;

						if( pvr->path[0] == 0 )
						{
							write_chunk("ee", 3);
							break;
						}
						str = index(value, ' ');
						if( str && str[1] )
						{
							*str = 0;
							str++;
						}
						memset(&url, 0, sizeof(url));
						if( parseURL(value, &url) != 0 )
						{
							ERROR("Error parsing RTP url '%s'", value);
							break;
						}
						memset(&desc, 0, sizeof(desc));
						switch(url.protocol)
						{
							case mediaProtoUDP:
							case mediaProtoRTP:
								desc.fmt   = payloadTypeMpegTS;
								desc.type  = mediaTypeVideo;
								desc.port  = url.port;
								desc.proto = url.protocol;
								ip.s_addr = inet_addr(url.address);
							default:;
						}
						if( desc.fmt == 0 )
						{
							ERROR("Wrong RTP proto '%s'", value);
							break;
						}
						rtp_recording_stop(pvr);
						if( rtp_recording_start( pvr, &desc, &ip, str ) != 0 )
							break;
						break;
					}
					case 's':
					{
						INFO("Stopping RTP recording\n");
						rtp_recording_stop(pvr);
						break;
					}
					default: ;// ignore
				}
				break;
			case 'h': // HTTP
				switch(*cmd)
				{
					case 'r':
					{
						char *str;

						if( pvr->path[0] == 0 )
						{
							write_chunk("ee", 3);
							break;
						}

						if( strncasecmp( value, "http://", 7 ) != 0 &&
						    strncasecmp( value, "https://", 8 ) != 0 &&
						    strncasecmp( value, "ftp://", 6 ) != 0 )
						{
							ERROR("Error parsing HTTP url '%s'", value);
							break;
						}

						str = index(value, ' ');
						if( str != NULL && str[1] )
						{
							*str = 0;
							str++;
						}
						http_recording_stop(pvr);
						if( http_recording_start( pvr, value, str ) != 0 )
							break;
						break;
					}
					case 's':
					{
						INFO("Stopping HTTP recording\n");
						http_recording_stop(pvr);
					}
				}
				break;
			default: ;// ignore
		}
	}
	write_chunk("di", 3);
	write_chunk("ui", 3);
on_exit:

	if (clients.count > 0)
	{
		int t;
		for (t=0; t<MAX_CLIENTS; t++)
		{
			if (clients.sockets[t] != 0)
			{
				close(clients.sockets[t]);
   				clients.sockets[t] = 0;
				clients.count--;
			}
		}
	}
	if (socket_fd > 0)
	{
		close(socket_fd);
	}

	return NULL;
}

static int dvb_exportChannelList(int xspf)
{
	if (!dvb_services)
		return -1;
	FILE *fout = fopen("/tmp/channel_list.template", "w");
	if (!fout)
		return -2;
	int index = 0;
	list_element_t *service_element;
	for (service_element = dvb_services; service_element != NULL; service_element = service_element->next )
	{
		EIT_service_t *service = (EIT_service_t *)service_element->data;
		if (dvb_hasMediaType(service))
		{
			index++;
			fprintf(fout, (xspf ?
					"<track><title>%s</title><location>PROTO://IPADDR:PORT/dvb/%d</location><track>\n" :
					"#EXTINF:-1,%s\nPROTO://IPADDR:PORT/dvb/%d\n"),
					service->service_descriptor.service_name, index);
		}
	}
	fclose(fout);
	return 0;
}

int main(int argc, char **argv)
{
	int fd;
	pid_t app_pid;
	time_t current_time;
	list_element_t *job_element;
	pvrJob_t *job;
	char buf[BUFFER_SIZE];
	EIT_service_t *service = NULL;
	pvrInfo_t pvr;
	pthread_t socket_thread;
	(void)argc;

	if( (fd = open( STBPVR_PIDFILE, O_RDONLY)) >= 0 )
	{
		if( read( fd, buf, 24 ) > 0 )
		{
			app_pid = atoi(buf);
			if( app_pid > 0 && kill( app_pid, 0 ) == 0)
			{
				INFO( "%s: Another instance of %s is already running\n", __FUNCTION__, basename(argv[0]) );
				close(fd);
				return 0;
			}
		}
		close(fd);
	}

	memset(&pvr, 0, sizeof(pvr));

	pvr.rtp.desc.fmt = payloadTypeUnknown;
	pvr.dvb.channel  = STBPVR_DVB_CHANNEL_NONE;
	for ( pvr.dvb.vmsp=inputTuners-1; pvr.dvb.vmsp >= 0; pvr.dvb.vmsp-- )
	{
		/* Create the name of the tuner device */
		sprintf(buf, "/dev/dvb/adapter%d/frontend0", pvr.dvb.vmsp);
		if( helperFileExists(buf) )
		{
			break;
		}
	}
	if ( pvr.dvb.vmsp < 0 )
	{
		ERROR( "%s: Tuner not present!", __FUNCTION__ );
		//return 1;
	}

	if( (fd = open(STBPVR_PIDFILE, O_CREAT | O_WRONLY )) >= 0)
	{
		sprintf(buf, "%jd\n", (intmax_t)getpid());
		write( fd, buf, strlen(buf) );
		close( fd );
	}

	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGUSR1, sigusr_handler);
	signal(SIGPIPE, SIG_IGN);

	tzset();

	if( pthread_create (&socket_thread, NULL, pvr_socket_thread, (void*)&pvr) >= 0)
	{
		pthread_detach(socket_thread);
	} else
	{
		ERROR("%s: Can't start socket thread!", __FUNCTION__);
	}

	while( exit_app == 0 )
	{
		if( update_required )
		{
			pvrJob_t current_job;
			char     url[MAX_URL];

			INFO("%s: Updating...\n", __FUNCTION__);
			free_services(&dvb_services);
			loadAppSettings(&pvr);
			if (pvr.dvb.vmsp >= 0) // no need to waste time and memory, if we can't record DVB
			{
				services_load_from_dump(&dvb_services, CHANNEL_FILE_NAME);
				dvb_exportChannelList(1);
			}

			if( pvr.current_job != NULL )
			{
				//INFO("%s: pvr.current_job %p data %p\n", __func__, pvr.current_job, pvr.current_job->data);
				current_job = *((pvrJob_t*)pvr.current_job->data);
				switch( current_job.type )
				{
					case pvrJobTypeUnknown: break;
					case pvrJobTypeRTP:     break;
					case pvrJobTypeDVB:
						current_job.info.dvb.service = NULL; // no old pointers allowed!
						break;
					case pvrJobTypeHTTP:
						strcpy(url, current_job.info.http.url );
						current_job.info.http.url = url;
						break;
				}
			}

			/* Clears old job list */
			pvr_importJobList();

			if( pvr.current_job != NULL ) // old bad pointer
			{
				pvr.current_job = pvr_findJob( &current_job );
			}
			update_required = 0;

			pvr_write_status(&pvr);
		}

		time(&current_time);

		/* Check current job finished and need to be stopped */
		if( pvr.current_job_end > 0 && pvr.current_job_end <= current_time )
		{
			if( pvr.current_job != NULL )
			{
				job = (pvrJob_t*)pvr.current_job->data;
				pvr_jobprint( buf, sizeof(buf), job );

				pvr_deleteJob( pvr.current_job );
				pvr_exportJobList();
				pvr.current_job = NULL;

				INFO("%s: Stopping job: %s\n", __FUNCTION__, buf);
			} else
				INFO("%s: Stopping job %ld\n", __FUNCTION__, pvr.current_job_end);
			pvr_recording_stop(&pvr);
		}

		/* Check all available jobs and change their status according to current time */
		for( job_element = pvr_jobs;
			 job_element != NULL;
			 job_element = job_element == NULL ? NULL : job_element->next )
		{
			time(&current_time);
			//struct tm *t = localtime(&current_time);
			/*strftime( buf, sizeof(buf), "%T", t);
			INFO("Current time %s\n", str);*/
			job = (pvrJob_t*)job_element->data;
			//INFO("\n ======= testing job: job->start_time=%d current_time=%d(GMT%+d) end_time=%d\n",job->start_time, current_time, (int)timezone/3600 - ( t && t->tm_isdst > 0 ? 1 : 0 ), job->end_time);
			if( pvr.current_job_end == 0 && job->end_time < current_time )
			{ // Expired job
				pvr_jobprint( buf, sizeof(buf), job );

				INFO("%s: Expired job: %s\n", __FUNCTION__, buf);
				pvr_deleteJob( job_element );
				pvr_exportJobList();
				job_element = pvr_jobs;
				continue;
			}
			if( job->start_time-current_time > 0 && job->start_time-current_time < notifyTimeout && pvr.current_job == NULL)
			{ // Starts soon
				switch( job->type )
				{
					case pvrJobTypeUnknown:
						continue;
					case pvrJobTypeDVB:
						if( pvr.dvb.vmsp < 0 )
							continue;
						sprintf( buf, "do%d", job->info.dvb.channel );
						break;
					case pvrJobTypeRTP:
						sprintf( buf, "uo" );
						break;
					case pvrJobTypeHTTP:
						sprintf( buf, "ho" );
						break;
				}
				write_chunk( buf, strlen(buf)+1 );
			}
			/*if( job->start_time <= current_time && job->end_time > current_time )
			{
				pvr_jobprint( buf, sizeof(buf), job );
				INFO("Active job %s\n", buf);
			}*/
			if( pvr.current_job_end == 0 && job->start_time <= current_time && job->end_time > current_time )
			{ // Start now
				if( pvr.path[0] == 0 )
				{
					pvr_deleteJob( job_element );
					pvr_exportJobList();
					job_element = pvr_jobs;
					write_chunk("ee", 3);
					continue;
				}

				int res;

				pvr.current_job     = job_element;
				pvr.current_job_end = job->end_time;

				pvr_jobprint( buf, sizeof(buf), job );
				INFO("%s: Starting job: %s\n", __FUNCTION__, buf);

				switch( job->type )
				{
					case pvrJobTypeUnknown:
						res = -1;
						break;
					case pvrJobTypeDVB:
						if( pvr.dvb.vmsp < 0 )
							res = -1;
						else
							res = dvb_recording_start( &pvr, job->info.dvb.channel, service );
						break;
					case pvrJobTypeRTP:
						res = rtp_recording_start( &pvr, &job->info.rtp.desc, &job->info.rtp.ip, job->info.rtp.session_name );
						break;
					case pvrJobTypeHTTP:
						res = http_recording_start( &pvr, job->info.http.url, job->info.http.session_name );
						break;
				}
				if( res != 0 )
				{
					INFO("%s: Failed to start job: %s\n", __FUNCTION__, buf);

					pvr.current_job = NULL;
					pvr.current_job_end = 0;
					pvr_deleteJob( job_element );
					pvr_exportJobList();
					job_element = pvr_jobs;
					continue;
				}
			}
		}

		sleep(1);
	} //while( exit_app == 0 )
	INFO("%s: stopping\n", __FUNCTION__);
	pvr_recording_stop(&pvr);
	INFO("%s: exit\n", __FUNCTION__);
	unlink(STBPVR_PIDFILE);
	return 0;
}
