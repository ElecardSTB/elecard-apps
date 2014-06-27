

#ifdef ENABLE_DVB
/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include "playlist_editor.h"
#include "dvbChannel.h"
#include "debug.h"
#include "output.h"
#include "bouquet.h"
#include "analogtv.h"
#include "interface.h"
#include "list.h"
#include "dvb.h"
#include "off_air.h"
#include "l10n.h"


/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
typedef struct {
	uint32_t frequency;
	//analog_service_t *service_index;
	service_index_data_t data;
} playListEditorAnalog_t;

typedef struct {
	int32_t               radio;
	int32_t               scrambled;
	service_index_t      *service_index;
	service_index_data_t  data;
	struct list_head      list;
} editorDigital_t;


/******************************************************************
* GLOBAL DATA                                                     *
*******************************************************************/
interfaceListMenu_t InterfacePlaylistMain;

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/
static int32_t channelNumber = -1;
static list_element_t *playListEditorAnalog = NULL;
static int32_t update_list = false;
static int32_t color_save = -1;
static int32_t swapMenu;
static interfaceListMenu_t InterfacePlaylistAnalog;
static interfaceListMenu_t InterfacePlaylistDigital;
static interfaceListMenu_t InterfacePlaylistSelectDigital;
static interfaceListMenu_t InterfacePlaylistSelectAnalog;
static interfaceListMenu_t InterfacePlaylistEditorDigital;
static interfaceListMenu_t InterfacePlaylistEditorAnalog;

static struct list_head editor_playList = LIST_HEAD_INIT(editor_playList);

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int32_t output_enterPlaylistMenu(interfaceMenu_t *pMenu, void* notused);
static int32_t output_enterPlaylistDigital(interfaceMenu_t *pMenu, void* notused);
static int32_t output_enterPlaylistAnalog(interfaceMenu_t *pMenu, void* notused);
static void playList_saveName(int32_t num, char *prev_name, char *new_name);
static int32_t get_statusLockPlaylist(void);
static void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, int32_t count);
// static void playlist_editor_removeElement(void);
static void playlist_editor_setupdate(void);
// static int32_t getChannelEditor(void);
static void playlistEditor_moveElement(int32_t sourceNum, int32_t move);
static interfaceMenu_t *output_getPlaylistEditorMenu(void);
static char *output_getSelectedNamePlaylistEditor(void);
static int32_t enablePlayListEditorMenu(interfaceMenu_t *interfaceMenu);
// static int32_t enablePlayListSelectMenu(interfaceMenu_t *interfaceMenu);

// function for editor list
editorDigital_t *editorList_get(struct list_head *listHead, uint32_t number);
editorDigital_t *editorList_add(struct list_head *listHead);
void  editorList_release(void);

static int32_t fillPlaylistEditorDigital(interfaceMenu_t *interfaceMenu, void* pArg);



/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>_<Word>+    *
*******************************************************************/
void setColor(int32_t color) {
	color_save = color;
}

int32_t getColor() {
	return color_save;
}

static void load_Analog_channels(list_element_t *curListEditor)
{
	extern analog_service_t 	analogtv_channelParam[MAX_ANALOG_CHANNELS];
	//analogtv_parseConfigFile(1);
	int32_t analogtv_channelCount = analogtv_getChannelCount(1);
	int32_t i;
	for(i = 0; i < analogtv_channelCount; i++) {
		list_element_t      *cur_element;
		playListEditorAnalog_t    *element;

		if (playListEditorAnalog == NULL) {
			cur_element = playListEditorAnalog = allocate_element(sizeof(playListEditorAnalog_t));
		} else {
			cur_element = append_new_element(playListEditorAnalog, sizeof(playListEditorAnalog_t));
		}
		if (!cur_element)
			break;
		element = (playListEditorAnalog_t *)cur_element->data;
		element->frequency =  analogtv_channelParam[i].frequency;
		snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", analogtv_channelParam[i].customCaption);
		element->data.visible = analogtv_channelParam[i].visible;
		element->data.parent_control = analogtv_channelParam[i].parent_control;
	}
}

static void load_digital_channels(struct list_head *listHead)
{
	extern dvb_channels_t g_dvb_channels;
	struct list_head *pos;
	editorDigital_t*element;
	if(!listHead) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return;
	}

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

		if(srvIdx == NULL)
			continue;

		element = editorList_add(listHead);

		element->service_index = srvIdx;
		element->data.visible = srvIdx->data.visible;
		element->data.parent_control = srvIdx->data.parent_control;

		if(	(srvIdx->service->service_descriptor.service_type == 2) ||
			(dvb_hasMediaType(srvIdx->service, mediaTypeAudio) && !dvb_hasMediaType(srvIdx->service, mediaTypeVideo)))
		{
			element->radio = 1;
		} else {
			element->radio = 0;
		}
		element->scrambled = dvb_getScrambled(srvIdx->service);
		snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->service->service_descriptor.service_name);
	}
}

static void set_lockColor(void)
{
	setColor(interfaceInfo.highlightColor);

	interfaceInfo.highlightColor++;
	if (interface_colors[interfaceInfo.highlightColor].A==0)
		interfaceInfo.highlightColor = 0;
}

static void set_unLockColor(void)
{
	interfaceInfo.highlightColor = getColor();
	setColor(-1);
	if (interface_colors[interfaceInfo.highlightColor].A==0)
		interfaceInfo.highlightColor = 0;
}

void playlist_editor_cleanup(typeBouquet_t index)
{

	if (index == eBouquet_all || index == eBouquet_digital) {
		editorList_release();
	}
	if (index == eBouquet_all || index == eBouquet_analog) {
		free_elements(&playListEditorAnalog);
	}
}

/*static int32_t getChannelEditor(void)
{
	return channelNumber;
}*/

static int32_t get_statusLockPlaylist(void)
{
	if (channelNumber == -1)
		return false;
	return true;
}


static interfaceMenu_t *output_getPlaylistEditorMenu(void)
{
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu))
		return interfaceInfo.currentMenu;
	return NULL;
}

static char *output_getSelectedNamePlaylistEditor(void)
{
	interfaceMenu_t  *baseMenu;
	baseMenu = output_getPlaylistEditorMenu();
	if (baseMenu != NULL)
		return baseMenu->menuEntry[baseMenu->selectedItem].info;
	return NULL;
}

static int32_t enablePlayListEditorMenu(interfaceMenu_t *interfaceMenu)
{
	if((int32_t)interfaceMenu == (int32_t)&InterfacePlaylistEditorDigital.baseMenu) {
		return 1;
	} else if((int32_t)interfaceMenu == (int32_t)&InterfacePlaylistEditorAnalog.baseMenu) {
		return 2;
	}

	return false;
}

/*static int32_t enablePlayListSelectMenu(interfaceMenu_t *interfaceMenu)
{
	if (!memcmp(interfaceMenu, &InterfacePlaylistSelectDigital.baseMenu, sizeof(interfaceListMenu_t)))
		return 1;
	if (!memcmp(interfaceMenu, &InterfacePlaylistSelectAnalog.baseMenu, sizeof(interfaceListMenu_t)))
		return 2;

	return false;
}*/


static void playList_saveName(int32_t num, char *prev_name, char *new_name)
{
	int32_t i = 0;
	editorDigital_t *element;

	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {

		element = editorList_get(&editor_playList, num);
		if (element == NULL)
			return;

		if (strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))) {
			snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
			return;
		}
		return;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		list_element_t *cur_element;
		playListEditorAnalog_t *element;
		for(cur_element = playListEditorAnalog; cur_element != NULL; cur_element = cur_element->next) {
			element = (playListEditorAnalog_t*)cur_element->data;
			if(element == NULL)
				continue;
			if (num == i && strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))){
				snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
				return;
			}
			i++;
		}
		return;
	}
}

static void playlist_editor_setupdate(void)
{
	update_list = true;
}

static void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, int32_t count)
{
	int32_t i = 0;
	list_element_t *cur_element;

	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
		editorDigital_t *element;

		element = editorList_get(&editor_playList, count);
		if (element == NULL)
			return;

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
//		interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
		return;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		playListEditorAnalog_t *element;
		for(cur_element = playListEditorAnalog; cur_element != NULL; cur_element = cur_element->next) {
			element = (playListEditorAnalog_t*)cur_element->data;
			if(element == NULL)
				continue;
			if (count == i){
				char desc[20];
				if(element->data.visible) {
					element->data.visible = false;
					snprintf(desc, sizeof(desc), "INVISIBLE");
				} else {
					element->data.visible = true;
					snprintf(desc, sizeof(desc), "VISIBLE");
				}
				interface_changeMenuEntryLabel(pMenuEntry, desc, strlen(desc) + 1);
				interface_changeMenuEntryThumbnail(pMenuEntry, element->data.visible ? thumbnail_tvstandard : thumbnail_not_selected);
//				interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
				return;
			}
			i++;
		}
		return;
	}
}

void merge_digitalLists(void)
{
	if ((list_empty(&editor_playList) &&  playListEditorAnalog == NULL) || update_list == false)
		return;
	update_list = false;
	int32_t first = 0;
	int32_t i = 0;
	list_element_t		*service_element;

	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
		struct list_head *pos;
		int32_t i = 0;
		list_for_each(pos, &editor_playList) {
			editorDigital_t *element = list_entry(pos, editorDigital_t, list);
			if(element == NULL)
				continue;

			if (element->service_index != NULL) {
				element->service_index->data.visible        = element->data.visible;
				element->service_index->data.parent_control = element->data.parent_control;
				snprintf(element->service_index->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", element->data.channelsName);
			}
			printf("%s[%d] %s\n",__func__, __LINE__, element->service_index->data.channelsName);
			first = dvbChannel_findNumberService(element->service_index);
			if ( i > first)
				dvbChannel_swapServices(first, i);
			else
				dvbChannel_swapServices(i, first);

			i++;
		}
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		service_element = playListEditorAnalog;
		extern analog_service_t 	analogtv_channelParam[MAX_ANALOG_CHANNELS];
		int32_t index;
		while (service_element != NULL) {
			playListEditorAnalog_t *curElement = (playListEditorAnalog_t *)service_element->data;
			service_element = service_element->next;
			index = analogtv_findOnFrequency(curElement->frequency);
			if (index == -1)
				continue;
			analogtv_channelParam[index].visible = curElement->data.visible;
			analogtv_channelParam[index].parent_control = curElement->data.parent_control;
			snprintf(analogtv_channelParam[index].customCaption, MENU_ENTRY_INFO_LENGTH, "%s", curElement->data.channelsName);

			analogtv_swapService(index, i);
			i++;
		}
		analogtv_saveConfigFile();
		bouquet_saveAnalogBouquet();
	}
}

int32_t playList_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	(void)pMenu;
	(void)pArg;
	if(parentControl_checkPass(value) == 0) {
		merge_digitalLists();
		dvbChannel_applyUpdates();
	} else {
		interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
		return 1;
	}

	return 0;
}

int32_t swap_playlistEditor(void)
{
	if ((list_empty(&editor_playList) && playListEditorAnalog == NULL) || update_list == false)
		return false;

	struct list_head *pos;
	list_for_each(pos, &editor_playList) {
		editorDigital_t *element = list_entry(pos, editorDigital_t, list);
		if(element == NULL)
			continue;


		if (element->service_index != NULL) {
			if((!element->data.parent_control) && (element->service_index->data.parent_control)) {
				const char *mask = "\\d{6}";
				interface_getText((interfaceMenu_t*)&InterfacePlaylistEditorDigital, _T("ENTER_PASSWORD"), mask, playList_checkParentControlPass, NULL, inputModeDirect, NULL);
				return true;
			}
		}
	}
	merge_digitalLists();
	dvbChannel_applyUpdates();
	return true;
}

int32_t playList_editorChannel(interfaceMenu_t *pMenu, void* pArg)
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

/*static void playlist_editor_removeElement(void)
{
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
	//	curList = playListEditorDigital;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {

	}

}*/

int32_t enterPlaylistAnalogSelect(interfaceMenu_t *interfaceMenu, void* pArg)
{
	interfaceMenu_t *channelMenu;
	int32_t i = 0;
	const char *name;
	char *bouquets_name;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];

	channelMenu = interfaceMenu;
	interface_clearMenuEntries(channelMenu);

	snprintf(channelEntry, sizeof(channelEntry), "%s: %s", _T("PLAYLIST_NOW_SELECT"), bouquet_getAnalogBouquetName());
	interface_addMenuEntryDisabled(channelMenu, channelEntry, 0);
	interface_addMenuEntry(channelMenu, _T("PLAYLIST_UPDATE_LIST"), bouquet_updateAnalogBouquetList, NULL, settings_interface);
	interface_addMenuEntryDisabled(channelMenu, "List of analog channels:", 0);


	while((name = strList_get(&bouquetNameAnalogList, i))!= NULL) {
		snprintf(channelEntry, sizeof(channelEntry), "%02d %s",i + 1, name);
		interface_addMenuEntry(channelMenu, channelEntry, bouquets_setAnalogBouquet, CHANNEL_INFO_SET(screenMain, i), thumbnail_epg);
		bouquets_name = bouquet_getAnalogBouquetName();
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
	}
	if (i == 0){
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	}
	return 0;
}

int32_t enterPlaylistDigitalSelect(interfaceMenu_t *interfaceMenu, void* pArg)
{
	interfaceMenu_t *channelMenu;
	int32_t i = 0;
	const char *name;
	char *bouquets_name;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];

	channelMenu = interfaceMenu;
	interface_clearMenuEntries(channelMenu);

	snprintf(channelEntry, sizeof(channelEntry), "%s: %s", _T("PLAYLIST_NOW_SELECT"), bouquet_getDigitalBouquetName());
	interface_addMenuEntryDisabled(channelMenu, channelEntry, 0);
	interface_addMenuEntry(channelMenu, _T("PLAYLIST_UPDATE_LIST"), bouquet_updateDigitalBouquetList, NULL, settings_interface);
	interface_addMenuEntryDisabled(channelMenu, "List of digital channels:", 0);

	while ((name = strList_get(&digitalBouquet.NameDigitalList, i) )!= NULL) {
		snprintf(channelEntry, sizeof(channelEntry), "%02d %s",i + 1, name);
		interface_addMenuEntry(channelMenu, channelEntry, bouquets_setDigitalBouquet, CHANNEL_INFO_SET(screenMain, i), thumbnail_epg);
		bouquets_name = bouquet_getDigitalBouquetName();
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
	}
	if (i == 0){
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	}
	return 0;

}

int32_t enterPlaylistEditorAnalog(interfaceMenu_t *interfaceMenu, void* pArg)
{
	if (playListEditorAnalog != NULL)
		return 0;

	list_element_t *cur_element = playListEditorAnalog;
	interfaceMenu_t *channelMenu = interfaceMenu;
	playListEditorAnalog_t *element;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	int32_t i = 0;

	interface_clearMenuEntries(channelMenu);
	load_Analog_channels(cur_element);// fixed pointer on list

	for(cur_element = playListEditorAnalog; cur_element != NULL; cur_element = cur_element->next) {
		element = (playListEditorAnalog_t*)cur_element->data;
		if(element == NULL)
			continue;

		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->data.channelsName);
		interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->data.visible ? thumbnail_tvstandard : thumbnail_not_selected);

		interfaceMenuEntry_t *entry;
		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			char desc[20];
			snprintf(desc, sizeof(desc), "%s", element->data.visible ? "VISIBLE" : "INVISIBLE");
			interface_setMenuEntryLabel(entry, desc);
//			interface_changeMenuEntrySelectable(entry, element->data.visible);
		}

		if(appControlInfo.dvbInfo.channel == i) {
			interface_setSelectedItem(channelMenu, interface_getMenuEntryCount(channelMenu) - 1);
		}
		i++;
	}
	if (i == 0)
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	return 0;
}

static int32_t fillPlaylistEditorDigital(interfaceMenu_t *interfaceMenu, void* pArg)
{
	interfaceMenu_t *channelMenu = interfaceMenu;
	struct list_head *pos;
	int32_t i = 0;
	(void)pArg;

	interface_clearMenuEntries(channelMenu);

	list_for_each(pos, &editor_playList) {
		char channelEntry[MENU_ENTRY_INFO_LENGTH];
		editorDigital_t *element = list_entry(pos, editorDigital_t, list);

		if(element == NULL) {
			continue;
		}

		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->data.channelsName);
		interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->data.visible ?
								   (element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels)) : thumbnail_not_selected);

		interfaceMenuEntry_t *entry;
		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			char desc[20];
			snprintf(desc, sizeof(desc), "%s", element->data.parent_control ? "PARENT" : (element->data.visible ? "VISIBLE" : "INVISIBLE"));
			interface_setMenuEntryLabel(entry, desc);
//			interface_changeMenuEntrySelectable(entry, element->data.visible);
		}

		i++;
	}
	if(i == 0) {
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	}

	return 0;
}

int32_t enterPlaylistEditorDigital(interfaceMenu_t *interfaceMenu, void* pArg)
{

	if(!(list_empty(&editor_playList))) {
		return 0;
	}

	load_digital_channels(&editor_playList);
	fillPlaylistEditorDigital(interfaceMenu, pArg);

	if((appControlInfo.dvbInfo.channel >= 0) && (appControlInfo.dvbInfo.channel < interface_getMenuEntryCount(interfaceMenu))) {
		interface_setSelectedItem(interfaceMenu, appControlInfo.dvbInfo.channel);
	}

	return 0;
}


static int32_t output_saveParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if(parentControl_savePass(value) != 0) {
		interface_showMessageBox(_T("PARENTCONTROLL_CANT_SAVE_PASS"), thumbnail_error, 3000);
		return -1;
	}
	return 0;
}

static int32_t output_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	if(parentControl_checkPass(value) == 0) {
		const char *mask = "\\d{6}";
		interface_getText(pMenu, _T("ENTER_NEW_PASSWORD"), mask, output_saveParentControlPass, NULL, inputModeDirect, &pArg);
		return 1;
	}

	return 0;
}

static int32_t output_changeParentControlPass(interfaceMenu_t* pMenu, void* pArg)
{
	const char *mask = "\\d{6}";
	interface_getText(pMenu, _T("ENTER_CURRENT_PASSWORD"), mask, output_checkParentControlPass, NULL, inputModeDirect, &pArg);
	return 0;
}

static int32_t output_changeCreateNewBouquet(interfaceMenu_t* pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_BOUQUET_NAME"), "\\w+", bouquet_createNewBouquet, NULL, inputModeABC, &pArg);
	return 0;
}

int32_t output_enterPlaylistMenu(interfaceMenu_t *interfaceMenu, void* notused)
{
	char str[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(interfaceMenu);
	snprintf(str, sizeof(str), "%s: %s", _T("PLAYLIST_ENABLE"), bouquet_getEnableStatus()? "ON" : "OFF");
	interface_addMenuEntry(interfaceMenu, str, bouquet_enableControl, NULL, settings_interface);
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_ANALOG"), interface_menuActionShowMenu, &InterfacePlaylistAnalog, settings_interface);
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_DIGITAL"), interface_menuActionShowMenu, &InterfacePlaylistDigital, settings_interface);
	return 0;
}

int32_t output_enterPlaylistAnalog(interfaceMenu_t *interfaceMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(interfaceMenu);
	if (bouquet_getEnableStatus()) {
		snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquet_getAnalogBouquetName());
		interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectAnalog, settings_interface);
	}
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_EDITOR"), interface_menuActionShowMenu, &InterfacePlaylistEditorAnalog, settings_interface);	
	if (bouquet_getEnableStatus()) {
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_UPDATE"), bouquet_updateAnalogBouquet, NULL, settings_interface);
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), bouquet_saveAnalogMenuBouquet, NULL, settings_interface);
	}
	return 0;
}

int32_t output_enterPlaylistDigital(interfaceMenu_t *interfaceMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	interface_clearMenuEntries(interfaceMenu);

	if (bouquet_getEnableStatus()) {
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_NEW_BOUQUETS"), output_changeCreateNewBouquet, NULL, settings_interface);
		snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquet_getDigitalBouquetName());
		interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectDigital, settings_interface);
	}
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_EDITOR"), interface_menuActionShowMenu, &InterfacePlaylistEditorDigital, settings_interface);
	if (bouquet_getEnableStatus()) {
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), bouquet_saveDigitalBouquet, NULL, settings_interface);
		interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_UPDATE"), bouquet_updateDigitalBouquet, NULL, settings_interface);
	}
	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_REMOVE"), bouquet_removeBouquet, NULL, settings_interface);
	interface_addMenuEntry(interfaceMenu, _T("PARENT_CONTROL_CHANGE"), output_changeParentControlPass, NULL, settings_interface);
	return 0;
}

static char* interface_getChannelCaption(int32_t dummy, void* pArg)
{
	(void)dummy;
	char * ptr = strstr(output_getSelectedNamePlaylistEditor(), ". ");
	if (ptr) {
		ptr += 2;
		return ptr;
	}
	return NULL;
}

static int32_t interface_saveChannelCaption(interfaceMenu_t *pMenu, char* pStr, void* pArg)
{
	if(pStr == NULL) {
		return -1;
	}
	playList_saveName(pMenu->selectedItem, pMenu->menuEntry[pMenu->selectedItem].info, pStr);
	snprintf(pMenu->menuEntry[pMenu->selectedItem].info, MENU_ENTRY_INFO_LENGTH, "%s. %s", offair_getChannelNumberPrefix(pMenu->selectedItem), pStr);
/*
    if((DVBTMenu.baseMenu.selectedItem - 2) < dvbChannel_getCount()) {
        EIT_service_t *service;
        int32_t channelNumber;

        channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
        service = dvbChannel_getService(channelNumber);
        if(service == NULL) {
            return -1;
        }

        snprintf((char *)service->service_descriptor.service_name, MENU_ENTRY_INFO_LENGTH, "%s", pStr);

        // todo : save new caption to config file
	} else if(DVBTMenu.baseMenu.selectedItem < (int32_t)(dvbChannel_getCount() + analogtv_getChannelCount(0) + 3)) {
        uint32_t selectedItem = DVBTMenu.baseMenu.selectedItem - dvbChannel_getCount() - 3;
        analogtv_updateName(selectedItem, pStr);
    }

#warning "Wrong printing number for analog TV!"
    snprintf(DVBTMenu.baseMenu.menuEntry[DVBTMenu.baseMenu.selectedItem].info,
              MENU_ENTRY_INFO_LENGTH, "%02d. %s", DVBTMenu.baseMenu.selectedItem - 2, pStr);
*/
	return 0;
}

static void playlistEditor_moveElement(int32_t elementId, int32_t move)
{
	struct list_head *el;
	int32_t i = 0;

	dprintf("%s[%d] elementId=%d\n", __func__, __LINE__, elementId);
	if(enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		return;
		//	curList = playListEditorAnalog;
	}

	list_for_each(el, &editor_playList) {
		editorDigital_t *secElement = list_entry(el, editorDigital_t, list);
		if(secElement == NULL) {
			continue;
		}

		if(i == elementId) {
			struct list_head *newPos = el->prev;
			list_del(el);
			while(move != 0) {
				if(move > 0) {
					newPos = newPos->next;
					move--;
				} else {
					newPos = newPos->prev;
					move++;
				}
				if(newPos == &editor_playList) {
					eprintf("%s(): Warning, something wrong!!!\n", __func__);
					break;
				}
			}
			list_add(el, newPos);
			break;
		}
		i++;
	}

}

static int32_t interface_switchMenuEntryCustom(interfaceMenu_t *pMenu, int32_t srcId, int32_t move)
{
// 	int32_t dstId;
// 	interfaceMenuEntry_t cur;

	if(move == 0) {
		return 0;
	} else if(move > 0) {
		int32_t menuEntryCount = interface_getMenuEntryCount(pMenu);
		if(srcId >= (menuEntryCount - 1)) {
			return -1;
		}
		if((srcId + move) >= menuEntryCount) {
			move = menuEntryCount - srcId - 1;
		}
	} else {
		if(srcId <= 0) {
			return -1;
		}
		if((srcId + move) < 0) {
			move = -srcId;
		}
	}

//	dstId = srcId + move;
	playlistEditor_moveElement(srcId, move);

	fillPlaylistEditorDigital(pMenu, NULL);

	//TODO: Here we can directly rename menu items instead of refilling full menu by calling fillPlaylistEditorDigital()!!!
// 	memcpy(&cur, &pMenu->menuEntry[srcId], sizeof(interfaceMenuEntry_t));
// 	memcpy(&pMenu->menuEntry[srcId], &pMenu->menuEntry[dstId], sizeof(interfaceMenuEntry_t));
// 	memcpy(&pMenu->menuEntry[dstId], &cur, sizeof(interfaceMenuEntry_t));
// 
// 	snprintf(pMenu->menuEntry[srcId].info, MENU_ENTRY_INFO_LENGTH, "%s", offair_getChannelNumberPrefix(srcId));
// 	snprintf(pMenu->menuEntry[dstId].info, MENU_ENTRY_INFO_LENGTH, "%s", offair_getChannelNumberPrefix(dstId));

// 	pMenu->menuEntry[dstId].pArg = pMenu->menuEntry[srcId].pArg;
// 	pMenu->menuEntry[srcId].pArg = cur.pArg;

	return 0;
}

static int32_t playlistEditor_processCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd)
{

	if(get_statusLockPlaylist()) {
		switch(cmd->command) {
// 			case interfaceCommandLeft:
// 				if(curItem >= 0 && curItem < interface_getMenuEntryCount(pMenu)) {
// 					playlist_editor_removeElement(curItem);
// 				}
// 
// 				interface_displayMenu(1);
// 				return 1;
			case interfaceCommandUp:
			case interfaceCommandDown:
			case interfaceCommandPageUp:
			case interfaceCommandPageDown:
			{
				const table_IntInt_t moves[] = {
					{interfaceCommandUp,        -1},
					{interfaceCommandDown,       1},
					{interfaceCommandPageUp,   -10},
					{interfaceCommandPageDown,  10},
					TABLE_INT_INT_END_VALUE
				};

				if(interface_switchMenuEntryCustom(pMenu, pMenu->selectedItem, table_IntIntLookup(moves, cmd->command, 0)) != 0) {
					return 1;
				}
				break;
			}
			case interfaceCommandEnter:
			case interfaceCommandOk:
				break;
			default:
				return 0;
		}
	} else {
		if(cmd->command == interfaceCommandYellow) {
				interface_getText(pMenu, _T("DVB_ENTER_CAPTION"), "\\w+", interface_saveChannelCaption, interface_getChannelCaption, inputModeABC, pMenu->pArg);
				return 0;
		}
	}

	if(cmd->command == interfaceCommandGreen) {
		playlist_editor_setupdate();
		offair_fillDVBTMenu();
		return 0;
	} else if(cmd->command == interfaceCommandRight) {
		int32_t n = pMenu->selectedItem;
		if((n >= 0) && (n < interface_getMenuEntryCount(pMenu))) {
			playList_nextChannelState(&pMenu->menuEntry[n], n);
			interface_displayMenu(1);
			return 1;
		}
	}

	return interface_listMenuProcessCommand(pMenu, cmd);
}



int32_t playlistEditor_init(void)
{
	int32_t playlistEditor_icons[4] = { statusbar_f1_cancel, statusbar_f2_ok, statusbar_f3_edit, 0};

	createListMenu(&InterfacePlaylistMain, _T("PLAYLIST_MAIN"), settings_interface, NULL, _M &OutputMenu,
        interfaceListMenuIconThumbnail, output_enterPlaylistMenu, NULL, NULL);

	createListMenu(&InterfacePlaylistAnalog, _T("PLAYLIST_ANALOG"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistAnalog, NULL, NULL);

	createListMenu(&InterfacePlaylistDigital, _T("PLAYLIST_DIGITAL"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistDigital, NULL, NULL);

	createListMenu(&InterfacePlaylistSelectDigital, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, enterPlaylistDigitalSelect, NULL, NULL);

	createListMenu(&InterfacePlaylistSelectAnalog, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, enterPlaylistAnalogSelect, NULL, NULL);

	createListMenu(&InterfacePlaylistEditorDigital, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, enterPlaylistEditorDigital, NULL, NULL);

	createListMenu(&InterfacePlaylistEditorAnalog, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, enterPlaylistEditorAnalog, NULL, NULL);

	InterfacePlaylistEditorDigital.baseMenu.processCommand = playlistEditor_processCommand;
	InterfacePlaylistEditorAnalog.baseMenu.processCommand = playlistEditor_processCommand;

	return 0;
}

int32_t playlistEditor_terminate(void)
{
	return 0;
}



editorDigital_t *editorList_add(struct list_head *listHead)
{
	editorDigital_t *element;
	element = malloc(sizeof(editorDigital_t));
	if(!element) {
		eprintf("%s(): Allocation error!\n", __func__);
		return NULL;
	}
	list_add_tail(&element->list, listHead);

	return element;
}

void editorList_release(void)
{
	struct list_head *pos;
	struct list_head *n;

	list_for_each_safe(pos, n, &editor_playList) {
		editorDigital_t *el = list_entry(pos, editorDigital_t, list);

		list_del(&el->list);
		free(el);
	}
}

editorDigital_t *editorList_get(struct list_head *listHead, uint32_t number)
{
	struct list_head *pos;
	uint32_t id = 0;
	if(!listHead) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return NULL;
	}
	list_for_each(pos, listHead) {
		if(id == number) {
			editorDigital_t *el = list_entry(pos, editorDigital_t, list);
			return el;
		}
		id++;
	}
	return NULL;
}

#endif //#ifdef ENABLE_DVB
