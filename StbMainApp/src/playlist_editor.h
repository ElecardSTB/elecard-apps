#ifndef PLAYLIST_EDITOR_H
#define PLAYLIST_EDITOR_H

/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include <stdint.h>
#include "interface.h"
#include "bouquet.h"


/******************************************************************
* EXPORTED FUNCTIONS PROTOTYPES               <Module>_<Word>+    *
*******************************************************************/
#ifdef ENABLE_DVB
int32_t enterPlaylistDigitalSelect(interfaceMenu_t *pMenu, void* pArg);
int32_t enterPlaylistAnalogSelect(interfaceMenu_t *pMenu, void* pArg);
int32_t enterPlaylistEditorDigital(interfaceMenu_t *pMenu, void* pArg);
int32_t enterPlaylistEditorAnalog(interfaceMenu_t *pMenu, void* pArg);
void playList_saveName(int32_t ,char* , char*);
void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, int32_t count);
void playlist_switchElementwithNext(int32_t source);
void playlist_editor_removeElement(void);
void playlist_editor_setupdate(void);
int32_t getChannelEditor(void);
void playlist_switchElement(int32_t , int32_t );
void playlist_editor_cleanup(typeBouquet_t);
int32_t get_statusLockPlaylist(void);
int32_t check_playlist(void);

#else //#ifdef ENABLE_DVB

#define get_statusLockPlaylist()	0
#define playList_saveName(...)
#define playlist_editor_setupdate()
#define playlist_switchElementwithNext(source)
#define playList_nextChannelState(...)


#endif //#ifdef ENABLE_DVB

#endif // PLAYLIST_EDITOR_H
