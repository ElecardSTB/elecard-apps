#if !defined(__APP_INFO_H)
#define __APP_INFO_H

/*
 app_info.h

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

/** @file app_info.h Runtime application settings
 */
/** @defgroup appinfo Global application run-time settings
 *  @ingroup StbMainApp
 *  @{
 */

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "defines.h"
#include "dvb_types.h"

#ifdef ENABLE_PVR
#include <StbPvr.h>
#endif

#include <directfb.h>
#include <directfb_strings.h>
#include <linux/dvb/frontend.h>
#include <limits.h>
#include <time.h>
#include <curl/curl.h>
#include <dirent.h>

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define URL_RTSP_MEDIA   "rtsp://"
#define URL_RTP_MEDIA    "rtp://"
#define URL_UDP_MEDIA    "udp://"
#define URL_IGMP_MEDIA   "igmp://"
#define URL_FILE_MEDIA   "file://"
#define URL_DVB_MEDIA    "dvb://"
#define URL_HTTP_MEDIA   "http://"
#define URL_HTTPS_MEDIA  "https://"
#define URL_RTMP_MEDIA   "rtmp" // rtmp/rtmpt/rtmpe

#define BUFFER_SIZE      (1024)
#define MAX_SIP_STRING   (78)
#define MAX_GRAPHICS_MODE_STRING (24)
#define MAX_URL          (PATH_MAX+7)
/* 7 == strlen("file://"); */

#define DIAG_ON          (1)
#define DIAG_OFF         (0)
#define DIAG_FORCED_OFF  (-1)

#define AUDIO_MPEG       ":MPEG"

#define CHANNEL_CUSTOM   (0xFFFF)

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef enum
{
	ifaceWAN = 0,
	ifaceLAN = 1,
#ifdef ENABLE_WIFI
	ifaceWireless,
#endif
#ifdef ENABLE_PPP
	ifacePPP,
#endif
} stb810_networkInterface_t;

typedef enum
{
	playlistModeNone = 0,
	playlistModeFavorites,
	playlistModeDLNA,
	playlistModeIPTV,
	playlistModeYoutube,
	playlistModeRutube,
#ifdef ENABLE_TELETES
	playlistModeTeletes,
#endif
} stb810_playlistMode;

typedef enum
{
	inputModeDirect, /**< Digit buttons inputs digits (virtual keypad not available) */
	inputModeABC     /**< Digit buttons selects letters (virtual keypad available) */
} stb810_inputMode;

typedef enum
{
	playback_single,
	playback_looped,
	playback_sequential,
	playback_random,
	playback_modes
} stb810_playbackMode;

#define PLAYBACK_MODE_NAMES { "single", "looped", "sequental", "random" }

typedef enum
{
	streamSourceNone = 0,
	streamSourceDVB,
	streamSourceIPTV,
	streamSourceVOD,
	streamSourceUSB,
	streamSourceFavorites,
	streamSourceDLNA,
	streamSourceYoutube,
	streamSourceRutube,
	streamSources
} stb810_streamSource;

#define STREAM_SOURCE_NAMES { \
	"none", \
	"DVB", \
	"IPTV", \
	"VOD", \
	"USB", \
	"Favorites", \
	"DLNA", \
	"YouTube", \
	"Rutube", \
	}

/* Trick Mode Speeds */
typedef enum
{
	speed_1_32,
	speed_1_16,
	speed_1_8,
	speed_1_4,
	speed_1_2,
	speed_1,
	speed_2,
	speed_4,
	speed_8,
	speed_16,
	speed_32,
	speedStates
} stb810_trickModeSpeed;

/* Trick Mode Direction */
typedef enum
{
	direction_none,
	direction_forwards,
	direction_backwards,
	direction_states,
} stb810_trickModeDirection;

/** Possible audio status
 */
typedef enum
{
	audioMute = 0, /**< Audio muted */
	audioMain,     /**< Normal audio playback */
	audioStates
} stb810_audioStatus;

/* Possible output screens */
typedef enum
{
	screenMain = 0,
	screenPip,
	screenOutputs
} screenOutput;

/* Video modes */
typedef enum
{
	videoMode_scale,
	videoMode_stretch,
#if (defined STB225)
	videoMode_native,
	//SergA:  FIXME: what with this??
	videoMode_custom,
#endif
	videoMode_count
} stb810_videoMode;

#if (!defined STB225)
#define VIDEO_MODE_NAMES { "scale", "stretch" }
#else
#define VIDEO_MODE_NAMES { "scale", "stretch", "native", "custom" }
#endif

#ifdef ENABLE_DVB
/* Possible input tuners */
typedef enum
{
	tunerNotPresent = 0,
	tunerInactive,
	tunerDVBMain,
	tunerDVBPvr,
	tunerStatusGaurd
} stb810_tunerStatus;

typedef enum
{
	signalStatusNoStatus = 0,
	signalStatusNoSignal,
	signalStatusBadSignal,
	signalStatusNewServices,
	signalStatusNoPSI,
	signalStatusBadQuality,
	signalStatusLowSignal,
	signalStatusBadCC,
	signalStatusNoProblems
} stb810_signalStatus;

typedef enum
{
	tunerDVBS = 1,
	tunerDVBC = 2,
	tunerDVBT = 4,
	tunerATSC = 8,
	tunerDVBT2 = 16,
	tunerMultistandard = 0x80,
} stb810_tunerCaps_t;

/* DVB tuner information */
typedef struct
{
	stb810_tunerStatus   status;
	fe_status_t          fe_status;
	fe_type_t            type;
	// Tuner info is always numbered from zero.
	// adapter field is used to describe the real port/adapter number
	// On ST if this index is greater than ADAPTER_COUNT, then it is a ST tuner
	int                  adapter;
	uint8_t              caps;
	uint32_t             ber;
	uint16_t             signal_strength;
	uint16_t             snr;
	uint32_t             uncorrected_blocks;
} stb810_tunerInfo;

/** DVB T input/display information
 */
typedef struct
{
	int                  active;
	tunerFormat          tuner; // points to tunerInfo
	int                  channel;
	int                  scrambled;
	int                  channelChange;
	int                  showInfo;
	int                  scanPSI;
	stb810_signalStatus  lastSignalStatus;
	stb810_signalStatus  savedSignalStatus;
	int                  reportedSignalStatus;
} stb810_dvbInfo;

typedef struct
{
	char channelConfigFile[256];
	int active;
	int channel;
} stb810_tvInfo;

typedef struct __stb810_dvbCommonInfo
{
	char                 channelConfigFile[256];
	int                  streamerInput;
	int                  adapterSpeed;
	int                  extendedScan;
	int                  networkScan;
} stb810_dvbCommonInfo;
#endif

// dgk
/* ip input/display information */
typedef struct __stb810_rtpInfo
{
	int                  active;
	char                 streamUrl[16];
	// dgk
	char                 streamInfoIP[16];
	// end dgk
	unsigned int         RTSPPort;
	char                 *pSDPDescription;
	char                 *pStreamName;
	char                 *pStreamDestIP;
	char                 *pStreamPort;
	char                 *pServerName;
} stb810_rtpInfo;

typedef enum
{
	iptvPlaylistSap = 0,
	iptvPlaylistUrl,
	iptvPlaylistFw,
} iptvPlaylistSource_t;

typedef struct __stb810_rtpMenuInfo
{
	int                  channel;
	int                  hasInternalPlaylist;
	int                  usePlaylistURL;
	char                 lastUrl[MAX_URL];
	char                 playlist[MAX_URL];
	char                 epg[MAX_URL];
	time_t               pidTimeout;
#ifdef ENABLE_TELETES
	char                 teletesPlaylist[MAX_URL];
#endif
} stb810_rtpMenuInfo;

typedef struct __stb810_rtspInfo
{
	int                  active;
	int                  usePlaylistURL;
	char                 streamIP[16];
	// dgk
	char                 streamInfoIP[16];
	char                 streamInfoUrl[MAX_URL];
	char                 **streamInfoFiles;
	char		         streamFile[1024];
	// end dgk
	unsigned int         RTSPPort;
	char                 *pSDPDescription;
	char                 *pStreamName;
	char                 *pStreamDestIP;
	char                 *pStreamPort;
	char                 *pServerName;
} stb810_rtspInfo;
// end dgk

#ifdef ENABLE_PVR
/* PVR recording information */
typedef struct __stb810_pvrInfo
{
	volatile int         active;
	dvbJobInfo_t         dvb;
	rtpJobInfo_t         rtp;
	httpJobInfo_t        http;
	char                 directory[PATH_MAX];
} stb810_pvrInfo;
#endif

/* Picture setting information */
typedef struct __stb810_pictureInfo
{
	int                  skinTone;
	int                  greenStretch;
	int                  blueStretch;

	int                  saturation;
	int                  contrast;
	int                  brightness;
} stb810_pictureInfo;

typedef enum __mediaType
{
	mediaVideo = 0,
	mediaAudio,
	mediaImage,
	mediaTypeCount,
	mediaAll   = -1,
} mediaType;

#define MEDIA_TYPE_NAMES { "video", "audio", "images" }

/* Media input/display information */
typedef struct __stb810_mediaInfo
{
	int                  active;
	int                  paused;
	int                  bHttp;
	char                 filename[PATH_MAX];
	char                 lastFile[PATH_MAX];
	int                  endOfStream;
	int                  endOfStreamReported;
	int                  currentFile;
	mediaType            typeIndex; // filter used in USB menu
#ifdef STBPNX
	int                (*fileSorting)(const void*, const void*);
#else
	int                (*fileSorting)(const struct dirent **, const struct dirent **);
#endif
#ifdef STB225
	int32_t              endOfStreamCountdown;
	int                  endOfFileReported;
	int                  endOfVideoDataReported;
	int                  endOfAudioDataReported;
	int                  audioPlaybackStarted;
	int                  videoPlaybackStarted;
	int                  bufferingData;
#endif
	stb810_playbackMode  playbackMode;
	DFBVideoProviderPlaybackFlags vidProviderPlaybackMode;
} stb810_mediaInfo;

typedef enum
{
	slideshowDisabled = 0, /**< image not showing at all */
	slideshowImage,        /**< single image is showing */
	slideshowShow,         /**< slideshow is showing */
	slideshowRandom        /**< images are showing in random order */
} stb810_slideshowMode;

#define SLIDESHOW_MODE_NAMES { "off", "image", "slideshow", "random" }

/* Image/slideshow display information */
typedef struct __stb810_slideshowInfo
{
	stb810_slideshowMode state;
	stb810_slideshowMode defaultState;
	int                  showingCover;
	int                  timeout;
	char                 filename[PATH_MAX];
} stb810_slideshowInfo;

/* Sound information */
typedef struct __stb810_soundInfo
{
	int                  fadeinVolume; // on playback start
	int                  muted;
	int                  volumeLevel;
	int                  rcaOutput;
	int                  hardwareRevision;
} stb810_soundInfo;

#ifdef STB225

#define VIDEO_MODE_MPEG2 "MPEG2"
#define VIDEO_MODE_H264  "H264"
#define VIDEO_MODE_AVS   "AVS"
#define VIDEO_MODE_MPEG4 "MPEG4"
#define VIDEO_MODE_DIVX  "DIVX"
#define VIDEO_MODE_NONE  "NONE"

#define AUDIO_MODE_MPEG  "MPEG"
#define AUDIO_MODE_AAC   "AAC"
#define AUDIO_MODE_AC3   "AC3"
#define AUDIO_MODE_MP3   "MP3"
#define AUDIO_MODE_NONE  "NONE"

/* Zoom Factor */
typedef enum __zoomFactor {
	zoomFactor_1,
	zoomFactor_2,
	zoomFactor_4,
	zoomFactor_8,
	zoomFactor_16
} zoomFactor_t;
#endif

/* Aspect Ratio modes */
typedef enum __aspectRatio {
	aspectRatio_4x3,
	aspectRatio_16x9
} aspectRatio_t;

/* Output format information */
typedef struct __stb810_outputInfo
{
	DFBScreenOutputConfig       config;
	DFBScreenOutputDescription  desc;
	DFBScreenOutputSignals      format;
	DFBScreenEncoderTVStandards standart;
	stb810_videoMode     autoScale;
	int                  numberOfEncoders;
	aspectRatio_t        aspectRatio;
	int                  bScreenFiltration;
#ifdef STB225
	zoomFactor_t         zoomFactor;
#endif
	DFBScreenEncoderConfig      encConfig[4];
	DFBScreenEncoderDescription encDesc[4];
	char                 graphicsMode[MAX_GRAPHICS_MODE_STRING];

#ifdef ENABLE_3D
	int                  content3d;
	int                  format3d;
	int                  use_factor;
	int                  use_offset;
	int                  factor;
	int                  offset;
#endif

} stb810_outputInfo;

/* Command Logging information */
typedef struct __stb810_commandInfo
{
	int                  loop;
	int                  inputFile;
	int                  outputFile;
} stb810_commandInfo;

typedef struct __stb810_dvbfeInfo
{
	uint32_t             lowFrequency;
	uint32_t             highFrequency;
	uint32_t             frequencyStep;
	int                  inversion;
} stb810_dvbfeInfo;

typedef struct __stb810_dvbtInfo
{
	stb810_dvbfeInfo     fe;
	fe_bandwidth_t       bandwidth;
	uint8_t              plp_id;
} stb810_dvbtInfo;

typedef struct __stb810_dvbcInfo
{
	stb810_dvbfeInfo     fe;
	fe_modulation_t      modulation;
	uint32_t             symbolRate;
} stb810_dvbcInfo;

typedef struct __stb810_atscInfo
{
	stb810_dvbfeInfo     fe;
	fe_modulation_t      modulation;
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
	diseqcSwitchType_t type;
	uint8_t port;       // 0..3
	uint8_t uncommited; // 0 - off, 1..16
} stb810_diseqcInfo_t;

typedef struct __stb810_dvbsInfo
{
	// NB: DVB-S treat this values as MHz
	stb810_dvbfeInfo     c_band;
	stb810_dvbfeInfo     k_band;
	uint32_t             symbolRate;
	fe_sec_voltage_t     polarization;
	stb810_dvbsBand_t    band;
	stb810_diseqcInfo_t  diseqc;
} stb810_dvbsInfo;

typedef enum __teletextStatus_t
{
	teletextStatus_disabled = 0,
	teletextStatus_begin,
	teletextStatus_processing,
	teletextStatus_demand,
	teletextStatus_finished,
	teletextStatus_ready
} teletextStatus_t;

typedef struct __stb810_teletextInfo
{
	int                  enabled;
	int                  exists;
	teletextStatus_t     status;
	int                  pageNumber;
	unsigned char        text[1000][25][40];
	unsigned char        subtitle[25][40];
	unsigned char        cyrillic[1000];
	int                  fresh[3];
	int                  freshCounter;
	int                  nextPage[3];
	int                  previousPage;
	unsigned char        time[14];
	int                  subtitlePage;
	int                  subtitleFlag;
} stb810_teletextInfo;

typedef enum __serviceSort_t
{
	serviceSortNone = 0,
	serviceSortAlpha,
	serviceSortType,
	serviceSortFreq,
	serviceSortCount
} serviceSort_t;

#define SERVICE_SORT_NAMES { \
	"none", \
	"alphabet", \
	"type", \
	"frequency" }

#define SCRAMBLED_OFF    0
#define SCRAMBLED_SHOW   1
#define SCRAMBLED_PLAY   2

typedef struct __stb810_offairInfo
{
	int                  dvbShowScrambled;
	char                 serviceList[MAX_URL];
	serviceSort_t        sorting;
	int                  tunerDebug;
	char                 profileLocation[128]; /**< Profile location file name */
	int                  wizardFinished;
#ifdef ENABLE_DVB_DIAG
	int                  diagnosticsMode;
#endif
} stb810_offairInfo;

#define VOIP_SERVER_NOTUSED      (0)
#define VOIP_SERVER_DISCONNECTED (1)
#define VOIP_SERVER_CONNECTED    (2)

typedef enum __voipStatus_t
{
	voipStatus_idle = 0,
	voipStatus_dialing,
	voipStatus_incoming,
	voipStatus_talking
} voipStatus_t;

typedef struct __stb810_voipInfo
{
	int                  enabled;
	volatile int         active;
	voipStatus_t         status;
	int                  connected;
	int                  buzzer;
	int                  expires;
	char                 login[MAX_SIP_STRING];
	char                 passwd[MAX_SIP_STRING];
	char                 realm[MAX_SIP_STRING];
	char                 server[256];
	char                 lastSip[MAX_URL];
} stb810_voipInfo;

#ifdef ENABLE_MULTI_VIEW
typedef struct __stb810_multiviewInfo
{
	int                  count;
	int                  selected;
	void                 *pArg[4];
	stb810_streamSource  source;
} stb810_multiviewInfo;
#endif

#ifdef ENABLE_MESSAGES
typedef struct __stb810_messagesInfo
{
	time_t               lastWatched;
	char*                newMessage;
} stb810_messagesInfo;
#endif

#ifdef ENABLE_VIDIMAX
typedef struct __stb810_vidimaxInfo
{
	int                 active;
	int					seeking;
} stb810_vidimaxInfo;
#endif

typedef enum {
	zoomStretch = 0,
	zoomScale,
	zoomFitWidth,
	zoomPresetsCount,
} zoomPreset_t;

/** Currently played stream characteristics
 */
typedef struct __stb810_playbackInfo
{
	mediaType            playingType;
	/** Playback module used to handle the stream */
	stb810_streamSource  streamSource;
	stb810_audioStatus   audioStatus;
	/** Source of the stream, used to determine navigation mode */
	stb810_playlistMode  playlistMode;
	int                  bAutoPlay;
	int                  bResumeAfterStart;
	int                  autoStop;
	float                scale;
	zoomPreset_t         zoom;
	int                  channel;
	char                 description[MENU_ENTRY_INFO_LENGTH];
	char                 thumbnail[MAX_URL];
} stb810_playbackInfo;

typedef struct __stb810_networkInfo
{
	char                 proxy[MENU_ENTRY_INFO_LENGTH]; // x.x.x.x:port
	char                 login[64];
	char                 passwd[64];
} stb810_networkInfo_t;

/** Global application settings
 */
typedef struct __stb810_controlInfo
{
	int		dvbApiVersion;
#ifdef ENABLE_DVB
	stb810_tunerInfo     tunerInfo[inputTuners];
	stb810_dvbInfo       dvbInfo;
	stb810_dvbtInfo      dvbtInfo;
	stb810_dvbcInfo      dvbcInfo;
	stb810_atscInfo			atscInfo;
	stb810_dvbsInfo      dvbsInfo;
	stb810_dvbCommonInfo dvbCommonInfo;
	stb810_teletextInfo  teletextInfo;
#endif
#ifdef ENABLE_ANALOGTV
	stb810_tvInfo        tvInfo;
#endif
	stb810_offairInfo    offairInfo;
#ifdef ENABLE_PVR
	stb810_pvrInfo       pvrInfo;
#endif
	// dgk
	stb810_rtpInfo       rtpInfo;
	stb810_rtspInfo      rtspInfo;
	stb810_rtpMenuInfo   rtpMenuInfo;
	// end dgk
	stb810_mediaInfo     mediaInfo;
	stb810_pictureInfo   pictureInfo;
	stb810_soundInfo     soundInfo;

	stb810_outputInfo    outputInfo;
	stb810_commandInfo   commandInfo;
	stb810_slideshowInfo slideshowInfo;
	stb810_voipInfo      voipInfo;
#ifdef ENABLE_MULTI_VIEW
	stb810_multiviewInfo multiviewInfo;
#endif
#ifdef ENABLE_MESSAGES
	stb810_messagesInfo  messagesInfo;
#endif
	stb810_playbackInfo  playbackInfo;
	stb810_networkInfo_t networkInfo;
	int                  watchdogEnabled;
	int                  bUseBufferModel;
	int                  bRendererDisableSync;
	int                  bProcessPCR;
#ifdef STB82
	int                  memSize;
#endif
	int                  inStandby;
	stb810_inputMode     inputMode;
#ifdef STB225
	uint32_t             restartResolution;
#endif
	int                  useVerimatrix;
	int                  useSecureMedia;
	
#ifdef ENABLE_VIDIMAX
	stb810_vidimaxInfo  vidimaxInfo;
#endif
} stb810_controlInfo;

typedef struct _stbTimeZoneDesc_t
{
	char                 file[32];
	char                 desc[128];
} stbTimeZoneDesc_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

/** Global application settings
 */
extern stb810_controlInfo appControlInfo;

extern char *globalFontDir;
extern char *globalFont;
extern char *globalSmallFont;
extern int   globalFontHeight;
extern int   globalSmallFontHeight;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
******************************************************************/
#ifdef __cplusplus
extern "C" {
#endif
/** Function used to initialise the application information data structure and load last saved settings
 * @sa loadAppSettings()
 * @sa loadVoipSettings()
 */
void appInfo_init(void);

/** Function used to load application settings from file
 * @return 0 on success
 * @sa saveAppSettings()
 */
int loadAppSettings(void);

/** Function used to load VoIP settings from file
 * @return 0 on success
 * @sa loadAppSettings()
 */
int loadVoipSettings(void);

/** Function used to save application and VoIP settings to file
 * @return 0 on success
 * @sa loadAppSettings()
 * @sa loadVoipSettings()
 */
int saveAppSettings(void);

#ifdef STSDK
int saveProxySettings(void);
#endif

void appInfo_setCurlProxy( CURL *curl );

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* __APP_INFO_H      Do not add any thing below this line */
