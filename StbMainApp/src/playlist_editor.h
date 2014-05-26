#ifndef PLAYLIST_EDITOR_H
#define PLAYLIST_EDITOR_H
#include "interface.h"

int enterPlaylistDigitalSelect(interfaceMenu_t *pMenu, void* pArg);
int enterPlaylistAnalogSelect(interfaceMenu_t *pMenu, void* pArg);
int enterPlaylistEditorDigital(interfaceMenu_t *pMenu, void* pArg);
int enterPlaylistEditorAnalog(interfaceMenu_t *pMenu, void* pArg);
void playList_saveName(int ,char* , char*);
void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, int count);
void playlist_switchElementwithNext(int source);
void playlist_editor_removeElement();
void playlist_editor_setupdate();
int getChannelEditor();
void playlist_switchElement(int , int );
void playlist_editor_cleanup(int);
int get_statusLockPlaylist();
void set_unLockColor();
int check_playlist();

#endif // PLAYLIST_EDITOR_H
