#include "playlist_editor.h"

#include "dvbChannel.h"

#include "interface.h"
#include "list.h"
#include "dvb.h"
#include "off_air.h"


typedef struct _playListEditor_t
{
    int visible;
    int radio;
    int scrambled;
    char name[MENU_ENTRY_INFO_LENGTH];
    service_index_t *service_index;
} playListEditor_t;

static int channelNumber = -1;
list_element_t *playListEditor = NULL;
static int update_list = false;

static void load_dvb_channels() {
    if (playListEditor != NULL)
        free_elements(&playListEditor);

    extern dvb_channels_t g_dvb_channels;
    struct list_head *pos;

    list_for_each(pos, &g_dvb_channels.orderSortHead) {
        service_index_t *srvIdx = list_entry(pos, service_index_t, orderSort);

        if(srvIdx == NULL)
            continue;

        list_element_t      *cur_element;
        playListEditor_t    *element;

        if (playListEditor == NULL) {
            cur_element = playListEditor = allocate_element(sizeof(playListEditor_t));
        } else {
            cur_element = append_new_element(playListEditor, sizeof(playListEditor_t));
        }
        if (!cur_element)
            break;

        element = (playListEditor_t *)cur_element->data;
        element->service_index = srvIdx;
        element->visible = srvIdx->visible;
        element->radio = srvIdx->service->service_descriptor.service_type == 2;
        element->scrambled = dvb_getScrambled(srvIdx->service);
        snprintf(element->name, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->service->service_descriptor.service_name);
    }
}

void getDVBList(){

}

void saveList(){

}

void addList(){

}
int lockChannel()
{
    return 1;
}
int unlockChannel()
{
    return 0;
}
static int color_save = -1;

void setColor(int color) {
    color_save = color;
}

int getColor() {
    return color_save;
}

void set_lockColor(){
    setColor(interfaceInfo.highlightColor);

    interfaceInfo.highlightColor++;
    if (interface_colors[interfaceInfo.highlightColor].A==0)
        interfaceInfo.highlightColor = 0;
}
void set_unLockColor(){
    interfaceInfo.highlightColor = getColor();
    setColor(-1);
    if (interface_colors[interfaceInfo.highlightColor].A==0)
        interfaceInfo.highlightColor = 0;
}
void playlist_editor_cleanup(){

    free_services(&playListEditor);

}

int getChannelEditor(){
    printf("channelNumber = %d\n",channelNumber);
    return channelNumber;
}
int get_statusLockPlaylist()
{
    if (channelNumber == -1)
        return false;
    return true;
}

void playList_saveName(int num, char *prev_name, char *new_name)
{
    int i = 0;
    list_element_t *cur_element;
    playListEditor_t *element;
    for(cur_element = playListEditor; cur_element != NULL; cur_element = cur_element->next) {
        element = (playListEditor_t*)cur_element->data;
        if(element == NULL)
            continue;
        if (num == i && strncasecmp(element->name, prev_name, strlen(prev_name))){
            snprintf(element->name, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
            return;
        }
        i++;
    }
}

void playlist_editor_setupdate(){
    update_list = true;
}

void playList_saveVisible(int num, int prev_num, int new_num)
{
    int i = 0;
    list_element_t *cur_element;
    playListEditor_t *element;
    for(cur_element = playListEditor; cur_element != NULL; cur_element = cur_element->next) {
        element = (playListEditor_t*)cur_element->data;
        if(element == NULL)
            continue;
        if (num == i && element->visible == prev_num){
            element->visible = new_num;
            return;
        }
        i++;
    }
}
int push_playlist()
{
    if (playListEditor == NULL || update_list == false)
        return false;
    update_list = false;
    int first = 0;
    int i = 0;
    list_element_t		*service_element;
    service_element = playListEditor;
    while (service_element != NULL) {
        playListEditor_t *curElement = (playListEditor_t *)service_element->data;
        if (curElement->service_index != NULL) {
            curElement->service_index->visible = curElement->visible;
            snprintf(curElement->service_index->service->service_descriptor.service_name, MENU_ENTRY_INFO_LENGTH, "%s", curElement->name);
        }
        printf("name %s  ",curElement->name);
        first = dvbChannel_findNumberService(curElement->service_index);
        //printf("SWAP %d _ %d\n",first, i);
        if ( i > first)
            dvbChannel_swapServices(first, i);
        else
            dvbChannel_swapServices(i, first);

        i++;
        service_element = service_element->next;
    }
    return true;
}

/*

    list_element_t *cur_tr_element;
    transpounder_t *element_tr;
    for(cur_tr_element = head_ts_list; cur_tr_element != NULL; cur_tr_element = cur_tr_element->next) {
        element_tr = (transpounder_t*)cur_tr_element->data;

        if(element_tr == NULL) {
            continue;
        }




    extern dvb_channels_t g_dvb_channels;
    struct list_head *pos;

    list_for_each(pos, &g_dvb_channels.orderSortHead) {
        service_index_t *srvIdx = list_entry(pos, service_index_t, orderSort);

        if(srvIdx == NULL)
            continue;

        dvbChannel_findNumberService

        list_element_t      *cur_element;
        playListEditor_t    *element;

        if (playListEditor == NULL) {
            cur_element = playListEditor = allocate_element(sizeof(playListEditor_t));
        } else {
            cur_element = append_new_element(playListEditor, sizeof(playListEditor_t));
        }
        if (!cur_element)
            break;

        element = (playListEditor_t *)cur_element->data;
        element->service_index = srvIdx;
        element->visible = srvIdx->visible;
        element->radio = srvIdx->service->service_descriptor.service_type == 2;
        element->scrambled = dvb_getScrambled(srvIdx->service);
        snprintf(element->name, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->service->service_descriptor.service_name);
    }*/
//}


static int swapMenu;

int playList_editorChannel(interfaceMenu_t *pMenu, void* pArg)
{
    if (channelNumber == -1) {
        swapMenu = CHANNEL_INFO_GET_CHANNEL(pArg);
        channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);

        set_lockColor();
        //return output_saveAndRedraw(saveAppSettings(), pMenu);
    } else {

        channelNumber = -1;
        set_unLockColor();
        printf("\n\nsritch %d -> %d\n\n\n",swapMenu, CHANNEL_INFO_GET_CHANNEL(pArg));
     //   interface_switchMenuEntryCustom(pMenu, swapMenu,  CHANNEL_INFO_GET_CHANNEL(pArg));
       // interface_switchMenu(pMenu,swapMenu);
        swapMenu = -1;
    }
    interface_displayMenu(1);
    return 0;
}
void playlist_switchElementwithNext(int source){

    if (playListEditor == NULL)
         return;
    int i = 0;

    list_element_t *cur = playListEditor;
    list_element_t *rec;
    list_element_t *last = NULL;
    while(cur != NULL) {
        if (i == source){
            if(last == NULL) {
                playListEditor = cur->next;
                cur->next = playListEditor->next;
                playListEditor->next = cur;
            } else {
                rec = cur->next;
                last->next = rec;
                cur->next = rec->next;
                rec->next = cur;
            }
        }
        i++;
        last = cur;
        cur = cur->next;
    }


/*





    service_element = remove_element(&dvb_services, service_element);
    list_element_t *insert_sorted_element_by_id(list_element_t **head, uint16_t id, uint32_t data_size);

    list_element_t *cur_element;
    playListEditor_t element;

    cur_element = playListEditor;
    while (cur_element != NULL) {
        i++;
        if (i == source) {
            element = (playListEditor_t)cur_element->data;
            remove_element(&playListEditor, cur_element);
            insert_sorted_element_by_id(&playListEditor, receiver, size);

            source_next_element = cur_element;
            source_element = cur_element->next;
        }
        i++;
        cur_element = service_element->next;
    }



    if (source_element != NULL && receiver_element != NULL) {
        cur_element = receiver_element;
        cur_element->next = receiver_next_element;
        receiver_element = source_element;
        receiver_next_element = source_next_element;



        cur_element = receiver_element;
        cur_element->next = receiver_next_element;



    }


    for(cur_element = playListEditor; cur_element != NULL; cur_element = cur_element->next) {
        element = (playListEditor_t*)cur_element->data;

        if (i == source) {
            source_element = element;
        }

        if (i == receiver) {
            receiver_element = element;
        }
        if (source_element != NULL && receiver_element != NULL)
            break;
        i++;
    }

    if (source_element != NULL && receiver_element != NULL) {
        element = source_element;
    }
    */

}


void createPlaylist(interfaceMenu_t  *interfaceMenu)
{
    if (playListEditor != NULL)
        return;

    load_dvb_channels();
    /*
     if (!(interfaceMenu && !interfaceMenu->menuEntryCount))
        return;
    */
    interfaceMenu_t *channelMenu = interfaceMenu;
    struct list_head *pos;
    char channelEntry[MENU_ENTRY_INFO_LENGTH];
    int32_t i = 0;
    interface_clearMenuEntries(channelMenu);

    //offair_updateChannelStatus();
    //	interface_addMenuEntryDisabled(channelMenu, "DVB", 0);


    list_element_t *cur_element;
    playListEditor_t *element;
    for(cur_element = playListEditor; cur_element != NULL; cur_element = cur_element->next) {
        element = (playListEditor_t*)cur_element->data;
        if(element == NULL)
            continue;

        snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->name);
        interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels));

        interfaceMenuEntry_t *entry;
        entry = menu_getLastEntry(channelMenu);
        if(entry) {
            if (element->visible)
                interface_setMenuEntryLabel(entry, "VISIBLE");
            else
                interface_setMenuEntryLabel(entry, "INVISIBLE");
        }

        if(appControlInfo.dvbInfo.channel == i) {
            interface_setSelectedItem(channelMenu, interface_getMenuEntryCount(channelMenu) - 1);
        }
        i++;
    }
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

