#ifndef PLAYLIST_EDITOR_H
#define PLAYLIST_EDITOR_H

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include <stdint.h>
#include "interface.h"
#include "bouquet.h"

/******************************************************************
* EXPORTED DATA                                [for headers only] *
*******************************************************************/
extern interfaceListMenu_t InterfacePlaylistMain;

/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef ENABLE_DVB

int32_t playlistEditor_init(void);
int32_t playlistEditor_terminate(void);

#endif //#ifdef ENABLE_DVB

#endif // PLAYLIST_EDITOR_H
