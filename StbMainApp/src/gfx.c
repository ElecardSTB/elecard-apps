
/*
 gfx.c

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

#include "gfx.h"

#include "debug.h"
#include "sem.h"
#include "interface.h"
#include "StbMainApp.h"
#include "backend.h"
#include "media.h"
#include "rtp.h"
#include "rtsp.h"
#include "sound.h"
#include "off_air.h"
#include "l10n.h"
#include "downloader.h"
#include "stsdk.h"
#include "crc32.h"
#include "gstreamer.h"
#ifdef ENABLE_VIDIMAX
#include "vidimax.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>

#ifdef STBTI
#include <sys/mman.h>
#include <linux/fb.h>
#define ATTR_DEVICE  "/dev/fb/2"
#define ATTR_BPP     (4)
#endif

#ifdef STBPNX
	#include <phStbVideoRenderer.h>
	#include <phStbSystemManager.h>
#endif
#ifdef STB82
	#include <phStbRpc.h>
#endif
#ifdef STB225
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <directfb.h>
#include <directfb_strings.h>
#endif

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define NUM_STEPS   (0)
#define FIELD_WAIT  (15000)
#define ROUND_UP(x) ((((x)+1)/2)*2)

//#define TRACE_PROVIDERS
#ifdef  TRACE_PROVIDERS
#define pprintf eprintf
#else
#define pprintf(x...)
#endif


/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

#ifdef STB225
typedef enum
{
	STOPPED,
	STOP_REQUESTED,
	RUNNING
}eventThreadState_t;
#endif

typedef enum
{
	providerNone   = 0,
	providerActive = 1,
	providerInit,
} providerState_t;

typedef struct
{
	char                     name[MAX_URL];
	int                      active;
	bool                     paused;    /* Is Instance paused? */
	double                   savedPos;
#ifdef STSDK
	// Waiting for rpc answer: negative if none, queue index otherwise
	int                      waiting;
	double                   httpDuration;
#endif
#ifdef STBxx
	IDirectFBVideoProvider * instance;
#ifdef STB225
	bool                     videoPresent;    /* Is there video data available? */
	DFBVideoProviderEventType events;
	pthread_t                event_thread;     /* The pthread object. */
	volatile eventThreadState_t eventThreadState;
	IDirectFBEventBuffer     *pEventBuffer; /* Event Buffer Handle.*/
	DFBStreamDescription     streamDescription;
	DFBEventBufferStats      stats;
#endif
#endif // STBxx
} gfx_videoProviderInfo;

/**
 * @brief A structure that indicates the layer ID of DirectFB that maps to named layers*/
typedef struct
{
	int fb_layerID;
	int image_layerID;
	int pip_layerID;
	int main_layerID;
	int maxLayers;
} gfx_videoLayerIDS;

typedef struct __gfxImageInfo_t
{
	char *url;
	char filename[64];
	int width;
	int height;
	int stretchToSize;
	interfaceMenu_t *pMenu;
	char noUpdate;
} gfxImageInfo_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/**
 * This function release all surfaces etc so that i can resize display layers
 * before I switch to NTSC etc.
 */
static void gfx_formatChange(void);

/**
 * Display layer callback function.
 */
static DFBEnumerationResult gfx_display_layer_callback( DFBDisplayLayerID           id,
														DFBDisplayLayerDescription  desc,
														void                       *arg );
static void gfx_setVideoProviderName(const char *videoSource);
#ifdef STSDK
static inline float st_getTimeValue (cJSON *object, const char *value_name);
static void gfx_videoProviderStarted(elcdRpcType_t type, cJSON *result, void* pArg);
#endif

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static stb810_gfxImageEntry gfx_imageTable[GFX_IMAGE_TABLE_SIZE];
static stb810_gfxImageEntry *pgfx_FreeList = NULL;
static stb810_gfxImageEntry *pgfx_ImageList = NULL;

/* The surface to render the video on */
static IDirectFBSurface *pgfx_videoSurface[screenOutputs] = {NULL, NULL};

/* Video Layer */
static IDirectFBDisplayLayer *pgfx_videoLayer[GFX_MAX_LAYERS_5L] = {NULL};

/* The decoder for video. */
static gfx_videoProviderInfo gfx_videoProvider =
	{	.name = {0},
#ifdef STBxx
		.instance = NULL,
#endif
	};

/* Display screen */
static IDirectFBScreen *pgfx_screen = NULL;

/* gfx semaphore */
static pmysem_t  gfx_semaphore;

static pthread_mutex_t flipMutex;

#ifdef STBxx
/* Current screen display rectangles */
static int gfx_destinationLocation[GFX_MAX_LAYERS_5L][4] = {
	{ -1, -1, -1, -1},
	{ -1, -1, -1, -1},
	{ -1, -1, -1, -1},
	{ -1, -1, -1, -1},
	{ -1, -1, -1, -1}};

#ifdef STB225

#define MIN_RECT_SIZE (64)

#define ASPECT_RATIO_4x3  (4.0/3.0)
#define ASPECT_RATIO_16x9 (16.0/9.0)

#define GRAPHICS_1_MASK  (0x01)
#define GRAPHICS_2_MASK  (0x02)
#define DISPLAY_1_MASK   (0x04)
#define DISPLAY_2_MASK   (0x08)

int32_t gfxScaleHeight = 576;
int32_t gfxScaleWidth = 720;
int32_t gfxScreenHeight = 576;
int32_t gfxScreenWidth = 720;
int    gfxUseScaleParams = false;

static bool gfx_keepDimensionsThreadAlive;

static void *gfx_eventThread(void *pArg);

pmysem_t  gfxDimensionsEvent;
#endif // STB225
#endif // STBxx

#ifdef STBTI
#define NUM_ATTR_BUFFERS (2)

/* TI uses separate attribute buffer to store graphics layer opacity.
 * Each pixel has 3-bit alpha value in attribute layer, padded to 4 bits.
 * Buffer is split on two virtual screens. */
static struct
{
	/** File descriptor of attribute buffer (/dev/fb/2). */
	int fd;
	/** Whole width of attribute buffer */
	int width;
	/**  Index of virtual screen.
	 *   Virtual screens are placed side by side. */
	int index;
	/** Virtual screen size in bytes */
	size_t size;
	/** Virtual screen pixels */
	unsigned char *screen[NUM_ATTR_BUFFERS];
} gfx_attributeBuffer;

void flipAttrBuffers();
#endif

/**
 * @brief The thread that is used to monitor the video surfaces
 */
static pthread_t dimensionsThread;

/**
 * @brief The mapping of video layers.
 */
static gfx_videoLayerIDS gfx_layerMapping;

/**
 * @brief Number of layers on the screen.
 */
static int ggfx_NumLayers = 0;

/**
 * @brief Pointer to the thread that handles directfb events.
 */
static pthread_t event_thread;
/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

/* The frame buffer surface, to which we write graphics. */
IDirectFBSurface *pgfx_frameBuffer = NULL;

#ifdef GFX_USE_HELPER_SURFACE
IDirectFBSurface *pgfx_helperFrameBuffer = NULL;
#endif

/* The font that we use to write text. */
IDirectFBFont *pgfx_font = NULL;

/* The font that we use to write description text. */
IDirectFBFont *pgfx_smallfont = NULL;

/* The root interface of DirectFB, from which all functionality is obtained. */
IDirectFB *pgfx_dfb = NULL;

/* Pointer to the DirectFB event buffer. */
IDirectFBEventBuffer *appEventBuffer = NULL;

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

// NB: Only primary layer is guaranteed to be present!
IDirectFBDisplayLayer *gfx_getLayer(int which)
{
	if ( which < 0 || which >= ggfx_NumLayers )
		return NULL;
	if ( pgfx_videoLayer[which]==NULL )
	{
		DFBCHECK( pgfx_dfb->GetDisplayLayer(pgfx_dfb, which, &pgfx_videoLayer[which]) );
		/* Ensure we have access rights to the layer */
		pgfx_videoLayer[which]->SetCooperativeLevel(pgfx_videoLayer[which], DLSCL_EXCLUSIVE);
	}
	return pgfx_videoLayer[which];
}

int gfx_getPrimaryLayer()
{
	return gfx_layerMapping.fb_layerID;
}

int gfx_getImageLayer()
{
	return gfx_layerMapping.image_layerID;
}

int gfx_getPipVideoLayer()
{
	return gfx_layerMapping.pip_layerID;
}

int gfx_getMainVideoLayer()
{
	return gfx_layerMapping.main_layerID;
}

int gfx_getNumberLayers()
{
	return ggfx_NumLayers;
}

#ifdef STBxx
static IDirectFBSurface * gfx_getSurface(int which)
{
	if ( pgfx_videoSurface[which]==NULL )
	{
		IDirectFBDisplayLayer * pLayer;
#if (defined STB225)
		pLayer = gfx_getLayer(which ? gfx_getImageLayer()    : gfx_getMainVideoLayer());
#else
		pLayer = gfx_getLayer(which ? gfx_getPipVideoLayer() : gfx_getMainVideoLayer());
#endif
		dprintf("%s: DFBCHECK( pLayer->GetSurface(pLayer, &pgfx_videoSurface[%d]) )\n", __FUNCTION__, which);
		DFBCHECK( pLayer->GetSurface(pLayer, &pgfx_videoSurface[which]) );
	}
	return pgfx_videoSurface[which];
}
#endif

static void gfx_addImageToList(stb810_gfxImageEntry *pEntry,  stb810_gfxImageEntry **pList)
{
	pEntry->pNext = *pList;
	pEntry->pPrev = NULL;
	if ( *pList )
	{
		(*pList)->pPrev = pEntry;
	}
	*pList = pEntry;
}

static void gfx_removeImageFromList(stb810_gfxImageEntry *pEntry,  stb810_gfxImageEntry **pList)
{
	if ( pEntry->pPrev )
	{
		pEntry->pPrev->pNext = pEntry->pNext;
	}
	if ( pEntry->pNext )
	{
		pEntry->pNext->pPrev = pEntry->pPrev;
	}

	if ( pEntry == *pList )
	{
		*pList = pEntry->pNext;
	}
}


/* Return next input byte, or EOF if no more */
#define NEXTBYTE()  getc(myfile)

int gfx_is_JPEG(const char *name)
{
	char    s1,s2,s3,s4;
	int             len;

	len     =       strlen(name);
	if(len<4) return 0;
	s1      =       tolower(name[len-1]);
	s2      =       tolower(name[len-2]);
	s3      =       tolower(name[len-3]);
	s4      =       tolower(name[len-4]);
	//check is JPG
	if(s4=='.' && s3=='j' && s2=='p' && s1=='g')
		return 1;
	return 0;
}

/* Read one byte, testing for EOF */
static int
read_1_byte (FILE *myfile)
{
	int c;
	c = NEXTBYTE();
	return c;
}

/* Read 2 bytes, convert to unsigned int */
/* All 2-byte quantities in JPEG markers are MSB first */
static unsigned int
read_2_bytes (FILE *myfile)
{
	int c1, c2;

	c1 = NEXTBYTE();
	c2 = NEXTBYTE();
	return (((unsigned int) c1) << 8) + ((unsigned int) c2);
}


int is_rotation_JPEG(const char *filename)
{
	int set_flag;
	unsigned int length, i;
	int is_motorola; /* Flag for byte order */
	unsigned int offset, number_of_tags, tagnum;
	FILE * myfile;		/* My JPEG file */
	unsigned char exif_data[65536L];


	set_flag = 0;


	myfile = fopen(filename,"rb");

	if(!myfile) return 0;

	/* Read File head, check for JPEG SOI + Exif APP1 */
	for (i = 0; i < 4; i++)
		exif_data[i] = (unsigned char) read_1_byte(myfile);
	if (exif_data[0] != 0xFF ||
		exif_data[1] != 0xD8 ||
		exif_data[2] != 0xFF ||
		exif_data[3] != 0xE1)
		goto failure;

	/* Get the marker parameter length count */
	length = read_2_bytes(myfile);
	/* Length includes itself, so must be at least 2 */
	/* Following Exif data length must be at least 6 */
	if (length < 8)
		goto failure;
	length -= 8;
	/* Read Exif head, check for "Exif" */
	for (i = 0; i < 6; i++)
		exif_data[i] = (unsigned char) read_1_byte(myfile);
	if (exif_data[0] != 0x45 ||
		exif_data[1] != 0x78 ||
		exif_data[2] != 0x69 ||
		exif_data[3] != 0x66 ||
		exif_data[4] != 0 ||
		exif_data[5] != 0)
		goto failure;
	/* Read Exif body */
	for (i = 0; i < length; i++)
		exif_data[i] = (unsigned char) read_1_byte(myfile);

	if (length < 12) goto failure; /* Length of an IFD entry */

	/* Discover byte order */
	if (exif_data[0] == 0x49 && exif_data[1] == 0x49)
		is_motorola = 0;
	else if (exif_data[0] == 0x4D && exif_data[1] == 0x4D)
		is_motorola = 1;
	else
		goto failure;

	/* Check Tag Mark */
	if (is_motorola) {
		if (exif_data[2] != 0) goto failure;
		if (exif_data[3] != 0x2A) goto failure;
	} else {
		if (exif_data[3] != 0) goto failure;
		if (exif_data[2] != 0x2A) goto failure;
	}

	/* Get first IFD offset (offset to IFD0) */
	if (is_motorola) {
		if (exif_data[4] != 0) goto failure;
		if (exif_data[5] != 0) goto failure;
		offset = exif_data[6];
		offset <<= 8;
		offset += exif_data[7];
	} else {
		if (exif_data[7] != 0) goto failure;
		if (exif_data[6] != 0) goto failure;
		offset = exif_data[5];
		offset <<= 8;
		offset += exif_data[4];
	}
	if (offset > length - 2) goto failure; /* check end of data segment */

	/* Get the number of directory entries contained in this IFD */
	if (is_motorola) {
		number_of_tags = exif_data[offset];
		number_of_tags <<= 8;
		number_of_tags += exif_data[offset+1];
	} else {
		number_of_tags = exif_data[offset+1];
		number_of_tags <<= 8;
		number_of_tags += exif_data[offset];
	}
	if (number_of_tags == 0) goto failure;
	offset += 2;

	/* Search for Orientation Tag in IFD0 */
	for (;;) {
		if (offset > length - 12) goto failure; /* check end of data segment */
		/* Get Tag number */
		if (is_motorola) {
			tagnum = exif_data[offset];
			tagnum <<= 8;
			tagnum += exif_data[offset+1];
		} else {
			tagnum = exif_data[offset+1];
			tagnum <<= 8;
			tagnum += exif_data[offset];
		}
		if (tagnum == 0x0112) break; /* found Orientation Tag */
		if (--number_of_tags == 0)
			goto failure;
		//break;
		offset += 12;
	}

	//  EXIF Orientation Value	Row #0 is:	Column #0 is:
	//1	Top	Left side
	//2	Top	Right side
	//3	Bottom	Right side
	//4	Bottom	Left side
	//5	Left side	Top
	//6	Right side	Top
	//7	Right side	Bottom
	//8	Left side	Bottom

	/* Get the Orientation value */
	if (is_motorola) {
		if (exif_data[offset+8] != 0) goto failure;
		set_flag = exif_data[offset+9];
	} else {
		if (exif_data[offset+9] != 0) goto failure;
		set_flag = exif_data[offset+8];
	}
	if (set_flag > 8) goto failure;

	fclose(myfile);

	if((set_flag == 5)||(set_flag == 6)||(set_flag == 7)||(set_flag == 8))
		return 1;
	else
		return 0;
	failure:
	fclose(myfile);
	return 0;
}

int  gfx_decode_and_render_Image(const char* filename)
{
	return gfx_decode_and_render_Image_to_layer(filename, screenPip);
}

static void gfx_getDestinationRectangle(int * pWidth, int * pHeight)
{
	int32_t x;
	int32_t y;

	switch(appControlInfo.outputInfo.encConfig[0].resolution)
	{
		case(DSOR_720_480) :
		{
			x = 720;
			y = 480;
		}
		break;
		case(DSOR_720_576) :
		{
			x = 720;
			y = 576;
		}
		break;
		case(DSOR_1280_720) :
		{
			x = 1280;
			y = 720;
//Kpy
			x = 1920;
			y = 1080;
		}
		break;
		case(DSOR_1920_1080) :
		{
			x = 1920;
			y = 1080;
		}
		break;
		default:
		{
			x = 0;
			y = 0;
		}
		break;
	}

	*pWidth  = x;
	*pHeight = y;
}

int  gfx_decode_and_render_Image_to_layer(const char* filename, int layer)
{
	IDirectFBSurface        *pImage = NULL;
	IDirectFBDisplayLayer   *pLayer;
	int                     width;
	int                     height;
	int                     widthDec;
	int                     heightDec;
	int                     new_x,new_y;
	DFBSurfaceDescription   imageDesc;
	IDirectFBImageProvider *pImageProvider = NULL;
	float                   coeff_w,coeff_h;

	int screenWidth;
	int screenHeight;

	mysem_get(gfx_semaphore);

	if (helperFileExists(filename) ) {
		DFBRectangle actualRect;

		//dprintf("gfx: load image '%s'\n", filename);
		pLayer = gfx_getLayer( layer == screenMain ? gfx_getMainVideoLayer() : gfx_getPipVideoLayer());
#ifndef STBPNX
		if (!pLayer)
			goto create_error;
#endif

		DFBCHECKLABEL(pgfx_dfb->CreateImageProvider(pgfx_dfb, filename,
				&pImageProvider), create_error);

		DFBCHECKLABEL( pLayer->GetSurface(pLayer, &pImage), surface_error);

		DFBCHECKLABEL(pImage->GetSize(pImage,&width,&height), decode_error);

		DFBCHECKLABEL(pImageProvider->GetSurfaceDescription(pImageProvider, &imageDesc), decode_error);

		if ((imageDesc.height < 32) || (imageDesc.width < 32)
#ifdef STBPNX
		    || (imageDesc.width * imageDesc.height > 
		        PHSTBSYSTEMMANAGER_IMAGE_WIDTH_SURFASE * PHSTBSYSTEMMANAGER_IMAGE_HEIGHT_SURFASE)
#endif
        )
			goto decode_error;

		if (gfx_isHDoutput())
		{
			gfx_getDestinationRectangle(&screenWidth, &screenHeight);
		} 
		else if ((appControlInfo.outputInfo.encConfig[0].tv_standard == DSETV_NTSC)
#ifdef STBPNX
			 || (appControlInfo.outputInfo.encConfig[0].tv_standard == DSETV_PAL_M)
#endif
		    )
		{
			screenWidth  = 720;
			screenHeight = 480;
		} else
		{
			screenWidth  = 720;
			screenHeight = 576;
		}

		widthDec  = imageDesc.width;
		heightDec = imageDesc.height;
		if (appControlInfo.outputInfo.aspectRatio == aspectRatio_4x3)
		{
			heightDec = screenHeight == 576 ? imageDesc.height*12/11 : imageDesc.height*10/11;
		}
		else if (appControlInfo.outputInfo.aspectRatio == aspectRatio_16x9)
		{
			heightDec = screenHeight == 576 ? imageDesc.height*16/11 : imageDesc.height*40/33;
		}

		if (gfx_is_JPEG(filename))
		{
			if(is_rotation_JPEG(filename))
			{
				int tmp_h = widthDec;
				widthDec = heightDec;
				heightDec = tmp_h;
			}
		}

		coeff_w = width / (float)widthDec;
		coeff_h = height / (float)heightDec;

		//dprintf("%s: image %dx%d coeff w %f h %f dec %dx%d\n", __FUNCTION__, width, height, coeff_w, coeff_h, widthDec, heightDec);

		if (coeff_h <= coeff_w)
		{
			widthDec = widthDec*coeff_h;
			heightDec = heightDec*coeff_h;
		}
		else
		{
			widthDec = widthDec*coeff_w;
			heightDec = heightDec*coeff_w;
		}

		new_x = (widthDec < width) ? ((width - widthDec)/2) : 0;
		new_y = (heightDec < height) ? ((height - heightDec)/2) : 0;

		//only parity value
		new_x		=	(new_x/2)*2;
		new_y		=	(new_y/2)*2;
		widthDec	=	((widthDec+1)/2)*2;
		heightDec	=	((heightDec+1)/2)*2;

		//dprintf("%s: image %dx%d screen %dx%d dest (%d,%d) %dx%d\n", __FUNCTION__, imageDesc.width, imageDesc.height, screenWidth, screenHeight, new_x, new_y, widthDec, heightDec);

		DFBCHECKLABEL (pLayer->SetScreenRectangle(pLayer,new_x, new_y,widthDec, heightDec), decode_error);

		actualRect.w = width;
		actualRect.h = height;
		actualRect.x = actualRect.y = 0;

		/* Render the image */
		DFBCHECKLABEL(pImageProvider->RenderTo(pImageProvider, pImage, &actualRect), decode_error);

		mysem_release(gfx_semaphore);

		/* Show the image layer */
		//pDispLayer = gfx_getLayer(gfx_getMainVideoLayer());
		//pDispLayer = gfx_getLayer( layer == screenMain ? gfx_getMainVideoLayer() : gfx_getPipVideoLayer());
		//DFBCHECK( pDispLayer->SetOpacity(pDispLayer, 255));
				
		interface_displayMenu(1);
		/* Release in reverse order to creation. */
		pImage->Release(pImage);
		pImageProvider->Release(pImageProvider);

		return 0;
	} 
	else
		goto create_error;

decode_error:
	pImage->Release(pImage);
surface_error:
	pImageProvider->Release(pImageProvider);
create_error:
	mysem_release(gfx_semaphore);

	interface_setBackground(0, 0, 0, 0x88, INTERFACE_WALLPAPER_IMAGE);

	return 1;
}

void  gfx_hideImage(int layer)
{
#ifdef STBPNX
	IDirectFBDisplayLayer *pDispLayer;

	pDispLayer = gfx_getLayer(layer == screenMain ? gfx_getMainVideoLayer() : gfx_getPipVideoLayer());
	DFBCHECK (pDispLayer->SetOpacity(pDispLayer, 0));
#endif
}

void  gfx_showImage(int layer)
{
#ifdef STBPNX
	IDirectFBDisplayLayer *pDispLayer;

	pDispLayer = gfx_getLayer(layer == screenMain ? gfx_getMainVideoLayer() : gfx_getPipVideoLayer());
	DFBCHECK( pDispLayer->SetOpacity(pDispLayer, 255));
#endif
	interface_disableBackground();
}

stb810_gfxImageEntry *gfx_findImageEntryByName( const char* filename )
{
	stb810_gfxImageEntry * pEntry = NULL;
	if (filename == NULL)
		return NULL;
	pEntry = pgfx_ImageList;
	while (pEntry && strcmp(filename,pEntry->filename))
	{
		pEntry = pEntry->pNext;
	}
	return pEntry;
}

static void gfx_freeImageEntry(stb810_gfxImageEntry* pEntry)
{
	if(!pEntry || !pEntry->pImage)
		return;
	dprintf("gfx: Releasing image '%s'...\n", pEntry->filename);
	pEntry->pImage->Release(pEntry->pImage);
	FREE(pEntry->filename);
}

void gfx_releaseImageEntry( stb810_gfxImageEntry* pImageEntry )
{
	if (pImageEntry != NULL)
	{/* Free off / release the memory used by the image */
		dprintf("%s: Freeing of image surface '%s' %dx%d\n", __FUNCTION__, pImageEntry->filename, pImageEntry->width, pImageEntry->height);
		//eprintf("%s: Freeing of image surface '%s' %dx%d\n", __FUNCTION__, pImageEntry->filename, pImageEntry->width, pImageEntry->height);
		gfx_removeImageFromList(pImageEntry, &pgfx_ImageList);
		gfx_addImageToList(pImageEntry, &pgfx_FreeList);
		gfx_freeImageEntry(pImageEntry);
	}
}

static IDirectFBSurface * gfx_decodeImageInternal(const char* img_source, const char* url, int width, int height, int stretchToSize)
{
	stb810_gfxImageEntry*   pEntry;
	DFBSurfaceDescription   surfaceDesc;
	DFBRectangle            actualRect;
	DFBSurfaceDescription   imageDesc;
	DFBResult               result;
	/* The decoder for a given image file type. */
	IDirectFBImageProvider *pImageProvider = NULL;	

	if (!helperFileExists (img_source))
		return NULL;

	//dprintf("%s: load image '%s'\n", __FUNCTION__, img_source);

	if(pgfx_dfb->CreateImageProvider(pgfx_dfb, img_source, &pImageProvider) != DFB_OK )
		return NULL;

	mysem_get(gfx_semaphore);

	//dprintf("%s: loaded image '%s'\n", __FUNCTION__, img_source);
	
	DFBCHECKLABEL(pImageProvider->GetSurfaceDescription(pImageProvider, &imageDesc), decode_error);

	//dprintf("%s: %s image size: %d %d (stretch %d)\n", __FUNCTION__, img_source, imageDesc.width, imageDesc.height, stretchToSize);

	surfaceDesc = imageDesc;
	if (stretchToSize)
	{
		/* Resize image to specified width and height */
		if (width != 0)
		{
			surfaceDesc.width = width;
		}
		if (height != 0)
		{
			surfaceDesc.height = height;
		}
	} 
	else
	{
		/* Use specified with and height as maximal values */
		if ( surfaceDesc.width > width && width > 0 )
		{
			surfaceDesc.height = surfaceDesc.height*width/surfaceDesc.width;
			surfaceDesc.width = width;
		}
		if ( surfaceDesc.height > height && height > 0 )
		{
			surfaceDesc.width = surfaceDesc.width*height/surfaceDesc.height;
			surfaceDesc.height = height;
		}
	}

	//dprintf("%s: surface desc: %dx%d\n", __FUNCTION__, surfaceDesc.width, surfaceDesc.height);

	//dprintf("%s: Creating image surface\n", __FUNCTION__);

	pEntry = pgfx_FreeList;
		
	//eprintf ("%s: CreateSurface err = %p\n", __FUNCTION__, err);
		
	while ( !pEntry || (result = pgfx_dfb->CreateSurface(pgfx_dfb, &surfaceDesc, &(pEntry->pImage))) != DFB_OK )
	{
		/* Probably not enough memory available - free off the oldest image */
		stb810_gfxImageEntry* pFree;

		dprintf("%s: Failed to create image surface for '%s'\n", __FUNCTION__, url);
		//eprintf("%s: CreateSurface rets %d, Failed to create image surface for '%s'\n", __FUNCTION__, result, url);

		pFree = pgfx_ImageList;
		//dprintf("gfx: Starting at %p\n", pFree);
		while ( pFree && pFree->pNext )
		{
			//dprintf("%p ---> %p\n", pFree, pFree->pNext);
			pFree = pFree->pNext;
		}
		//dprintf("gfx: Using %p\n", pFree);
		if ( pFree )
		{
			gfx_releaseImageEntry(pFree);
			if( !pEntry )
				pEntry = pFree;
		} else
		{
			eprintf("%s: Given up trying to create image surface\n", __FUNCTION__);
			/* No more memory to free off - give up! */
			goto decode_error;
		}
	}

	actualRect.w = surfaceDesc.width;
	actualRect.h = surfaceDesc.height;
	actualRect.x = actualRect.y = 0;
	/* Render the image */
	DFBCHECKLABEL(pImageProvider->RenderTo(pImageProvider,pEntry->pImage, &actualRect), render_error);

	/* Release in reverse order to creation. */
	pImageProvider->Release(pImageProvider);
	
	gfx_removeImageFromList (pEntry, &pgfx_FreeList);
	helperSafeStrCpy (&pEntry->filename, url);
	pEntry->width = width;
	pEntry->height = height;
	pEntry->stretch = stretchToSize;
	//dprintf("%s: Adding new surface to store '%s' %dx%d %d\n", __FUNCTION__, pEntry->filename, pEntry->width, pEntry->height, stretchToSize);
	gfx_addImageToList(pEntry, &pgfx_ImageList);
	mysem_release(gfx_semaphore);
	return pEntry->pImage;
render_error:
	eprintf("gfx: Can't render image\n");
	pEntry->pImage->Release(pEntry->pImage);
	pEntry->pImage = NULL;
decode_error:
	eprintf("gfx: Failed to decode image '%s'\n", url);
	pImageProvider->Release(pImageProvider);
	mysem_release(gfx_semaphore);
	return NULL;
}

static void gfx_updateImage(int index, void *pArg )
{
	gfxImageInfo_t   *info = (gfxImageInfo_t*)pArg;
	IDirectFBSurface *pImage = NULL;

	//assert( info != NULL )

	if (helperFileExists (info->filename))
		pImage = gfx_decodeImageInternal (info->filename, info->url, info->width, info->height, info->stretchToSize);

	downloader_cleanupTempFile (info->filename);

	if (pImage && interfaceInfo.currentMenu == info->pMenu && !info->noUpdate)
	{
		interface_displayMenu(1);
	}

	dfree (info->url);
	dfree (info);
}

IDirectFBSurface * gfx_decodeImage (const char* filename, int width, int height, int stretchToSize)
{
	int foundImage = 0;
	IDirectFBSurface* pImage = NULL;
	stb810_gfxImageEntry* pEntry;
	int index;
	gfxImageInfo_t *info;

	pEntry = pgfx_ImageList;

	//dprintf("%s: '%s' %dx%d stretch %d\n", __FUNCTION__, filename, width, height, stretchToSize);
	//eprintf("%s: '%s' %dx%d stretch %d\n", __FUNCTION__, filename, width, height, stretchToSize);

	/* Find the entry in the decode table */
	while (pEntry && !foundImage)
	{
		if (!strcmp(filename, pEntry->filename) &&
			(pEntry->stretch == stretchToSize))
		{
			foundImage = 1;
		} else
		{
			pEntry = pEntry->pNext;
		}
	}

	/* Not found - decode the image */
	if (!foundImage)
	{
		if (strncasecmp (filename, "http", 4) == 0)
		{
			//dprintf("%s: from url '%s' %dx%d\n", __FUNCTION__, filename, width, height );
			//eprintf("%s: from url '%s' %dx%d\n", __FUNCTION__, filename, width, height );
			index = downloader_find (filename);
			if (index < 0)
			{
				info = dmalloc (sizeof(gfxImageInfo_t));
				if (info != NULL)
 				{
					int result;
					info->url = NULL;
					helperSafeStrCpy (&info->url, filename);
					info->filename[0] = 0;
					info->width = width;
					info->height = height;
					info->stretchToSize = stretchToSize;
					info->pMenu = interfaceInfo.currentMenu;
					
					result = downloader_push (info->url, 
					                    info->filename, sizeof(info->filename), 
					                    GFX_IMAGE_DOWNLOAD_SIZE, 
					                    gfx_updateImage, 
					                    (void*)info);
					if (result < 0)
					{
						eprintf("%s: Can't start image download: pool is full!\n", __FUNCTION__);
						dfree (info);
					}
				}
			}
		} else
		{
			//dprintf("%s: Creating image '%s'... \n", __FUNCTION__, filename);
			pImage = gfx_decodeImageInternal (filename, filename, width, height, stretchToSize);
		}
	} else
	{
		//dprintf("%s: Re-using image surface '%s' %dx%d, %d\n", __FUNCTION__, pEntry->filename, pEntry->width, pEntry->height, stretchToSize);
		/* Move the image entry to the front of the list - preserve it for as long as possible */
		gfx_removeImageFromList(pEntry, &pgfx_ImageList);
		gfx_addImageToList(pEntry, &pgfx_ImageList);
		pImage = pEntry->pImage;
	}

	/* Return the surface */
	return pImage;
}

IDirectFBSurface * gfx_decodeImageNoUpdate (const char* filename, int width, int height, int stretchToSize)
{
	int foundImage = 0;
	IDirectFBSurface* pImage = NULL;
	stb810_gfxImageEntry* pEntry;
	int index;
	gfxImageInfo_t *info;

	pEntry = pgfx_ImageList;

	while (pEntry && !foundImage)
	{
		if (!strcmp(filename, pEntry->filename) && (pEntry->stretch == stretchToSize))
		{
			foundImage = 1;
		} 
		else
		{
			pEntry = pEntry->pNext;
		}
	}

	if (!foundImage)
	{
		if (strncasecmp (filename, "http", 4) == 0)
		{
			index = downloader_find (filename);
			if (index < 0)
			{
				info = dmalloc (sizeof(gfxImageInfo_t));
				if (info != NULL)
 				{
					int result;
					info->url = NULL;
					helperSafeStrCpy (&info->url, filename);
					info->filename[0] = 0;
					info->width = width;
					info->height = height;
					info->stretchToSize = stretchToSize;
					info->pMenu = interfaceInfo.currentMenu;
					info->noUpdate = 1;                     // make no screen refresh
					
					result = downloader_push (info->url, 
					                          info->filename, sizeof(info->filename), 
					                          GFX_IMAGE_DOWNLOAD_SIZE, 
					                          gfx_updateImage, 
					                          (void*)info);
					if (result < 0)
					{
						eprintf("%s: Can't start image download: pool is full!\n", __FUNCTION__);
						dfree (info);
					}
				}
			}
		} 
		else {
			pImage = gfx_decodeImageInternal (filename, filename, width, height, stretchToSize);
		}
	} 
	else
	{
		gfx_removeImageFromList (pEntry, &pgfx_ImageList);
		gfx_addImageToList (pEntry, &pgfx_ImageList);
		pImage = pEntry->pImage;
	}
	return pImage;
}

int gfx_videoProviderIsActive(int videoLayer)
{
	return
#ifdef STBxx
		gfx_videoProvider.instance != NULL &&
#endif
		(gfx_videoProvider.active || gfx_videoProvider.paused);
}

int gfx_videoProviderIsPaused(int videoLayer)
{
	return
#ifdef STBxx
		gfx_videoProvider.instance != NULL &&
#endif
		gfx_videoProvider.paused;
}

int gfx_videoProviderIsCreated(int videoLayer, const char *videoSource)
{
	return
#ifdef STBxx
		gfx_videoProvider.instance != NULL &&
#endif
		strcmp(gfx_videoProvider.name, videoSource) == 0;
}

int gfx_setSpeed(int videoLayer, double multiplier)
{
	int result = 1;

#ifdef STSDK
	if (gfx_videoProvider.active == providerInit)
	{
		pprintf("%s(%d): not ready\n", __FUNCTION__, videoLayer);
		return result;
	}
#endif
	pprintf("%s(%d): %.1f\n", __FUNCTION__, videoLayer, multiplier);

#ifdef STB6x8x
	if (gfx_videoProvider.instance && gfx_videoProvider.active)
	{
		mysem_get(gfx_semaphore);

		if (gfx_videoProvider.instance->SetSpeed != NULL)
		{
			DFBResult dfbResult = gfx_videoProvider.instance->SetSpeed (
			                               gfx_videoProvider.instance, multiplier);
			result = (dfbResult == DFB_OK) ? 0 : 1;
			if (result != 0)
			{
				eprintf("gfx: Failed to set speed to %f\n", multiplier);
			}
		}

		mysem_release(gfx_semaphore);
	}
#endif
#ifdef STSDK
	elcdRpcType_t type;
	int           ret;
	cJSON        *res = NULL;
	cJSON        *param = cJSON_CreateNumber(multiplier);
	if (!param) return result;

	ret = st_rpcSync (elcmd_setspeed, param, &type, &res);
	
	if (!ret && type == elcdRpcResult && 
	    res && res->type == cJSON_String &&
		!strcmp (res->valuestring, "ok"))
	{
		result = 0;
	} 
	else 
	{
		eprintf("%s: failed to set speed to %f\n", __FUNCTION__, multiplier);
	}
	cJSON_Delete(res);
	cJSON_Delete(param);
#endif
	return result;
}

DFBVideoProviderStatus gfx_getVideoProviderStatus (int videoLayer)
{
	DFBVideoProviderStatus result = DVSTATE_PLAY;
#ifdef STBxx
	if (gfx_videoProvider.instance && gfx_videoProvider.active)
	{
		mysem_get(gfx_semaphore);

		if (gfx_videoProvider.instance->GetStatus != NULL)
		{
			DFBCHECK (gfx_videoProvider.instance->GetStatus(gfx_videoProvider.instance, &result));
		}

		mysem_release(gfx_semaphore);
	}
#endif
#ifdef STSDK
	if (gfx_videoProvider.active == providerInit)
	{
		pprintf("%s(%d): not ready\n", __FUNCTION__, videoLayer);
		return result;
	}

	elcdRpcType_t type;
	cJSON        *res = NULL;
	int           ret;

	ret = st_rpcSync (elcmd_state, NULL, &type, &res);
	if (ret == 0 && type == elcdRpcResult && 
	    res && res->type == cJSON_String)
	{
		// "paused" equal to "play"
		if (!strcmp (res->valuestring, "stopped"))
		{
			result = DVSTATE_FINISHED;
		}
		else if (!strcmp (res->valuestring, "ready"))
		{
			result = DVSTATE_STOP;
		}
		// not used yet
		//else if (strcmp (res->valuestring, "buffering") == 0)
		//	result = DVSTATE_BUFFERING;
	}
	cJSON_Delete(res);
#endif // STSDK
#ifdef ENABLE_GSTREAMER
	result = gstreamer_getStatus();
#endif
	pprintf("%s(%d): 0x%08x\n", __FUNCTION__, videoLayer, result);

	return result;
}

void gfx_setVideoProviderPlaybackFlags (int videoLayer, DFBVideoProviderPlaybackFlags playbackFlags, int value)
{
	int mode = appControlInfo.mediaInfo.vidProviderPlaybackMode;
	if (value)
	{
		mode = mode | playbackFlags;
	}
	else 
	{
		mode = mode & ~playbackFlags;
	}

	appControlInfo.mediaInfo.vidProviderPlaybackMode = mode;
#ifdef STBxx
	if (gfx_videoProvider.instance /*&& gfx_videoProvider.active*/ )
	{
		mysem_get(gfx_semaphore);
		if (gfx_videoProvider.instance->SetPlaybackFlags != NULL)
		{
			DFBCHECK (gfx_videoProvider.instance->SetPlaybackFlags(gfx_videoProvider.instance, 
			                                                                  (DFBVideoProviderPlaybackFlags)mode));
		}
		mysem_release(gfx_semaphore);
	}
#endif
}


int gfx_enableVideoProviderVerimatrix(int videoLayer, const char *inifile)
{
#ifdef STBPNX
	DFBEvent provEvent;
	DFBResult dfbResult;

	if (!gfx_videoProvider.instance) return 0;
	
	provEvent.clazz = DFEC_USER;
	provEvent.user.type = userEventEnableVerimatrix;
	provEvent.user.data = (void*)inifile;

	dfbResult = gfx_videoProvider.instance->SendEvent(gfx_videoProvider.instance, &provEvent);
	return (dfbResult == DFB_OK) ? 0 : (-1);
	if (dfbResult != DFB_OK)
	{
		return -1;
	}
#endif
	return 0;
}

int gfx_enableVideoProviderSecureMedia(int videoLayer)
{
#ifdef STBPNX
	DFBEvent provEvent;

	if (!gfx_videoProvider.instance) return 0;
	
	provEvent.clazz = DFEC_USER;
	provEvent.user.type = userEventEnableSecureMedia;
	provEvent.user.data = NULL;

	if (gfx_videoProvider.instance->SendEvent(gfx_videoProvider.instance, &provEvent) != DFB_OK)
	{
		return -1;
	}
#endif
	return 0;
}

int gfx_resumeVideoProvider(int videoLayer)
{
	int result = 0;
	pprintf("%s(%d): in\n", __FUNCTION__, videoLayer);
#ifdef STBxx
	IDirectFBSurface * pSurface;

	if (gfx_videoProvider.instance != NULL)
	{
		mysem_get(gfx_semaphore);
	
		pSurface = gfx_getSurface(videoLayer);

		DFBCHECK(result = gfx_videoProvider.instance->PlayTo(gfx_videoProvider.instance, 
		                                                                 pSurface, NULL, NULL, NULL) );

		gfx_videoProvider.paused = 0;
		gfx_videoProvider.active = 1;

		if ((appControlInfo.playbackInfo.audioStatus == audioMute) && (videoLayer == screenMain))
		{
			appControlInfo.playbackInfo.audioStatus = audioMain;
			sound_setVolume(appControlInfo.soundInfo.muted ? 0 : appControlInfo.soundInfo.volumeLevel);
		}

		mysem_release(gfx_semaphore);
	}
#endif
#ifdef STSDK
	if (gfx_videoProvider.waiting >= 0)
	{
		pprintf("%s(%d): not ready\n", __FUNCTION__, videoLayer);
		return 0;
	}

//	elcdRpcType_t type;
	cJSON        *param = NULL;

	if (gfx_videoProvider.savedPos > 0){
		param = cJSON_CreateNumber((int)gfx_videoProvider.savedPos);
		pprintf("%s: elcmd_play, startPos = %d, play\n", __func__, (int)gfx_videoProvider.savedPos);
	}
	else {
		pprintf("%s: elcmd_play, play\n", __func__);
	}

	gfx_videoProvider.waiting = st_rpcAsync( elcmd_play, param, gfx_videoProviderStarted, NULL);
	if (gfx_videoProvider.waiting >= 0)
		gfx_videoProvider.active = providerInit;
	cJSON_Delete(param);

#endif
#ifdef ENABLE_GSTREAMER
	result = gstreamer_resume();
#endif
	return result;
}

#ifdef STSDK

static size_t http_getinfo_cb(char* ptr, size_t size, size_t nmemb, void* userp)
{
	if (strstr(ptr, "VCinema-Content-Duration:") != NULL){
		sscanf(ptr, "VCinema-Content-Duration: %lf\n", &gfx_videoProvider.httpDuration);
	}
	return size*nmemb;
}

float getHttpVideoLength (const char * videoSource)
{
	CURL     *curl;
	CURLcode  res;

	gfx_videoProvider.httpDuration = 0.0;

	curl = curl_easy_init();
	if(!curl) {
		eprintf("%s: failed to init curl!\n", __FUNCTION__);
		return 0;
	}

	curl_easy_setopt(curl, CURLOPT_URL,            videoSource);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10);
	curl_easy_setopt(curl, CURLOPT_HEADER,         1L);
	curl_easy_setopt(curl, CURLOPT_NOBODY,         1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  http_getinfo_cb);
	appInfo_setCurlProxy(curl);
	
	res = curl_easy_perform(curl);
	(void)res;

	pprintf("%s: 0x%02x: %s. Duration = %.2f\n", __FUNCTION__, res, curl_easy_strerror(res), gfx_videoProvider.httpDuration);
	curl_easy_cleanup(curl);
	return gfx_videoProvider.httpDuration;
}	
#endif

#ifdef STSDK
void gfx_setStartPosition (int videoLayer, long posInSec)
{	
	gfx_videoProvider.savedPos = (double)posInSec;
	return;
}
#endif

void gfx_setVideoProviderName(const char* videoSource)
{
	size_t source_len = strlen(videoSource);
	if (source_len < sizeof(gfx_videoProvider.name))
		memcpy(gfx_videoProvider.name, videoSource, source_len+1);
	else
		gfx_videoProvider.name[0] = 0;
}

int gfx_startVideoProvider(const char* videoSource, int videoLayer, int force, char* options)
{
	int result = 0;

	pprintf("%s(%d): %d %s (%s)\n", __FUNCTION__, videoLayer, force, videoSource, options);
#ifdef STBxx
	IDirectFBSurface * pSurface;

	mysem_get(gfx_semaphore);
#ifdef STBPNX
	IDirectFBDisplayLayer* pDispLayer = gfx_getLayer(videoLayer ? gfx_getPipVideoLayer() : gfx_getMainVideoLayer());
	pSurface = gfx_getSurface(videoLayer);
#else
	pSurface = NULL;
#endif
	if ( force || strcmp(videoSource, gfx_videoProvider.name) )
	{
		int err;
		if (gfx_videoProvider.instance)
		{

			eprintf("gfx: Stopping previous video provider\n");
			gfx_videoProvider.instance->Stop(gfx_videoProvider.instance);
			eprintf("gfx: Releasing previous video provider\n");
			gfx_videoProvider.instance->Release(gfx_videoProvider.instance);
			gfx_videoProvider.instance = NULL;
			gfx_videoProvider.paused = 0;

#ifdef STBPNX
			/* Hide the video layer */
			DFBCHECK( pDispLayer->SetOpacity(pDispLayer, 0));
#endif
		}
		/* Create the video provider */
		dprintf("gfx: Creating new video provider\n");
		err = pgfx_dfb->CreateVideoProvider(pgfx_dfb, videoSource, &gfx_videoProvider.instance);
		if ( err != DFB_OK )
		{
			eprintf("gfx: %s not supported by installed video providers!\n", videoSource);
			result = -1;
		}
		gfx_setVideoProviderName(videoSource);
	}

	if ( gfx_videoProvider.instance )
	{
		dprintf("gfx: Displaying video %s on layer %d\n", videoSource, videoLayer);
#ifdef STB6x8x
		DFBCHECK(result = gfx_videoProvider.instance->PlayTo(gfx_videoProvider.instance, pSurface, NULL, NULL, options) );

		if (result != DFB_OK)
		{
			gfx_videoProvider.instance->Release(gfx_videoProvider.instance);
			gfx_videoProvider.instance = NULL;
			mysem_release(gfx_semaphore);
			return -1;
		}

		if (result != DFB_OK)
		{
			gfx_videoProvider.instance->Release(gfx_videoProvider.instance);
			gfx_videoProvider.instance = NULL;
			gfx_videoProvider.paused = 0;
			mysem_release(gfx_semaphore);
			return -1;
		}

		if (result == DFB_OK && appControlInfo.inStandby && gfx_videoProvider.savedPos!=0)  {
			gfx_videoProvider.instance->SeekTo(gfx_videoProvider.instance, 
			                                               gfx_videoProvider.savedPos);
		}
#endif // STB6x8x
#ifdef STB225
		DFBStreamAttributes streamAttr;
		char *video_mode;
		char *audio_mode;

		/* Create DFB video provider event buffer No events enabled yet.*/
		DFBCHECK(gfx_videoProvider.instance->CreateEventBuffer(gfx_videoProvider.instance,
		&gfx_videoProvider.pEventBuffer));

		/* Create a separate events thread */
		(void)pthread_create (&gfx_videoProvider.event_thread, NULL, gfx_eventThread, (void*) videoLayer);
		/* Enable the DATA_EXHAUSTED and DVPET_STARTED/STOPPED and DVPET_STREAMCHANGE and DVPET_FATALERROR and DATALOW/HIGH event. */
		gfx_videoProvider.events = DVPET_DATAEXHAUSTED | 
		                                       DVPET_STARTED       | 
		                                       DVPET_STOPPED       | 
		                                       DVPET_STREAMCHANGE  | 
		                                       DVPET_FATALERROR    | 
		                                       DVPET_DATALOW       | 
		                                       DVPET_DATAHIGH;
		DFBCHECK(gfx_videoProvider.instance->EnableEvents(gfx_videoProvider.instance, 
		                                                              gfx_videoProvider.events));

		if (strstr(options, ":H264") != 0)
		{
			video_mode = VIDEO_MODE_H264;
		} else if (strstr(options, ":MPEG2") != 0) {
			video_mode = VIDEO_MODE_MPEG2;
		} else
			video_mode = VIDEO_MODE_NONE;

		(void)strcpy(streamAttr.video.encoding, video_mode);

		if (strstr(options, ":AAC") != 0)
		{
			audio_mode = AUDIO_MODE_AAC;
			streamAttr.audio.container_format=DACF_ADTS;
		} else if (strstr(options, ":AC3") != 0) {
			audio_mode = AUDIO_MODE_AC3;
		} else
			audio_mode = AUDIO_MODE_MPEG;

		(void)strcpy(streamAttr.audio.encoding, audio_mode);

		streamAttr.caps = DVSCAPS_VIDEO | DVSCAPS_AUDIO;
		streamAttr.drm.valid = DFB_FALSE;
        eprintf("%s: video_mode %s audio_mode %s\n", __FUNCTION__, video_mode, audio_mode);

		DFBCHECK( gfx_videoProvider.instance->SetStreamAttributes(gfx_videoProvider.instance, streamAttr) );

		mysem_release(gfx_semaphore);
		gfx_setVideoProviderPlaybackFlags(videoLayer, DVPLAY_LOOPING, (appControlInfo.mediaInfo.playbackMode == playback_looped));
		mysem_get(gfx_semaphore);

		DFBCHECK( gfx_videoProvider.instance->PlayTo(gfx_videoProvider.instance, pSurface, NULL, NULL, options) );

		if (appControlInfo.inStandby && gfx_videoProvider.savedPos!=0)  {
			gfx_videoProvider.instance->SeekTo(gfx_videoProvider.instance, gfx_videoProvider.savedPos);
		}

		gfx_videoProvider.videoPresent = false;
#endif // STB225
		gfx_videoProvider.active = 1;
		gfx_videoProvider.paused = 0;
#ifdef STBPNX
		/* Show the video layer */
		DFBCHECK( pDispLayer->SetOpacity(pDispLayer, 255));
		/* Apply color adjustments */
		DFBColorAdjustment adj;
		adj.flags = 0;

		if (appControlInfo.pictureInfo.saturation >= 0 && appControlInfo.pictureInfo.saturation < 0x10000)
		{
			adj.flags |= DCAF_SATURATION;
			adj.saturation = appControlInfo.pictureInfo.saturation&0xFFFF;
		}
		if (appControlInfo.pictureInfo.brightness >= 0 && appControlInfo.pictureInfo.brightness < 0x10000)
		{
			adj.flags |= DCAF_BRIGHTNESS;
			adj.brightness = appControlInfo.pictureInfo.brightness&0xFFFF;
		}
		if (appControlInfo.pictureInfo.contrast >= 0 && appControlInfo.pictureInfo.contrast < 0x10000)
		{
			adj.flags |= DCAF_CONTRAST;
			adj.contrast = appControlInfo.pictureInfo.contrast&0xFFFF;
		}
		pDispLayer->SetColorAdjustment(pDispLayer, &adj);
#endif // STBPNX
	}
// STBxx
#else
	if ( force || strcmp(videoSource, gfx_videoProvider.name) )
	{
		gfx_stopVideoProvider(videoLayer, 1, 1);

		gfx_videoProvider.name[0] = 0;
#ifdef STSDK
		gfx_videoProvider.active = providerInit;

		cJSON *param = NULL;

		if (gfx_videoProvider.savedPos > 0)
		{
			pprintf("%s: elcmd_play %s, posInSec = %d\n",
			    __func__, videoSource, (int)gfx_videoProvider.savedPos);
			param = cJSON_CreateArray();

			cJSON_AddItemToArray (param, cJSON_CreateString(videoSource) );
			cJSON_AddItemToArray (param, cJSON_CreateNumber((int)gfx_videoProvider.savedPos));
		} else
		{
			pprintf("%s: elcmd_play %s\n",
				__func__, videoSource);
			param = cJSON_CreateString(videoSource);
		}
		gfx_videoProvider.waiting = st_rpcAsync( elcmd_play, param, gfx_videoProviderStarted, NULL);

		if (gfx_videoProvider.waiting >= 0)
		{
			gfx_setVideoProviderName(videoSource);
			
			float len = getHttpVideoLength (gfx_videoProvider.name);
			if( len > 0.0 )
				eprintf ("%s: http len = %.2f\n", __FUNCTION__, len);

			result = 0;
		} else
		{
			gfx_videoProvider.active = 0;
		}
		cJSON_Delete(param);
#endif // STSDK
#ifdef ENABLE_GSTREAMER
		result = gstreamer_play(videoSource);
		if (result == 0)
		{
			gfx_setVideoProviderName(videoSource);
			gfx_videoProvider.active = 1;
			gfx_videoProvider.paused = 0;
		}
#endif
	} else
	{
		result = gfx_resumeVideoProvider(videoLayer);
	}
#endif // !STBxx
	if ( (appControlInfo.playbackInfo.audioStatus == audioMute) && (videoLayer == screenMain) )
	{
		appControlInfo.playbackInfo.audioStatus = audioMain;
		sound_setVolume(appControlInfo.soundInfo.muted ? 0 : appControlInfo.soundInfo.volumeLevel);
	}
#ifdef STBxx
	mysem_release(gfx_semaphore);
#endif
	return result;
}

#ifdef STSDK
static void gfx_videoProviderGetTimes(elcdRpcType_t type, cJSON *res, void* pArg)
{
	gfx_videoProvider.waiting = -1;

	if ( type == elcdRpcResult && res && res->type == cJSON_Object )
	{
		double position = st_getTimeValue(res, "current");
		double length   = gfx_videoProvider.httpDuration > 0.0 ? gfx_videoProvider.httpDuration :
	           st_getTimeValue(res, "total");
		if (position < length)
		{
			interface_playControlSlider(0, (unsigned int)length, (unsigned int)position);
			if (!interfaceInfo.showMenu && interface_playControlSliderIsVisible())
			{
				interface_displayMenu(1);
			}
		}
		pprintf("%s: %.2f/%.2f\n", __FUNCTION__, position, length);
	}
#ifdef TRACE_PROVIDERS
	else pprintf("%s: failed\n", __FUNCTION__);
#endif
	cJSON_Delete(res);
}

static int st_checkSuccess(elcdRpcType_t type, cJSON *res, const char *msg)
{
	if ( type != elcdRpcResult || !res || res->type != cJSON_String )
	{
		eprintf("%s: playback failed: %s\n", msg, res&&res->type==cJSON_String?res->valuestring:"unknown error");
		gfx_videoProvider.active = 0;
		return 0;
	}

	if ( strcmp(res->valuestring, "ok") )
	{
		eprintf("%s: playback not successfull: %s\n", msg, gfx_videoProvider.name, res->valuestring);
		gfx_videoProvider.active = 0;
		return 0;
	}

	return 1;
}

void gfx_videoProviderStarted(elcdRpcType_t type, cJSON *res, void* pArg)
{
#ifdef TRACE_PROVIDERS
	char *res_str = cJSON_Print(res);
	pprintf("%s: %s %s\n", __FUNCTION__, type==elcdRpcResult?"result":"error", res_str);
	FREE(res_str);
#endif
	gfx_videoProvider.waiting = -1;
	gfx_videoProvider.active  = 0;

	if (st_checkSuccess(type, res, __FUNCTION__))
	{
		gfx_videoProvider.active = 1;
		gfx_videoProvider.paused = 0;

		gfx_videoProvider.waiting = st_rpcAsync(elcmd_times, NULL, gfx_videoProviderGetTimes, NULL);
	}

	cJSON_Delete(res);
}
#endif // STSDK

#ifdef STB82
void gfx_setupTrickMode(int videoLayer, stb810_trickModeDirection direction, stb810_trickModeSpeed speed)
{
	double dfbSpeed = 1.0;
	
	if(direction == direction_backwards)
	{
		gfx_setVideoProviderPlaybackFlags( videoLayer, DVPLAY_REWIND, 1 );
	}
	else
	{
		gfx_setVideoProviderPlaybackFlags( videoLayer, DVPLAY_REWIND, 0 );
	}
	switch(speed)
	{
	case speed_1_32:
		dfbSpeed = (1.0/32.0);
		break;

	case speed_1_16:
		dfbSpeed = (1.0/16.0);
		break;

	case speed_1_8:
		dfbSpeed = (1.0/8.0);
		break;

	case speed_1_4:
		dfbSpeed = (1.0/4.0);
		break;

	case speed_1_2:
		dfbSpeed = (1.0/2.0);
		break;

	case speed_2:
		dfbSpeed = (2.0);
		break;

	case speed_4:
		dfbSpeed = (4.0);
		break;

	case speed_8:
		dfbSpeed = (8.0);
		break;

	case speed_16:
		dfbSpeed = (16.0);
		break;

	case speed_32:
		dfbSpeed = (32.0);
		break;

	case speed_1:
	default:
		dfbSpeed = (1.0);
		break;
	}

	gfx_videoProvider.instance->SetSpeed(gfx_videoProvider.instance,
		dfbSpeed);
}
#endif

int gfx_isTrickModeSupported(int videoLayer)
{
#ifdef STBxx
	DFBVideoProviderCapabilities caps = 0;

	if (gfx_videoProvider.instance != NULL && gfx_videoProvider.active != 0)
	{
		DFBCHECK( gfx_videoProvider.instance->GetCapabilities(gfx_videoProvider.instance,
		                                                                  &caps) );
		if ( (caps & DVCAPS_SPEED) )
		{
			return 1;
		}
	}
#endif
#ifdef STSDK
#warning unimplemented
#endif
	return 0;
}

#ifdef STBxx
static void gfx_ThreadTerm(void* pArg)
{
	mysem_release(gfx_semaphore);
	dimensionsThread = 0;
}

static void *gfx_getDimensionsThread(void *pArg)
{
#ifdef STB6x8x
	static int currentWidth = 0;
	static int currentHeight = 0;
#endif
	unsigned char notifyHls = 0;

	pthread_cleanup_push(gfx_ThreadTerm, pArg);

#ifndef STB225
	while(1)
#else
	while(gfx_keepDimensionsThreadAlive)
#endif
	{
		int screenWidth;
		int screenHeight;
		int width;
		int height;
		int new_x;
		int new_y;
#ifdef ENABLE_VIDIMAX
		unsigned char notifyVidimax = 0;
#endif			
		
#ifndef STB225
		pthread_testcancel();
		//sleep(1);
		usleep(200000);
		pthread_testcancel();
#else
		event_wait(gfxDimensionsEvent);
#endif
		mysem_get(gfx_semaphore);
		if ( gfx_videoProvider.instance && gfx_videoProvider.active
#ifdef STB225
		  && gfx_videoProvider.videoPresent
#endif
		   )
		{
			if ( gfx_isHDoutput() )
			{
				gfx_getDestinationRectangle(&screenWidth, &screenHeight);
			} else
			{
				if ( (appControlInfo.outputInfo.encConfig[0].tv_standard == DSETV_NTSC)
#ifdef STB225
					 || (appControlInfo.outputInfo.encConfig[0].tv_standard == DSETV_PAL_M)
#endif
				   )
				{
					screenWidth  = 720;
					screenHeight = 480;
				} else
				{
					screenWidth  = 720;
					screenHeight = 576;
				}
			}

			width  = screenWidth;
			height = screenHeight;

			//dprintf("%s: %dx%d\n", __FUNCTION__, width, height);

			//Get the dimensions of the stream			
			DFBSurfaceDescription videoDesc;
			DFBCHECKLABEL(gfx_videoProvider.instance->GetSurfaceDescription(gfx_videoProvider.instance, &videoDesc),release_semaphore);

			if ( ((appControlInfo.outputInfo.autoScale == videoMode_scale)
#ifdef STB225
			   || (appControlInfo.outputInfo.autoScale == videoMode_native)
			   ||  gfxUseScaleParams
#endif
				 )
#ifdef ENABLE_MULTI_VIEW
			     && appControlInfo.multiviewInfo.count <= 0
#endif
			   )
			{
				if ( videoDesc.flags&(DSDESC_WIDTH | DSDESC_HEIGHT) )
				{
					//eprintf("%s: GetSurfaceDescription %d, %d.\n", __FUNCTION__, videoDesc.width, videoDesc.height);
					
					if ( (videoDesc.width>0) && (videoDesc.width < screenWidth) )
					{
						width  = videoDesc.width;
					}
					if ( (videoDesc.height>0) && (videoDesc.height < screenHeight) )
					{
						height = videoDesc.height;
					}

#ifdef STB6x8x
					if (gfx_isHDoutput())
					{
						// square pixels
					} else
					{
						if (appControlInfo.outputInfo.aspectRatio == aspectRatio_4x3)
						{
							height = screenHeight == 576 ? height*12/11 : height*10/11;
						} else if (appControlInfo.outputInfo.aspectRatio == aspectRatio_16x9)
						{
							height = screenHeight == 576 ? height*16/11 : height*40/33;
						}
					}

					//dprintf("%s: GetSurfaceDescription(screenMain) %dx%d\n", __FUNCTION__, videoDesc.width, videoDesc.height);

					if ( appControlInfo.outputInfo.autoScale == videoMode_scale )
					{
						float widthProp;
						float heightProp;
						widthProp = (float)width/(float)screenWidth;
						heightProp = (float)height/(float)screenHeight;
						if ( widthProp > heightProp )
						{
							width  = (int)((float)width/widthProp);
							height = (int)((float)height/widthProp);
						} else
						{
							width  = (int)((float)width/heightProp);
							height = (int)((float)height/heightProp);
						}
					}
#endif // STB6x8x
#ifdef STB225
					if (gfxUseScaleParams)
					{
						width = gfxScaleWidth;
						height = gfxScaleHeight;
					}
					else
					{
						if (appControlInfo.outputInfo.autoScale == videoMode_scale)
						{
							float screenAspectRatio;
							float streamAspectRatio;
							float outputAspectRatio;

							//dprintf("%s: scale!\n", __FUNCTION__);

							/* Check stream information for valid aspect ratio */
							if ((gfx_videoProvider.streamDescription.video.aspect > 0.99) &&
								(gfx_videoProvider.streamDescription.video.aspect < 1.01))
								/* Not a valid aspect ratio - assume 16:9 */
								streamAspectRatio = ASPECT_RATIO_16x9;
							else
								streamAspectRatio = gfx_videoProvider.streamDescription.video.aspect;

							if (appControlInfo.outputInfo.aspectRatio == aspectRatio_4x3)
								screenAspectRatio = ASPECT_RATIO_4x3;
							else
								screenAspectRatio = ASPECT_RATIO_16x9;

							outputAspectRatio = streamAspectRatio / screenAspectRatio;
							if (outputAspectRatio < 1.00)
							{
								width = (int32_t)(screenWidth * outputAspectRatio);
								height = screenHeight;
							} else
							{
								width = screenWidth;
								height = (int32_t)(screenHeight / outputAspectRatio);
							}
						}
					}
#endif // STB225
				}
			}

			new_x = (screenWidth - width)/2;
			new_y = (screenHeight - height)/2;

#ifdef STB6x8x
			if ( (width != currentWidth) ||
				 (height != currentHeight) )
			{
				eprintf("gfx: Video dimensions are %d, %d (aspect %s)\n", 
				        width, height, 
				        appControlInfo.outputInfo.aspectRatio == aspectRatio_4x3 ? "4:3" : 
				            (appControlInfo.outputInfo.aspectRatio == aspectRatio_16x9 ? "16:9" : "n/a"));
				eprintf("%s: Video dimensions are %d, %d (aspect %s), x = %d, y = %d\n", __FUNCTION__, 
				        width, height, appControlInfo.outputInfo.aspectRatio == aspectRatio_4x3 ? "4:3" : 
				            (appControlInfo.outputInfo.aspectRatio == aspectRatio_16x9 ? "16:9" : "n/a"), 
				        new_x, new_y);
				/* Modify the display to fit the stream dimensions */
				gfx_setDestinationRectangle(gfx_getMainVideoLayer(), new_x, new_y, width, height, NUM_STEPS);

#ifdef ENABLE_VIDIMAX
				notifyVidimax = 1;
#endif
				notifyHls ++;
				currentWidth = width;
				currentHeight = height;
			}
#endif // STB6x8x
#ifdef STB225
			if (width < MIN_RECT_SIZE)
			{
				width = MIN_RECT_SIZE;
			}

			if (height < MIN_RECT_SIZE)
			{
				height = MIN_RECT_SIZE;
			}

			gfxScaleWidth = width;
			gfxScaleHeight = height;

			dprintf("%s: Dimensions are %d, %d\n", __FUNCTION__, width, height);

			//DBG_PRINT2(MY_DBG_UNIT, DBG_LEVEL_1, "Dimensions are %d, %d\n", width, height);
			/* Modify the display to fit the stream dimensions */
			gfx_setDestinationRectangle (gfx_getMainVideoLayer(), 
			                             new_x, new_y, width, height, 
			                             gfxUseScaleParams ? 1 : NUM_STEPS);

			if (videoDesc.flags&(DSDESC_WIDTH | DSDESC_HEIGHT))
			{
				width = 0;
				height = 0;
				if (videoDesc.width>0)
				{
					width  = videoDesc.width;
				}
				if (videoDesc.height>0)
				{
					height = videoDesc.height;
				}

				if ((width !=0) && (height !=0))
				{
					int32_t factor;
					switch (appControlInfo.outputInfo.zoomFactor)
					{
						case(zoomFactor_2):
							factor = 2;
							break;
						case(zoomFactor_4):
							factor = 4;
							break;
						case(zoomFactor_8):
							factor = 8;
							break;
						case(zoomFactor_16):
							factor = 16;
							break;
						default:
							factor = 1;
							break;
					}
					new_x = (width - (width/factor)) /2;
					new_y = (height - (height/factor)) /2;
					width = width/factor;
					height = height/factor;
					if (width < MIN_RECT_SIZE)
					{
						width = MIN_RECT_SIZE;
					}

					if (height < MIN_RECT_SIZE)
					{
						height = MIN_RECT_SIZE;
					}
					gfx_setSourceRectangle(gfx_getMainVideoLayer(), new_x, new_y, width, height, NUM_STEPS);
				}
			}
#endif // STB225
		}
		release_semaphore:
		mysem_release(gfx_semaphore);

		if (strstr(appControlInfo.mediaInfo.filename, ".m3u8") && (notifyHls == 2))
		{
			interface_hideLoading();
			interface_displayMenu(1);
			notifyHls = 0;
		}

#ifdef ENABLE_VIDIMAX
		if (notifyVidimax && appControlInfo.vidimaxInfo.active)
		{	
			vidimax_notifyVideoSize(new_x, new_y, width, height);
		}
#endif
	};

	pthread_cleanup_pop(1);
	return NULL;
}

void gfx_setDestinationRectangle(int videoLayer, int x, int y, int width, int height, int steps)
{
#ifdef STB225
	static int currentWidth  = 0;
	static int currentHeight = 0;
#endif
	x = ROUND_UP(x);
	y = ROUND_UP(y);
	width = ROUND_UP(width);
	height = ROUND_UP(height);

	dprintf("%s: %dx%d\n", __FUNCTION__, width, height);

#ifdef STB225
	if ((width != currentWidth) ||
		(height != currentHeight))
	{
		currentWidth = width;
		currentHeight = height;
#endif

		if (
#ifdef STB6x8x
			(videoLayer != DLID_PRIMARY) &&
#endif
			gfx_destinationLocation[videoLayer][0] >= 0 &&
			( steps > 1 )
		   )
		{
			if ( (x != gfx_destinationLocation[videoLayer][0]) ||
				 (y != gfx_destinationLocation[videoLayer][1]) ||
				 (width != gfx_destinationLocation[videoLayer][2]) ||
				 (height != gfx_destinationLocation[videoLayer][3]) )
			{
				IDirectFBSurface * pSurface;

#ifdef STBxx
				/* Get the surface on the main video layer */
				pSurface = gfx_getSurface(screenMain);
				if ( pSurface!=0 )
				{
					int i;
					int diff_x;
					int diff_y;
					int diff_width;
					int diff_height;
					int screenWidth;
					int screenHeight;

					DFBCHECKLABEL( pSurface->GetSize(pSurface, &screenWidth, &screenHeight),finish_rectangle);

					dprintf("%s: GetSize %dx%d\n", __FUNCTION__, screenWidth, screenHeight);

					diff_x = x - gfx_destinationLocation[videoLayer][0];
					diff_y = y - gfx_destinationLocation[videoLayer][1];
					diff_width = width - gfx_destinationLocation[videoLayer][2];
					diff_height = height - gfx_destinationLocation[videoLayer][3];
					for ( i=0; i<steps; i++ )
					{
						int new_x;
						int new_y;
						int new_width;
						int new_height;
						/* Calculate the next itermediate location of window */
						/* Note : Must be on a 2 pixel boundary              */
						new_x = ROUND_UP(gfx_destinationLocation[videoLayer][0] + ((diff_x * i)/steps));
						new_y = ROUND_UP(gfx_destinationLocation[videoLayer][1] + ((diff_y * i)/steps));
						new_width = ROUND_UP(gfx_destinationLocation[videoLayer][2] + ((diff_width * i)/steps));
						new_height = ROUND_UP(gfx_destinationLocation[videoLayer][3] + ((diff_height * i)/steps));
						/* Make sure we don't get any embarassing overshoots! */
						if ( (new_x+new_width)> (screenWidth) )
						{
							new_width = screenWidth-new_x;
						}
						if ( (new_y+new_height)> (screenHeight) )
						{
							new_height = screenHeight-new_y;
						}
						dprintf("gfx: Setting rectangle to %d, %d %dx%d\n", new_x, new_y, new_width, new_height);
						/* Wait for the hardware to stop accessing the screen */
						DFBCHECKLABEL( pgfx_dfb->WaitForSync(pgfx_dfb), finish_rectangle);
						/* Move the window */
						DFBCHECKLABEL( gfx_getLayer(videoLayer)->SetScreenRectangle(gfx_getLayer(videoLayer),
						                                                            new_x, new_y,
						                                                            new_width, new_height), finish_rectangle);
						usleep(FIELD_WAIT);
					}
				}
#endif // STBxx
			}
		}

	gfx_destinationLocation[videoLayer][0] = x;
	gfx_destinationLocation[videoLayer][1] = y;
	gfx_destinationLocation[videoLayer][2] = width;
	gfx_destinationLocation[videoLayer][3] = height;
	dprintf("gfx: Setting final rectangle to %dx%d %dx%d\n",
		gfx_destinationLocation[videoLayer][0],
		gfx_destinationLocation[videoLayer][1],
		gfx_destinationLocation[videoLayer][2],
		gfx_destinationLocation[videoLayer][3]);

	/* Wait for the hardware to stop accessing the screen */
	DFBCHECKLABEL( pgfx_dfb->WaitForSync(pgfx_dfb), finish_rectangle);

#ifdef STBxx
	/* Move the window */
	if ( videoLayer == DLID_PRIMARY )
	{
		IDirectFBDisplayLayer *pDispLayer;
		/* The configuration of the layer*/
		DFBDisplayLayerConfig layerConfig;
		pDispLayer = gfx_getLayer(videoLayer);
		pDispLayer->SetCooperativeLevel(pDispLayer, DLSCL_EXCLUSIVE );
		dprintf("gfx: Calling SetConfiguration for DLID_PRIMARY\n");
		memset(&layerConfig, 0, sizeof(DFBDisplayLayerConfig));
		layerConfig.height = gfx_destinationLocation[videoLayer][3];
		layerConfig.width = gfx_destinationLocation[videoLayer][2];
		layerConfig.flags = DLCONF_WIDTH | DLCONF_HEIGHT;
		DFBCHECK( pDispLayer->SetConfiguration( pDispLayer, &layerConfig) );

		dprintf("gfx: Calling SetVideoMode for DLID_PRIMARY\n");
		DFBCHECKLABEL( pgfx_dfb->SetVideoMode(pgfx_dfb,
											  gfx_destinationLocation[videoLayer][2],
											  gfx_destinationLocation[videoLayer][3],
											  32), finish_rectangle);

		dprintf("gfx: Calling Release for DLID_PRIMARY\n");
		if ( pgfx_frameBuffer != NULL )
		{
			DFBCHECK( pgfx_frameBuffer->Release(pgfx_frameBuffer));
		}
		pDispLayer->GetSurface(pDispLayer, &pgfx_frameBuffer);
	} else
	{
		IDirectFBDisplayLayer *pDispLayer;

		pDispLayer = gfx_getLayer(videoLayer);

		DFBCHECKLABEL( pDispLayer->SetScreenRectangle(pDispLayer,
			gfx_destinationLocation[videoLayer][0],
			gfx_destinationLocation[videoLayer][1],
			gfx_destinationLocation[videoLayer][2],
			gfx_destinationLocation[videoLayer][3]), finish_rectangle);
	}
#endif // STBxx
#ifdef STB225
	} // width != currentWidth && height != currentHeight
#endif
	finish_rectangle:;
}

#ifdef STB225
static void gfx_setMixerInfo(gfxMixer_t mixer, uint32_t backgroundColour, gfxLayerDisplay_t *displayInfo)
{
	DFBScreenMixerConfig mixerConfig;

	mixerConfig.flags = DSMCONF_LAYERS | DSMCONF_BACKGROUND;

	mixerConfig.background.a = (uint8_t)((backgroundColour >> 24) & 0xFF);
	mixerConfig.background.r = (uint8_t)((backgroundColour >> 16) & 0xFF);
	mixerConfig.background.g = (uint8_t)((backgroundColour >> 8) & 0xFF);
	mixerConfig.background.b = (uint8_t)(backgroundColour & 0xFF);

	mixerConfig.layers = 0;

	mixerConfig.layers |= (uint32_t)((displayInfo[0] == gfxLayerDisplayed) ? GRAPHICS_1_MASK : 0);
	mixerConfig.layers |= (uint32_t)((displayInfo[1] == gfxLayerDisplayed) ? GRAPHICS_2_MASK : 0);
	mixerConfig.layers |= (uint32_t)((displayInfo[2] == gfxLayerDisplayed) ? DISPLAY_1_MASK : 0);
	mixerConfig.layers |= (uint32_t)((displayInfo[3] == gfxLayerDisplayed) ? DISPLAY_2_MASK : 0);

	(void)pgfx_screen->SetMixerConfiguration(pgfx_screen, (int32_t)mixer, &mixerConfig);
}

void gfx_getVideoPlaybackSize(int *width, int *height) {
	DFBSurfaceDescription videoDesc;

	*width = *height = 0;

	videoDesc.flags = 0;

	if (gfx_videoProvider.instance) {
		mysem_get(gfx_semaphore);
		gfx_videoProvider.instance->GetSurfaceDescription(gfx_videoProvider.instance, &videoDesc);
		if ( videoDesc.flags&(DSDESC_WIDTH | DSDESC_HEIGHT) )
		{
			*width  = videoDesc.width;
			*height = videoDesc.height;
		}
		mysem_release(gfx_semaphore);
	}
}

void gfx_setSourceRectangle(int32_t videoLayer, int32_t x, int32_t y, int32_t width, int32_t height, int32_t steps)
{
	static int32_t currentWidth = 0;
	static int32_t currentHeight = 0;

	/* Current screen display rectangles */
	static int32_t gfx_sourceRectangle[4] = { -1, -1, -1, -1 };

	x = ROUND_UP(x);
	y = ROUND_UP(y);
	width = ROUND_UP(width);
	height = ROUND_UP(height);

	if ((width != currentWidth) ||
		(height != currentHeight))
	{
		currentWidth = width;
		currentHeight = height;
#ifdef STBPNX
		if ((gfx_sourceRectangle[0] >= 0) && (steps > 1))
		{
			if ((x != gfx_sourceRectangle[0]) ||
				(y != gfx_sourceRectangle[1]) ||
				(width != gfx_sourceRectangle[2]) ||
				(height != gfx_sourceRectangle[3]))
			{
				IDirectFBSurface * pSurface;

				/* Get the surface on the main video layer */
				pSurface = gfx_getSurface(screenMain);

				if (pSurface!=0)
				{
					int32_t i;
					int32_t diff_x;
					int32_t diff_y;
					int32_t diff_width;
					int32_t diff_height;

					diff_x = x - gfx_sourceRectangle[0];
					diff_y = y - gfx_sourceRectangle[1];
					diff_width = width - gfx_sourceRectangle[2];
					diff_height = height - gfx_sourceRectangle[3];
					for(i=0; i<steps; i++)
					{
						int32_t new_x;
						int32_t new_y;
						int32_t new_width;
						int32_t new_height;
						/* Calculate the next itermediate location of window */
						/* Note : Must be on a 2 pixel boundary              */
						new_x = ROUND_UP(gfx_sourceRectangle[0] + ((diff_x * i)/steps));
						new_y = ROUND_UP(gfx_sourceRectangle[1] + ((diff_y * i)/steps));
						new_width = ROUND_UP(gfx_sourceRectangle[2] + ((diff_width * i)/steps));
						new_height = ROUND_UP(gfx_sourceRectangle[3] + ((diff_height * i)/steps));

						//DBG_PRINT4(MY_DBG_UNIT, DBG_LEVEL_1, "Setting src rectangle to %d, %d %dx%d\n", new_x, new_y, new_width, new_height);
						/* Wait for the hardware to stop accessing the screen */
						DFBCHECKLABEL( pgfx_dfb->WaitForSync(pgfx_dfb), finish_rectangle);
						/* Move the window */
						DFBCHECKLABEL( gfx_getLayer(videoLayer)->SetSourceRectangle(gfx_getLayer(videoLayer),
						                                                            new_x, new_y,
						                                                            new_width, new_height), finish_rectangle);
						(void)usleep(FIELD_WAIT);
					}
				}
			}
		}
#endif // STBPNX
		gfx_sourceRectangle[0] = x;
		gfx_sourceRectangle[1] = y;
		gfx_sourceRectangle[2] = width;
		gfx_sourceRectangle[3] = height;
		/*DBG_PRINT4(MY_DBG_UNIT, DBG_LEVEL_1, "Setting final src rectangle to %dx%d %dx%d\n",
			gfx_sourceRectangle[0],
			gfx_sourceRectangle[1],
			gfx_sourceRectangle[2],
			gfx_sourceRectangle[3]);*/

		/* Wait for the hardware to stop accessing the screen */
		DFBCHECKLABEL( pgfx_dfb->WaitForSync(pgfx_dfb), finish_rectangle);
#ifdef STBPNX
		{
			IDirectFBDisplayLayer *pDispLayer;

			pDispLayer = gfx_getLayer(videoLayer);

			DFBCHECKLABEL( pDispLayer->SetSourceRectangle(pDispLayer,
			                                              gfx_sourceRectangle[0],
			                                              gfx_sourceRectangle[1],
			                                              gfx_sourceRectangle[2],
			                                              gfx_sourceRectangle[3]), finish_rectangle);
		}
#endif // STBPNX
		finish_rectangle:
		;
	}
}
/*
static void gfx_eventThreadTerm(void* pArg)
{
	int32_t videoLayer = (int32_t)pArg;

	gfx_videoProvider.eventThreadState = STOPPED;
}
*/
static void *gfx_eventThread(void *pArg)
{
	DFBResult ret;
	DFBEvent eventsInfo;
	int32_t videoLayer = (int32_t)pArg;

	//exStbDemo_threadRename("EventMonitor");
	gfx_videoProvider.eventThreadState = RUNNING;

	//DBG_PRINT1(MY_DBG_UNIT, DBG_LEVEL_1, "Event Thread Active Video Layer %d\n", videoLayer);
//	pthread_cleanup_push(gfx_eventThreadTerm, pArg);

	//printf("%s: in\n", __FUNCTION__);

	while(gfx_videoProvider.eventThreadState == RUNNING)
	{
		//printf("%s: running\n", __FUNCTION__);
		// Get DFB video provider events
		ret = gfx_videoProvider.pEventBuffer->WaitForEventWithTimeout(gfx_videoProvider.pEventBuffer, 0, 100);
		if(ret == DFB_OK)
		{
			/* Check to see if a command has been received */
			if (gfx_videoProvider.pEventBuffer->HasEvent(gfx_videoProvider.pEventBuffer) == DFB_OK)
			{
				//printf("%s: got event\n", __FUNCTION__);
dprintf("%s[%d]:  Has event\n",__FILE__,__LINE__);
				/* Now get the event (remove it from queue. */
				ret = gfx_videoProvider.pEventBuffer->GetEvent(gfx_videoProvider.pEventBuffer, &eventsInfo);
				if(ret == DFB_OK)
				{
dprintf("%s[%d]:  eventsInfo.videoprovider.type=%d\n", __FILE__, __LINE__, eventsInfo.videoprovider.type);
#if 0
					if (eventsInfo.videoprovider.type & DVPET_DATAEXHAUSTED)
					{
						(void)printf("gfx: Got event - DVPET_DATAEXHAUSTED from %s for VideoProvider for file: '%s'\n",
							eventsInfo.videoprovider.data_type == DVPEDST_AUDIO ? "AUDIO":"VIDEO",
							gfx_videoProvider.name);
						/* What we do when we get a DATA EXHAUSTED message depends on whether we have Audio & video or just one of them.*/
						if (gfx_videoProvider.streamDescription.caps == (DVSCAPS_AUDIO | DVSCAPS_VIDEO))
						{
							/* If we are the ASF or TS device we do something.*/
							if ((!strcmp(gfx_videoProvider.name, OUTPUT_DEVICE_ASF)) ||
								(!strcmp(gfx_videoProvider.name, DEMUX_DEVICE_TS)))
							{
								/* Is the event audio or video. */
								if(eventsInfo.videoprovider.data_type == DVPEDST_VIDEO)
								{
									appControlInfo.mediaInfo.endOfVideoDataReported = true;
								}
								else
								{
									appControlInfo.mediaInfo.endOfAudioDataReported = true;
								}
								if (!gfx_videoProvider.paused)
								{
									/* Set EndOfStream based on audio AND video.*/
									appControlInfo.mediaInfo.endOfStreamReported = (appControlInfo.mediaInfo.endOfVideoDataReported &&
									                                                appControlInfo.mediaInfo.endOfAudioDataReported);
									appControlInfo.mediaInfo.endOfStreamCountdown = 5;
								}
							}
						}
						/* Only Video*/
						else if (gfx_videoProvider.streamDescription.caps == (DVSCAPS_VIDEO))
						{
							/* Video Only ES should send DVPET_STOPPED when it has finished.  But ASF/TS files
							will send DATA_EXHAUSTED when they have finished.*/
							if ((!strcmp(gfx_videoProvider.name, OUTPUT_DEVICE_ASF)) ||
								(!strcmp(gfx_videoProvider.name, DEMUX_DEVICE_TS)))
							{
								/* This stream has Video only so we only care about Video data_type events. */
								if(eventsInfo.videoprovider.data_type == DVPEDST_VIDEO)
								{
									if (!gfx_videoProvider.paused)
									{
										appControlInfo.mediaInfo.endOfVideoDataReported = true;
										appControlInfo.mediaInfo.endOfStreamReported = true;
										appControlInfo.mediaInfo.endOfStreamCountdown = 5;
									}
								}
							}
						}
						else
						{
							/* Audio Only ES should send DVPET_STOPPED when it has finished.  But ASF/TS files
							will send DATA_EXHAUSTED when they have finished.*/
							if ((!strcmp(gfx_videoProvider.name, OUTPUT_DEVICE_ASF)) ||
								(!strcmp(gfx_videoProvider.name, DEMUX_DEVICE_TS)))
							{
								/* This stream has audio only so we only care about audio data_type events. */
								if(eventsInfo.videoprovider.data_type == DVPEDST_AUDIO)
								{
									if (!gfx_videoProvider.paused)
									{
										appControlInfo.mediaInfo.endOfAudioDataReported = true;
										appControlInfo.mediaInfo.endOfStreamReported = true;
										appControlInfo.mediaInfo.endOfStreamCountdown = 5;
									}
								}
							}
						}
						/* Collect stats.*/
						gfx_videoProvider.stats.DVPET_DATAEXHAUSTED++;
					}
					else
#endif
					if (eventsInfo.videoprovider.type & DVPET_STARTED)
					{
						(void)printf("gfx: Got event - DVPET_STARTED from %s for VideoProvider for file: '%s'\n",
						eventsInfo.videoprovider.data_type == DVPEDST_AUDIO ? "AUDIO":"VIDEO",
						gfx_videoProvider.name);

						if (gfx_videoProvider.streamDescription.caps == (DVSCAPS_AUDIO | DVSCAPS_VIDEO))
						{
							/* we have both so we should stay in state buffering until we have start both.*/
							if(eventsInfo.videoprovider.data_type == DVPEDST_VIDEO)
							{
								appControlInfo.mediaInfo.videoPlaybackStarted = true;
							}
							else
							{
								appControlInfo.mediaInfo.audioPlaybackStarted = true;
							}
							appControlInfo.mediaInfo.bufferingData = !( appControlInfo.mediaInfo.audioPlaybackStarted &&
							appControlInfo.mediaInfo.videoPlaybackStarted);
						}
						else
						{
							/* As we are only doing one format. so we are now playing.*/
							appControlInfo.mediaInfo.bufferingData = false;

							/* Update state variables otherwise after we have decided we are not buffering data
							we may decide we are which is bad.
							This could be caused by the File saying it is Video or Audio only.
							We then get a started event and mark bufferingData as false.
							We then get an updated StreamDescription indicating we have both Audio and Video.
							We then get a started event for the new format (either audio or video).
							The above logic would then have set one to true and then set buffering data
							to false.  Not correct.*/
							appControlInfo.mediaInfo.videoPlaybackStarted = true;
							appControlInfo.mediaInfo.audioPlaybackStarted = true;
						}
						if (eventsInfo.videoprovider.data_type == DVPEDST_VIDEO)
						{
							int32_t screenWidth;
							int32_t screenHeight;

							if (!gfx_videoProvider.paused)
							{
								gfx_getDestinationRectangle(&screenWidth, &screenHeight);
								
								gfx_setSourceRectangle (gfx_getMainVideoLayer(), 
								                       0, 0,
								                       gfx_videoProvider.streamDescription.video.width,
								                       gfx_videoProvider.streamDescription.video.height, 
								                       0);
									
								gfx_setDestinationRectangle (gfx_getMainVideoLayer(), 
								                            0, 0, screenWidth, screenHeight, 0);

								printf("%s[%d]:  %dx%d\n", __FILE__, __LINE__, 
								       gfx_videoProvider.streamDescription.video.width, 
								       gfx_videoProvider.streamDescription.video.height);
								printf("%s[%d]:  %dx%d\n", __FILE__, __LINE__, screenWidth, screenHeight);

								(void)event_send(gfxDimensionsEvent);

								/* Show the video layer */
								IDirectFBDisplayLayer* pDispLayer;
								pDispLayer = gfx_getLayer (videoLayer ? gfx_getPipVideoLayer() : gfx_getMainVideoLayer());
								DFBCHECK (pDispLayer->SetOpacity(pDispLayer, 255));
							}
							gfx_videoProvider.videoPresent = true;
						}
						gfx_videoProvider.paused = false;
						/* Collect stats.*/
						gfx_videoProvider.stats.DVPET_STARTED++;
					}
					else if (eventsInfo.videoprovider.type & DVPET_STOPPED)
					{
						(void)printf("gfx: Got event - DVPET_STOPPED from %s for VideoProvider for file: '%s'\n",
						             eventsInfo.videoprovider.data_type == DVPEDST_AUDIO ? "AUDIO":"VIDEO",
						             gfx_videoProvider.name);

						/* Video-only and Audio-only ES streams send a STOPPED event when the feeding thread has exited and
						the renderer has run out of data.*/
						if (!gfx_videoProvider.paused)
						{
							if (gfx_videoProvider.streamDescription.caps == (DVSCAPS_VIDEO))
							{
								appControlInfo.mediaInfo.endOfVideoDataReported = true;
								appControlInfo.mediaInfo.endOfStreamReported = true;
								appControlInfo.mediaInfo.endOfStreamCountdown = 5;
							}
							if (gfx_videoProvider.streamDescription.caps == (DVSCAPS_AUDIO))
							{
								appControlInfo.mediaInfo.endOfAudioDataReported = true;
								appControlInfo.mediaInfo.endOfStreamReported = true;
								appControlInfo.mediaInfo.endOfStreamCountdown = 5;
							}
						}
						/* Collect stats.*/
						gfx_videoProvider.stats.DVPET_STOPPED++;
					}
					else if (eventsInfo.videoprovider.type & DVPET_STREAMCHANGE)
					{
						/* Using this event means we have upto date stream information.*/
						//DBG_PRINT0(MY_DBG_UNIT, DBG_LEVEL_1, "Stream Description has changed getting update.\n");
						ret = gfx_videoProvider.instance->GetStreamDescription(gfx_videoProvider.instance,
						                                                                  &gfx_videoProvider.streamDescription);

						/* Print Stream Details.*/
						(void)printf("gfx: Stream Name:               %s\n", gfx_videoProvider.name);
						if ((gfx_videoProvider.streamDescription.caps & DVSCAPS_VIDEO) == (DVSCAPS_VIDEO))
						{
							(void)printf("Stream Video Type:         %s\n"
							             "Stream width:              %d\n"
							             "Stream height:             %d\n"
							             "Stream Framerate:          %f\n"
							             "Aspect Ratio:              %f\n",
							             gfx_videoProvider.streamDescription.video.encoding,
							             gfx_videoProvider.streamDescription.video.width,
							             gfx_videoProvider.streamDescription.video.height,
							             gfx_videoProvider.streamDescription.video.framerate,
							             gfx_videoProvider.streamDescription.video.aspect);
							             
							/* Force the display to be re-scaled */
							(void)event_send(gfxDimensionsEvent);
						}
						if ((gfx_videoProvider.streamDescription.caps & DVSCAPS_AUDIO) == (DVSCAPS_AUDIO))
						{
							(void)printf("Stream Audio Type:         %s\n"
							             "Stream Audio Bitrate:      %d\n"
							             "Stream Audio Num Channels: %d\n"
							             "Stream Audio Sample Rate:  %d\n",
							              gfx_videoProvider.streamDescription.audio.encoding,
							              gfx_videoProvider.streamDescription.audio.bitrate,
							              gfx_videoProvider.streamDescription.audio.channels,
							              gfx_videoProvider.streamDescription.audio.samplerate);

						}
						/* Collect stats.*/
						gfx_videoProvider.stats.DVPET_STREAMCHANGE++;
					}
					else if (eventsInfo.videoprovider.type & DVPET_FATALERROR)
					{
						if (eventsInfo.videoprovider.data_type == DVPEDST_VIDEO)
						{
							(void)printf("gfx: Fatal Error in Video Subsystem.  Suggest exiting and restarting :-).\n");
						}
						else
						{
							(void)printf("gfx: Fatal Error in Audio Subsystem.  Suggest exiting and restarting :-).\n");
						}
						/* Collect stats.*/
						gfx_videoProvider.stats.DVPET_FATALERROR++;
					}
					else if (eventsInfo.videoprovider.type & DVPET_DATALOW)
					{
						/* Collect stats.*/
						gfx_videoProvider.stats.DVPET_DATALOW++;
					}
					else if (eventsInfo.videoprovider.type & DVPET_DATAHIGH)
					{
						/* Collect stats.*/
						gfx_videoProvider.stats.DVPET_DATAHIGH++;
					}
					else
					{
						//DBG_PRINT2(MY_DBG_UNIT, DBG_LEVEL_1, "%s Got events - %08x\n", __FUNCTION__, eventsInfo.videoprovider.type);
					}
				}
				else
				{
					//DBG_PRINT4(MY_DBG_UNIT, DBG_LEVEL_1,  "%s <%d>: GetEvent returned %08x %s\n\t", __FUNCTION__, __LINE__, ret, DirectFBErrorString(ret));
				}
			}
		}
		else
		{
			if (ret != DFB_TIMEOUT)
			{
				//DBG_PRINT4(MY_DBG_UNIT, DBG_LEVEL_1,  "%s <%d>: WaitForEventWithTimeout returned %08x %s \n\t", __FUNCTION__, __LINE__, ret, DirectFBErrorString(ret));
			}
		}
	}
//	pthread_cleanup_pop(1);

	return NULL;
}

#endif // STB225
#endif // STBxx

#ifdef STBPNX
void gfx_hideVideoLayer (int videoLayer)
{	
	mysem_get(gfx_semaphore);
	
	IDirectFBDisplayLayer* pDispLayer;
	pDispLayer = gfx_getLayer (videoLayer ? gfx_getPipVideoLayer() : gfx_getMainVideoLayer());
	eprintf ("gfx: Hide layer %d (%s)\n", videoLayer ? gfx_getPipVideoLayer() : gfx_getMainVideoLayer(), 
	         videoLayer ? "PIP" : "MAIN");
	DFBCHECK (pDispLayer->SetOpacity(pDispLayer, 0));
	
	mysem_release(gfx_semaphore);
	return;
}

void gfx_showVideoLayer (int videoLayer)
{
	mysem_get(gfx_semaphore);
	
	IDirectFBDisplayLayer* pDispLayer;
	pDispLayer = gfx_getLayer(videoLayer ? gfx_getPipVideoLayer() : gfx_getMainVideoLayer());
	eprintf ("gfx: Show layer %d (%s)\n", videoLayer ? gfx_getPipVideoLayer() : gfx_getMainVideoLayer(), 
	         videoLayer ? "PIP" : "MAIN");
	DFBCHECK (pDispLayer->SetOpacity(pDispLayer, 0xFF));
	
	mysem_release(gfx_semaphore);
	return;
}

#endif

void gfx_stopVideoProvider(int videoLayer, int force, int hideLayer)
{
	pprintf("%s(%d): %s %s\n", __FUNCTION__, videoLayer, force?"force":"", hideLayer?"hide":"");
#ifdef STBPNX
	if (hideLayer)
	{
		gfx_hideVideoLayer(videoLayer);
	}
#endif
#ifdef STBxx
	if (gfx_videoProvider.instance && gfx_videoProvider.active )
	{
		mysem_get(gfx_semaphore);

		eprintf("gfx: Stopping current video provider (%s)\n", gfx_videoProvider.name);
		if (appControlInfo.inStandby)
		{
			gfx_videoProvider.instance->GetPos(gfx_videoProvider.instance, 
			                                              &gfx_videoProvider.savedPos);
		}
		else 
		{
			gfx_videoProvider.savedPos = 0;
		}
		
		gfx_videoProvider.instance->Stop(gfx_videoProvider.instance);
		gfx_videoProvider.active = 0;
		gfx_videoProvider.paused = 1;

#ifdef STB225
		/* Disable Events */
		gfx_videoProvider.instance->DisableEvents(gfx_videoProvider.instance, DVPET_ALL);

		/* Stop the event thread.
		 * pthread_cancel will cause a blocked read() or write() to exit.
		 * pthread_join will wait for this to occur, ensuring synchronisation. */
		if (gfx_videoProvider.eventThreadState == RUNNING)
		{
			//DBG_PRINT0(MY_DBG_UNIT, DBG_LEVEL_1, "Waiting for event thread to exit.\n");
			/* Set Flag for thread to exit then join and wait...*/
			gfx_videoProvider.eventThreadState = STOP_REQUESTED;
			(void)pthread_join(gfx_videoProvider.event_thread, NULL);
		}

		/* Clear stats.*/
		(void)memset(&gfx_videoProvider.stats, 0, sizeof(DFBEventBufferStats));
#endif

		if (videoLayer == screenMain && appControlInfo.playbackInfo.audioStatus == audioMain)
		{
			appControlInfo.playbackInfo.audioStatus = audioMute;
			sound_setVolume(-1);
		}
		mysem_release(gfx_semaphore);
	}

	if (force)
	{
		if (gfx_videoProvider.instance)
		{
			eprintf("gfx: Releasing current video provider\n");
			gfx_videoProvider.instance->Release(gfx_videoProvider.instance);
			gfx_videoProvider.instance = NULL;
			gfx_videoProvider.paused = 0;
		}
		gfx_videoProvider.name[0] = 0;
	}
#endif // STBxx
#ifdef STSDK
	if (gfx_videoProvider.waiting >= 0)
	{
		st_cancelAsync(gfx_videoProvider.waiting, 0);
		gfx_videoProvider.waiting = -1;
	}

	elcdRpcType_t type;
	cJSON        *res = NULL;
	int           ret;

	ret = st_rpcSync (force ? elcmd_stop : elcmd_pause, NULL, &type, &res);
	if (ret == 0 && type == elcdRpcResult && res && res->type == cJSON_String)
	{
		if (strcmp(res->valuestring, "ok") == 0)
		{
			gfx_videoProvider.paused = !force;
			gfx_videoProvider.active = 0;
			if (force) gfx_videoProvider.httpDuration = 0.0;
		}
		else 
		{
			eprintf("%s: failed: %s\n", __FUNCTION__, res->valuestring);
		}
	}
	cJSON_Delete(res);
	
	if (force == GFX_STOP)
	{
		gfx_videoProvider.name[0] = 0;
	}
#endif // STSDK
#ifdef ENABLE_GSTREAMER
	if (force)
		gstreamer_stop();
	else
		gstreamer_pause();
	gfx_videoProvider.paused = !force;
	gfx_videoProvider.active = 0;
#endif
#ifdef TRACE_PROVIDERS
#ifdef STBPNX
	pprintf ("%s(%d): out | active %d paused %d instance %d\n", __FUNCTION__, videoLayer,
		gfx_videoProvider.active, gfx_videoProvider.paused, gfx_videoProvider.instance != NULL);
#else
	pprintf ("%s(%d): out | active %d paused %d\n", __FUNCTION__, videoLayer,
		gfx_videoProvider.active, gfx_videoProvider.paused);
#endif
#else
	eprintf ("gfx: Video provider stopped\n");
#endif
}

void gfx_stopVideoProviders (int which)
{
	if ((which==screenMain) && appControlInfo.mediaInfo.active)
	{
		/* Stop any Media playback */
		media_stopPlayback();
	}

	/* Stop any exisiting rtp video display */
	if (appControlInfo.rtpInfo.active)
	{
		rtp_stopVideo(which);
	}

	/* Stop any exisiting rtsp video display */
	if (appControlInfo.rtspInfo.active)
	{
		rtsp_stopVideo(which);
	}

#ifdef ENABLE_DVB
	if (appControlInfo.dvbInfo.active)
	{
		/* Stop any DVB playback */
		offair_stopVideo(which, 1);
	}
#endif
}

#ifdef STSDK
static inline float st_getTimeValue (cJSON *object, const char *value_name)
{
	unsigned int hh, mm, ss;
	cJSON *pos = cJSON_GetObjectItem(object, value_name);
	if (pos && 
	    pos->type == cJSON_String &&
	    sscanf(pos->valuestring, "%02u:%02u:%02u", &hh, &mm, &ss) == 3)
	{
		return ss + 60 * mm + 3600 * hh;
	}
	return 0;
}
#endif

double gfx_getVideoProviderPosition(int videoLayer)
{
	double position = 0.0;
#ifdef STBxx
	mysem_get(gfx_semaphore);
	
	if (gfx_videoProvider.instance && gfx_videoProvider.active)
	{
		if (gfx_videoProvider.instance->GetPos(gfx_videoProvider.instance, &position) != DFB_OK)
		{
			/* Failed to get the current position - just return the end of the file! */
			mysem_release(gfx_semaphore);
			position = gfx_getVideoProviderLength(videoLayer);
			mysem_get(gfx_semaphore);
		}
	}
	mysem_release(gfx_semaphore);
#endif
#ifdef STSDK
	if (gfx_videoProvider.active == providerInit)
	{
		pprintf("%s(%d): not ready\n", __FUNCTION__, videoLayer);
		return position;
	}

	elcdRpcType_t type;
	cJSON        *res = NULL;
	int           ret;

	ret = st_rpcSync (elcmd_times, NULL, &type, &res);
	if (ret == 0 && type == elcdRpcResult && res && res->type == cJSON_Object)
	{
		position = st_getTimeValue(res, "current");
	}
	cJSON_Delete(res);
#endif
	pprintf("%s(%d): %.2f\n", __FUNCTION__, videoLayer, position);
	return position;
}

double gfx_getVideoProviderLength (int videoLayer)
{
	double length = 1;
#ifdef STBxx
	mysem_get(gfx_semaphore);
	if (gfx_videoProvider.instance && gfx_videoProvider.active)
	{
		double offset;
		if (gfx_videoProvider.instance->GetLength(gfx_videoProvider.instance, &offset) == DFB_OK)
		{
			length = offset;
		}
	}
	mysem_release(gfx_semaphore);
#endif
#ifdef STSDK
	if (gfx_videoProvider.active == providerInit)
	{
		pprintf("%s(%d): not ready\n", __FUNCTION__, videoLayer);
		return 0.0;
	}

	if (gfx_videoProvider.httpDuration > 0.0)
	{
		pprintf("%s(%d): http %.2f\n", __FUNCTION__, videoLayer, gfx_videoProvider.httpDuration);
		return gfx_videoProvider.httpDuration;
	}
	
	elcdRpcType_t type;
	cJSON        *res = NULL;
	int           ret;

	ret = st_rpcSync (elcmd_times, NULL, &type, &res);
	if (ret == 0 && type == elcdRpcResult && res && res->type == cJSON_Object)
	{
		length = st_getTimeValue(res, "total");
		if (length < 1.0) length = 1.0;
	}
	else {
		length = 0.0;
	}
	cJSON_Delete(res);
#endif
	pprintf("%s(%d): %.2f\n", __FUNCTION__, videoLayer, length);
	return length;
}

int gfx_getPosition (double *plength,double *pposition)
{
	double length   = 1.0;
	double position = 0.0;

#ifdef STBPNX
	length = gfx_getVideoProviderLength(screenMain);
#endif
#ifdef STSDK
	if (gfx_videoProvider.active == providerInit)
	{
		pprintf("%s: not ready\n", __FUNCTION__);
		*plength   = gfx_videoProvider.httpDuration;
		*pposition = 0.0;
		return 0;
	}

	elcdRpcType_t type;
	cJSON        *res = NULL;
	int           ret;
	
	ret = st_rpcSync (elcmd_times, NULL, &type, &res);
	if (ret == 0 && type == elcdRpcResult && res && res->type == cJSON_Object)
	{
		position = st_getTimeValue(res, "current");
		length   = gfx_videoProvider.httpDuration > 0.0 ? gfx_videoProvider.httpDuration :
		           st_getTimeValue(res, "total");
	} else {
		pprintf("%s: times failed\n", __FUNCTION__);
	}
	cJSON_Delete(res);
#endif
	if (length < 2.0)
	{
		pprintf("%s: stream length is not avaliable\n", __FUNCTION__);
		return -1;
	}
#ifdef STBPNX
	position = gfx_getVideoProviderPosition(screenMain);
#endif

	if (position>length)
	{
		pprintf("%s: pos %.2f > len %.2f %\n", __FUNCTION__, position, length);
		return -1;
	}

	*plength   = length;
	*pposition = position;

	pprintf("%s: %.2f/%.2f\n", __FUNCTION__, position, length);

	return 0;
}

void gfx_setVideoProviderPosition (int videoLayer, long position)
{
#ifdef STSDK
	if (gfx_videoProvider.active == providerInit)
	{
		pprintf("%s(%d): not ready\n", __FUNCTION__, videoLayer);
		return;
	}
#endif
	pprintf("%s(%d): %ld\n", __FUNCTION__, videoLayer, position);
#ifdef STBxx
	mysem_get(gfx_semaphore);
	if (gfx_videoProvider.instance && gfx_videoProvider.active)
	{
		dprintf("gfx: Setting video provider position: %ld seconds\n", position);
		eprintf("gfx: Setting video provider position: %ld seconds\n", position);
		gfx_videoProvider.instance->SeekTo(gfx_videoProvider.instance, (double)position);
	}
	mysem_release(gfx_semaphore);
#endif
#ifdef STSDK
	elcdRpcType_t type;
	cJSON        *param = cJSON_CreateNumber(position);
	cJSON        *res   = NULL;
	int           ret;

	ret = st_rpcSync (elcmd_setpos, param, &type, &res);
	if (ret == 0 && type == elcdRpcResult && res && res->type == cJSON_String)
	{
		if (strcmp(res->valuestring, "ok"))
		{
			eprintf("%s: failed: %s\n", __FUNCTION__, res->valuestring);
		}
	}
	cJSON_Delete(res);
	cJSON_Delete(param);
#endif
}

stb810_gfxStreamTypes_t gfx_getVideoProviderStreamType(int videoLayer)
{
	stb810_gfxStreamTypes_t streamType = stb810_gfxStreamTypesUnknown;
#ifdef STBxx
	mysem_get(gfx_semaphore);
	if (gfx_videoProvider.instance && gfx_videoProvider.active)
	{
		DFBStreamDescription streamDesc;
		if (gfx_videoProvider.instance->GetStreamDescription(gfx_videoProvider.instance, &streamDesc) == DFB_OK)
		{
			if (strcmp(streamDesc.video.encoding,"WMT") == 0)
			{
				streamType = stb810_gfxStreamTypesWMT;
			} else if (!strcmp(streamDesc.video.encoding,"DIVX"))
			{
				streamType = stb810_gfxStreamTypesDivx;
			} else if (!strcmp(streamDesc.video.encoding,"MPEGTS"))
			{
				streamType = stb810_gfxStreamTypesMpegTS;
			} else if (!strcmp(streamDesc.video.encoding,"MPEGPS"))
			{
				streamType = stb810_gfxStreamTypesMpegPS;
			} else if (!strcmp(streamDesc.video.encoding,"MPEG4"))
			{
				streamType = stb810_gfxStreamTypesMpeg4;
			} else if (!strcmp(streamDesc.video.encoding,"MP3"))
			{
				streamType = stb810_gfxStreamTypesMP3;
			} else if (!strcmp(streamDesc.video.encoding,"ANAVID"))
			{
				streamType = stb810_gfxStreamTypesAnalog;
			} else if (!strcmp(streamDesc.video.encoding,"H264ES"))
			{
				streamType = stb810_gfxStreamTypesH264ES;
			}
		}
	}
	dprintf("gfx: Got video provider stream type: %d\n", streamType);
	mysem_release(gfx_semaphore);
#endif
#ifdef STSDK
#warning unimplemented
	eprintf("%s: unimplemented\n", __FUNCTION__);
#endif
	return streamType;
}


DFBStreamCapabilities gfx_getVideoProviderCaps(int videoLayer)
{
	DFBStreamCapabilities streamCaps = DVSCAPS_VIDEO | DVSCAPS_AUDIO;
#ifdef STBxx
	mysem_get(gfx_semaphore);
	if (gfx_videoProviderIsActive(videoLayer))
	{
		DFBStreamDescription streamDesc;
		if (gfx_videoProvider.instance->GetStreamDescription(gfx_videoProvider.instance, &streamDesc) == DFB_OK)
		{
			streamCaps = streamDesc.caps;
		}
	}
	dprintf("gfx: Got video provider stream Caps: %x\n", streamCaps);
	mysem_release(gfx_semaphore);
#endif
#ifdef STSDK
//#warning unimplemented
	streamCaps |= DFBCAPS_LENGTH_IN_SEC;
#endif
	return streamCaps;
}

inline int gfx_getVideoProviderHasVideo (int videoLayer)
{
	return DVSCAPS_VIDEO == (DVSCAPS_VIDEO & gfx_getVideoProviderCaps(videoLayer));
}

#ifdef STBTI
void flipAttrBuffers()
{
	struct fb_var_screeninfo varInfo;

	if (ioctl (gfx_attributeBuffer.fd, FBIOGET_VSCREENINFO, &varInfo) == -1) {
		eprintf("gfx: Failed FBIOGET_VSCREENINFO (%s)\n", strerror(errno));
		return;
	}

	varInfo.yoffset = varInfo.yres * gfx_attributeBuffer.index;

	/* Swap the working buffer for the displayed buffer */
	if (ioctl (gfx_attributeBuffer.fd, FBIOPAN_DISPLAY, &varInfo) == -1) {
		eprintf("gfx: Failed FBIOPAN_DISPLAY (%s)\n", strerror(errno));
		return;
	}
	gfx_attributeBuffer.index = (gfx_attributeBuffer.index+1)%NUM_ATTR_BUFFERS;
}

/* Fills attribute display rectangle with opacity bits */
void gfx_setAttrRectangle (unsigned char alpha, int x, int y, int width, int height)
{
	//dprintf("%s: (%d %d %d %d = 0x%02X)\n", __FUNCTION__, x, y, width, height, alpha | (alpha << 4));

	if (x <= 0 && width >= interfaceInfo.screenWidth)
	{
		memset (gfx_attributeBuffer.screen[gfx_attributeBuffer.index] + y * gfx_attributeBuffer.width, 
		        alpha, 
		        height * gfx_attributeBuffer.width);
	} 
	else
	{
		int y_attr;
		/* Filling the half-bytes first */
		//dprintf("%s: (%02x %02x %02x)\n", __FUNCTION__,alpha, alpha << 4, alpha | (alpha << 4));
		if (x & 1)
		{
			/* X offset is odd, need to set high bits of first visible pixel in each line */
			for (y_attr = y; y_attr < y + height; ++y_attr) 
			{
				gfx_attributeBuffer.screen[gfx_attributeBuffer.index][y_attr * gfx_attributeBuffer.width + x / 2] = (alpha << 4);
			}
			if (!(width & 1))
			{
				/* X offset is odd and width is even - need to set low bits of last byte in each line */
				for (y_attr = y; y_attr < y + height; ++y_attr) {
					gfx_attributeBuffer.screen[gfx_attributeBuffer.index][y_attr * gfx_attributeBuffer.width + (x+width) / 2 ] = alpha;
				}
				/* We already filled two half-bytes */
				width--;
			}
		}
		/* Then full bytes: filling higher bits with the same mask */
		alpha = alpha | (alpha << 4);
		for (y_attr = y; y_attr < y+height; ++y_attr) 
		{
			memset(gfx_attributeBuffer.screen[gfx_attributeBuffer.index] + (y_attr * gfx_attributeBuffer.width + x / 2), 
			       alpha, 
			       width / 2 );
		}
	}
}
#endif // STBTI

/* Draw rectangle onto the surface */
void gfx_drawRectangle (IDirectFBSurface *pSurface, int r, int g, int b, int a, int x, int y, int width, int height)
{
#ifdef	STBTI
	/* Prepare the opacity layer */
	gfx_setAttrRectangle ((a >> 5) & 0x07, x, y, width, height);
#endif
#ifdef STSDK
	/* Drawing on ARGB is very slow on ST, so we use accelerated mode, ignoring source alpha */
	pSurface->SetPorterDuff (pSurface, DSPD_SRC_OVER);
#endif

	DFBCHECKLABEL (pSurface->SetColor(pSurface, r, g, b, a), finish_draw_rectangle);
	DFBCHECKLABEL (pSurface->FillRectangle(pSurface, x, y, width, height), finish_draw_rectangle);

#ifdef STSDK
	pSurface->SetPorterDuff (pSurface, DSPD_NONE);
#endif
	finish_draw_rectangle:;
}

/* Clear the screen */
void gfx_clearSurface (IDirectFBSurface *pSurface, int width, int height)
{
	/* Clear the screen with a transparent rectangle */
	gfx_drawRectangle(pSurface, 0x0, 0x0, 0x0, 0x0, 0, 0, width, height);
}

/* Draw the specified text to the specified surface */
void gfx_drawText(IDirectFBSurface *pSurface, IDirectFBFont *pgfx_Font,
				  int r, int g, int b , int a, int x, int y,
				  const char *pText, int drawBox, int shadow)
{
	/* Load a font onto the surface */
	DFBCHECKLABEL (pSurface->SetFont(pSurface, pgfx_Font), finish_drawText);
	if (drawBox)
	{
		DFBRectangle rectangle;
		DFBCHECKLABEL (pgfx_Font->GetStringExtents(pgfx_Font, pText, -1, &rectangle, NULL), finish_drawText);
		DFBCHECKLABEL (pSurface->SetDrawingFlags(pSurface, DSDRAW_BLEND), finish_drawText);
		gfx_drawRectangle (pSurface, 255, 255, 255, 180, x+rectangle.x, y+rectangle.y, rectangle.w, rectangle.h);
		DFBCHECKLABEL (pSurface->SetDrawingFlags(pSurface, DSDRAW_NOFX), finish_drawText );
	}

	if (shadow)
	{
		/* Set up colour and alpha - draw the drop shadow */
		DFBCHECKLABEL (pSurface->SetDrawingFlags(pSurface, DSDRAW_NOFX), finish_drawText );
		DFBCHECKLABEL (pSurface->SetColor(pSurface, r/4, g/4, b/4, a), finish_drawText );
		DFBCHECKLABEL (pSurface->DrawString(pSurface, pText, strlen(pText), x+1, y+1, DSTF_LEFT), finish_drawText );
		DFBCHECKLABEL (pSurface->DrawString(pSurface, pText, strlen(pText), x+2, y+2, DSTF_LEFT), finish_drawText );
	}

	/* Set up colour and alpha - draw the text */
	DFBCHECKLABEL (pSurface->SetDrawingFlags(pSurface, DSDRAW_BLEND), finish_drawText );
	DFBCHECKLABEL (pSurface->SetColor(pSurface, r, g, b, a), finish_drawText );
	DFBCHECKLABEL (pSurface->DrawString(pSurface, pText, strlen(pText), x, y, DSTF_LEFT), finish_drawText );

	DFBCHECKLABEL (pSurface->SetDrawingFlags(pSurface, DSDRAW_NOFX), finish_drawText );
	finish_drawText:;
}

void gfx_DoubleSize (IDirectFBSurface *pSurface, 
                     int i, int column, 
                     int flagDH, int flagDW, int flagDS, 
                     int symbolWidth, int symbolHeight, 
                     int horIndent, int verIndent)
{
	int k, j, beginH, endH, beginW, endW, step, length, stride;
	static char *src;

	beginH = (i - 1) * symbolHeight + verIndent;
	endH   = i * symbolHeight + verIndent - 1;

	beginW = (column * symbolWidth + horIndent) * 4;
	endW   = (column * symbolWidth + horIndent) * 4 + (symbolWidth - 1) * 4;

	if (flagDH)
		length = symbolWidth;
	else
		length = 2 * symbolWidth;
    
	DFBCHECKLABEL (pSurface->Lock (pSurface, DSLF_WRITE, (void**)&src,&stride), finish_DoubleSize); 

	if ((flagDW) || (flagDS))
	{
		for (k = beginH; k <= endH; k++)
		{
			step = symbolWidth * 4;
            for (j = endW; j > beginW; j = j - 4)
			{
				memcpy (&src[k*interfaceInfo.screenWidth*4 + j + step], &src[k*interfaceInfo.screenWidth*4 + j], 4);
				step = step - 4;
				memcpy (&src[k*interfaceInfo.screenWidth*4 + j + step], &src[k*interfaceInfo.screenWidth*4 + j], 4);
			}
			memcpy (&src[k*interfaceInfo.screenWidth*4 + j + 4], &src[k*interfaceInfo.screenWidth*4 + j], 4);
		}
	}

    if ((flagDH) || (flagDS))
	{
		step = symbolHeight;

		for (k = endH; k > beginH; k--)
		{
			memcpy (&src[(k+step)*interfaceInfo.screenWidth*4 + beginW], &src[k*interfaceInfo.screenWidth*4 + beginW], length*4);
			step --;
			memcpy (&src[(k+step)*interfaceInfo.screenWidth*4 + beginW], &src[k*interfaceInfo.screenWidth*4 + beginW], length*4);
		}
		memcpy (&src[(beginH+1)*interfaceInfo.screenWidth*4] + beginW, &src[beginH*interfaceInfo.screenWidth*4 + beginW], length*4);
	}

	DFBCHECKLABEL (pSurface->Unlock(pSurface), finish_DoubleSize); 

	finish_DoubleSize:;
}

#if (defined STB225)
void gfx_fb1_clear char r, char g, char b, char a)
{
	IDirectFBSurface * pgfx_frameBuffer_fb1 = gfx_getSurface (gfx_getImageLayer());
	pgfx_frameBuffer_fb1->Clear (pgfx_frameBuffer_fb1, r, g, b, a);
	//printf("%s[%d] rgba = 0x%02x 0x%02x 0x%02x 0x%02x \n", __FILE__, __LINE__, r, g, b, a);
}

void gfx_fb1_draw_rect(int x, int y, int w, int h, unsigned char depth)
{
	IDirectFBSurface * pgfx_frameBuffer_fb1 = gfx_getSurface (gfx_getImageLayer());
	pgfx_frameBuffer_fb1->SetColor (pgfx_frameBuffer_fb1, depth, depth, depth, 0xff);
	pgfx_frameBuffer_fb1->FillRectangle (pgfx_frameBuffer_fb1, x, y, w, h);
}

static void gfx_fb1_flip()
{
	IDirectFBSurface * pgfx_frameBuffer_fb1 = gfx_getSurface (gfx_getImageLayer());
	pgfx_frameBuffer_fb1->Flip (pgfx_frameBuffer_fb1, NULL, DSFLIP_NONE);

}
#endif // STB225

#ifdef ENABLE_3D
//Kpy 16  it is 2 bytes for each bit of a header 
//#define REAL_3DHEADER_LEN 32*16
//Kpy let it be the whole line length
#define REAL_3DHEADER_LEN 1920
#define DIRTY_3DHEADER_LEN 1*16

static unsigned char real3Dheader[REAL_3DHEADER_LEN*4];		// * 4 = RGBA
static unsigned char dirty3Dheader[DIRTY_3DHEADER_LEN*4];

unsigned long CalcCRC32 (unsigned char *p, unsigned long len);

static unsigned char * fill3Dheader (unsigned char *header, const unsigned char *format, unsigned int format_len)
{
	unsigned int i;
	int bit;
	unsigned char * rawData = header;

	// header 10 and 22 bytes length
	unsigned int end_byte = 10;

	if (format_len != 10) end_byte = 0;

	for (i = 0; i < format_len; i++)
	{
		unsigned char ch = format[i];
		for (bit = 7; bit >= 0; bit--)
		{
			unsigned char mask = 0x1 << bit;
			if ((ch & mask)!=0)
			{
				rawData[0] = 0xff;	//blue
				rawData[1] = 0x00;	//green
				rawData[2] = 0x0;	//red
				rawData[3] = 0xff;	//alpha

				rawData[4] = 0x0;	//blue
				rawData[5] = 0x0;	//green
				rawData[6] = 0x0;	//red
				rawData[7] = 0xff;	//alpha

				if (i < end_byte) 
				{
					rawData[0] = 0x90;	//blue
					rawData[1] = 0x90;	//green
					rawData[2] = 0x90;	//red
				}
  			}
			else
			{
				rawData[0] = 0x0;//blue
				rawData[1] = 0x0;//green
				rawData[2] = 0x0;//red
				rawData[3] = 0xff;//alpha
				rawData[4] = 0x0;//blue
				rawData[5] = 0x0;//green
				rawData[6] = 0x0;//red
				rawData[7] = 0xff;//alpha
			}
			rawData+=8;
		}
	}
	return rawData;
}

#if 0
static unsigned char *fill3Dheader_Old (unsigned char *header, const unsigned char *format, unsigned int format_len)
{
	unsigned int i;
	int bit;
	unsigned char *rawData = header;
#define ALPHAFORHEADER 0xff

	for(i = 0; i < format_len; i++)
	{
		unsigned char ch = format[i];
		for (bit = 7; bit >= 0; bit--)
		{
			unsigned char mask = 0x1 << bit;
			if ((ch & mask)!=0)
			{
				rawData[0] = 0x90;//blue
				rawData[1] = 0x90;//green
				rawData[2] = 0x90;//red
				rawData[3] = ALPHAFORHEADER;//alpha
				rawData[4] = 0x90;//blue
				rawData[5] = 0x90;//green
				rawData[6] = 0x90;//red
				rawData[7] = ALPHAFORHEADER;//alpha
			}
			else
			{
				rawData[0] = 0x70;//blue
				rawData[1] = 0x70;//green
				rawData[2] = 0x70;//red
				rawData[3] = ALPHAFORHEADER;//alpha
				rawData[4] = 0x70;//blue
				rawData[5] = 0x70;//green
				rawData[6] = 0x70;//red
				rawData[7] = ALPHAFORHEADER;//alpha
			}
			rawData+=8;
		}
	}
	
	return rawData;
}
#endif

static void init_3Dheader()
{
#define HEADERCONTENT_LEN	 	10
#define HEADERFORMAT_LEN 		22

	// v3d - video content
	static const unsigned char Header_VideoContent[HEADERCONTENT_LEN]
			= {0xF1, 0x02, 0x40, 0x80, 0x00, 0x00, 0x1F, 0x3A, 0x7B, 0x38};

	static const unsigned char Header_Signage[HEADERCONTENT_LEN]
			= {0xF1, 0x01, 0x40, 0x80, 0x00, 0x00, 0xC4, 0x2D, 0xD3, 0xAF};

	static const unsigned char Header_StillContent[HEADERCONTENT_LEN]
			= {0xF1, 0x05, 0x40, 0x80, 0x00, 0x00, 0xE4, 0xD9, 0x50, 0x2C};

//Kpy // debug
//	static const unsigned char Header_PhilipsHeader[HEADERCONTENT_LEN]
//			= {0xF1, 0x01, 0x00, 0x00, 0x00, 0x00, 0xf9, 0x68, 0x00, 0x72};


	// 2D+Z format (odd rows are not used)
	static const unsigned char Header_2DZ[HEADERFORMAT_LEN]
			= {0xF2, 0x14,  0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x36, 0x95, 0x82, 0x21};

	// Declipse - removed redundant data
	static const unsigned char Header_DeclRD[HEADERFORMAT_LEN]
			= {0xF2, 0x14,  0x9A, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x6B, 0xF6, 0xC6, 0x89};

	// Declipse - full background data
	static const unsigned char Header_DeclFull[HEADERFORMAT_LEN]
			= {0xF2, 0x14,  0xEF, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0x00,
			   0x2F, 0xF0, 0xC4, 0x5F};


	unsigned char Header_Content[HEADERCONTENT_LEN];
	unsigned char Header_Format [HEADERFORMAT_LEN];

	const unsigned char *pContent[3] = {&Header_VideoContent[0], &Header_Signage[0], &Header_StillContent[0]};
	const unsigned char *pFormat[3] = {&Header_2DZ[0], &Header_DeclRD[0], &Header_DeclFull[0]};

	const unsigned char Header_Dirty[1] = {0x0e};

	unsigned char *header = real3Dheader;

	memset(real3Dheader, 0, REAL_3DHEADER_LEN * 4);
	memcpy(Header_Content,pContent[appControlInfo.outputInfo.content3d],HEADERCONTENT_LEN);
	memcpy(Header_Format,pFormat[appControlInfo.outputInfo.format3d],HEADERFORMAT_LEN);

	if (appControlInfo.outputInfo.use_factor || appControlInfo.outputInfo.use_offset) 
	{
		unsigned int uiCRC, i;
		
		Header_Content[2] = appControlInfo.outputInfo.factor;
		Header_Content[3] = appControlInfo.outputInfo.offset;
		Header_Content[4] &= 0x3f;
		Header_Content[4] |= (appControlInfo.outputInfo.use_factor << 7) | (appControlInfo.outputInfo.use_offset << 6);

		uiCRC = CalcCRC32 (Header_Content, HEADERCONTENT_LEN - 4);
		for (i = 6; i < 10; i++) 
		{
			Header_Content[i] = uiCRC >> 24;
			uiCRC <<= 8;
		}
	}
	header = fill3Dheader (header, Header_Content, HEADERCONTENT_LEN);

//Kpy // debug
//	header = fill3Dheader (header, Header_PhilipsHeader, HEADERCONTENT_LEN);

	header = fill3Dheader (header, Header_Format, HEADERFORMAT_LEN);
	
	fill3Dheader (dirty3Dheader, Header_Dirty, 1);
}

void gfx_draw_3Dheader (IDirectFBSurface *pSurface)
{
	DFBRectangle rcHeader;
	rcHeader.x = 0;
	rcHeader.y = 0;
	rcHeader.w = 1920;
	rcHeader.h = 1;

	if (interfaceInfo.enable3d==0 || pSurface==NULL) return;

	init_3Dheader();
#ifdef STSDK
       /* Drawing on ARGB is very slow on ST, so we use accelerated mode, ignoring source alpha */
       pSurface->SetPorterDuff (pSurface, DSPD_SRC_OVER);
#endif
	if(interfaceInfo.enable3d)
	{
		pSurface->Write (pSurface, &rcHeader, real3Dheader, sizeof(real3Dheader));
	} 
	else 
	{
		rcHeader.w = sizeof(dirty3Dheader) >> 2;
		pSurface->Write (pSurface, &rcHeader, dirty3Dheader, sizeof(dirty3Dheader));
	}
#ifdef STSDK
	pSurface->SetPorterDuff (pSurface, DSPD_NONE);
#endif
	return;
	printf("Error occured in %s\n", __FUNCTION__);
}
#endif // ENABLE_3D

void gfx_flipSurface (IDirectFBSurface *pSurface)
{
#ifdef ENABLE_3D

	if (interfaceInfo.enable3d
#if (defined STSDK)
	&&	!interfaceInfo.showMenu && !interfacePlayControl.visibleFlag &&
		!interfaceSlideshowControl.visibleFlag
#endif
	   )
	{
//Kpy need to check a line on the screen at the bottom
		if (interfaceInfo.mode3D != 0)
		{
			gfx_draw_3Dheader(pSurface);
		}
#if (defined STB225)
		gfx_fb1_flip();
#endif
		if (interfaceInfo.mode3D == 0)
		{
			gfx_draw_3Dheader(pSurface);
		}

	} else {
//		printf("%s[%d] width=%d\n", __FILE__, __LINE__, interfaceInfo.screenWidth);
		gfx_drawRectangle(DRAWING_SURFACE, 0x00, 0x00, 0x00, 0xff, 0, 0, interfaceInfo.screenWidth, 1); /// FIXME
	}
#endif // ENABLE_3D
	
	/* Flip the surface to update the front buffer */
	
#ifndef STSDK
	{
		int res;
		struct timespec tmout;
		tmout.tv_sec = 2;
		tmout.tv_nsec = 0;
		res = pthread_mutex_timedlock(&flipMutex, (const struct timespec*)&tmout);
		if (res){
			eprintf ("%s: - pthread_mutex_timedlock failed. errno = %d\n", __FUNCTION__, errno);
		}
		else {
			DFBCHECKLABEL (pSurface->Flip(pSurface, NULL, 0), finish_flip );
			pthread_mutex_unlock(&flipMutex);
		}
	}
#else
	// Tripple buffering is not working on ST, so we have to wait for vsync to remove flicker
	DFBCHECKLABEL (pSurface->Flip(pSurface, NULL, DSFLIP_WAITFORSYNC), finish_flip);
#endif

#ifdef STBTI
	/* Flip the attribute layer as well */
	if (pSurface == pgfx_frameBuffer)
	{
		flipAttrBuffers();
	}
#endif

	finish_flip:;
}

void gfx_changeOutputFormat (int format)
{
	appControlInfo.outputInfo.encConfig[0].out_signals = format;
	appControlInfo.outputInfo.encConfig[0].flags = DSECONF_OUT_SIGNALS;

	/*Always turn on both scarts as we turn them off if not needed*/
	if (appControlInfo.outputInfo.encDesc[0].all_connectors & (DSOC_SCART | DSOC_SCART2))
	{
		if (!(appControlInfo.outputInfo.encConfig[0].out_connectors & DSOC_SCART) ||
		    !(appControlInfo.outputInfo.encConfig[0].out_connectors & DSOC_SCART2))
		{
			appControlInfo.outputInfo.encConfig[0].out_connectors |= (DSOC_SCART | DSOC_SCART2);
			appControlInfo.outputInfo.encConfig[0].flags |= DSECONF_CONNECTORS;
		}
	}
	/*Always turn off component connector if selected as we turn it on again if needed*/
	if ((appControlInfo.outputInfo.encConfig[0].out_connectors & DSOC_COMPONENT))
	{
		appControlInfo.outputInfo.encConfig[0].out_connectors &= ~(DSOC_COMPONENT);
	}
	switch (format)
	{
		case(DSOS_YCBCR) :
			if((appControlInfo.outputInfo.encConfig[0].out_connectors & DSOC_SCART) ||
			   (appControlInfo.outputInfo.encConfig[0].out_connectors & DSOC_SCART2))
			{
				appControlInfo.outputInfo.encConfig[0].out_connectors &= ~(DSOC_SCART | DSOC_SCART2);
				appControlInfo.outputInfo.encConfig[0].out_connectors |= DSOC_COMPONENT;
				appControlInfo.outputInfo.encConfig[0].flags |= DSECONF_CONNECTORS;
			}
			break;
		default: break;
	}

	gfx_setOutputFormat(0);
#ifdef STB82
	backend_setup();
#endif
}

void gfx_setOutputFormat (int forceChange)
{
	DFBScreenEncoderConfigFlags encFlags;
	DFBCHECKLABEL (pgfx_screen->SetOutputConfiguration(pgfx_screen, 0, &appControlInfo.outputInfo.config), finish_format);

	if (appControlInfo.outputInfo.encDesc[0].caps & DSECAPS_TV_STANDARDS)
	{
		if (((pgfx_screen->TestEncoderConfiguration(pgfx_screen, 0, &appControlInfo.outputInfo.encConfig[0], &encFlags)) == DFB_UNSUPPORTED) ||
			forceChange)
		{
			/*We are changing format so need to release all video providers, stop all image providers etc
			  then resize all display layers.*/
			if ((encFlags & DSECONF_TV_STANDARD) || forceChange)
			{
				/*Release all surfaces*/
				gfx_formatChange();

#ifdef STBxx
				int maxWidth, maxHeight;

				switch (appControlInfo.outputInfo.encConfig[0].tv_standard)
				{
					case DSETV_NTSC:
#ifdef STB82
					case DSETV_NTSC_M_JPN:
					case DSETV_PAL_60:
					case DSETV_PAL_M:
#endif
						maxWidth = 720;
						maxHeight = 480;
						break;

					case DSETV_PAL:
#ifndef STB6x8x
					case DSETV_PAL_BG:
					case DSETV_PAL_I:
					case DSETV_PAL_N:
					case DSETV_PAL_NC:
#endif
					case DSETV_SECAM:
						/* Changes to PAL or SECAM need to be effected BEFORE the layer rectangles are modified */
						DFBCHECK (pgfx_screen->SetEncoderConfiguration(pgfx_screen, 0, 
						                                        &appControlInfo.outputInfo.encConfig[0]));
						maxWidth = 720;
						maxHeight = 576;
						break;

					default:
						maxWidth = 720;
						maxHeight = 576;
				}

				dprintf("%s: %dx%d\n", __FUNCTION__, maxWidth, maxHeight);
				int i;
				for (i = 0; i < gfx_getNumberLayers(); i++)
				{
					/*We do not ever use layer 1 so make sure we do not try to set the destination rectangle
					  for it*/
					if (i != 1)
					{
						dprintf("%s: setting dest rect layer %d\n", __FUNCTION__, i);
						gfx_setDestinationRectangle(i, 0, 0, maxWidth, maxHeight, 1);
					}
				}
#ifdef STB225
				/*Now we need to restart dimensions thread*/
				if ((dimensionsThread == 0))
				{
					dprintf("%s[%d]\n", __FILE__, __LINE__);
					int err = pthread_create (&dimensionsThread, NULL,
					                      gfx_getDimensionsThread, NULL);
					pthread_detach(dimensionsThread);
				}
#endif
#endif // STBxx
#ifdef STSDK
#warning unimplemented!
				eprintf("%s: unimplemented\n", __FUNCTION__);
#endif
			}
		}
		DFBCHECK (pgfx_screen->SetEncoderConfiguration(pgfx_screen, 0, &appControlInfo.outputInfo.encConfig[0]));
		if (appControlInfo.outputInfo.numberOfEncoders == 2)
		{
			DFBCHECK (pgfx_screen->SetEncoderConfiguration(pgfx_screen, 1, &appControlInfo.outputInfo.encConfig[1]));
		}
	}

	finish_format:;
}

int gfx_isHDoutput()
{
#ifdef STBxx
	if (appControlInfo.outputInfo.encDesc[0].tv_standards & (DSETV_DIGITAL))
	{
		if (appControlInfo.outputInfo.encConfig[0].scanmode == DSESM_PROGRESSIVE)
		{
			dprintf("%s: progressive\n", __FUNCTION__);
			return 2;
		} 
		else
		{
			dprintf("%s: interlace\n", __FUNCTION__);
			return 1;
		}
	}
#endif
	return 0;
}

void gfx_init (int argc, char* argv[])
{
	int i;
	char buf[PATH_MAX];

	/* The description of the frame buffer surface to create. */
	DFBSurfaceDescription surfaceDesc;

	/* The description of the font to create. */
	DFBFontDescription fontDesc;

	/* The description of the screen. */
	DFBScreenDescription screenDesc;

	(void)memset(&gfx_videoProvider, 0, sizeof(gfx_videoProviderInfo));
#ifdef STSDK
	gfx_videoProvider.waiting = -1;
#endif
	/* Initialise the free image list */
	for (i = 0; i < GFX_IMAGE_TABLE_SIZE; i++)
	{
		gfx_addImageToList(&gfx_imageTable[i], &pgfx_FreeList);
	}
	
	dprintf("%s[%d]: DirectFBInit\n", __FILE__, __LINE__);
	/* Initialise DirectFB, passing command line options.
	 * Options recognised by DirectFB will be stripped. */
	DFBCHECK (DirectFBInit(&argc, &argv));

	dprintf("%s[%d]: DirectFBSetOption\n", __FILE__, __LINE__);
	/*Set the background colour to be black Otherwise it will be a nice shade of blue*/
	DFBCHECK (DirectFBSetOption("bg-color", "00000000"));

	if (appControlInfo.outputInfo.graphicsMode[0])
		DFBCHECK(DirectFBSetOption("mode", appControlInfo.outputInfo.graphicsMode));

	dprintf("%s[%d]: DirectFBCreate\n", __FILE__, __LINE__);
	/* Create the DirectFB root interface. */
	DFBCHECK (DirectFBCreate(&pgfx_dfb));

	dprintf("%s[%d]: SetCooperativeLevel\n", __FILE__, __LINE__);
	/* Use full screen mode so that a surface has full control of a layer
	 * and no windows are created. */
	DFBCHECK (pgfx_dfb->SetCooperativeLevel(pgfx_dfb, DFSCL_FULLSCREEN));

	dprintf("%s[%d]: pgfx_dfb ready\n", __FILE__, __LINE__);
	/* Set the surface description - specify which fields are set and set them. */
#ifndef GFX_USE_HELPER_SURFACE
	surfaceDesc.flags = DSDESC_CAPS;
#else
	surfaceDesc.flags = DSDESC_CAPS | DSDESC_PIXELFORMAT;
	//surfaceDesc.pixelformat = DSPF_ARGB1555; //DSPF_RGB16;
	surfaceDesc.pixelformat = DSPF_ARGB;
#endif
	surfaceDesc.caps = DSCAPS_PRIMARY | DSCAPS_STATIC_ALLOC | DSCAPS_DOUBLE | DSCAPS_FLIPPING;

	dprintf("%s[%d]: CreateSurface\n", __FILE__, __LINE__);
	/* Create the frame buffer primary surface by passing our surface description. */
	DFBCHECK (pgfx_dfb->CreateSurface (pgfx_dfb, &surfaceDesc, &pgfx_frameBuffer) );

	surfaceDesc.flags = DSDESC_WIDTH | DSDESC_HEIGHT;
	pgfx_frameBuffer->GetSize (pgfx_frameBuffer, &surfaceDesc.width, &surfaceDesc.height);

	eprintf("gfx: Primary surface size: %dx%d\n", surfaceDesc.width, surfaceDesc.height);

#ifdef GFX_USE_HELPER_SURFACE
	DFBCHECK (pgfx_dfb->CreateSurface (pgfx_dfb, &surfaceDesc, &pgfx_helperFrameBuffer));
	dprintf("gfx: new surface size: %dx%d\n", surfaceDesc.width, surfaceDesc.height);
	int w, h;
	pgfx_helperFrameBuffer->GetSize (pgfx_helperFrameBuffer, &w, &h);
	dprintf("gfx: Helper surface size: %dx%d\n", w, h);
#endif

	dprintf("%s[%d]: surface ready\n", __FILE__, __LINE__);
	/* Create the font interface for the built in fixed font. */
	fontDesc.flags = DFDESC_HEIGHT;

	switch (surfaceDesc.width) {
		case  720: globalFontHeight = 18; break;
		case 1280: globalFontHeight = 22; break;
		default:   globalFontHeight = 24; break;
	}
	globalSmallFontHeight = globalFontHeight - 2;

	fontDesc.height = globalFontHeight;
	//DFBCHECK( pgfx_dfb->CreateFont(pgfx_dfb, NULL, &fontDesc, &pgfx_font) );
	if (strcmp(_T("FONT"), "FONT") != 0)
	{
		strcpy(buf, globalFontDir);
		strcat(buf, _T("FONT"));
		if (helperFileExists(buf))
		{
			globalFont = _T("FONT");
		}
	}
	strcpy(buf, globalFontDir);
	strcat(buf, globalFont);

	eprintf ("%s: Selected font %s (size %d and %d)\n", __FUNCTION__, buf, globalFontHeight, globalSmallFontHeight);
	
	DFBCHECK (pgfx_dfb->CreateFont(pgfx_dfb, buf, &fontDesc, &pgfx_font));

	fontDesc.height = globalSmallFontHeight;
	if (strcmp(_T("FONT_SMALL"), "FONT_SMALL") != 0)
	{
		strcpy(buf, globalFontDir);
		strcat(buf, _T("FONT_SMALL"));
		if (helperFileExists(buf))
		{
			globalSmallFont = _T("FONT_SMALL");
		}
	}
	strcpy(buf, globalFontDir);
	strcat(buf, globalSmallFont);
	DFBCHECK (pgfx_dfb->CreateFont(pgfx_dfb, buf, &fontDesc, &pgfx_smallfont));

	/*Need to get the screen and then get the output configuration*/
	DFBCHECK (pgfx_dfb->GetScreen(pgfx_dfb, 0, &pgfx_screen));
	DFBCHECK (pgfx_screen->EnumDisplayLayers( pgfx_screen, gfx_display_layer_callback, NULL ));
	DFBCHECK (pgfx_screen->GetDescription(pgfx_screen, &screenDesc));
	appControlInfo.outputInfo.numberOfEncoders = screenDesc.encoders;

	dprintf("gfx: encoders %d\n", screenDesc.encoders);
#ifdef STB82
	DFBCHECK (pgfx_screen->GetEncoderDescriptions(pgfx_screen, appControlInfo.outputInfo.encDesc));
	for (i=0; i<appControlInfo.outputInfo.numberOfEncoders; i++)
	{
		/*Do this first so that we can detect if we have HD output or not*/
		DFBCHECK (pgfx_screen->GetEncoderConfiguration(pgfx_screen, i, &appControlInfo.outputInfo.encConfig[i]));
	}

	DFBCHECK (pgfx_screen->GetOutputConfiguration(pgfx_screen, 0, &appControlInfo.outputInfo.config));
	DFBCHECK (pgfx_screen->GetOutputDescriptions(pgfx_screen, &appControlInfo.outputInfo.desc));

	/*Setup output defaults*/
	if (gfx_isHDoutput())
	{
		dprintf("%s: gfx_isHDoutput() == TRUE\n", __FUNCTION__);

		appControlInfo.outputInfo.encConfig[0].out_signals = DSOS_YCBCR;
		appControlInfo.outputInfo.encConfig[0].out_connectors = DSOC_COMPONENT;
		appControlInfo.outputInfo.encConfig[0].slow_blanking = DSOSB_OFF;
		appControlInfo.outputInfo.encConfig[0].flags = (DSECONF_SLOW_BLANKING | DSECONF_CONNECTORS | DSECONF_OUT_SIGNALS);

		appControlInfo.outputInfo.encConfig[1].out_signals = DSOS_HDMI;
		appControlInfo.outputInfo.encConfig[1].out_connectors = DSOC_HDMI;
		appControlInfo.outputInfo.encConfig[1].slow_blanking = DSOSB_OFF;
		appControlInfo.outputInfo.encConfig[1].flags = (DSECONF_CONNECTORS | DSECONF_OUT_SIGNALS);

		gfx_layerMapping.fb_layerID = 0;
		gfx_layerMapping.image_layerID = 0;
		gfx_layerMapping.pip_layerID = 4;
		gfx_layerMapping.main_layerID = 4;
		gfx_layerMapping.maxLayers = ggfx_NumLayers;
	} else
	{
		/*if (appControlInfo.outputInfo.encDesc[0].out_signals & DSOS_RGB)
		{
			appControlInfo.outputInfo.encConfig[0].out_signals = DSOS_RGB;
		} else if (appControlInfo.outputInfo.encDesc[0].out_signals & DSOS_YC)
		{
			appControlInfo.outputInfo.encConfig[0].out_signals = DSOS_YC;
		} else
		{
			appControlInfo.outputInfo.encConfig[0].out_signals = DSOS_CVBS;
		}*/

		/* Set CVBS as default output */
		appControlInfo.outputInfo.encConfig[0].out_signals = DSOS_CVBS;

		/* Get startup video format from options if available */
		if (appControlInfo.outputInfo.format != 0xFFFFFFFF && 
		    appControlInfo.outputInfo.encDesc[0].out_signals & 
		    appControlInfo.outputInfo.format)
		{
			appControlInfo.outputInfo.encConfig[0].out_signals = appControlInfo.outputInfo.format;
		}

		if (appControlInfo.outputInfo.encDesc[0].all_connectors & ( DSOC_SCART | DSOC_SCART2))
		{
			appControlInfo.outputInfo.encConfig[0].out_connectors = DSOC_SCART | DSOC_SCART2;
		} else if (appControlInfo.outputInfo.encDesc[0].all_connectors & DSOC_YC)
		{
			appControlInfo.outputInfo.encConfig[0].out_connectors = DSOC_YC;
		} else
		{
			appControlInfo.outputInfo.encConfig[0].out_connectors = DSOC_CVBS;
		}

		if (appControlInfo.outputInfo.encDesc[0].caps & DSOCAPS_SLOW_BLANKING)
		{
			appControlInfo.outputInfo.encConfig[0].slow_blanking = DSOSB_4x3;
		}
		appControlInfo.outputInfo.encConfig[0].flags = (DSECONF_SLOW_BLANKING | DSECONF_CONNECTORS | DSECONF_OUT_SIGNALS);

		/* Store the number of layers*/
		gfx_layerMapping.maxLayers = ggfx_NumLayers;
		/* Map the layer IDS*/
		if (gfx_layerMapping.maxLayers == GFX_MAX_LAYERS_5L)
		{
			gfx_layerMapping.fb_layerID = 0;
			gfx_layerMapping.image_layerID = 2;
			gfx_layerMapping.pip_layerID = 3;
			gfx_layerMapping.main_layerID = 4;
		} else
		{
			gfx_layerMapping.fb_layerID = 0;
			gfx_layerMapping.image_layerID = -1;
			gfx_layerMapping.pip_layerID = -1;
			gfx_layerMapping.main_layerID = 1;
		}
	}
	/* Force format change. When forced, memory gets reallocated I guess.
	 * This helps avoid problems when starting from NTSC/PAL_M and switching to PAL/SECAM
	 * After format change is forced problems with sync arise... Don't force for now. */
	gfx_setOutputFormat(0);
#endif // STB82
#ifdef STB225
	gfx_getDestinationRectangle (&gfxScreenWidth, &gfxScreenHeight);
	gfxUseScaleParams = false;

	(void)event_create (&gfxDimensionsEvent);
	gfx_keepDimensionsThreadAlive = true;

	gfx_layerMapping.fb_layerID = 0;
	gfx_layerMapping.image_layerID = 1;
	gfx_layerMapping.pip_layerID = 2;
	gfx_layerMapping.main_layerID = 3;
	gfx_layerMapping.maxLayers = ggfx_NumLayers;
	
	{
		gfxLayerDisplay_t layers[4] = { gfxLayerDisplayed, gfxLayerDisplayed, gfxLayerDisplayed, gfxLayerDisplayed };

		gfx_setMixerInfo (gfxMixerAnalog, 0xFF000000u, layers);
		gfx_setMixerInfo (gfxMixerHdmi,   0xFF000000u, layers);
	}
#endif
#ifdef STBTI
	gfx_layerMapping.fb_layerID = 0;
	gfx_layerMapping.image_layerID = -1;
	gfx_layerMapping.pip_layerID = -1;
	gfx_layerMapping.main_layerID = -1;

	/* Initializing attribute buffer */
	struct fb_var_screeninfo varInfo;
	struct fb_fix_screeninfo fixInfo;
	int           bufIdx;

	/* Open video attribute device */
	gfx_attributeBuffer.fd = open(ATTR_DEVICE, O_RDWR);

	if (gfx_attributeBuffer.fd == -1) 
	{
		eprintf("gfx: Failed to open fb device %s (%s)\n", ATTR_DEVICE, strerror(errno));
		goto finish_attr_init;
	}

	if (ioctl(gfx_attributeBuffer.fd, FBIOGET_FSCREENINFO, &fixInfo) == -1) 
	{
		eprintf("gfx: Failed FBIOGET_FSCREENINFO on %s (%s)\n", ATTR_DEVICE, strerror(errno));
		goto finish_attr_init;
	}

	if (ioctl(gfx_attributeBuffer.fd, FBIOGET_VSCREENINFO, &varInfo) == -1) 
	{
		eprintf("gfx: Failed FBIOGET_VSCREENINFO on %s (%s)\n", ATTR_DEVICE, strerror(errno));
		goto finish_attr_init;
	}

	varInfo.xres = surfaceDesc.width;
	varInfo.yres = surfaceDesc.height;
	varInfo.bits_per_pixel = ATTR_BPP;

	/* Set video display format */
	if (ioctl(gfx_attributeBuffer.fd, FBIOPUT_VSCREENINFO, &varInfo) == -1) 
	{
		eprintf("gfx: Failed FBIOPUT_VSCREENINFO on %s (%s)\n", ATTR_DEVICE, strerror(errno));
		goto finish_attr_init;
	}

	if (varInfo.xres != surfaceDesc.width ||
	    varInfo.yres != surfaceDesc.height ||
	    varInfo.bits_per_pixel != ATTR_BPP) 
	{
		eprintf("gfx: Failed to get the requested screen size: %dx%d at %d bpp\n",
		        surfaceDesc.width, surfaceDesc.height, ATTR_BPP);
		goto finish_attr_init;
	}

	gfx_attributeBuffer.width = fixInfo.line_length; //varInfo.xres_virtual / 2;

	dprintf("gfx: fixInfo.line_length=%d varInfo.yres=%d varInfo.yres_virtual=%d gfx_attributeBuffer.width=%d\n",
		fixInfo.line_length,varInfo.yres,varInfo.yres_virtual,gfx_attributeBuffer.width);

	/* Map the video buffers to user space */
	gfx_attributeBuffer.screen[0] = (unsigned char *) mmap (NULL,
	                                                   fixInfo.line_length * varInfo.yres_virtual,
	                                                   PROT_READ | PROT_WRITE,
	                                                   MAP_SHARED,
	                                                   gfx_attributeBuffer.fd, 
	                                                   0);

	if (gfx_attributeBuffer.screen[0] == MAP_FAILED) 
	{
		eprintf("gfx: Failed mmap on %s (%s)\n", ATTR_DEVICE, strerror(errno));
		goto finish_attr_init;
	}

	gfx_attributeBuffer.size = fixInfo.line_length * surfaceDesc.height;

	for (bufIdx = 0; bufIdx < NUM_ATTR_BUFFERS-1; bufIdx++) 
	{
		gfx_attributeBuffer.screen[bufIdx+1] = gfx_attributeBuffer.screen[bufIdx] + gfx_attributeBuffer.size;
	}

	dprintf("gfx: mmap successfull. %d buffers, gfx_attributeBuffer.size=%d \n", NUM_ATTR_BUFFERS , gfx_attributeBuffer.size);

finish_attr_init:;
#endif // STBTI
#ifdef STSDK
	gfx_layerMapping.fb_layerID = 0;
	gfx_layerMapping.image_layerID = -1;
	gfx_layerMapping.pip_layerID = -1;
	gfx_layerMapping.main_layerID = 0;
	gfx_layerMapping.maxLayers = ggfx_NumLayers;

#endif // STSDK
#ifdef STBxx
	IDirectFBDisplayLayer * pDispLayer;
	pDispLayer = gfx_getLayer(gfx_getMainVideoLayer());
	// Restore saved color settings
	DFBColorAdjustment adj;
	adj.flags = 0;
	
	if (appControlInfo.pictureInfo.saturation >= 0 && appControlInfo.pictureInfo.saturation < 0x10000)
	{
		adj.flags |= DCAF_SATURATION;
		adj.saturation = appControlInfo.pictureInfo.saturation&0xFFFF;
	}
	if (appControlInfo.pictureInfo.brightness >= 0 && appControlInfo.pictureInfo.brightness < 0x10000)
	{
		adj.flags |= DCAF_BRIGHTNESS;
		adj.brightness = appControlInfo.pictureInfo.brightness&0xFFFF;
	}
	if (appControlInfo.pictureInfo.contrast >= 0 && appControlInfo.pictureInfo.contrast < 0x10000)
	{
		adj.flags |= DCAF_CONTRAST;
		adj.contrast = appControlInfo.pictureInfo.contrast&0xFFFF;
	}

	pDispLayer->SetColorAdjustment(pDispLayer, &adj);

	/* Hide pip Layer */
	if (gfx_layerMapping.pip_layerID >= 0)
    {
		pDispLayer = gfx_getLayer(gfx_layerMapping.pip_layerID);
		DFBCHECK( pDispLayer->SetOpacity(pDispLayer, 0));
	}
#endif // STBxx
	mysem_create(&gfx_semaphore);
	pthread_mutex_init(&flipMutex, NULL);

	dprintf("%s[%d]: gfx_clearSurface\n", __FILE__, __LINE__);
	gfx_clearSurface(pgfx_frameBuffer, 720, 576);
	dprintf("%s[%d]: gfx_flipSurface\n", __FILE__, __LINE__);
	gfx_flipSurface(pgfx_frameBuffer);
	dprintf("%s[%d]: surface ready\n", __FILE__, __LINE__);

#ifdef STB82
	int st;
	dprintf("%s[%d]: create dimensions thread\n", __FILE__, __LINE__);
	st = pthread_create (&dimensionsThread, NULL, gfx_getDimensionsThread, NULL);
	pthread_detach(dimensionsThread);
#endif

	dprintf("%s[%d]: %s done\n", __FILE__, __LINE__, __FUNCTION__);
}

void gfx_clearImageList()
{
	stb810_gfxImageEntry* pEntry;
	pEntry = pgfx_ImageList;
	while (pEntry)
	{
		stb810_gfxImageEntry* pNext;
		gfx_freeImageEntry(pEntry);
		pNext = pEntry->pNext;
		gfx_removeImageFromList(pEntry, &pgfx_ImageList);
		gfx_addImageToList(pEntry, &pgfx_FreeList);
		pEntry = pNext;
	}
	return;
}

void gfx_clearImageListExcept(char ** names, int count)
{
	stb810_gfxImageEntry* pEntry;
	unsigned char skip;
	if (!count || !names) return;
	pEntry = pgfx_ImageList;
	while (pEntry)
	{
		stb810_gfxImageEntry* pNext;
		int i;
		skip = 0;
		for (i=0; i<count; i++){
			if (!names[i]) continue;
			if (!strcmp(pEntry->filename, names[i])){
				pNext = pEntry->pNext;
				pEntry = pNext;
				skip = 1;
				break;
			}
		}
		if (!skip) {
			gfx_freeImageEntry(pEntry);
			pNext = pEntry->pNext;
			gfx_removeImageFromList(pEntry, &pgfx_ImageList);
			gfx_addImageToList(pEntry, &pgfx_FreeList);
			pEntry = pNext;
		}
	}
	return;
}

void gfx_terminate(void)
{
	int i;

	gfx_formatChange();

	/* Deal with the font - if it was created */
	if (pgfx_font)
	{
		dprintf("gfx: Releasing font ...\n");
		pgfx_font->Release(pgfx_font);
		pgfx_font = NULL;
	}
	if (pgfx_smallfont)
	{
		dprintf("gfx: Releasing small font ...\n");
		pgfx_smallfont->Release(pgfx_smallfont);
		pgfx_smallfont = NULL;
	}

	if (gfx_layerMapping.pip_layerID >= 0)
	{
		IDirectFBDisplayLayer 	*pDispLayer;
		pDispLayer = gfx_getLayer(gfx_layerMapping.pip_layerID);
		DFBCHECK (pDispLayer->SetOpacity(pDispLayer, 0));
	}

	for (i = 0; i < gfx_getNumberLayers(); i++)
	{
		if (pgfx_videoLayer[i])
		{
			dprintf("gfx: Releasing layer %d...\n", i);
			pgfx_videoLayer[i]->Release(pgfx_videoLayer[i]);
			pgfx_videoLayer[i] = NULL;
		}
	}

	if (pgfx_frameBuffer)
	{
		dprintf("gfx: Releasing frame buffer...\n");
		pgfx_frameBuffer->Release(pgfx_frameBuffer);
		pgfx_frameBuffer = NULL;
	}

#ifdef GFX_USE_HELPER_SURFACE
	if (pgfx_helperFrameBuffer)
	{
		dprintf("gfx: Releasing frame buffer...\n");
		pgfx_helperFrameBuffer->Release(pgfx_helperFrameBuffer);
		pgfx_helperFrameBuffer = NULL;
	}
#endif

#ifdef STBTI
	munmap (gfx_attributeBuffer.screen[0], gfx_attributeBuffer.size);
	if (gfx_attributeBuffer.fd > 0)
	{
		close(gfx_attributeBuffer.fd);
		gfx_attributeBuffer.fd = 0;
	}
#endif

	gfx_clearImageList();

	/* Release the super interface. */
	dprintf("gfx: Releasing DirectFB Interface...\n");
	pgfx_dfb->Release( pgfx_dfb );
	pgfx_dfb = NULL;

	mysem_destroy(gfx_semaphore);
	pthread_mutex_destroy(&flipMutex);
}

/*This function is used to release all surfaces etc for doing a format change*/
static void gfx_formatChange ()
{
	int i;

	/**
	 * Signal that we want to kill the dimensions thread.
	 */
	if (dimensionsThread)
	{
		pthread_cancel (dimensionsThread);
		/*Now make sure thread has exited*/
		while (dimensionsThread)
		{
			usleep(10000);
		}
	}

	/*Make sure we stop media as well*/
	if (appControlInfo.mediaInfo.active)
	{
		media_stopPlayback();
	}
	/* Stop any exisiting rtp video display */
	if (appControlInfo.rtpInfo.active)
	{
		rtp_stopVideo(screenMain);
	}
	/* Stop any exisiting rtp video display */
	if (appControlInfo.rtspInfo.active)
	{
		rtsp_stopVideo(screenMain);
	}
#ifdef ENABLE_DVB
	/*Stop off_air playback*/
	if (appControlInfo.dvbInfo.active)
	{
		/* Stop any DVB playback on Main*/
		offair_stopVideo(screenMain, 1);
	}
#endif
	/*Now shutdown all DirectFB stuff*/
	{
#ifdef STBxx
		/* Deal with video providers */
		if (gfx_videoProvider.instance)
		{
			dprintf("gfx: Stopping/Releasing video provider %d...\n", i);
			if (gfx_videoProvider.active)
			{
#ifdef STBPNX
				IDirectFBDisplayLayer* pDispLayer;
#endif
				gfx_videoProvider.instance->Stop (gfx_videoProvider.instance);

#ifdef STBPNX
				/* Hide the video layer */
				pDispLayer = gfx_getLayer (gfx_getMainVideoLayer());
				DFBCHECK( pDispLayer->SetOpacity(pDispLayer, 0));
#endif
				gfx_videoProvider.active = 0;
			}
			gfx_videoProvider.instance->Release (gfx_videoProvider.instance);
			gfx_videoProvider.name[0] = 0;
			gfx_videoProvider.instance = NULL;
			gfx_videoProvider.paused = 0;
		}
#endif // STBxx
	}
	for (i = 0; i < screenOutputs; i++)
	{
		/* Deal with surfaces */
		if (pgfx_videoSurface[i])
		{
			dprintf("gfx: Releasing surface %d...\n", i);
			pgfx_videoSurface[i]->Release(pgfx_videoSurface[i]);
			pgfx_videoSurface[i] = NULL;
		}
	}
}

DFBEnumerationResult
gfx_display_layer_callback (DFBDisplayLayerID id,
                            DFBDisplayLayerDescription  desc,
                            void *arg)
{
	dprintf("%s: Layer[%d] %s\n", __FUNCTION__, id, desc.name);
	ggfx_NumLayers++;
	return DFENUM_OK;
}

int gfx_getVideoProviderAudioCount (int videoLayer)
{
	int audioCount = 0;
#ifdef STBPNX
	DFBEvent provEvent;

	if (!gfx_videoProvider.instance) return 0;

	provEvent.clazz = DFEC_USER;
	provEvent.user.type = userEventGetAudioCount;
	provEvent.user.data = &audioCount;

	if (gfx_videoProvider.instance->SendEvent(gfx_videoProvider.instance, &provEvent) != DFB_OK)
		return -1;
#endif
#ifdef STSDK
	elcdRpcType_t type;
	cJSON        *res = NULL;
	st_rpcSync (elcmd_getaudio, NULL, &type, &res);
	if (type == elcdRpcResult)
		audioCount = objGetInt(res, "total", 0);
	cJSON_Delete(res);
#endif
	return audioCount;
}

int gfx_getVideoProviderAudioStream (int videoLayer)
{
	int audioStream = 0;
#ifdef STBPNX
	DFBEvent provEvent;

	if (!gfx_videoProvider.instance) return 0;

	provEvent.clazz = DFEC_USER;
	provEvent.user.type = userEventGetAudio;
	provEvent.user.data = &audioStream;

	if (gfx_videoProvider.instance->SendEvent(gfx_videoProvider.instance, &provEvent) != DFB_OK)
		return -1;
#endif
#ifdef STSDK
	elcdRpcType_t type;
	cJSON        *res = NULL;
	st_rpcSync (elcmd_getaudio, NULL, &type, &res);
	if (type == elcdRpcResult)
		audioStream = objGetInt(res, "current", 0);
	cJSON_Delete(res);
#endif
	return audioStream;
}

int gfx_setVideoProviderAudioStream (int videoLayer, int audioStream)
{
#ifdef STBPNX
	DFBEvent provEvent;

	if (!gfx_videoProvider.instance) return 0;
	
	provEvent.clazz = DFEC_USER;
	provEvent.user.type = userEventSetAudio;
	provEvent.user.data = &audioStream;

	if (gfx_videoProvider.instance->SendEvent(gfx_videoProvider.instance, &provEvent) != DFB_OK)
		return -1;
#endif
#ifdef STSDK
	elcdRpcType_t type;
	cJSON        *res = NULL;
	cJSON        *params = cJSON_CreateNumber(audioStream);
	if (!params)
		return -1;
	int ret = st_rpcSync (elcmd_setaudio, params, &type, &res);
	cJSON_Delete(params);
	if (ret != 0 || type != elcdRpcResult) {
		eprintf("%s: failed: %s\n", __FUNCTION__, jsonGetString(res, ""));
		cJSON_Delete(res);
		return -1;
	}
	cJSON_Delete(res);
#endif
	return 0;
}

int gfx_setVideoProviderLive(int videoLayer)
{
#ifdef STB82
	DFBEvent provEvent;
	int compatibility = httpServerCompatLiveStreaming;

	if (!gfx_videoProvider.instance) return 0;
	
	provEvent.clazz = DFEC_USER;
	provEvent.user.type = userEventSetCompatibility;
	provEvent.user.data = &compatibility;

	if (gfx_videoProvider.instance->SendEvent(gfx_videoProvider.instance, &provEvent) != DFB_OK)
	{
		return -1;
	}
#endif
	return 0;
}

void gfx_waitForProviders()
{
	mysem_get(gfx_semaphore);
	mysem_release(gfx_semaphore);
}

void gfx_startEventThread(void)
{
	pgfx_dfb->CreateInputEventBuffer( pgfx_dfb, DICAPS_KEYS|DICAPS_BUTTONS|DICAPS_AXES, DFB_TRUE, &appEventBuffer);

	pthread_create(&event_thread, NULL, keyThread, (void*)appEventBuffer);
//	pthread_detach(event_thread);
}

void gfx_stopEventThread(void)
{
	pthread_cancel(event_thread);
	pthread_join(event_thread, NULL);
/*printf("%s[%d]: ***\n", __FILE__, __LINE__);
	if(appEventBuffer) {
		appEventBuffer->Release(appEventBuffer);
		appEventBuffer = NULL;
	}
printf("%s[%d]: ***\n", __FILE__, __LINE__);*/
}
