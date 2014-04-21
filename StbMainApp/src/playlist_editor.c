#include "playlist_editor.h"

#include "dvbChannel.h"
#include "bouquet.h"

#include "interface.h"
#include "list.h"
#include "dvb.h"
#include "off_air.h"
#include "l10n.h"
#include "md5.h"

typedef struct _playListEditor_t
{
	int radio;
	int scrambled;
	service_index_t *service_index;
	service_index_data_t data;
} playListEditor_t;

static int channelNumber = -1;
list_element_t *playListEditor = NULL;
static int update_list = false;

static void load_dvb_channels() {
	if (playListEditor != NULL) {
		playlist_editor_cleanup();
	}

	extern dvb_channels_t g_dvb_channels;
	struct list_head *pos;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

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
		element->data.visible = srvIdx->data.visible;
		element->data.parent_control = srvIdx->data.parent_control;
		element->radio = srvIdx->service->service_descriptor.service_type == 2;
		element->scrambled = dvb_getScrambled(srvIdx->service);
		snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->service->service_descriptor.service_name);
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
	free_elements(&playListEditor);
}

int getChannelEditor(){
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
		if (num == i && strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))){
			snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
			return;
		}
		i++;
	}
}

void playlist_editor_setupdate(){
	update_list = true;
}

void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, int count)
{
	int i = 0;
	list_element_t *cur_element;
	playListEditor_t *element;
	for(cur_element = playListEditor; cur_element != NULL; cur_element = cur_element->next) {
		element = (playListEditor_t*)cur_element->data;
		if(element == NULL)
			continue;
		if (count == i){
			char desc[20];
			if(element->data.visible) {
				if(element->data.parent_control) {
					element->data.parent_control = false;
					element->data.visible = false;
					snprintf(desc, sizeof(desc), "INVISIBLE");
				} else {
					element->data.parent_control = true;
					snprintf(desc, sizeof(desc), "PARENT");
				}
			} else {
				element->data.visible = true;
				snprintf(desc, sizeof(desc), "VISIBLE");
			}
			interface_changeMenuEntryLabel(pMenuEntry, desc, strlen(desc) + 1);
			interface_changeMenuEntryThumbnail(pMenuEntry, element->data.visible ? (element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels)) : thumbnail_not_selected);
			interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
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
			curElement->service_index->data.visible        = curElement->data.visible;
			curElement->service_index->data.parent_control = curElement->data.parent_control;
			snprintf(curElement->service_index->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", curElement->data.channelsName);
		}
		first = dvbChannel_findNumberService(curElement->service_index);
		if ( i > first)
			dvbChannel_swapServices(first, i);
		else
			dvbChannel_swapServices(i, first);

		i++;
		service_element = service_element->next;
	}
	return true;
}

int playList_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	unsigned char out[16];
	char out_hex[32];
	char pass[32];
	if(value == NULL) {
		return 0;
	}

	md5((unsigned char *)value, strlen(value), out);
	for (int i=0;i<16;i++) {
		sprintf(&out_hex[i*2], "%02hhx", out[i]);
	}
	FILE *pass_file = fopen(PARENT_CONTROL_FILE, "r");
	if(pass_file == NULL) {
		char cmd[256];
		pass_file = fopen(PARENT_CONTROL_DEFAULT_FILE, "r");
		if(pass_file == NULL) {
			return 0;
		}
		snprintf(cmd, sizeof(cmd), "cp %s %s", PARENT_CONTROL_DEFAULT_FILE, PARENT_CONTROL_FILE);
		system(cmd);
	}
	fread(pass, 32, 1, pass_file);
	fclose(pass_file);

	if(strncmp(out_hex, pass, 32) == 0) {
		push_playlist();
		dvbChannel_applyUpdates();
	}
	else {
		interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
		return 1;
	}

	return 0;
}

int check_playlist()
{
	if (playListEditor == NULL || update_list == false)
		return false;
	extern interfaceListMenu_t InterfacePlaylistEditor;

	list_element_t		*service_element = playListEditor;
	while (service_element != NULL) {
		playListEditor_t *curElement = (playListEditor_t *)service_element->data;
		if (curElement->service_index != NULL) {
			if((!curElement->data.parent_control) && (curElement->service_index->data.parent_control)) {
				const char *mask = "\\d{6}";
				interface_getText((interfaceMenu_t*)&InterfacePlaylistEditor, _T("ENTER_PASSWORD"), mask, playList_checkParentControlPass, NULL, inputModeDirect, NULL);
				return true;
			}
		}
		service_element = service_element->next;
	}

	push_playlist();
	dvbChannel_applyUpdates();
	return true;
}

static int swapMenu;

int playList_editorChannel(interfaceMenu_t *pMenu, void* pArg)
{
	if (channelNumber == -1) {
		swapMenu = CHANNEL_INFO_GET_CHANNEL(pArg);
		channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);

		set_lockColor();
	} else {
		channelNumber = -1;
		set_unLockColor();
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
}


void createPlaylist(interfaceMenu_t  *interfaceMenu)
{
	if (playListEditor != NULL)
		return;
	load_dvb_channels();
	interfaceMenu_t *channelMenu = interfaceMenu;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	int32_t i = 0;
	interface_clearMenuEntries(channelMenu);

	list_element_t *cur_element;
	playListEditor_t *element;
	for(cur_element = playListEditor; cur_element != NULL; cur_element = cur_element->next) {
		element = (playListEditor_t*)cur_element->data;
		if(element == NULL)
			continue;

		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->data.channelsName);
		interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->data.visible ?
								   (element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels)) : thumbnail_not_selected);

		interfaceMenuEntry_t *entry;
		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			char desc[20];
			snprintf(desc, sizeof(desc), "%s", element->data.parent_control ? "PARENT" : (element->data.visible ? "VISIBLE" : "INVISIBLE"));
			interface_setMenuEntryLabel(entry, desc);
			interface_changeMenuEntrySelectable(entry, element->data.visible);
		}

		if(appControlInfo.dvbInfo.channel == i) {
			interface_setSelectedItem(channelMenu, interface_getMenuEntryCount(channelMenu) - 1);
		}
		i++;
	}
	if (i == 0)
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
}

int enterPlaylistSelect(interfaceMenu_t *interfaceMenu, void* pArg)
{
	interfaceMenu_t *channelMenu = interfaceMenu;
	interface_clearMenuEntries(channelMenu);
	int i = 0;
	char *name;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];

	while ((name = bouquet_getNameBouquetList(i) )!= NULL) {
		snprintf(channelEntry, sizeof(channelEntry), "%02d %s",i + 1, name);
		interface_addMenuEntry(channelMenu, channelEntry, bouquets_setBouquet, CHANNEL_INFO_SET(screenMain, i), thumbnail_epg);
		char *bouquets_name;
		bouquets_name = bouquet_getBouquetName();
		if (bouquets_name != NULL && strcasecmp(name, bouquets_name) == 0) {
			interfaceMenuEntry_t *entry;
			entry = menu_getLastEntry(channelMenu);
			if(entry) {
				char desc[20];
				snprintf(desc, sizeof(desc), "SELECTED");
				interface_setMenuEntryLabel(entry, desc);
			}
		}
		i++;
	};
	if (i == 0)
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);

	return 0;
}

int enterPlaylistEditorMenu(interfaceMenu_t *interfaceMenu, void* pArg)
{
	createPlaylist(interfaceMenu);
	return 0;
}

