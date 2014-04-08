#ifndef PLAYLIST_EDITOR_H
#define PLAYLIST_EDITOR_H
#include "interface.h"

int enterPlaylistEditorMenu(interfaceMenu_t *pMenu, void* pArg);
int getChannelEditor();
int get_statusLockPlaylist();
void set_unLockColor();

#endif // PLAYLIST_EDITOR_H
