
/*
 gstreamer.c

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

#ifdef ENABLE_GSTREAMER

// Should be included before <directfb.h>!
#include <gst/gst.h>

#include "gstreamer.h"
#include "debug.h"
#include "gfx.h"
#include "StbMainApp.h"

#include <sys/wait.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#ifndef GST_VIDEO_SINK
#define GST_VIDEO_SINK  "autovideosink"
#endif
#ifndef GST_AUDIO_SINK
#define GST_AUDIO_SINK  "autoaudiosink"
#endif

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct {
	GstElement *play;
	char       *uri;
} GstProvider;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static GstProvider gst_provider;

/******************************************************************
* FUNCTION IMPLEMENTATION                                         *
*******************************************************************/

int gstreamer_CreateGraph(void)
{
	int sink = 0;
	GstElement *audio_sink = NULL;
	GstElement *video_sink = NULL;

	// Create GStreamer graph
	do {
		audio_sink = gst_element_factory_make (GST_AUDIO_SINK, "audio-sink");
		if (!audio_sink) {
			eprintf("%s: failed to create audio sink %s\n", __func__, GST_AUDIO_SINK);
			break;
		}
		sink++;
	} while (0);

	do {
		video_sink = gst_element_factory_make (GST_VIDEO_SINK, "video-sink");
		if (!video_sink) {
			eprintf("%s: failed to create video sink %s\n", __func__, GST_VIDEO_SINK);
			break;
		}
		sink++;
	} while (0);

	if (!sink) {
		eprintf("%s: (!) no sink created!\n", __func__);
		return -1;
	}

	gst_provider.play = gst_element_factory_make ("playbin2", "play");
	if (!gst_provider.play) {
		eprintf("%s: (!) failed to create a GStreamer play object", __func__);
		if (video_sink) {
			gst_element_set_state (video_sink, GST_STATE_NULL);
			gst_object_unref (video_sink);
			video_sink = NULL;
		}
		if (audio_sink) {
			gst_element_set_state (audio_sink, GST_STATE_NULL);
			gst_object_unref (audio_sink);
			audio_sink = NULL;
		}
		return -1;
	}
	g_object_set(gst_provider.play, "video-sink", video_sink, NULL);
	g_object_set(gst_provider.play, "audio-sink", audio_sink, NULL);

	dprintf("%s: video %s audio %s\n", __func__,
		video_sink ? GST_VIDEO_SINK : "none",
		audio_sink ? GST_AUDIO_SINK : "none");

	return 0;
}

int gstreamer_init(void)
{
	memset(&gst_provider, 0, sizeof(gst_provider));

	if (gstreamer_CreateGraph())
		return -1;

	return 0;
}

void gstreamer_terminate(void)
{
	if (gst_provider.play) {
		dprintf("%s: destroying pipeline\n", __func__);
		gst_element_set_state (gst_provider.play, GST_STATE_NULL);
		gst_object_unref(gst_provider.play);
		gst_provider.play = NULL;
	}
	FREE (gst_provider.uri);
	dprintf("%s: out\n", __func__);
}

int gstreamer_play(const char *url)
{
	size_t new_uri_size = strlen(url)+1;
	int is_file = helperFileExists(url);

	if (is_file)
		new_uri_size += 7; // "file://"
	if (!gst_provider.uri || strcmp(gst_provider.uri + (is_file ? 7 : 0), url)) {
		char *new_uri = realloc(gst_provider.uri, new_uri_size);
		if (!new_uri) {
			eprintf("%s: failed to alloc %u bytes for uri\n", __func__, new_uri_size);
			return -1;
		}
		gst_provider.uri = new_uri;
		if (is_file) {
			memcpy(new_uri, "file://", 7);
			new_uri      += 7;
			new_uri_size -= 7;
		}
		memcpy(new_uri, url, new_uri_size);

		dprintf("%s: playing %s\n", __func__, gst_provider.uri);

		GstBus *bus = gst_element_get_bus (gst_provider.play);
		gst_bus_set_flushing (bus, TRUE);

		gst_element_set_state (gst_provider.play, GST_STATE_READY);

		gst_bus_set_flushing (bus, FALSE);
		gst_object_unref (bus);

		g_object_set (gst_provider.play, "uri", gst_provider.uri, NULL);
	}
	gst_element_set_state (gst_provider.play, GST_STATE_PLAYING);

	return 0;
}

int  gstreamer_resume(void)
{
	if (!gst_provider.play || !gst_provider.uri) {
		eprintf("%s: failed to play: %s\n", gst_provider.play ? "no uri specified" : "pipeline is not created");
		return -1;
	}

	dprintf("%s: resume %s\n", __func__, gst_provider.uri);
	gst_element_set_state (gst_provider.play, GST_STATE_PLAYING);
	return 0;
}

int  gstreamer_pause(void)
{
	if (!gst_provider.play) {
		eprintf("%s: pipeline is not created\n", __func__);
		return 0;
	}
	dprintf("%s: in\n", __func__);
	gst_element_set_state (gst_provider.play, GST_STATE_PAUSED);
	return 0;
}

int gstreamer_stop(void)
{
	if (!gst_provider.play)
		return 0;

	dprintf("%s: in\n", __func__);

	GstBus *bus = gst_element_get_bus (gst_provider.play);
	gst_bus_set_flushing (bus, TRUE);

	GstState cur_state;
	gst_element_get_state (gst_provider.play, &cur_state, NULL, 0);
	if (cur_state > GST_STATE_READY) {
		dprintf ("%s: stopping %s\n", __func__, gst_provider.uri);
		gst_element_set_state (gst_provider.play, GST_STATE_READY);
	}
	gst_object_unref (bus);
	dprintf("%s: out\n", __func__);

	return 0;
}

DFBVideoProviderStatus gstreamer_getStatus(void)
{
	GstState cur_state, pending_state;
	gst_element_get_state (gst_provider.play, &cur_state, &pending_state, 0);
	dprintf("%s: status %d pending %d\n", __func__, cur_state, pending_state);

	if (pending_state != GST_STATE_VOID_PENDING)
		cur_state = pending_state;
	switch (cur_state) {
		case GST_STATE_READY:
			return DVSTATE_STOP;
		case GST_STATE_PAUSED:
		case GST_STATE_PLAYING:
			return DVSTATE_PLAY;
		default: break;
	}
	return DVSTATE_FINISHED;
}

#endif // ENABLE_GSTREAMER
