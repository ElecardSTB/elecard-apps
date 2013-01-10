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

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "interface.h"

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES                                   *
*******************************************************************/

void garb_init();
void garb_terminate();

void garb_resetViewership();
void garb_checkViewership();
void garb_showStats();

void garb_startWatching(int channel);
void garb_stopWatching(int channel);

void garb_drawViewership();

#endif // __GARB_H
