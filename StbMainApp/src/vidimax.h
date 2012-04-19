#if !defined(__VIDIMAX_H)
#define __VIDIMAX_H

//
// Created:  2010/12/09
// File name:  vm.h
//
//////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012 Elecard Devices
// All rights are reserved.  Reproduction in whole or in part is 
//  prohibited without the written consent of the copyright owner.
//
// Elecard Devices reserves the right to make changes without
// notice at any time. Elecard Ltd. makes no warranty, expressed,
// implied or statutory, including but not limited to any implied
// warranty of merchantability of fitness for any particular purpose,
// or that the use will not infringe any third party patent, copyright
// or trademark.
//
// Elecard Devices must not be liable for any loss or damage arising
// from its use.
//
//////////////////////////////////////////////////////////////////////////
//
// Authors: Victoria Peshkova <Victoria.Peshkova@elecard.ru>
// 
// Purpose: Vidimax exported function prototypes.
//
//////////////////////////////////////////////////////////////////////////
/***********************************************
* INCLUDE FILES                                *
************************************************/

#include <directfb.h>
#include "interface.h"

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif	
	void vidimax_buildCascadedMenu(interfaceMenu_t* pParent);	
	int vidimax_fillMenu(interfaceMenu_t *pMenu, void* pArg);
	void vidimax_drawMainMenuIcons (imageIndex_t imIndex, DFBRectangle * rect, int selected);
	int vidimax_refreshMenu();
	void vidimax_stopVideoCallback();
	void vidimax_cleanup();
	void vidimax_notifyVideoSize(int x, int y, int w, int h);
#ifdef __cplusplus
}
#endif

#endif /* __VIDIMAX_H      Do not add any thing below this line */

