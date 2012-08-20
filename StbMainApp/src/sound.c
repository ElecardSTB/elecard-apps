
/*
 sound.c

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

#include "sound.h"

#include "app_info.h"
#include "debug.h"
#include "sem.h"
#include "gfx.h"
#include "interface.h"
#include "StbMainApp.h"
#include "stsdk.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifdef STBPNX
#include <alsa/asoundlib.h>
#endif

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define SOUND_VOLUME_STEPS (25)

#define FADEIN_SPEED	5

#if defined (ENABLE_VIDIMAX) || defined( STSDK )
//disable fade in effect
#define FADEIN_STEPS	0
#endif

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int  sound_callback(interfaceSoundControlAction_t action, void *pArg);
static long sound_getVolume(void);
static void *fadeinThread(void *notused);

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

#ifdef STBPNX
static snd_mixer_elem_t *mainVolume;
static snd_mixer_t *handle;
#endif

static long min = 0;
static long max = 100;

static struct {
	int saved_volume;
	int final;
	int current;
	pthread_t thread;
} fadein;

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

/* -------------------------- SOUND SETTING --------------------------- */

int sound_init(void)
{
	memset(&fadein, 0, sizeof(fadein));
	fadein.saved_volume  = -1;
#ifdef STBPNX
	int err;
	snd_mixer_selem_id_t *sid;
#ifdef STB225
	static snd_mixer_elem_t *elem;
#endif
	char card[64] = "default";

	/* Perform the necessary pre-amble to start up ALSA Mixer */
	snd_mixer_selem_id_alloca(&sid);
	if ( (err = snd_mixer_open(&handle, 0)) < 0 )
	{
		eprintf("Sound: Mixer %s open error: %s\n", card, snd_strerror(err));
		return err;
	}
	if ( (err = snd_mixer_attach(handle, card)) < 0 )
	{
		eprintf("Sound: Mixer attach %s error: %s\n", card, snd_strerror(err));
		snd_mixer_close(handle);
		return err;
	}
	if ( (err = snd_mixer_selem_register(handle, NULL, NULL)) < 0 )
	{
		eprintf("Sound: Mixer register error: %s\n", snd_strerror(err));
		snd_mixer_close(handle);
		return err;
	}
	err = snd_mixer_load(handle);
	if ( err < 0 )
	{
		eprintf("Sound: Mixer %s load error: %s\n", card, snd_strerror(err));
		snd_mixer_close(handle);
		return err;
	}

#ifdef STB6x8x
	/* Get the first mixer element - this will be the AK4702 which controls the volume! */
	mainVolume = snd_mixer_first_elem(handle);
	eprintf("Sound: First channel is %s (Rev. %d)\n",
	        snd_mixer_selem_get_name(mainVolume), appControlInfo.soundInfo.hardwareRevision);
	if (appControlInfo.soundInfo.hardwareRevision < 3)
	{
		if ( appControlInfo.soundInfo.rcaOutput==1 )
		{
			/* Get the next mixer element if the SAA8510 (Anabel) is going to be used */
			mainVolume = snd_mixer_elem_next(mainVolume);
			eprintf("Sound: Second channel is %s\n", snd_mixer_selem_get_name(mainVolume));
		}
	} else
	{
		/* Get the first channel of SAA8510 (Anabel) */
		mainVolume = snd_mixer_elem_next(mainVolume);
		eprintf("Sound: Second channel is %s\n", snd_mixer_selem_get_name(mainVolume));
		if ( appControlInfo.soundInfo.rcaOutput==0 )
		{
			/* Get the next channel of SAA8510 (Anabel) */
			mainVolume = snd_mixer_elem_next(mainVolume);
			eprintf("Sound: Third channel is %s\n", snd_mixer_selem_get_name(mainVolume));
		}
	}
	snd_mixer_selem_get_id(mainVolume, sid);
#endif // STB6x8x
#ifdef STB225
    /* find the Scart volume control */ 
    snd_mixer_selem_id_set_name(sid, "SCART");
    snd_mixer_selem_id_set_index(sid, 0);
    mainVolume = snd_mixer_find_selem( handle, sid );
    if(mainVolume == NULL)
    {
        eprintf("Sound: SCART Volume not found - using Decoder1 volume instead\n");
        snd_mixer_selem_id_set_name(sid, "Decoder");
        snd_mixer_selem_id_set_index(sid, 0);
        mainVolume = snd_mixer_find_selem( handle, sid );
        if(mainVolume == NULL)
        {
            eprintf("Sound: No Decoder1 volume control either!\n");
        }
    }
    // Set the default audio mixer controls
    // decoders are set to 100% volume and centre balance
    snd_mixer_selem_id_set_name(sid, "Decoder"); // decoder volume
    snd_mixer_selem_id_set_index(sid, 0);
    elem = snd_mixer_find_selem( handle, sid );
    if(elem == NULL)
    {
        eprintf("Sound: Decoder 0 Volume not found\n");
    }
    else
    {
        (void)snd_mixer_selem_set_playback_volume_all(elem, 100 );
    }
    snd_mixer_selem_id_set_index(sid, 1);
    elem = snd_mixer_find_selem( handle, sid );
    if(elem == NULL)
    {
        eprintf("Sound: Decoder 1 Volume not found\n");
    }
    else
    {
        (void)snd_mixer_selem_set_playback_volume_all(elem, 100 );
    }
    snd_mixer_selem_id_set_name(sid, "Decoder Balance"); // decoder balance
    snd_mixer_selem_id_set_index(sid, 0);
    elem = snd_mixer_find_selem( handle, sid );
    if(elem == NULL)
    {
        eprintf("Sound: Decoder 0 Balance not found\n");
    }
    else
    {
        (void)snd_mixer_selem_set_playback_volume_all(elem, 0 );
    }
    snd_mixer_selem_id_set_index(sid, 1);
    elem = snd_mixer_find_selem( handle, sid );
    if(elem == NULL)
    {
        eprintf("Sound: Decoder 1 Balance not found\n");
    }
    else
    {
        (void)snd_mixer_selem_set_playback_volume_all(elem, 0 );
    }
#endif // STB225

	sound_setVolume(appControlInfo.soundInfo.volumeLevel);

	snd_mixer_selem_get_playback_volume_range(mainVolume, &min, &max);
#endif // STBPNX
	interface_soundControlSetup(sound_callback, NULL, min, max, sound_getVolume());

	return 0;
}

int sound_term(void )
{
#ifdef STBPNX
	snd_mixer_close(handle);
#endif
	if (fadein.thread) {
		pthread_cancel(fadein.thread);
		dprintf("%s: waiting fadein thread\n", __func__);
		pthread_join(fadein.thread, NULL);
		fadein.thread = 0;
	}
	return 0;
}

void sound_setVolume(long value)
{
	if ( value >= 0 )
	{
		if ( appControlInfo.soundInfo.muted && value )
		{
			/* Turn off muting */
			appControlInfo.soundInfo.muted = 0;
		}

		if ( !appControlInfo.soundInfo.muted )
		{
			/* Save the new volume level */
			appControlInfo.soundInfo.volumeLevel = (int)value;
		}
	}

	dprintf("%s: Volume is %d (%d)\n", __FUNCTION__, appControlInfo.soundInfo.volumeLevel, appControlInfo.soundInfo.muted);

#ifdef STBPNX
	snd_mixer_selem_set_playback_volume_all(mainVolume,
		(appControlInfo.playbackInfo.audioStatus == audioMute || appControlInfo.soundInfo.muted) ?
		0 : appControlInfo.soundInfo.volumeLevel);
#endif
#ifdef STSDK
	elcdRpcType_t type;
	cJSON        *param = cJSON_CreateNumber(value);
	cJSON        *res   = NULL;
	int           ret;

	ret = st_rpcSync( elcmd_setvol, param, &type, &res );
	if( ret == 0 && type == elcdRpcResult && res && res->type == cJSON_String )
	{
		if( strcmp(res->valuestring, "ok") )
		{
			eprintf("%s: failed: %s\n", __FUNCTION__, res->valuestring);
		}
	}
	cJSON_Delete(res);
	cJSON_Delete(param);
#endif
}

static long sound_getVolume(void)
{
	return appControlInfo.soundInfo.volumeLevel;
}

static int sound_callback(interfaceSoundControlAction_t action, void *pArg)
{
	long v;

	//dprintf("%s: in\n", __FUNCTION__);

	if ( action == interfaceSoundControlActionVolumeUp )
	{
		v = sound_getVolume() + (max-min)/SOUND_VOLUME_STEPS;
		sound_setVolume(v > max ? max : v);

		//dprintf("%s: interfaceSoundControlActionVolumeUp %d (min %d, max %d)\n", __FUNCTION__, sound_getVolume(), min, max);

		interface_soundControlSetValue(sound_getVolume());
		interface_soundControlSetMute(appControlInfo.soundInfo.muted);
	} else if ( action == interfaceSoundControlActionVolumeDown )
	{
		v = sound_getVolume() - (max-min)/SOUND_VOLUME_STEPS;
		sound_setVolume(v < min ? min : v);

		//dprintf("%s: interfaceSoundControlActionVolumeDown\n", __FUNCTION__);

		interface_soundControlSetValue(sound_getVolume());
		interface_soundControlSetMute(appControlInfo.soundInfo.muted);
	} else if ( action == interfaceSoundControlActionVolumeMute )
	{
		if ( appControlInfo.soundInfo.muted )
		{
			appControlInfo.soundInfo.muted = 0;
			sound_setVolume(appControlInfo.soundInfo.volumeLevel);
		} else
		{
			appControlInfo.soundInfo.muted = 1;
			sound_setVolume(0);
		}

		//dprintf("%s: interfaceSoundControlActionVolumeMute\n", __FUNCTION__);

		interface_soundControlSetMute(appControlInfo.soundInfo.muted);
	} else if ( action == interfaceSoundControlActionVolumeHide )
	{
		saveAppSettings();
		return 0;
	}

	interface_soundControlRefresh(1);
	return 0;
}

int sound_restart(void)
{
	dprintf("%s: Restarting sound system.\n", __FUNCTION__);
	sound_term();
	return sound_init();
}

void sound_fadeInit(void)
{
	if (appControlInfo.soundInfo.muted) {
		fadein.saved_volume = -1;
	} else {
		if (fadein.saved_volume < 0)
		fadein.saved_volume = appControlInfo.soundInfo.volumeLevel;
#ifdef 	FADEIN_STEPS
		fadein.current = fadein.final = fadein.saved_volume - FADEIN_STEPS*FADEIN_SPEED;
		if (fadein.current<0)
#endif
		fadein.current = fadein.final = 0;
		sound_setVolume(fadein.current);
	}
}

void sound_fadein(void)
{
	if (!appControlInfo.soundInfo.fadeinVolume) {
		sound_restoreVolume();
		return;
	}
	if (fadein.saved_volume != -1) {
		fadein.final = fadein.saved_volume;
		fadein.saved_volume = -1;
		if (fadein.thread == 0)
			pthread_create(&fadein.thread, NULL, fadeinThread, NULL);
	}
}

void sound_restoreVolume(void)
{
	if (fadein.saved_volume != -1) {
		sound_setVolume(fadein.saved_volume);
		fadein.saved_volume = -1;
		fadein.final = 0;
		fadein.current = 0;
	}
}

void *fadeinThread(void *notused) {
	while (1) {
		if (fadein.current>=fadein.final || appControlInfo.soundInfo.muted) {
			usleep(100000);
			pthread_testcancel();
		} else {
			dprintf("%s: fade volume from %d to %d\n", __func__, fadein.current, fadein.final);
			do {
				pthread_testcancel();
				sound_setVolume(fadein.current);
				usleep(100000);
				pthread_testcancel();
				if (fadein.current != appControlInfo.soundInfo.volumeLevel || appControlInfo.soundInfo.muted) {
					// Volume changed during fadein, aborting
					fadein.final = appControlInfo.soundInfo.volumeLevel;
					break;
				}
				fadein.current += FADEIN_SPEED;
				if (fadein.current>fadein.final) fadein.current = fadein.final;
			} while (fadein.current<fadein.final);
			sound_setVolume(fadein.final);
			fadein.final = 0;
		}
	}
	return NULL;
}
