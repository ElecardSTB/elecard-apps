#include "playlist_editor.h"

#include "dvbChannel.h"

#include "interface.h"
#include "list.h"
#include "dvb.h"
#include "off_air.h"

//extern dvb_channels_t g_dvb_channels;

void getDVBList(){

}

void saveList(){

}

void addList(){

}

void createPlaylist(interfaceMenu_t  *interfaceMenu)
{
    interface_clearMenuEntries(interfaceMenu);

    int32_t i = 0;
    for (i = 0; i < 15; i++){
        interface_addMenuEntry(interfaceMenu, "channelEntry", NULL,NULL, thumbnail_channels);
    }



 //   interface_addMenuEntry(channelMenu, channelEntry, offair_channelChange, CHANNEL_INFO_SET(screenMain, i),
  //                          scrambled ? thumbnail_billed : ( radio ? thumbnail_radio : thumbnail_channels));

}


int enterPlaylistEditorMenu(interfaceMenu_t *interfaceMenu, void* pArg)
{

    printf("%s[%d]\n",__func__, __LINE__);
    createPlaylist(interfaceMenu);
/*
    offair_createPlaylist(&pMenu);
    printf("%s[%d]\n",__func__, __LINE__);
    if((dvbChannel_getCount() == 0) && (analogtv_getChannelCount() == 0)) {
        output_showDVBMenu(pMenu, NULL);
        interface_showConfirmationBox( _T("DVB_NO_CHANNELS"), thumbnail_dvb, offair_confirmAutoScan, NULL);
        return 1;
    }

    printf("%s[%d]\n",__func__, __LINE__);
    offair_channelChange(interfaceInfo.currentMenu, CHANNEL_INFO_SET(screenMain, appControlInfo.dvbInfo.channel));
    return 0;
*/
    return 0;
    /*
    char buf[MENU_ENTRY_INFO_LENGTH];
    interface_clearMenuEntries(interfaceMenu);
    printf("%s[%d]\n");
    snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYCONTROL_SHOW_ON_START"), interfacePlayControl.showOnStart ? _T("ON") : _T("OFF") );
    interface_addMenuEntry(interfaceMenu, buf, output_togglePlayControlShowOnStart, NULL, settings_interface);
    return 0;
*/
}

