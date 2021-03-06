
/*
 app_info.c

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

#include "app_info.h"

#include "debug.h"
#include "interface.h"
#include "l10n.h"
#include "messages.h"
#include "playlist.h"
#include "media.h"
#include "helper.h"
#include "bouquet.h"
#include "dvbChannel.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/***********************************************
* EXPORTED DATA                                *
************************************************/

stb810_controlInfo appControlInfo;

/* This is needed because we send some of the components as libraries
   to customers */
char *globalFontDir = FONT_DIR;
char *globalFont = DEFAULT_FONT;
char *globalSmallFont = DEFAULT_SMALL_FONT;
int   globalFontHeight = DEFAULT_FONT_HEIGHT;
int   globalSmallFontHeight = DEFAULT_SMALL_FONT_HEIGHT;
char *globalInfoFiles[] = RTSP_SERVER_FILES;
const table_IntStr_t g_serviceSortNames[] = {
	//this is key for l10n_getText() (aka _T)
	{serviceSortNone,  "NONE"},
	{serviceSortAlpha, "SORT_ALPHA"},
	{serviceSortType,  "SORT_TYPE"},
	{serviceSortFreq,  "SORT_FREQUENCY"},

	//legacy names in settings
	{serviceSortNone,  "none"},
	{serviceSortAlpha, "alphabet"},
	{serviceSortType,  "type"},
	{serviceSortFreq,  "frequency"},

	TABLE_INT_STR_END_VALUE,
};


/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static const DirectFBScreenEncoderTVStandardsNames(tv_standards);
static const DirectFBScreenOutputSignalsNames(signals);
static const DirectFBScreenOutputResolutionNames(resolutions);
static const int signals_count         = sizeof(signals)/sizeof(signals[0]);
static const int tv_standards_count    = sizeof(tv_standards)/sizeof(tv_standards[0]);
static const char *streamSourceNames[] = STREAM_SOURCE_NAMES;
static const char *mediaTypeNames[]    = MEDIA_TYPE_NAMES;
static const char *playbackModeNames[] = PLAYBACK_MODE_NAMES;
static const char *slideshowModeNames[]= SLIDESHOW_MODE_NAMES;
static const char *videoModeNames[]    = VIDEO_MODE_NAMES;
#ifdef ENABLE_DVB
static const char *diseqcSwictNames[]  = DISEQC_SWITCH_NAMES;
#endif

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int loadVoipSettings()
{
	char buf[BUFFER_SIZE];
	FILE *fd;

	fd = fopen(SETTINGS_FILE, "rb");

	if (fd == NULL)
	{
		eprintf("AppSettings: Failed to open %s for reading\n", SETTINGS_FILE);
		return -1;
	}

	while (fgets(buf, BUFFER_SIZE, fd) != NULL)
	{
#ifdef ENABLE_VOIP
		if (sscanf(buf, "VOIP_ENABLE=%d", &appControlInfo.voipInfo.enabled) == 1)
		{
			//dprintf("%s: VoIP enable %d\n", __FUNCTION__, appControlInfo.voipInfo.enabled);
		} else if (sscanf(buf, "VOIP_SERVER=%[^\r\n]", appControlInfo.voipInfo.server) == 1)
		{
			//dprintf("%s: VoIP server %s\n", __FUNCTION__, appControlInfo.voipInfo.server);
		}
		else if (sscanf(buf, "VOIP_LOGIN=%[^\r\n]", appControlInfo.voipInfo.login) == 1)
		{
			//dprintf("%s: VoIP login %s\n", __FUNCTION__, appControlInfo.voipInfo.login);
		}
		else if (sscanf(buf, "VOIP_PASSWORD=%[^\r\n]", appControlInfo.voipInfo.passwd) == 1)
		{
			//dprintf("%s: VoIP password %s\n", __FUNCTION__, appControlInfo.voipInfo.passwd);
		}
		else if (sscanf(buf, "VOIP_REALM=%[^\r\n]", appControlInfo.voipInfo.realm) == 1)
		{
			//dprintf("%s: VoIP realm %s\n", __FUNCTION__, appControlInfo.voipInfo.realm);
		}
		else if (sscanf(buf, "VOIP_EXPIRE_TIME=%d", &appControlInfo.voipInfo.expires) == 1)
		{
			//dprintf("%s: VoIP expires %s\n", __FUNCTION__, appControlInfo.voipInfo.expires);
		}
#endif
	}

	fclose(fd);

	return 0;
}

int loadAppSettings()
{
	char buf[BUFFER_SIZE];
	char val[BUFFER_SIZE];
	FILE *fd;
	int res = 0;
	char cmd[1024];
	int i;

	fd = fopen(SETTINGS_FILE, "rb");

	if(fd == NULL) {
		eprintf("AppSettings: Failed to open %s for reading\n", SETTINGS_FILE);
		res = -1;
		goto set_overrides;
	}

	while (fgets(buf, BUFFER_SIZE, fd) != NULL)
	{
		//dprintf("%s: Read '%s'\n", __FUNCTION__, buf);
		if (sscanf(buf, "VODIP=%15s", appControlInfo.rtspInfo.streamIP) == 1)
		{
			//dprintf("%s: streamurl %s\n", __FUNCTION__, appControlInfo.rtspInfo.streamIP);
		} else if (sscanf(buf, "VODINFOIP=%15s", appControlInfo.rtspInfo.streamInfoIP) == 1)
		{
			//dprintf("%s: streaminfoip %s\n", __FUNCTION__, appControlInfo.rtspInfo.streamInfoIP);
		} else if (sscanf(buf, "VODINFOURL=%[^\r\n]", appControlInfo.rtspInfo.streamInfoUrl) == 1)
		{
			//dprintf("%s: streaminfourl %s\n", __FUNCTION__, appControlInfo.rtspInfo.streamInfoUrl);
		} else if (sscanf(buf, "VODUSEPLAYLIST=%d", &appControlInfo.rtspInfo.usePlaylistURL) == 1)
		{
			//dprintf("%s: use vod playlist %d\n", __FUNCTION__, appControlInfo.rtspInfo.usePlaylistURL);
		} else if (sscanf(buf, "USEPCR=%d", &appControlInfo.bProcessPCR) == 1)
		{
			//dprintf("%s: usepcr %d\n", __FUNCTION__, appControlInfo.bProcessPCR);
		} else if (sscanf(buf, "NORSYNC=%d", &appControlInfo.bRendererDisableSync) == 1)
		{
			//dprintf("%s: nosync %d\n", __FUNCTION__, appControlInfo.bRendererDisableSync);
		} else if (sscanf(buf, "BTRACK=%d", &appControlInfo.bUseBufferModel) == 1)
		{
			//dprintf("%s: btrack %d\n", __FUNCTION__, appControlInfo.bUseBufferModel);
		} else if (sscanf(buf, "OUTPUT=%[^\r\n ]", val ) == 1)
		{
			for( i = 0; i < signals_count; i++ )
				if( strcasecmp( val, signals[i].name ) == 0 )
				{
					appControlInfo.outputInfo.format = signals[i].signal;
					break;
				}
			//dprintf("%s: output %d\n", __FUNCTION__, appControlInfo.outputInfo.format);
		} else if (sscanf(buf, "STANDART=%[^\r\n ]", val ) == 1)
		{
			for( i = 0; i < tv_standards_count; i++ )
				if( strcasecmp( val, tv_standards[i].name ) == 0 )
				{
					appControlInfo.outputInfo.standart = tv_standards[i].standard;
					break;
				}
			//dprintf("%s: standart %d\n", __FUNCTION__, appControlInfo.outputInfo.standart);
		} else if (sscanf(buf, "ASPECTRATIO=%[^\r\n ]", val) == 1)
		{
			if( strcmp( val, "16:9" ) == 0 )
			{
				appControlInfo.outputInfo.aspectRatio = aspectRatio_16x9;
			} else
				appControlInfo.outputInfo.aspectRatio = aspectRatio_4x3;
			//dprintf("%s: aspectRatio %d\n", __FUNCTION__, appControlInfo.outputInfo.aspectRatio);
		} else if (sscanf(buf, "GRAPHICS_MODE=%[^\r\n ]", val) == 1)
		{
			strncpy(appControlInfo.outputInfo.graphicsMode, val, sizeof(appControlInfo.outputInfo.graphicsMode)-1);
			appControlInfo.outputInfo.graphicsMode[sizeof(appControlInfo.outputInfo.graphicsMode)-1]=0;
			//dprintf("%s: graphics mode %s\n", __FUNCTION__, appControlInfo.outputInfo.graphicsMode);
		}
#ifdef ENABLE_3D
		else if (sscanf(buf, "3D_MONITOR=%d", &interfaceInfo.enable3d) == 1)	{}
		else if (sscanf(buf, "3D_CONTENT=%d", &appControlInfo.outputInfo.content3d) == 1)	{}
		else if (sscanf(buf, "3D_FORMAT=%d", &appControlInfo.outputInfo.format3d) == 1)	{}
		else if (sscanf(buf, "3D_USE_FACTOR=%d", &appControlInfo.outputInfo.use_factor) == 1)	{}
		else if (sscanf(buf, "3D_USE_OFFSET=%d", &appControlInfo.outputInfo.use_offset) == 1)	{}
		else if (sscanf(buf, "3D_FACTOR=%d", &appControlInfo.outputInfo.factor) == 1)	{}
		else if (sscanf(buf, "3D_OFFSET=%d", &appControlInfo.outputInfo.offset) == 1)	{}
#endif
		else if (sscanf(buf, "AUDIO_OUTPUT=%[^\r\n]", val) == 1)
		{
			//dprintf("%s: aout %s\n", __FUNCTION__, val);
			if (strcmp("RCA", val) == 0)
			{
				appControlInfo.soundInfo.rcaOutput = 1;
			} else
			{
				appControlInfo.soundInfo.rcaOutput = 0;
			}
		} else if (sscanf(buf, "VOLUME=%d", &appControlInfo.soundInfo.volumeLevel) == 1)
		{
			//dprintf("%s: volume level %d\n", __FUNCTION__, appControlInfo.soundInfo.volumeLevel);
		}
		else if (sscanf(buf, "FADEIN_VOLUME=%d", &appControlInfo.soundInfo.fadeinVolume) == 1) {}
#ifdef ENABLE_DVB
		else if (sscanf(buf, "DVBCINVERSION=%d", &appControlInfo.dvbcInfo.fe.inversion) == 1)
		{
			//dprintf("%s: dvb inversion %d\n", __FUNCTION__, appControlInfo.dvbInversion);
		} else if (sscanf(buf, "DVBTINVERSION=%d", &appControlInfo.dvbtInfo.fe.inversion) == 1)
		{
			//dprintf("%s: dvb inversion %d\n", __FUNCTION__, appControlInfo.dvbInversion);
		} else if (sscanf(buf, "TUNERSPEED=%d", &appControlInfo.dvbCommonInfo.adapterSpeed) == 1)
		{
			//dprintf("%s: dvb tuner speed %d\n", __FUNCTION__, appControlInfo.dvbCommonInfo.adapterSpeed);
		} else if (sscanf(buf, "EXTENDEDSCAN=%d", &appControlInfo.dvbCommonInfo.extendedScan) == 1)
		{
			//dprintf("%s: dvb ext scan %d\n", __FUNCTION__, appControlInfo.dvbCommonInfo.extendedScan);
		}
		else if (sscanf(buf, "NETWORKSCAN=%d", &appControlInfo.dvbCommonInfo.networkScan) == 1)
		{
			//dprintf("%s: dvb net scan %d\n", __FUNCTION__, appControlInfo.dvbCommonInfo.networkScan);
		}
		else if (sscanf(buf, "DVBCLOWFREQUENCY=%u", &appControlInfo.dvbcInfo.fe.lowFrequency) == 1)
		{
			//dprintf("%s: dvb low freq %d\n", __FUNCTION__, appControlInfo.dvbLowFrequency);
		}
		else if (sscanf(buf, "DVBCHIGHFREQUENCY=%u", &appControlInfo.dvbcInfo.fe.highFrequency) == 1)
		{
			//dprintf("%s: dvb high freq %d\n", __FUNCTION__, appControlInfo.dvbHighFrequency);
		}
		else if (sscanf(buf, "DVBCFREQUENCYSTEP=%u", &appControlInfo.dvbcInfo.fe.frequencyStep) == 1)
		{
			//dprintf("%s: dvb freq step %d\n", __FUNCTION__, appControlInfo.dvbFrequencyStep);
		}
		else if (sscanf(buf, "DVBTLOWFREQUENCY=%u", &appControlInfo.dvbtInfo.fe.lowFrequency) == 1)
		{
			//dprintf("%s: dvb low freq %d\n", __FUNCTION__, appControlInfo.dvbLowFrequency);
		}
		else if (sscanf(buf, "DVBTHIGHFREQUENCY=%u", &appControlInfo.dvbtInfo.fe.highFrequency) == 1)
		{
			//dprintf("%s: dvb high freq %d\n", __FUNCTION__, appControlInfo.dvbHighFrequency);
		}
		else if (sscanf(buf, "DVBTFREQUENCYSTEP=%u", &appControlInfo.dvbtInfo.fe.frequencyStep) == 1)
		{
			//dprintf("%s: dvb freq step %d\n", __FUNCTION__, appControlInfo.dvbFrequencyStep);
		}
		else if (sscanf(buf, "DVBTBANDWIDTH=%u", (unsigned*)&appControlInfo.dvbtInfo.bandwidth) == 1)
		{
			//dprintf("%s: dvb freq step %d\n", __FUNCTION__, appControlInfo.dvbFrequencyStep);
		}
		else if (sscanf(buf, "QAMMODULATION=%u", (unsigned*)&appControlInfo.dvbcInfo.modulation) == 1)
		{
			//dprintf("%s: dvb modulation %d\n", __FUNCTION__, appControlInfo.dvbModulation);
		}
		else if (sscanf(buf, "QAMSYMBOLRATE=%u", &appControlInfo.dvbcInfo.symbolRate) == 1)
		{
			//dprintf("%s: dvb sym rate %lu\n", __FUNCTION__, appControlInfo.dvbSymbolRate);
		}
		else if (sscanf(buf, "SHOWSCRAMBLED=%d", &appControlInfo.offairInfo.dvbShowScrambled) == 1)
		{
			//dprintf("%s: show scrambled %d\n", __FUNCTION__, appControlInfo.offairInfo.dvbShowScrambled);
		}
		else if (sscanf(buf, "DISEQC_SWITCH=%[^\r\n ]", val ) == 1) {
			for (int i = 0; i < diseqcSwitchTypeCount; i++)
				if (!strcasecmp(diseqcSwictNames[i], val)) {
					appControlInfo.dvbsInfo.diseqc.type = i;
					break;
				}
		}
		else if (sscanf(buf, "DISEQC_PORT=%hhu", &appControlInfo.dvbsInfo.diseqc.port ) == 1) {}
		else if (sscanf(buf, "DISEQC_UNCOMMITED=%hhu", &appControlInfo.dvbsInfo.diseqc.uncommited) == 1) {}
		else if (sscanf(buf, "DVBSORTING=%[^\r\n ]", val ) == 1)
		{
			appControlInfo.offairInfo.sorting = table_IntStrLookupR(g_serviceSortNames, val, serviceSortNone);
			dvbChannel_sort(appControlInfo.offairInfo.sorting);
			//dprintf("%s: sorting %d\n", __FUNCTION__, appControlInfo.offairInfo.sorting);
		}
		else if (sscanf(buf, "DVBSERVICELIST=%[^\r\n]", appControlInfo.offairInfo.serviceList) == 1)
		{
			//dprintf("%s: service list %s\n", __FUNCTION__, appControlInfo.offairInfo.serviceList);
		}
		else if (sscanf(buf, "DVBSTARTCHANNEL=%d", &appControlInfo.offairInfo.startChannel) == 1) {}
		else if (sscanf(buf, "LASTDVBCHANNEL=%d", &appControlInfo.dvbInfo.channel) == 1)
		{
			//dprintf("%s: Last DVB channel %d\n", __FUNCTION__, appControlInfo.dvbInfo.channel);
		}
#endif /* ENABLE_DVB */
#ifdef ENABLE_VERIMATRIX
		else if (sscanf(buf, "USEVERIMATRIX=%d", &appControlInfo.useVerimatrix) == 1)
		{
			//dprintf("%s: verimatrix enable %d\n", __FUNCTION__, appControlInfo.useVerimatrix);
		}
#endif
#ifdef ENABLE_SECUREMEDIA
		else if (sscanf(buf, "USESECUREMEDIA=%d", &appControlInfo.useSecureMedia) == 1)
		{
			//dprintf("%s: securemedia enable %d\n", __FUNCTION__, appControlInfo.useSecureMedia);
		}
#endif
#ifdef ENABLE_MESSAGES
		else if (sscanf(buf, "LASTMESSAGE=%ld", &appControlInfo.messagesInfo.lastWatched) == 1)
		{
			//dprintf("%s: Last watched message %s\n", __FUNCTION__, ctime(appControlInfo.messagesInfo.lastWatched));
		}
#endif
		else if (sscanf(buf, "AUTOPLAY=%d", &appControlInfo.playbackInfo.bAutoPlay) == 1)
		{
			//dprintf("%s: Auto play in menu %d\n", __FUNCTION__, appControlInfo.playbackInfo.bResumeAfterStart);
		}
		else if (sscanf(buf, "AUTOSTOP=%d", &appControlInfo.playbackInfo.autoStop) == 1)
		{
			//dprintf("%s: Stop broadcasting %d\n", __FUNCTION__, appControlInfo.playbackInfo.autoStop);
		}
		else if (sscanf(buf, "RESUMEAFTERSTART=%d", &appControlInfo.playbackInfo.bResumeAfterStart) == 1)
		{
			//dprintf("%s: Resume after start %d\n", __FUNCTION__, appControlInfo.playbackInfo.bResumeAfterStart);
		}
		else if (sscanf(buf, "LASTSOURCE=%[^\r\n ]", val) == 1)
		{
			for( i = 0; i < streamSources; i++ )
				if( strcasecmp( val, streamSourceNames[i] ) == 0 )
				{
					appControlInfo.playbackInfo.streamSource = i;
					break;
				}
			//dprintf("%s: Last stream source %d\n", __FUNCTION__, appControlInfo.playbackInfo.streamSource);
		}
		else if (sscanf(buf, "LASTFILE=%[^\r\n]", appControlInfo.mediaInfo.lastFile) == 1)
		{
			//dprintf("%s: Last media file %s\n", __FUNCTION__, appControlInfo.mediaInfo.lastFile);
		}
		else if (sscanf(buf, "LASTRTP=%[^\r\n]", appControlInfo.rtpMenuInfo.lastUrl) == 1)
		{
			//dprintf("%s: Last rtp stream %s\n", __FUNCTION__, appControlInfo.rtpLastUrl);
		}
		else if (sscanf(buf, "LASTFAVORITE=%[^\r\n]", val) == 1)
		{
			playlist_setLastUrl(val);
			//dprintf("%s: Last rtp stream %s\n", __FUNCTION__, appControlInfo.rtpLastUrl);
		}
		else if (sscanf(buf, "RTPUSEPLAYLIST=%d", &appControlInfo.rtpMenuInfo.usePlaylistURL) == 1)
		{
			//dprintf("%s: rtp use playlist url %d\n", __FUNCTION__, appControlInfo.rtpMenuInfo.usePlaylistURL);
		}
		else if (sscanf(buf, "RTPPLAYLIST=%[^\r\n]", appControlInfo.rtpMenuInfo.playlist) == 1)
		{
			//dprintf("%s: rtp playlist url %s\n", __FUNCTION__, appControlInfo.rtpMenuInfo.playlist);
		}
#ifdef ENABLE_TELETES
		else if (sscanf(buf, "TELETESPLAYLIST=%[^\r\n]", appControlInfo.rtpMenuInfo.teletesPlaylist) == 1)
		{
			//dprintf("%s: teletes playlist url %s\n", __FUNCTION__, appControlInfo.rtpMenuInfo.teletesPlaylist);
		}
#endif
		else if (sscanf(buf, "RTPEPG=%[^\r\n]", appControlInfo.rtpMenuInfo.epg) == 1)
		{
			//dprintf("%s: rtp EPG url %s\n", __FUNCTION__, appControlInfo.rtpMenuInfo.epg);
		}
		else if (sscanf(buf, "RTPPIDTIMEOUT=%ld", &appControlInfo.rtpMenuInfo.pidTimeout) == 1)
		{
			//dprintf("%s: rtp pid timeout %ld\n", __FUNCTION__, appControlInfo.rtpMenuInfo.pidTimeout);
		}
#ifdef ENABLE_PVR
		else if (sscanf(buf, "PVRDIRECTORY=%[^\r\n]", appControlInfo.pvrInfo.directory) == 1)
		{
			//dprintf("%s: PVR directory %s\n", __FUNCTION__, appControlInfo.pvrInfo.directory);
		}
#endif
		else if (sscanf(buf, "LANGUAGE=%[^\r\n]", l10n_currentLanguage) == 1)
		{
			//dprintf("%s: Language selected %s\n", __FUNCTION__, l10n_currentLanguage);
		}
		else if (sscanf(buf, "MEDIA_FILTER=%[^\r\n ]", val) == 1)
		{
			for( i = 0; i < mediaTypeCount; i++ )
				if( strcasecmp( val, mediaTypeNames[i] ) == 0 )
				{
					appControlInfo.mediaInfo.typeIndex = i;
					break;
				}
			//dprintf("%s: Media type index %d\n", __FUNCTION__, appControlInfo.mediaInfo.typeIndex);
		}
		else if (sscanf(buf, "FILE_SORTING=%[^\r\n ]", val) == 1)
		{
			if (strcasecmp(val, "natural") == 0)
				appControlInfo.mediaInfo.fileSorting = naturalsort;
			else
				appControlInfo.mediaInfo.fileSorting = alphasort;
		}
		else if (sscanf(buf, "PLAYBACK_MODE=%[^\r\n ]", val) == 1)
		{
			for( i = 0; i < playback_modes; i++ )
				if( strcasecmp( val, playbackModeNames[i] ) == 0 )
				{
					appControlInfo.mediaInfo.playbackMode = i;
					break;
				}
			//dprintf("%s: Media playback mode %d\n", __FUNCTION__, appControlInfo.mediaInfo.playbackMode);
		}
		else if (sscanf(buf, "SLIDESHOW_MODE=%[^\r\n ]", val) == 1)
		{
			for( i = slideshowImage; i <= slideshowRandom; i++ )
				if( strcasecmp( val, slideshowModeNames[i] ) == 0 )
				{
					appControlInfo.slideshowInfo.defaultState = i;
					break;
				}
			//dprintf("%s: Slideshow mode %d\n", __FUNCTION__, appControlInfo.slideshowInfo.defaultState);
		}
		else if (sscanf(buf, "ANIMATION=%u", (unsigned *)&interfaceInfo.animation) == 1)
		{
			//dprintf("%s: Animation mode %d\n", __FUNCTION__, interfaceInfo.animation);
		}
		else if (sscanf(buf, "HIGHLIGHTCOLOR=%d", &interfaceInfo.highlightColor) == 1)
		{
			//dprintf("%s: Highlight color %d\n", __FUNCTION__, interfaceInfo.highlightColor);
		}
		else if (sscanf(buf, "PLAYCONTROL_TIMEOUT=%d", &interfacePlayControl.showTimeout) == 1)
		{
			//dprintf("%s: play control show timeout %d\n", __FUNCTION__, interfacePlayControl.showTimeout);
		}
		else if (sscanf(buf, "PLAYCONTROL_SHOWONSTART=%d", &interfacePlayControl.showOnStart) == 1)
		{
			//dprintf("%s: play control show on start %d\n", __FUNCTION__, interfacePlayControl.showOnStart);
		}
		else if (sscanf(buf, "SCALE_MODE=%[^\r\n ]", val) == 1)
		{
			for( i = 0; i < videoMode_count; i++ )
				if( strcasecmp( val, videoModeNames[i] ) == 0 )
				{
					appControlInfo.outputInfo.autoScale = i;
					break;
				}
			//dprintf("%s: scale mode %d\n", __FUNCTION__, appControlInfo.outputInfo.autoScale);
		}
		else if (sscanf(buf, "SCREEN_FILTRATION=%d", &appControlInfo.outputInfo.bScreenFiltration) == 1)
		{
			//dprintf("%s: Screen filtration %s\n", __FUNCTION__, appControlInfo.outputInfo.bScreenFiltration ? "on" : "off");
		}
		else if (sscanf(buf, "SATURATION=%d", &appControlInfo.pictureInfo.saturation) == 1)
		{
			//dprintf("%s: saturation %d\n", __FUNCTION__, appControlInfo.pictureInfo.saturation);
		}
		else if (sscanf(buf, "CONTRAST=%d", &appControlInfo.pictureInfo.contrast) == 1)
		{
			//dprintf("%s: contrast %d\n", __FUNCTION__, appControlInfo.pictureInfo.contrast);
		}
		else if (sscanf(buf, "BRIGHTNESS=%d", &appControlInfo.pictureInfo.brightness) == 1)
		{
			//dprintf("%s: brightness %d\n", __FUNCTION__, appControlInfo.pictureInfo.brightness);
		}
		else if (sscanf(buf, "PROFILE_LOCATION=%[^\r\n]", appControlInfo.offairInfo.profileLocation) == 1)
		{
			//dprintf("%s: location %s\n", __FUNCTION__, appControlInfo.offairInfo.profileLocation);
		}
		else if (sscanf(buf, "PROFILE_WIZARD_FINISHED=%d", &appControlInfo.offairInfo.wizardFinished) == 1)
		{
			//dprintf("%s: wizard finished %d\n", __FUNCTION__, appControlInfo.offairInfo.wizardFinished);
		}
		else if (sscanf(buf, "PLAYLIST_DIGITAL_NAME=%s", val) == 1)
		{
			bouquet_setCurrentName(eBouquet_digital, val);
			//dprintf("%s: wizard finished %s\n", __FUNCTION__, val);
		}
		else if (sscanf(buf, "PLAYLIST_ANALOG_NAME=%s", val) == 1)
		{
			bouquet_setCurrentName(eBouquet_analog, val);
			//dprintf("%s: wizard finished %s\n", __FUNCTION__, val);
		}
		else if (sscanf(buf, "PLAYLIST_ENABLE=%s", val) == 1)
		{
			if(strncasecmp(val, "YES", 3) == 0) {
				bouquet_setEnable(1);
			} else {
				bouquet_setEnable(0);
			}
			//dprintf("%s: wizard finished %s\n", __FUNCTION__, val);
		}
#ifdef ENABLE_DVB_DIAG
		else if (sscanf(buf, "PROFILE_DIAGNOSTICS=%d", &appControlInfo.offairInfo.diagnosticsMode) == 1)
		{
			//dprintf("%s: wizard diagnostics %d\n", __FUNCTION__, appControlInfo.offairInfo.diagnosticsMode);
		}
#endif
#ifdef ENABLE_VOIP
		else if (sscanf(buf, "VOIP_LASTSIP=%[^\r\n]", appControlInfo.voipInfo.lastSip) == 1)
		{
			//dprintf("%s: VoIP last SIP %s\n", __FUNCTION__, appControlInfo.voipInfo.lastSip);
		}
		else if (sscanf(buf, "VOIP_BUZZER=%d", &appControlInfo.voipInfo.buzzer ) == 1)
		{
			//dprintf("%s: VoIP buzzer %s\n", __FUNCTION__, appControlInfo.voipInfo.buzzer ? "on" : "off" );
		}
		else if (sscanf(buf, "VOIP_INDICATION=%d", &interfaceInfo.enableVoipIndication ) == 1)
		{
			//dprintf("%s: VoIP indication %d\n", __FUNCTION__, interfaceInfo.enableVoipIndication );
		}
#endif
#ifdef ENABLE_ANALOGTV
		else if (sscanf(buf, "ATVLOWFREQUENCY=%u", &appControlInfo.tvInfo.lowFrequency) == 1)
		{
			//dprintf("%s: Analog TV low frequency %s\n", __FUNCTION__, appControlInfo.tvInfo.lowFrequency);
		}
		else if (sscanf(buf, "ATVHIGHFREQUENCY=%u", &appControlInfo.tvInfo.highFrequency ) == 1)
		{
			//dprintf("%s: Analog TV high frequency %s\n", __FUNCTION__, appControlInfo.tvInfo.highFrequency );
		}
		else if (sscanf(buf, "ATVDELSYS=%d", &appControlInfo.tvInfo.delSys ) == 1)
		{
			//dprintf("%s: Analog TV delivery system %d\n", __FUNCTION__, appControlInfo.tvInfo.delSys );
		}
		else if (sscanf(buf, "ATVAUDIOMODE=%d", &appControlInfo.tvInfo.audioMode ) == 1)
		{
			//dprintf("%s: Analog TV audio mode %d\n", __FUNCTION__, appControlInfo.tvInfo.audioMode );
		}
		else if (sscanf(buf, "ATVCHANNELNAMESFILE=%[^\r\n]", appControlInfo.tvInfo.channelNamesFile ) == 1)
		{
			//dprintf("%s: Analog TV file with channel names %s\n", __FUNCTION__, appControlInfo.tvInfo.channelNamesFile );
		}
#endif
		else if(sscanf(buf, "YOUTUBE_SEARCH_NUM=%d", &appControlInfo.youtubeSearchNumber ) == 1) {

		} else if(sscanf(buf, "DEBUG=%d", &i) == 1) {
			dbg_setDebug(eDebug_gloabal, i);
		} else if(sscanf(buf, "DEBUG_BOUQUET=%d", &i) == 1) {
			dbg_setDebug(eDebug_bouquet, i);
		}
	}

	fclose(fd);

set_overrides:

	if (helperFileExists("/var/tmp/rev3"))
	{
		appControlInfo.soundInfo.hardwareRevision = 3;
	}

#ifdef STBPNX
	getParam(BROWSER_CONFIG_FILE, "HTTPProxyServer", "", appControlInfo.networkInfo.proxy);
	getParam(BROWSER_CONFIG_FILE, "HTTPProxyLogin",  "", appControlInfo.networkInfo.login);
	getParam(BROWSER_CONFIG_FILE, "HTTPProxyPasswd", "", appControlInfo.networkInfo.passwd);
#endif
#ifdef STSDK
	memset(appControlInfo.networkInfo.proxy,  0, sizeof(appControlInfo.networkInfo.proxy));
	memset(appControlInfo.networkInfo.login,  0, sizeof(appControlInfo.networkInfo.login));
	memset(appControlInfo.networkInfo.passwd, 0, sizeof(appControlInfo.networkInfo.passwd));

	fd = fopen(ELCD_PROXY_CONFIG_FILE, "r");
	if( fd != NULL )
	{
		char *ptr    = NULL;
		char *passwd = NULL;
		char *addr   = NULL;

		fgets(buf, sizeof(buf), fd);
		fclose(fd);

		// http://login:passwd@addr:port
		ptr = strstr(buf, "://");
		if( ptr )
			ptr+=3;
		else
			ptr = buf;
		addr = index(ptr, '@');
		if( addr )
		{
			*addr++ = 0;
			passwd = index(ptr, ':');
			if( passwd )
			{
				*passwd++ = 0;
				strncpy( appControlInfo.networkInfo.passwd, ptr, sizeof(appControlInfo.networkInfo.passwd) );
			}
			strncpy( appControlInfo.networkInfo.login, ptr, sizeof(appControlInfo.networkInfo.login) );
		}
		else
			addr = ptr;
		strncpy( appControlInfo.networkInfo.proxy, addr, sizeof(appControlInfo.networkInfo.proxy) );
	}
#endif
#if (defined i386) || (defined __x86_64__)
	{
		char *env = NULL;
		if ((env = getenv("http_proxy")))
			strncpy(appControlInfo.networkInfo.proxy,  env, sizeof(appControlInfo.networkInfo.proxy)-1);
		if ((env = getenv("http_proxy_login")))
			strncpy(appControlInfo.networkInfo.login,  env, sizeof(appControlInfo.networkInfo.login)-1);
		if ((env = getenv("http_proxy_passwd")))
			strncpy(appControlInfo.networkInfo.passwd, env, sizeof(appControlInfo.networkInfo.passwd)-1);
	}
#else

	if( appControlInfo.networkInfo.proxy[0] != 0 )
		setenv("http_proxy", appControlInfo.networkInfo.proxy, 1);
	else
		unsetenv("http_proxy");
	if( appControlInfo.networkInfo.login[0] != 0 )
	{
		setenv("http_proxy_login",  appControlInfo.networkInfo.login,  1);
		setenv("http_proxy_passwd", appControlInfo.networkInfo.passwd, 1);
	} else
	{
		unsetenv("http_proxy_login");
		unsetenv("http_proxy_passwd");
	}
#endif // i386 || x86_64

	/* Force video settings !!! */
	appControlInfo.bUseBufferModel = 0;
	appControlInfo.bRendererDisableSync = 1;
	appControlInfo.bProcessPCR = 1;

	/* Force audio settings !!! */
	appControlInfo.soundInfo.rcaOutput = 1;

	/* Force wildcard realm for voip */
	strcpy(appControlInfo.voipInfo.realm, "*");

	appControlInfo.inputMode = inputModeDirect;

	//dprintf("AppSettings: Profile Location: %s\n", appControlInfo.offairInfo.profileLocation);

	sprintf(cmd, PROFILE_LOCATIONS_PATH "/%s", appControlInfo.offairInfo.profileLocation);
	if (appControlInfo.offairInfo.profileLocation[0] == 0 || helperFileExists(cmd) == 0)
	{
		int res, i;
		struct dirent **locationFiles;
		int found = 0;

		//dprintf("AppSettings: No current location is set. Find any suitable...\n");
		appControlInfo.offairInfo.wizardFinished = 0;

		res = scandir(PROFILE_LOCATIONS_PATH, &locationFiles, NULL, alphasort);

		if (res > 0)
		{
			for (i=0;i<res; i++)
			{
				/* Skip if we have found suitable file or file is a directory */
				if ((locationFiles[i]->d_type & DT_DIR) || found)
				{
					free(locationFiles[i]);
					continue;
				}

				//dprintf("AppSettings: Test location %s\n", locationFiles[i]->d_name);

				sprintf(cmd, PROFILE_LOCATIONS_PATH "/%s", locationFiles[i]->d_name);
				if (getParam(cmd, "LOCATION_NAME", NULL, NULL))
				{
					strcpy(appControlInfo.offairInfo.profileLocation, locationFiles[i]->d_name);
					found = 1;
				}
				free(locationFiles[i]);
			}
			free(locationFiles);
		}
	}

#ifdef ENABLE_TEST_MODE
	appControlInfo.dvbtInfo.fe.lowFrequency  = 474000;
	appControlInfo.dvbtInfo.fe.highFrequency = 860000;
	appControlInfo.dvbtInfo.fe.frequencyStep = 192000;
	appControlInfo.dvbCommonInfo.adapterSpeed = 10;
	appControlInfo.soundInfo.volumeLevel = 100;

	appControlInfo.playbackInfo.bAutoPlay = 0;
	appControlInfo.playbackInfo.autoStop = 0;
	appControlInfo.playbackInfo.bResumeAfterStart = 0;

	system("cp -f /channels.conf " CHANNEL_FILE_NAME);
	system("cp -f /offair.conf " OFFAIR_SERVICES_FILENAME);

#endif // #ifdef ENABLE_TEST_MODE

	return res;
}

int saveAppSettings()
{
	//char buf[BUFFER_SIZE];
	FILE *fd;
	int i;
	char *description = appControlInfo.playbackInfo.description;
	char *thumbnail   = appControlInfo.playbackInfo.thumbnail;
	stb810_streamSource streamSource = appControlInfo.playbackInfo.streamSource;
// 	int dontUpdateSource = appControlInfo.mediaInfo.bHttp && 
// 	                       appControlInfo.playbackInfo.playlistMode != playlistModeIPTV && 
// 	                       appControlInfo.playbackInfo.streamSource != streamSourceYoutube;
// dont know why we couldnt save http source, so disable it
	int dontUpdateSource = 0;

	if( dontUpdateSource )
	{
		// Last URL will not be saved, so here is a trick to not break saved description and thumbnail
		char buf[MAX_URL];
		getParam(SETTINGS_FILE, "LASTDESCRIPTION", "", buf);
		description = dmalloc( strlen(buf)+1 );
		if( description != NULL )
			strcpy( description, buf );
		else
			description = appControlInfo.playbackInfo.description;
		getParam(SETTINGS_FILE, "LASTTHUMBNAIL", "", buf);
		thumbnail = dmalloc( strlen(buf)+1 );
		if( thumbnail != NULL )
			strcpy( thumbnail, buf );
		else
			thumbnail = appControlInfo.playbackInfo.thumbnail;

		getParam(SETTINGS_FILE, "LASTSOURCE", "", buf);
		for( i = 0; i < streamSources; i++ )
			if( strcasecmp( buf, streamSourceNames[i] ) == 0 )
			{
				streamSource = i;
				break;
			}
	}

	fd = fopen(SETTINGS_FILE ".tmp", "wb");

	//dprintf("AppSettings: save to %s\n", SETTINGS_FILE);

	if (fd == NULL)
	{
		eprintf("Failed to open %s for writing\n", SETTINGS_FILE);
		return -1;
	}
	fprintf(fd, "VODIP=%s\n",                     appControlInfo.rtspInfo.streamIP);
	fprintf(fd, "VODINFOIP=%s\n",                 appControlInfo.rtspInfo.streamInfoIP);
	fprintf(fd, "VODINFOURL=%s\n",                appControlInfo.rtspInfo.streamInfoUrl);
	fprintf(fd, "VODUSEPLAYLIST=%d\n",            appControlInfo.rtspInfo.usePlaylistURL);
	fprintf(fd, "AUDIO_OUTPUT=%s\n",              appControlInfo.soundInfo.rcaOutput == 0 ? "SCART" : "RCA");
	fprintf(fd, "BTRACK=%d\n",                    appControlInfo.bUseBufferModel);
	fprintf(fd, "NORSYNC=%d\n",                   appControlInfo.bRendererDisableSync);
	fprintf(fd, "USEPCR=%d\n",                    appControlInfo.bProcessPCR);
	for( i = 0; signals[i].signal != DSOS_NONE; i++ )
		if( signals[i].signal == appControlInfo.outputInfo.format )
		{
			fprintf(fd, "OUTPUT=%s\n", signals[i].name);
			break;
		}
	for( i = 0; tv_standards[i].standard != DSETV_UNKNOWN; i++ )
		if( tv_standards[i].standard == appControlInfo.outputInfo.standart )
		{
			fprintf(fd, "STANDART=%s\n", tv_standards[i].name);
			break;
		}
	fprintf(fd, "ASPECTRATIO=%s\n",               appControlInfo.outputInfo.aspectRatio == aspectRatio_16x9 ? "16:9" : "4:3");
	if (appControlInfo.outputInfo.graphicsMode[0])
	fprintf(fd, "GRAPHICS_MODE=%s\n",             appControlInfo.outputInfo.graphicsMode);
	fprintf(fd, "VOLUME=%d\n",                    appControlInfo.soundInfo.volumeLevel);
	fprintf(fd, "FADEIN_VOLUME=%d\n",             appControlInfo.soundInfo.fadeinVolume);
#ifdef ENABLE_DVB
	fprintf(fd, "TUNERSPEED=%d\n",                appControlInfo.dvbCommonInfo.adapterSpeed);
	fprintf(fd, "EXTENDEDSCAN=%d\n",              appControlInfo.dvbCommonInfo.extendedScan);
	fprintf(fd, "NETWORKSCAN=%d\n",               appControlInfo.dvbCommonInfo.networkScan);
	fprintf(fd, "DVBTLOWFREQUENCY=%u\n",          appControlInfo.dvbtInfo.fe.lowFrequency);
	fprintf(fd, "DVBTHIGHFREQUENCY=%u\n",         appControlInfo.dvbtInfo.fe.highFrequency);
	fprintf(fd, "DVBTFREQUENCYSTEP=%u\n",         appControlInfo.dvbtInfo.fe.frequencyStep);
	fprintf(fd, "DVBTINVERSION=%d\n",             appControlInfo.dvbtInfo.fe.inversion);
	fprintf(fd, "DVBTBANDWIDTH=%u\n",             appControlInfo.dvbtInfo.bandwidth);
	fprintf(fd, "DVBCLOWFREQUENCY=%u\n",          appControlInfo.dvbcInfo.fe.lowFrequency);
	fprintf(fd, "DVBCHIGHFREQUENCY=%u\n",         appControlInfo.dvbcInfo.fe.highFrequency);
	fprintf(fd, "DVBCFREQUENCYSTEP=%u\n",         appControlInfo.dvbcInfo.fe.frequencyStep);
	fprintf(fd, "DVBCINVERSION=%d\n",             appControlInfo.dvbcInfo.fe.inversion);
	fprintf(fd, "QAMMODULATION=%d\n",             appControlInfo.dvbcInfo.modulation);
	fprintf(fd, "QAMSYMBOLRATE=%u\n",             appControlInfo.dvbcInfo.symbolRate);
	fprintf(fd, "DISEQC_SWITCH=%s\n",             diseqcSwictNames[ appControlInfo.dvbsInfo.diseqc.type ]);
	fprintf(fd, "DISEQC_PORT=%u\n",               appControlInfo.dvbsInfo.diseqc.port);
	fprintf(fd, "DISEQC_UNCOMMITED=%u\n",         appControlInfo.dvbsInfo.diseqc.uncommited);
	fprintf(fd, "SHOWSCRAMBLED=%d\n",             appControlInfo.offairInfo.dvbShowScrambled);
	fprintf(fd, "DVBSORTING=%s\n",                table_IntStrLookup(g_serviceSortNames, appControlInfo.offairInfo.sorting, ""));
	fprintf(fd, "DVBSERVICELIST=%s\n",            appControlInfo.offairInfo.serviceList);
	fprintf(fd, "DVBSTARTCHANNEL=%d\n",           appControlInfo.offairInfo.startChannel);
	fprintf(fd, "LASTDVBCHANNEL=%d\n",            appControlInfo.dvbInfo.channel);
#endif
#ifdef ENABLE_PVR
	fprintf(fd, "PVRDIRECTORY=%s\n",              appControlInfo.pvrInfo.directory);
#endif
#ifdef ENABLE_VERIMATRIX
	fprintf(fd, "USEVERIMATRIX=%d\n",             appControlInfo.useVerimatrix);
#endif
#ifdef ENABLE_SECUREMEDIA
	fprintf(fd, "USESECUREMEDIA=%d\n",            appControlInfo.useSecureMedia);
#endif
#ifdef ENABLE_MESSAGES
	fprintf(fd, "LASTMESSAGE=%ld\n",              appControlInfo.messagesInfo.lastWatched);
#endif
	fprintf(fd, "AUTOPLAY=%d\n",                  appControlInfo.playbackInfo.bAutoPlay);
	fprintf(fd, "AUTOSTOP=%d\n",                  appControlInfo.playbackInfo.autoStop);
	fprintf(fd, "RESUMEAFTERSTART=%d\n",          appControlInfo.playbackInfo.bResumeAfterStart);
	if( dontUpdateSource == 0 && playlistModeFavorites == appControlInfo.playbackInfo.playlistMode )
		fprintf(fd, "LASTSOURCE=%s\n", streamSourceNames[streamSourceFavorites] );
	else
		fprintf(fd, "LASTSOURCE=%s\n", streamSourceNames[streamSource] );
	fprintf(fd, "LASTFILE=%s\n",                  appControlInfo.mediaInfo.lastFile);
	fprintf(fd, "LASTRTP=%s\n",                   appControlInfo.rtpMenuInfo.lastUrl);
	fprintf(fd, "LASTFAVORITE=%s\n",              playlist_getLastURL());
	fprintf(fd, "LASTDESCRIPTION=%s\n",           description);
	fprintf(fd, "LASTTHUMBNAIL=%s\n",             thumbnail);
	fprintf(fd, "RTPUSEPLAYLIST=%d\n",            appControlInfo.rtpMenuInfo.usePlaylistURL);
	fprintf(fd, "RTPPLAYLIST=%s\n",               appControlInfo.rtpMenuInfo.playlist);
#ifdef ENABLE_TELETES
	fprintf(fd, "TELETESPLAYLIST=%s\n",           appControlInfo.rtpMenuInfo.teletesPlaylist);
#endif
	fprintf(fd, "RTPEPG=%s\n",                    appControlInfo.rtpMenuInfo.epg);
	fprintf(fd, "RTPPIDTIMEOUT=%ld\n",            appControlInfo.rtpMenuInfo.pidTimeout);
	fprintf(fd, "LANGUAGE=%s\n",                  l10n_currentLanguage);
	fprintf(fd, "MEDIA_FILTER=%s\n",              appControlInfo.mediaInfo.typeIndex < 0 ?
	                                                "showall" : mediaTypeNames[appControlInfo.mediaInfo.typeIndex] );
	fprintf(fd, "FILE_SORTING=%s\n",              appControlInfo.mediaInfo.fileSorting == naturalsort ? "natural" : "alpha" );
	fprintf(fd, "PLAYBACK_MODE=%s\n",             playbackModeNames[appControlInfo.mediaInfo.playbackMode]);
	fprintf(fd, "SLIDESHOW_MODE=%s\n",            slideshowModeNames[appControlInfo.slideshowInfo.defaultState]);
	fprintf(fd, "ANIMATION=%d\n",                 interfaceInfo.animation);
	fprintf(fd, "HIGHLIGHTCOLOR=%d\n",            interfaceInfo.highlightColor);
	fprintf(fd, "PLAYCONTROL_TIMEOUT=%d\n",       interfacePlayControl.showTimeout);
	fprintf(fd, "PLAYCONTROL_SHOWONSTART=%d\n",   interfacePlayControl.showOnStart);
	fprintf(fd, "SCALE_MODE=%s\n",                videoModeNames[appControlInfo.outputInfo.autoScale]);
	fprintf(fd, "SCREEN_FILTRATION=%d\n",         appControlInfo.outputInfo.bScreenFiltration);
	fprintf(fd, "SATURATION=%d\n",                appControlInfo.pictureInfo.saturation);
	fprintf(fd, "CONTRAST=%d\n",                  appControlInfo.pictureInfo.contrast);
	fprintf(fd, "BRIGHTNESS=%d\n",                appControlInfo.pictureInfo.brightness);
	fprintf(fd, "PROFILE_LOCATION=%s\n",          appControlInfo.offairInfo.profileLocation);
	fprintf(fd, "PROFILE_WIZARD_FINISHED=%d\n",   appControlInfo.offairInfo.wizardFinished);
	fprintf(fd, "PLAYLIST_DIGITAL_NAME=%s\n",     bouquet_getCurrentName(eBouquet_digital) ? bouquet_getCurrentName(eBouquet_digital) : "");
	fprintf(fd, "PLAYLIST_ANALOG_NAME=%s\n",      bouquet_getCurrentName(eBouquet_analog) ? bouquet_getCurrentName(eBouquet_analog) : "");
	fprintf(fd, "PLAYLIST_ENABLE=%s\n",           bouquet_isEnable() ? "YES" : "NO");
#ifdef ENABLE_DVB_DIAG
	fprintf(fd, "PROFILE_DIAGNOSTICS=%d\n",       appControlInfo.offairInfo.diagnosticsMode == DIAG_FORCED_OFF ?
	                                                DIAG_FORCED_OFF : DIAG_ON);
#endif
#ifdef ENABLE_VOIP
	fprintf(fd, "VOIP_ENABLE=%d\n",               appControlInfo.voipInfo.enabled);
	fprintf(fd, "VOIP_SERVER=%s\n",               appControlInfo.voipInfo.server);
	fprintf(fd, "VOIP_LOGIN=%s\n",                appControlInfo.voipInfo.login);
	fprintf(fd, "VOIP_PASSWORD=%s\n",             appControlInfo.voipInfo.passwd);
	fprintf(fd, "VOIP_REALM=%s\n",                appControlInfo.voipInfo.realm);
	fprintf(fd, "VOIP_LASTSIP=%s\n",              appControlInfo.voipInfo.lastSip);
	fprintf(fd, "VOIP_BUZZER=%d\n",               appControlInfo.voipInfo.buzzer);
	fprintf(fd, "VOIP_EXPIRE_TIME=%d\n",          appControlInfo.voipInfo.expires);
	fprintf(fd, "VOIP_INDICATION=%d\n",           interfaceInfo.enableVoipIndication);
#endif
#ifdef ENABLE_3D
	fprintf(fd, "3D_MONITOR=%d\n",			interfaceInfo.enable3d);
	fprintf(fd, "3D_CONTENT=%d\n",			appControlInfo.outputInfo.content3d);
	fprintf(fd, "3D_FORMAT=%d\n",			appControlInfo.outputInfo.format3d);
	fprintf(fd, "3D_USE_FACTOR=%d\n",		appControlInfo.outputInfo.use_factor);
	fprintf(fd, "3D_USE_OFFSET=%d\n",		appControlInfo.outputInfo.use_offset);
	fprintf(fd, "3D_FACTOR=%d\n",			appControlInfo.outputInfo.factor);
	fprintf(fd, "3D_OFFSET=%d\n",			appControlInfo.outputInfo.offset);
#endif
#ifdef ENABLE_ANALOGTV
	fprintf(fd, "ATVLOWFREQUENCY=%u\n",      	appControlInfo.tvInfo.lowFrequency);
	fprintf(fd, "ATVHIGHFREQUENCY=%u\n",     	appControlInfo.tvInfo.highFrequency);
	fprintf(fd, "ATVDELSYS=%d\n",          		appControlInfo.tvInfo.delSys);
	fprintf(fd, "ATVAUDIOMODE=%d\n",         	appControlInfo.tvInfo.audioMode);
	fprintf(fd, "ATVCHANNELNAMESFILE=%s\n",         	appControlInfo.tvInfo.channelNamesFile);
#endif
	fprintf(fd, "YOUTUBE_SEARCH_NUM=%d\n",           appControlInfo.youtubeSearchNumber);
	fprintf(fd, "DEBUG=%d\n",         dbg_getDebug(eDebug_gloabal));
	fprintf(fd, "DEBUG_BOUQUET=%d\n", dbg_getDebug(eDebug_bouquet));
	fclose(fd);
	rename( SETTINGS_FILE ".tmp", SETTINGS_FILE );

	if( description != appControlInfo.playbackInfo.description )
		dfree( description );
	if( thumbnail != appControlInfo.playbackInfo.thumbnail )
		dfree( thumbnail );

	return 0;
}

#ifdef STSDK
int saveProxySettings(void)
{
	FILE *f = fopen(ELCD_PROXY_CONFIG_FILE, "w");
	if( f == NULL )
		return -1;

	fputs("http://", f);
	if( appControlInfo.networkInfo.login[0] )
	{
		fputs(appControlInfo.networkInfo.login, f);
		if( appControlInfo.networkInfo.passwd[0] )
		{
			fputc(':', f);
			fputs(appControlInfo.networkInfo.passwd, f);
		}
		fputc('@', f);
	}
	fputs(appControlInfo.networkInfo.proxy, f);
	fclose(f);
	return 0;
}
#endif

void appInfo_init(void)
{
	appControlInfo.pictureInfo.skinTone           = 0;
	appControlInfo.pictureInfo.greenStretch       = 0;
	appControlInfo.pictureInfo.blueStretch        = 0;

	appControlInfo.pictureInfo.brightness         = -1;
	appControlInfo.pictureInfo.contrast           = -1;
	appControlInfo.pictureInfo.saturation         = -1;
	appControlInfo.mediaInfo.vidProviderPlaybackMode = 0;

	appControlInfo.mediaInfo.typeIndex            = mediaAll;
	appControlInfo.mediaInfo.active               = 0;
	appControlInfo.mediaInfo.endOfStream          = 0;
	appControlInfo.mediaInfo.playbackMode         = playback_sequential;
	appControlInfo.mediaInfo.fileSorting          = alphasort;

	appControlInfo.slideshowInfo.defaultState     = slideshowShow;
	appControlInfo.slideshowInfo.state            = 0;
	appControlInfo.slideshowInfo.showingCover     = 0;
	appControlInfo.slideshowInfo.timeout          = 3000;
	appControlInfo.slideshowInfo.filename[0]      = 0;

#ifdef STB225
	appControlInfo.mediaInfo.endOfStreamCountdown = 0;
	appControlInfo.mediaInfo.endOfStreamReported  = 0;
	appControlInfo.mediaInfo.endOfFileReported    = 0;
	appControlInfo.mediaInfo.endOfVideoDataReported = 0;
	appControlInfo.mediaInfo.endOfAudioDataReported = 0;
	appControlInfo.mediaInfo.audioPlaybackStarted = 0;
	appControlInfo.mediaInfo.videoPlaybackStarted = 0;
	appControlInfo.mediaInfo.bufferingData        = 0;
#endif
	appControlInfo.soundInfo.muted                = 0;
#ifdef ENABLE_FUSION
	appControlInfo.soundInfo.volumeLevel = 0;
	appControlInfo.soundInfo.muted                = 1;
#else
	appControlInfo.soundInfo.volumeLevel          = 100;
#endif
	appControlInfo.soundInfo.rcaOutput            = 1;
	appControlInfo.soundInfo.hardwareRevision     = 1;
	appControlInfo.outputInfo.autoScale           = videoMode_scale;
#ifdef STB225
	appControlInfo.outputInfo.aspectRatio         = aspectRatio_16x9;
#else
	appControlInfo.outputInfo.aspectRatio         = aspectRatio_4x3;
#endif
	appControlInfo.outputInfo.format              = 0xFFFFFFFF;
	appControlInfo.outputInfo.standart            = 0xFFFFFFFF;
	appControlInfo.outputInfo.bScreenFiltration   = 0;
	appControlInfo.outputInfo.graphicsMode[0]     = 0;
#ifdef ENABLE_3D
	interfaceInfo.enable3d	                  = 0;
	appControlInfo.outputInfo.content3d	      = 0; 
	appControlInfo.outputInfo.format3d	      = 0;
	appControlInfo.outputInfo.use_factor	      = 0;
	appControlInfo.outputInfo.use_offset	      = 0;
	appControlInfo.outputInfo.factor	      = 64;
	appControlInfo.outputInfo.offset	      = 128;
#endif
	appControlInfo.commandInfo.loop               = 0;
	appControlInfo.commandInfo.inputFile          = 0;
	appControlInfo.commandInfo.outputFile         = 0;

#ifdef ENABLE_DVB
	appControlInfo.dvbCommonInfo.adapterSpeed     = 4;
	appControlInfo.dvbCommonInfo.extendedScan     = 0;
	appControlInfo.dvbCommonInfo.networkScan      = 1;
	appControlInfo.dvbCommonInfo.streamerInput    = 0;
	strcpy(appControlInfo.dvbCommonInfo.channelConfigFile, CHANNEL_FILE_NAME);

	appControlInfo.dvbtInfo.fe.lowFrequency       = 410000;
	appControlInfo.dvbtInfo.fe.highFrequency      = 850000;
	appControlInfo.dvbtInfo.fe.frequencyStep      =   8000;
	appControlInfo.dvbtInfo.fe.inversion          = 0;
	appControlInfo.dvbtInfo.bandwidth             = BANDWIDTH_8_MHZ;

#ifndef STSDK
	appControlInfo.dvbcInfo.fe.lowFrequency       = 410000;
	appControlInfo.dvbcInfo.fe.highFrequency      = 850000;
	appControlInfo.dvbcInfo.fe.frequencyStep      =   8000;
#else
	// STB 840 Avit
	appControlInfo.dvbcInfo.fe.lowFrequency       =  51000;
	appControlInfo.dvbcInfo.fe.highFrequency      = 860000;
	appControlInfo.dvbcInfo.fe.frequencyStep      =   8000;
#endif
#ifdef ENABLE_ANALOGTV
//	strcpy(appControlInfo.tvInfo.channelConfigFile, ANALOGTV_CHANNEL_FILE);
	strcpy(appControlInfo.tvInfo.channelNamesFile, "");
	appControlInfo.tvInfo.active	= 0;
	appControlInfo.tvInfo.id		= 0;
	appControlInfo.tvInfo.highFrequency	= 960000;
	appControlInfo.tvInfo.lowFrequency		= 43000;
	appControlInfo.tvInfo.delSys	= 0;
	appControlInfo.tvInfo.audioMode		= 0;
#endif
	appControlInfo.dvbcInfo.fe.inversion          = 0;
	appControlInfo.dvbcInfo.modulation            = QAM_64;
	appControlInfo.dvbcInfo.symbolRate            = 6875;

	appControlInfo.atscInfo.fe.lowFrequency       =  54000;
	appControlInfo.atscInfo.fe.highFrequency      = 858000;
	appControlInfo.atscInfo.fe.frequencyStep      =   6000;
	appControlInfo.atscInfo.fe.inversion          = 0;
	appControlInfo.atscInfo.modulation            = VSB_8;

	appControlInfo.dvbsInfo.c_band.lowFrequency   =  3700;
	appControlInfo.dvbsInfo.c_band.highFrequency  =  4200;
	appControlInfo.dvbsInfo.k_band.lowFrequency   = 10700;
	appControlInfo.dvbsInfo.k_band.highFrequency  = 12750;
	appControlInfo.dvbsInfo.symbolRate            = 22000;
	appControlInfo.dvbsInfo.band                  = dvbsBandK;
	appControlInfo.dvbsInfo.polarization          = 1;

	appControlInfo.dvbInfo.active     = 0;
	appControlInfo.dvbInfo.adapter    = 0;
	appControlInfo.dvbInfo.scrambled  = 0;
	appControlInfo.dvbInfo.channel    = CHANNEL_CUSTOM;
	appControlInfo.dvbInfo.showInfo   = 0;

	appControlInfo.offairInfo.dvbShowScrambled    = 0;
	appControlInfo.offairInfo.sorting             = serviceSortNone;
	appControlInfo.offairInfo.serviceList[0]      = 0;
	appControlInfo.offairInfo.tunerDebug          = 0;
	appControlInfo.offairInfo.startChannel        = 0;
	appControlInfo.offairInfo.previousChannel     = 0;
#endif
#ifdef ENABLE_PVR
	appControlInfo.pvrInfo.directory[0]           = 0;
	appControlInfo.pvrInfo.active                 = 0;
	appControlInfo.pvrInfo.dvb.channel            = STBPVR_DVB_CHANNEL_NONE;
	appControlInfo.pvrInfo.dvb.service            = NULL;
	appControlInfo.pvrInfo.dvb.event_id           = -1;
	memset(&appControlInfo.pvrInfo.rtp, 0, sizeof(appControlInfo.pvrInfo.rtp));
	appControlInfo.pvrInfo.http.url               = NULL;
#endif
	appControlInfo.watchdogEnabled = 1;

	appControlInfo.playbackInfo.playlistMode      = playlistModeNone;
	appControlInfo.playbackInfo.bAutoPlay         = 1;
	appControlInfo.playbackInfo.autoStop          = 0;
	appControlInfo.playbackInfo.bResumeAfterStart = 0;
	appControlInfo.playbackInfo.streamSource      = streamSourceNone;
	appControlInfo.playbackInfo.audioStatus       = audioMute;
	appControlInfo.playbackInfo.scale             = 1.0;
	appControlInfo.playbackInfo.channel           = -1;

	strcpy(appControlInfo.rtpMenuInfo.lastUrl,      "rtp://");
	appControlInfo.rtpMenuInfo.playlist[0]        = 0;
	appControlInfo.rtpMenuInfo.pidTimeout         = 3;
	appControlInfo.rtpMenuInfo.hasInternalPlaylist=helperFileExists(IPTV_FW_PLAYLIST_FILENAME);
	if (appControlInfo.rtpMenuInfo.hasInternalPlaylist)
		appControlInfo.rtpMenuInfo.usePlaylistURL = iptvPlaylistFw;

	appControlInfo.inStandby                      = 0;

	appControlInfo.bUseBufferModel                = 0;
	appControlInfo.bRendererDisableSync           = 0;
	appControlInfo.bProcessPCR                    = 1;

	strcpy(appControlInfo.rtspInfo.streamIP,     RTSP_SERVER_ADDR);
	strcpy(appControlInfo.rtspInfo.streamInfoIP, RTSP_SERVER_ADDR);
	appControlInfo.rtspInfo.streamInfoUrl[0]   = 0;
	appControlInfo.rtspInfo.streamInfoFiles    = globalInfoFiles;
	appControlInfo.rtspInfo.RTSPPort           = RTSP_SERVER_PORT;
	strcpy(appControlInfo.rtspInfo.streamFile,   RTSP_STREAM_FILE);

#ifdef STB82
	/* get rank1 limit (it is ether equals to rank0 limit or is greater) and convert it to megabytes */
	appControlInfo.memSize                        = (helperParseMmio(0x65018)+1)>>20;
#endif
#ifdef STB225
	appControlInfo.restartResolution              = 0;
#endif
	appControlInfo.useVerimatrix                  = 0;
	appControlInfo.useSecureMedia                 = 0;

	appControlInfo.voipInfo.enabled               = 1;
	appControlInfo.voipInfo.active                = 0;
	appControlInfo.voipInfo.status                = voipStatus_idle;
	appControlInfo.voipInfo.buzzer                = 1;
	appControlInfo.voipInfo.connected             = 0;
	appControlInfo.voipInfo.server[0]             = 0;
	appControlInfo.voipInfo.login[0]              = 0;
	appControlInfo.voipInfo.passwd[0]             = 0;
	appControlInfo.voipInfo.realm[0]              = 0;

#ifdef ENABLE_MESSAGES
	appControlInfo.messagesInfo.lastWatched       = 0;
#endif

#ifdef ENABLE_VOIP
	interfaceInfo.enableVoipIndication            = 1;
#endif

	appControlInfo.offairInfo.profileLocation[0]  = 0;
	appControlInfo.offairInfo.wizardFinished      = 0;
#ifdef ENABLE_DVB_DIAG
	appControlInfo.offairInfo.diagnosticsMode     = DIAG_ON;
#endif

	interfaceInfo.highlightColor                  = 0;
	interfacePlayControl.showTimeout              = 5;
	interfacePlayControl.showOnStart              = 1;

	appControlInfo.youtubeSearchNumber            = 5;

	loadAppSettings();
	loadVoipSettings();

#ifdef ENABLE_MESSAGES
	appControlInfo.messagesInfo.newMessage = messages_checkNew();
#endif

#ifdef ENABLE_VIDIMAX
	appControlInfo.vidimaxInfo.active = 0;
#endif
}

void appInfo_setCurlProxy( CURL *curl )
{
	curl_easy_setopt(curl, CURLOPT_PROXY, appControlInfo.networkInfo.proxy);
	if( appControlInfo.networkInfo.login[0] )
	{
		if( appControlInfo.networkInfo.passwd[0] )
		{
			char userpwd[sizeof(appControlInfo.networkInfo.login)+sizeof(appControlInfo.networkInfo.passwd)];
			sprintf(userpwd, "%s:%s", appControlInfo.networkInfo.login, appControlInfo.networkInfo.passwd);
			curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, userpwd);
		}
		else
			curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, appControlInfo.networkInfo.login);
	}
}
