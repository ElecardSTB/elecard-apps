
/*
 interface.c

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

#include "interface.h"

#include "debug.h"
#include "app_info.h"
#include "sem.h"
#include "gfx.h"
#include "l10n.h"
#include "media.h"
#include "StbMainApp.h"
#include "output.h"
#include "menu_app.h"
#include "off_air.h"
#include "rtp.h"
#include "voip.h"
#include "teletext.h"
#include "pvr.h"
#ifdef STB225
#include "Stb225.h"
#endif
#ifdef WCHAR_SUPPORT
#include "chartables.h"
#include <common.h>
#endif

// Optional features
#ifdef ENABLE_REGPLAT
#include "../third_party/regplat/regplat.h"
#endif
#ifdef ENABLE_VIDIMAX
#include "vidimax.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <directfb.h>
#include <ctype.h>
#include <pthread.h>

// for DirectFB memory access in interface_animateSurface()
#include <core/system.h>
#include <display/idirectfbsurface.h>
#ifdef STB82
#include <core/surfaces.h>
#include <phStbSystemManager.h>
#endif

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define ICONS_STATE_PREV      (0)
#define ICONS_STATE_REW       (1)
#define ICONS_STATE_PLAY      (2)
#define ICONS_STATE_PAUSE     (3)
#define ICONS_STATE_STOP      (4)
#define ICONS_STATE_FF        (5)
#define ICONS_STATE_NEXT      (6)
#define ICONS_STATE_REC       (7)

#ifdef WCHAR_SUPPORT
#  define SYMBOL_TABLE_LENGTH (20)
#  define ALPHABET_LENGTH     (256)
#else
#  define SYMBOL_TABLE_LENGTH (20)
#  define ALPHABET_LENGTH     (60)
#endif

#define INTERFACE_CHANNEL_CONTROL_TIMEOUT (2000)

//#define INTERFACE_DRAW_ARROW

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct
{
	char *name;
	int r,g,b,a;
	interfaceCommand_t command;
} button_t;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static pthread_t interfaceEventThread;

/* display semaphore */
static pmysem_t  interface_semaphore;

/* event semaphore */
static pmysem_t  event_semaphore;

static const int interface_confirmBoxIcons[4] =
{ statusbar_f1_cancel, statusbar_f2_ok, 0, 0 };
static const int interface_textBoxIcons[4] =
{ statusbar_f1_cancel, statusbar_f2_ok, statusbar_f3_keyboard, 0 };

static IDirectFBSurface *interface_splash = NULL;

static button_t controlButtons[] = {
#ifdef WCHAR_SUPPORT
	{ "Lang", INTERFACE_SCROLLBAR_COLOR_RED, INTERFACE_SCROLLBAR_COLOR_GREEN,
	          INTERFACE_SCROLLBAR_COLOR_BLUE, 0xFF, interfaceCommandRefresh },
#endif
	{ "Shift", 0xAA, 0x00, 0x00, 0xFF, interfaceCommandRed },
	{ "<", 0x00, 0xAA, 0x00, 0xFF, interfaceCommandGreen },
	{ ">", 0xAA, 0xAA, 0x00, 0xFF, interfaceCommandYellow },
	{ "Bksp", 0x00, 0x00, 0xAA, 0xFF, interfaceCommandBlue },
	{ "Exit", INTERFACE_SCROLLBAR_COLOR_RED, INTERFACE_SCROLLBAR_COLOR_GREEN,
	          INTERFACE_SCROLLBAR_COLOR_BLUE, 0xFF, interfaceCommandExit }
};

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

#ifdef STB82
static void interface_animateSurface();
#endif

static void interface_animateMenu(int flipFB, int animate);

static int  interface_animationFrameEvent(void *pArg);

static void interface_displayPlayControl();

static inline void interface_displayPlayState();

static void interface_displaySoundControl();

static int interface_refreshClock(void *pArg);

static int interface_channelNumberReset(void *pArg);

static int interface_channelNumberHide (void *pArg);

static void messageBox_setDefaultColors(void);

static int interface_displayPosterBox();

static int interface_hideMessageBoxEvent(void *pArg);

static void interface_enterChannelList();

static void interface_reinitializeListMenu(interfaceMenu_t *pMenu);

static void interface_displaySliderControl();

static void interface_displayStatusbar();

static void interface_displayCall();

#ifdef ENABLE_MESSAGES
static void interface_displayMessageNotify();
#endif

#if 0
static int getLeftStringOverflow(char *string, int maxWidth);
#endif

static int interface_playControlSetVisible(void *pArg);

static int interface_enterTextCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);

static int interface_sliderCallback   (interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);

static int interface_hideSliderControl(void *pArg);

static int interface_editNext(interfaceMenu_t* pMenu);

static int interface_listEntryDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i);

static int interface_editEntryDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i);

static int interface_editDateDisplay (interfaceMenu_t* pMenu, DFBRectangle *rect, int i);

static int interface_editTimeDisplay (interfaceMenu_t* pMenu, DFBRectangle *rect, int i);

/***********************************************
* EXPORTED DATA                                *
************************************************/

interfaceInfo_t interfaceInfo;

interfacePlayControl_t interfacePlayControl;

interfaceSlideshowControl_t interfaceSlideshowControl;

interfaceChannelControl_t interfaceChannelControl;

interfaceSoundControl_t interfaceSoundControl;

interfaceSlider_t interfaceSlider;

const interfaceColor_t interface_colors[] = {
	{ 120, 120, 255, INTERFACE_HIGHLIGHT_RECT_ALPHA }, // blue
	{ 231, 120,  23, INTERFACE_HIGHLIGHT_RECT_ALPHA }, // orange
	// Add your colors here
	{ 0, 0, 0, 0 }
};

#ifdef WCHAR_SUPPORT
wchar_t symbol_table[10][SYMBOL_TABLE_LENGTH] = {
	{L' ', L'+', L'=', L'<', L'>', L'$', L'%', L'&', L'0', 0},
	{L'.', L',', L'?', L'!', L'\'', L'"', L'1', L'-', L'(', L')', L'@', L'/', L':', L'_', 0},
	{L'A', L'B', L'C', L'2', 0},
	{L'D', L'E', L'F', L'3', 0},
	{L'G', L'H', L'I', L'4', 0},
	{L'J', L'K', L'L', L'5', 0},
	{L'M', L'N', L'O', L'6', 0},
	{L'P', L'Q', L'R', L'S', L'7', 0},
	{L'T', L'U', L'V', L'8', 0},
	{L'W', L'X', L'Y', L'Z', L'9', 0},
};

wchar_t alphabet[ALPHABET_LENGTH] = {
	L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9',
	L'A', L'B', L'C', L'D', L'E', L'F', L'G', L'H', L'I', L'J',
	L'K', L'L', L'M', L'N', L'O', L'P', L'Q', L'R', L'S', L'T',
	L'U', L'V', L'W', L'X', L'Y', L'Z',
	L'.', L',', L'?', L'!', L'\'', L'"', L'-', L'(', L')', L'@', L'/', L':', L'_',
	L' ', L'+', L'=', L'<', L'>', L'$', L'%', L'&',
	0
};

wchar_t keypad[KEYPAD_MAX_ROWS][KEYPAD_MAX_CELLS] = {
	{ L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'0', L'-', L'=', 0 },
	{ L'A', L'B', L'C', L'D', L'E', L'F', L'G', L'H', L'I', L'J', L'K', L'L', 0 },
	{ L'M', L'N', L'O', L'P', L'Q', L'R', L'S', L'T', L'U', L'V', L'W', L'X', 0 },
	{ L'Y', L'Z', L'.', L',', L'?', L'!', L'\'', L'"', L'(', L')', L'@', L'/', 0 },
	{ L':', L'_', L'+', L'<', L'>', L'$', L'%', L'&', L'*', L'#', L' ', 0 },
	{ 0 }
};

wchar_t keypad_local[8][16];
#else
char symbol_table[10][SYMBOL_TABLE_LENGTH] = {
	{' ', '+', '=', '<', '>', '$', '%', '&', '0', 0},
	{'.', ',', '?', '!', '\'', '"', '1', '-', '(', ')', '@', '/', ':', '_', 0},
	{'A', 'B', 'C', '2', 0},
	{'D', 'E', 'F', '3', 0},
	{'G', 'H', 'I', '4', 0},
	{'J', 'K', 'L', '5', 0},
	{'M', 'N', 'O', '6', 0},
	{'P', 'Q', 'R', 'S', '7', 0},
	{'T', 'U', 'V', '8', 0},
	{'W', 'X', 'Y', 'Z', '9', 0},
};

char alphabet[ALPHABET_LENGTH] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
	'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z',
	'.', ',', '?', '!', '\'', '"', '-', '(', ')', '@', '/', ':', '_',
	' ', '+', '=', '<', '>', '$', '%', '&',
	0
};

char keypad[KEYPAD_MAX_ROWS][KEYPAD_MAX_CELLS] = {
	{ '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0 },
	{ 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 0 },
	{ 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 0 },
	{ 'Y', 'Z', '.', ',', '?', '!', '\'', '"', '(', ')', '@', '/', 0 },
	{ ':', '_', '+', '<', '>', '$', '%', '&', '*', '#', ' ', 0 },
	{ 0 }
};
#endif

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

void interface_drawInnerBorder(IDirectFBSurface *pSurface,
                               int r, int g, int b, int a,
                               int x, int y, int w, int h,
                               int border, interfaceBorderSide_t sides)
{
	if ( sides & interfaceBorderSideTop )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x+border, y, w-border*2, border);
	}
	if ( sides & interfaceBorderSideBottom )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x, y+h-border, w, border);
	}
	if ( sides & interfaceBorderSideLeft )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x, y, border, h-border);
	}
	if ( sides & interfaceBorderSideRight )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x+w-border, y, border, h-border);
	}
}

void interface_drawOuterBorder(IDirectFBSurface *pSurface,
                               int r, int g, int b, int a,
                               int x, int y, int w, int h,
                               int border, interfaceBorderSide_t sides)
{
	if ( sides & interfaceBorderSideTop )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x-border, y-border, w+border*2, border);
	}
	if ( sides & interfaceBorderSideBottom )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x-border, y+h, w+border*2, border);
	}
	if ( sides & interfaceBorderSideLeft )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x-border, y, border, h);
	}
	if ( sides & interfaceBorderSideRight )
	{
		gfx_drawRectangle(pSurface, r, g, b, a, x+w, y, border, h);
	}
}

void interface_drawBookmark(IDirectFBSurface *pSurface, IDirectFBFont *pFont,
                            int x, int y, const char *pText, int selected, int *endx)
{
	int tx, ty, tw, th;
	int r, g, b, a;
	DFBRectangle rectangle;
	
	DFBCHECK( pFont->GetStringExtents(pFont, pText, -1, &rectangle, NULL) );

	ty = y-rectangle.h-interfaceInfo.paddingSize*2;
	th = rectangle.h+interfaceInfo.paddingSize*2;
	if ( selected >= 0 )
	{
		tx = x+rectangle.x;
		tw = interfaceInfo.paddingSize*2/*+INTERFACE_ARROW_SIZE*/+rectangle.w;
	} else
	{
		tw = interfaceInfo.paddingSize*2+rectangle.w;
		tx = x-tw-rectangle.x;
	}

	DFBCHECK( pSurface->SetDrawingFlags(pSurface, DSDRAW_BLEND) );

	gfx_drawRectangle(pSurface,
	                  INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN,
	                  INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
	                  tx+INTERFACE_ROUND_CORNER_RADIUS, ty,
	                  tw-INTERFACE_ROUND_CORNER_RADIUS*2,INTERFACE_ROUND_CORNER_RADIUS);
	gfx_drawRectangle(pSurface,
	                  INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN,
	                  INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, 
	                  tx, ty+INTERFACE_ROUND_CORNER_RADIUS,
	                  tw, th-INTERFACE_ROUND_CORNER_RADIUS);

	DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE,
	                                    INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN,
	                                    INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
	                   tx, ty, 
	                   INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
	                   0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);

	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
	                   tx+tw-INTERFACE_ROUND_CORNER_RADIUS, ty,
	                   INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
	                   0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);

	//gfx_drawRectangle(pSurface, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, tx+interfaceInfo.borderWidth-INTERFACE_ROUND_CORNER_RADIUS, ty+INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, ty+INTERFACE_ROUND_CORNER_RADIUS - interfaceInfo.clientY);
	
	/*{
		DFBRectangle clip;
		clip.x = tx+interfaceInfo.borderWidth;
		clip.y = ty;
		clip.w = tw-interfaceInfo.borderWidth*2;
		clip.h = th-interfaceInfo.borderWidth;
		DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_THUMBNAIL_BACKGROUND_RED, INTERFACE_THUMBNAIL_BACKGROUND_GREEN, INTERFACE_THUMBNAIL_BACKGROUND_BLUE, INTERFACE_THUMBNAIL_BACKGROUND_ALPHA);
		interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "background.png", tx+interfaceInfo.borderWidth, ty, interfaceInfo.screenWidth, interfaceInfo.screenHeight, 0, &clip, DSBLIT_BLEND_COLORALPHA, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
		//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "background.png", tx+interfaceInfo.borderWidth, ty, interfaceInfo.screenWidth, interfaceInfo.screenHeight, 0, &clip, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
	}*/
	//}

	//interface_drawInnerBorder(pSurface, INTERFACE_BORDER_RED, INTERFACE_BORDER_GREEN, INTERFACE_BORDER_BLUE, INTERFACE_BORDER_ALPHA, tx, ty, tw, th, interfaceInfo.borderWidth, interfaceBorderSideTop|interfaceBorderSideRight|interfaceBorderSideLeft);

	if ( selected == 1 )
	{
		//interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR INTERFACE_ARROW_IMAGE, tx+interfaceInfo.borderWidth+interfaceInfo.paddingSize, ty+th/2, INTERFACE_ARROW_SIZE, INTERFACE_ARROW_SIZE, 0, 0, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignMiddle);
		gfx_drawRectangle(DRAWING_SURFACE,
		                  interface_colors[interfaceInfo.highlightColor].R,
		                  interface_colors[interfaceInfo.highlightColor].G,
		                  interface_colors[interfaceInfo.highlightColor].B,
		                  interface_colors[interfaceInfo.highlightColor].A,
		                  tx+interfaceInfo.paddingSize, ty +interfaceInfo.paddingSize, 
		                  tw - 2*interfaceInfo.paddingSize, th - interfaceInfo.paddingSize);
	}

	if ( endx != NULL )
	{
		*endx = tx+tw;
	}

	//r = selected == 1 ? INTERFACE_BOOKMARK_SELECTED_RED : INTERFACE_BOOKMARK_RED;
	//g = selected == 1 ? INTERFACE_BOOKMARK_SELECTED_GREEN : INTERFACE_BOOKMARK_GREEN;
	//b = selected == 1 ? INTERFACE_BOOKMARK_SELECTED_BLUE : INTERFACE_BOOKMARK_BLUE;
	//a = selected == 1 ? INTERFACE_BOOKMARK_SELECTED_ALPHA : INTERFACE_BOOKMARK_ALPHA;
	r = INTERFACE_BOOKMARK_RED;
	g = INTERFACE_BOOKMARK_GREEN;
	b = INTERFACE_BOOKMARK_BLUE;
	a = INTERFACE_BOOKMARK_ALPHA;
	ty = y-(rectangle.y+rectangle.h)/2-interfaceInfo.paddingSize;
	if ( selected >= 0 )
	{
		tx = x+interfaceInfo.paddingSize;//+INTERFACE_ARROW_SIZE;
	} else
	{
		tx = x-tw+interfaceInfo.paddingSize;
	}
	gfx_drawText(pSurface, pFont, r, g, b, a, tx, ty, pText, 0, 0);

	//dprintf("%s: %d %d %d %d\n", __FUNCTION__, rectangle.x, rectangle.y, rectangle.w, rectangle.h);

	DFBCHECK( pSurface->SetDrawingFlags(pSurface, DSDRAW_NOFX) );
}

#ifdef STB82
static void interface_animateSurface()
{
	static char new_frame[720 * 576 * 4], *src;
	int stride, line_size = 720 * 4; // Bpp
#ifdef STB82
	char* pPhysAddrSurface;
	char* pVirtAddrSurface;
	int frame_size;
#endif
	int y, i;
	IDirectFBSurface_data *dst_data;
	CoreSurface *dst_surface;

	if (interfaceInfo.animation == interfaceAnimationNone ||
	    interfaceInfo.animation >= interfaceAnimationCount)
	{
		interface_flipSurface();
		return;
	}
#ifdef GFX_USE_HELPER_SURFACE
	//if ( DRAWING_SURFACE != pgfx_frameBuffer )
	{ 
		interface_drawImage(pgfx_frameBuffer, IMAGE_DIR INTERFACE_BACKGROUND_IMAGE,
		                    0, 0, interfaceInfo.screenWidth, interfaceInfo.screenHeight,
		                    1, NULL, DSBLIT_NOFX, interfaceAlignTopLeft, NULL, NULL); 
		
		DFBCHECK(pgfx_frameBuffer->SetBlittingFlags(pgfx_frameBuffer, DSBLIT_BLEND_ALPHACHANNEL)); 
		DFBCHECK(pgfx_frameBuffer->Blit(pgfx_frameBuffer, DRAWING_SURFACE, NULL, 0, 0)); 
	}
#endif

	dst_data = (IDirectFBSurface_data*)DRAWING_SURFACE->priv; 
	if (!dst_data) 
	{ 
		eprintf("interface:\n ERROR::dsp_open_image_instance to destination->priv\n\n");
		gfx_flipSurface(pgfx_frameBuffer); 
		return; 
	} 
	
	dst_surface = dst_data->surface; 
	if (!dst_surface) 
	{ 
		eprintf("interface:\n ERROR::dsp_open_image_instance to dst_data->surface\n\n");
		gfx_flipSurface(pgfx_frameBuffer); 
		return; 
	} 

//SergA
//FIXME: 'CoreSurface' has no member named 'front_buffer'
#ifdef STB82
	pVirtAddrSurface = (char*)dfb_system_video_memory_virtual(dst_surface->front_buffer->video.offset); 
	if (!pVirtAddrSurface) 
	{ 
		eprintf("interface:\n ERROR::dsp_open_image_instance to pPhysAddrSurface \n \n \n ");
		gfx_flipSurface(pgfx_frameBuffer); 
		return; 
	} 
	
	DFBCHECK(DRAWING_SURFACE->Lock(DRAWING_SURFACE, DSLF_WRITE, (void**)&src,&stride));
	frame_size = line_size * interfaceInfo.screenHeight; 
	memcpy( new_frame, src, frame_size ); 
	memcpy( src, pVirtAddrSurface, frame_size);

	
	DFBCHECK(DRAWING_SURFACE->Unlock(DRAWING_SURFACE)); 

	/* gfx_flipSurface(DRAWING_SURFACE); 

	pVirtAddrSurface = (char*)dfb_system_video_memory_virtual(dst_surface->front_buffer->video.off); 
	
	DFBCHECK(DRAWING_SURFACE->Lock(DRAWING_SURFACE, DSLF_WRITE, &src,&stride));
	memcpy( src, pVirtAddrSurface, frame_size); 
	DFBCHECK(DRAWING_SURFACE->Unlock(DRAWING_SURFACE)); 
	*/ 

	//get new addr before flip, is second addr 
	pPhysAddrSurface = (char*)dfb_system_video_memory_physical(dst_surface->front_buffer->video.offset);
	gfx_flipSurface(DRAWING_SURFACE);
#endif

	switch (interfaceInfo.animation)
	{
		case interfaceAnimationVerticalCinema:
//SergA
#ifdef STB82
			for ( y = 1; y <= (interfaceInfo.screenHeight/INTERFACE_ANIMATION_STEP); y ++ )
			{
				int is_first = 0;

				if (is_first)
				{
					phStbSystemManager_MoveMem(pPhysAddrSurface,0,INTERFACE_ANIMATION_STEP,
					interfaceInfo.screenHeight,line_size);
				}
				is_first = 1;
			
				phStbSystemManager_MoveMem(pPhysAddrSurface,0,INTERFACE_ANIMATION_STEP,
				interfaceInfo.screenHeight,line_size);
			
				DFBCHECK(DRAWING_SURFACE->Lock(DRAWING_SURFACE, DSLF_WRITE, (void**)&src,&stride));
				memcpy(src,
				&new_frame[frame_size-(INTERFACE_ANIMATION_STEP*line_size)*y],INTERFACE_ANIMATION_STEP*line_size*y);
				DFBCHECK(DRAWING_SURFACE->Unlock(DRAWING_SURFACE));

				//get new addr before flip, is second addr
				pPhysAddrSurface = (char*)dfb_system_video_memory_physical(dst_surface->front_buffer->video.offset);
				gfx_flipSurface(DRAWING_SURFACE);
			}
#endif
			break;
		case interfaceAnimationHorizontalPanorama:
			for ( y = 1; y <= (interfaceInfo.screenHeight/(20*2)); y ++ )
			{
				DFBCHECK(DRAWING_SURFACE->Lock(DRAWING_SURFACE, DSLF_WRITE, (void**)&src,&stride));
				//top...
				memcpy(src,&new_frame[0],line_size*20*y);
				//bottom...
				memcpy(src+(line_size*(interfaceInfo.screenHeight-y*20)),
				       &new_frame[(line_size*(interfaceInfo.screenHeight-y*20))],
				       20*y*line_size);

				DFBCHECK(DRAWING_SURFACE->Unlock(DRAWING_SURFACE));
				gfx_flipSurface(DRAWING_SURFACE);
			}
			break;
		case interfaceAnimationVerticalPanorama:
			for ( y = 1; y <= (interfaceInfo.screenHeight/(20*2)); y ++ )
			{
				DFBCHECK(DRAWING_SURFACE->Lock(DRAWING_SURFACE, DSLF_WRITE, (void**)&src,&stride));
				//pVirtAddrSurface = (char*)dfb_system_video_memory_virtual(dst_surface->front_buffer->video.offset);
				
				//left...
				for (i=0;i<interfaceInfo.screenHeight;i++)
					memcpy(src+(line_size*i),&new_frame[line_size*i],y*20*4);
				//right...
				for (i=0;i<interfaceInfo.screenHeight;i++)
					memcpy(src+(line_size*i)+(line_size)-(y*20*4),&new_frame[line_size*i+line_size-(y*20*4)],y*20*4);
		
				DFBCHECK(DRAWING_SURFACE->Unlock(DRAWING_SURFACE));
				gfx_flipSurface(DRAWING_SURFACE);
				//sleep(1);
			}
			break;
		case interfaceAnimationHorizontalSlide:
			for ( y = 1; y <= (interfaceInfo.screenWidth/(20)); y ++ )
			{
				DFBCHECK(DRAWING_SURFACE->Lock(DRAWING_SURFACE, DSLF_WRITE, (void**)&src,&stride));
				for (i=0;i<interfaceInfo.screenHeight;i++)
					memcpy(src+(line_size*i)+(line_size)-(y*20*4),&new_frame[line_size*i+line_size-(y*20*4)],y*20*4);
				DFBCHECK(DRAWING_SURFACE->Unlock(DRAWING_SURFACE));
				gfx_flipSurface(DRAWING_SURFACE);
			}
			break;
		case interfaceAnimationHorizontalStripes:
			for ( y = 1; y <= (interfaceInfo.screenHeight/(20)); y ++ )
			{
				int offset = 0;
				DFBCHECK(DRAWING_SURFACE->Lock(DRAWING_SURFACE, DSLF_WRITE, (void**)&src,&stride));
				//pVirtAddrSurface = (char*)dfb_system_video_memory_virtual(dst_surface->front_buffer->video.offset);
				//left...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+(line_size*i)+offset,&new_frame[line_size*i+offset],y*20*4);

				offset	=	(interfaceInfo.screenHeight/8)*line_size;
				//right...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+ offset 
					+((line_size)*i)+(line_size)-(y*20*4),
					&new_frame[line_size*i+line_size-(y*20*4) 
					+ offset],y*20*4);

				offset	+=	(interfaceInfo.screenHeight/8)*line_size;
				//left...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+(line_size*i)+offset,&new_frame[line_size*i+offset],y*20*4);

				offset	+=	(interfaceInfo.screenHeight/8)*line_size;
				//right...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+ offset 
					+((line_size)*i)+(line_size)-(y*20*4),
					&new_frame[line_size*i+line_size-(y*20*4) 
					+ offset],y*20*4);
				//left...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+(line_size*i)+offset,&new_frame[line_size*i+offset],y*20*4);

				offset	+=	(interfaceInfo.screenHeight/8)*line_size;
				//right...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+ offset 
					+((line_size)*i)+(line_size)-(y*20*4),
					&new_frame[line_size*i+line_size-(y*20*4) 
					+ offset],y*20*4);

				offset	+=	(interfaceInfo.screenHeight/8)*line_size;
				//left...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+(line_size*i)+offset,&new_frame[line_size*i+offset],y*20*4);

				offset	+=	(interfaceInfo.screenHeight/8)*line_size;
				//right...
				for (i=0;i<interfaceInfo.screenHeight/8;i++)
					memcpy(src+ offset 
					+((line_size)*i)+(line_size)-(y*20*4),
					&new_frame[line_size*i+line_size-(y*20*4) 
					+ offset],y*20*4);

				DFBCHECK(DRAWING_SURFACE->Unlock(DRAWING_SURFACE));
				gfx_flipSurface(DRAWING_SURFACE);
			}
			break;
		default: ;
	}
}
#endif // STB82

void interface_flipSurface()
{
#ifdef GFX_USE_HELPER_SURFACE
	//if ( DRAWING_SURFACE != pgfx_frameBuffer )
	{
		interface_drawImage(pgfx_frameBuffer, IMAGE_DIR INTERFACE_BACKGROUND_IMAGE,
		                    0, 0, interfaceInfo.screenWidth, interfaceInfo.screenHeight,
		                    1, NULL, DSBLIT_NOFX, interfaceAlignTopLeft, NULL, NULL);

		DFBCHECK(pgfx_frameBuffer->SetBlittingFlags(pgfx_frameBuffer, DSBLIT_BLEND_ALPHACHANNEL));
		DFBCHECK(pgfx_frameBuffer->Blit(pgfx_frameBuffer, DRAWING_SURFACE, NULL, 0, 0));
	}
#endif

#ifdef ENABLE_TEST_MODE
	gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 100, 100, 255,
	             interfaceInfo.screenWidth/2-100, interfaceInfo.screenHeight/2-50-20, "TEST TEST TEST TEST", 0, 0);
	gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 100, 100, 255,
	             interfaceInfo.screenWidth/2-100, interfaceInfo.screenHeight/2-20,    "TEST TEST TEST TEST", 0, 0);
	gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 100, 100, 255, 
	             interfaceInfo.screenWidth/2-100, interfaceInfo.screenHeight/2+50-20, "TEST TEST TEST TEST", 0, 0);
	{
		static char diversity[128] = { -1, 0 };
		if (diversity[0] == -1)
			getParam(DIVERSITY_FILE, "DIVERSITY_NAME", "", diversity);
		gfx_drawText(DRAWING_SURFACE, pgfx_font, 100, 255, 100, 255, 
		             interfaceInfo.screenWidth/2-50, interfaceInfo.screenHeight/2+100-20, diversity, 0, 0);
	}
#endif

	gfx_flipSurface(pgfx_frameBuffer);
}

int interface_getTextBoxMaxLineCount()
{
	return (interfaceInfo.clientHeight - interfaceInfo.paddingSize*2) / 18;
}

void interface_displayTextBox( int targetX, int targetY, char *message, 
                               const char *icon, int fixedWidth, 
                               DFBRectangle *resultingBox, int addHeight )
{
	interface_displayCustomTextBoxColor(
	  targetX, targetY, message, icon,
	  fixedWidth, resultingBox, addHeight, NULL,
	  INTERFACE_MESSAGE_BOX_BORDER_RED,  INTERFACE_MESSAGE_BOX_BORDER_GREEN,
	  INTERFACE_MESSAGE_BOX_BORDER_BLUE, INTERFACE_MESSAGE_BOX_BORDER_ALPHA,
	  INTERFACE_BOOKMARK_RED,  INTERFACE_BOOKMARK_GREEN,
	  INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA,
	  INTERFACE_MESSAGE_BOX_RED,  INTERFACE_MESSAGE_BOX_GREEN,
	  INTERFACE_MESSAGE_BOX_BLUE, INTERFACE_MESSAGE_BOX_ALPHA,
	  pgfx_font);
}

void interface_displayCustomTextBox( int targetX, int targetY, char *message,
                                     const char *icon, int fixedWidth,
                                     DFBRectangle *resultingBox, int addHeight, const int *icons )
{
	interface_displayCustomTextBoxColor(
	  targetX, targetY, message, icon,
	  fixedWidth, resultingBox, addHeight, icons,
	  INTERFACE_MESSAGE_BOX_BORDER_RED,  INTERFACE_MESSAGE_BOX_BORDER_GREEN,
	  INTERFACE_MESSAGE_BOX_BORDER_BLUE, INTERFACE_MESSAGE_BOX_BORDER_ALPHA,
	  INTERFACE_BOOKMARK_RED,  INTERFACE_BOOKMARK_GREEN,
	  INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA,
	  INTERFACE_MESSAGE_BOX_RED,  INTERFACE_MESSAGE_BOX_GREEN,
	  INTERFACE_MESSAGE_BOX_BLUE, INTERFACE_MESSAGE_BOX_ALPHA,
	  pgfx_font);
}

void interface_displayTextBoxColor( int targetX, int targetY, char *message, const char *icon, 
                                    int fixedWidth, DFBRectangle *resultingBox, int addHeight,
                                    int br, int bg, int bb, int ba,
                                    int tr, int tg, int tb, int ta)
{
	interface_displayCustomTextBoxColor(
	  targetX, targetY, message, icon,
	  fixedWidth, resultingBox, addHeight, NULL,
	  br, bg, bb, ba, tr, tg, tb, ta,
	  INTERFACE_MESSAGE_BOX_RED, INTERFACE_MESSAGE_BOX_GREEN,
	  INTERFACE_MESSAGE_BOX_BLUE, INTERFACE_MESSAGE_BOX_ALPHA,
	  pgfx_font);
}

void interface_displayCustomTextBoxColor( int targetX, int targetY, char *message, const char *icon,
                                          int fixedWidth, DFBRectangle *resultingBox,
                                          int addHeight, const int *icons,
                                          int br, int bg, int bb, int ba,
                                          int tr, int tg, int tb, int ta,
                                          int r, int g, int b, int a,
                                          IDirectFBFont *pFont)
{
	DFBRectangle rectangle, rect;
	int x,y,w,h,iw,ih,maxWidth,n,i,icons_w = 0;
	size_t len;
	char *ptr, *pos, tmp = 0;
	IDirectFBSurface *pIcon;
	interfaceAlign_t align = interfaceAlignCenter;

	if (fixedWidth < 0)
	{
		fixedWidth *= -1;
		align = interfaceAlignLeft;
	}

	/* fixed width does not include padding - it's size of client area */
	maxWidth = fixedWidth == 0 ?
		interfaceInfo.clientWidth-interfaceInfo.paddingSize*3-interfaceInfo.thumbnailSize :
		fixedWidth;

	memset(&rectangle, 0, sizeof(DFBRectangle));

	pIcon = NULL;
	iw = ih = 0;
	if ( icon != NULL )
	{
		pIcon = gfx_decodeImage(icon, interfaceInfo.thumbnailSize, interfaceInfo.thumbnailSize, 0);
		if ( pIcon != NULL )
		{
			pIcon->GetSize(pIcon, &iw, &ih);
			iw += interfaceInfo.paddingSize*2;
		}
	}

	maxWidth -= iw;

	rectangle.x = rectangle.y = rectangle.w = rectangle.h;

	if (message[0] != 0)
	{
		ptr = message;
		do
		{
			pos = strchr(ptr, '\n');
			//dprintf("%s: ptr %08X, pos %08X, msg: %s\n", __FUNCTION__, ptr, pos, ptr);
			if (pos != NULL)
			{
				tmp = *pos;
				*pos = 0;
			}
			len = getMaxStringLengthForFont(pFont, ptr, maxWidth);
			if (pos != NULL)
			{
				*pos = tmp;
			}
			/* if we have \n symbol in our string... */
			if ( pos != NULL )
			{
				if ((size_t)(pos-ptr) > len)
				{
					pos = &ptr[len];
					while (pos > ptr && *pos != ' ')
					{
						pos--;
					}
					if (pos > ptr)
					{
						len = pos-ptr;
						//pos++;
					} else
					{
						pos = &ptr[len];
					}
				}
				tmp = *pos;
				*pos = 0;
			} else if (strlen(ptr) > len)
			{
				pos = &ptr[len];
				while (pos > ptr && *pos != ' ')
				{
					pos--;
				}
				if (pos > ptr)
				{
					len = pos-ptr;
					//pos++;
				} else
				{
					pos = &ptr[len];
				}
				tmp = *pos;
				*pos = 0;
			}
			DFBCHECK( pFont->GetStringExtents(pFont, ptr, -1, &rect, NULL) );

			rectangle.w = rectangle.w > rect.w ? rectangle.w : rect.w;
			rectangle.h += rect.h;

			if ( pos != NULL )
			{
				*pos = tmp;
				ptr = pos+(tmp=='\n' || tmp==' ' ? 1 : 0);
			} else
			{
				ptr += len;
			}
			//pos = strchr(ptr, '\n');
		} while ( pos != NULL );
	}

	if ( icons != NULL )
	{
		rectangle.h += interfaceInfo.paddingSize + INTERFACE_STATUSBAR_ICON_HEIGHT;
	}

	if (fixedWidth > 0)
	{
		rectangle.w = fixedWidth-iw;
	}

	if ( icons != NULL )
	{
		n = 0;
		for ( i = 0; i < 4; i++)
		{
			if ( icons[i] > 0)
			{
				n++;
			}
		}
		icons_w = n * INTERFACE_STATUSBAR_ICON_WIDTH + (n-1)*3*interfaceInfo.paddingSize;
		if ( icons_w > rectangle.w && fixedWidth == 0 )
		{
			rectangle.w = icons_w;
		}
	}

	//dprintf("%s: load icon\n", __FUNCTION__);

	ih = (ih > rectangle.h ? ih : rectangle.h) + addHeight;
	if (align == interfaceAlignLeft)
	{
		x = targetX;
		y = targetY;
	} else
	{
		x = targetX-(rectangle.w+iw)/2-interfaceInfo.paddingSize;
		y = targetY-(ih)/2-interfaceInfo.paddingSize;
	}
	w = rectangle.w+iw+interfaceInfo.paddingSize*2;
	h = ih+interfaceInfo.paddingSize*2;
	
	//dprintf("%s: draw box\n", __FUNCTION__);
	/* resulting rectangle is client area rather than bounding rectangle */
	if (resultingBox != NULL)
	{
		resultingBox->x = x+interfaceInfo.paddingSize;
		resultingBox->y = y+interfaceInfo.paddingSize;
		resultingBox->w = w-interfaceInfo.paddingSize*2;
		resultingBox->h = h-interfaceInfo.paddingSize*2;
	}

	/*
	gfx_drawRectangle(DRAWING_SURFACE,
	                 INTERFACE_MESSAGE_BOX_RED, INTERFACE_MESSAGE_BOX_GREEN,
	                 INTERFACE_MESSAGE_BOX_BLUE, INTERFACE_MESSAGE_BOX_ALPHA,
	                 x, y, w, h);
	{
		DFBRectangle clip;
		clip.x = x;
		clip.y = y;
		clip.w = w;
		clip.h = h;
		interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "background.png", x, y,
		                    interfaceInfo.screenWidth, interfaceInfo.screenHeight,
		                    0, &clip, DSBLIT_BLEND_ALPHACHANNEL,
		                    interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
	}*/
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

	if ( a > 0)
	{
		gfx_drawRectangle(DRAWING_SURFACE, r, g, b, a, x, y, w, h);
	}
	if (ba > 0)
	{
		interface_drawOuterBorder(DRAWING_SURFACE, br, bg, bb, ba, x, y, w, h, interfaceInfo.borderWidth, interfaceBorderSideAll);
	}

	if ( pIcon != NULL )
	{
		//dprintf("%s: draw icon\n", __FUNCTION__);
		interface_drawImage(DRAWING_SURFACE, icon,
		                    x+interfaceInfo.paddingSize, y+interfaceInfo.paddingSize,
		                    interfaceInfo.thumbnailSize, interfaceInfo.thumbnailSize,
		                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL,
		                    interfaceAlignTopLeft, 0, 0);
	}
	// helper icons
	if (icons != NULL)
	{
		//n = (interfaceInfo.messageBox.type == interfaceMessageBoxCallback && interfaceInfo.messageBox.pCallback == interface_enterTextCallback) ? 3 : 2;
		x = (interfaceInfo.screenWidth - icons_w) / 2;
		y = y + h - interfaceInfo.paddingSize - INTERFACE_STATUSBAR_ICON_HEIGHT;
		for ( i = 0; i < 4; i++)
		{
			if ( icons[i] > 0)
			{
				interface_drawImage(DRAWING_SURFACE, resource_thumbnails[icons[i]],
				                    x, y, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT,
				                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL,
				                    interfaceAlignTopLeft, 0, 0);
				x += INTERFACE_STATUSBAR_ICON_WIDTH + interfaceInfo.paddingSize * 3;
			}
		}
	}

	if (align == interfaceAlignLeft)
	{
		x = targetX+interfaceInfo.paddingSize;
		y = targetY+interfaceInfo.paddingSize;
	} else
	{
		x = targetX-(rectangle.w)/2+iw/2;
		y = targetY-(rectangle.h)/2-addHeight/2;
	}

	//dprintf("%s: draw text\n", __FUNCTION__);

	if (message[0] != 0 && ta > 0)
	{
		ptr = message;
		do
		{
			pos = strchr(ptr, '\n');
			if (pos != NULL)
			{
				tmp = *pos;
				*pos = 0;
			}
			len = getMaxStringLengthForFont(pFont, ptr, maxWidth);
			if (pos != NULL)
			{
				*pos = tmp;
			}
			if ( pos != NULL )
			{
				if ((size_t)(pos-ptr) > len)
				{
					pos = &ptr[len];
					while (pos > ptr && *pos != ' ')
					{
						pos--;
					}
					if (pos > ptr)
					{
						len = pos-ptr;
						//pos++;
					} else
					{
						pos = &ptr[len];
					}
				}
				tmp = *pos;
				*pos = 0;
			} else if (strlen(ptr) > len)
			{
				pos = &ptr[len];
				while (pos > ptr && *pos != ' ')
				{
					pos--;
				}
				if (pos > ptr)
				{
					len = pos-ptr;
					//pos++;
				} else
				{
					pos = &ptr[len];
				}
				tmp = *pos;
				*pos = 0;
			}
			DFBCHECK( pFont->GetStringExtents(pFont, ptr, -1, &rect, NULL) );

			//x = (interfaceInfo.screenWidth-rectangle.w)/2-rect.x;
			if (ta > 0)
			{
				gfx_drawText(DRAWING_SURFACE, pFont, tr, tg, tb, ta, x-rect.x, y-rect.y, ptr, 0, 0);
			}
			y += rect.h;

			if ( pos != NULL )
			{
				*pos = tmp;
				ptr = pos+(tmp=='\n' || tmp==' ' ? 1 : 0);
			} else
			{
				ptr += len;
			}
			//pos = strchr(ptr, '\n');
		} while ( pos != NULL );
	}

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
}

void interface_drawScrollingBar(IDirectFBSurface *pSurface,
                                int x, int y, int w, int h,
                                int lineCount, int visibleLines, int lineOffset)
{
	int ty, inner_h, th;
	float step;

	if ( w >= INTERFACE_SCROLLBAR_WIDTH && 4*w <= h )
	{
		inner_h = h - 2*w;
		ty = y + w;
	} else
	{
		inner_h = h;
		ty = y;
	}

	step = (float)(inner_h)/(float)(lineCount > visibleLines ? lineCount : visibleLines);
	ty += step*lineOffset;
	th  = step*visibleLines;

	//dprintf("%s: step = %f\n", __FUNCTION__, step);

	DFBCHECK( pSurface->SetDrawingFlags(pSurface, DSDRAW_BLEND) );

	/* Draw scrollbar */
	//gfx_drawRectangle(pSurface, 0, 0, 200, 200, x, interfaceInfo.clientY + interfaceInfo.paddingSize*2 + INTERFACE_SCROLLBAR_WIDTH, width, step*pListMenu->baseMenu.menuEntryCount);
	gfx_drawRectangle(pSurface, 
	                  INTERFACE_SCROLLBAR_COLOR_RED,  INTERFACE_SCROLLBAR_COLOR_GREEN,
	                  INTERFACE_SCROLLBAR_COLOR_BLUE, INTERFACE_SCROLLBAR_COLOR_ALPHA,
	                  x, ty, w, th);
	interface_drawInnerBorder(pSurface,
	                          INTERFACE_SCROLLBAR_COLOR_LT_RED,  INTERFACE_SCROLLBAR_COLOR_LT_GREEN,
	                          INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA,
	                          x, ty, w, th, interfaceInfo.borderWidth,
	                          interfaceBorderSideTop|interfaceBorderSideLeft);
	interface_drawInnerBorder(pSurface,
	                          INTERFACE_SCROLLBAR_COLOR_DK_RED,  INTERFACE_SCROLLBAR_COLOR_DK_GREEN,
	                          INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA, 
	                          x, ty, w, th, interfaceInfo.borderWidth,
	                          interfaceBorderSideBottom|interfaceBorderSideRight);

	if ( inner_h < h )
	{
		th = w;

		ty = y;
		gfx_drawRectangle(pSurface,
		                  INTERFACE_SCROLLBAR_COLOR_RED,  INTERFACE_SCROLLBAR_COLOR_GREEN,
		                  INTERFACE_SCROLLBAR_COLOR_BLUE, INTERFACE_SCROLLBAR_COLOR_ALPHA,
		                  x, ty, w, th);
		interface_drawInnerBorder(pSurface, 
		                          INTERFACE_SCROLLBAR_COLOR_LT_RED,  INTERFACE_SCROLLBAR_COLOR_LT_GREEN,
		                          INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA,
		                          x, ty, w, th, interfaceInfo.borderWidth,
		                          interfaceBorderSideTop|interfaceBorderSideLeft);
		interface_drawInnerBorder(pSurface,
		                          INTERFACE_SCROLLBAR_COLOR_DK_RED,  INTERFACE_SCROLLBAR_COLOR_DK_GREEN,
		                          INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA,
		                          x, ty, w, th, interfaceInfo.borderWidth,
		                          interfaceBorderSideBottom|interfaceBorderSideRight);
		interface_drawIcon(pSurface, IMAGE_DIR "arrows.png",
		                   x+w/2, ty+w/2,
		                   INTERFACE_SCROLLBAR_ARROW_SIZE, INTERFACE_SCROLLBAR_ARROW_SIZE,
		                   0, 0, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle);

		ty = y + h - w;
		gfx_drawRectangle(pSurface,
		                  INTERFACE_SCROLLBAR_COLOR_RED,  INTERFACE_SCROLLBAR_COLOR_GREEN,
		                  INTERFACE_SCROLLBAR_COLOR_BLUE, INTERFACE_SCROLLBAR_COLOR_ALPHA,
		                  x, ty, w, th);
		interface_drawInnerBorder(pSurface,
		                          INTERFACE_SCROLLBAR_COLOR_LT_RED,  INTERFACE_SCROLLBAR_COLOR_LT_GREEN,
		                          INTERFACE_SCROLLBAR_COLOR_LT_BLUE, INTERFACE_SCROLLBAR_COLOR_LT_ALPHA,
		                          x, ty, w, th, interfaceInfo.borderWidth,
		                          interfaceBorderSideTop|interfaceBorderSideLeft);
		interface_drawInnerBorder(pSurface,
		                          INTERFACE_SCROLLBAR_COLOR_DK_RED,  INTERFACE_SCROLLBAR_COLOR_DK_GREEN,
		                          INTERFACE_SCROLLBAR_COLOR_DK_BLUE, INTERFACE_SCROLLBAR_COLOR_DK_ALPHA,
		                          x, ty, w, th, interfaceInfo.borderWidth,
		                          interfaceBorderSideBottom|interfaceBorderSideRight);
		interface_drawIcon(pSurface, IMAGE_DIR "arrows.png",
		                   x+w/2, ty+w/2,
		                   INTERFACE_SCROLLBAR_ARROW_SIZE, INTERFACE_SCROLLBAR_ARROW_SIZE,
		                   0, 1, DSBLIT_BLEND_ALPHACHANNEL,
		                   interfaceAlignCenter|interfaceAlignMiddle);
	}
}

void interface_displayCustomScrollingTextBox( int x, int y, int w, int h,
                                              const char *message,
                                              int lineOffset, int visibleLines, int lineCount, int icon)
{
	DFBRectangle rectangle, rect;
	int fh, i,maxWidth,tx,ty;
	const char *ptr;
	char *pos, tmp = 0;

	memset(&rectangle, 0, sizeof(DFBRectangle));

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

	tx = x + interfaceInfo.paddingSize;
	ty = y + interfaceInfo.paddingSize;
	maxWidth = w - 2*interfaceInfo.paddingSize;
	if ( icon > 0 )
	{
		//dprintf("%s: draw icon\n", __FUNCTION__);
		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[icon],
		                    x+interfaceInfo.paddingSize, y+interfaceInfo.paddingSize,
		                    interfaceInfo.thumbnailSize, interfaceInfo.thumbnailSize,
		                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
		tx       += interfaceInfo.thumbnailSize + interfaceInfo.paddingSize;
		maxWidth -= interfaceInfo.thumbnailSize + interfaceInfo.paddingSize;
	}

	//dprintf("%s: draw text\n", __FUNCTION__);

	if (message[0] != 0)
	{
		pgfx_font->GetHeight(pgfx_font, &fh);
		i = 0;
		for ( ptr = message; i < lineOffset && *ptr != 0; ptr++ )
		{
			if ( *ptr == '\n' )
			{
				i++;
			}
		}
		if ( *ptr != 0 )
		{
			i = 0;
			if ( *ptr == '\n' )
				ptr++;
			do
			{
				pos = strchr(ptr, '\n');
				if (pos != NULL)
				{
					tmp = *pos;
					*pos = 0;
				}
				DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, ptr, -1, &rect, NULL) );

				//x = (interfaceInfo.screenWidth-rectangle.w)/2-rect.x;
				gfx_drawText(DRAWING_SURFACE, pgfx_font,
				             INTERFACE_BOOKMARK_RED,  INTERFACE_BOOKMARK_GREEN,
				             INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA,
				             tx, ty + rect.h/2, ptr, 0, 0);
				ty += rect.h;
				i++;

				if ( pos != NULL )
				{
					*pos = tmp;
					ptr = pos+(tmp=='\n' ? 1 : 0);
				}
				//pos = strchr(ptr, '\n');
			} while ( pos != NULL && i < visibleLines );

			interface_drawScrollingBar(DRAWING_SURFACE,
			                           x + w - interfaceInfo.paddingSize - INTERFACE_SCROLLBAR_WIDTH,
			                           y + interfaceInfo.paddingSize,
			                           INTERFACE_SCROLLBAR_WIDTH,
			                           h - 2*interfaceInfo.paddingSize,
			                           lineCount, visibleLines, lineOffset );
		}
	}

#ifdef ENABLE_REGPLAT
	if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatPaymentMenu && summary_amount != NULL)
	{
		int n = (atoi(summary_amount) < atoi(card_balance) ? 2 : 1);
		int icons_w = n * INTERFACE_STATUSBAR_ICON_WIDTH + (n-1)*3*interfaceInfo.paddingSize;
		if ( icons_w > rectangle.w && maxWidth == 0 )
			rectangle.w = icons_w;
		tx = (interfaceInfo.screenWidth - icons_w) / 2;
		ty = y + h - INTERFACE_STATUSBAR_ICON_HEIGHT;
		/* draw cancel button */
		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[statusbar_f1_cancel],
		                    tx, ty, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT, 
		                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
		tx += INTERFACE_STATUSBAR_ICON_WIDTH + interfaceInfo.paddingSize * 3;
		if (n == 2)
			/* draw ok button */
			interface_drawImage(DRAWING_SURFACE, resource_thumbnails[statusbar_f2_ok],
			                    tx, ty, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT,
			                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
	}
#endif

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
}

void interface_displayScrollingTextBox( int x, int y, int w, int h, const char *message,
                                        int lineOffset, int visibleLines, int lineCount, int icon)
{
	interface_displayScrollingTextBoxColor(x, y, w, h, message, lineOffset, visibleLines, lineCount, icon,
	                                       INTERFACE_MESSAGE_BOX_BORDER_RED,  INTERFACE_MESSAGE_BOX_BORDER_GREEN,
	                                       INTERFACE_MESSAGE_BOX_BORDER_BLUE, INTERFACE_MESSAGE_BOX_BORDER_ALPHA,
	                                       INTERFACE_MESSAGE_BOX_RED,  INTERFACE_MESSAGE_BOX_GREEN,
	                                       INTERFACE_MESSAGE_BOX_BLUE, INTERFACE_MESSAGE_BOX_ALPHA);
}

void interface_displayScrollingTextBoxColor( int x, int y, int w, int h,
                                             const char *message,
                                             int lineOffset, int visibleLines,
                                             int lineCount, int icon,
                                             int br, int bg, int bb, int ba,
                                             int r, int g, int b, int a)
{
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

	//dprintf("%s: draw box\n", __FUNCTION__);
	gfx_drawRectangle(DRAWING_SURFACE, r, g, b, a, x, y, w, h);
	interface_drawOuterBorder(DRAWING_SURFACE, br, bg, bb, ba,
	                          x, y, w, h, interfaceInfo.borderWidth, interfaceBorderSideAll);

	interface_displayCustomScrollingTextBox(x,y,w,h,message,lineOffset,visibleLines,lineCount,icon);

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
}

void interface_displayMessageBox()
{
	int *icons = NULL;
	if ( interfaceInfo.messageBox.type != interfaceMessageBoxNone )
	{
		tprintf("-----------------------------------------------------------\n");
		tprintf("| %s\n", interfaceInfo.messageBox.message);
		tprintf("-----------------------------------------------------------\n");
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
		gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0x5F, 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenHeight);
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
		switch ( interfaceInfo.messageBox.type )
		{
			case interfaceMessageBoxCallback:
				if ( !interfaceInfo.keypad.enable )
					icons = (interfaceInfo.messageBox.pCallback == interface_enterTextCallback && 
					         appControlInfo.inputMode == inputModeABC) ?
					  (int*)interface_textBoxIcons :
					  (int*)interface_confirmBoxIcons;
#ifdef ENABLE_REGPLAT
				if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatPaymentMenu && 
				    summary_amount != NULL && atoi(summary_amount) > atoi(card_balance))
				{
					int interface_confBoxIcns[] = {statusbar_f1_cancel,0,0,0};
					icons = (int*)interface_confBoxIcns;
				}
#endif
				tprintf("| Confirmation: YES/NO |");
				// fall through
			case interfaceMessageBoxSimple:
				interface_displayCustomTextBoxColor(
					interfaceInfo.messageBox.target.x, interfaceInfo.messageBox.target.y,
					interfaceInfo.messageBox.message,
					interfaceInfo.messageBox.icon > 0 ? resource_thumbnails[interfaceInfo.messageBox.icon] : NULL, 
					interfaceInfo.messageBox.target.w, NULL, interfaceInfo.messageBox.target.h, icons,
					interfaceInfo.messageBox.colors.border.R, interfaceInfo.messageBox.colors.border.G,
					interfaceInfo.messageBox.colors.border.B, interfaceInfo.messageBox.colors.border.A,
					interfaceInfo.messageBox.colors.text.R, interfaceInfo.messageBox.colors.text.G,
					interfaceInfo.messageBox.colors.text.B, interfaceInfo.messageBox.colors.text.A,
					interfaceInfo.messageBox.colors.background.R, interfaceInfo.messageBox.colors.background.G,
					interfaceInfo.messageBox.colors.background.B, interfaceInfo.messageBox.colors.background.A,
   					pgfx_font);
				break;
			case interfaceMessageBoxScrolling:
#ifdef ENABLE_REGPLAT
				if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatPaymentMenu && summary_amount != NULL)
					interface_displayScrollingTextBox(
						interfaceInfo.clientX, interfaceInfo.clientY,
						interfaceInfo.clientWidth, interfaceInfo.clientHeight+2*interfaceInfo.paddingSize,
						interfaceInfo.messageBox.message, interfaceInfo.messageBox.scrolling.offset,
						interfaceInfo.messageBox.scrolling.visibleLines, interfaceInfo.messageBox.scrolling.lineCount,
						interfaceInfo.messageBox.icon);
				else
#endif
				interface_displayScrollingTextBoxColor(
					interfaceInfo.messageBox.target.x, interfaceInfo.messageBox.target.y,
					interfaceInfo.messageBox.target.w, interfaceInfo.messageBox.target.h,
					interfaceInfo.messageBox.message, interfaceInfo.messageBox.scrolling.offset,
					interfaceInfo.messageBox.scrolling.visibleLines, interfaceInfo.messageBox.scrolling.lineCount,
					interfaceInfo.messageBox.icon,
					interfaceInfo.messageBox.colors.border.R, interfaceInfo.messageBox.colors.border.G,
					interfaceInfo.messageBox.colors.border.B, interfaceInfo.messageBox.colors.border.A,
					interfaceInfo.messageBox.colors.background.R, interfaceInfo.messageBox.colors.background.G,
					interfaceInfo.messageBox.colors.background.B, interfaceInfo.messageBox.colors.background.A);
				break;
			case interfaceMessageBoxPoster:
				interface_displayPosterBox();
				break;
			default:
				eprintf("%s: Unsupported message box type %d\n", __FUNCTION__, interfaceInfo.messageBox.type);
		}
	}
}

static int interface_displayPosterBox()
{
	int x,y,h;
	int len;
	char tmp = 0;
	int fa;
	DFBRectangle title_rect;

	pgfx_font->GetAscender(pgfx_font, &fa);
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
	gfx_drawRectangle(DRAWING_SURFACE,
	                  interfaceInfo.messageBox.colors.title.R,
	                  interfaceInfo.messageBox.colors.title.G,
	                  interfaceInfo.messageBox.colors.title.B,
	                  interfaceInfo.messageBox.colors.title.A,
	                  interfaceInfo.messageBox.target.x, interfaceInfo.messageBox.target.y,
	                  interfaceInfo.messageBox.target.w, INTERFACE_POSTER_TITLE_HEIGHT);

	len = getMaxStringLength(interfaceInfo.messageBox.title,
	                         interfaceInfo.messageBox.target.w - 2*interfaceInfo.paddingSize);
	if (len < (int)strlen(interfaceInfo.messageBox.title))
		tmp = interfaceInfo.messageBox.title[len];
	interfaceInfo.messageBox.title[len] = 0;

	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, interfaceInfo.messageBox.title, -1, &title_rect, NULL) );
	gfx_drawText(DRAWING_SURFACE, pgfx_font,
	             INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, 
	             INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA,
	             interfaceInfo.messageBox.target.x + (interfaceInfo.messageBox.target.w - title_rect.w)/2, 
	             interfaceInfo.messageBox.target.y + (INTERFACE_POSTER_TITLE_HEIGHT + fa)/2,
	             interfaceInfo.messageBox.title, 0 /* no box */, 1 /* shadow */);
	interfaceInfo.messageBox.title[len] = tmp;

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
	gfx_drawRectangle(DRAWING_SURFACE,
	                  INTERFACE_BACKGROUND_RED,  INTERFACE_BACKGROUND_GREEN,
	                  INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
	                  interfaceInfo.messageBox.target.x, interfaceInfo.messageBox.target.y+INTERFACE_POSTER_TITLE_HEIGHT,
	                  interfaceInfo.messageBox.target.w, interfaceInfo.messageBox.target.h-INTERFACE_POSTER_TITLE_HEIGHT);

	h = interfaceInfo.messageBox.target.h - INTERFACE_POSTER_TITLE_HEIGHT;
	y = interfaceInfo.messageBox.target.y + INTERFACE_POSTER_TITLE_HEIGHT + h/2;
	if ( interfaceInfo.messageBox.message[0] )
	{
		interface_displayCustomScrollingTextBox(
			interfaceInfo.messageBox.target.x + INTERFACE_POSTER_PICTURE_WIDTH,
			interfaceInfo.messageBox.target.y + INTERFACE_POSTER_TITLE_HEIGHT,
			interfaceInfo.messageBox.target.w - INTERFACE_POSTER_PICTURE_WIDTH, h,
			interfaceInfo.messageBox.message, interfaceInfo.messageBox.scrolling.offset,
			interfaceInfo.messageBox.scrolling.visibleLines, interfaceInfo.messageBox.scrolling.lineCount, -1);
		x = interfaceInfo.messageBox.target.x + INTERFACE_POSTER_PICTURE_WIDTH/2;
	} else
		x = interfaceInfo.messageBox.target.x + interfaceInfo.messageBox.target.w/2;

	if ( 0 != interface_drawImage(DRAWING_SURFACE, interfaceInfo.messageBox.poster,
	                              x, y, INTERFACE_POSTER_PICTURE_WIDTH,  interfaceInfo.messageBox.target.h,
	                              0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0)
	   )
	{
		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[interfaceInfo.messageBox.icon],
		                    x, y, INTERFACE_THUMBNAIL_SIZE, INTERFACE_THUMBNAIL_SIZE,
		                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0);
	}

	return 0;
}

void interface_displayVirtualKeypad()
{
	int maxKeysInRow = 0;
	int maxRows = 0;
	int cell=0;
	int row=0;
	int x,y,w,h,cx,cy,cw,ch;
	char buf[16];
	int r, g, b, a;
	DFBRectangle rectangle;

	int controlButtonCount = (int)(sizeof(controlButtons)/sizeof(button_t));

	if (interfaceInfo.keypad.enable)
	{
		while (keypad[row][cell] != 0)
		{
			while (keypad[row][cell] != 0)
			{

				cell++;
			}
			if (maxKeysInRow < cell)
			{
				maxKeysInRow = cell;
			}
			cell = 0;
			row++;
		}
		maxRows = row;

		//dprintf("%s: %dx%d\n", __FUNCTION__, maxKeysInRow, maxRows);

		w = VKEYPAD_BUTTON_WIDTH*maxKeysInRow+(maxKeysInRow+1)*interfaceInfo.paddingSize;
		h = VKEYPAD_BUTTON_HEIGHT*(maxRows+1)+(maxRows+2)*interfaceInfo.paddingSize;
		x = interfaceInfo.clientX+(interfaceInfo.clientWidth-w)/2;
#ifdef ENABLE_REGPLAT
		if (interfaceInfo.keypad.enable == 1 && interfaceInfo.currentMenu->selectedItem >= 2)
			y = interfaceInfo.clientY+interfaceInfo.paddingSize*2;
		else
#endif
		y = interfaceInfo.clientY+interfaceInfo.clientHeight-h+VKEYPAD_BUTTON_HEIGHT+interfaceInfo.paddingSize*2;

		gfx_drawRectangle(DRAWING_SURFACE,
		                  INTERFACE_MESSAGE_BOX_RED, INTERFACE_MESSAGE_BOX_GREEN,
		                  INTERFACE_MESSAGE_BOX_BLUE, 0xFF, x, y, w, h);
		interface_drawInnerBorder(DRAWING_SURFACE,
		                          INTERFACE_BORDER_RED, INTERFACE_BORDER_GREEN,
		                          INTERFACE_BORDER_BLUE, 0xFF, x, y, w, h,
		                          interfaceInfo.borderWidth, interfaceBorderSideAll);

		ch = VKEYPAD_BUTTON_HEIGHT;
		for (row=0; row<maxRows; row++)
		{
			for (cell=0; cell<maxKeysInRow; cell++)
			{
				if (keypad[row][cell] != 0)
				{
					cx = x+interfaceInfo.paddingSize*(1+cell)+VKEYPAD_BUTTON_WIDTH*cell;
					cy = y+interfaceInfo.paddingSize*(1+row)+VKEYPAD_BUTTON_HEIGHT*row;
					if (keypad[row][cell+1] == 0)
					{
						cw = VKEYPAD_BUTTON_WIDTH*(maxKeysInRow-cell)+interfaceInfo.paddingSize*(maxKeysInRow-cell-1);
					} else
					{
						cw = VKEYPAD_BUTTON_WIDTH;
					}
					gfx_drawRectangle(DRAWING_SURFACE,
					                  INTERFACE_SCROLLBAR_COLOR_RED, INTERFACE_SCROLLBAR_COLOR_GREEN,
					                  INTERFACE_SCROLLBAR_COLOR_BLUE, 0xFF, cx, cy, cw, ch);
					if (row == interfaceInfo.keypad.row && cell == interfaceInfo.keypad.cell)
					{
						r = 0x00;
						g = 0x00;
						b = 0xFF;
						a = 0xFF;
					} else
					{
						r = INTERFACE_SCROLLBAR_COLOR_LT_RED;
						g = INTERFACE_SCROLLBAR_COLOR_LT_GREEN;
						b = INTERFACE_SCROLLBAR_COLOR_LT_BLUE;
						a = 0xFF;
					}
					interface_drawInnerBorder(DRAWING_SURFACE,
					                          r, g, b, a, cx, cy, cw, ch,
					                          interfaceInfo.borderWidth, interfaceBorderSideAll);
#ifdef WCHAR_SUPPORT
					memset(buf,0,sizeof(buf));
					if ( interfaceInfo.keypad.altLayout == ALTLAYOUT_ON && keypad_local[row][cell] != 0 )
						utf8_wctomb( (unsigned char *)buf,
						             interfaceInfo.keypad.shift ? wuc(keypad_local[row][cell]) :
						                                          wlc(keypad_local[row][cell]), sizeof(buf));
					else
						utf8_wctomb( (unsigned char *)buf,
						             interfaceInfo.keypad.shift ? wuc(keypad[row][cell]) :
						                                          wlc(keypad[row][cell]), sizeof(buf));
#else
					sprintf(buf, "%c", interfaceInfo.keypad.shift ? toupper(keypad[row][cell]) :
					                                                tolower(keypad[row][cell]));
#endif
					pgfx_font->GetStringExtents(pgfx_font, buf, -1, &rectangle, NULL);
					if (row == interfaceInfo.keypad.row && cell == interfaceInfo.keypad.cell)
					{
						r = 0x00;
						g = 0x00;
						b = 0xFF;
						a = 0xFF;
					} else
					{
						r = INTERFACE_BOOKMARK_RED;
						g = INTERFACE_BOOKMARK_GREEN;
						b = INTERFACE_BOOKMARK_BLUE;
						a = 0xFF;
					}
					gfx_drawText(DRAWING_SURFACE, pgfx_font,
					             r, g, b, a,
					             cx+cw/2-(rectangle.w-rectangle.x)/2,
					             cy+ch+ch/2-(rectangle.h-rectangle.y)/2,
					             buf, 0, 0);
				}
			}
		}
		cy = y+interfaceInfo.paddingSize*(1+maxRows)+VKEYPAD_BUTTON_HEIGHT*maxRows;
		cw = (w-interfaceInfo.paddingSize*(1+controlButtonCount))/controlButtonCount;
		for (cell=0; cell<controlButtonCount; cell++)
		{
			cx = x+interfaceInfo.paddingSize*(1+cell)+cw*cell;
			gfx_drawRectangle(DRAWING_SURFACE,
			                  controlButtons[cell].r, controlButtons[cell].g,
			                  controlButtons[cell].b, controlButtons[cell].a,
			                  cx, cy, cw, ch);
			if (interfaceInfo.keypad.row == maxRows && cell == interfaceInfo.keypad.cell)
			{
				r = 0x00;
				g = 0x00;
				b = 0xFF;
				a = 0xFF;
			} else
			{
				r = INTERFACE_BOOKMARK_RED;
				g = INTERFACE_BOOKMARK_GREEN;
				b = INTERFACE_BOOKMARK_BLUE;
				a = 0xFF;
			}
			interface_drawInnerBorder(DRAWING_SURFACE,
			                          r, g, b, a, cx, cy, cw, ch,
			                          interfaceInfo.borderWidth, interfaceBorderSideAll);
			pgfx_font->GetStringExtents(pgfx_font, controlButtons[cell].name, -1, &rectangle, NULL);
			gfx_drawText(DRAWING_SURFACE,
			             pgfx_font, r, g, b, a,
			             cx+cw/2-(rectangle.w-rectangle.x)/2, cy+ch+ch/2-(rectangle.h-rectangle.y)/2,
			             controlButtons[cell].name, 0, 0);
		}
	}
}

void interface_displayCustomSliderInMenu()
{
	if (interfaceInfo.customSlider != NULL &&
		(interfaceInfo.showMenu == 0 || interfaceInfo.customSliderVisibleInMenu != 0))
	{
		interface_displayCustomSlider(
			interfaceInfo.customSlider, interfaceInfo.customSliderArg, 1,
			interfaceInfo.clientX+interfaceInfo.paddingSize,
			interfaceInfo.clientY+interfaceInfo.marginSize/2,
			interfaceInfo.clientWidth-interfaceInfo.paddingSize*2,
			pgfx_smallfont);
	}
}

int interface_displayCustomSlider(customSliderFunction pCallback, void *pCallbackArg,
                                  int drawBox, int cx, int cy, int cw, IDirectFBFont *pFont)
{
	char buffer[MAX_MESSAGE_BOX_LENGTH];
	int count, i, fh, fa, ch;
	interfaceCustomSlider_t info;

	/************************************************
	* Slider 1                                     *
	* ==========---------------------------------- *
	* Slider 2                                     *
	* ===================------------------------- *
	* ...                                          *
	************************************************/

	ch = 0;
	count = pCallback(-1, NULL, pCallbackArg);
	if (count > 0)
	{
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

		DFBCHECK( pFont->GetHeight(pFont, &fh) );
		DFBCHECK( pFont->GetAscender(pFont, &fa) );

		ch = fh*count*2;

		if (drawBox)
		{
			cx += INTERFACE_ROUND_CORNER_RADIUS;
			cy += INTERFACE_ROUND_CORNER_RADIUS;
			cw -= INTERFACE_ROUND_CORNER_RADIUS*2;

			interface_drawRoundBox(cx, cy, cw, ch);

			ch += INTERFACE_ROUND_CORNER_RADIUS*2;
		}

		i = 0;
		while (i < count && pCallback(i, &info, pCallbackArg) > 0)
		{
			int x, y;
			DFBRectangle clip, rect;
			float value;
			int colors[4][4] =	{
				{0xFF, 0x00, 0x00, 0xFF},
				{0xFF, 0xFF, 0x00, 0xFF},
				{0x00, 0xFF, 0x00, 0xFF},
				{0x00, 0xFF, 0x00, 0xFF},
			};

			x = cx;
			y = cy+fh*2*i+fa;

			strcpy(buffer, info.caption);
			gfx_drawText(DRAWING_SURFACE, pFont, 255, 255, 255, 255, x, y, buffer, 0, 0);

			rect.x = x;
			rect.y = cy+fh*(i*2+1);
			rect.w = cw;
			rect.h = fa;

			value = (float)(info.value-info.min)/(float)(info.max-info.min);
			if (value < 1.0f)
			{
				/*if (info.steps == 0)
				{
				interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_inactive.png", rect.x, rect.y, rect.w, rect.h, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
				} else*/
				{
					gfx_drawRectangle(DRAWING_SURFACE, 0xc9, 0xc9, 0xc9, 0xFF, rect.x, rect.y, rect.w, rect.h);
				}
			}

			clip.x = 0;
			clip.y = 0;
			clip.w = rect.w*value;
			clip.h = rect.h;

			/* Leave at least a small part of slider */
			if (clip.w == 0)
			{
				clip.w = rect.w/100+1;
			}

			if (info.steps == 0 || info.steps > 3)
			{
				gfx_drawRectangle(DRAWING_SURFACE, 
				                  colors[2][0], colors[2][1], colors[2][2], colors[2][3],
				                  rect.x, rect.y, clip.w, clip.h);
			} else
			{
				int stepsize = (info.max-info.min)/(info.steps);
				int step = info.value/stepsize;
				int cindex;

				if (info.steps == 1)
				{
					cindex = step == 1 ? 2 : 0;
				} else
				{
					cindex = step*3/info.steps;
				}

				//dprintf("%s: %d/%d = %d-%d/%d = %d\n", __FUNCTION__, info.value, step, info.min, info.max, info.steps, cindex);

				gfx_drawRectangle(DRAWING_SURFACE,
				                  colors[cindex][0], colors[cindex][1], colors[cindex][2], colors[cindex][3],
				                  rect.x, rect.y, clip.w, clip.h);
			}

			i++;
		}
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
	}
	return ch;
}

void interface_displayNotify()
{
	if ( interfaceInfo.notifyText[0] != 0 )
	{
		DFBRectangle rect;

		tprintf("+++ %s\n", interfaceInfo.notifyText);

		DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, interfaceInfo.notifyText, -1, &rect, NULL) );

		gfx_drawText(DRAWING_SURFACE, pgfx_font, 0, 0, 200, 255,
		             interfaceInfo.clientX+interfaceInfo.clientWidth-(rect.w-rect.x)-interfaceInfo.paddingSize,
		             interfaceInfo.clientY+interfaceInfo.clientHeight+interfaceInfo.paddingSize+rect.h,
		             interfaceInfo.notifyText, 1, 0);
	}
}

void interface_setBackground(int r, int g, int b, int a, const char *image)
{
	interfaceInfo.background.Enable = 1;

	interfaceInfo.background.R = r;
	interfaceInfo.background.G = g;
	interfaceInfo.background.B = b;
	interfaceInfo.background.A = a;

	if (image != NULL)
	{
		strcpy(interfaceInfo.background.image, image);
	} else
	{
		interfaceInfo.background.image[0] = 0;
	}
}

inline void interface_disableBackground()
{
	interfaceInfo.background.Enable = 0;
}

void interface_displayBackground()
{
#if !(defined STB225) && !(defined STSDK)
	/// FIXME: Quality of jpeg decoder on STB225 is too bad
	/* FIXME: Background drawing is slow on ST. 
	Slow drawing can be fixed with DRAWING_SURFACE->SetPorterDuff (DRAWING_SURFACE, DSPD_SRC_OVER); */
	if (interfaceInfo.showMenu &&
	    interfacePlayControl.activeButton == interfacePlayControlStop &&
	    appControlInfo.slideshowInfo.state == slideshowDisabled &&
	    appControlInfo.slideshowInfo.showingCover == 0)
	{
		interface_drawImage(DRAWING_SURFACE, INTERFACE_WALLPAPER_IMAGE,
		                    interfaceInfo.screenWidth/2, interfaceInfo.screenHeight/2,
		                    interfaceInfo.screenWidth,   interfaceInfo.screenHeight, 
		                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, NULL, NULL);
		return;
	}

	if ( interfaceInfo.background.Enable != 0 )
	{
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

		gfx_drawRectangle(DRAWING_SURFACE, 
		                  interfaceInfo.background.R, interfaceInfo.background.G,
		                  interfaceInfo.background.B, interfaceInfo.background.A,
		                  0, 0, interfaceInfo.screenWidth, interfaceInfo.screenHeight);

		if (interfaceInfo.background.image[0] != 0)
		{
			interface_drawImage(DRAWING_SURFACE, interfaceInfo.background.image,
			                    interfaceInfo.screenWidth/2, interfaceInfo.screenHeight/2,
			                    interfaceInfo.screenWidth,   interfaceInfo.screenHeight,
			                    0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, NULL, NULL);
		}
	}
#endif // !STB225 && !STSDK
#ifndef STBPNX
	if (appControlInfo.slideshowInfo.state != slideshowDisabled)
	{
		interface_drawImage(DRAWING_SURFACE, appControlInfo.slideshowInfo.filename,
		                    interfaceInfo.screenWidth/2, interfaceInfo.screenHeight/2,
		                    interfaceInfo.screenWidth,   interfaceInfo.screenHeight,
		                    0, NULL, DSBLIT_NOFX, interfaceAlignCenter|interfaceAlignMiddle, NULL, NULL);
	}
#endif
}


void interface_displayLoading()
{
	char *str = _T("LOADING");
	if (interfaceInfo.showLoading)
		interface_displayTextBox(
			interfaceInfo.screenWidth/2, interfaceInfo.screenHeight/2, 
			str, IMAGE_DIR "thumbnail_loading.png", 0, NULL, 0);
}

void interface_showLoading()
{
	interfaceInfo.showLoading = 1;
}

void interface_hideLoading()
{
	interfaceInfo.showLoading = 0;
}

void interface_displayMenu(int flipFB)
{
	interface_animateMenu(flipFB, 0);
}


static void interface_animateMenu(int flipFB, int animate)
{
	int x, y, w, h, n;
#ifdef SCREEN_TRACE
	unsigned long long cur, start;
#endif

	if (appControlInfo.inStandby) return;
	if (interfaceInfo.cleanUpState) return;

	//dprintf("%s: in\n", __FUNCTION__);

#ifdef ENABLE_VIDIMAX
	if (appControlInfo.vidimaxInfo.active){	
		if (vidimax_refreshMenu() == 0){
			return;
		}
	}
#endif

	mysem_get(interface_semaphore);

#ifdef ENABLE_TELETEXT
	if ( interfaceInfo.teletext.show && 
	     appControlInfo.teletextInfo.status>=teletextStatus_demand &&
	    !appControlInfo.teletextInfo.subtitleFlag)
	{
		teletext_displayTeletext();
		interface_flipSurface();
		mysem_release(interface_semaphore);
		return;
	}
#endif
#ifdef SCREEN_TRACE
	start = getCurrentTime();
#endif

	DFBCHECK(DRAWING_SURFACE->GetSize(DRAWING_SURFACE, &interfaceInfo.screenWidth, &interfaceInfo.screenHeight));

	interfaceInfo.clientX = interfaceInfo.marginSize;
	interfaceInfo.clientY = interfaceInfo.marginSize;
	interfaceInfo.clientWidth = interfaceInfo.screenWidth-interfaceInfo.marginSize*2;
	interfaceInfo.clientHeight = interfaceInfo.screenHeight-interfaceInfo.marginSize*2;

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

	gfx_clearSurface(DRAWING_SURFACE, interfaceInfo.screenWidth, interfaceInfo.screenHeight);

	/*if (appControlInfo.inStandby != 0)
	{
	interface_flipSurface(DRAWING_SURFACE);
	mysem_release(interface_semaphore);
	return;
	}*/
	interface_displayBackground();

#ifdef SHOW_LOGO_TEXT
	/* Show logo text */
	int lw = 330;
	int lh = 36;
	int lx = interfaceInfo.screenWidth-lw-12;
	int ly = 12;
	DFBRectangle lrect;
	//	int fh;
	//pgfx_font->GetHeight(pgfx_font, &fh);
	gfx_drawRectangle(DRAWING_SURFACE, 20, 50, 100, 255, lx, ly, lw, lh);
	pgfx_font->GetStringExtents(pgfx_font, SHOW_LOGO_TEXT, -1, &lrect, NULL);
	//dprintf("%s: %d %d %d %d, %d\n", __FUNCTION__, lx, ly, lw, lh, fh);
	gfx_drawText(DRAWING_SURFACE, pgfx_font, 255, 255, 255, 255, lx+lw/2-lrect.w/2, ly+lh/2+lrect.h/2, SHOW_LOGO_TEXT, 0, 0);
#endif

#ifndef STB225
#ifdef SHOW_LOGO_IMAGE
	/* Show logo image */
	int lx = interfaceInfo.screenWidth-12;
	int ly = 12;
	interface_drawImage(pgfx_frameBuffer, IMAGE_DIR SHOW_LOGO_IMAGE,
	                    lx, ly, 0, 0, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL,
	                    interfaceAlignRight|interfaceAlignTop, NULL, NULL);
#endif
#endif

	//Kpy.. Special for 3d output, draw line that hides 3D header at the top left coner of the screen
/*	if (interfaceInfo.showMenu || interfacePlayControl.visibleFlag || interfaceSlideshowControl.visibleFlag)
	{
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
		gfx_drawRectangle(DRAWING_SURFACE, 127, 127, 127, 255, 0, 0, 2, 2);
	}*/

#if (defined STB225)
// 	Kpy disable logo
//	interface_drawImage(pgfx_frameBuffer, IMAGE_DIR "logo-elc.png", 40, 40, 20, 25, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, NULL, NULL);
	{
	int videoLayer = gfx_getMainVideoLayer();
	int imgWidth, imgHeight;
	char outBuf[256];
	gfx_getVideoPlaybackSize(&imgWidth, &imgHeight);

	if (imgWidth!=0 && imgHeight!=0) {
		if (appControlInfo.outputInfo.has_3D_TV || interfaceInfo.mode3D==0) {
			gfx_setSourceRectangle(gfx_getMainVideoLayer(), 0, 0, imgWidth, imgHeight, 1);
		} else {
			gfx_setSourceRectangle(gfx_getMainVideoLayer(), 0, 0, imgWidth/2, imgHeight, 1);
		}
//		gfx_setDestinationRectangle(gfx_getMainVideoLayer(), 0, 0, 1920, 1080, 1);
	}

	if ( appControlInfo.outputInfo.has_3D_TV && interfaceInfo.mode3D) {
		if (interfaceInfo.showMenu || interfacePlayControl.visibleFlag || interfaceSlideshowControl.visibleFlag) {
			gfx_fb1_clear(0x7f, 0x7f, 0x7f, 0xff);
		} else {
			gfx_fb1_clear(0x0, 0x0, 0x0, 0x0);
//	Kpy disable logo
//			gfx_fb1_draw_rect(40, 40, 20, 20, 0x7f); //depth for logo
		}
//		usleep(10000); //trick for correct write 3dheader
	} else {
		gfx_fb1_clear(0x0, 0x0, 0x0, 0x0);
	}
		
	}
#endif



	if ( !interfaceInfo.showMenu )
	{
		//dprintf("%s: display play control\n", __FUNCTION__);
#ifdef ENABLE_MESSAGES
		interface_displayMessageNotify();
#endif

		interface_displayLoading();
		interface_displayNotify();
		interface_displayCustomSliderInMenu();
		if ( interfacePlayControl.pDisplay != NULL )
			interfacePlayControl.pDisplay();
		interface_displaySoundControl();
		interface_displaySliderControl();
		interface_displayMessageBox();
		interface_displayVirtualKeypad();
		interface_displayCall();
#ifdef ENABLE_TELETEXT
		if (interfaceInfo.teletext.show)
		{
			teletext_displayTeletext();
		}
#endif

		interface_flipSurface();

#ifdef SCREEN_TRACE
		cur = getCurrentTime();
		eprintf("%s: screen updated in %lu sec\n", __FUNCTION__, (unsigned long)(cur-start));
#endif
		mysem_release(interface_semaphore);
		return;
	}

	//DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

	//gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, interfaceInfo.clientX, interfaceInfo.clientY, interfaceInfo.clientWidth, interfaceInfo.clientHeight);

	/*if (interfaceInfo.currentMenu->menuType != interfaceMenuList || ((interfaceListMenu_t*)interfaceInfo.currentMenu)->listMenuType != interfaceListMenuBigThumbnail)
	{
	interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_BORDER_RED, INTERFACE_BORDER_GREEN, INTERFACE_BORDER_BLUE, INTERFACE_BORDER_ALPHA, interfaceInfo.clientX, interfaceInfo.clientY, interfaceInfo.clientWidth, interfaceInfo.clientHeight, interfaceInfo.borderWidth, interfaceBorderSideAll);
	}*/

	//dprintf("%s: check parent\n", __FUNCTION__);
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

	if ( interfaceInfo.currentMenu->pParentMenu != NULL )
	{
		n = 1;
		if ( interfaceInfo.currentMenu->pParentMenu->pParentMenu != NULL )
		{
			n++;
		}
		w = n*INTERFACE_MENU_ICON_WIDTH + (n+1)*interfaceInfo.paddingSize;
		x = interfaceInfo.clientX + interfaceInfo.clientWidth - w;
		y = interfaceInfo.clientY - INTERFACE_MENU_ICON_HEIGHT - interfaceInfo.paddingSize;

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

		gfx_drawRectangle(DRAWING_SURFACE,
		                  INTERFACE_BACKGROUND_RED,  INTERFACE_BACKGROUND_GREEN,
		                  INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
		                  x+INTERFACE_ROUND_CORNER_RADIUS, y,
		                  w - 2*INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);
		gfx_drawRectangle(DRAWING_SURFACE,
		                  INTERFACE_BACKGROUND_RED,  INTERFACE_BACKGROUND_GREEN,
		                  INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
		                  x, y+INTERFACE_ROUND_CORNER_RADIUS,
		                  w, INTERFACE_MENU_ICON_HEIGHT + interfaceInfo.paddingSize - INTERFACE_ROUND_CORNER_RADIUS);

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

		DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE,
			INTERFACE_BACKGROUND_RED,  INTERFACE_BACKGROUND_GREEN,
			INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
		                   x, y, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
		                   0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
		                   x+w-INTERFACE_ROUND_CORNER_RADIUS, y, 
		                   INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
		                   0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);


		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "menu_buttons.png", 
		                   interfaceInfo.clientX + interfaceInfo.clientWidth - interfaceInfo.paddingSize - INTERFACE_MENU_ICON_WIDTH,
		                   y + interfaceInfo.paddingSize, INTERFACE_MENU_ICON_WIDTH, INTERFACE_MENU_ICON_HEIGHT,
		                   interfaceInfo.currentMenu->selectedItem == MENU_ITEM_MAIN,
		                   0, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);

		//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "menu_logo.png", interfaceInfo.clientX + interfaceInfo.clientWidth - interfaceInfo.paddingSize - INTERFACE_MENU_ICON_WIDTH, y + interfaceInfo.paddingSize, INTERFACE_MENU_ICON_WIDTH, INTERFACE_MENU_ICON_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);

		//interface_drawBookmark(DRAWING_SURFACE, pgfx_font, interfaceInfo.clientX, interfaceInfo.clientY/*+interfaceInfo.borderWidth*/, _T("MAIN_MENU"), interfaceInfo.currentMenu->selectedItem == 	MENU_ITEM_MAIN, &x);
		if ( interfaceInfo.currentMenu->pParentMenu->pParentMenu != NULL )
		{
			tprintf("|%cMain menu|", interfaceInfo.currentMenu->selectedItem == MENU_ITEM_MAIN ? '>' : ' ');
			tprintf("|%cBack|", interfaceInfo.currentMenu->selectedItem == MENU_ITEM_BACK ? '>' : ' ');
			//interface_drawBookmark(DRAWING_SURFACE, pgfx_font, x+interfaceInfo.paddingSize, interfaceInfo.clientY/*+interfaceInfo.borderWidth*/, _T("BACK"), interfaceInfo.currentMenu->selectedItem == MENU_ITEM_BACK, &x);
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "menu_buttons.png",
			                   interfaceInfo.clientX + interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize - 2*INTERFACE_MENU_ICON_WIDTH,
			                   y + interfaceInfo.paddingSize,
			                   INTERFACE_MENU_ICON_WIDTH, INTERFACE_MENU_ICON_HEIGHT,
			                   interfaceInfo.currentMenu->selectedItem == MENU_ITEM_BACK, 
			                   1, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);

			//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "menu_back.png", interfaceInfo.clientX + interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize - 2*INTERFACE_MENU_ICON_WIDTH, y + interfaceInfo.paddingSize, INTERFACE_MENU_ICON_WIDTH, INTERFACE_MENU_ICON_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
		}
		if (interfaceInfo.currentMenu->name[0] != 0)
		{
			/*//right-aligned bookmark
			interface_drawBookmark(DRAWING_SURFACE, pgfx_font,
			interfaceInfo.clientX,
			interfaceInfo.clientY,//+interfaceInfo.borderWidth,
			&interfaceInfo.currentMenu->name[getLeftStringOverflow(interfaceInfo.currentMenu->name,
			interfaceInfo.clientX+interfaceInfo.clientWidth-x-3*interfaceInfo.paddingSize)], -1, &x);*/
			char c;
			int name_length;
			DFBRectangle rectangle;

			x = 3*interfaceInfo.paddingSize;
			if (interfaceInfo.currentMenu->logo >0 && interfaceInfo.currentMenu->logoX < 0)
			{
				x += 2*interfaceInfo.paddingSize + INTERFACE_THUMBNAIL_SIZE;
			}
			name_length = getMaxStringLength(interfaceInfo.currentMenu->name, interfaceInfo.clientWidth - w - x);
			c = interfaceInfo.currentMenu->name[name_length];
			interfaceInfo.currentMenu->name[name_length] = 0;

			DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, interfaceInfo.currentMenu->name, -1, &rectangle, NULL) );
			w = rectangle.w + x;
			x = interfaceInfo.clientX;
			h = 2*INTERFACE_ROUND_CORNER_RADIUS;
			y = interfaceInfo.clientY - interfaceInfo.paddingSize - (INTERFACE_THUMBNAIL_SIZE + h)/2;

			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

			gfx_drawRectangle(DRAWING_SURFACE,
			                  INTERFACE_BACKGROUND_RED,  INTERFACE_BACKGROUND_GREEN,
			                  INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
			                  x+INTERFACE_ROUND_CORNER_RADIUS, y,
			                  w-INTERFACE_ROUND_CORNER_RADIUS*2, 2*INTERFACE_ROUND_CORNER_RADIUS);

			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

			DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, 
				INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
			                   x, y,
			                   INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
			                   0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
			                   x+w-INTERFACE_ROUND_CORNER_RADIUS, y,
			                   INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
			                   0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
			                   x, y+INTERFACE_ROUND_CORNER_RADIUS,
			                   INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
			                   1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
			                   x+w-INTERFACE_ROUND_CORNER_RADIUS, y+INTERFACE_ROUND_CORNER_RADIUS,
			                   INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
			                   1, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);

			if (interfaceInfo.currentMenu->logo >0 && interfaceInfo.currentMenu->logoX < 0)
			{
				x += 2*interfaceInfo.paddingSize;
#ifdef ENABLE_REGPLAT
				if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatServicesMenu)
				{
					template_t *templ = (template_t *)regplatServicesMenu.baseMenu.menuEntry[0].pArg;
					if (templ->sectionInfo.icon[0] != 0)
					{
						/* draw icon for a section */
						interface_drawImage(DRAWING_SURFACE, templ->sectionInfo.icon,
							x, interfaceInfo.clientY - INTERFACE_THUMBNAIL_SIZE - interfaceInfo.paddingSize,
							INTERFACE_THUMBNAIL_SIZE, INTERFACE_THUMBNAIL_SIZE,
							0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
					}
					/* draw user balance */
					int font_h;
					pgfx_font->GetHeight(pgfx_font, &font_h);
					char balance_str[126] = "Баланс:\n";
					strcat(balance_str, card_balance);
					strcat(balance_str, " р.");
					interface_drawTextWW(pgfx_font, 
						INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN,
						INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA,
#ifndef STSDK
		                interfaceInfo.clientX+interfaceInfo.clientWidth-3.3*(INTERFACE_CLOCK_DIGIT_WIDTH*4+INTERFACE_CLOCK_COLON_WIDTH),
		                interfaceInfo.screenHeight - interfaceInfo.marginSize, 
#else
		                interfaceInfo.clientX+interfaceInfo.clientWidth-(int)(5*(INTERFACE_CLOCK_DIGIT_WIDTH*4+INTERFACE_CLOCK_COLON_WIDTH)), 
		                interfaceInfo.screenHeight - interfaceInfo.marginSize*5/4, 
#endif
						10*font_h, 2*font_h, balance_str, ALIGN_RIGHT);
				}
				else
#endif
				interface_drawImage(DRAWING_SURFACE, resource_thumbnails[interfaceInfo.currentMenu->logo],
					x, interfaceInfo.clientY - INTERFACE_THUMBNAIL_SIZE - interfaceInfo.paddingSize,
					INTERFACE_THUMBNAIL_SIZE, INTERFACE_THUMBNAIL_SIZE,
					0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
				x += INTERFACE_THUMBNAIL_SIZE;
				w -= INTERFACE_THUMBNAIL_SIZE + interfaceInfo.paddingSize;
			}
			gfx_drawRectangle(DRAWING_SURFACE,
			                  INTERFACE_BOOKMARK_RED,  INTERFACE_BOOKMARK_GREEN,
			                  INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA,
			                  x - interfaceInfo.paddingSize + INTERFACE_ROUND_CORNER_RADIUS,
			                  y + 2*INTERFACE_ROUND_CORNER_RADIUS,
			                  w-INTERFACE_ROUND_CORNER_RADIUS*2, 2);

			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

			y += INTERFACE_ROUND_CORNER_RADIUS + rectangle.h/2 - interfaceInfo.paddingSize;// + interfaceInfo.paddingSize - rectangle.h/2;//interfaceInfo.clientY - (INTERFACE_THUMBNAIL_SIZE + interfaceInfo.paddingSize - rectangle.h)/2;
			x += interfaceInfo.paddingSize;

			gfx_drawText(DRAWING_SURFACE, pgfx_font, 
			             INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA,
			             x, y, interfaceInfo.currentMenu->name, 0, 0);
			interfaceInfo.currentMenu->name[name_length] = c;
		}
	}
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

	//dprintf("%s: display menu\n", __FUNCTION__);

	if ( interfaceInfo.currentMenu != NULL && interfaceInfo.currentMenu->displayMenu != NULL )
	{
		tprintf(" >>> %s <<<\n", interfaceInfo.currentMenu->name != NULL ? interfaceInfo.currentMenu->name : "[N/A]");
		interfaceInfo.currentMenu->displayMenu(interfaceInfo.currentMenu);
	}

#ifdef ENABLE_MESSAGES
	interface_displayMessageNotify();
#endif
	interface_displayLoading();
	interface_displayNotify();
	interface_displayCustomSliderInMenu();
	if (interfacePlayControl.pDisplay != NULL )
		interfacePlayControl.pDisplay();
	interface_displaySoundControl();
	interface_displaySliderControl();
	interface_displayStatusbar();
	
	if (interfaceInfo.currentMenu != NULL && interfaceInfo.currentMenu->menuType == interfaceMenuList)
	{
#ifdef ENABLE_VIDIMAX
		if ( interfaceInfo.currentMenu->pParentMenu != NULL )
#endif
		interface_displayClock( 
			((interfaceListMenu_t*)interfaceInfo.currentMenu)->listMenuType == interfaceListMenuBigThumbnail );
	}
	interface_displayMessageBox();
	interface_displayVirtualKeypad();
	interface_displayCall();
#ifdef ENABLE_TELETEXT
	if (interfaceInfo.teletext.show)
	{
		teletext_displayTeletext();
	}
#endif

	//dprintf("%s: flip\n", __FUNCTION__);

	if ( flipFB )
	{
#ifdef STB82
		if ( animate == 1 )
		{
			interface_animateSurface();
		} else
#endif
		{
			interface_flipSurface();
		}
	}
	//dprintf("%s: done\n", __FUNCTION__);

#ifdef SCREEN_TRACE
	cur = getCurrentTime();
	eprintf("%s: menu screen updated in %lu sec\n", __FUNCTION__, (unsigned long)(cur-start));
#endif

	mysem_release(interface_semaphore);

	// Set clock refresh event
	time_t rawtime;
	struct tm *cur_time;

	time( &rawtime );
	cur_time = localtime(&rawtime);
	if (cur_time != NULL)
	{
		interface_addEvent(interface_refreshClock, NULL, (61 - cur_time->tm_sec)*1000, 1);
	}
}


static int interface_soundControlSetVisible(void *pArg)
{
	int flag = GET_NUMBER(pArg);

	//dprintf("%s: %d\n", __FUNCTION__, flag);

	interfaceSoundControl.visibleFlag = flag;

	if (interfaceSoundControl.visibleFlag == 0)
	{
		interfaceSoundControl.pCallback(interfaceSoundControlActionVolumeHide, interfaceSoundControl.pArg);
	}

	interface_displayMenu(1);

	return 0;
}


void interface_soundControlRefresh(int redraw)
{
	interfaceSoundControl.visibleFlag = 1;
	interface_addEvent(interface_soundControlSetVisible, (void*)0, 5000, 1);
	if ( redraw )
	{
		//dprintf("%s: display menu\n", __FUNCTION__);
		interface_displayMenu(1);
	}
}

void interface_soundControlSetup(soundControlCallback pAction, void *pArg, long min, long max, long cur)
{
	interfaceSoundControl.pCallback = pAction;
	interfaceSoundControl.pArg = pArg;
	interfaceSoundControl.minValue = min;
	interfaceSoundControl.maxValue = max;
	interfaceSoundControl.curValue = cur;
}

int interface_soundControlProcessCommand(pinterfaceCommandEvent_t cmd)
{
	interfaceSoundControlAction_t action = (interfaceSoundControlAction_t)cmd->command;

	dprintf("%s: in\n", __FUNCTION__);

	switch ( cmd->command )
	{
		case interfaceCommandUp:
		case interfaceCommandVolumeUp:   action = interfaceSoundControlActionVolumeUp;   break;
		case interfaceCommandDown:
		case interfaceCommandVolumeDown: action = interfaceSoundControlActionVolumeDown; break;
		case interfaceCommandVolumeMute: action = interfaceSoundControlActionVolumeMute; break;
		case interfaceCommandExit: interfaceSoundControl.visibleFlag = 0; return 1;
		default:;
	}

	if ( interfaceSoundControl.pCallback != NULL )
	{
		//dprintf("%s: callback\n", __FUNCTION__);
		interfaceSoundControl.pCallback(action, interfaceSoundControl.pArg);
	}

	return 0;
}

int interface_slideshowControlProcessCommand(pinterfaceCommandEvent_t cmd)
{
	int ret = 0;

	dprintf("%s: in\n", __FUNCTION__);

	/* Processed only when inputFocus == inputFocusSlideshow ! */
	if ( interfaceSlideshowControl.enabled == 0 || interfaceInfo.inputFocus != inputFocusSlideshow )
		return 1;

	switch ( cmd->command )
	{
		case interfaceCommandExit:
			if (interfacePlayControl.visibleFlag || interfaceSlideshowControl.visibleFlag)
			{
				interfacePlayControl.visibleFlag = 0;
				interfaceSlideshowControl.visibleFlag = 0;
				interface_displayMenu(1);
				return 0;
			} else
				ret = 1;
			break;
		case interfaceCommandEnter:
		case interfaceCommandOk:
			if ( interfaceSlideshowControl.visibleFlag == 0 && interfaceSlideshowControl.visibleFlag == 0)
			{
				interface_showMenu(!interfaceInfo.showMenu, 1);
				return 0;
			} else
			{
				switch (interfaceSlideshowControl.highlightedButton)
				{
					case interfacePlayControlNext:
						media_slideshowNext(0);
						break;
					case interfacePlayControlPrevious:
						media_slideshowNext(1);
						break;
					case interfacePlayControlTimeout:
						media_slideshowSetTimeout(VALUE_NEXT);
						break;
					default: /* interfacePlayControlMode */
						if ( appControlInfo.slideshowInfo.state == slideshowRandom )
						{
							media_slideshowStop(0);
						}
						else
						{
							media_slideshowSetMode(VALUE_NEXT);
							media_slideshowStart();
						}
				}
			}
			break;
		case interfaceCommandUp:
			if ( appControlInfo.slideshowInfo.state > 0 )
			{
				switch (interfaceSlideshowControl.highlightedButton)
				{
					case interfacePlayControlPrevious:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlNext;
						break;
					case interfacePlayControlMode:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlPrevious;
						break;
					case interfacePlayControlNext:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlTimeout;
						break;
					default: // interfacePlayControlTimeout:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlMode;
				}
			} else
				ret = 1;
			break;
		case interfaceCommandDown:
			if ( appControlInfo.slideshowInfo.state > 0 )
			{
				switch (interfaceSlideshowControl.highlightedButton)
				{
					case interfacePlayControlMode:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlTimeout;
						break;
					case interfacePlayControlTimeout:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlNext;
						break;
					case interfacePlayControlNext:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlPrevious;
						break;
					default: //interfacePlayControlPrevious:
						interfaceSlideshowControl.highlightedButton = interfacePlayControlMode;
				}
			} else
				ret = 1;
			break;
		case interfaceCommandPageUp:
		case interfaceCommandPageDown:
			ret = media_slideshowNext( cmd->command == interfaceCommandPageUp );
			if ( ret == 0 && !interfacePlayControl.showOnStart )
				return 0;
			break;
		case interfaceCommandChannelUp:
		case interfaceCommandChannelDown:
		case interfaceCommandPrevious:
		case interfaceCommandRewind:
		case interfaceCommandStop:
		case interfaceCommandPause:
		case interfaceCommandPlay:
		case interfaceCommandFastForward:
		case interfaceCommandNext:
			if ( !interfacePlayControl.enabled )
			{
				switch ( cmd->command )
				{
					case interfaceCommandRewind:
						if ( appControlInfo.slideshowInfo.state > slideshowImage )
						{
							media_slideshowSetTimeout(VALUE_PREV);
							break;
						}
					case interfaceCommandChannelDown:
					case interfaceCommandPrevious:
						ret = media_slideshowNext(1);
						if ( ret == 0 && !interfacePlayControl.showOnStart )
							return 0;
						break;
					case interfaceCommandStop:
						media_slideshowStop(0);
						break;
					case interfaceCommandPause:
						media_slideshowSetMode(slideshowImage);
						break;
					case interfaceCommandPlay:
						media_slideshowStart();
						break;
					case interfaceCommandFastForward:
						if ( appControlInfo.slideshowInfo.state > slideshowImage )
						{
							media_slideshowSetTimeout(VALUE_NEXT);
							break;
						}
					case interfaceCommandChannelUp:
					case interfaceCommandNext:
						ret = media_slideshowNext(0);
						if ( ret == 0 && !interfacePlayControl.showOnStart )
							return 0;
						break;
					default:
						ret = 1;
				}
			} else
				ret = 1;
			break;
		case interfaceCommandRefresh:
			if ( interfacePlayControl.enabled )
			{
				interface_playControlSetInputFocus(inputFocusPlayControl);
				break;
			} else
				ret = 1;
		default:
			ret = 1;
	}

	//dprintf("%s: out\n", __FUNCTION__);

	/* Any pressed button should make play control visible.
	 * If play control is disabled, we should make it here.
	 */
	if ( ret == 0 || !interfacePlayControl.enabled )
	{
		interface_playControlRefresh(1);
	}

	return ret;
}

#ifdef ENABLE_MULTI_VIEW
int interface_multiviewProcessCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch ( cmd->command )
	{
		case interfaceCommandPrevious:
		case interfaceCommandPageUp:
			if ( interfacePlayControl.pChannelChange )
				interfacePlayControl.pChannelChange(1, pArg);
			return 0;
		case interfaceCommandNext:
		case interfaceCommandPageDown:
			if ( interfacePlayControl.pChannelChange )
				interfacePlayControl.pChannelChange(0, pArg);
			return 0;
		case interfaceCommandUp:
		case interfaceCommandDown:
			do
			{
				appControlInfo.multiviewInfo.selected = (appControlInfo.multiviewInfo.selected + 2)%4;
			} while ( appControlInfo.multiviewInfo.selected >= appControlInfo.multiviewInfo.count);
			interface_displayMenu(1);
			return 0;
		case interfaceCommandLeft:
			appControlInfo.multiviewInfo.selected =
				(appControlInfo.multiviewInfo.selected + appControlInfo.multiviewInfo.count -1)%appControlInfo.multiviewInfo.count;
			interface_displayMenu(1);
			return 0;
		case interfaceCommandRight:
			appControlInfo.multiviewInfo.selected = (appControlInfo.multiviewInfo.selected + 1)%appControlInfo.multiviewInfo.count;
			interface_displayMenu(1);
			return 0;
		case interfaceCommandRecord: // ignore
			return 0;
		default:
			return 1;
	}
	return 1;
}
#endif

static int interface_audioChange(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int which = screenMain;
	int selected = GET_NUMBER(pArg);
	char buf[MENU_ENTRY_INFO_LENGTH];
	char *str;
	int audioCount, i, current;

	audioCount = gfx_getVideoProviderAudioCount(which);

	if ( audioCount <= 1 )
		return 0;

	if (cmd != NULL)
	{
		switch (cmd->command)
		{
			case interfaceCommandExit:
			case interfaceCommandRed:
			case interfaceCommandLeft:
				return 0;
			case interfaceCommandEnter:
			case interfaceCommandOk:
			case interfaceCommandGreen:
				if ( gfx_setVideoProviderAudioStream(which, selected) == 0 )
				{
					if (interfacePlayControl.pAudioChange != NULL )
						interfacePlayControl.pAudioChange( SET_NUMBER(selected) );
				} else
				{
					eprintf("RTP: Can't set audio stream to %d\n", selected);
				}
				return 0;
			case interfaceCommandDown:
				selected = (selected + 1) % audioCount;
				break;
			case interfaceCommandUp:
				selected = (selected + audioCount - 1) % audioCount;
				break;
			default:; // ignore
		}
	} else
	{
		selected = -1;
	}

	str = buf;
	current = gfx_getVideoProviderAudioStream(which);
	for ( i = 0; i < audioCount; i++ )
	{
		if ( selected == i || (selected < 0 && i == current ) )
		{
			sprintf(str, "> Audio %d <\n", i);
			selected = i;
		} else
		{
			sprintf(str, "  Audio %d  \n", i);
		}
		str = &str[strlen(str)];
	}
	buf[strlen(buf)-1] = 0;
	interface_showConfirmationBox(buf, -1, interface_audioChange, SET_NUMBER(selected));
	return 1;
}

int interface_playControlProcessCommand(pinterfaceCommandEvent_t cmd)
{
	int n;
	int res = 0;

	dprintf("%s: in\n", __FUNCTION__);

	//eprintf("%s: * source %d inputFocus  %d enablePlay %d enableSlide %d visiblePlay %d visibleSlide %d playHighlighted %d\n", __FUNCTION__, cmd->source, interfaceInfo.inputFocus, interfacePlayControl.enabled, interfaceSlideshowControl.enabled, interfacePlayControl.visibleFlag, interfaceSlideshowControl.visibleFlag, interfaceSlideshowControl.highlightedButton);

	if ( interface_slideshowControlProcessCommand(cmd) == 0 )
	{
		return 0;
	}
	
	if ( interfacePlayControl.enabled == 0 )
	{
		eprintf("%s: interfacePlayControl.enabled == 0. Return.\n", __FUNCTION__);
		return 1;
	}

	if ( interfacePlayControl.pProcessCommand != NULL )
	{
		dprintf("%s: pProcessCommand... %d\n", __FUNCTION__, cmd->command);
		if ( interfacePlayControl.pProcessCommand( cmd, interfacePlayControl.pArg) == 0 )
		{
			dprintf("%s: break\n", __FUNCTION__);
			return 0;
		}
	}

	/* Default play control command processing */
	switch ( cmd->command )
	{
		case interfaceCommandExit:
#ifdef ENABLE_TELETEXT
			if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
			{
				interfaceInfo.teletext.show=0;
			}
#endif
			if (interfaceInfo.inputFocus == inputFocusSlider || interfaceInfo.inputFocus == inputFocusSliderMoving )
			{
				interface_playControlSetInputFocus(inputFocusPlayControl);
				interfacePlayControl.sliderPointer = interfacePlayControl.sliderPos;
			} else if (interfacePlayControl.visibleFlag || interfaceSlideshowControl.visibleFlag)
			{
				interfacePlayControl.visibleFlag = 0;
				interfaceSlideshowControl.visibleFlag = 0;
			} else if (interfacePlayControl.showState)
			{
				interfacePlayControl.showState = 0;
			}
			interface_displayMenu(1);
			res = 1;
			break;
		case interfaceCommandLeft:
			if (( interfaceInfo.inputFocus == inputFocusSlideshow )
#ifdef ENABLE_TELETEXT
				|| (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
#endif
			)
			{
				break;
			}
			if (interfaceInfo.inputFocus == inputFocusSlider || interfaceInfo.inputFocus == inputFocusSliderMoving )
			{
				float step = interfacePlayControl.sliderEnd*(cmd->repeat+1)/50;
				if (step > interfacePlayControl.sliderEnd/10)
				{
					step = interfacePlayControl.sliderEnd/10;
				}
				if (interfacePlayControl.sliderPointer >= step)
				{
					interfacePlayControl.sliderPointer -= step;
				} else
				{
					interfacePlayControl.sliderPointer = 0.0;
				}
			} else
			{
				n = interfacePlayControl.highlightedButton;
				do
				{
					switch ( n )
					{
						case interfacePlayControlPrevious: n = interfacePlayControlAddToPlaylist; break;
						case interfacePlayControlRewind: n = interfacePlayControlPrevious; break;
						case interfacePlayControlPlay: n = interfacePlayControlRewind; break;
						case interfacePlayControlPause: n = interfacePlayControlPlay; break;
						case interfacePlayControlStop: n = interfacePlayControlPause; break;
						case interfacePlayControlFastForward: n = interfacePlayControlStop; break;
						case interfacePlayControlNext: n = interfacePlayControlFastForward; break;
#ifdef ENABLE_PVR
						case interfacePlayControlRecord: n = interfacePlayControlNext; break;
						case interfacePlayControlMode: n = interfacePlayControlRecord; break;
#else
						case interfacePlayControlMode: n = interfacePlayControlNext; break;
#endif
						case interfacePlayControlAddToPlaylist: n = interfacePlayControlMode; break;
						default: n = interfacePlayControl.highlightedButton;
					}
					//dprintf("%s: initial = %d, n = %d\n", __FUNCTION__, interfacePlayControl.highlightedButton, n);
				} while ( n != (int)interfacePlayControl.highlightedButton && !(n & (int)interfacePlayControl.enabledButtons) );
	
				if ( n & interfacePlayControl.enabledButtons )
				{
					interfacePlayControl.highlightedButton = n;
				}
			}
			break;
		case interfaceCommandRight:
			if (( interfaceInfo.inputFocus == inputFocusSlideshow )
#ifdef ENABLE_TELETEXT
				|| (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
#endif
			)
			{
				break;
			}
			if (interfaceInfo.inputFocus == inputFocusSlider || interfaceInfo.inputFocus == inputFocusSliderMoving )
			{
				float step = interfacePlayControl.sliderEnd*(cmd->repeat+1)/50;
				if (step > interfacePlayControl.sliderEnd/10)
				{
					step = interfacePlayControl.sliderEnd/10;
				}
				interfacePlayControl.sliderPointer += step;
	
				if (interfacePlayControl.sliderPointer > interfacePlayControl.sliderEnd)
				{
					interfacePlayControl.sliderPointer = interfacePlayControl.sliderEnd;
				}
	
			} else
			{
				n = interfacePlayControl.highlightedButton;
				do
				{
					switch ( n )
					{
						case interfacePlayControlPrevious: n = interfacePlayControlRewind; break;
						case interfacePlayControlRewind: n = interfacePlayControlPlay; break;
						case interfacePlayControlPlay: n = interfacePlayControlPause; break;
						case interfacePlayControlPause: n = interfacePlayControlStop; break;
						case interfacePlayControlStop: n = interfacePlayControlFastForward; break;
						case interfacePlayControlFastForward: n = interfacePlayControlNext; break;
#ifdef ENABLE_PVR
						case interfacePlayControlNext: n = interfacePlayControlRecord; break;
						case interfacePlayControlRecord: n = interfacePlayControlMode; break;
#else
						case interfacePlayControlNext: n = interfacePlayControlMode; break;
#endif
						case interfacePlayControlMode: n = interfacePlayControlAddToPlaylist; break;
						case interfacePlayControlAddToPlaylist: n = interfacePlayControlPrevious; break;
						default: n = interfacePlayControl.highlightedButton;
					}
					//dprintf("%s: initial = %d, n = %d\n", __FUNCTION__, interfacePlayControl.highlightedButton, n);
				} while ( n != (int)interfacePlayControl.highlightedButton && !(n & (int)interfacePlayControl.enabledButtons) );
	
				if ( n & interfacePlayControl.enabledButtons )
				{
					interfacePlayControl.highlightedButton = n;
				}
			}
			break;
		case interfaceCommandEnter:
		case interfaceCommandOk:
			if (( interfaceInfo.inputFocus == inputFocusSlideshow )
#ifdef ENABLE_TELETEXT
				|| (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
#endif
			)
			{
				break;
			}
			if (interfaceInfo.inputFocus == inputFocusSlider)
			{
				if ( interfacePlayControl.pCallback != NULL )
				{
					res = interfacePlayControl.pCallback(interfacePlayControlSetPosition, interfacePlayControl.pArg);
					interfaceInfo.inputFocus = inputFocusSlider;
				}
			} else
			{
				/* If control is invisible, treat OK button as Toggle Menu */
				if ( interfacePlayControl.visibleFlag == 0 && interfaceSlideshowControl.visibleFlag == 0)
				{
					interface_showMenu(!interfaceInfo.showMenu, 1);
					res = 1; // don't refresh play control
				} else
				{

					switch (interfacePlayControl.highlightedButton)
					{
						case interfacePlayControlMode:
							media_setMode((interfaceMenu_t*)&MediaMenu,NULL);
							break;
#ifdef ENABLE_PVR
						case interfacePlayControlRecord:
							{
								int handled = 0;
								if ( interfacePlayControl.pProcessCommand != NULL )
								{
									interfaceCommandEvent_t new_cmd;
									new_cmd.command = interfaceCommandRecord;
									handled = (0 == interfacePlayControl.pProcessCommand(&new_cmd, interfacePlayControl.pArg));
								}
								if (!handled)
								{
									pvr_toggleRecording();
								}
							}
							break;
#endif
						default:

						if ( interfacePlayControl.pCallback != NULL )
						{
							res = interfacePlayControl.pCallback(interfacePlayControl.highlightedButton, interfacePlayControl.pArg);
						}
					}
				}
			}
			break;
		case interfaceCommandUp:
#ifdef ENABLE_TELETEXT
			if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
			{
				break;
			}
#endif
			if ( interfacePlayControl.visibleFlag )
			{
				switch (interfaceInfo.inputFocus)
				{
					case inputFocusSlider:
					case inputFocusSliderMoving:
						interface_playControlSetInputFocus(inputFocusPlayControl);
						break;
					case inputFocusSlideshow:
						break;
					default: /* inputFocusPlayControl */
						if (interfacePlayControl.sliderEnd > 0.0)
						{
							interface_playControlSetInputFocus(inputFocusSlider);
						} else
						{
							res = 1;
						}
					break;
				} // switch (interfaceInfo.inputFocus)
			} else if ( cmd->source == DID_FRONTPANEL ) // enablePlayControl==1 && interfacePlayControl.visibleFlag == 0
			{
				interface_soundControlProcessCommand(cmd);
				return 0;
			}
			break;
		case interfaceCommandDown:
#ifdef ENABLE_TELETEXT
			if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
			{
				break;
			}
#endif
			if ( interfacePlayControl.visibleFlag )
			{
				switch (interfaceInfo.inputFocus)
				{
					case inputFocusSlider:
					case inputFocusSliderMoving:
						interface_playControlSetInputFocus(inputFocusPlayControl);
						break;
					case inputFocusSlideshow:
						break;
					default: /* inputFocusPlayControl */
						if (interfacePlayControl.sliderEnd > 0.0)
						{
							interface_playControlSetInputFocus(inputFocusSlider);
						} else
						{
							res = 1;
						}
					break;
				} // switch (interfaceInfo.inputFocus)
			} else if ( cmd->source == DID_FRONTPANEL ) // enablePlayControl==1 && interfacePlayControl.visibleFlag == 0
			{
				interface_soundControlProcessCommand(cmd);
				return 0;
			}
			break;
		case interfaceCommandMainMenu:
		case interfaceCommandBack:
#ifdef ENABLE_TELETEXT
			if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
			{
				interfaceInfo.teletext.show=0;
			}
#endif
			interface_showMenu(!interfaceInfo.showMenu, 1);
			res = 1;
			break;
		case interfaceCommandChannelUp:
		case interfaceCommandChannelDown:
#ifdef ENABLE_TELETEXT
			if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
			{
				interfaceInfo.teletext.show=0;
			}
#endif
			if (interfacePlayControl.pChannelChange != NULL)
			{
				interfacePlayControl.pChannelChange(cmd->command == interfaceCommandChannelDown, interfacePlayControl.pArg);
				res = 1;
			}
			else if ( interfaceSlideshowControl.enabled )
			{
				res = media_slideshowNext( cmd->command == interfaceCommandChannelDown );
			}
			break;
		case interfaceCommandPrevious:
		case interfaceCommandRewind:
		case interfaceCommandStop:
		case interfaceCommandPause:
		case interfaceCommandPlay:
		case interfaceCommandFastForward:
		case interfaceCommandNext:
			if ( interfacePlayControl.enabled )
			{
				switch ( cmd->command )
				{
					case interfaceCommandPrevious: n = interfacePlayControlPrevious; break;
					case interfaceCommandRewind:
						if ( interfacePlayControl.sliderEnd > 0.0 )
						{
							interface_playControlSetInputFocus(inputFocusSlider);
							interfacePlayControl.visibleFlag = 1;
							interfaceSlideshowControl.visibleFlag = 1;
						}
						n = interfacePlayControlRewind;
						break;
					case interfaceCommandStop:
						res = media_slideshowStop(0);
						n = interfacePlayControlStop;
						break;
					case interfaceCommandPause: n = interfacePlayControlPause; break;
					case interfaceCommandPlay: n = interfacePlayControlPlay; break;
					case interfaceCommandFastForward:
						if ( interfacePlayControl.sliderEnd > 0.0 )
						{
							interface_playControlSetInputFocus(inputFocusSlider);
							interfacePlayControl.visibleFlag = 1;
							interfaceSlideshowControl.visibleFlag = 1;
						}
						n = interfacePlayControlFastForward;
						break;
					case interfaceCommandNext: n = interfacePlayControlNext; break;
					default: n = -1;
				}
	
				if ( n != -1 && n & interfacePlayControl.enabledButtons )
				{
					interfacePlayControl.highlightedButton = n;
					if ( interfacePlayControl.pCallback != NULL )
					{
						res = interfacePlayControl.pCallback(interfacePlayControl.highlightedButton, interfacePlayControl.pArg);
					}
				}
			}
			break;
		case interfaceCommand0:
		case interfaceCommand1:
		case interfaceCommand2:
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand5:
		case interfaceCommand6:
		case interfaceCommand7:
		case interfaceCommand8:
		case interfaceCommand9:
#ifdef ENABLE_TELETEXT
			if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
			{
				break;
			}
#endif
			if (interfaceChannelControl.pSet != NULL )
			{
				interface_removeEvent(interface_channelNumberReset,NULL);
				interfaceChannelControl.number[interfaceChannelControl.length] = '0' + cmd->command - interfaceCommand0;
				interfaceChannelControl.length++;
				interfaceChannelControl.number[interfaceChannelControl.length] = 0;
				interfaceChannelControl.showingLength = interfaceChannelControl.length;
				if ( interfaceChannelControl.length > ( MAX_MEMORIZED_SERVICES > 100 ? 2 : 1 ) )
				{
					interface_channelNumberReset(NULL);
				} else {
					interface_addEvent(interface_channelNumberReset, NULL, INTERFACE_CHANNEL_CONTROL_TIMEOUT, 1);
				}
				if ( !interfacePlayControl.showOnStart )
				{
					res = 1; // don't display playcontrol
					interface_displayMenu(1);
				}
			} else
			{
				res = 1;
			}
			break;
		case interfaceCommandRefresh:
			if ( interfaceSlideshowControl.enabled )
			{
				interface_playControlSetInputFocus(inputFocusSlideshow);
			} else
				res = 1;
			break;
/*		case interfaceCommandRed:
		case interfaceCommandGreen:
		case interfaceCommandYellow:
		case interfaceCommandBlue:*/
#ifdef ENABLE_PVR
		case interfaceCommandRecord:
			pvr_toggleRecording();
			break;
#endif
		case interfaceCommandTV:
			interface_enterChannelList();
			break;
		case interfaceCommandPageUp:
		case interfaceCommandPageDown:
			if ( interfaceSlideshowControl.enabled )
			{
				if ( media_slideshowNext( cmd->command == interfaceCommandPageUp ) == 0 && !interfacePlayControl.showOnStart )
					return 0;
			}
			// fall through
		default:
			if (
#ifdef ENABLE_TELETEXT
				(!(appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))) &&
#endif
				interfacePlayControl.enabled && interfacePlayControl.pCallback != NULL )
			{
				interfacePlayControlButton_t play_cmd;
				switch (cmd->command)
				{
					case interfaceCommandBlue:       play_cmd = interfacePlayControlMode; break;
					case interfaceCommandGreen:      play_cmd = interfacePlayControlAudioTracks; break;
					case interfaceCommandYellow:     play_cmd = interfacePlayControlAddToPlaylist; break;
					case interfaceCommandRed:        play_cmd = interfacePlayControlInfo; break;
					default: play_cmd = (interfacePlayControlButton_t)cmd->command;
				}
				res = interfacePlayControl.pCallback(play_cmd, interfacePlayControl.pArg);
				if ( res != 0 )
				{
					switch ( play_cmd )
					{
						case interfacePlayControlAudioTracks:
							if (gfx_getVideoProviderAudioCount(screenMain) > 1)
								interface_audioChange(interfaceInfo.currentMenu, NULL, SET_NUMBER(gfx_getVideoProviderAudioStream(screenMain)));
							break;
						default:;
					}
				}
			}
	}

	//dprintf("%s: process command\n", __FUNCTION__);

	if (interfaceInfo.inputFocus == inputFocusSlider || interfaceInfo.inputFocus == inputFocusSliderMoving)
	{
		interfacePlayControl.visibleFlag = 1;
		interfaceSlideshowControl.visibleFlag = 1;
		/* bug 9051
		interface_removeEvent(interface_playControlSetVisible, (void*)0);*/
		interface_addEvent(interface_playControlSetVisible, (void*)0, 1000*PLAYCONTROL_TIMEOUT_SLIDER, 1);
		interface_displayMenu(1);
	} else if ( (interfacePlayControl.enabled | interfaceSlideshowControl.enabled) && res == 0 )
	{
		interface_playControlRefresh(1);
	}

	return 0;
}

pinterfaceCommandEvent_t interface_keypadProcessCommand(pinterfaceCommandEvent_t cmd, pinterfaceCommandEvent_t outcmd)
{
	int maxKeysInRow = 0;
	int maxRows = 0;
	int row = 0;
	int cell = 0;
	int controlButtonCount = (int)(sizeof(controlButtons)/sizeof(button_t));

	dprintf("%s: in\n", __FUNCTION__);

	outcmd->command = interfaceCommandNone;
	outcmd->original = interfaceCommandNone;
	outcmd->repeat = 0;
	outcmd->source = DID_KEYBOARD;

	while (keypad[row][cell] != 0)
	{
		while (keypad[row][cell] != 0)
		{
			cell++;
		}
		if (maxKeysInRow < cell)
		{
			maxKeysInRow = cell;
		}
		cell = 0;
		row++;
	}
	maxRows = row;

	switch (cmd->original)
	{
		case interfaceCommandExit:
			interfaceInfo.keypad.enable = 0;
			break;
		case interfaceCommandRed:
			interfaceInfo.keypad.shift = 1-interfaceInfo.keypad.shift;
			break;
		case interfaceCommandEnter:
		case interfaceCommandOk:
			if ( interfaceInfo.keypad.row < maxRows )
			{
#ifdef WCHAR_SUPPORT
				if ( interfaceInfo.keypad.altLayout == ALTLAYOUT_ON && 
				     keypad_local[interfaceInfo.keypad.row][interfaceInfo.keypad.cell] != 0 )
					outcmd->command = outcmd->original = interfaceInfo.keypad.shift ?
					                                     wuc(keypad_local[interfaceInfo.keypad.row][interfaceInfo.keypad.cell]) :
					                                     wlc(keypad_local[interfaceInfo.keypad.row][interfaceInfo.keypad.cell]);
				else
					outcmd->command = outcmd->original = interfaceInfo.keypad.shift ?
					                                     wuc(keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell]) :
					                                     wlc(keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell]);
#else
				outcmd->command = outcmd->original = interfaceInfo.keypad.shift ?
				                                     toupper(keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell]) :
				                                     tolower(keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell]);
#endif
			} else if ( interfaceInfo.keypad.cell >= 0 && interfaceInfo.keypad.cell < controlButtonCount )
			{
				interfaceCommandEvent_t mycmd;
				mycmd.command = mycmd.original = controlButtons[ interfaceInfo.keypad.cell ].command;
				mycmd.repeat = 0;
				mycmd.source = DID_KEYBOARD;
				return interface_keypadProcessCommand(&mycmd, outcmd);
			}
			break;
		case  interfaceCommandUp:
			interfaceInfo.keypad.row--;
			if (interfaceInfo.keypad.row < 0)
			{
				interfaceInfo.keypad.row = maxRows;
				if (interfaceInfo.keypad.cell >= controlButtonCount )
					interfaceInfo.keypad.cell = 0;
				break;
			}
			if (keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell] == 0)
			{
				interfaceInfo.keypad.cell = 0;
				while (keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell+1] != 0)
				{
					interfaceInfo.keypad.cell++;
				}
			}
			break;
		case  interfaceCommandDown:
			interfaceInfo.keypad.row++;
			if (interfaceInfo.keypad.row > maxRows)
			{
				interfaceInfo.keypad.row = 0;
				break;
			}
			if ( interfaceInfo.keypad.row == maxRows )
			{
				if ( interfaceInfo.keypad.cell >= controlButtonCount )
					interfaceInfo.keypad.cell = 0;
				break;
			}
			if (keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell] == 0)
			{
				interfaceInfo.keypad.cell = 0;
				while (keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell+1] != 0)
				{
					interfaceInfo.keypad.cell++;
				}
			}
			break;
		case interfaceCommandLeft:
			interfaceInfo.keypad.cell--;
			if (interfaceInfo.keypad.cell < 0)
			{
				interfaceInfo.keypad.row--;
				if (interfaceInfo.keypad.row < 0)
				{
					//interfaceInfo.keypad.row = 0; // Stay on the first row with digits...
					interfaceInfo.keypad.row = maxRows;
					interfaceInfo.keypad.cell = controlButtonCount-1;
					break;
				}
				interfaceInfo.keypad.cell = 0;
				while (keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell+1] != 0)
				{
					interfaceInfo.keypad.cell++;
				}
			}
			break;
		case interfaceCommandRight:
			interfaceInfo.keypad.cell++;
			if (interfaceInfo.keypad.row == maxRows)
			{
				if ( interfaceInfo.keypad.cell >= controlButtonCount )
				{
					interfaceInfo.keypad.row = 0;
					interfaceInfo.keypad.cell = 0;
				}
				break;
			}
			if (keypad[interfaceInfo.keypad.row][interfaceInfo.keypad.cell] == 0)
			{
				interfaceInfo.keypad.row++;
				interfaceInfo.keypad.cell = 0;
			}
			break;
		case 8:
		case interfaceCommandBlue:
			outcmd->command = cmd->command;
			break;
#if (defined STB225)
		case interfaceCommandBack:
			outcmd->command = 8;
			break;
#endif
		case interfaceCommandGreen:
			outcmd->command = interfaceCommandLeft;
			break;
		case interfaceCommandYellow:
			outcmd->command = interfaceCommandRight;
			break;
		case interfaceCommand0:
		case interfaceCommand1:
		case interfaceCommand2:
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand5:
		case interfaceCommand6:
		case interfaceCommand7:
		case interfaceCommand8:
		case interfaceCommand9:
			outcmd->command = cmd->original;
			interfaceInfo.keypad.cell = cmd->original == interfaceCommand0 ? 9 : cmd->original-interfaceCommand1;
			interfaceInfo.keypad.row = 0;
			break;
#ifdef WCHAR_SUPPORT
		case interfaceCommandRefresh:
			if ( interfaceInfo.keypad.altLayout != ALTLAYOUT_DISABLED )
				interfaceInfo.keypad.altLayout = 1-interfaceInfo.keypad.altLayout;
			break;
#endif
		default:
			if (cmd->command >= (interfaceCommand_t)DIKS_SPACE && 
				cmd->command <= (interfaceCommand_t)DIKS_TILDE ) // see directfb_keyboard.h
				outcmd->command = cmd->original;
			//ignore
	}

	if (outcmd->command == interfaceCommandNone)
	{
		interface_displayMenu(1);
	}

	return outcmd;
}

static void interface_enterChannelList()
{
#ifdef ENABLE_DVB
#ifdef ENABLE_TELETEXT
	if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
	{
		interfaceInfo.teletext.show=0;
	}
#endif
	if ( appControlInfo.playbackInfo.streamSource != streamSourceIPTV && offair_tunerPresent() )
	{
		if ( dvb_getNumberOfServices() > 0 )
		{
			interface_showMenu(1, 0);
			interface_menuActionShowMenu(interfaceInfo.currentMenu,(void*)&DVBTMenu);
		} else
		{
			output_fillDVBMenu((interfaceMenu_t *)&DVBSubMenu, NULL);
			interface_menuActionShowMenu(interfaceInfo.currentMenu,(void*)&DVBSubMenu);
		}
	} else
#endif /* ENABLE_DVB */
	{
#ifdef ENABLE_IPTV
		rtp_initStreamMenu(interfaceInfo.currentMenu,(void*)screenMain);
		interface_showMenu(1, 1);
#endif
	}
}

void interface_processCommand(pinterfaceCommandEvent_t cmd)
{
	interfaceCommandEvent_t mycmd;
	dprintf("%s: in %d\n", __FUNCTION__, cmd->command);

#ifdef ENABLE_VIDIMAX
	if (appControlInfo.vidimaxInfo.active){
		//eprintf("%s: redirecting to vidimax command process.\n", __FUNCTION__);
		interfaceInfo.currentMenu->processCommand(interfaceInfo.currentMenu, cmd);
		return;
	}
#endif

	if (interfaceInfo.keypad.enable)
	{
		cmd = interface_keypadProcessCommand(cmd, &mycmd);
		if (cmd->command == interfaceCommandNone)
		{
			return;
		}
	}

#ifdef ENABLE_REGPLAT
	if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatPaymentMenu)
	{
		if (regplatPaymentMenu.baseMenu.selectedItem >=0 && 
		    regplatPaymentMenu.baseMenu.selectedItem != regplatPaymentMenu.baseMenu.menuEntryCount-1)
		{
			interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t*)
				regplatPaymentMenu.baseMenu.menuEntry[regplatPaymentMenu.baseMenu.selectedItem].pArg;
			if (pEditEntry->active != 0)
			{
				int r = interfaceInfo.currentMenu->processCommand(interfaceInfo.currentMenu, cmd);
				if (r == 0)
					return;
			}
		}
	} else if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatLoginMenu)
	{
		if (regplatLoginMenu.baseMenu.selectedItem == 0)
		{
			interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t*)regplatLoginMenu.baseMenu.menuEntry[0].pArg;
			if (pEditEntry->active != 0)
			{
				int r = interfaceInfo.currentMenu->processCommand(interfaceInfo.currentMenu, cmd);
				if (r == 0)
					return;
			}
		}
	} else if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatRegistrationMenu)
	{
		if (regplatRegistrationMenu.baseMenu.selectedItem >=0 && 
		    regplatRegistrationMenu.baseMenu.selectedItem != regplatRegistrationMenu.baseMenu.menuEntryCount-1)
		{
			interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t*)
				regplatRegistrationMenu.baseMenu.menuEntry[regplatRegistrationMenu.baseMenu.selectedItem].pArg;
			if (pEditEntry->active != 0)
			{
				int r = interfaceInfo.currentMenu->processCommand(interfaceInfo.currentMenu, cmd);
				if (r == 0)
					return;
			}
		}
	}
#endif // ENABLE_REGPLAT

#if (defined STB225)
//printf("%s[%d]: cmd->command=%d\n", __FILE__, __LINE__, cmd->command);
	if (((cmd->command == DIKS_RADIO) ||
	     (cmd->command == DIKS_ZOOM)) &&
	    interfaceInfo.enable3d )
	{
		dprintf("%s[%d]: *****DIKS_RADIO mode3d = %d *****\n", __FILE__, __LINE__, interfaceInfo.mode3D);

		// disable flat 3D mode that equals 2
		if (interfaceInfo.mode3D || appControlInfo.outputInfo.has_3D_TV==0)
		{
			Stb225ChangeDestRect("/dev/fb0", 0, 0, 1920, 1080);
//			Stb225ChangeDestRect("/dev/fb0", 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenHeight);
		}
		else
		{
			Stb225ChangeDestRect("/dev/fb0", 0, 0, 960, 1080);
//			Stb225ChangeDestRect("/dev/fb0", 0, 0, interfaceInfo.screenWidth/2, interfaceInfo.screenHeight);
		}
		interfaceInfo.mode3D++;
		interfaceInfo.mode3D&=1;
		interface_displayMenu(1);
	} else
#endif
	if ( interfaceInfo.messageBox.type == interfaceMessageBoxCallback )
	{
		if (cmd->command == interfaceCommandYellow && cmd != &mycmd && appControlInfo.inputMode == inputModeABC)
		{
			interfaceInfo.keypad.enable = !interfaceInfo.keypad.enable;
			interfaceInfo.keypad.shift = 0;
			interface_displayMenu(1);
		} else if (interfaceInfo.messageBox.pCallback == NULL ||
			interfaceInfo.messageBox.pCallback(interfaceInfo.currentMenu, cmd, interfaceInfo.messageBox.pArg) == 0)
		{
			interface_hideMessageBox();
		}
	} else 
	if ( interfaceInfo.messageBox.type == interfaceMessageBoxScrolling ||
	     interfaceInfo.messageBox.type == interfaceMessageBoxPoster)
	{
		if (interfaceInfo.messageBox.pCallback == NULL ||
			interfaceInfo.messageBox.pCallback(interfaceInfo.currentMenu, cmd, interfaceInfo.messageBox.pArg) != 0)
		{
			switch ( cmd->command )
			{
				case interfaceCommandUp:
					if ( interfaceInfo.messageBox.scrolling.offset > 0 )
					{
						interfaceInfo.messageBox.scrolling.offset--;
						interface_displayMenu(1);
					}
					break;
				case interfaceCommandDown:
					if ( interfaceInfo.messageBox.scrolling.offset + interfaceInfo.messageBox.scrolling.visibleLines < interfaceInfo.messageBox.scrolling.lineCount )
					{
						interfaceInfo.messageBox.scrolling.offset++;
						interface_displayMenu(1);
					}
					break;
				case interfaceCommandPageUp:
					if ( interfaceInfo.messageBox.scrolling.offset > 0 )
					{
						interfaceInfo.messageBox.scrolling.offset -= interfaceInfo.messageBox.scrolling.visibleLines - 1;
						if (interfaceInfo.messageBox.scrolling.offset < 0 )
						{
							interfaceInfo.messageBox.scrolling.offset = 0;
						}
						interface_displayMenu(1);
					}
					break;
				case interfaceCommandPageDown:
					if ( interfaceInfo.messageBox.scrolling.offset + interfaceInfo.messageBox.scrolling.visibleLines < interfaceInfo.messageBox.scrolling.lineCount )
					{
						interfaceInfo.messageBox.scrolling.offset += interfaceInfo.messageBox.scrolling.visibleLines - 1;
						if (interfaceInfo.messageBox.scrolling.offset + interfaceInfo.messageBox.scrolling.visibleLines >= interfaceInfo.messageBox.scrolling.lineCount )
						{
							interfaceInfo.messageBox.scrolling.offset = interfaceInfo.messageBox.scrolling.lineCount - interfaceInfo.messageBox.scrolling.visibleLines;
						}
						interface_displayMenu(1);
					}
					break;
				default:
					interface_hideMessageBox();
			}
		}
	} else if ( interfaceInfo.messageBox.type != interfaceMessageBoxNone )
	{
		interface_hideMessageBox();
	} else if (interfaceInfo.showSliderControl == 2)
	{
		interface_sliderCallback(interfaceInfo.currentMenu, cmd, interfaceSlider.pArg);
	} else if ( (cmd->command == interfaceCommandVolumeUp ||
	             cmd->command == interfaceCommandVolumeDown ||
	             cmd->command == interfaceCommandVolumeMute) ||
	            (cmd->command == interfaceCommandExit && !interfaceInfo.showMenu &&
	            !interfacePlayControl.visibleFlag && interfaceSoundControl.visibleFlag) )
	{
		if (interface_soundControlProcessCommand(cmd))
		{
			interface_displayMenu(1);
		}
	}
	else if ( interfacePlayControl.enabled &&
	         (cmd->command == interfaceCommandPrevious ||
	          cmd->command == interfaceCommandRewind ||
	          cmd->command == interfaceCommandStop ||
	          cmd->command == interfaceCommandPause ||
	          cmd->command == interfaceCommandPlay ||
	          cmd->command == interfaceCommandFastForward ||
	          cmd->command == interfaceCommandNext ||
	          cmd->command == interfaceCommandChannelUp ||
	          cmd->command == interfaceCommandChannelDown) )
	{
		interface_playControlProcessCommand(cmd);
	}
	else if ( interfacePlayControl.enabled && !interfaceInfo.showMenu &&
	         (cmd->command == interfaceCommandRed ||
	          cmd->command == interfaceCommandGreen ||
	          cmd->command == interfaceCommandExit) )
	{
		interface_playControlProcessCommand(cmd);
	} else if ( cmd->command == interfaceCommandToggleMenu )
	{
#ifdef ENABLE_TELETEXT
		if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
		{
			interfaceInfo.teletext.show=0;
		}
#endif
		interface_showMenu(!interfaceInfo.showMenu, 1);
/*
		if (interfaceInfo.currentMenu != NULL && interfaceInfo.currentMenu->selectedItem > 0 && interfaceInfo.currentMenu->selectedItem < interfaceInfo.currentMenu->menuEntryCount)
		{
			if (!interfaceInfo.showMenu && interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pDeselectedAction != NULL)
			{
				interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pDeselectedAction(interfaceInfo.currentMenu, interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pArg);
			} else if (interfaceInfo.showMenu && interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pSelectedAction != NULL)
			{
				interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pSelectedAction(interfaceInfo.currentMenu, interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pArg);
			}
		}
*/
		//dprintf("%s: toggle show menu: %d\n", __FUNCTION__, interfaceInfo.showMenu);

	}
	/* Hot keys */
	else if (!interfaceInfo.lockMenu &&
	         (cmd->command == interfaceCommandServices ||
	          cmd->command == interfaceCommandPhone ||
	          cmd->command == interfaceCommandWeb ||
	          cmd->command == interfaceCommandFavorites)
	        )
	{
		static interfaceMenu_t *lastMenu = (interfaceMenu_t*)&OutputMenu;
		static int              lastShow = 1;
		switch (cmd->command)
		{
			case interfaceCommandServices:
#ifdef ENABLE_TELETEXT
				if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
				{
					interfaceInfo.teletext.show=0;
				}
#endif
				if ( interfaceInfo.currentMenu == (interfaceMenu_t*)&OutputMenu )
				{
					if (interfaceInfo.showMenu)
					{
						interface_menuActionShowMenu(interfaceInfo.currentMenu,(void*)lastMenu);
						interface_showMenu(lastShow, 1);
					}
					else
					{
						interface_showMenu(1, 1);
					}
				}
				else
				{
					lastMenu = interfaceInfo.currentMenu;
					lastShow = interfaceInfo.showMenu;
					interface_menuActionShowMenu(interfaceInfo.currentMenu,(void*)&OutputMenu);
					interface_showMenu(1, 1);
				}
			break;
			case interfaceCommandPhone:
#ifdef ENABLE_TELETEXT
				if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
				{
					interfaceInfo.teletext.show=0;
				}
#endif
#ifdef ENABLE_VOIP
				if (appControlInfo.voipInfo.status == voipStatus_incoming)
				{
					voip_answerCall(interfaceInfo.currentMenu,NULL);
				} else
				{
					if (interfaceInfo.currentMenu == (interfaceMenu_t*)&VoIPMenu)
					{
						if (interfaceInfo.showMenu)
						{
							interface_menuActionShowMenu(interfaceInfo.currentMenu,(void*)lastMenu);
							interface_showMenu(lastShow, 1);
						}
						else
						{
							interface_showMenu(1, 1);
						}
					} else
					{
						lastMenu = interfaceInfo.currentMenu;
						lastShow = interfaceInfo.showMenu;
						interfaceInfo.showMenu = 1;
						voip_fillMenu(interfaceInfo.currentMenu, (void*)1);
					}
				}
#endif
			break;
			case interfaceCommandWeb:
#ifdef ENABLE_BROWSER
				open_browser((interfaceMenu_t*)&interfaceMainMenu, NULL);
#endif
			break;
			default:;
		}
	}	
	else if ( interfaceInfo.showMenu )
	{
#ifdef ENABLE_TELETEXT
		if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
		{
			interfaceInfo.teletext.show=0;
		}
#endif
		if ( interfaceInfo.currentMenu != NULL && interfaceInfo.currentMenu->processCommand != NULL )
		{
			interfaceInfo.currentMenu->processCommand(interfaceInfo.currentMenu, cmd);
		}
	} else if ( interfacePlayControl.enabled || interfaceSlideshowControl.enabled)
	{
		interface_playControlProcessCommand(cmd);
	} else // menu is hidden and play control is disabled
	{
#ifdef ENABLE_TELETEXT
		if (appControlInfo.teletextInfo.exists && interfaceInfo.teletext.show && (!appControlInfo.teletextInfo.subtitleFlag))
		{
			interfaceInfo.teletext.show=0;
		}
#endif
		interface_showMenu(!interfaceInfo.showMenu, 1);
	}
}

/*
static int interface_triggerChange(void *pArg)
{
	interface_processCommand(interfaceCommandEnter);

	return 0;
}
*/

int interface_MenuDefaultProcessCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd)
{
	int n;
	int executeAction = 0;
	int oldSelected = pMenu->selectedItem;
	interfaceMenu_t *pOldMenu = interfaceInfo.currentMenu;
	interfaceListMenu_t *pListMenu = pMenu->menuType == interfaceMenuList ? (interfaceListMenu_t*)pMenu : NULL;

	dprintf("%s: in\n", __FUNCTION__);

	//interface_removeEvent(interface_triggerChange, NULL);

	if ( pMenu->pCustomKeysCallback != NULL )// && pMenu->selectedItem >=0 )
	{
		if ( pMenu->pCustomKeysCallback(pMenu, cmd,
			pMenu->selectedItem >=0 ? pMenu->menuEntry[pMenu->selectedItem].pArg : NULL ) == 0 )
			return 0;
	}

	if ( cmd->command == interfaceCommandUp )
	{
// 		dprintf("%s: up\n", __FUNCTION__);
		n = pListMenu != NULL && pListMenu->listMenuType == interfaceListMenuBigThumbnail ?
			(pMenu->selectedItem-pListMenu->infoAreaWidth + pMenu->menuEntryCount) % pMenu->menuEntryCount : 
			 pMenu->selectedItem-1;
		while ( n >= 0 && pMenu->menuEntry[n].isSelectable == 0 )
		{
			n--;
		};

// 		dprintf("%s: n=%d\n", __FUNCTION__, n);
		if ((pMenu->pParentMenu != NULL && n < MENU_ITEM_MAIN) ||
		    (pMenu->pParentMenu == NULL && n < 0))
		{
			//dprintf("%s: loop\n", __FUNCTION__);
			n = pMenu->menuEntryCount-1;
			while ( n >= 0 && pMenu->menuEntry[n].isSelectable == 0 )
			{
				//dprintf("%s: loop n=%d\n", __FUNCTION__, n);
				n--;
			};
		}

		if ( pMenu->pParentMenu != NULL )
		{
			//pMenu->selectedItem = pMenu->selectedItem > MENU_ITEM_MAIN ? n : pMenu->selectedItem;
			pMenu->selectedItem = n;
			if ( pMenu->selectedItem == MENU_ITEM_BACK && pMenu->pParentMenu->pParentMenu == NULL )
			{
				pMenu->selectedItem = MENU_ITEM_MAIN;
			}
		} else
		{
			pMenu->selectedItem = n >= 0 ? n : pMenu->selectedItem;
		}
	} else if ( cmd->command == interfaceCommandDown )
	{
// 		dprintf("%s: down\n", __FUNCTION__);
		if ( pMenu->selectedItem == MENU_ITEM_MAIN && pMenu->pParentMenu != NULL && pMenu->pParentMenu->pParentMenu != NULL )
		{
			n = MENU_ITEM_BACK;
		} else
		{
			if (pMenu->selectedItem < 0)
			{
				n = 0;
			}
			else
			{
				n = pListMenu != NULL && pListMenu->listMenuType == interfaceListMenuBigThumbnail ?
					(pMenu->selectedItem + pListMenu->infoAreaWidth) % pMenu->menuEntryCount : pMenu->selectedItem+1;
			}

			while ( n < pMenu->menuEntryCount && pMenu->menuEntry[n].isSelectable == 0 )
			{
				n++;
			};
		}

		if (n >= pMenu->menuEntryCount)
		{
			//dprintf("%s: loop\n", __FUNCTION__);
			if ( pMenu->pParentMenu != NULL )
			{
				n = MENU_ITEM_MAIN;
			} else
			{
				//dprintf("%s: zero\n", __FUNCTION__);
				n = 0;
				while ( n < pMenu->menuEntryCount && pMenu->menuEntry[n].isSelectable == 0 )
				{
					n++;
				};
			}
		}
		//dprintf("%s: new n = %d of %d\n", __FUNCTION__, n, pMenu->menuEntryCount);
		//dprintf("%s: n = %d of %d\n", __FUNCTION__, n, pMenu->menuEntryCount);
		pMenu->selectedItem = n;// < pMenu->menuEntryCount ? n : (pMenu->selectedItem == MENU_ITEM_MAIN ? pMenu->selectedItem+1 : pMenu->selectedItem);
		//dprintf("%s: check\n", __FUNCTION__);
		/*if ( pMenu->selectedItem < 0 )
		{
			if ( n < pMenu->menuEntryCount && pMenu->menuEntryCount > 0 )
			{
				//dprintf("%s: zero\n", __FUNCTION__);
				pMenu->selectedItem = 0;
			} else if ( pMenu->pParentMenu != NULL )
			{
				//dprintf("%s: main-back\n", __FUNCTION__);
				if ( pMenu->selectedItem == MENU_ITEM_BACK && pMenu->pParentMenu->pParentMenu == NULL )
				{
					pMenu->selectedItem = MENU_ITEM_MAIN;
				}
			} else
			{
				pMenu->selectedItem = 0;
			}
		}*/
	} else if ( cmd->command == interfaceCommandLeft )
	{
		//dprintf("%s: left\n", __FUNCTION__);
		if ( pMenu->selectedItem == MENU_ITEM_BACK && pMenu->pParentMenu != NULL )
		{
			pMenu->selectedItem = MENU_ITEM_MAIN;
		} else if ( pMenu->selectedItem == MENU_ITEM_MAIN && pMenu->pParentMenu->pParentMenu != NULL )
		{
			pMenu->selectedItem = MENU_ITEM_BACK;
		} else if ( pListMenu != NULL && pListMenu->listMenuType == interfaceListMenuBigThumbnail )
		{
			if (pMenu->selectedItem == 0 && pMenu->pParentMenu != NULL)
			{
				if (pMenu->pParentMenu->pParentMenu != NULL)
				{
					pMenu->selectedItem = MENU_ITEM_BACK;
				} else
				{
					pMenu->selectedItem = MENU_ITEM_MAIN;
				}
			} else
			{
				n = pMenu->selectedItem-1;
				while ( n >= 0 && pMenu->menuEntry[n].isSelectable == 0 )
				{
					n--;
				};
		
				//dprintf("%s: n=%d\n", __FUNCTION__, n);
				if ((pMenu->pParentMenu != NULL && n < MENU_ITEM_MAIN) ||
					(pMenu->pParentMenu == NULL && n < 0))
				{
					//dprintf("loop\n");
					n = pMenu->menuEntryCount-1;
					while ( n >= 0 && pMenu->menuEntry[n].isSelectable == 0 )
					{
						//dprintf("loop n=%d\n", n);
						n--;
					};
				}
		
				if ( pMenu->pParentMenu != NULL )
				{
					//pMenu->selectedItem = pMenu->selectedItem > MENU_ITEM_MAIN ? n : pMenu->selectedItem;
					pMenu->selectedItem = n;
					if ( pMenu->selectedItem == MENU_ITEM_BACK && pMenu->pParentMenu->pParentMenu == NULL )
					{
						pMenu->selectedItem = MENU_ITEM_MAIN;
					}
				} else
				{
					pMenu->selectedItem = n >= 0 ? n : pMenu->selectedItem;
				}
			}
		} else if (pMenu->selectedItem >= 0 && pMenu->pParentMenu != NULL)
		{
			if (pMenu->pParentMenu->pParentMenu != NULL)
			{
				pMenu->selectedItem = MENU_ITEM_BACK;
			} else
			{
				pMenu->selectedItem = MENU_ITEM_MAIN;
			}
		} else if (pMenu->pParentMenu == NULL)
		{
			interface_showMenu(!interfaceInfo.showMenu, 1);
		}
	} else if ( cmd->command == interfaceCommandRight )
	{
		//dprintf("%s: right\n", __FUNCTION__);
		if ( pListMenu != NULL && pListMenu->listMenuType == interfaceListMenuBigThumbnail )
		{
			if ( pMenu->selectedItem == MENU_ITEM_MAIN )
			{
				n = MENU_ITEM_BACK;
			} else
			{
				if (pMenu->selectedItem < 0)
				{
					n = 0;
				}
				else
				{
					n = pMenu->selectedItem+1;
				}
	
				while ( n < pMenu->menuEntryCount && pMenu->menuEntry[n].isSelectable == 0 )
				{
					n++;
				};
			}
	
			if (n >= pMenu->menuEntryCount)
			{
				//dprintf("%s: loop\n", __FUNCTION__);
				if ( pMenu->pParentMenu != NULL )
				{
					n = MENU_ITEM_MAIN;
				} else
				{
					//dprintf("%s: zero\n", __FUNCTION__);
					n = 0;
					while ( n < pMenu->menuEntryCount && pMenu->menuEntry[n].isSelectable == 0 )
					{
						n++;
					};
				}
			}
			//dprintf("%s: new n = %d of %d\n", __FUNCTION__, n, pMenu->menuEntryCount);
			//dprintf("%s: n = %d of %d\n", __FUNCTION__, n, pMenu->menuEntryCount);
			pMenu->selectedItem = n;
		} else if ( pMenu->selectedItem < 0 && pMenu->pParentMenu != NULL && pMenu->pParentMenu->pParentMenu != NULL )
		{
			pMenu->selectedItem = 1-(pMenu->selectedItem+2)-2;
		}
	} else if ( cmd->command == interfaceCommandBack )
	{
		if ( pMenu->pParentMenu != NULL )
		{
			//dprintf("%s: go back\n", __FUNCTION__);
			interface_menuActionShowMenu(pMenu, pMenu->pParentMenu);
			//interfaceInfo.currentMenu = pMenu->pParentMenu;
		}
	} else if (cmd->command == interfaceCommandExit)
	{
		if ( pMenu->pParentMenu == NULL )
		{
			interface_showMenu(!interfaceInfo.showMenu, 1);
		} else
		{
			interface_menuActionShowMenu(pMenu, &interfaceMainMenu);
		}
	} else if ( cmd->command == interfaceCommandMainMenu )
	{
		interfaceMenu_t *pParent = pMenu;
		while ( pParent->pParentMenu != NULL )
		{
			pParent = pParent->pParentMenu;
		}
		interface_menuActionShowMenu(pMenu, pParent);
		//interfaceInfo.currentMenu = pParent;
	} else if ( cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk )
	{
		//dprintf("%s: enter\n", __FUNCTION__);
		if ( pMenu->selectedItem == MENU_ITEM_BACK && pMenu->pParentMenu != NULL )
		{
			//dprintf("%s: go back\n", __FUNCTION__);
			interface_menuActionShowMenu(pMenu, pMenu->pParentMenu);
			//interfaceInfo.currentMenu = pMenu->pParentMenu;
		} else if ( pMenu->selectedItem == MENU_ITEM_MAIN )
		{
			//dprintf("%s: go to main\n", __FUNCTION__);
			interfaceMenu_t *pParent = pMenu;
			while ( pParent->pParentMenu != NULL )
			{
				pParent = pParent->pParentMenu;
			}
			interface_menuActionShowMenu(pMenu, pParent);
			//interfaceInfo.currentMenu = pParent;
		} else if ( pMenu->selectedItem >= 0 && pMenu->menuEntryCount > 0 )
		{
			if ( pMenu->menuEntry[pMenu->selectedItem].type == interfaceMenuEntryEdit )
			{
				dprintf("%s: setting %d edit entry active\n", __FUNCTION__, pMenu->selectedItem);
				((interfaceEditEntry_t*)pMenu->menuEntry[pMenu->selectedItem].pArg)->active = 1;
				interface_displayMenu(1);
			} else
			if ( pMenu->menuEntry[pMenu->selectedItem].pAction != NULL )
			{
				pMenu->menuEntry[pMenu->selectedItem].pAction(pMenu, pMenu->menuEntry[pMenu->selectedItem].pArg);
			}
		}
	} else if ( cmd->command >= interfaceCommand0 && cmd->command <= interfaceCommand9 )
	{
		n = (cmd->command-interfaceCommand1);
		if (pMenu->selectedItem >= 0 && n+(pMenu->selectedItem+1)*10 < pMenu->menuEntryCount)
		{
			n += (pMenu->selectedItem+1)*10;
		}
		if ( n < pMenu->menuEntryCount && pMenu->menuEntry[n].isSelectable )
		{
			pMenu->selectedItem = n;
			executeAction = 0;
			//interface_addEvent(interface_triggerChange, NULL, 1000, 1);
		}
	} else if ( cmd->command == interfaceCommandTV )
	{
		interface_enterChannelList();
	} else
	{
		// unknown command
		return 1;
	}

	//dprintf("%s: show %d %d\n", __FUNCTION__, pMenu->selectedItem, pMenu->menuEntryCount);

	if ( oldSelected != pMenu->selectedItem || pOldMenu != interfaceInfo.currentMenu )
	{
		//dprintf("%s: changed menu or selected item\n", __FUNCTION__);
		if ( oldSelected >= 0 && pOldMenu->menuEntryCount > 0 && pOldMenu->menuEntry[oldSelected].pDeselectedAction != NULL &&
		     pOldMenu->menuEntry[oldSelected].type != interfaceMenuEntryEdit ) // edit entries use deselected action pointer as reset action
		{
			//dprintf("%s: call DeselectedAction\n", __FUNCTION__);
			pOldMenu->menuEntry[oldSelected].pDeselectedAction(pOldMenu, pOldMenu->menuEntry[oldSelected].pArg);
		}
		if (interfaceInfo.currentMenu->selectedItem >= 0 && 
		    interfaceInfo.currentMenu->menuEntryCount > 0 && 
		    interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pSelectedAction != NULL)
		{
			//dprintf("%s: call SelectedAction\n", __FUNCTION__);
			interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pSelectedAction(
				interfaceInfo.currentMenu,
				interfaceInfo.currentMenu->menuEntry[interfaceInfo.currentMenu->selectedItem].pArg);
		}

		interface_displayMenu(1);
	}

	if (executeAction)
	{
		if (pMenu->menuEntry[pMenu->selectedItem].pAction != NULL)
		{
			pMenu->menuEntry[pMenu->selectedItem].pAction(pMenu, pMenu->menuEntry[pMenu->selectedItem].pArg);
		}
	}

	return 0;
}

int interface_listMenuProcessCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd)
{
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t*)pMenu;
	int itemHeight, maxVisibleItems, n;
	interfaceEditEntry_t *pEditEntry;

	dprintf("%s: in %d\n", __FUNCTION__, cmd->command);

#ifdef ENABLE_REGPLAT
	sleep(0.2);
#endif

	if (pListMenu->baseMenu.selectedItem >= 0 &&
	    pListMenu->baseMenu.menuEntry[pListMenu->baseMenu.selectedItem].type == interfaceMenuEntryEdit &&
	    (pEditEntry = (interfaceEditEntry_t*)pListMenu->baseMenu.menuEntry[pListMenu->baseMenu.selectedItem].pArg)->active
	  )
	{
		if (pEditEntry->pCallback(cmd, (void*)pEditEntry) == 0 )
		{
			return 0;
		}
		switch ( cmd->command )
		{
			case interfaceCommandOk:
			case interfaceCommandEnter:
			case interfaceCommandGreen:
				if (pListMenu->baseMenu.menuEntry[pListMenu->baseMenu.selectedItem].pAction == NULL ||
				    pListMenu->baseMenu.menuEntry[pListMenu->baseMenu.selectedItem].pAction(pMenu, pEditEntry) == 0 )
				{
					pEditEntry->active = 0;
					interface_editNext(pMenu);
					interface_displayMenu(1);
				}
				return 0;
			case interfaceCommandExit:
			case interfaceCommandRed:
				pEditEntry->active = 0;
				if (pListMenu->baseMenu.menuEntry[pListMenu->baseMenu.selectedItem].pDeselectedAction )
					pListMenu->baseMenu.menuEntry[pListMenu->baseMenu.selectedItem].pDeselectedAction(pMenu, pEditEntry);
// 				interface_editNext(pMenu); /* auto skipping is annoying, if there are a lot of items to edit */
				interface_displayMenu(1);
				return 0;
			default:
				return 0;
		}
	}

	if ( cmd->command == interfaceCommandChangeMenuStyle )
	{
		if ( pListMenu->listMenuType < interfaceListMenuTypeCount-1 )
		{
			pListMenu->listMenuType++;
		} else
		{
			pListMenu->listMenuType = 0;
		}
		interface_reinitializeListMenu(pMenu);
		interface_displayMenu(1);
	} else if ( cmd->command == interfaceCommandPageUp )
	{
		//dprintf("%s: up\n", __FUNCTION__);

		interface_listMenuGetItemInfo(pListMenu,&itemHeight,&maxVisibleItems);

		if (pListMenu->baseMenu.selectedItem == MENU_ITEM_BACK && 
		    pListMenu->baseMenu.pParentMenu != NULL &&
		    pListMenu->baseMenu.pParentMenu->pParentMenu != NULL )
		{
			n = MENU_ITEM_MAIN;
		} else if ( pListMenu->baseMenu.menuEntryCount <= maxVisibleItems )
		{
			n = 0;
		}else
		{
			n = pListMenu->baseMenu.selectedItem-maxVisibleItems;
			if (n < 0)
				n = 0;
		}

		while ( n >= 0 && pListMenu->baseMenu.menuEntry[n].isSelectable == 0 )
		{
			n--;
		};

		//dprintf("%s: n=%d\n", __FUNCTION__, n);
		if ((pListMenu->baseMenu.pParentMenu != NULL && n < MENU_ITEM_MAIN) ||
			(pListMenu->baseMenu.pParentMenu == NULL && n < 0))
		{
			//dprintf("%s: loop\n", __FUNCTION__);
			n = pListMenu->baseMenu.menuEntryCount-1;
			while ( n >= 0 && pListMenu->baseMenu.menuEntry[n].isSelectable == 0 )
			{
				//dprintf("%s: loop n=%d\n", __FUNCTION__, n);
				n--;
			};
		}

		if ( pListMenu->baseMenu.pParentMenu != NULL )
		{
			//pListMenu->baseMenu.selectedItem = pListMenu->baseMenu.selectedItem > MENU_ITEM_MAIN ? n : pListMenu->baseMenu.selectedItem;
			pListMenu->baseMenu.selectedItem = n;
			if (pListMenu->baseMenu.selectedItem == MENU_ITEM_BACK && 
			    pListMenu->baseMenu.pParentMenu->pParentMenu == NULL )
			{
				pListMenu->baseMenu.selectedItem = MENU_ITEM_MAIN;
			}
		} else
		{
			pListMenu->baseMenu.selectedItem = n >= 0 ? n : pListMenu->baseMenu.selectedItem;
		}
		interface_displayMenu(1);
	} else if ( cmd->command == interfaceCommandPageDown )
	{
		interface_listMenuGetItemInfo(pListMenu,&itemHeight,&maxVisibleItems);
		//dprintf("%s: down\n", __FUNCTION__);

		if ( pListMenu->baseMenu.selectedItem == MENU_ITEM_MAIN && 
		     pListMenu->baseMenu.pParentMenu != NULL && 
		     pListMenu->baseMenu.pParentMenu->pParentMenu != NULL )
		{
			n = MENU_ITEM_BACK;
		} else
		{

			if ( pListMenu->baseMenu.menuEntryCount <= maxVisibleItems )
			{
				n = pListMenu->baseMenu.menuEntryCount-1;
			}else
			{
				n = pListMenu->baseMenu.selectedItem+maxVisibleItems;
				if ( n >= pListMenu->baseMenu.menuEntryCount )
					n = pListMenu->baseMenu.menuEntryCount-1;
			}
			while ( n < pListMenu->baseMenu.menuEntryCount && pListMenu->baseMenu.menuEntry[n].isSelectable == 0 )
			{
				n++;
			};
		}

		if (n >= pListMenu->baseMenu.menuEntryCount)
		{
			//dprintf("%s: loop\n", __FUNCTION__);
			if ( pListMenu->baseMenu.pParentMenu != NULL )
			{
				n = MENU_ITEM_MAIN;
			} else
			{
				//dprintf("%s: zero\n", __FUNCTION__);
				n = 0;
				while ( n < pListMenu->baseMenu.menuEntryCount && pListMenu->baseMenu.menuEntry[n].isSelectable == 0 )
				{
					n++;
				};
			}
		}

		//dprintf("%s: new n = %d of %d\n", __FUNCTION__, n, pListMenu->baseMenu.menuEntryCount);

		//dprintf("%s: n = %d of %d\n", __FUNCTION__, n, pListMenu->baseMenu.menuEntryCount);

		pListMenu->baseMenu.selectedItem = n;
		interface_displayMenu(1);
	} else
	{
		interface_MenuDefaultProcessCommand(&pListMenu->baseMenu, cmd);
	}
	return 0;
}

int interface_menuActionShowMenu(interfaceMenu_t *pMenu, void *pArg)
{
	interfaceMenu_t *pTargetMenu = (interfaceMenu_t*)pArg;
	int ret = 0;

	if ( pTargetMenu != NULL && pTargetMenu != interfaceInfo.currentMenu )
	{
		if (pMenu->pDeactivatedAction != NULL)
		{
			if ( (ret = pMenu->pDeactivatedAction(pMenu, pMenu->pArg)) != 0 )
			{
				dprintf("%s: can't leave menu '%s' (new '%s')\n", __FUNCTION__, pMenu->name, pTargetMenu->name);
				return ret;
			}
		}
		if (pTargetMenu->pActivatedAction != NULL)
		{
			if ( (ret = pTargetMenu->pActivatedAction(pTargetMenu, pTargetMenu->pArg)) != 0 )
			{
				dprintf("%s: can't enter menu '%s' (old '%s')\n", __FUNCTION__, pTargetMenu->name, pMenu->name);
				return ret;
			}
		}
		interfaceInfo.currentMenu = pTargetMenu;
		//eprintf ("%s: interface_animateMenu...\n", __FUNCTION__);
		interface_animateMenu(1, 1);
	}
	return ret;
}

void interface_listMenuGetItemInfo(interfaceListMenu_t *pListMenu, int* itemHeight, int* maxVisibleItems)
{
	int fh;
	pgfx_font->GetHeight(pgfx_font, &fh);
	/* calculate total height */
	if ( pListMenu->listMenuType == interfaceListMenuIconThumbnail )
	{
		*itemHeight = (interfaceInfo.paddingSize+pListMenu->baseMenu.thumbnailHeight);
	} else
	{
		*itemHeight = (interfaceInfo.paddingSize+fh);
	}

	//dprintf("%s: item height: %d\n", __FUNCTION__, itemHeight);

	if ((*itemHeight)*pListMenu->baseMenu.menuEntryCount > interfaceInfo.clientHeight-interfaceInfo.paddingSize*2)
	{
		*maxVisibleItems = interfaceInfo.clientHeight/(*itemHeight);
	} else
	{
		*maxVisibleItems = pListMenu->baseMenu.menuEntryCount;
	}
	//dprintf("%s: maxVisibleItems: %d\n", __FUNCTION__, maxVisibleItems);
}

void interface_listMenuDisplay(interfaceMenu_t *pMenu)
{
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t*)pMenu;
	int i, x, y;
	int fh;
	int itemHeight, maxVisibleItems, itemOffset, itemDisplayIndex;
	int r, g, b, a;
	char *str;
	DFBRectangle rect;

	//dprintf("%s: displaying\n", __FUNCTION__);

	//DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

	//	gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0, interfaceInfo.clientX, interfaceInfo.clientY, interfaceInfo.clientWidth, interfaceInfo.clientHeight);
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
	DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, 
		INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );

	if ( pListMenu->listMenuType != interfaceListMenuBigThumbnail)
	{
		gfx_drawRectangle(DRAWING_SURFACE, 
		                  INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN,
		                  INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, 
		                  interfaceInfo.clientX, interfaceInfo.clientY+INTERFACE_ROUND_CORNER_RADIUS,
		                  interfaceInfo.clientWidth, interfaceInfo.clientHeight-2*INTERFACE_ROUND_CORNER_RADIUS);
		// top left corner
		gfx_drawRectangle(DRAWING_SURFACE,
			INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
			interfaceInfo.clientX+INTERFACE_ROUND_CORNER_RADIUS, interfaceInfo.clientY,
			interfaceInfo.clientWidth-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png",
			interfaceInfo.clientX, interfaceInfo.clientY,
			INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS,
			0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
		// bottom left corner
		gfx_drawRectangle(DRAWING_SURFACE, 
			INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
			interfaceInfo.clientX+INTERFACE_ROUND_CORNER_RADIUS, interfaceInfo.clientY+interfaceInfo.clientHeight-INTERFACE_ROUND_CORNER_RADIUS,
			interfaceInfo.clientWidth-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", interfaceInfo.clientX, interfaceInfo.clientY+interfaceInfo.clientHeight-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
	} else {
#ifndef ENABLE_VIDIMAX
		gfx_drawRectangle(DRAWING_SURFACE, 
			INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA,
			0, 0, interfaceInfo.screenWidth, interfaceInfo.marginSize);
#endif
	}
	/*{
		DFBRectangle clip;
		clip.x = interfaceInfo.clientX;
		clip.y = interfaceInfo.clientY;
		clip.w = interfaceInfo.clientWidth;
		clip.h = interfaceInfo.clientHeight;
		DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA);
		interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "background.png", interfaceInfo.clientX, interfaceInfo.clientY, interfaceInfo.screenWidth, interfaceInfo.screenHeight, 0, &clip, DSBLIT_BLEND_COLORALPHA, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
		//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "background.png", interfaceInfo.clientX, interfaceInfo.clientY, interfaceInfo.screenWidth, interfaceInfo.screenHeight, 0, &clip, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
	}*/


	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

	/* Show menu logo if needed */
	if (interfaceInfo.currentMenu->logo > 0 && interfaceInfo.currentMenu->logoX > 0)
	{
		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[interfaceInfo.currentMenu->logo],
			interfaceInfo.currentMenu->logoX, interfaceInfo.currentMenu->logoY,
			interfaceInfo.currentMenu->logoWidth, interfaceInfo.currentMenu->logoHeight,
			0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0);
	}

	/*
	if ( pListMenu->listMenuType == interfaceListMenuBigThumbnail )
	{
		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_THUMBNAIL_BACKGROUND_RED, INTERFACE_THUMBNAIL_BACKGROUND_GREEN, INTERFACE_THUMBNAIL_BACKGROUND_BLUE, INTERFACE_THUMBNAIL_BACKGROUND_ALPHA, pListMenu->infoAreaX, pListMenu->infoAreaY, pListMenu->infoAreaWidth, pListMenu->infoAreaHeight);
	}*/

	pgfx_font->GetHeight(pgfx_font, &fh);

#ifdef INTERFACE_DRAW_ARROW
	IDirectFBSurface *pArrow;
	pArrow = gfx_decodeImage(IMAGE_DIR INTERFACE_ARROW_IMAGE, INTERFACE_ARROW_SIZE, INTERFACE_ARROW_SIZE, 0);
#endif

	//dprintf("%s: go through entries\n", __FUNCTION__);

	if ( pListMenu->listMenuType == interfaceListMenuBigThumbnail )
	{
		int last_row_index, last_row_col_count;
		interfaceMenuEntry_t *selectedEntry = NULL;

		r = INTERFACE_BOOKMARK_RED;
		g = INTERFACE_BOOKMARK_GREEN;
		b = INTERFACE_BOOKMARK_BLUE;
		a = INTERFACE_BOOKMARK_ALPHA;
		switch (pListMenu->baseMenu.selectedItem)
		{
			case MENU_ITEM_MAIN: str = _T("MAIN_MENU"); break;
			case MENU_ITEM_BACK: str = _T("BACK"); break;
			default:
				selectedEntry = &pListMenu->baseMenu.menuEntry[pListMenu->baseMenu.selectedItem];
				str = selectedEntry->info;
				if (selectedEntry->infoReplacedChar != 0 )
					selectedEntry->info[selectedEntry->infoReplacedCharPos] = selectedEntry->infoReplacedChar;
		}
		DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, str, -1, &rect, NULL) );
		x = (interfaceInfo.screenWidth - rect.w)/2;
		y = interfaceInfo.marginSize - rect.h/2 - interfaceInfo.paddingSize;
		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, str, 0, 1);
		if (selectedEntry != NULL && selectedEntry->infoReplacedChar != 0)
			selectedEntry->info[selectedEntry->infoReplacedCharPos] = 0;

		switch (pListMenu->baseMenu.menuEntryCount)
		{
			case 0:case 1: pListMenu->infoAreaWidth = 1; break;
			case 2:case 4: pListMenu->infoAreaWidth = 2; break;
			case 3:case 5:
			case 6:case 9: pListMenu->infoAreaWidth = 3; break;
			default: pListMenu->infoAreaWidth = 4;
		}
		maxVisibleItems = pListMenu->infoAreaWidth * 3;
		pListMenu->infoAreaHeight = (pListMenu->baseMenu.menuEntryCount + pListMenu->infoAreaWidth-1) / pListMenu->infoAreaWidth;
		last_row_col_count = pListMenu->baseMenu.menuEntryCount - (pListMenu->infoAreaWidth*(pListMenu->infoAreaHeight-1));
		last_row_index = last_row_col_count == pListMenu->infoAreaWidth ?
		                 pListMenu->baseMenu.menuEntryCount :
		                 pListMenu->infoAreaWidth * (pListMenu->infoAreaHeight - 1);
		//dprintf("%s: count=%d col=%d row=%d last_count=%d last_index=%d\n", __FUNCTION__,pListMenu->baseMenu.menuEntryCount,pListMenu->infoAreaWidth,pListMenu->infoAreaHeight,last_row_col_count,last_row_index);
		
		pListMenu->infoAreaX = (interfaceInfo.screenWidth  - INTERFACE_BIG_THUMBNAIL_SIZE*pListMenu->infoAreaWidth)/2;
		pListMenu->infoAreaY = (interfaceInfo.screenHeight - INTERFACE_BIG_THUMBNAIL_SIZE*
			(pListMenu->baseMenu.menuEntryCount <= maxVisibleItems ? pListMenu->infoAreaHeight : 3 ))/2;
		int last_row_start = last_row_col_count == 0 ? 
		                     pListMenu->infoAreaX :
		                     (interfaceInfo.screenWidth - last_row_col_count*INTERFACE_BIG_THUMBNAIL_SIZE)/2;

		if ( pListMenu->baseMenu.menuEntryCount > maxVisibleItems && pListMenu->baseMenu.selectedItem >= pListMenu->infoAreaWidth*2 )
		{
			itemOffset = (pListMenu->baseMenu.selectedItem / pListMenu->infoAreaWidth - 1) * pListMenu->infoAreaWidth;
			itemOffset = itemOffset > (pListMenu->baseMenu.menuEntryCount - last_row_col_count - pListMenu->infoAreaWidth*2) ?
			             pListMenu->baseMenu.menuEntryCount - last_row_col_count - pListMenu->infoAreaWidth*2 :
			             itemOffset;
		} else
		{
			itemOffset = 0;
		}

		rect.w = INTERFACE_BIG_THUMBNAIL_SIZE;
		rect.h = INTERFACE_BIG_THUMBNAIL_SIZE;
		for ( i = itemOffset; i < pListMenu->baseMenu.menuEntryCount && i < itemOffset + maxVisibleItems; i++ )
		{
			rect.x = i < last_row_index
				? pListMenu->infoAreaX + INTERFACE_BIG_THUMBNAIL_SIZE * (i % pListMenu->infoAreaWidth)              // complete row
				: last_row_start       + INTERFACE_BIG_THUMBNAIL_SIZE *((i - last_row_index) % last_row_col_count); // incomplete row
			rect.y = pListMenu->infoAreaY + INTERFACE_BIG_THUMBNAIL_SIZE * ((i-itemOffset) / pListMenu->infoAreaWidth);

			pListMenu->baseMenu.menuEntry[i].pDisplay( pMenu, &rect, i );
		}
	} else
	{
		//pListMenu->listMenuType != interfaceListMenuBigThumbnail
		interface_listMenuGetItemInfo(pListMenu,&itemHeight,&maxVisibleItems);

		if ( pListMenu->baseMenu.selectedItem > maxVisibleItems/2 )
		{
			itemOffset = pListMenu->baseMenu.selectedItem - maxVisibleItems/2;
			itemOffset = itemOffset > (pListMenu->baseMenu.menuEntryCount-maxVisibleItems) ? 
			             pListMenu->baseMenu.menuEntryCount-maxVisibleItems :
			             itemOffset;
		} else
		{
			itemOffset = 0;
		}

		rect.w = interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize;
		if (maxVisibleItems < pListMenu->baseMenu.menuEntryCount)
			rect.w -= INTERFACE_SCROLLBAR_WIDTH + interfaceInfo.paddingSize;
		rect.h = pListMenu->listMenuType == interfaceListMenuNoThumbnail ?
		         fh + interfaceInfo.paddingSize :
		         pListMenu->baseMenu.thumbnailHeight;
		for ( i=itemOffset; i<itemOffset+maxVisibleItems; i++ )
		{
			itemDisplayIndex = i-itemOffset;
			rect.x = interfaceInfo.clientX+interfaceInfo.paddingSize;
			rect.y = pListMenu->listMenuType == interfaceListMenuNoThumbnail
				? interfaceInfo.clientY+(interfaceInfo.paddingSize+fh)*itemDisplayIndex + interfaceInfo.paddingSize
				: interfaceInfo.clientY+(interfaceInfo.paddingSize+pListMenu->baseMenu.thumbnailHeight)*itemDisplayIndex + interfaceInfo.paddingSize;

			pListMenu->baseMenu.menuEntry[i].pDisplay( pMenu, &rect, i );
		}
	}

	/* draw scroll bar if needed */
	if ( maxVisibleItems < pListMenu->baseMenu.menuEntryCount )
	{
		interface_drawScrollingBar( DRAWING_SURFACE,
			interfaceInfo.clientX + interfaceInfo.clientWidth - interfaceInfo.paddingSize - INTERFACE_SCROLLBAR_WIDTH,
			interfaceInfo.clientY + interfaceInfo.paddingSize,
			INTERFACE_SCROLLBAR_WIDTH,
			interfaceInfo.clientHeight - interfaceInfo.paddingSize*2,
			pListMenu->listMenuType == interfaceListMenuBigThumbnail ? pListMenu->infoAreaWidth * pListMenu->infoAreaHeight : pListMenu->baseMenu.menuEntryCount,
			maxVisibleItems,
			itemOffset);
	}
}

int interface_menuEntryDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i)
{
	if ( pMenu->menuType != interfaceMenuList )
	{
		eprintf("%s: unsupported menu type %d\n", __FUNCTION__, pMenu->menuType);
		return -1;
	}
	int selected                   = pMenu->selectedItem == i;
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t *)pMenu;
	switch ( pListMenu->listMenuType )
	{
		case interfaceListMenuBigThumbnail:
			/*if ( selected )
			{
				gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_HIGHLIGHT_RECT_RED, INTERFACE_HIGHLIGHT_RECT_GREEN, INTERFACE_HIGHLIGHT_RECT_BLUE, INTERFACE_HIGHLIGHT_RECT_ALPHA, x - interfaceInfo.paddingSize/2, y-interfaceInfo.paddingSize/2, INTERFACE_BIG_THUMBNAIL_SIZE+interfaceInfo.paddingSize, INTERFACE_BIG_THUMBNAIL_SIZE+interfaceInfo.paddingSize);
			}*/
#ifdef ENABLE_VIDIMAX
			vidimax_drawMainMenuIcons (pListMenu->baseMenu.menuEntry[i].thumbnail, rect, selected);
#else
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "big_background.png", rect->x, rect->y,
				INTERFACE_BIG_THUMBNAIL_SIZE, INTERFACE_BIG_THUMBNAIL_SIZE, 0, selected, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
#ifdef ENABLE_REGPLAT
			if (pListMenu->baseMenu.menuEntry[i].image == NULL ||
				interface_drawIcon(DRAWING_SURFACE, pListMenu->baseMenu.menuEntry[i].image,
							rect->x+INTERFACE_BIG_THUMBNAIL_SIZE/2, rect->y+INTERFACE_BIG_THUMBNAIL_SIZE/2,
							INTERFACE_BIG_THUMBNAIL_SIZE, INTERFACE_BIG_THUMBNAIL_SIZE,
							0, selected, DSBLIT_BLEND_ALPHACHANNEL,
							(interfaceAlignCenter|interfaceAlignMiddle)))
#endif
			interface_drawIcon(DRAWING_SURFACE,
				resource_thumbnailsBig[pListMenu->baseMenu.menuEntry[i].thumbnail] != NULL
				 ? resource_thumbnailsBig[pListMenu->baseMenu.menuEntry[i].thumbnail]
				 : resource_thumbnails   [pListMenu->baseMenu.menuEntry[i].thumbnail],
				rect->x+INTERFACE_BIG_THUMBNAIL_SIZE/2, rect->y+INTERFACE_BIG_THUMBNAIL_SIZE/2,
				INTERFACE_BIG_THUMBNAIL_SIZE, INTERFACE_BIG_THUMBNAIL_SIZE,
				0, selected, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle);
#endif // ENABLE_VIDIMAX
#if (defined STB225)
			if (selected && interfaceInfo.mode3D==1 && appControlInfo.outputInfo.has_3D_TV)
				gfx_fb1_draw_rect(rect->x, rect->y, INTERFACE_BIG_THUMBNAIL_SIZE, INTERFACE_BIG_THUMBNAIL_SIZE, 0x96);
#endif

			tprintf("%c%c%d|\t%d\n", selected ? '>' : ' ', pListMenu->baseMenu.menuEntry[i].isSelectable ? ' ' : '-', i, pListMenu->baseMenu.menuEntry[i].thumbnail);

			//interface_drawImage(DRAWING_SURFACE, resource_thumbnailsBig[pListMenu->baseMenu.menuEntry[i].thumbnail], rect->x, rect->y, INTERFACE_BIG_THUMBNAIL_SIZE, INTERFACE_BIG_THUMBNAIL_SIZE, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
			break;
		case interfaceListMenuNoThumbnail:
		case interfaceListMenuIconThumbnail:
			switch ( pListMenu->baseMenu.menuEntry[i].type )
			{
				case interfaceMenuEntryText:
					return interface_listEntryDisplay(pMenu, rect, i);
					return 0;
				case interfaceMenuEntryEdit:
					return interface_editEntryDisplay(pMenu, rect, i);
				default:
					eprintf("%s: unsupported entry type %d\n", __FUNCTION__, pListMenu->baseMenu.menuEntry[i].type);
					return -1;
			}
			break;
		default:
			eprintf("%s: unsupported list menu type %d\n", __FUNCTION__, pListMenu->listMenuType);
			return -1;
	}

	return 0;
}

static int interface_listEntryDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i)
{
	int selected                   = pMenu->selectedItem == i;
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t *)pMenu;
	int fh;
	int x,y;
	int r,g,b,a;
	char entryText[MENU_ENTRY_INFO_LENGTH];
	int maxWidth;
	char *second_line;
	if ( selected )
	{
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
		// selection rectangle
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, rect->x, rect->y, rect->w, rect->h);
#ifdef INTERFACE_DRAW_ARROW
		if ( pArrow != NULL )
		{
			//dprintf("%s: draw arrow\n", __FUNCTION__);
			if ( pListMenu->listMenuType == interfaceListMenuBigThumbnail || pListMenu->listMenuType == interfaceListMenuNoThumbnail )
			{
				x = interfaceInfo.clientX+interfaceInfo.paddingSize;
				y = interfaceInfo.clientY+(interfaceInfo.paddingSize+fh)*(itemDisplayIndex+1)-INTERFACE_ARROW_SIZE;
			} else
			{
				x = interfaceInfo.clientX+interfaceInfo.paddingSize;
				y = interfaceInfo.clientY+(interfaceInfo.paddingSize+pListMenu->baseMenu.thumbnailHeight)*(itemDisplayIndex+1)-(pListMenu->baseMenu.thumbnailHeight+INTERFACE_ARROW_SIZE)/2;
			}
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR INTERFACE_ARROW_IMAGE, x, y, INTERFACE_ARROW_SIZE, INTERFACE_ARROW_SIZE, 0, 0, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
		}
#endif
	}
	if ( pListMenu->listMenuType == interfaceListMenuIconThumbnail && pListMenu->baseMenu.menuEntry[i].thumbnail > 0 )
	{
		x = rect->x+interfaceInfo.paddingSize+/*INTERFACE_ARROW_SIZE +*/pListMenu->baseMenu.thumbnailWidth/2;
		y = rect->y+pListMenu->baseMenu.thumbnailHeight/2;
#ifdef ENABLE_REGPLAT
		if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatServicesMenu 
			&& strcmp(regplatMainMenu.baseMenu.menuEntry[regplatMainMenu.baseMenu.selectedItem].info, "Личный кабинет") != 0
			&& pListMenu->baseMenu.menuEntry[i].image != NULL)
		{
			template_t *templ = (template_t *)pListMenu->baseMenu.menuEntry[0].pArg;
			/* draw background */
			x = rect->x+interfaceInfo.paddingSize+pListMenu->baseMenu.thumbnailWidth;
			if (interface_drawImage(DRAWING_SURFACE, templ->sectionInfo.bgtemplate, x, y, 5*pMenu->thumbnailWidth/2, pMenu->thumbnailHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, (interfaceAlign_t)(interfaceAlignCenter|interfaceAlignMiddle), 0, 0) != 0)
					interface_drawImage(DRAWING_SURFACE, resource_thumbnails[pListMenu->baseMenu.menuEntry[i].thumbnail], x, y, 
										5*pMenu->thumbnailWidth/2, pMenu->thumbnailHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, 
										(interfaceAlign_t)(interfaceAlignCenter|interfaceAlignMiddle), 0, 0);
			/* draw image */
			if (interface_drawImage(DRAWING_SURFACE, pListMenu->baseMenu.menuEntry[i].image, x, y, 5*pMenu->thumbnailWidth/2, pMenu->thumbnailHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, (interfaceAlign_t)(interfaceAlignCenter|interfaceAlignMiddle), 0, 0) != 0)
					interface_drawImage(DRAWING_SURFACE, resource_thumbnails[pListMenu->baseMenu.menuEntry[i].thumbnail], x, y, 
										5*pMenu->thumbnailWidth/2, pMenu->thumbnailHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, 
										(interfaceAlign_t)(interfaceAlignCenter|interfaceAlignMiddle), 0, 0);
		}
		else
#endif
		if (pListMenu->baseMenu.menuEntry[i].image == NULL ||
			interface_drawImage(DRAWING_SURFACE, pListMenu->baseMenu.menuEntry[i].image,
			                    x, y, pMenu->thumbnailWidth, pMenu->thumbnailHeight, 0, NULL,
			                    DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0))
			interface_drawImage(DRAWING_SURFACE, resource_thumbnails[pListMenu->baseMenu.menuEntry[i].thumbnail],
			                    x, y, pMenu->thumbnailWidth, pMenu->thumbnailHeight, 0, NULL,
			                    DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle, 0, 0);
		//interface_drawIcon(DRAWING_SURFACE, pListMenu->baseMenu.menuEntry[i].thumbnail, x, y, pMenu->thumbnailWidth, pMenu->thumbnailHeight, 0, 0, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle);
	}
	//dprintf("%s: draw text\n", __FUNCTION__);
	if ( pListMenu->baseMenu.menuEntry[i].isSelectable )
	{
		//r = i == pListMenu->baseMenu.selectedItem ? INTERFACE_BOOKMARK_SELECTED_RED : INTERFACE_BOOKMARK_RED;
		//g = i == pListMenu->baseMenu.selectedItem ? INTERFACE_BOOKMARK_SELECTED_GREEN : INTERFACE_BOOKMARK_GREEN;
		//b = i == pListMenu->baseMenu.selectedItem ? INTERFACE_BOOKMARK_SELECTED_BLUE : INTERFACE_BOOKMARK_BLUE;
		//a = i == pListMenu->baseMenu.selectedItem ? INTERFACE_BOOKMARK_SELECTED_ALPHA : INTERFACE_BOOKMARK_ALPHA;
		r = INTERFACE_BOOKMARK_RED;
		g = INTERFACE_BOOKMARK_GREEN;
		b = INTERFACE_BOOKMARK_BLUE;
		a = INTERFACE_BOOKMARK_ALPHA;
	} else
	{
		r = INTERFACE_BOOKMARK_DISABLED_RED;
		g = INTERFACE_BOOKMARK_DISABLED_GREEN;
		b = INTERFACE_BOOKMARK_DISABLED_BLUE;
		a = INTERFACE_BOOKMARK_DISABLED_ALPHA;
	}
	pgfx_font->GetHeight(pgfx_font, &fh);
	second_line = strchr(pListMenu->baseMenu.menuEntry[i].info, '\n');
	if ( pListMenu->listMenuType == interfaceListMenuNoThumbnail )
	{
		maxWidth = interfaceInfo.clientWidth - interfaceInfo.paddingSize*4/*- INTERFACE_ARROW_SIZE*/ - INTERFACE_SCROLLBAR_WIDTH;
		x = rect->x+interfaceInfo.paddingSize;//+INTERFACE_ARROW_SIZE;
		y = rect->y + fh;
	} else
	{
		maxWidth = interfaceInfo.clientWidth - interfaceInfo.paddingSize*5/* - INTERFACE_ARROW_SIZE*/ - interfaceInfo.thumbnailSize - INTERFACE_SCROLLBAR_WIDTH;
		x = rect->x+interfaceInfo.paddingSize*2/*+INTERFACE_ARROW_SIZE*/+pListMenu->baseMenu.thumbnailWidth;
		if (second_line)
			y = rect->y + pListMenu->baseMenu.thumbnailHeight/2 - 2; // + fh/4;
		else
			y =rect->y + pListMenu->baseMenu.thumbnailHeight/2 + fh/4;
	}

	tprintf("%c%c%s\t\t\t|%d\n", i == pListMenu->baseMenu.selectedItem ? '>' : ' ', pListMenu->baseMenu.menuEntry[i].isSelectable ? ' ' : '-', pListMenu->baseMenu.menuEntry[i].info, pListMenu->baseMenu.menuEntry[i].thumbnail);
	if (second_line)
	{
		char *info_second_line = entryText + (second_line - pListMenu->baseMenu.menuEntry[i].info) + 1;
		int length;

		interface_getMenuEntryInfo(pMenu,i,entryText,MENU_ENTRY_INFO_LENGTH);

		*second_line = 0;
#ifdef ENABLE_REGPLAT
		if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatServicesMenu 
			&& strcmp(regplatMainMenu.baseMenu.menuEntry[regplatMainMenu.baseMenu.selectedItem].info, "Личный кабинет") != 0)
			gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x+pListMenu->baseMenu.thumbnailWidth+10, y, pListMenu->baseMenu.menuEntry[i].info, 0, i == pListMenu->baseMenu.selectedItem);
		else
#endif
			gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, pListMenu->baseMenu.menuEntry[i].info, 0, i == pListMenu->baseMenu.selectedItem);

		length = getMaxStringLengthForFont(pgfx_smallfont, info_second_line, maxWidth-fh);
		info_second_line[length] = 0;
		gfx_drawText(DRAWING_SURFACE, pgfx_smallfont, r, g, b, a, x+fh, y+fh, info_second_line, 0, i == pListMenu->baseMenu.selectedItem);
		*second_line = '\n';
	}
#ifdef ENABLE_REGPLAT
	if (interfaceInfo.currentMenu == (interfaceMenu_t*)&regplatServicesMenu
		&& strcmp(regplatMainMenu.baseMenu.menuEntry[regplatMainMenu.baseMenu.selectedItem].info, "Личный кабинет") != 0)
		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x+pListMenu->baseMenu.thumbnailWidth+10, y, pListMenu->baseMenu.menuEntry[i].info, 0, i == pListMenu->baseMenu.selectedItem);
	else
#endif
		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, pListMenu->baseMenu.menuEntry[i].info, 0, i == pListMenu->baseMenu.selectedItem);

	return 0;
}

int getMaxStringLengthForFont(IDirectFBFont *font, const char *string, int maxWidth)
{
	int total_length, width, pos, last_length, real_length;

	total_length = 0;
	real_length = strlen(string);

	DFBCHECK( font->GetStringWidth(font, string, -1, &width) );

	/* Do we have a short string */
	if (width <= maxWidth)
	{
		return real_length;
	}

	/* Try guessing the expected length */
	pos = maxWidth*(real_length-6)/width;
	if (pos > 0 && pos < real_length)
	{
		while ((string[pos] & 0xC0) == 0x80)
		{
			pos++;
		}
		DFBCHECK( font->GetStringWidth(font, string, pos, &width) );
		if (width <= maxWidth)
		{
			total_length = pos;
		}
	}

	/* Count symbols in string and check length */
	while (string[total_length] != 0)
	{
		last_length = total_length;
		total_length++;
		while ((string[total_length] & 0xC0) == 0x80)
		{
			total_length++;
		}
		DFBCHECK( font->GetStringWidth(font, string, total_length, &width) );
		if (width > maxWidth)
		{
			return last_length;
		}
	}

	return total_length;
}

int getMaxStringLengthForFontWW(IDirectFBFont *font, const char *string, int maxWidth)
{
	int len;
	const char *tmp;
	const char *ptr = string;

	tmp = strchr(ptr, '\n');

	len = getMaxStringLengthForFont(font, ptr, maxWidth);

	if (tmp == NULL || tmp-ptr > len)
	{
		tmp = &ptr[len];
	} else
	{
		len = tmp-ptr;
	}
	if (ptr[len] != 0)
	{
		while (tmp > ptr && *tmp != ' ' && ptr[len] != '\n')
		{
			tmp--;
		}
		if (tmp > ptr)
		{
			len = tmp-ptr;
			tmp++;
		} else
		{
			tmp = &ptr[len];
		}
	}

	while (ptr[len] == ' ')
	{
		len++;
	}	

	return len;
}

int getMaxStringLength(const char *string, int maxWidth)
{
	return getMaxStringLengthForFont(pgfx_font, string, maxWidth);
}

#if 0
int getLeftStringOverflow(char *string, int maxWidth)
{
	int overflow = 0;
	DFBRectangle rectangle;

	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, string, -1, &rectangle, NULL) );
	while ( rectangle.w-rectangle.x > maxWidth && string[0] != 0 )
	{
		/* ensure that we don't stop in the middle of utf-8 multibyte symbol */
		do
		{
			overflow++;
			string = &string[1];
		} while ( (string[0] & 0xC0) == 0x80 || string[0] == ' ' );
		DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, string, -1, &rectangle, NULL) );
	}
	return overflow;
}
#endif

void smartLineTrim(char *string, int length)
{
	for ( string[length--] = 0; length >= 0 && ((string[length] & 0xC0) == 0x80 || string[length] <= ' '); length-- )
		string[length] = 0;
}

static void interface_reinitializeListMenu(interfaceMenu_t *pMenu)
{
	int i, length, maxWidth;
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t*)pMenu;

	if ( pListMenu->listMenuType == interfaceListMenuBigThumbnail )
	{
		pListMenu->baseMenu.thumbnailWidth = pListMenu->infoAreaWidth-interfaceInfo.paddingSize*2;
		pListMenu->baseMenu.thumbnailHeight = pListMenu->infoAreaHeight-interfaceInfo.paddingSize*2;
	} else if ( pListMenu->listMenuType == interfaceListMenuIconThumbnail )
	{
		pListMenu->baseMenu.thumbnailWidth = interfaceInfo.thumbnailSize;
		pListMenu->baseMenu.thumbnailHeight = interfaceInfo.thumbnailSize;
	}

	if ( pListMenu->listMenuType == interfaceListMenuBigThumbnail )
	{
		maxWidth =  interfaceInfo.clientWidth/2 - interfaceInfo.paddingSize*4/*- INTERFACE_ARROW_SIZE*/ - INTERFACE_SCROLLBAR_WIDTH;
	} else if ( pListMenu->listMenuType == interfaceListMenuIconThumbnail )
	{
		maxWidth = interfaceInfo.clientWidth - interfaceInfo.paddingSize*5/* - INTERFACE_ARROW_SIZE*/ - interfaceInfo.thumbnailSize - INTERFACE_SCROLLBAR_WIDTH;
	} else
	{
		maxWidth = interfaceInfo.clientWidth - interfaceInfo.paddingSize*4/*- INTERFACE_ARROW_SIZE*/ - INTERFACE_SCROLLBAR_WIDTH;
	}

	//dprintf("%s: maxWidth %d, clientWidth %d\n", __FUNCTION__, maxWidth, interfaceInfo.clientWidth);

	for ( i=0; i<pListMenu->baseMenu.menuEntryCount; i++ )
	{
		if ( pListMenu->baseMenu.menuEntry[i].infoReplacedChar != 0 )
		{
			pListMenu->baseMenu.menuEntry[i].info[pListMenu->baseMenu.menuEntry[i].infoReplacedCharPos] = pListMenu->baseMenu.menuEntry[i].infoReplacedChar;
		}

#if 0
		length = strlen(pListMenu->baseMenu.menuEntry[i].info)+1;

		do
		{
			/* ensure that we don't stop in the middle of utf-8 multibyte symbol */
			do
			{
				length--;
			} while ( (pListMenu->baseMenu.menuEntry[i].info[length] & 0xC0) == 0x80 );

			val = pListMenu->baseMenu.menuEntry[i].info[length];
			pListMenu->baseMenu.menuEntry[i].info[length] = 0;
			DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, pListMenu->baseMenu.menuEntry[i].info, -1, &rectangle, NULL) );
			pListMenu->baseMenu.menuEntry[i].info[length] = val;
		} while ( rectangle.w-rectangle.x > maxWidth && length > 0 );
#endif

		length = getMaxStringLength(pListMenu->baseMenu.menuEntry[i].info, maxWidth);

		pListMenu->baseMenu.menuEntry[i].infoReplacedCharPos = length;
		pListMenu->baseMenu.menuEntry[i].infoReplacedChar = pListMenu->baseMenu.menuEntry[i].info[length];
		pListMenu->baseMenu.menuEntry[i].info[length] = 0;
	}
}

void interface_menuReset(interfaceMenu_t *pMenu)
{
	if ( pMenu->reinitializeMenu != NULL )
	{
		pMenu->reinitializeMenu(pMenu);
	}
}

void interface_setMenuLogo(interfaceMenu_t *pMenu, int logo, int x, int y, int w, int h)
{
	//STRMAXCPY(pMenu->logo, logo, SHORT_PATH);
	pMenu->logo = logo;
	pMenu->logoX = x;
	pMenu->logoY = y;
	pMenu->logoWidth = w;
	pMenu->logoHeight = h;
}

void interface_setMenuImage(interfaceMenu_t *pMenu, char *image, int x, int y, int w, int h)
{
	pMenu->logo = logo;
	pMenu->logoX = x;
	pMenu->logoY = y;
	pMenu->logoWidth = w;
	pMenu->logoHeight = h;
}

inline void interface_setCustomKeysCallback(interfaceMenu_t *pMenu, menuConfirmFunction pCallback)
{
	pMenu->pCustomKeysCallback = pCallback;
}

void interface_setCustomDisplayCallback(interfaceMenu_t *pMenu, menuEventFunction pCallback)
{
	pMenu->displayMenu = pCallback;
}

static int interface_hideSliderControl(void *pArg)
{
	interface_sliderShow(0, 1);

	return 0;
}

static int interface_sliderCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	long val, step;

	if (interfaceSlider.customKeyCallback != NULL)
	{
		if (interfaceSlider.customKeyCallback(pMenu, cmd, pArg) != 0)
		{
			return 0;
		}
	}

	if (cmd->command == interfaceCommandExit)
	{
		interface_hideSliderControl(pArg);
		return 0;
	} else if (cmd->command == interfaceCommandLeft || cmd->command == interfaceCommandRight)
	{
		if (interfaceSlider.getCallback != NULL && interfaceSlider.setCallback != NULL)
		{
			val = interfaceSlider.getCallback(pArg);
			step = (interfaceSlider.maxValue-interfaceSlider.minValue)/interfaceSlider.divisions;
			val += (cmd->command == interfaceCommandLeft ? -1 : 1)*step;
			if (val < interfaceSlider.minValue)
			{
				val = interfaceSlider.minValue;
			} else if (val > interfaceSlider.maxValue)
			{
				val = interfaceSlider.maxValue;
			}
			interfaceSlider.setCallback(val, pArg);
			interface_sliderShow(2, 1);
		}
	}

	return 0;
}

static void interface_displayStatusbar()
{
	int i, x, y, n;

#ifdef ENABLE_PVR
	if ( pvr_getActive() )
	{
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "icons_state.png", interfaceInfo.clientX + interfaceInfo.clientWidth - INTERFACE_CLOCK_DIGIT_WIDTH*4 - INTERFACE_CLOCK_COLON_WIDTH - INTERFACE_ROUND_CORNER_RADIUS/2 - INTERFACE_THUMBNAIL_SIZE, interfaceInfo.screenHeight - interfaceInfo.marginSize + INTERFACE_THUMBNAIL_SIZE/2, INTERFACE_THUMBNAIL_SIZE, INTERFACE_THUMBNAIL_SIZE, 0, ICONS_STATE_REC, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle);
	}
#endif

	if ( interfaceInfo.keypad.enable != 0 )
	{
		return;
	}
	
	/*// Half-opaque bar
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, 0, interfaceInfo.screenHeight - interfaceInfo.marginSize, interfaceInfo.screenWidth, interfaceInfo.marginSize);*/

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

	y = interfaceInfo.screenHeight - interfaceInfo.marginSize + 2*interfaceInfo.paddingSize;

	/*if ( interfaceInfo.messageBox.type != interfaceMessageBoxNone )
	{
		n = (interfaceInfo.messageBox.type == interfaceMessageBoxCallback && interfaceInfo.messageBox.pCallback == interface_enterTextCallback) ? 3 : 2;
		x = interfaceInfo.marginSize + interfaceInfo.paddingSize;//center: (interfaceInfo.screenWidth - ( n * INTERFACE_STATUSBAR_ICON_WIDTH + (n-1) * 3*interfaceInfo.paddingSize )) / 2;

		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[statusbar_f1_cancel], x, y, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
		x += INTERFACE_STATUSBAR_ICON_WIDTH + interfaceInfo.paddingSize * 3;
		interface_drawImage(DRAWING_SURFACE, resource_thumbnails[statusbar_f2_ok], x, y, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
		if ( n == 3 )
		{
			x += INTERFACE_STATUSBAR_ICON_WIDTH + interfaceInfo.paddingSize * 3;
			interface_drawImage(DRAWING_SURFACE, resource_thumbnails[statusbar_f3_keyboard], x, y, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
		}
	} else */
	if ( interfaceInfo.messageBox.type == interfaceMessageBoxNone )
	{
		n = 0;
		for ( i = 0; i < 4; i++ )
		{
			if ( interfaceInfo.currentMenu->statusBarIcons[i] > 0 )
			{
				n++;
			}
		}
		if ( n == 0 )
		{
			return;
		}
		x = interfaceInfo.marginSize - INTERFACE_BORDER_WIDTH;//center: (interfaceInfo.screenWidth - ( n * INTERFACE_STATUSBAR_ICON_WIDTH + (n-1) * 2*interfaceInfo.paddingSize )) / 2;
		for ( i = 0; i < 4; i++ )
		{
			if ( interfaceInfo.currentMenu->statusBarIcons[i] > 0 )
			{
				interface_drawImage(DRAWING_SURFACE, resource_thumbnails[interfaceInfo.currentMenu->statusBarIcons[i]], x, y, INTERFACE_STATUSBAR_ICON_WIDTH, INTERFACE_STATUSBAR_ICON_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, 0, 0);
			}
			x += INTERFACE_STATUSBAR_ICON_WIDTH + interfaceInfo.paddingSize * 2;
		}
	}
}

static void interface_displayCall()
{
#if 0
	int x, y, fh, fa;
	DFBRectangle rectangle;
	int strLen;
	static char sip[MAX_URL];
	char *str, *ptr;

	if ( interfaceInfo.enableVoipIndication == 1 && interfaceInfo.showIncomingCall == 1 )
	{
		x = interfaceInfo.screenWidth - interfaceInfo.marginSize;
		y = interfaceInfo.marginSize;
		interface_drawImage(pgfx_frameBuffer, IMAGE_DIR "icon_incoming_call.png", x + interfaceInfo.paddingSize, y, 0, 0, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignRight|interfaceAlignTop, NULL, NULL);
		str = index(appControlInfo.voipInfo.lastSip, '<');
		if ( str && str != appControlInfo.voipInfo.lastSip ) // name present in specified address
		{
			strLen = str - appControlInfo.voipInfo.lastSip - 1; // removing space between name and <sip:*>
			if ( appControlInfo.voipInfo.lastSip[0] == '"' )
			{
				strLen-=2;
				if ( strLen > 0 )
					strncpy(sip, &appControlInfo.voipInfo.lastSip[1], strLen );
				else
				{
					strLen = strlen(str);
					strncpy(sip, str, strLen );
					str = index( sip, '>' );
					if ( str )
						strLen = str - sip;
				}
			}
			else
				strncpy(sip, appControlInfo.voipInfo.lastSip, strLen );
			sip[strLen] = 0;
		} else
		{
			if ( str )
				str++;
			else
				str = appControlInfo.voipInfo.lastSip;
			ptr = strstr( str, "sip:" );
			if ( ptr )
				str = ptr+4;
			strcpy(sip, str);
			str = index(sip, '>');
			if (str)
				*str = 0;
			str = index(sip, '@');
			if ( str && strcasecmp( appControlInfo.voipInfo.server, str+1 ) == 0 )
				*str = 0;
		}
		
		strLen = getMaxStringLength( sip, x - interfaceInfo.marginSize - INTERFACE_THUMBNAIL_SIZE );
		DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, sip, -1, &rectangle, NULL) );
		x -= rectangle.w + INTERFACE_THUMBNAIL_SIZE;
		y += (INTERFACE_THUMBNAIL_SIZE - rectangle.h)/2;
		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_MESSAGE_BOX_RED, INTERFACE_MESSAGE_BOX_GREEN, INTERFACE_MESSAGE_BOX_BLUE, INTERFACE_MESSAGE_BOX_ALPHA, x, y, rectangle.w, rectangle.h);
		interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_MESSAGE_BOX_BORDER_RED, INTERFACE_MESSAGE_BOX_BORDER_GREEN, INTERFACE_MESSAGE_BOX_BORDER_BLUE, INTERFACE_MESSAGE_BOX_BORDER_ALPHA, x, y, rectangle.w, rectangle.h, interfaceInfo.borderWidth, interfaceBorderSideAll);
		DFBCHECK( pgfx_font->GetHeight(pgfx_font, &fh) );
		DFBCHECK( pgfx_font->GetAscender(pgfx_font, &fa) );
		gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, x, y+fa, sip, 0, 0);
	}
#else
	if ( interfaceInfo.enableVoipIndication == 1 && interfaceInfo.showIncomingCall == 1 )
	{
		interface_drawImage(pgfx_frameBuffer, IMAGE_DIR "icon_incoming_call.png", interfaceInfo.screenWidth - interfaceInfo.marginSize + interfaceInfo.paddingSize, interfaceInfo.marginSize, 0, 0, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignRight|interfaceAlignTop, NULL, NULL);
	}
#endif
}

#ifdef ENABLE_MESSAGES
static void interface_displayMessageNotify()
{
	if ( appControlInfo.messagesInfo.newMessage )
	{
		interface_drawImage(pgfx_frameBuffer, IMAGE_DIR "thumbnail_message_new.png", interfaceInfo.screenWidth - interfaceInfo.marginSize + interfaceInfo.paddingSize, interfaceInfo.marginSize, 0, 0, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignRight|interfaceAlignTop, NULL, NULL);
	}
}
#endif

interfaceMenu_t *createBasicMenu( interfaceMenu_t *pMenu, 
							interfaceMenuType_t type, 
							const char *name, 
							int logo, 
							const int* statusIcons, 
							interfaceMenu_t *pParent, 
							menuProcessCommandFunction processCommand, 
							menuEventFunction displayCommand, 
							menuEventFunction reinitializeCommand, 
							menuActionFunction pActivatedAction, 
							menuActionFunction pDeactivatedAction, 
							void *pArg)
{
	int i;

	pMenu->menuType = type;

	if ( pParent != NULL )
	{
		if ( pParent->pParentMenu != NULL )
		{
			pMenu->selectedItem = MENU_ITEM_BACK;
		} else
		{
			pMenu->selectedItem = MENU_ITEM_MAIN;
		}
	} else
	{
		pMenu->selectedItem = 0;
	}

	pMenu->processCommand = processCommand != NULL ? processCommand : interface_MenuDefaultProcessCommand;
	pMenu->pCustomKeysCallback = NULL;
	pMenu->displayMenu = displayCommand;
	pMenu->reinitializeMenu = reinitializeCommand;

	pMenu->pActivatedAction = pActivatedAction;
	pMenu->pDeactivatedAction = pDeactivatedAction;

	pMenu->pArg = pArg;

	pMenu->menuEntryCount = 0;

	pMenu->pParentMenu = pParent;

	if (name != NULL)
	{
		strcpy(pMenu->name, name);
	} else
	{
		pMenu->name[0] = 0;
	}

	pMenu->logo = logo;
	pMenu->logoX = -1;

	if (statusIcons != NULL)
	{
		for ( i = 0; i < 4; ++i )
		{
			pMenu->statusBarIcons[i] = statusIcons[i];
		}
	}
	
	interface_clearMenuEntries(pMenu);

	interface_menuReset(pMenu);

	return pMenu;
}

interfaceListMenu_t * createListMenu( interfaceListMenu_t *pMenu,
									  const char *name,
									  int logo,
									  const int* statusIcons,
									  interfaceMenu_t *pParent, /*int x, int y, int w, int h,*/ 
									  interfaceListMenuType_t listMenuType,
									  menuActionFunction pActivatedAction,
									  menuActionFunction pDeactivatedAction,
									  void *pArg)
{
	createBasicMenu((interfaceMenu_t*)pMenu, interfaceMenuList,
					name, logo, statusIcons, pParent,
					interface_listMenuProcessCommand,
					interface_listMenuDisplay,
					interface_reinitializeListMenu,
					pActivatedAction, pDeactivatedAction, pArg);

	pMenu->infoAreaX = interfaceInfo.clientX+interfaceInfo.clientWidth/2;
	pMenu->infoAreaY = interfaceInfo.clientY;
	pMenu->infoAreaWidth = interfaceInfo.clientWidth/2;
	pMenu->infoAreaHeight = interfaceInfo.clientHeight;
	pMenu->listMenuType = listMenuType;

	return pMenu;
}

int interface_addMenuEntryCustom(interfaceMenu_t *pMenu, 
							interfaceMenuEntryType_t type, 
							const void *data, 
							size_t dataSize, 
							int isSelectable, 
							menuActionFunction pFunc, 
							menuActionFunction pSelectedFunc, 
							menuActionFunction pDeselectedFunc, 
							menuEntryDisplayFunction pDisplay, 
							void *pArg, 
							int thumbnail)
{
	if ( pMenu->menuEntryCount < MENU_MAX_ENTRIES )
	{
		dataSize = dataSize > MENU_ENTRY_INFO_LENGTH-1 ? MENU_ENTRY_INFO_LENGTH-1 : dataSize;
		memcpy(pMenu->menuEntry[pMenu->menuEntryCount].info, data, dataSize );
		/* enshure there's zero at the end of a string */
		pMenu->menuEntry[pMenu->menuEntryCount].info[dataSize] = 0;
		pMenu->menuEntry[pMenu->menuEntryCount].pArg = pArg;
		pMenu->menuEntry[pMenu->menuEntryCount].type = type;

		pMenu->menuEntry[pMenu->menuEntryCount].isSelectable = isSelectable;

		pMenu->menuEntry[pMenu->menuEntryCount].infoReplacedChar = 0;

		if ( pFunc != NULL && (unsigned int)(intptr_t)pFunc < menuDefaultActionCount )
		{
			switch ( (interfaceMenuActionType_t)pFunc )
			{
				case menuDefaultActionShowMenu:
					pMenu->menuEntry[pMenu->menuEntryCount].pAction = interface_menuActionShowMenu;
					break;
				default:
					pMenu->menuEntry[pMenu->menuEntryCount].pAction = pFunc;
			}
		} else
		{
			pMenu->menuEntry[pMenu->menuEntryCount].pAction = pFunc;
		}

		pMenu->menuEntry[pMenu->menuEntryCount].pSelectedAction = pSelectedFunc;
		pMenu->menuEntry[pMenu->menuEntryCount].pDeselectedAction = pDeselectedFunc;
		pMenu->menuEntry[pMenu->menuEntryCount].pDisplay = pDisplay ? pDisplay : interface_menuEntryDisplay;

		pMenu->menuEntry[pMenu->menuEntryCount].thumbnail = thumbnail;
		pMenu->menuEntry[pMenu->menuEntryCount].image     = 0;

		pMenu->menuEntryCount++;

		interface_menuReset(pMenu);

		return pMenu->menuEntryCount;
	}

	return -1;
}

inline int interface_addMenuEntry(interfaceMenu_t *pMenu, 
								const char *text, 
								menuActionFunction pFunc, 
								void *pArg, 
								int thumbnail)
{
	return interface_addMenuEntryCustom (pMenu,
						interfaceMenuEntryText, text, strlen(text)+1,
						1 /* selectable */,
						pFunc, NULL, NULL, NULL, pArg,
						thumbnail);
}

inline int interface_addMenuEntryDisabled(interfaceMenu_t *pMenu, const char *text, int thumbnail)
{
	return interface_addMenuEntryCustom (pMenu, 
						interfaceMenuEntryText, text, strlen(text)+1, 
						0 /* not selectable */, 
						NULL, NULL, NULL, NULL, NULL, 
						thumbnail);
}

void interface_clearMenuEntries(interfaceMenu_t *pMenu)
{
	pMenu->menuEntryCount = 0;
}

int interface_setSelectedItem(interfaceMenu_t *pMenu, int index)
{
	int old_index = pMenu->selectedItem;

	pMenu->selectedItem = index;

	return old_index;
}

int interface_showSplash(int x, int y, int w, int h, int animate, int showMenu)
{
	IDirectFBImageProvider *pImageProvider = NULL;
	DFBRectangle rect;
	int frame;
	int frameCount = animate ? INTERFACE_SPLASH_FRAMES : 0;

	//dprintf("%s: x %d y %d w %d h %d animate %d, show %d\n", __FUNCTION__, x, y, w, h, animate, showMenu);	

	if ( !interface_splash )
	{
		DFBSurfaceDescription imageDesc;
		DFBRectangle          actualRect;
		const char *img_source;
#ifdef ENABLE_VIDIMAX
		img_source = "/usr/local/share/vidimax/images/splash.jpg";
#else
		img_source = IMAGE_DIR INTERFACE_SPLASH_IMAGE;
#endif
		if (pgfx_dfb->CreateImageProvider(pgfx_dfb, img_source, &pImageProvider) != DFB_OK )
			return -2;
 
		DFBCHECKLABEL(pImageProvider->GetSurfaceDescription(pImageProvider, &imageDesc), decode_error);
#ifdef STSDK
		imageDesc.flags |= DSDESC_PIXELFORMAT;
		imageDesc.pixelformat = DSPF_RGB24;
#endif
		/* Resize image to specified width and height */
		if ( w != 0 )
			imageDesc.width = w;
		if ( h != 0 )
			imageDesc.height = h;
		DFBCHECKLABEL(pgfx_dfb->CreateSurface(pgfx_dfb, &imageDesc, &interface_splash), decode_error);
		actualRect.w = w;
		actualRect.h = h;
		actualRect.x = actualRect.y = 0;
		/* Render the image */
		DFBCHECKLABEL(pImageProvider->RenderTo(pImageProvider,interface_splash, &actualRect), render_error);
		pImageProvider->Release(pImageProvider);
	}

	frame = 0;
	do
	{
		if ( frameCount > 0 && showMenu )
		{
			interface_displayMenu(0);
		} else
		{
			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
			gfx_clearSurface(DRAWING_SURFACE, interfaceInfo.screenWidth, interfaceInfo.screenHeight);
		}

		rect.x = (frameCount == 0 ? 0 : w*frame/frameCount/2);
		rect.y = (frameCount == 0 ? 0 : h*frame/frameCount/2);
		rect.w = w-rect.x*2;
		rect.h = h-rect.y*2;

		//dprintf("%s: %d blit %d %d %d %d to %d %d\n", __FUNCTION__, frame, rect.x, rect.y, rect.w, rect.h, x+rect.x, y+rect.y);

		DFBCHECK(DRAWING_SURFACE->SetBlittingFlags(DRAWING_SURFACE, DSBLIT_NOFX));
		DFBCHECK(DRAWING_SURFACE->Blit(DRAWING_SURFACE, interface_splash, &rect, x+rect.x, y+rect.y));

		//usleep(1);

		//dprintf("%s: flip\n", __FUNCTION__);

		interface_flipSurface();

		//dprintf("%s: next\n", __FUNCTION__);

		frame++;
	} while ( frame < frameCount );

	if ( frameCount > 0 && showMenu )
	{
		interface_displayMenu(1);
	}
	return 0;

render_error:
	eprintf("%s: Can't render splash image\n", __FUNCTION__);
	interface_splash->Release(interface_splash);
	interface_splash = NULL;
decode_error:
	eprintf("%s: Failed to decode splash image'\n", __FUNCTION__);
	pImageProvider->Release(pImageProvider);
	return -1;
}

void interface_splashCleanup(void)
{
	if ( interface_splash != NULL )
	{
		interface_splash->Release( interface_splash );
		interface_splash = NULL;
	}
}

int interface_drawImage(IDirectFBSurface *pSurface, const char *path, int x, int y, int w, int h,
						int stretch, 
						const DFBRectangle *clip,
						DFBSurfaceBlittingFlags blend,
						interfaceAlign_t align,
						int *rw, int *rh)
{
	IDirectFBSurface *pImg;
	int iw, ih;

	//dprintf("%s: %s\n", __FUNCTION__, path);
	
	pImg = gfx_decodeImage(path, w, h, stretch);

	if ( pImg )
	{

		pImg->GetSize(pImg, &iw, &ih);
		if ( clip != NULL )
		{
			iw = iw > clip->w ? clip->w : iw;
			ih = ih > clip->h ? clip->h : ih;
		}

		switch ( align&(interfaceAlignBottom|interfaceAlignMiddle|interfaceAlignTop) )
		{
			case interfaceAlignBottom: y -= ih; break;
			case interfaceAlignMiddle: y -= ih/2; break;
			case interfaceAlignTop:
			default:;
		}

		switch ( align&(interfaceAlignRight|interfaceAlignCenter|interfaceAlignLeft) )
		{
			case interfaceAlignRight: x -= iw; break;
			case interfaceAlignCenter: x -= iw/2; break;
			case interfaceAlignLeft:
			default:;
		}

		DFBCHECK(pSurface->SetBlittingFlags(pSurface, blend));
		DFBCHECK(pSurface->Blit(pSurface, pImg, clip, x, y));
		DFBCHECK(pSurface->SetBlittingFlags(pSurface, DSBLIT_NOFX));

		if ( rw != 0 )
		{
			*rw = iw;
		}
		if ( rh != 0 )
		{
			*rh = ih;
		}

		//dprintf("%s: done\n", __FUNCTION__);

		return 0;
	}

	return -1;
}

int interface_drawIcon(IDirectFBSurface *pSurface,
					   const char *path,
					   int x, int y, int w, int h,
					   int row, int col,
					   DFBSurfaceBlittingFlags blend,
					   interfaceAlign_t align)
{
	DFBRectangle clip;

	clip.x = w*col;
	clip.y = h*row;
	clip.w = w;
	clip.h = h;

	//dprintf("%s: clip %d %d %d %d\n", __FUNCTION__, clip.x, clip.y, clip.w, clip.h);	

	return interface_drawImage(pSurface, path, x, y, 0, 0, 0, &clip, blend, align, 0, 0);
}

#define DRAW_4STATE_BUTTON(btnid, img, num)	if (interfacePlayControl.enabledButtons & btnid) { \
											if (interfacePlayControl.activeButton == btnid) \
											{ \
												row = 1; \
											} else \
											{ \
												row = 0; \
											} \
											if (interfacePlayControl.highlightedButton == btnid) \
											{ \
												row += 2; \
											} \
											tprintf("[%c%c%s]", interfacePlayControl.highlightedButton == btnid ? '>' : ' ', interfacePlayControl.activeButton == btnid ? '*' : ' ', #btnid); \
											interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR img, x, y, INTERFACE_PLAY_CONTROL_BUTTON_WIDTH, INTERFACE_PLAY_CONTROL_BUTTON_HEIGHT, row, num, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft); \
											x += INTERFACE_PLAY_CONTROL_BUTTON_WIDTH+interfaceInfo.paddingSize; \
										}

#define DRAW_2STATE_BUTTON(btnid, img, num)	if (interfacePlayControl.enabledButtons & btnid) { \
											if (interfacePlayControl.highlightedButton == btnid) \
											{ \
												row = 1; \
											} else \
											{ \
												row = 0; \
											} \
											tprintf("[%c %s]", interfacePlayControl.highlightedButton == btnid ? '>' : ' ', #btnid); \
											interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR img, x, y, INTERFACE_PLAY_CONTROL_BUTTON_WIDTH, INTERFACE_PLAY_CONTROL_BUTTON_HEIGHT, row, num, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft); \
											x += INTERFACE_PLAY_CONTROL_BUTTON_WIDTH+interfaceInfo.paddingSize; \
										}

static void interface_displayPlayControl()
{

	int x, y, row, num;

	//dprintf("%s: Show state %d\n", __FUNCTION__, interfacePlayControl.showState);

	if ( interfaceChannelControl.pSet != NULL && interfaceChannelControl.showingLength > 0 )
	{
		interface_displayTextBox( interfaceInfo.screenWidth - interfaceInfo.marginSize + interfaceInfo.paddingSize + 22, interfaceInfo.marginSize, interfaceChannelControl.number, NULL, 0, NULL, 0 );
	}

	if ( !interfaceInfo.showMenu && interfacePlayControl.description[0] != 0 && ((interfacePlayControl.enabled && interfacePlayControl.visibleFlag) || interfaceSlideshowControl.visibleFlag || interfacePlayControl.showState) )
	{
		tprintf("--- %s ---\n", interfacePlayControl.description);
		interface_displayTextBox(interfaceInfo.screenWidth/2, interfaceInfo.marginSize,
								 interfacePlayControl.description,
								 appControlInfo.playbackInfo.thumbnail[0] ? appControlInfo.playbackInfo.thumbnail : resource_thumbnails[interfacePlayControl.descriptionThumbnail],
								 interfaceInfo.screenWidth - 2*interfaceInfo.marginSize,
								 NULL, 0);
	}

	if ( interfacePlayControl.enabled )
	{
		int n = 0;

		n += (interfacePlayControl.enabledButtons & interfacePlayControlPlay) != 0;
		n += (interfacePlayControl.enabledButtons & interfacePlayControlPause) != 0;
		n += (interfacePlayControl.enabledButtons & interfacePlayControlStop) != 0;
		n += (interfacePlayControl.enabledButtons & interfacePlayControlRewind) != 0;
		n += (interfacePlayControl.enabledButtons & interfacePlayControlFastForward) != 0;
		n += (interfacePlayControl.enabledButtons & interfacePlayControlPrevious) != 0;
		n += (interfacePlayControl.enabledButtons & interfacePlayControlNext) != 0;
#ifdef ENABLE_PVR
		n += (interfacePlayControl.enabledButtons & interfacePlayControlRecord) != 0;
#endif

		interfacePlayControl.width = interfaceInfo.paddingSize+n*(INTERFACE_PLAY_CONTROL_BUTTON_WIDTH+interfaceInfo.paddingSize);
		interfacePlayControl.height = INTERFACE_PLAY_CONTROL_BUTTON_HEIGHT+interfaceInfo.paddingSize*2;
		interfacePlayControl.positionX = (interfaceInfo.screenWidth - interfacePlayControl.width)/2;
		interfacePlayControl.positionY = interfaceInfo.screenHeight-interfaceInfo.marginSize-interfacePlayControl.height;

		if ( !interfaceInfo.showMenu &&  interfacePlayControl.visibleFlag )
		{
			/*DFBRectangle clip;
			clip.x = interfacePlayControl.positionX;
			clip.y = interfacePlayControl.positionY;
			clip.w = interfacePlayControl.width;
			clip.h = interfacePlayControl.height;
			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
			//gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_PLAY_CONTROL_BACKGROUND_RED, INTERFACE_PLAY_CONTROL_BACKGROUND_GREEN, INTERFACE_PLAY_CONTROL_BACKGROUND_BLUE, INTERFACE_PLAY_CONTROL_BACKGROUND_ALPHA, interfacePlayControl.positionX, interfacePlayControl.positionY, interfacePlayControl.width, interfacePlayControl.height);
			//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "background.png", 0, 0, interfaceInfo.screenWidth, interfaceInfo.screenHeight, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
			//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "background.png", interfacePlayControl.positionX, interfacePlayControl.positionY, interfaceInfo.screenWidth, interfaceInfo.screenHeight, 0, &clip, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
			if (interfaceInfo.inputFocus != inputFocusSlider)
			{
				interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_HIGHLIGHT_BORDER_RED, INTERFACE_HIGHLIGHT_BORDER_GREEN, INTERFACE_HIGHLIGHT_BORDER_BLUE, INTERFACE_HIGHLIGHT_BORDER_ALPHA, interfacePlayControl.positionX, interfacePlayControl.positionY, interfacePlayControl.width, interfacePlayControl.height, interfaceInfo.borderWidth*2, interfaceBorderSideAll);
			} else
			{
				//interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_PLAY_CONTROL_BORDER_RED, INTERFACE_PLAY_CONTROL_BORDER_GREEN, INTERFACE_PLAY_CONTROL_BORDER_BLUE, INTERFACE_PLAY_CONTROL_BORDER_ALPHA, interfacePlayControl.positionX, interfacePlayControl.positionY, interfacePlayControl.width, interfacePlayControl.height, interfaceInfo.borderWidth, interfaceBorderSideAll);
			}*/
			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );

			x = interfacePlayControl.positionX+interfaceInfo.paddingSize;
			y = interfacePlayControl.positionY+interfaceInfo.paddingSize;

			//dprintf("%s: display play control\n", __FUNCTION__);

			DRAW_2STATE_BUTTON(interfacePlayControlPrevious,    "button2.png", 0)
			DRAW_4STATE_BUTTON(interfacePlayControlRewind,      "button4.png", 0)
			DRAW_4STATE_BUTTON(interfacePlayControlPlay,        "button4.png", 1)
			DRAW_4STATE_BUTTON(interfacePlayControlPause,       "button4.png", 2)
			DRAW_4STATE_BUTTON(interfacePlayControlStop,        "button4.png", 3)
			DRAW_4STATE_BUTTON(interfacePlayControlFastForward, "button4.png", 4)
			DRAW_2STATE_BUTTON(interfacePlayControlNext,        "button2.png", 1)
#ifdef ENABLE_PVR
			if (interfacePlayControl.enabledButtons & interfacePlayControlRecord)
			{
				if (pvr_getActive())
				{
					row = 1;
				} else
				{
					row = 0;
				}
				if (interfacePlayControl.highlightedButton == interfacePlayControlRecord)
				{
					row += 2;
				}
				tprintf("[%c%c%s]", interfacePlayControl.highlightedButton == interfacePlayControlRecord ? '>' : ' ', interfacePlayControl.activeButton == interfacePlayControlRecord ? '*' : ' ', "interfacePlayControlRecord");
				interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "button4.png", x, y, INTERFACE_PLAY_CONTROL_BUTTON_WIDTH, INTERFACE_PLAY_CONTROL_BUTTON_HEIGHT, row, 5, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
				x += INTERFACE_PLAY_CONTROL_BUTTON_WIDTH+interfaceInfo.paddingSize;
			}
#endif
			x += interfaceInfo.paddingSize * 2;
			if (interfacePlayControl.enabledButtons & interfacePlayControlMode)
			{
				row = interfacePlayControl.highlightedButton == interfacePlayControlMode;
				switch ( appControlInfo.mediaInfo.playbackMode )
				{
					case playback_looped:
						num = 1;
						tprintf("[%c%s]", interfacePlayControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "loop");
						break;
					case playback_sequential:
						num = 2;
						tprintf("[%c%s]", interfacePlayControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "sequental");
						break;
					case playback_random:
						num = 3;
						tprintf("[%c%s]", interfacePlayControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "random");
						break;
					default: //playback_single
						num = 0;
						tprintf("[%c%s]", interfacePlayControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "single");
				}
				interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "playcontrol_mode.png", x, y, INTERFACE_PLAY_CONTROL_BUTTON_WIDTH, INTERFACE_PLAY_CONTROL_BUTTON_HEIGHT, row, num, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
				x += INTERFACE_PLAY_CONTROL_BUTTON_WIDTH+interfaceInfo.paddingSize;
			}
			if (interfacePlayControl.enabledButtons & interfacePlayControlAddToPlaylist) {
				row = interfacePlayControl.highlightedButton == interfacePlayControlAddToPlaylist;
				num = 2;
				interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "button2.png", x, y, INTERFACE_PLAY_CONTROL_BUTTON_WIDTH, INTERFACE_PLAY_CONTROL_BUTTON_HEIGHT, row, num, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
				x += INTERFACE_PLAY_CONTROL_BUTTON_WIDTH+interfaceInfo.paddingSize;
			}
			tprintf("\n");
		}

		if ( !interfaceInfo.showMenu && (interfacePlayControl.visibleFlag || interfacePlayControl.alwaysShowSlider) )
		{
			if (interfacePlayControl.sliderEnd > 0 && (signed)interfacePlayControl.sliderStart >= 0)
			{
				float value;
				DFBRectangle rect;
				DFBRectangle clip;
				char stime[128];
				/*
				gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_PLAY_CONTROL_BORDER_RED, INTERFACE_PLAY_CONTROL_BORDER_GREEN, INTERFACE_PLAY_CONTROL_BORDER_BLUE, INTERFACE_PLAY_CONTROL_BORDER_ALPHA, interfaceInfo.clientX, interfacePlayControl.positionY-interfaceInfo.paddingSize-20, interfaceInfo.clientWidth, interfaceInfo.borderWidth*2+16);
				gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0xFF, 0xFF, interfaceInfo.clientX+interfaceInfo.borderWidth, interfacePlayControl.positionY-interfaceInfo.paddingSize-20+interfaceInfo.borderWidth, (interfaceInfo.clientWidth-interfaceInfo.borderWidth*2)*interfacePlayControl.sliderPos/interfacePlayControl.sliderEnd, 16);
				*/
				value = (float)interfacePlayControl.sliderPos/(float)interfacePlayControl.sliderEnd;
				rect.h = 16;
				rect.x = interfaceInfo.clientX+interfaceInfo.borderWidth;
				rect.y = interfacePlayControl.positionY+(interfacePlayControl.visibleFlag ? 0 : interfacePlayControl.height)-interfaceInfo.paddingSize-interfaceInfo.borderWidth-rect.h*2-1;
				rect.w = interfaceInfo.clientWidth-interfaceInfo.borderWidth*2;

				if (appControlInfo.rtspInfo.active || (gfx_getVideoProviderCaps(screenMain) & DFBCAPS_LENGTH_IN_SEC)) // playing RTSP or BitBand
				{
					sprintf(stime, "%02d:%02d:%02d", interfacePlayControl.sliderPos/3600, (interfacePlayControl.sliderPos%3600)/60, interfacePlayControl.sliderPos%60);
					gfx_drawText(DRAWING_SURFACE, pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, rect.x, rect.y-4, stime, 0, 1);

					sprintf(stime, "%02d:%02d:%02d", interfacePlayControl.sliderEnd/3600, (interfacePlayControl.sliderEnd%3600)/60, interfacePlayControl.sliderEnd%60);
					gfx_drawText(DRAWING_SURFACE, pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, rect.x+rect.w-73, rect.y-4, stime, 0, 1);
				}
				// Green slider highlight
				/*if (interfaceInfo.inputFocus == inputFocusSlider)
				{
					//interface_drawOuterBorder(DRAWING_SURFACE, 0xFF, 0, 0, 0xFF, rect.x, rect.y, rect.w, rect.h, 4, interfaceBorderSideAll);
					gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_HIGHLIGHT_BORDER_RED, INTERFACE_HIGHLIGHT_BORDER_GREEN, INTERFACE_HIGHLIGHT_BORDER_BLUE, INTERFACE_HIGHLIGHT_BORDER_ALPHA, rect.x-interfaceInfo.borderWidth*2, rect.y-interfaceInfo.borderWidth*2, rect.w+interfaceInfo.borderWidth*4, rect.h+interfaceInfo.borderWidth*4);
				}*/

				if (value < 1.0f)
				{
					interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_inactive.png", rect.x, rect.y, rect.w, rect.h, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
				}
				if (interfacePlayControl.sliderPos > 0)
				{
					clip.x = 0;
					clip.y = 0;
					clip.w = rect.w*value;
					clip.h = rect.h;
					interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_active.png", rect.x, rect.y, rect.w, rect.h, 1, &clip, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
				}

				if (interfaceInfo.inputFocus == inputFocusSlider || interfaceInfo.inputFocus == inputFocusSliderMoving)
				{
					value = (float)interfacePlayControl.sliderPointer/(float)interfacePlayControl.sliderEnd;
					clip.x = 0;
					clip.y = 0;
					clip.w = INTERFACE_SLIDER_CURSOR_WIDTH;
					clip.h = INTERFACE_SLIDER_CURSOR_HEIGHT;
					//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_cursor.png", rect.x+rect.w*value - (INTERFACE_SLIDER_CURSOR_WIDTH / 2), rect.y + (rect.h - INTERFACE_SLIDER_CURSOR_HEIGHT) / 2, INTERFACE_SLIDER_CURSOR_WIDTH, INTERFACE_SLIDER_CURSOR_HEIGHT, 1, &clip, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
					// Green cursor INTERFACE_HIGHLIGHT_BORDER_RED, INTERFACE_HIGHLIGHT_BORDER_GREEN, INTERFACE_HIGHLIGHT_BORDER_BLUE
					gfx_drawRectangle(DRAWING_SURFACE, 231, 120, 23, INTERFACE_HIGHLIGHT_BORDER_ALPHA, rect.x+rect.w*value, rect.y-rect.h, interfaceInfo.borderWidth*2, rect.h+rect.h*2);
				}
			}
		}

		if ( interfacePlayControl.showState )
		{
			switch ( interfacePlayControl.activeButton )
			{
				case interfacePlayControlPrevious: num = 0; tprintf("--- .Previous. ---\n"); break;
				case interfacePlayControlRewind: num = 1; tprintf("--- .Rewind. ---\n"); break;
				case interfacePlayControlPlay: num = 2; tprintf("--- .Play. ---\n"); break;
				case interfacePlayControlPause: num = 3; tprintf("--- .Pause. ---\n"); break;
				case interfacePlayControlStop: num = 4; tprintf("--- .Stop. ---\n"); break;
				case interfacePlayControlFastForward: num = 5; tprintf("--- .FastForward. ---\n"); break;
				case interfacePlayControlNext: num = 6; tprintf("--- .Next. ---\n"); break;
				default:
					num = 0;
			}
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "icons_state.png", interfaceInfo.marginSize - 3*interfaceInfo.paddingSize - INTERFACE_THUMBNAIL_SIZE/2, interfaceInfo.marginSize, INTERFACE_THUMBNAIL_SIZE, INTERFACE_THUMBNAIL_SIZE, 0, num, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignCenter|interfaceAlignMiddle);
		}
	}

	interface_slideshowControlDisplay();
}

#define DRAW_SSHOW_BUTTON(btnid, num)	row = interfaceSlideshowControl.highlightedButton == btnid; \
										tprintf("[%c%s]", interfaceSlideshowControl.highlightedButton == btnid ? '>' : ' ', #btnid); \
										interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "slideshow_buttons.png", x, y, INTERFACE_SLIDESHOW_BUTTON_WIDTH, INTERFACE_SLIDESHOW_BUTTON_HEIGHT, row, num, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft); \
										y += INTERFACE_SLIDESHOW_BUTTON_WIDTH+interfaceInfo.paddingSize;

void interface_slideshowControlDisplay()
{
	int x, y, row, n, btn_num;
	char timer_text[4];
	DFBRectangle rectangle;

	if ( !interfaceInfo.showMenu && interfaceSlideshowControl.enabled && interfaceSlideshowControl.visibleFlag)
	{
		n = appControlInfo.slideshowInfo.state > 0 ? 4 : 1;

		interfaceSlideshowControl.width = INTERFACE_SLIDESHOW_BUTTON_WIDTH;
		interfaceSlideshowControl.height = n*INTERFACE_SLIDESHOW_BUTTON_HEIGHT + (n-1)*interfaceInfo.paddingSize;
		interfaceSlideshowControl.positionX = interfaceInfo.screenWidth - interfaceInfo.marginSize;
		interfaceSlideshowControl.positionY = (interfaceInfo.screenHeight - interfaceSlideshowControl.height)/2;

		x = interfaceSlideshowControl.positionX;
		y = interfaceSlideshowControl.positionY;

		/* // Drawing half-opaque rectangle
		DFBRectangle clip;
		clip.x = interfaceSlideshowControl.positionX;
		clip.y = interfaceSlideshowControl.positionY;
		clip.w = interfaceSlideshowControl.width;
		clip.h = interfaceSlideshowControl.height;
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_PLAY_CONTROL_BACKGROUND_RED, INTERFACE_PLAY_CONTROL_BACKGROUND_GREEN, INTERFACE_PLAY_CONTROL_BACKGROUND_BLUE, INTERFACE_PLAY_CONTROL_BACKGROUND_ALPHA, interfaceSlideshowControl.positionX, interfaceSlideshowControl.positionY, interfaceSlideshowControl.width, interfaceSlideshowControl.height);
		interface_drawInnerBorder(DRAWING_SURFACE, INTERFACE_PLAY_CONTROL_BORDER_RED, INTERFACE_PLAY_CONTROL_BORDER_GREEN, INTERFACE_PLAY_CONTROL_BORDER_BLUE, INTERFACE_PLAY_CONTROL_BORDER_ALPHA, interfaceSlideshowControl.positionX, interfaceSlideshowControl.positionY, interfaceSlideshowControl.width, interfaceSlideshowControl.height, interfaceInfo.borderWidth, interfaceBorderSideAll);
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
		*/
		//dprintf("%s: display play control\n", __FUNCTION__);
		if ( n > 1 )
		{
			DRAW_SSHOW_BUTTON(interfacePlayControlPrevious, 0)
		}

		row = interfaceSlideshowControl.highlightedButton == interfacePlayControlMode;
		switch ( appControlInfo.slideshowInfo.state )
		{
			case slideshowImage:
				btn_num = 3;
				tprintf("[%c%s]", interfaceSlideshowControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "image");
				break;
			case slideshowShow:
				btn_num = 1;
				tprintf("[%c%s]", interfaceSlideshowControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "show");
				break;
			case slideshowRandom:
				btn_num = 2;
				tprintf("[%c%s]", interfaceSlideshowControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "random");
				break;
			default:
				btn_num = 4;
				tprintf("[%c%s]", interfaceSlideshowControl.highlightedButton == interfacePlayControlMode ? '>' : ' ', "stop");
				break;
		}
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "slideshow_buttons.png", x, y, INTERFACE_SLIDESHOW_BUTTON_WIDTH, INTERFACE_SLIDESHOW_BUTTON_HEIGHT, row, btn_num, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
		y += INTERFACE_SLIDESHOW_BUTTON_WIDTH+interfaceInfo.paddingSize;

		if ( n > 1 )
		{
			row = interfaceSlideshowControl.highlightedButton == interfacePlayControlTimeout;
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "slideshow_buttons.png", x, y, INTERFACE_SLIDESHOW_BUTTON_WIDTH, INTERFACE_SLIDESHOW_BUTTON_HEIGHT, row, 6, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
	
			if ( appControlInfo.slideshowInfo.timeout < 1000 * 60)
			{
				sprintf( timer_text, "%d%s", appControlInfo.slideshowInfo.timeout / 1000, _T("SECOND_SHORT") );
			} else
			{
				sprintf( timer_text, "%d%s", appControlInfo.slideshowInfo.timeout / 60000, _T("MINUTE_SHORT") );
			}
			DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, timer_text, -1, &rectangle, NULL) );
			//dprintf("%s: timer_text x=%d y=%d w=%d h=%d\n", __FUNCTION__,rectangle.x,rectangle.y,rectangle.w,rectangle.h);
			gfx_drawText(DRAWING_SURFACE, pgfx_font, 32, 32, 32, 255, x + (INTERFACE_SLIDESHOW_BUTTON_WIDTH- rectangle.w) / 2, y + (INTERFACE_SLIDESHOW_BUTTON_HEIGHT + rectangle.h) / 2, timer_text, 0, 0);
			y += INTERFACE_SLIDESHOW_BUTTON_WIDTH+interfaceInfo.paddingSize;

			DRAW_SSHOW_BUTTON(interfacePlayControlNext, 5)
		}
		tprintf("\n");
	}
}

static int interface_refreshClock(void *pArg)
{	
	if ( interfaceInfo.showMenu)
	{		
		interface_displayMenu(1);
	}
	return 0;
}

void interface_displayClock(int detached)
{
	int x,y,w;
	time_t rawtime;
	struct tm *cur_time;

	if (interfaceInfo.enableClock == 0)
	{
		return;
	}

	time( &rawtime );
	cur_time = localtime(&rawtime);
	if (cur_time == NULL)
	{
		return;
	}

	w = INTERFACE_CLOCK_DIGIT_WIDTH*4 + INTERFACE_CLOCK_COLON_WIDTH;
	x = interfaceInfo.screenWidth - interfaceInfo.marginSize - w - INTERFACE_ROUND_CORNER_RADIUS/2;
	y = interfaceInfo.screenHeight - interfaceInfo.marginSize + interfaceInfo.paddingSize;
	// background
	if ( detached )
	{
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x+INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, w-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_ROUND_CORNER_RADIUS/2, w+INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS);

		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
		DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w-INTERFACE_ROUND_CORNER_RADIUS/2, y-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);

	} else 
	{
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );

		gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x-INTERFACE_ROUND_CORNER_RADIUS/2, interfaceInfo.clientY + interfaceInfo.clientHeight, w+INTERFACE_ROUND_CORNER_RADIUS, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2 - (interfaceInfo.clientY + interfaceInfo.clientHeight));
	}
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
	gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA, x+INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, w-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS);

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
	DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA) );
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
	
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w-INTERFACE_ROUND_CORNER_RADIUS/2, y+INTERFACE_CLOCK_DIGIT_HEIGHT-INTERFACE_ROUND_CORNER_RADIUS/2, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
	
	//
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "digits.png", x, y, INTERFACE_CLOCK_DIGIT_WIDTH, INTERFACE_CLOCK_DIGIT_HEIGHT, 0, cur_time->tm_hour / 10, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
	x += INTERFACE_CLOCK_DIGIT_WIDTH;
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "digits.png", x, y, INTERFACE_CLOCK_DIGIT_WIDTH, INTERFACE_CLOCK_DIGIT_HEIGHT, 0, cur_time->tm_hour % 10, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
	x += INTERFACE_CLOCK_DIGIT_WIDTH;
	// draw :
	interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "colon.png", x, y, INTERFACE_CLOCK_COLON_WIDTH, INTERFACE_CLOCK_DIGIT_HEIGHT, 0, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft, NULL, NULL);
	//
	x += INTERFACE_CLOCK_COLON_WIDTH;
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "digits.png", x, y, INTERFACE_CLOCK_DIGIT_WIDTH, INTERFACE_CLOCK_DIGIT_HEIGHT, 0, cur_time->tm_min / 10, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
	x += INTERFACE_CLOCK_DIGIT_WIDTH;
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "digits.png", x, y, INTERFACE_CLOCK_DIGIT_WIDTH, INTERFACE_CLOCK_DIGIT_HEIGHT, 0, cur_time->tm_min % 10, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
}

inline int interface_playControlSliderIsEnabled()
{
	return interfacePlayControl.alwaysShowSlider;
}

inline float interface_playControlSliderGetPosition()
{
	return interfacePlayControl.sliderPointer;
}


inline void interface_playControlSliderEnable(int enable)
{
	interfacePlayControl.alwaysShowSlider = enable;
}


static int interface_hidePlayState(void *pArg)
{
	//dprintf("%s: in %d\n", __FUNCTION__, (int)pArg);

	interfacePlayControl.showState = 0;
	interface_displayMenu(1);

	return 0;
}

void interface_displayPlayState()
{
	interfacePlayControl.showState = 1;
	interface_addEvent(interface_hidePlayState, NULL, 1000*interfacePlayControl.showTimeout, 1);
}

void interface_soundControlSetMute(int muteFlag)
{
	interfaceSoundControl.muted = muteFlag;
}

void interface_soundControlSetValue(int value)
{
	//dprintf("%s: %d\n", __FUNCTION__, value);

	interfaceSoundControl.curValue = value;
}

static void interface_displaySliderControl()
{
	DFBRectangle rect;//, textRect;
	//int x, y, w, h;
	char percent[16];
	float value;

	if ( interfaceInfo.showSliderControl != 0 && interfaceSlider.getCallback != NULL)
	{
		value = (float)(interfaceSlider.getCallback(interfaceSlider.pArg)-interfaceSlider.minValue)/(float)(interfaceSlider.maxValue-interfaceSlider.minValue);
		interface_displayTextBox(interfaceInfo.screenWidth/2, interfaceInfo.screenHeight/2, interfaceSlider.textValue, NULL, interfaceSlider.width, &rect, interfaceSlider.height+(interfaceSlider.textValue[0] != 0 ? interfaceInfo.paddingSize : 0));
		//interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_BORDER_RED, INTERFACE_BORDER_GREEN, INTERFACE_BORDER_BLUE, INTERFACE_BORDER_ALPHA, rect.x, rect.y+rect.h-interfaceSlider.height, rect.w, interfaceSlider.height, interfaceInfo.borderWidth, interfaceBorderSideAll);
		//rect.x += interfaceInfo.paddingSize;
		rect.y += rect.h-interfaceSlider.height;// - interfaceInfo.paddingSize - INTERFACE_STATUSBAR_ICON_HEIGHT;//+interfaceInfo.paddingSize;
		//rect.w -= interfaceInfo.paddingSize*2;
		rect.h = interfaceSlider.height;//-interfaceInfo.paddingSize*2;
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
		if (value < 1.0f)
		{
			interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_inactive.png", rect.x, rect.y, rect.w, rect.h, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
		}
		//gfx_drawRectangle(DRAWING_SURFACE, 0x22, 0x22, 0xFF, 0xFF, rect.x, rect.y, rect.w*value, rect.h);
		{
			DFBRectangle clip;
			clip.x = 0;
			clip.y = 0;
			clip.w = rect.w*value;
			clip.h = rect.h;
			interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_active.png", rect.x, rect.y, rect.w, rect.h, 1, &clip, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
		}
		//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_active.png", rect.x, rect.y, rect.w*value, rect.h, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
		/*if (value < 1.0f)
		{
			{
				DFBRectangle clip;
				clip.x = 0;
				clip.y = 0;
				clip.w = rect.w-rect.w*value+interfaceInfo.borderWidth;
				clip.h = rect.h;
				interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_inactive.png", rect.x, rect.y, rect.w, rect.h, 1, &clip, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
			}
			//interface_drawImage(DRAWING_SURFACE, IMAGE_DIR "slider_inactive.png", rect.x+rect.w*value, rect.y, rect.w-rect.w*value+interfaceInfo.borderWidth, rect.h, 1, NULL, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTop|interfaceAlignLeft, NULL, NULL);
		}*/
		interface_drawOuterBorder(DRAWING_SURFACE, INTERFACE_BORDER_RED, INTERFACE_BORDER_GREEN, INTERFACE_BORDER_BLUE, INTERFACE_BORDER_ALPHA, rect.x, rect.y, rect.w, interfaceSlider.height, interfaceInfo.borderWidth, interfaceBorderSideAll);
		sprintf(percent, "%d", (int)(interfaceSlider.divisions*value));
		//dprintf("%s: slider divs %d, min %d, max %d, cur %d, cdiv %f\n", __FUNCTION__, interfaceSlider.divisions, interfaceSlider.minValue, interfaceSlider.maxValue, interfaceSlider.getCallback(), value);
		/*DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, percent, -1, &textRect, NULL) );
		gfx_drawText(DRAWING_SURFACE, pgfx_font, INTERFACE_BOOKMARK_RED, INTERFACE_BOOKMARK_GREEN, INTERFACE_BOOKMARK_BLUE, INTERFACE_BOOKMARK_ALPHA, rect.x+rect.w/2-(textRect.x+textRect.w)/2, rect.y+rect.h, percent, 0, 0);
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );*/
		tprintf("-----------------------------------------------------------\n");
		tprintf("| %s: %s\n", interfaceSlider.textValue[0] != 0 ? interfaceSlider.textValue : "", percent);
		tprintf("-----------------------------------------------------------\n");
	}
}

static void interface_displaySoundControl()
{
	int x, y, w, h;
	int adv, fa;
	char buffer[16];

	if ( interfaceSoundControl.visibleFlag != 0 )
	{
		DFBCHECK( pgfx_font->GetAscender (pgfx_font, &fa) );

		x = interfaceInfo.clientX+interfaceInfo.marginSize/2;
		y = interfaceInfo.marginSize/2;
		w = interfaceInfo.clientWidth-interfaceInfo.marginSize;
		h = interfaceInfo.thumbnailSize;

		interface_drawRoundBoxColor(x, y, w, h, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA);

		if ( interfaceSoundControl.muted )
		{
			tprintf("X\n");
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "sound.png", x, y, interfaceInfo.thumbnailSize, interfaceInfo.thumbnailSize, 0, 1, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);
		} else
		{
			tprintf("O ");
			interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "sound.png", x, y, interfaceInfo.thumbnailSize, interfaceInfo.thumbnailSize, 0, 0, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);

			sprintf(buffer, "% 4ld%%", interfaceSoundControl.curValue*100/interfaceSoundControl.maxValue);

			DFBCHECK( pgfx_font->GetStringWidth (pgfx_font, buffer, -1, &adv) );

			x += interfaceInfo.thumbnailSize+interfaceInfo.paddingSize;
			y += interfaceInfo.thumbnailSize/4;
			w = w-interfaceInfo.thumbnailSize-interfaceInfo.paddingSize-adv;
			h = 2*interfaceInfo.thumbnailSize/4;

			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
			gfx_drawRectangle(DRAWING_SURFACE, INTERFACE_SOUND_CONTROL_RED, INTERFACE_SOUND_CONTROL_GREEN, INTERFACE_SOUND_CONTROL_BLUE, INTERFACE_SOUND_CONTROL_ALPHA, x, y, interfaceSoundControl.curValue*100/interfaceSoundControl.maxValue == 0 ? w/100+1 : w*interfaceSoundControl.curValue/interfaceSoundControl.maxValue, h);

			gfx_drawText(DRAWING_SURFACE, pgfx_font, 0xFF, 0xFF, 0xFF, 0xFF, x+w, y+fa, buffer, 0, 0);

			DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
		}
		tprintf("\n");
	}
}

void interface_playControlSelect(interfacePlayControlButton_t button)
{
	interfaceInfo.inputFocus = inputFocusPlayControl;
	interfaceSlideshowControl.highlightedButton = 0;
	if ( interfacePlayControl.activeButton != button && interfacePlayControl.visibleFlag )
	{
		interface_displayPlayState();
	}
	interfacePlayControl.activeButton = button;
}

inline void interface_playControlHighlight(interfacePlayControlButton_t button)
{
	interfacePlayControl.highlightedButton = button;
}

static int interface_playControlSetVisible(void *pArg)
{
	int flag = GET_NUMBER(pArg);

	//dprintf("%s: in %d\n", __FUNCTION__, flag);

	interfacePlayControl.visibleFlag = flag;
	interfaceSlideshowControl.visibleFlag = flag;

	if ( interfacePlayControl.enabled || interfaceSlideshowControl.enabled )
	{
		interface_displayMenu(1);
	}
	return 0;
}

void interface_playControlDisable(int redraw)
{
	interface_playControlReset();

	interfacePlayControl.enabled = 0;

	if ( redraw )
	{
		interface_displayMenu(1);
	}
}

void interface_playControlReset()
{
	interfacePlayControl.sliderStart = 0;
	interfacePlayControl.sliderEnd = 0;
	interfacePlayControl.sliderPos = 0;
	interfacePlayControl.sliderPointer = 0;
	interfacePlayControl.alwaysShowSlider = 0;
	interfaceInfo.inputFocus = inputFocusPlayControl;

	interfacePlayControl.pDisplay = interface_displayPlayControl;
	interfacePlayControl.pProcessCommand = NULL;
	interfacePlayControl.pChannelChange = NULL;
	interfacePlayControl.pAudioChange = NULL;
	interfaceChannelControl.pSet = NULL;
}

void interface_playControlSetup(playControlCallback pCallback, void *pArg, interfacePlayControlButton_t buttons, const char *description, int description_thumbnail)
{
	interface_playControlReset();

	interfacePlayControl.enabledButtons = buttons;
#ifdef ENABLE_PVR
	dprintf("%s: dvb active = %d pvrActive = %d\n", __FUNCTION__,appControlInfo.dvbInfo.active, pvr_getActive());
	if ( pvr_getActive()
#ifdef ENABLE_DVB
		|| appControlInfo.dvbInfo.active
#endif
	  )
	{
		interfacePlayControl.enabledButtons |= interfacePlayControlRecord;
	}
#endif
	interfacePlayControl.pCallback = pCallback;
	interfacePlayControl.pArg = pArg;

	if ((interfacePlayControl.enabledButtons & interfacePlayControl.highlightedButton) == 0)
	{
		if (interfacePlayControl.enabledButtons & interfacePlayControlPlay)
		{
			interfacePlayControl.highlightedButton = interfacePlayControlPlay;
		} else
		{
			interfacePlayControl.highlightedButton = interfacePlayControlNext;
		}
	}

	//dprintf("%s: got %d buttons, x = %d\n", __FUNCTION__, n, interfacePlayControl.positionX);

	interfacePlayControl.enabled = 1;

	//interfacePlayControl.showState = 0;

	interfacePlayControl.visibleFlag = interfacePlayControl.showOnStart;

	if ( description != NULL )
	{
		strncpy(interfacePlayControl.description,description,MENU_ENTRY_INFO_LENGTH);
		interfacePlayControl.description[MENU_ENTRY_INFO_LENGTH-1] = 0;
	} else
	{
		interfacePlayControl.description[0] = 0;
	}

	interfacePlayControl.descriptionThumbnail = description_thumbnail;

	//dprintf("%s: out\n", __FUNCTION__);
}

inline void interface_playControlSetProcessCommand(playControlProcessCommandFunction pProcessCommand)
{
	interfacePlayControl.pProcessCommand = pProcessCommand;
}

inline void interface_playControlSetChannelCallbacks(playControlChannelCallback pChannelChange, playControlChannelCallback pSetChannelNumber)
{
	interfacePlayControl.pChannelChange = pChannelChange;
	interfaceChannelControl.pSet = pSetChannelNumber;
}

inline void interface_playControlSetAudioCallback(eventActionFunction pAudioChange)
{
	interfacePlayControl.pAudioChange = pAudioChange;
}

inline void interface_playControlSetDisplayFunction(playControlDisplayFunction pDisplay)
{
	interfacePlayControl.pDisplay = pDisplay == NULL ? interface_displayPlayControl : pDisplay;
}

void interface_playControlSetInputFocus(interfaceInputFocus_t newFocus)
{
	if (interfaceInfo.inputFocus != newFocus)
	{
		interfacePlayControl.highlightedButton = newFocus == inputFocusPlayControl ? interfacePlayControl.activeButton : 0;
		interfaceSlideshowControl.highlightedButton = newFocus == inputFocusSlideshow ? interfacePlayControlMode : 0;
	}
	interfaceInfo.inputFocus = newFocus;
}

inline void interface_playControlSetButtons(interfacePlayControlButton_t buttons)
{
	interfacePlayControl.enabledButtons = buttons;
}

inline interfacePlayControlButton_t interface_playControlGetButtons()
{
	return interfacePlayControl.enabledButtons;
}

void interface_playControlSlider(unsigned int start, unsigned int end, unsigned int pos)
{	
	mysem_get(interface_semaphore);
	interfacePlayControl.sliderStart = start;
	interfacePlayControl.sliderEnd = end;
	interfacePlayControl.sliderPos = pos;
	if (interfaceInfo.inputFocus != inputFocusSlider)
	{
		//dprintf("%s: Set slider %d, %d, %d\n", __FUNCTION__, start, end, pos);
		if (interfaceInfo.inputFocus == inputFocusSliderMoving)
		{
			interfaceInfo.inputFocus = inputFocusSlider;
		}
		interfacePlayControl.sliderPointer = pos;
	}
	mysem_release(interface_semaphore);
	if (interfacePlayControl.visibleFlag || interfacePlayControl.alwaysShowSlider)
	{
		interface_displayMenu(1);
	}
}

void interface_showMenu(int showFlag, int redrawFlag)
{
	//dprintf("%s: %d\n", __FUNCTION__, showFlag);

	if (!interfaceInfo.lockMenu)
	{
		interfaceInfo.showMenu = showFlag;
	}

	if ( !interfaceInfo.showMenu )
	{
		if (interfacePlayControl.showOnStart && (interfacePlayControl.enabled || interfaceSlideshowControl.enabled))
		{
			interface_playControlRefresh(0);
		}
		interfaceSlideshowControl.visibleFlag = interfaceSlideshowControl.enabled && interfacePlayControl.showOnStart;
	}

	if ( redrawFlag )
	{		
		interface_displayMenu(1);
	}
}

static void interface_ThreadTerm(void* pArg)
{
	mysem_release(event_semaphore);
	interfaceEventThread = 0;
}


static void *interface_EventThread(void *pArg)
{
	int n, i;
	struct timeval tv;
	void *pEventArg;
	eventActionFunction pAction;

	//dprintf("interface: event thread in\n");

	pthread_cleanup_push(interface_ThreadTerm, pArg);

	while (keepCommandLoopAlive)
	{
		pthread_testcancel();
		usleep(100000);
		pthread_testcancel();

		mysem_get(event_semaphore);

		//dprintf("interface: check events %d\n", interfaceInfo.eventCount);
		if ( interfaceInfo.eventCount > 0 )
		{
			for ( n=0; n<interfaceInfo.eventCount; n++ )
			{
				gettimeofday(&tv, NULL);
				//dprintf("interface: check event %d count %d start %d time %d diff %d\n", n, interfaceInfo.event[n].counter, interfaceInfo.event[n].startTime, time(0), time(0) - interfaceInfo.event[n].startTime);
				if ( (unsigned long long)(tv.tv_sec - interfaceInfo.event[n].startTime.tv_sec)*(unsigned long long)1000000+(unsigned long long)(tv.tv_usec - interfaceInfo.event[n].startTime.tv_usec) >= (unsigned long long)interfaceInfo.event[n].counter*(unsigned long long)1000 )
				{
					pAction = interfaceInfo.event[n].pAction;
					pEventArg = interfaceInfo.event[n].pArg;
					/* remove event */
					for ( i=n; i<interfaceInfo.eventCount-1;i++ )
					{
						interfaceInfo.event[i] = interfaceInfo.event[i+1];
					}
					interfaceInfo.eventCount--;
					//dprintf("interface: execute event %d\n", n);
					/* execute event */
					//mysem_release(event_semaphore);
					if ( pAction != NULL && keepCommandLoopAlive )
					{
						pthread_t id;
						if (pthread_create(&id, NULL, (void *(*)(void*))pAction, pEventArg) == 0)
						{
							pthread_detach(id);
						} else
						{
							eprintf("interface: Failed to create event-action thread!\n");
						}
						//pAction(pEventArg);
					}
					//mysem_get(event_semaphore);
					/* start over since someone might changed sequence during execution of action */
					n = 0;
				}
			}
		}

		mysem_release(event_semaphore);
	}

	pthread_cleanup_pop(1);
	return 0;
}

static int interface_hideMessageBoxEvent(void *pArg)
{
	//dprintf("interface: hide messagebox\n");
	if ( interfaceInfo.messageBox.type != interfaceMessageBoxNone )
	{
		//dprintf("interface: do hide messagebox\n");
		interface_removeEvent(interface_hideMessageBoxEvent, pArg);
		interfaceInfo.messageBox.type = interfaceMessageBoxNone;
		interface_displayMenu(1);
	}

	return 0;
}

void interface_hideMessageBox()
{
	interface_hideMessageBoxEvent(NULL);
}

static int interface_channelNumberReset(void *pArg)
{
	// Trying to change channel even on incomplete number (fixes bug #9548)
	interfaceChannelControl.pSet(atoi(interfaceChannelControl.number), interfacePlayControl.pArg);
	interfaceChannelControl.length = 0;
	interface_addEvent(interface_channelNumberHide, NULL, 1000*interfacePlayControl.showTimeout, 1);
	return 0;
}

static int interface_channelNumberHide(void *pArg)
{
	interfaceChannelControl.showingLength = 0;
	interface_displayMenu(1);
	return 0;
}

void interface_channelNumberShow(int channelNumber)
{
	snprintf(interfaceChannelControl.number, sizeof(interfaceChannelControl.number), "%03d", channelNumber);
	interfaceChannelControl.number[sizeof(interfaceChannelControl.number)-1] = 0;
	interfaceChannelControl.showingLength = strlen(interfaceChannelControl.number);
	interface_addEvent(interface_channelNumberHide, NULL, 1000*interfacePlayControl.showTimeout, 1);
}

void messageBox_setDefaultColors(void)
{
	interfaceInfo.messageBox.colors.text.R = INTERFACE_BOOKMARK_RED;
	interfaceInfo.messageBox.colors.text.G = INTERFACE_BOOKMARK_GREEN;
	interfaceInfo.messageBox.colors.text.B = INTERFACE_BOOKMARK_BLUE;
	interfaceInfo.messageBox.colors.text.A = INTERFACE_BOOKMARK_ALPHA;
	interfaceInfo.messageBox.colors.background.R = INTERFACE_MESSAGE_BOX_RED;
	interfaceInfo.messageBox.colors.background.G = INTERFACE_MESSAGE_BOX_GREEN;
	interfaceInfo.messageBox.colors.background.B = INTERFACE_MESSAGE_BOX_BLUE;
	interfaceInfo.messageBox.colors.background.A = INTERFACE_MESSAGE_BOX_ALPHA;
	interfaceInfo.messageBox.colors.border.R = INTERFACE_MESSAGE_BOX_BORDER_RED;
	interfaceInfo.messageBox.colors.border.G = INTERFACE_MESSAGE_BOX_BORDER_GREEN;
	interfaceInfo.messageBox.colors.border.B = INTERFACE_MESSAGE_BOX_BORDER_BLUE;
	interfaceInfo.messageBox.colors.border.A = INTERFACE_MESSAGE_BOX_BORDER_ALPHA;
	interfaceInfo.messageBox.colors.title.R = INTERFACE_BORDER_RED;
	interfaceInfo.messageBox.colors.title.G = INTERFACE_BORDER_GREEN;
	interfaceInfo.messageBox.colors.title.B = INTERFACE_BORDER_BLUE;
	interfaceInfo.messageBox.colors.title.A = INTERFACE_BORDER_ALPHA;
}

void interface_showConfirmationBox(const char *text, int icon, menuConfirmFunction pCallback, void *pArg)
{
	interface_removeEvent(interface_hideMessageBoxEvent, (void*)0);

	STRMAXCPY(interfaceInfo.messageBox.message, text, MAX_MESSAGE_BOX_LENGTH);

	interfaceInfo.messageBox.icon = icon;
	interfaceInfo.messageBox.pCallback = pCallback;
	interfaceInfo.messageBox.pArg = pArg;
	interfaceInfo.messageBox.target.x = interfaceInfo.screenWidth/2;
	interfaceInfo.messageBox.target.y = interfaceInfo.screenHeight/2;
	interfaceInfo.messageBox.target.w = 0;
	interfaceInfo.messageBox.target.h = 0;
	messageBox_setDefaultColors();

	interfaceInfo.messageBox.type = interfaceMessageBoxCallback;

	interface_displayMenu(1);
}

void interface_showMessageBox(const char *text, int icon, int hideDelay)
{
	if ( hideDelay > 0 )
	{
		interface_addEvent(interface_hideMessageBoxEvent, (void*)0, hideDelay, 1);
	} else
	{
	    interface_removeEvent(interface_hideMessageBoxEvent, (void*)0);
	}

	STRMAXCPY(interfaceInfo.messageBox.message, text, sizeof(interfaceInfo.messageBox.message));

	interfaceInfo.messageBox.icon = icon;
	messageBox_setDefaultColors();
	interfaceInfo.messageBox.target.x = interfaceInfo.screenWidth/2;
	interfaceInfo.messageBox.target.y = interfaceInfo.screenHeight/2;
	interfaceInfo.messageBox.target.w = 0;
	interfaceInfo.messageBox.target.h = 0;

	interfaceInfo.messageBox.type = interfaceMessageBoxSimple;

	/* We don't want our message box to disappear if any keys was pressed */
	helperFlushEvents();

	interface_displayMenu(1);
}

void interface_showSlideshowControl()
{
	interfaceSlideshowControl.enabled = 1;
	interfacePlayControl.highlightedButton = 0;
	interfaceInfo.inputFocus = inputFocusSlideshow;
	if (interfaceSlideshowControl.highlightedButton == 0)
		interfaceSlideshowControl.highlightedButton = interfacePlayControlMode;
	interface_playControlRefresh(0);
	interface_showMenu(0, 1);
}

void interface_playControlRefresh(int redraw)
{
	interfacePlayControl.visibleFlag = 1;
	interfaceSlideshowControl.visibleFlag = 1;
#ifdef ENABLE_PVR
	if ( pvr_getActive()
#ifdef ENABLE_DVB
		|| appControlInfo.dvbInfo.active
#endif
	  )
	{
		interfacePlayControl.enabledButtons |= interfacePlayControlRecord;
	}
#endif
	if ( interfacePlayControl.enabled || interfaceSlideshowControl.enabled )
	{
		interface_addEvent(interface_playControlSetVisible, (void*)0, 1000*interfacePlayControl.showTimeout, 1);
		if ( redraw )
		{
			interface_displayMenu(1);
		}
	}
}

void interface_playControlHide(int redraw)
{
	interfacePlayControl.visibleFlag = 0;
	interfaceSlideshowControl.visibleFlag = 0;
	interface_removeEvent(interface_playControlSetVisible, (void*)0);
	if ( redraw )
	{
		interface_displayMenu(1);
	}
}

int s_interface_addEvent(eventActionFunction pAction, void *pArg, int counter, int replaceSimilar)
{
	int eid, i;

	mysem_get(event_semaphore);

	//dprintf("%s: called\n", __FUNCTION__);

	eid = -1;

	if ( replaceSimilar )
	{
		for ( i=0; i<interfaceInfo.eventCount; i++ )
		{
			if ( interfaceInfo.event[i].pAction == pAction && interfaceInfo.event[i].pArg == pArg )
			{
				eid = i;
				//dprintf("%s: refresh event %d\n", __FUNCTION__, eid);
				break;
			}
		}
	}

	if ( (interfaceInfo.eventCount < MAX_EVENTS || eid != -1) && pAction != NULL )
	{
		//dprintf("%s: adding/updating %d\n", __FUNCTION__, eid);
		interfaceInfo.event[eid == -1 ? interfaceInfo.eventCount : eid].pAction = pAction;
		interfaceInfo.event[eid == -1 ? interfaceInfo.eventCount : eid].pArg = pArg;
		interfaceInfo.event[eid == -1 ? interfaceInfo.eventCount : eid].counter = counter;
		gettimeofday(&interfaceInfo.event[eid == -1 ? interfaceInfo.eventCount : eid].startTime, NULL);
		if ( eid == -1 )
		{
			interfaceInfo.eventCount++;
		}
	} else
	{
		//dprintf("%s: failed\n", __FUNCTION__);
		mysem_release(event_semaphore);
		return -1;
	}

	mysem_release(event_semaphore);

	return interfaceInfo.eventCount;
}


int interface_removeEvent(eventActionFunction pAction, void *pArg)
{
	int n, i;

	mysem_get(event_semaphore);

	for ( n=0; n<interfaceInfo.eventCount; n++ )
	{
		if ( interfaceInfo.event[n].pAction == pAction && interfaceInfo.event[n].pArg == pArg )
		{
			break;
		}
	}

	if ( interfaceInfo.eventCount > 0 && n < interfaceInfo.eventCount )
	{
		for ( i=n; i<interfaceInfo.eventCount-1; i++ )
		{
			interfaceInfo.event[i] = interfaceInfo.event[i+1];
		}
		interfaceInfo.eventCount--;
	} else
	{
		mysem_release(event_semaphore);
		return -1;
	}

	mysem_release(event_semaphore);

	return n;
}

static int interface_animationFrameEvent(void *pArg)
{
#ifdef ENABLE_LOADING_ANUMATION
	static int frame = 1;

	dprintf("%s: menu\n", __FUNCTION__);
	//interface_displayMenu(0);

	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_NOFX) );
	gfx_drawRectangle(DRAWING_SURFACE, 0, 0, 0, 0, interfaceInfo.screenWidth-interfaceInfo.marginSize, interfaceInfo.screenHeight-interfaceInfo.marginSize, 16, 16);

	if ( interfaceInfo.showLoadingAnimation )
	{
		frame = 1 - frame;

		dprintf("%s: animation frame %d\n", __FUNCTION__, frame);

		interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "cycle.png", interfaceInfo.screenWidth-interfaceInfo.marginSize, interfaceInfo.screenHeight-interfaceInfo.marginSize, 16, 16, 0, frame, DSBLIT_BLEND_ALPHACHANNEL, interfaceAlignTopLeft);

		interface_addEvent(interface_animationFrameEvent, NULL, 1000, 1);
	}

	dprintf("%s: flip\n", __FUNCTION__);

	interface_flipSurface();

	dprintf("%s: done\n", __FUNCTION__);
#endif
	return 0;
}

void interface_hideLoadingAnimation()
{
	dprintf("interface: hide loading animation\n");

	interfaceInfo.showLoadingAnimation = 0;
	interface_removeEvent(interface_animationFrameEvent, NULL);
}

void interface_showLoadingAnimation()
{
	dprintf("interface: show loading animation\n");

	interfaceInfo.showLoadingAnimation = 1;
	interface_animationFrameEvent(NULL);
}


void interface_init()
{
	int err;

	DFBCHECK(DRAWING_SURFACE->GetSize(DRAWING_SURFACE, &interfaceInfo.screenWidth, &interfaceInfo.screenHeight));

	interfaceInfo.borderWidth = INTERFACE_BORDER_WIDTH;
	interfaceInfo.paddingSize = INTERFACE_PADDING;
	interfaceInfo.marginSize = INTERFACE_MARGIN_SIZE;
	interfaceInfo.thumbnailSize = INTERFACE_THUMBNAIL_SIZE;

	/*interfaceInfo.screenWidth = 720;
	interfaceInfo.screenHeight = 576;*/

	interfaceInfo.clientX = interfaceInfo.marginSize;
	interfaceInfo.clientY = interfaceInfo.marginSize;
	interfaceInfo.clientWidth = interfaceInfo.screenWidth-interfaceInfo.marginSize*2;
	interfaceInfo.clientHeight = interfaceInfo.screenHeight-interfaceInfo.marginSize*2;

	interfaceInfo.showMenu = 0;

	interfaceInfo.eventCount = 0;

	interfaceInfo.keypad.enable = 0;
	interfaceInfo.keypad.row = 0;
	interfaceInfo.keypad.cell = 0;

	interfaceInfo.enableClock = 1;
	interfaceInfo.lockMenu = 0;

#if (defined STB225)
	interfaceInfo.mode3D = 0;
#endif

	interfaceInfo.currentMenu = NULL;

	interfaceInfo.notifyText[0] = 0;

	interfaceInfo.teletext.show = 0;

	interfaceInfo.customSlider = NULL;
	interfaceInfo.customSliderVisibleInMenu = 0;
	interfaceInfo.customSliderArg = NULL;

	interface_sliderSetDivisions(100);
	interface_sliderSetSize(400, SLIDER_DEFAULT_HEIGHT);

	interface_playControlReset();
	interfacePlayControl.activeButton = interfacePlayControlStop;

	interfaceSlideshowControl.enabled = 0;
	interfaceSlideshowControl.highlightedButton = interfacePlayControlMode;

	interfaceInfo.background.Enable = 0;
	interfaceInfo.background.image[0] = 0;

	// Initialized in loadAppSettings()
	if (interfaceInfo.animation >= interfaceAnimationCount)
	{
		interfaceInfo.animation = interfaceAnimationHorizontalPanorama;
	}

	if (interfaceInfo.highlightColor < 0 || 
	    interfaceInfo.highlightColor >= (int)(sizeof(interface_colors)/sizeof(interface_colors[0])))
	{
		interfaceInfo.highlightColor = 0;
	}

#ifdef WCHAR_SUPPORT
	int keypad_row = 0, keypad_cell = 0;
	int mbstr_index = 0;
	int mbstr_len;
	int str_len;
	char local_key[8] = "LOCAL_#";
	unsigned char *local_str = (unsigned char *)_T("LOCAL_ABC");

	if ( local_str != NULL && *local_str && strncmp((char*)local_str, "LOCAL_ABC", sizeof("LOCAL_ABC")-1 ) != 0 )
	{
		interfaceInfo.keypad.altLayout = ALTLAYOUT_OFF;

		str_len = strlen( (char*)local_str );
		mbstr_index = 0;
		while (keypad[keypad_row][keypad_cell] != 0)
		{
			while (keypad[keypad_row][keypad_cell] != 0)
			{
				mbstr_index++;
				keypad_cell++;
			}
			keypad_cell = 0;
			keypad_row++;
		}
		mbstr_len = utf8_mbslen(local_str, str_len);

		if ( mbstr_len + (int)wcslen(keypad[0]) <= mbstr_index ) // keypad can fit numeric row and local alphabet
		{
			keypad_row = 1;
		} else
			keypad_row = 0;

		mbstr_index = 0;
		while (local_str[mbstr_index])
		{
			if (keypad[keypad_row][keypad_cell] == 0)
			{
				keypad_local[keypad_row][keypad_cell] = 0;
				keypad_row++;
				keypad_cell = 0;
				if ( keypad[keypad_row][keypad_cell] == 0 )
				{
					break;
				}
			}
			do
			{
				mbstr_len = utf8_mbtowc( &keypad_local[keypad_row][keypad_cell], &local_str[mbstr_index], str_len-mbstr_index );
				if ( mbstr_len <= 0 )
					mbstr_index++;
			} while ( mbstr_index < str_len && mbstr_len <= 0 );
			if ( mbstr_len <= 0 )
				break;
			mbstr_index += mbstr_len;
			keypad_cell++;
		}
		keypad_local[keypad_row][keypad_cell] = 0;

		mbstr_index = 0;
		for (keypad_cell = 0; keypad_cell < ALPHABET_LENGTH && alphabet[keypad_cell]; keypad_cell++ );
		while ( keypad_cell < ALPHABET_LENGTH - 1 && local_str[mbstr_index] )
		{
			do
			{
				mbstr_len = utf8_mbtowc( &alphabet[keypad_cell], &local_str[mbstr_index], str_len-mbstr_index );
				if ( mbstr_len <= 0 )
					mbstr_index++;
			} while ( mbstr_index < str_len && mbstr_len <= 0 );
			if ( mbstr_len <= 0 )
					break;
			mbstr_index += mbstr_len;
			keypad_cell++;
		}
	} else
	{
		interfaceInfo.keypad.altLayout = -1;
	}

	for ( keypad_row = 0; keypad_row < 10; keypad_row++ )
	{
		local_key[6] = '0' + keypad_row;
		local_str = (unsigned char*)_T(local_key);
		if ( local_str != NULL && *local_str && strcmp((char*)local_str, local_key ) != 0 )
		{
			mbstr_index = 0;
			str_len = strlen( (char*)local_str );
			for ( keypad_cell = 0; keypad_cell < SYMBOL_TABLE_LENGTH && symbol_table[keypad_row][keypad_cell]; keypad_cell++ );
			while ( keypad_cell < SYMBOL_TABLE_LENGTH - 1 && local_str[mbstr_index] )
			{
				do
				{
					mbstr_len = utf8_mbtowc( &symbol_table[keypad_row][keypad_cell], &local_str[mbstr_index], str_len-mbstr_index );
					if ( mbstr_len <= 0 )
						mbstr_index++;
				} while ( mbstr_index < str_len && mbstr_len <= 0 );
				if ( mbstr_len <= 0 )
					break;
				mbstr_index += mbstr_len;
				keypad_cell++;
			}
			symbol_table[keypad_row][keypad_cell] = 0;
		}
	}
#endif

	mysem_create(&interface_semaphore);
	mysem_create(&event_semaphore);

	err = pthread_create (&interfaceEventThread, NULL,
						  interface_EventThread,
						  NULL);
	if (err)
		eprintf("%s: failed to create event thread: %s\n", __FUNCTION__, strerror(err));
	pthread_detach(interfaceEventThread);

	//dprintf("%s: dimensions: %dx%d\n", __FUNCTION__, interfaceInfo.screenWidth, interfaceInfo.screenHeight);
}

void interface_destroy()
{
	if ( 0 != interfaceEventThread )
	{
		pthread_cancel (interfaceEventThread);
		dprintf("interface: wait for thread\n");
		/*Now make sure thread has exited*/
		while ( interfaceEventThread )
		{
			usleep(10000);
		}
	}
	dprintf("interface: cleaned up\n");
	mysem_destroy(interface_semaphore);
	mysem_destroy(event_semaphore);
}

void interface_customSlider(customSliderFunction pFunction, void *pArg, int showOverMenu, int bRedrawFlag)
{
	interfaceInfo.customSlider = pFunction;
	interfaceInfo.customSliderArg = pArg;
	interfaceInfo.customSliderVisibleInMenu = showOverMenu;

	if ( bRedrawFlag &&
	    (interfaceInfo.showMenu == 0 || interfaceInfo.customSliderVisibleInMenu != 0) )
	{
		interface_displayMenu(1);
	}
}

void interface_notifyText(const char *text, int bRedrawFlag)
{
	if (text != NULL)
	{
		STRMAXCPY(interfaceInfo.notifyText, text, MENU_ENTRY_INFO_LENGTH);
	} else
	{
		interfaceInfo.notifyText[0] = 0;
	}

	if (bRedrawFlag)
	{
		interface_displayMenu(1);
	}
}


void interface_sliderSetText(const char *text)
{
	if (text != NULL)
	{
		STRMAXCPY(interfaceSlider.textValue, text, MENU_ENTRY_INFO_LENGTH);
	} else
	{
		interfaceSlider.textValue[0] = 0;
	}

}

void interface_sliderSetKeyCallback(menuConfirmFunction callback)
{
	interfaceSlider.customKeyCallback = callback;
}

void interface_sliderSetHideDelay(int delay)
{
	interfaceSlider.hideDelay = delay;
}

void interface_sliderSetMinValue(int minValue)
{
	interfaceSlider.minValue = minValue;
}

void interface_sliderSetMaxValue(int maxValue)
{
	interfaceSlider.maxValue = maxValue;
}

void interface_sliderSetCallbacks(sliderGetCallback getcallback, sliderSetCallback setcallback, void *pArg)
{
	interfaceSlider.getCallback = getcallback;
	interfaceSlider.setCallback = setcallback;
	interfaceSlider.pArg = pArg;
}

void interface_sliderSetDivisions(int count)
{
	interfaceSlider.divisions = count;
}

void interface_sliderSetSize(int width, int height)
{
	interfaceSlider.width = width;
	interfaceSlider.height = height;
}

void interface_sliderShow( int bShowFlag, int bRedrawFlag )
{
	interfaceInfo.showSliderControl = bShowFlag;

	if (bShowFlag && interfaceSlider.hideDelay > 0)
	{
		interface_addEvent(interface_hideSliderControl, NULL, interfaceSlider.hideDelay*1000, 1);
	} else
	{
		interface_removeEvent(interface_hideSliderControl, NULL);
		interfaceSlider.hideDelay = 0;
		// Allow slider client to execute some actions on slider exit (like settings save)
		if (interfaceSlider.customKeyCallback != NULL)
		{
			interfaceCommandEvent_t mycmd;
			mycmd.command = interfaceCommandExit;
			interfaceSlider.customKeyCallback(interfaceInfo.currentMenu, &mycmd, interfaceSlider.pArg);
		}
	}

	if (bRedrawFlag)
	{
		interface_displayMenu(1);
	}
}

int interface_symbolLookup( int num, int repeat, int *offset )
{
	if ( num < 0 || num > 9 || offset == NULL )
		return 0;

	if (repeat)
	{
		(*offset)++;
		{
			if (symbol_table[num][*offset] == 0)
			{
				*offset = 0;
			}
		}
	} else
	{
		*offset = 0;
	}

#ifdef DEBUG
#ifdef WCHAR_SUPPORT
	unsigned char tmp[10];
	memset(tmp,0,sizeof(tmp));
	utf8_wctomb(tmp, symbol_table[num][*offset], sizeof(tmp));
	dprintf("%s: '%s' (0x%X)\n", __FUNCTION__, tmp, symbol_table[num][*offset]);
#else
	dprintf("%s: %c\n", __FUNCTION__, symbol_table[num][*offset]);
#endif
#endif

	return symbol_table[num][*offset];
}

int interface_enterTextGetValue( interfaceEnterTextInfo_t *field )
{
	int enumPattern;
	//int patternLength;
	int dest;
	int i,j;
	char tmpbuf[16];
#ifdef WCHAR_SUPPORT
	int mbstr_avail;
	int ucstr_index;
#endif

	/* Collect field values */
	enumPattern = 0;
	memset(field->value, 0, sizeof(field->value));
	dest = 0;
	for (i=0; i<(int)strlen(field->pattern); i++)
	{
		if (field->pattern[i] == '\\')
		{
			i+=2;
			if (field->pattern[i] == '{')
			{
				/* Got fixed length pattern */
				j = i+1;
				/* Find out length */
				for (;field->pattern[i] != '}';i++);
				memset(tmpbuf, 0, sizeof(tmpbuf));
				strncpy(tmpbuf, &field->pattern[j], i-j);
				//patternLength = atoi(tmpbuf);
			}
			/* else
			{
				// Variable length pattern
				patternLength = 0;
			}
			if ((patternLength > 0 && patternLength != strlen(field->subPatterns[enumPattern].data)) ||
				(patternLength == 0 && field->pattern[i] == '+' && strlen(field->subPatterns[enumPattern].data) == 0) )
			{
				interface_showMessageBox(_T("ERR_NOT_ALL_FIELDS"), IMAGE_DIR "thumbnail_warning.png", 0);
				return -1;
			}*/
#ifdef WCHAR_SUPPORT
			int ret;
			mbstr_avail = sizeof(field->value) - dest;
			ucstr_index = 0;
			while ( field->subPatterns[enumPattern].data[ucstr_index] &&
			  (ret = utf8_wctomb((unsigned char *)&field->value[dest], field->subPatterns[enumPattern].data[ucstr_index], mbstr_avail)) > 0 )
			{
				dest        += ret;
				mbstr_avail -= ret;
				ucstr_index ++;
			}
#else
			strcpy(&field->value[dest], field->subPatterns[enumPattern].data);
			dest += strlen(field->subPatterns[enumPattern].data);
#endif
			enumPattern++;
		} else
		{
			field->value[dest++] = field->pattern[i];
		}
	}
	return dest;
}

int interface_enterTextShow( interfaceEnterTextInfo_t *field, size_t bufferLength, char *buf )
{
	char tmpbuf[16];
	int i,j, pos, len;
	int enumPattern;

	/* Go through patterns */
	enumPattern = 0;
	pos = 0;
	for (i=0; i<(int)strlen(field->pattern); i++)
	{
		/* got pattern specifier */
		if (field->pattern[i] == '\\')
		{
			/*if (field->pattern[i+1] != 'd')
			{
				interface_showMessageBox(_T("ERR_MW_ALPHANUMERIC_NOT_SUPPORTED"), IMAGE_DIR "thumbnail_warning.png", 0);
				return 0;
			}*/
			/* skip format specifier as we only support digits */
			i+=2;
			if (field->pattern[i] == '{')
			{
				/* Got fixed length pattern */
				j = i+1;
				/* Find out length */
				for (;field->pattern[i] != '}';i++);
				memset(tmpbuf, 0, sizeof(tmpbuf));
				strncpy(tmpbuf, &field->pattern[j], i-j);
				len = atoi(tmpbuf);
				/* Fill fixed length field */;
				for (j=0;j<=len;j++)
				{
					if (field->currentPattern == enumPattern && (int)field->currentSymbol == j)
					{
						buf[pos++] = ']';
						buf[pos++] = '[';
					}
					if (j < len)
					{
#ifdef WCHAR_SUPPORT
						if ( field->subPatterns[enumPattern].data[j] == L'\0' )
						{
							buf[pos++] = '_';
						} else
						{
							int ret = utf8_wctomb((unsigned char *)&buf[pos], field->subPatterns[enumPattern].data[j], MAX_MESSAGE_BOX_LENGTH-pos);
							if ( ret>0 )
								pos += ret;
						}
#else
						buf[pos++] = field->subPatterns[enumPattern].data[j] == 0 ? '_' : field->subPatterns[enumPattern].data[j];
#endif
					}
					if (field->currentPattern == enumPattern && (int)field->currentSymbol == j)
					{
						//buf[pos++] = ']';
					}
				}
			} else
			{
				/* Got variable length pattern */
				if ((field->pattern[i] == '*' || field->pattern[i] == '+') && field->subPatterns[enumPattern].data[0] == 0)
				{
					/* Empty field */
					if (field->currentPattern == enumPattern && field->currentSymbol == 0)
					{
						buf[pos++] = ']';
						buf[pos++] = '[';
					}
					buf[pos++] = '_';
					if (field->currentPattern == enumPattern && field->currentSymbol == 0)
					{
						//buf[pos++] = ']';
					}
				} else
				{
					/* Non-empty field */
#ifdef WCHAR_SUPPORT
					for (j=0;j<=(int)wcslen(field->subPatterns[enumPattern].data); j++)
#else
					for (j=0;j<=(int)strlen(field->subPatterns[enumPattern].data); j++)
#endif
					{
						if (field->currentPattern == enumPattern && (int)field->currentSymbol == j)
						{
							buf[pos++] = ']';
							buf[pos++] = '[';
						}
#ifdef WCHAR_SUPPORT
						if (j < (int)wcslen(field->subPatterns[enumPattern].data))
						{
							int ret = utf8_wctomb((unsigned char *)&buf[pos], field->subPatterns[enumPattern].data[j], MAX_MESSAGE_BOX_LENGTH-pos);
							if ( ret>0 )
								pos += ret;
						}
#else
						if (j < (int)strlen(field->subPatterns[enumPattern].data))
						{
							buf[pos++] = field->subPatterns[enumPattern].data[j];
						}
#endif
						else
						{
							buf[pos++] = '_';
						}
						if (field->currentPattern == enumPattern && (int)field->currentSymbol == j)
						{
							//buf[pos++] = ']';
						}
					}
				}
			}
			enumPattern++;
		} else
		{
			/* Got fixed symbol in pattern */
			buf[pos++] = field->pattern[i];
		}
		buf[pos] = 0;
	}

	return 0;
}

static int interface_enterTextGetPatternCount(interfaceEnterTextInfo_t* field, int *ppatternCount, int *ppatternLength)
{
	int patternCount = 0;
	int patternLength = 0;
	int i, j;
	char tmpbuf[16];

	for (i=0; i<(int)strlen(field->pattern); i++)
	{
		if (field->pattern[i] == '\\')
		{
			if (patternCount == field->currentPattern)
			{
				i+=2;
				if (field->pattern[i] == '{')
				{
					/* Got fixed length pattern */
					j = i+1;
					/* Find out length */
					for (;field->pattern[i] != '}';i++);
					memset(tmpbuf, 0, sizeof(tmpbuf));
					strncpy(tmpbuf, &field->pattern[j], i-j);
					patternLength = atoi(tmpbuf);
				} else
				{
					/* Variable length pattern */
					patternLength = 0;
				}
			}
			patternCount++;
		}
	}
	if ( ppatternCount  != NULL )
		*ppatternCount  = patternCount;
	if ( ppatternLength != NULL )
		*ppatternLength = patternLength;
	return patternCount;
}

int interface_enterTextProcessCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	interfaceEnterTextInfo_t *field = (interfaceEnterTextInfo_t *)pArg;
	int i;
	int patternCount;
	int patternLength;
	int posdiff = 0;
	int ret = 0;
#ifdef WCHAR_SUPPORT
	wchar_t newchar = 0;
#else
	char newchar = 0;
#endif

	dprintf("%s: %d (%c)\n", __FUNCTION__, cmd->command, cmd->command);

	appControlInfo.inputMode = field->inputMode; 
	
	if (field->inputMode == inputModeABC &&
		((cmd->source != DID_KEYBOARD && /* not keyboard */
		cmd->repeat != 0 &&
		cmd->original >= interfaceCommand0 &&
		cmd->original <= interfaceCommand9) ||
		(cmd->original == interfaceCommandUp ||
		 cmd->original == interfaceCommandDown)))
	{
		interfaceCommandEvent_t newcmd;
 		//eprintf("%s: repeat symbol\n", __FUNCTION__);
		newcmd.command = 8;
		newcmd.repeat  = 0;
		newcmd.source  = DID_KEYBOARD;
		interface_enterTextProcessCommand(pMenu, &newcmd, pArg);
	}
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit)
	{
		if ( field->pCallback != NULL )
			field->pCallback(pMenu, NULL, field->pArg);

		appControlInfo.inputMode = inputModeDirect;
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		interface_enterTextGetValue(field);
		if (field->pCallback != NULL)
		{
			ret = field->pCallback(pMenu, field->value, field->pArg);
		}
		appControlInfo.inputMode = inputModeDirect;
		return ret;
	} else if (cmd->command == interfaceCommandRight)
	{
		posdiff = 1;
	} else if (cmd->command == interfaceCommandLeft)
	{
		posdiff = -1;
	} else if (cmd->command == interfaceCommandUp && field->inputMode == inputModeABC)
	{
		int alph = 0;

		while (alphabet[alph] != field->lastChar && alphabet[alph] != 0)
		{
			alph++;
		}

		if (alphabet[alph] == 0 || alphabet[++alph] == 0)
		{
			alph = 0;
		}

		newchar = alphabet[alph];
	} else if (cmd->command == interfaceCommandDown && field->inputMode == inputModeABC)
	{
		int alph = 0;

		while (alphabet[alph] != field->lastChar && alphabet[alph] != 0)
		{
			alph++;
		}

		alph--;

		if (alph < 0)
		{
			while (alphabet[alph+1] != 0)
			{
				alph++;
			}
		}

		newchar = alphabet[alph];
	} else if (cmd->command == 127)
	{
		newchar = 127; // delete
	} else if (cmd->command == 8 || cmd->command == interfaceCommandBlue
#if (defined STB225)
	        || cmd->command == interfaceCommandBack
#endif
	          )
	{
		newchar = 8;// backspace
	} else if (cmd->source != DID_KEYBOARD &&
	          (cmd->original < interfaceCommand0 ||
	           cmd->original > interfaceCommand9) )
	{
		dprintf("%s: skip\n", __FUNCTION__);
		newchar = 0; // deny non-digit input from not keyboard
	} else if (cmd->command == interfaceCommandNone)
	{
#ifdef WCHAR_SUPPORT
		field->currentSymbol = wcslen(field->subPatterns[field->currentPattern].data);
#else
		field->currentSymbol = strlen(field->subPatterns[field->currentPattern].data);
#endif
	} else
#ifdef WCHAR_SUPPORT
	if (cmd->command >= 32 && cmd->command < 0xF000 ) // wchar
#else
	if (cmd->command >= 32 && cmd->command <= 126) // 7-bit ASCII charset
#endif
	{
		newchar = cmd->command;
	} else
	{
		return 1;
	}

	dprintf("%s: cmd: %d, org: %d, repeat: %d, source: %d\n", __FUNCTION__, cmd->command, cmd->original, cmd->repeat, cmd->source);
#ifdef WCHAR_SUPPORT
#ifdef DEBUG
	unsigned char tmp[10];
	memset(tmp,0,sizeof(tmp));
	i= utf8_wctomb(tmp, cmd->command, 10);
	eprintf("%s: symbol: '%s' (%d)\n", __FUNCTION__, tmp, i);
#endif
#endif

	/* Get pattern count */
	interface_enterTextGetPatternCount( field, &patternCount, &patternLength );

	/* Check if we've got a new char */
	if (newchar != 0 && newchar != 8 && newchar != 127)
	{
		if (patternLength > 0 && field->currentSymbol >= patternLength)
		{
			field->currentSymbol = 0;
			field->currentPattern++;
			if (field->currentPattern >= patternCount)
			{
				field->currentPattern = 0;
			}
		}
#ifdef WCHAR_SUPPORT
		if (wcslen(field->subPatterns[field->currentPattern].data) < MAX_FIELD_PATTERN_LENGTH-1)
#else
		if (strlen(field->subPatterns[field->currentPattern].data) < MAX_FIELD_PATTERN_LENGTH-1)
#endif
		{
#ifdef WCHAR_SUPPORT
			if (patternLength == 0 || (int)wcslen(field->subPatterns[field->currentPattern].data) < patternLength)
#else
			if (patternLength == 0 || (int)strlen(field->subPatterns[field->currentPattern].data) < patternLength)
#endif
			{
#ifdef WCHAR_SUPPORT
				wchar_t chnext, chcur;
#else
				char chnext, chcur;
#endif
				chcur = field->subPatterns[field->currentPattern].data[field->currentSymbol];
				for (i=field->currentSymbol+1; chcur!=0; i++)
				{
					chnext = field->subPatterns[field->currentPattern].data[i];
					field->subPatterns[field->currentPattern].data[i] = chcur;
					chcur = chnext;
				}
			}
			field->subPatterns[field->currentPattern].data[field->currentSymbol] = newchar;
			posdiff = 1;
			field->lastChar = newchar;
		}
	} else if (newchar == 127)
	{
		if (field->subPatterns[field->currentPattern].data[field->currentSymbol] != 0)
		{
			if (field->currentSymbol-1 >= 0)
			{
				field->lastChar = field->subPatterns[field->currentPattern].data[field->currentSymbol-1];
			} else
			{
				field->lastChar = 0;
			}
			for (i=field->currentSymbol;field->subPatterns[field->currentPattern].data[i]!=0; i++)
			{
				field->subPatterns[field->currentPattern].data[i] = field->subPatterns[field->currentPattern].data[i+1];
			}
		} else if (field->currentSymbol > 0)
		{
			newchar = 8;
		}
	}

	if (newchar == 8)
	{
		if (field->currentSymbol == 0 && field->currentPattern > 0)
		{
			field->currentPattern--;
#ifdef WCHAR_SUPPORT
			field->currentSymbol = wcslen(field->subPatterns[field->currentPattern].data);
#else
			field->currentSymbol = strlen(field->subPatterns[field->currentPattern].data);
#endif
		}

		if (field->currentSymbol > 0)
		{
			if (field->subPatterns[field->currentPattern].data[field->currentSymbol-1] != 0)
			{
				field->lastChar = field->subPatterns[field->currentPattern].data[field->currentSymbol-1];
				for (i=field->currentSymbol-1;field->subPatterns[field->currentPattern].data[i]!=0; i++)
				{
					field->subPatterns[field->currentPattern].data[i] = field->subPatterns[field->currentPattern].data[i+1];
				}
				field->currentSymbol--;
			}
		} else
		{
			field->lastChar = 0;
		}
	}

	/* Move input marker */
	field->currentSymbol += posdiff;
	if (posdiff > 0 && ((patternLength > 0 && field->currentSymbol >= patternLength) ||
#ifdef WCHAR_SUPPORT
		field->currentSymbol > (int)wcslen(field->subPatterns[field->currentPattern].data)
#else
		field->currentSymbol > (int)strlen(field->subPatterns[field->currentPattern].data)
#endif
	))
	{
		field->currentSymbol = 0;
		field->currentPattern++;
		if (field->currentPattern >= patternCount)
		{
			field->currentPattern = 0;
		}
	} else if (posdiff < 0 && field->currentSymbol < 0)
	{
		field->currentPattern--;
		if (field->currentPattern < 0)
		{
			field->currentPattern = patternCount-1;
		}
#ifdef WCHAR_SUPPORT
		field->currentSymbol = wcslen(field->subPatterns[field->currentPattern].data);
#else
		field->currentSymbol = strlen(field->subPatterns[field->currentPattern].data);
#endif
	}

	/* Recalculate pattern count */
	//interface_enterTextGetPatternCount( field, &patternCount, &patternLength );

	/* Ensure that our text marker is inside bounds of fixed-length field */
	/*if (patternLength > 0 && field->currentSymbol >= patternLength)
	{
		field->currentSymbol = patternLength-1;
	}*/
	return 0;
}

static int interface_enterTextCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	interfaceEnterTextInfo_t *field = (interfaceEnterTextInfo_t *)pArg;
	char buf[MAX_MESSAGE_BOX_LENGTH], *str;
	int ret;

	ret = interface_enterTextProcessCommand(pMenu, cmd, pArg);
	switch ( cmd->command )
	{
		// see interface_enterTextProcessCommand for full list of accepted commands
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
		case interfaceCommandRed:
		case interfaceCommandExit:
			dfree(field);
			return ret;
		default:;
	}

	strcpy(buf, field->description);
	strcat(buf, "\n\n");

	str = &buf[strlen(buf)];
	interface_enterTextShow(field, sizeof(buf) - (str-buf), str);

	/*strcat(buf, "\n\n");
	strcat(buf, _T("EDIT_TEXT_BUTTONS"));*/
	interface_showConfirmationBox(buf, thumbnail_account, interface_enterTextCallback, pArg);

	return 1;
}

int interface_getText(interfaceMenu_t *pMenu, const char *description, const char *pattern, menuEnterTextFunction pCallback, menuEnterTextFieldsFunction pGetFields, stb810_inputMode inputMode, void *pArg)
{
	int i;
	interfaceEnterTextInfo_t *info;
	char *field;
	interfaceCommandEvent_t cmd;
	int patternCount;
#ifdef WCHAR_SUPPORT
	int dest_index;
	int mb_count;
#endif

	info = (interfaceEnterTextInfo_t *)dmalloc(sizeof(interfaceEnterTextInfo_t));

	memset(info, 0, sizeof(interfaceEnterTextInfo_t));

	info->currentPattern = 0;
	info->currentSymbol = 0;
	strcpy(info->description, description);
	info->pArg = pArg;
	strcpy(info->pattern, pattern);
	info->pCallback = pCallback;
	info->inputMode = inputMode;
	info->lastChar = 0;

	interface_enterTextGetPatternCount(info, &patternCount, NULL);

	for (i=0;i<patternCount; i++)
	{
		if (pGetFields != NULL)
		{
			field = pGetFields(i, pArg);
			if (field != NULL)
			{
#ifdef WCHAR_SUPPORT
				info->subPatterns[i].data[0] = 0;
				dest_index = 0;
				while (*field && dest_index < MAX_FIELD_PATTERN_LENGTH && (mb_count = utf8_mbtowc(&info->subPatterns[i].data[dest_index], (unsigned char *)field, strlen(field))) > 0 )
				{
					dest_index++;
					field += mb_count;
				}
#else
				strncpy(info->subPatterns[i].data, field, sizeof(info->subPatterns[i].data));
				info->subPatterns[i].data[sizeof(info->subPatterns[i].data)-1] = 0;
#endif
			}
		}
	}

	cmd.command = cmd.original = interfaceCommandNone;
	cmd.repeat = 0;
	cmd.source = DID_KEYBOARD;

	return interface_enterTextCallback(pMenu, &cmd, (void*)info);
}

int  interface_getMenuEntryCount(interfaceMenu_t *pMenu)
{
	return pMenu->menuEntryCount;
}

int interface_getMenuEntryInfo(interfaceMenu_t *pMenu, int entryIndex, char* entryInfo, size_t entryInfoLength)
{
	if (entryIndex < 0 || entryIndex >= pMenu->menuEntryCount)
		return -1;
	strncpy(entryInfo,pMenu->menuEntry[entryIndex].info,entryInfoLength);
	if ( pMenu->menuEntry[entryIndex].infoReplacedChar != 0 )
	{
	    entryInfo[pMenu->menuEntry[entryIndex].infoReplacedCharPos] = pMenu->menuEntry[entryIndex].infoReplacedChar;
	    strncpy(&entryInfo[pMenu->menuEntry[entryIndex].infoReplacedCharPos+1],
		&pMenu->menuEntry[entryIndex].info[pMenu->menuEntry[entryIndex].infoReplacedCharPos+1],
		entryInfoLength-pMenu->menuEntry[entryIndex].infoReplacedCharPos-1);
	}
	dprintf("%s: %d %s\n", __FUNCTION__, entryIndex, entryInfo);
	return 0;
}

void* interface_getMenuEntryArg(interfaceMenu_t *pMenu, int entryIndex)
{
	if (entryIndex < 0 || entryIndex >= pMenu->menuEntryCount)
		return NULL;
	return pMenu->menuEntry[entryIndex].pArg;
}

menuActionFunction interface_getMenuEntryAction(interfaceMenu_t *pMenu, int entryIndex)
{
	if (entryIndex < 0 || entryIndex >= pMenu->menuEntryCount)
		return NULL;
	return pMenu->menuEntry[entryIndex].pAction;
}

void interface_setMenuEntryImage(interfaceMenu_t *pMenu, int entryIndex, char* image)
{
	if (entryIndex < 0 || entryIndex >= pMenu->menuEntryCount)
		return;
	pMenu->menuEntry[entryIndex].image = image;
}

void interface_setMenuName(interfaceMenu_t *pMenu, const char* name, size_t nameLength)
{
	if (name!=NULL)
	{
		strncpy(pMenu->name,name,min(nameLength,MENU_ENTRY_INFO_LENGTH));
		if (nameLength > MENU_ENTRY_INFO_LENGTH)
			smartLineTrim(pMenu->name, MENU_ENTRY_INFO_LENGTH);
	}
	else
		pMenu->name[0] = 0;
}

int interface_getSelectedItem(interfaceMenu_t *pMenu)
{
	return pMenu->selectedItem;
}

int interface_isMenuEntrySelectable(interfaceMenu_t *pMenu, int index)
{
	if (index < 0 || index >= pMenu->menuEntryCount)
		return 0;
	return pMenu->menuEntry[index].isSelectable;
}

void interface_playControlUpdateDescriptionThumbnail(const char* description, int thumbnail)
{
	mysem_get(interface_semaphore);
	if ( description != NULL )
	{
		strncpy(interfacePlayControl.description, description, MENU_ENTRY_INFO_LENGTH);
		interfacePlayControl.description[MENU_ENTRY_INFO_LENGTH-1] = 0;
	} else
	{
		interfacePlayControl.description[0] = 0;
	}
	if ( thumbnail >= 0 )
	{
		interfacePlayControl.descriptionThumbnail = thumbnail;
	}
	mysem_release(interface_semaphore);
}

void interface_playControlUpdateDescription(const char* description)
{
	interface_playControlUpdateDescriptionThumbnail(description, -1);
}

int interface_drawTextWW(IDirectFBFont *font, int r, int g, int b, int a, int x, int y, int w, int h, const char *text, int align)
{
	int len;
	const char *ptr, *tmp;
	char buf[MAX_MESSAGE_BOX_LENGTH];
	int fh, fa, fd, cy, width;

	// useful thing :)
	//gfx_drawRectangle(DRAWING_SURFACE, 0xFF, 0x00, 0xFF, 0xFF, x, y, w, h);

	DFBCHECK( font->GetHeight(font, &fh) );
	DFBCHECK( font->GetAscender(font, &fa) );
	fd = fh-fa; // descender
	cy = y+fa;
	ptr = text;
	do
	{
		tmp = strchr(ptr, '\n');

		if ( tmp == ptr ) // new line
		{
			ptr++;
			cy += fh;
			continue;
		} else

		len = getMaxStringLengthForFont(font, ptr, w);

		if (tmp == NULL || tmp-ptr > len)
		{
			tmp = &ptr[len];
		} else
		{
			len = tmp-ptr;
		}
		if (ptr[len] != 0)
		{
			while (tmp > ptr && *tmp != ' ' && ptr[len] != '\n')
			{
				tmp--;
			}
			if (tmp > ptr)
			{
				len = tmp-ptr;
				tmp++;
			} else
			{
				tmp = &ptr[len];
			}
		}

		memcpy(buf, ptr, len);
		buf[len] = 0;

		if (align != ALIGN_LEFT)
		{
			DFBCHECK( font->GetStringWidth(font, buf, -1, &width) );

			gfx_drawText(DRAWING_SURFACE, font, r, g, b, a, align == ALIGN_CENTER ? x+w/2-width/2 : x+w-width, cy, buf, 0, 0);
		} else
		{
			gfx_drawText(DRAWING_SURFACE, font, r, g, b, a, x, cy, buf, 0, 0);
		}

		//dprintf("%s: h %d y %d inc %d\n", __FUNCTION__, rectangle.h, rectangle.y, (rectangle.h-rectangle.y)*3/2);

		cy += fh;

		ptr = tmp;
	} while (*ptr != 0 && cy+fd-y <= h);

	/* Return used height */
	return cy-fa-y;
}

int interface_formatTextWW(const char *text, IDirectFBFont *font, int maxWidth, int maxHeight, int dest_size, char *dest, int *lineCount, int *visibleLines)
{
	int len;
	int dest_free = dest_size;
	const char *ptr, *tmp;
	char *ptr2;
	int height, fh;
	DFBRectangle rectangle;

	pgfx_font->GetHeight(font, &fh);
	*visibleLines = maxHeight / fh;
	*lineCount = 0;

	height = 0;
	ptr = text;
	ptr2 = dest;
	while ( *ptr != 0 && dest_free > 0 )
	{
		tmp = strchr(ptr, '\n');

		if ( tmp == ptr ) // new line
		{
			ptr++;
			*ptr2 = '\n';
			ptr2++;
			(*lineCount)++;
			height += fh;
			continue;
		}

		len = getMaxStringLengthForFont(font, ptr, maxWidth);

		if (tmp == NULL || tmp-ptr > len)
		{
			tmp = &ptr[len];
		} else
		{
			len = tmp-ptr;
		}
		if (ptr[len] != 0)
		{
			while (tmp > ptr && *tmp != ' ' && ptr[len] != '\n')
			{
				tmp--;
			}
			if (tmp > ptr)
			{
				len = tmp-ptr;
				tmp++;
			} else
			{
				tmp = &ptr[len];
			}
		}
		if (dest_free-len <= 0)
			len = dest_free-1;

		memcpy(ptr2, ptr, len);
		ptr2[len] = 0;

		DFBCHECK( font->GetStringExtents(font, ptr2, -1, &rectangle, NULL) );

		ptr2[len] = '\n';
		(*lineCount)++;
		++len;
		ptr2 += len;
		dest_free -= len;

		//dprintf("%s: h %d y %d inc %d\n", __FUNCTION__, rectangle.h, rectangle.y, (rectangle.h-rectangle.y)*3/2);

		height += rectangle.h;

		ptr = tmp;
	}
	if ( ptr2 > dest )
		ptr2[-1] = 0;

	return height;
}

void interface_showScrollingBox(const char *text, int icon, menuConfirmFunction pCallback, void *pArg)
{
	interface_showScrollingBoxColor(text, icon, pCallback, pArg,
		INTERFACE_MESSAGE_BOX_BORDER_RED,  INTERFACE_MESSAGE_BOX_BORDER_GREEN,
		INTERFACE_MESSAGE_BOX_BORDER_BLUE, INTERFACE_MESSAGE_BOX_BORDER_ALPHA,
		INTERFACE_MESSAGE_BOX_RED,  INTERFACE_MESSAGE_BOX_GREEN,
		INTERFACE_MESSAGE_BOX_BLUE, INTERFACE_MESSAGE_BOX_ALPHA);
}

void interface_showScrollingBoxColor(const char *text, int icon, menuConfirmFunction pCallback, void *pArg,
                                     int br, int bg, int bb, int ba,
                                     int  r, int  g, int  b, int  a)
{
	interface_showScrollingBoxCustom(text, icon, pCallback, pArg,
		interfaceInfo.clientX, interfaceInfo.clientY,
		interfaceInfo.clientWidth, interfaceInfo.clientHeight,
		br, bg, bb, ba, r, g, b, a);
}

void interface_showScrollingBoxCustom(const char *text, int icon, menuConfirmFunction pCallback, void *pArg,
                                      int x, int y, int w, int h,
                                      int br, int bg, int bb, int ba,
                                      int  r, int  g, int  b, int  a)
{
	int maxWidth;

	//dprintf("%s: scrolling in: '%s'\n", __FUNCTION__, text);

	maxWidth = interfaceInfo.clientWidth - 2*interfaceInfo.paddingSize - INTERFACE_SCROLLBAR_WIDTH - ( icon > 0 ? interfaceInfo.thumbnailSize + interfaceInfo.paddingSize : 0 );
	interface_formatTextWW(text, pgfx_font, maxWidth, interfaceInfo.clientHeight - 2*interfaceInfo.paddingSize, sizeof(interfaceInfo.messageBox.message), interfaceInfo.messageBox.message, &interfaceInfo.messageBox.scrolling.lineCount, &interfaceInfo.messageBox.scrolling.visibleLines);

	interfaceInfo.messageBox.scrolling.maxOffset = interfaceInfo.messageBox.scrolling.lineCount - interfaceInfo.messageBox.scrolling.visibleLines;
	if ( interfaceInfo.messageBox.scrolling.maxOffset < 0 )
	{
		interfaceInfo.messageBox.scrolling.maxOffset = 0;
	}
	interfaceInfo.messageBox.scrolling.offset = 0;
	interfaceInfo.messageBox.icon = icon;
	interfaceInfo.messageBox.pCallback = pCallback;
	interfaceInfo.messageBox.pArg = pArg;
	messageBox_setDefaultColors();
	interfaceInfo.messageBox.colors.background.R = r;
	interfaceInfo.messageBox.colors.background.G = g;
	interfaceInfo.messageBox.colors.background.B = b;
	interfaceInfo.messageBox.colors.background.A = a;
	interfaceInfo.messageBox.colors.border.R = br;
	interfaceInfo.messageBox.colors.border.G = bg;
	interfaceInfo.messageBox.colors.border.B = bb;
	interfaceInfo.messageBox.colors.border.A = ba;
	interfaceInfo.messageBox.target.x = x;
	interfaceInfo.messageBox.target.y = y;
	interfaceInfo.messageBox.target.w = w;
	interfaceInfo.messageBox.target.h = h;
	interfaceInfo.messageBox.type = interfaceMessageBoxScrolling;

	interface_displayMenu(1);
}

void interface_showPosterBox(const char *text, const char *title, int tr, int tg, int tb, int ta, int icon, const char *poster, menuConfirmFunction pCallback, void *pArg)
{
	dprintf("%s: title '%s' poster '%s' text '%s'\n", __FUNCTION__, title, poster, text ? text : "");
	eprintf("%s: title '%s' poster '%s' text '%s'\n", __FUNCTION__, title, poster, text ? text : "");

	if ( text )
	{
		interface_formatTextWW(text, pgfx_font, interfaceInfo.clientWidth - INTERFACE_POSTER_PICTURE_WIDTH - INTERFACE_SCROLLBAR_WIDTH, interfaceInfo.clientHeight - INTERFACE_POSTER_TITLE_HEIGHT, sizeof(interfaceInfo.messageBox.message), interfaceInfo.messageBox.message, &interfaceInfo.messageBox.scrolling.lineCount, &interfaceInfo.messageBox.scrolling.visibleLines);
		interfaceInfo.messageBox.scrolling.maxOffset = interfaceInfo.messageBox.scrolling.lineCount - interfaceInfo.messageBox.scrolling.visibleLines;
		if ( interfaceInfo.messageBox.scrolling.maxOffset < 0 )
		{
			interfaceInfo.messageBox.scrolling.maxOffset = 0;
		}
		interfaceInfo.messageBox.scrolling.offset = 0;
	}
	else
		interfaceInfo.messageBox.message[0] = 0;

	if ( title )
		strcpy(interfaceInfo.messageBox.title, title);
	else
		interfaceInfo.messageBox.title[0] = 0;

	interfaceInfo.messageBox.target.x = interfaceInfo.clientX;
	interfaceInfo.messageBox.target.y = interfaceInfo.clientY;
	interfaceInfo.messageBox.target.w = interfaceInfo.clientWidth;
	interfaceInfo.messageBox.target.h = interfaceInfo.clientHeight;
	messageBox_setDefaultColors();
	interfaceInfo.messageBox.colors.title.R = tr;
	interfaceInfo.messageBox.colors.title.G = tg;
	interfaceInfo.messageBox.colors.title.B = tb;
	interfaceInfo.messageBox.colors.title.A = ta;
	interfaceInfo.messageBox.icon         = icon;
	interfaceInfo.messageBox.poster       = poster;
	interfaceInfo.messageBox.pCallback    = pCallback;
	interfaceInfo.messageBox.pArg         = pArg;
	interfaceInfo.messageBox.type         = interfaceMessageBoxPoster;

	interface_displayMenu(1);
}

void interface_drawRoundBoxColor(int x, int y, int w, int h, int r, int g, int b, int a)
{
	DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
	DFBCHECK( DRAWING_SURFACE->SetColor(DRAWING_SURFACE, r, g, b, a) );
	gfx_drawRectangle(DRAWING_SURFACE, r, g, b, a, x-INTERFACE_ROUND_CORNER_RADIUS, y, w+INTERFACE_ROUND_CORNER_RADIUS*2, h);
	// top left corner
	gfx_drawRectangle(DRAWING_SURFACE, r, g, b, a, x, y-INTERFACE_ROUND_CORNER_RADIUS, w, INTERFACE_ROUND_CORNER_RADIUS);
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS, y-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
	// bottom left corner
	gfx_drawRectangle(DRAWING_SURFACE, r, g, b, a, x, y+h, w, INTERFACE_ROUND_CORNER_RADIUS);
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x-INTERFACE_ROUND_CORNER_RADIUS, y+h, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 0, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
	// top right
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w, y-INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 0, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
	// bottom right
	interface_drawIcon(DRAWING_SURFACE, IMAGE_DIR "black_circle.png", x+w, y+h, INTERFACE_ROUND_CORNER_RADIUS, INTERFACE_ROUND_CORNER_RADIUS, 1, 1, DSBLIT_BLEND_COLORALPHA, interfaceAlignTopLeft);
}

void interface_drawRoundBox(int x, int y, int w, int h)
{
	interface_drawRoundBoxColor(x, y, w, h, INTERFACE_BACKGROUND_RED, INTERFACE_BACKGROUND_GREEN, INTERFACE_BACKGROUND_BLUE, INTERFACE_BACKGROUND_ALPHA);
}

int interface_editDateProcessCommand(pinterfaceCommandEvent_t cmd, interfaceEditEntry_t *pEditEntry)
{
	char maxDigit, newDigit;

	dprintf("%s: in %d '%s' [%d]\n", __FUNCTION__, cmd->command, pEditEntry->info.date.value, pEditEntry->info.date.selected);

	switch ( pEditEntry->info.date.selected )
	{
		case 0:  maxDigit = '3'; break;
		case 1:  maxDigit = pEditEntry->info.date.value[0] == '3' ? '1' : '9' ; break;
		case 2:  maxDigit = '1'; break;
		case 3:  maxDigit =  pEditEntry->info.date.value[2] == '1' ? '2' : '9'; break;
		//case 4:  maxDigit = '2'; break; // max year
		default: maxDigit = '9';
	}
	switch ( cmd->command )
	{
		case interfaceCommandLeft:
			if ( pEditEntry->info.date.selected > 0 )
				pEditEntry->info.date.selected--;
			else
				pEditEntry->info.date.selected = 7;
			break;
		case interfaceCommandRight:
			if ( pEditEntry->info.date.selected < 7 )
				pEditEntry->info.date.selected++;
			else
				pEditEntry->info.date.selected = 0;
			break;
		case interfaceCommandUp:
			if ( pEditEntry->info.date.value[pEditEntry->info.date.selected] >= maxDigit )
				pEditEntry->info.date.value[pEditEntry->info.date.selected] = '0';
			else
				pEditEntry->info.date.value[pEditEntry->info.date.selected]++;
			break;
		case interfaceCommandDown:
			if ( pEditEntry->info.date.value[pEditEntry->info.date.selected] == '0' ||
			    pEditEntry->info.date.value[pEditEntry->info.date.selected] >  maxDigit )
				pEditEntry->info.date.value[pEditEntry->info.date.selected] = maxDigit;
			else
				pEditEntry->info.date.value[pEditEntry->info.date.selected]--;
			break;
		case interfaceCommand0:
		case interfaceCommand1:
		case interfaceCommand2:
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand5:
		case interfaceCommand6:
		case interfaceCommand7:
		case interfaceCommand8:
		case interfaceCommand9:
			newDigit = '0' + cmd->command - interfaceCommand0;
			if ( newDigit <= maxDigit )
			{
				pEditEntry->info.date.value[pEditEntry->info.date.selected] = newDigit;
				switch (pEditEntry->info.date.selected)
				{
					case 0:
						if (pEditEntry->info.date.value[0] == '3' && pEditEntry->info.date.value[1] > '1')
							pEditEntry->info.date.value[1] = '0';
						if (pEditEntry->info.date.value[0] == '0' && pEditEntry->info.date.value[1] == '0')
							pEditEntry->info.date.value[1] = '1';
						break;
					case 2:
						if (pEditEntry->info.date.value[2] == '1' && pEditEntry->info.date.value[3] > '2')
							pEditEntry->info.date.value[3] = '0';
						if (pEditEntry->info.date.value[2] == '0' && pEditEntry->info.date.value[3] == '0')
							pEditEntry->info.date.value[3] = '1';
						break;
					default:;
				}
				if ( pEditEntry->info.date.selected < 7 )
					pEditEntry->info.date.selected++;
				else
					pEditEntry->info.date.selected = 0;
			}
			break;
		case interfaceCommandOk:
		case interfaceCommandRed:
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandExit:
			pEditEntry->info.date.selected = 0;
			return 1; // resets state before deactivating edit control
		default:
			eprintf("%s: unknown command %d\n", __FUNCTION__, cmd->command);
			return 1;
	}
	interface_displayMenu(1);
	return 0;
}

int interface_editTimeProcessCommand(pinterfaceCommandEvent_t cmd, interfaceEditEntry_t *pEditEntry)
{
	char newDigit;
	char maxDigit = pEditEntry->info.time.selected == 2 /* tens of minutes */ ? '5' : '9';
	switch ( pEditEntry->info.time.type )
	{
		case interfaceEditTime01:
			if ( pEditEntry->info.time.selected == 0 )
				maxDigit = '5';
			break;
		case interfaceEditTime24:
			if ( pEditEntry->info.time.selected == 0 )
				maxDigit = '2';
			if ( pEditEntry->info.time.selected == 1 && pEditEntry->info.time.value[0] == '2' )
				maxDigit = '4';
			break;
		default:;
	}

	switch ( cmd->command )
	{
		case interfaceCommandLeft:
			if ( pEditEntry->info.time.selected > 0 )
				pEditEntry->info.time.selected--;
			else
				pEditEntry->info.time.selected = 3;
			break;
		case interfaceCommandRight:
			if ( pEditEntry->info.time.selected < 3 )
				pEditEntry->info.time.selected++;
			else
				pEditEntry->info.time.selected = 0;
			break;
		case interfaceCommandUp:
			if ( pEditEntry->info.time.value[pEditEntry->info.time.selected] == maxDigit )
				pEditEntry->info.time.value[pEditEntry->info.time.selected] = '0';
			else
				pEditEntry->info.time.value[pEditEntry->info.time.selected]++;
			break;
		case interfaceCommandDown:
			if ( pEditEntry->info.time.value[pEditEntry->info.time.selected] == '0' )
				pEditEntry->info.time.value[pEditEntry->info.time.selected] = maxDigit;
			else
				pEditEntry->info.time.value[pEditEntry->info.time.selected]--;
			break;
		case interfaceCommand0:
		case interfaceCommand1:
		case interfaceCommand2:
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand5:
		case interfaceCommand6:
		case interfaceCommand7:
		case interfaceCommand8:
		case interfaceCommand9:
			newDigit = '0' + cmd->command - interfaceCommand0;
			if ( newDigit <= maxDigit )
			{
				pEditEntry->info.time.value[pEditEntry->info.time.selected] = newDigit;
				if ( pEditEntry->info.time.type == interfaceEditTime24 &&
				    pEditEntry->info.time.selected == 0   &&
				    pEditEntry->info.time.value[0] == '2' &&
				    pEditEntry->info.time.value[1] >  '4')
					pEditEntry->info.time.value[1] = '0';

				if ( pEditEntry->info.time.selected < 3 )
					pEditEntry->info.time.selected++;
				else
					pEditEntry->info.time.selected = 0;
			}
			break;
		case interfaceCommandOk:
		case interfaceCommandRed:
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandExit:
			pEditEntry->info.time.selected = 0;
			return 1; // resets state before deactivating edit control
		default:
			//dprintf("%s: unknown command %d\n", __FUNCTION__, cmd->command);
			return 1;
	}
	interface_displayMenu(1);
	return 0;
}

int interface_editSelectProcessCommand(pinterfaceCommandEvent_t cmd, interfaceEditEntry_t *pEditEntry)
{
	int newIndex;
	switch ( cmd->command )
	{
		case interfaceCommandUp:
			pEditEntry->info.select.selected = (pEditEntry->info.select.selected + pEditEntry->info.select.count - 1) % pEditEntry->info.select.count;
			break;
		case interfaceCommandDown:
			pEditEntry->info.select.selected = (pEditEntry->info.select.selected + 1) % pEditEntry->info.select.count;
			break;
		case interfaceCommand0:
		case interfaceCommand1:
		case interfaceCommand2:
		case interfaceCommand3:
		case interfaceCommand4:
		case interfaceCommand5:
		case interfaceCommand6:
		case interfaceCommand7:
		case interfaceCommand8:
		case interfaceCommand9:
			if ( cmd->command == interfaceCommand0 )
				newIndex = 9;
			else
				newIndex = '0' + cmd->command - interfaceCommand0 - 1;
			if ( newIndex < pEditEntry->info.select.count )
				pEditEntry->info.select.selected = newIndex;
			break;
		default:
			//dprintf("%s: unknown command %d\n", __FUNCTION__, cmd->command);
			return 1;
	}
	interface_displayMenu(1);
	return 0;
}

inline int interface_addEditEntryDate(interfaceMenu_t *pMenu, const char *text, menuActionFunction pActivate, menuActionFunction pReset, void *pArg, int thumbnail, interfaceEditEntry_t *pEditEntry)
{
	pEditEntry->pArg               = pArg;
	pEditEntry->pCallback          = interface_editDateProcessCommand;
	pEditEntry->type               = interfaceEditDate;
	pEditEntry->info.date.selected = 0;

	return interface_addMenuEntryCustom( pMenu, interfaceMenuEntryEdit, text, strlen(text)+1, 1, pActivate, NULL, pReset, NULL /* pDisplay */, pEditEntry, thumbnail );
}

inline int interface_addEditEntryTime(interfaceMenu_t *pMenu, const char *text, menuActionFunction pActivate, menuActionFunction pReset, void *pArg, int thumbnail, interfaceEditEntry_t *pEditEntry)
{
	pEditEntry->pArg               = pArg;
	pEditEntry->pCallback          = interface_editTimeProcessCommand;
	pEditEntry->type               = interfaceEditTime;
	pEditEntry->info.date.selected = 0;

	return interface_addMenuEntryCustom( pMenu, interfaceMenuEntryEdit, text, strlen(text)+1, 1, pActivate, NULL, pReset, NULL /* pDisplay */, pEditEntry, thumbnail );
}

static int interface_editNext(interfaceMenu_t *pMenu)
{
	if ( pMenu->menuEntryCount <= 0 )
		return -1;
	pMenu->selectedItem = (pMenu->selectedItem+1) % pMenu->menuEntryCount;
	if ( pMenu->selectedItem >= 0 && pMenu->menuEntry[pMenu->selectedItem].type == interfaceMenuEntryEdit )
	{
#ifdef ENABLE_REGPLAT
		if (pMenu->menuEntry[pMenu->selectedItem].isSelectable == 0)
		{
			while ((pMenu->selectedItem + 1 < pMenu->menuEntryCount) && (pMenu->menuEntry[pMenu->selectedItem].isSelectable == 0))
				pMenu->selectedItem++;
		}
#endif
		if (pMenu->menuEntry[pMenu->selectedItem].pArg){
			((interfaceEditEntry_t*)pMenu->menuEntry[pMenu->selectedItem].pArg)->active = 1;
		}
	}
	return pMenu->selectedItem;
}

static int interface_editEntryDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i)
{
	interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t*)pMenu->menuEntry[i].pArg;
	int selected = pMenu->selectedItem;
	DFBRectangle rectangle;
	int ret = 1;

	memcpy(&rectangle, rect, sizeof(rectangle));
	rectangle.w -= INTERFACE_VALUE_WIDTH;
	if ( selected == i && pEditEntry->active )
	{
		pMenu->selectedItem = -1; // no selection rectangle trick
	}
	interface_listEntryDisplay(pMenu, &rectangle, i);
	pMenu->selectedItem = selected;
	rectangle.x = rectangle.x+rectangle.w;
	rectangle.w = INTERFACE_VALUE_WIDTH;
	if ( selected == i && pEditEntry->active == 0 )
	{
		DFBCHECK( DRAWING_SURFACE->SetDrawingFlags(DRAWING_SURFACE, DSDRAW_BLEND) );
		// selection rectangle
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, rectangle.x, rectangle.y, rectangle.w, rectangle.h);
	}

	switch ( pEditEntry->type )
	{
		case interfaceEditDate:
			ret = interface_editDateDisplay(pMenu, &rectangle, i);
			break;
		case interfaceEditTime:
			ret = interface_editTimeDisplay(pMenu, &rectangle, i);
			break;
		default:
			eprintf("%s: unsupported edit entry type %d\n", __FUNCTION__, pEditEntry->type);
	}
	
	return ret;
}

static int interface_editDateDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i)
{
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t *)pMenu;
	interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t*)pMenu->menuEntry[i].pArg;
	char entryText[2];
	DFBRectangle rectangle;
	
	int r,g,b,a;
	int x,y,w;
	int fh;

	int dig_w;
	int dot_w;

	pgfx_font->GetHeight(pgfx_font, &fh);
	if ( pListMenu->listMenuType == interfaceListMenuNoThumbnail )
	{
		y = rect->y;
	} else
	{
		y =rect->y + pListMenu->baseMenu.thumbnailHeight/2 + fh/4;
	}
	r = INTERFACE_BOOKMARK_RED;
	g = INTERFACE_BOOKMARK_GREEN;
	b = INTERFACE_BOOKMARK_BLUE;
	a = INTERFACE_BOOKMARK_ALPHA;
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, "4", -1, &rectangle, NULL) );
	dig_w = rectangle.w;

	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, ".", -1, &rectangle, NULL) );
	dot_w = rectangle.w;

	i = 0;
	entryText[1] = 0;
	//x = rect->x + rect->w - INTERFACE_VALUE_WIDTH/2 - (dig_w*(8/2)+dot_w*(2/2)); // center align
	x = rect->x;

	if ( pEditEntry->active )
	{
		w = x + dig_w * pEditEntry->info.date.selected;
		if (pEditEntry->info.date.selected > 3)
			w += dot_w*2;
		else if (pEditEntry->info.date.selected > 1)
			w += dot_w;
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, w, rect->y, dig_w, rect->h);
	}

	entryText[0] = pEditEntry->info.date.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dig_w;
	i++;

	entryText[0] = pEditEntry->info.date.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dig_w;
	i++;

	entryText[0] = '.';
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dot_w;

	entryText[0] = pEditEntry->info.date.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dig_w;
	i++;

	entryText[0] = pEditEntry->info.date.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dig_w;
	i++;

	entryText[0] = '.';
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dot_w;

	for ( ; i < 8; i++ )
	{
		entryText[0] = pEditEntry->info.date.value[i];
		gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
		x += dig_w;
	}
	return 0;
}

static int interface_editTimeDisplay(interfaceMenu_t* pMenu, DFBRectangle *rect, int i)
{
	interfaceListMenu_t *pListMenu = (interfaceListMenu_t *)pMenu;
	interfaceEditEntry_t *pEditEntry = (interfaceEditEntry_t*)pMenu->menuEntry[i].pArg;
	char entryText[2];
	DFBRectangle rectangle;

	int r,g,b,a;
	int x,y,w;
	int fh;

	int dig_w;
	int dot_w;

	pgfx_font->GetHeight(pgfx_font, &fh);
	if ( pListMenu->listMenuType == interfaceListMenuNoThumbnail )
	{
		y = rect->y;
	} else
	{
		y =rect->y + pListMenu->baseMenu.thumbnailHeight/2 + fh/4;
	}
	r = INTERFACE_BOOKMARK_RED;
	g = INTERFACE_BOOKMARK_GREEN;
	b = INTERFACE_BOOKMARK_BLUE;
	a = INTERFACE_BOOKMARK_ALPHA;
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, "4", -1, &rectangle, NULL) );
	dig_w = rectangle.w;
	DFBCHECK( pgfx_font->GetStringExtents(pgfx_font, ":", -1, &rectangle, NULL) );
	dot_w = rectangle.w;

	i = 0;
	entryText[1] = 0;
	//x = rect->x + rect->w - INTERFACE_VALUE_WIDTH/2 - (dig_w*(4/2)+dot_w/2); // center align
	x = rect->x;

	if ( pEditEntry->active )
	{
		w = x + dig_w * pEditEntry->info.time.selected;
		if (pEditEntry->info.time.selected > 3)
			w += dot_w*2;
		else if (pEditEntry->info.time.selected > 1)
			w += dot_w;
		gfx_drawRectangle(DRAWING_SURFACE, interface_colors[interfaceInfo.highlightColor].R, interface_colors[interfaceInfo.highlightColor].G, interface_colors[interfaceInfo.highlightColor].B, interface_colors[interfaceInfo.highlightColor].A, w, rect->y, dig_w, rect->h);
	}

	entryText[0] = pEditEntry->info.time.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dig_w;
	i++;

	entryText[0] = pEditEntry->info.time.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dig_w;
	i++;

	entryText[0] = ':';
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dot_w;

	entryText[0] = pEditEntry->info.time.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);
	x += dig_w;
	i++;

	entryText[0] = pEditEntry->info.time.value[i];
	gfx_drawText(DRAWING_SURFACE, pgfx_font, r, g, b, a, x, y, entryText, 0, 0);

	return 0;
}
