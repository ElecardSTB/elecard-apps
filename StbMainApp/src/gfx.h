#if !defined(__GFX_H)
	#define __GFX_H

/*
 gfx.h

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

#include "defines.h"
#include "app_info.h"
#include "sem.h"
#if (defined STSDK)
#include "client.h"
#endif

#include <directfb.h>
#ifdef STBPNX
#if (defined STB225)
#include <phStbDFBVideoProviderCommonElcTypes.h>
#else
#include <phStbDFBVideoProviderCommonTypes.h>
#endif
#endif // STBPNX

/***********************************************
* EXPORTED MACROS                              *
************************************************/

#define DFBCAPS_LENGTH_IN_SEC (0x00001000)

#define MAX_SCALE             (8.0)

/* image cache table size */
#define GFX_IMAGE_TABLE_SIZE (100)
#define GFX_IMAGE_DOWNLOAD_SIZE (128*1024)

#define GFX_MAX_LAYERS_5L    (5)
#define GFX_MAX_LAYERS_2L    (2)

#define GFX_PAUSE		 (0)
#define GFX_HALFSTOP	 (1)
#define GFX_STOP		 (2)

/*
 * An error checking macro for a call to DirectFB.
 * It is suitable for very simple apllications or tutorials.
 * In more sophisticated applications this general error checking should not be used.
 */


#define DFBRETURN(x)                                           \
{                                                              \
    DFBResult err;                                             \
    err = x;                                                   \
                                                               \
    if (err != DFB_OK)                                         \
    {                                                          \
        fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
        DirectFBError( #x, err );                              \
        return err;                                            \
    }                                                          \
}

#define DFBCHECK(x)                                            \
{                                                              \
    DFBResult err;                                             \
    err = x;                                                   \
                                                               \
    if (err != DFB_OK)                                         \
    {                                                          \
        fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
        DirectFBError( #x, err );								\
    }                                                          \
}

#define DFBCHECKLABEL(x, label)                                \
{                                                              \
    DFBResult err = x;                                         \
                                                               \
    if (err != DFB_OK)                                         \
    {                                                          \
        goto label;                                            \
    }                                                          \
}

/***********************************************
* EXPORTED TYPEDEFS                            *
************************************************/

typedef struct __stb810_gfxImageEntry
{
	char *filename;
	IDirectFBSurface *pImage;
	int width;
	int height;
	int stretch;
	struct __stb810_gfxImageEntry *pNext;
	struct __stb810_gfxImageEntry *pPrev;
} stb810_gfxImageEntry;

typedef enum
{
	stb810_gfxStreamTypesUnknown = 0,
	stb810_gfxStreamTypesMpegTS,
	stb810_gfxStreamTypesMpegPS,
	stb810_gfxStreamTypesMpeg4,
	stb810_gfxStreamTypesMP3,
	stb810_gfxStreamTypesAnalog,
	stb810_gfxStreamTypesDivx,
	stb810_gfxStreamTypesWMT,
	stb810_gfxStreamTypesH264ES
} stb810_gfxStreamTypes_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/


#ifdef __cplusplus
extern "C" {
#endif

/* The frame buffer surface, to which we write graphics. */
extern IDirectFBSurface *pgfx_frameBuffer;

#ifdef GFX_USE_HELPER_SURFACE
extern IDirectFBSurface *pgfx_helperFrameBuffer;
#endif

/* The font that we use to write text. */
extern IDirectFBFont *pgfx_font;

/* The font that we use to write description text. */
extern IDirectFBFont *pgfx_smallfont;

/* The root interface of DirectFB, from which all functionality is obtained. */
extern IDirectFB *pgfx_dfb;

/* The DirectFB event buffer. */
extern IDirectFBEventBuffer *appEventBuffer;

#ifdef STB225
extern int       gfxUseScaleParams;
extern pmysem_t  gfxDimensionsEvent;

#define NUM_LAYERS     (4)
#define NUM_MIXERS     (2)

typedef enum
{
    gfxMixerAnalog,
    gfxMixerHdmi
} gfxMixer_t;


typedef enum
{
    gfxLayerNotDisplayed,
    gfxLayerDisplayed
} gfxLayerDisplay_t;


typedef struct _mixerInfo
{
    gfxLayerDisplay_t layers[NUM_LAYERS];
    int32_t backgroundEntry;
}mixerInfo_t;

#endif


#ifdef __cplusplus
}
#endif

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
*   @brief Function used to return a pointer to a given video layer
*
*   @param  which       I       Layer to be returned
*
*   @retval Pointer to requested video layer
*/
IDirectFBDisplayLayer *gfx_getLayer(int which);

/**
*   @brief Return id of the primary layer
*
*   @retval id of requested video layer
*/
int gfx_getPrimaryLayer();

/**
*   @brief Return id of the image layer
*
*   @retval id of requested video layer
*/
int gfx_getImageLayer();

/**
*   @brief Return id of the pip video layer
*
*   @retval id of requested video layer
*/
int gfx_getPipVideoLayer();

/**
*   @brief Return id of the main video layer
*
*   @retval id of requested video layer
*/
int gfx_getMainVideoLayer();

/**
*   @brief Return a number of layers supported
*
*   @retval number of supported layers.
*/
int gfx_getNumberLayers();

/**
*   @brief Function used to decode an Image
*
*   @param  filename      I     Image file to be decoded
*   @param  width         I     Output image width
*   @param  height        I     Output image height
*
*   @retval Pointer to surface containing decoded image - or NULL if decoding failed
*/
IDirectFBSurface * gfx_decodeImage(const char* filename, int width, int height, int stretchToSize);

IDirectFBSurface * gfx_decodeImageNoUpdate (const char* filename, int width, int height, int stretchToSize);
/**
*   @brief Function used to decode an Image and render it to layer
*
*   @param  filename    I     Image file to be decoded
*   @param  layer       I
*
*   @retval  = 0 on success
*           != 0 otherwise
*/
int  gfx_decode_and_render_Image_to_layer(const char* filename, int layer);

int  gfx_decode_and_render_Image(const char* filename);

/**
*   @brief Function used to hide rendered image
*
*   @param  layer       I       Layer with rendered image
*
*/
void  gfx_hideImage(int layer);

void  gfx_showImage(int layer);

void gfx_hideVideoLayer (int videoLayer);
void gfx_showVideoLayer (int videoLayer);

/**
*   @brief Function used to find image entry by name
*
*   @param  filename    I       Look up name
*
*   @retval image entry
*/
stb810_gfxImageEntry *gfx_findImageEntryByName( const char* filename );

/**
*   @brief Function used to release image entry
*
*   @param  pImageEntry    I       Pointer to image entry
*
*   @retval void
*/
void gfx_releaseImageEntry( stb810_gfxImageEntry* pImageEntry );

/**
*   @brief Function used to set up the output size for a given layer
*
*   @param  videoLayer  I       Layer to be modified
*   @param  x           I       Output x position
*   @param  y           I       Output y position
*   @param  width       I       Output width
*   @param  height      I       Output height
*   @param	steps		I		Number of step to take to reach final dimensions
*
*   @retval void
*/
void gfx_setDestinationRectangle(int videoLayer, int x, int y, int width, int height, int steps);

void gfx_setSourceRectangle(int32_t videoLayer, int32_t x, int32_t y, int32_t width, int32_t height, int32_t steps);

void gfx_getVideoPlaybackSize(int *width, int *height);

void gfx_setStartPosition (int videoLayer, long posInSec);

/**
*   @brief Function used to start a video provider
*
*   @param  videoSource   I     Source file for the video provider
*   @param  videoLayer    I     Destination video layer for the decode
*   @param  force         I     Flag to indicate if the video provider should be forcefully re-started
*   @param  options       I     String containing video provider options
*
*   @retval  = 0 on success
* 	        != 0 otherwise
*/
int gfx_startVideoProvider(const char* videoSource, int videoLayer, int force, char* options);

/**
*   @brief Function used to set video provider rendering speed
*
*   @param  videoLayer    I     Destination video layer for the decode
*   @param  multiplier    I     Desired speed multiplier
*
*   @retval  = 0 on success
* 	        != 0 otherwise
*/
int gfx_setSpeed(int videoLayer, double multiplier);


/**
*   @brief Function used to stop a video provider
*
*   @param  videoLayer    I     Destination video layer for the decode
*   @param  force         I     If true the video provider is stopped then released
*   @param  hideLayer     I     If true the video layer is hidden
*
*   @retval void
*/
void gfx_stopVideoProvider(int videoLayer, int force, int hideLayer);

/**
*   @brief Function used to stop all active video providers on specified screen
*
*   @param  which         I       Screen on which to stop video providers
*
*   @retval void
*/
void gfx_stopVideoProviders(int which);

/**
*   @brief Function used to clear the screen
*
*   @param  pSurface      I     Surface to be cleared
*   @param  width	  I	Width of surface to be cleared
*   @param  height	  I	Height of surface to be cleared
*
*   @retval void
*/
void gfx_clearSurface(IDirectFBSurface *pSurface, int width, int height);

/**
*   @brief Function used to draw a rectangle
*
*   @param  pSurface      I     Surface to be drawn to
*   @param  r    	  I	\
*   @param  g   	  I	 \ 32 bit ARGB fill colour
*   @param  b   	  I	 /
*   @param  a   	  I	/
*   @param  x   	  I	X location of rectangle
*   @param  y   	  I	Y location of rectangle
*   @param  width         I	Width of rectangle
*   @param  height   	  I	Height of rectangle
*
*   @retval void
*/
void gfx_drawRectangle(IDirectFBSurface *pSurface, int r, int g, int b, int a, int x, int y, int width, int height);

/**
*   @brief Function used to draw text
*
*   @param  pSurface      I     Surface to be drawn to
*   @param  pFont         I     Font to be used
*   @param  r    	  I	\
*   @param  g   	  I	 \ 32 bit ARGB text colour
*   @param  b   	  I	 /
*   @param  a   	  I	/
*   @param  x   	  I	X location of text
*   @param  y   	  I	Y location of text
*   @param  pText         I	Pointer to text string
*   @param  drawBox   I Flag to indicate if a box should be drawn behind the text
*   @param  shadow    I Flag to indicate if a drop shadow should be drawn behind the text
*
*   @retval void
*/
void gfx_drawText(IDirectFBSurface *pSurface, IDirectFBFont *pFont, int r, int g, int b , int a, int x, int y, const char *pText, int drawBox, int shadow);

#if (defined STB225)
/**
*   @brief Function used to clear buffer of depths in 3D mode
*
*   @param  r    	  I	\
*   @param  g   	  I	 \ 32 bit ARGB clear colour
*   @param  b   	  I	 /
*   @param  a   	  I	/
*
*   @retval void
*/

void gfx_fb1_clear(char r, char g, char b, char a);

/**
*   @brief Function used to draw a depth for 3D object
*
*   @param  x   	  I	X location of rectangle
*   @param  y   	  I	Y location of rectangle
*   @param  width     I	Width of rectangle
*   @param  height    I	Height of rectangle
*   @param  depth     I	Depth of rectangle
*
*   @retval void
*/
void gfx_fb1_draw_rect(int x, int y, int w, int h, unsigned char depth);
#endif

/**
*   @brief Function used to flip the display buffer
*
*   @param  pSurface      I     Surface to be flipped
*
*   @retval void
*/
void gfx_flipSurface(IDirectFBSurface *pSurface);

/**
*   @brief Function used to enlarge teletext symbols
*
*   @param  pSurface      I     Surface to be flipped
*   @param  flagDH        I     Enlarge height
*   @param  flagDW        I     Enlarge width
*   @param  flagDS        I     Enlarge all
* 
*   @retval void
*/
void gfx_DoubleSize(IDirectFBSurface *pSurface, int i, int column, int flagDH, int flagDW, int flagDS, int symbolWidth, int symbolHeight, int horIndent, int verIndent);

/**
*   @brief Function used to set up the output format
*
*   @param  forceChange   I     Force the removal of all surfaces etc
*
*   @retval void
*/
void gfx_setOutputFormat(int forceChange);

/**
*   @brief Function used to change current output format
*
*   @param  format   I     Format to set
*
*   @retval void
*/
void gfx_changeOutputFormat(int format);

/**
 * @brief Function used to determine if we have hd ouput or not.
 *
 * @retval 1 if HD output
 *         0 if not.
 */
int gfx_isHDoutput();

/**
 * @brief Function used to determine if we support TV standard selection or not.
 *
 * @retval 1 if TV Standard selection possible
 *         0 if not.
 */
int gfx_tvStandardSelectable();

/**
*   @brief Function used to initialise the graphics
*
*   @param  argc	I	Input argument count
*   @param  argv	I	Input argument list
*
*   @retval void
*/
void gfx_init(int argc, char* argv[]);

/**
*   @brief Function used to terminate the graphics
*
*   @retval void
*/
void gfx_terminate(void);

/**
*   @brief Function used to return the position within the current video stream
*
*   @param  videoLayer	I	Video provider's layer
*
*   @retval Current position
*/
double gfx_getVideoProviderPosition(int videoLayer);

/**
*   @brief Function used to return the length of the current video stream
*
*   @param  videoLayer	I	Video provider's layer
*
*   @retval Video stream length
*/
double gfx_getVideoProviderLength(int videoLayer);

/**
*   @brief Function used to set the position within the current video stream
*
*   @param  videoLayer	I	Video provider's layer
*
*   @retval void
*/
void gfx_setVideoProviderPosition(int videoLayer, long position);

int  gfx_getPosition(double *plength,double *pposition);

/**
*   @brief Function used to get the current video stream type
*
*   @param  videoLayer	I	Video provider's layer
*
*   @retval void
*/
stb810_gfxStreamTypes_t gfx_getVideoProviderStreamType(int videoLayer);

DFBStreamCapabilities gfx_getVideoProviderCaps(int videoLayer);

int gfx_getVideoProviderHasVideo(int videoLayer);

/**
*   @brief Return video provider status
*
*   @retval video provider status.
*/
DFBVideoProviderStatus gfx_getVideoProviderStatus(int videoLayer);

/**
*   @brief Set video provider playback flags.
* 
*   @param  videoLayer		I	Video provider's layer
* 	@param  playbackFlags	I	Playback flag
* 	@param  value			I	Mean bool value, if this =0 then unset, else set flag
*
*   @retval void.
*/
void gfx_setVideoProviderPlaybackFlags( int videoLayer, DFBVideoProviderPlaybackFlags playbackFlags, int value );

/**
*   @brief Function used to turn on/off the trickmode settings for Video Provider
*
*   @param  videoLayer	I	Video provider's layer
*   @param  direction	I	TrickMode direction
*   @param  speed    	I	TrickMode Speed.
*
*   @retval void
*/
void gfx_setupTrickMode(int videoLayer,
							   stb810_trickModeDirection direction,
							   stb810_trickModeSpeed speed);

/**
*   @brief Is Trickmode supported
*
*   @param  videoLayer	I	Video provider's layer
*
*   @retval 1 if supported
*           0 if not
*/
int gfx_isTrickModeSupported(int videoLayer);

int gfx_videoProviderIsActive(int videoLayer);

int gfx_videoProviderIsPaused(int videoLayer);

int gfx_videoProviderIsCreated(int videoLayer, const char *videoSource);

int gfx_enableVideoProviderVerimatrix(int videoLayer, const char *inifile);

int gfx_enableVideoProviderSecureMedia(int videoLayer);

void gfx_setAttrRectangle(unsigned char alpha, int x, int y, int width, int height);

int gfx_getVideoProviderAudioCount(int videoLayer);

int gfx_getVideoProviderAudioStream(int videoLayer);

int gfx_setVideoProviderAudioStream(int videoLayer, int audioStream);

int gfx_setVideoProviderLive(int videoLayer);

int gfx_resumeVideoProvider(int videoLayer);

/* Wait for last operation with providers to finish */
void gfx_waitForProviders();

void gfx_startEventThread(void);
void gfx_stopEventThread(void);

#if (defined STSDK)
socketClient_t* gfx_stSocket(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __GFX_H      Do not add any thing below this line */
