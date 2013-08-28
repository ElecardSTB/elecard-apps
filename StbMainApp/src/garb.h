#ifndef __GARB_H
#define __GARB_H

//
// Created:  2013/01/08
// File name:  garb.h
//
//////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012 Elecard Devices
// All rights are reserved.  Reproduction in whole or in part is 
//  prohibited without the written consent of the copyright owner.
//
// Elecard Devices reserves the right to make changes without
// notice at any time. Elecard Devices makes no warranty, expressed,
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
// Authors: Andrey Kuleshov <Andrey.Kuleshov@elecard.ru>
// 
// Purpose: Defines GARB API
//
//////////////////////////////////////////////////////////////////////////

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#ifdef ENABLE_DVB
#include "interface.h"
#include "defines.h"
#include "app_info.h"

/******************************************************************
* EXPORTED TYPEDEFS
*******************************************************************/

/******************************************************************
* EXPORTED MACROS                                                 *
*******************************************************************/

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES                                   *
*******************************************************************/
void garb_init(void);
void garb_terminate(void);

void garb_askViewership(void);
void garb_resetViewership(void);
void garb_checkViewership(void);
void garb_showStats(void);

int32_t garb_startWatching(stb810_streamSource type, void *pArg);
int32_t garb_stopWatching(stb810_streamSource type, void *pArg);

void garb_drawViewership(void);

int32_t currentmeter_isExist(void);
int32_t currentmeter_getValue(uint32_t *watt);
uint32_t currentmeter_getCalibrateHighValue(void);
uint32_t currentmeter_getCalibrateLowValue(void);
void currentmeter_setCalibrateHighValue(uint32_t val);
void currentmeter_setCalibrateLowValue(uint32_t val);

/******************************************************************
* EXPORTED DATA                                                   *
*******************************************************************/
#endif //ENABLE_DVB
#endif // __GARB_H
