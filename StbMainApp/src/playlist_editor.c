

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
#include "gfx.h"


/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
typedef struct {
	uint32_t frequency;
	//analog_service_t *service_index;
	service_index_data_t  data;
	struct list_head      list;
} playListEditorAnalog_t;

typedef struct {
	int32_t               radio;
	int32_t               scrambled;
	service_index_t      *service_index;
	service_index_data_t  data;
	struct list_head      list;
} editorDigital_t;

typedef struct {
	typeBouquet_t  type;
	int32_t        isChanged;
	void          *data;
} playlistEditorMenuParam_t;

/******************************************************************
* GLOBAL DATA                                                     *
*******************************************************************/
interfaceListMenu_t InterfacePlaylistMain;

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/
static int32_t channelNumber = -1;
static int32_t color_save = -1;
static int32_t swapMenu;
static interfaceListMenu_t InterfacePlaylistAnalog;
static interfaceListMenu_t InterfacePlaylistDigital;
static interfaceListMenu_t InterfacePlaylistSelectDigital;
static interfaceListMenu_t InterfacePlaylistSelectAnalog;
static interfaceListMenu_t InterfacePlaylistEditorDigital;
static interfaceListMenu_t InterfacePlaylistEditorAnalog;

static struct list_head editor_digitalPlayList = LIST_HEAD_INIT(editor_digitalPlayList);
static struct list_head editor_analogPlayList = LIST_HEAD_INIT(editor_analogPlayList);

static playlistEditorMenuParam_t playlistEditorMenu_digitalParam = {eBouquet_digital, 0, NULL};
static playlistEditorMenuParam_t playlistEditorMenu_analogParam = {eBouquet_analog, 0, NULL};

static int32_t needRefillDigital = 1;
static int32_t needRefillAnalog = 1;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int32_t playlistEditor_enterMainMenu(interfaceMenu_t *pMenu, void* notused);
static int32_t output_enterPlaylistDigital(interfaceMenu_t *pMenu, void* notused);
static int32_t output_enterPlaylistAnalog(interfaceMenu_t *pMenu, void* notused);
static void playList_saveName(typeBouquet_t btype, int32_t num, char *prev_name, char *new_name);
static int32_t get_statusLockPlaylist(void);
static void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, typeBouquet_t btype, int32_t count);
// static void playlist_editor_removeElement(void);
// static int32_t getChannelEditor(void);
static void playlistEditor_moveElement(typeBouquet_t index, int32_t sourceNum, int32_t move);
static char *output_getSelectedNamePlaylistEditor(void);
// static int32_t enablePlayListSelectMenu(interfaceMenu_t *interfaceMenu);
static int32_t playlistEditor_fillDigital(interfaceMenu_t *interfaceMenu, void *pArg);
static int32_t playlistEditor_fillAnalog(interfaceMenu_t *interfaceMenu, void *pArg);
static int32_t playlistEditor_save(playlistEditorMenuParam_t *pParam);

// function for editor list
static playListEditorAnalog_t *editorAnalogList_get(struct list_head *listHead, uint32_t number);
static editorDigital_t *editorList_get(struct list_head *listHead, uint32_t number);
static editorDigital_t *editorList_add(struct list_head *listHead);
static playListEditorAnalog_t *editorAnalogList_add(struct list_head *listHead);
static void editorList_release(typeBouquet_t index, struct list_head *listHead);
static void merge_editorList(typeBouquet_t editorType);

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>_<Word>+    *
*******************************************************************/
void setColor(int32_t color) {
	color_save = color;
}

int32_t getColor() {
	return color_save;
}
static void load_channels(typeBouquet_t index, struct list_head *listHead)
{
	struct list_head *pos;
	if(!listHead) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return;
	}
	if (index == eBouquet_digital) {

		list_for_each(pos, dvbChannel_getSortList()) {
			editorDigital_t *element;
			service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

			if(srvIdx == NULL)
				continue;

			element = editorList_add(listHead);

			element->service_index = srvIdx;
			element->data.visible = srvIdx->data.visible;
			element->data.parent_control = srvIdx->data.parent_control;

			if(service_isRadio(srvIdx->service)
					|| (dvb_hasMediaType(srvIdx->service, mediaTypeAudio) && !dvb_hasMediaType(srvIdx->service, mediaTypeVideo)))
			{
				element->radio = 1;
			} else {
				element->radio = 0;
			}
			element->scrambled = dvb_getScrambled(srvIdx->service);

			if (srvIdx->data.channelsName){
				snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->data.channelsName);
			}
		}
	} else if (index == eBouquet_analog) {
		struct list_head *head = analogtv_getChannelList();
		if(head) {
			list_for_each(pos, head) {
				playListEditorAnalog_t *element;

				analog_service_t *srvIdx = list_entry(pos, analog_service_t, channelList);

				if(srvIdx == NULL) {
					continue;
				}

				element = editorAnalogList_add(listHead);
				element->frequency = srvIdx->frequency;
				element->data.visible = srvIdx->visible;
				snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->customCaption);
			}
		}
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

static int32_t get_statusLockPlaylist(void)
{
	if (channelNumber == -1)
		return false;
	return true;
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

static void playList_saveName(typeBouquet_t btype, int32_t num, char *prev_name, char *new_name)
{
	if(btype == eBouquet_digital) {
		editorDigital_t *element;

		element = editorList_get(&editor_digitalPlayList, num);
		if(element == NULL) {
			return;
		}

		if(strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))) {
			snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
			return;
		}
		return;
	} else if(btype == eBouquet_analog) {
		playListEditorAnalog_t *element;

		element = editorAnalogList_get(&editor_analogPlayList, num);
		if(element == NULL) {
			return;
		}
		if(strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))) {
			snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
			return;
		}
		return;
	}
}

static void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, typeBouquet_t btype, int32_t count)
{
	if(btype == eBouquet_digital) {
		editorDigital_t *element;

		element = editorList_get(&editor_digitalPlayList, count);
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
		interface_changeMenuEntryLabel(pMenuEntry, desc);
		interface_changeMenuEntryThumbnail(pMenuEntry, element->data.visible ? (element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels)) : thumbnail_not_selected);
//		interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
		return;
	} else if(btype == eBouquet_analog) {
		playListEditorAnalog_t *element;
		element = editorAnalogList_get(&editor_analogPlayList, count);

		char desc[20];
		if(element->data.visible) {
			element->data.visible = false;
			snprintf(desc, sizeof(desc), "INVISIBLE");
		} else {
			element->data.visible = true;
			snprintf(desc, sizeof(desc), "VISIBLE");
		}
		interface_changeMenuEntryLabel(pMenuEntry, desc);
		interface_changeMenuEntryThumbnail(pMenuEntry, element->data.visible ? thumbnail_tvstandard : thumbnail_not_selected);
		//				interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
		return;
	}
}

static void merge_editorList(typeBouquet_t editorType)
{
	struct list_head *pos;
	int32_t i = 0;
	int32_t first = 0;

	if(editorType == eBouquet_digital) {
		list_for_each(pos, &editor_digitalPlayList) {
			editorDigital_t *element = list_entry(pos, editorDigital_t, list);

			if(element == NULL) {
				continue;
			}

			if(element->service_index != NULL) {
				element->service_index->data.visible        = element->data.visible;
				element->service_index->data.parent_control = element->data.parent_control;
				snprintf(element->service_index->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", element->data.channelsName);
			}
			first = dvbChannel_findNumberService(element->service_index);
			if(i > first) {
				dvbChannel_swapServices(first, i);
			} else {
				dvbChannel_swapServices(i, first);
			}
			i++;
		}
	} else if(editorType == eBouquet_analog) {
		list_for_each(pos, &editor_analogPlayList) {
			playListEditorAnalog_t *element = list_entry(pos, playListEditorAnalog_t, list);
			if(element == NULL) {
				continue;
			}

			first = analogtv_findOnFrequency(element->frequency);
			if(first == -1) {
				continue;
			}

			analogtv_setChannelsData(element->data.channelsName, element->data.visible, first);

			if(i > first) {
				analogtv_swapService(first, i);
			} else {
				analogtv_swapService(i, first);
			}
			i++;
		}
	}
}

static int32_t playlistEditor_saveInternal(playlistEditorMenuParam_t *pParam)
{
	merge_editorList(pParam->type);
	if(pParam->type == eBouquet_digital) {
		dvbChannel_applyUpdates();
	} else  if(pParam->type == eBouquet_analog) {
		analogtv_applyUpdates();
	}
	interface_showMessageBox(_T("SAVE"), thumbnail_info, 3000);
	pParam->isChanged = 0;

	return 0;
}

static int32_t playList_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	(void)pMenu;

	if(parentControl_checkPass(value) == 0) {
		playlistEditor_saveInternal(pArg);
	} else {
		interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
		return 1;
	}
	return 0;
}

static int32_t playlistEditor_save(playlistEditorMenuParam_t *pParam)
{
	if(pParam->type == eBouquet_digital) {
		if(list_empty(&editor_digitalPlayList)) {
			return -1;
		}

		struct list_head *pos;
		list_for_each(pos, &editor_digitalPlayList) {
			editorDigital_t *element = list_entry(pos, editorDigital_t, list);
			if(element == NULL) {
				continue;
			}
			if(element->service_index != NULL) {
				if(element->data.parent_control != element->service_index->data.parent_control) {
					const char *mask = "\\d{6}";
					interface_getText((interfaceMenu_t*)&InterfacePlaylistEditorDigital, _T("ENTER_PASSWORD"), mask, playList_checkParentControlPass, NULL, inputModeDirect, (void *)pParam);
					return 0;
				}
			}
		}
	} else if(pParam->type == eBouquet_analog) {
		if(list_empty(&editor_analogPlayList)) {
			return -1;
		}
	} else {
		eprintf("%s(): Error, unknown playlist editor editorType=%d\n", __func__, pParam->type);
		return -1;
	}

	playlistEditor_saveInternal(pParam);
	bouquet_save(pParam->type, bouquet_getCurrentName(pParam->type));

	return 0;
}

int32_t playList_editorChannel(interfaceMenu_t *pMenu, void* pArg)
{
	if(channelNumber == -1) {
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

static int32_t playlistEditor_toggleEnable(interfaceMenu_t *pMenu, void *pArg)
{
	bouquet_setEnable(!bouquet_isEnable());
	output_redrawMenu(pMenu);
	return 1;
}

static int32_t bouquet_removeBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	const char *bouquetName;
	bouquetName = bouquet_getCurrentName(eBouquet_digital);
    offair_stopVideo(screenMain, 1);
	if(bouquetName != NULL) {
		interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
		bouquet_remove(eBouquet_digital, bouquetName);
		interface_hideMessageBox();
	}

	output_redrawMenu(pMenu);
	//Change selection on "Select playlist", coz this current item became disabled
	interface_setSelectedItem(pMenu, 0);
	return 0;
}

static int32_t bouquet_updateBouquetList(interfaceMenu_t *pMenu, void *pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;
	typeBouquet_t btype = pParam->type;

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	bouquet_updateNameList(btype, 1);
	interface_hideMessageBox();

	output_redrawMenu(pMenu);
	return 0;
}


static int32_t bouquet_sendBouquetOnServer(interfaceMenu_t *pMenu, void *pArg)
{
	typeBouquet_t btype = (typeBouquet_t)pArg;

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	bouquet_upload(btype, bouquet_getCurrentName(btype));
	interface_hideMessageBox();

	return 0;
}

static int32_t bouquet_updateCurent(interfaceMenu_t *pMenu, void *pArg)
{
	const char *bouquetName;
	typeBouquet_t btype = (typeBouquet_t)pArg;

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	gfx_stopVideoProvider(screenMain, 1, 1);

	bouquetName = bouquet_getCurrentName(btype);
	bouquet_update(btype, bouquetName);
	bouquet_open(btype, bouquetName, 1);

	gfx_resumeVideoProvider(screenMain);
	interface_hideMessageBox();

	return 0;
}

static int32_t playlistEditor_createNewBouquet(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;
	typeBouquet_t btype = pParam->type;

	if((value == NULL) || (strlen(value) <= 0)) {
		interface_showMessageBox(_T("PLAYLIST_WRONG_NAME"), thumbnail_loading, 3000);
		return 0;
	}
	gfx_stopVideoProvider(screenMain, 1, 1);


	if(strList_isExist(bouquet_getNameList(btype), value)) {
		interface_showMessageBox(_T("PLAYLIST_NAME_EXIST"), thumbnail_loading, 0);
		if(!bouquet_isExist(btype, value)) {
			interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
			bouquet_update(btype, value);
		}
	} else {
		bouquet_create(btype, value);
	}

	interface_hideMessageBox();
	if(bouquet_open(btype, value, 0) == 0) {
		if(btype == eBouquet_digital) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistDigital);
		} else if(btype == eBouquet_analog) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistAnalog);
		}
	} else {
		output_redrawMenu(pMenu);
		interface_showMessageBox(_T("PLAYLIST_CANT_OPEN"), thumbnail_error, 3000);
		return -2;
	}
	output_redrawMenu(pMenu);
	return 0;
}


static int32_t playlistEditor_enterNewBouquetNameConfirm(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			interface_getText(pMenu, _T("ENTER_BOUQUET_NAME"), "\\w+", playlistEditor_createNewBouquet, NULL, inputModeABC, pArg);
			return 1;
			break;
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
		default:
			return 0;
			break;
	}
	return 1;
}

static int32_t playlistEditor_enterNewBouquetName(interfaceMenu_t *pMenu, void *pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	if(pParam->isChanged) {
		interface_showConfirmationBox(_T("PLAYLIST_CHANGES_LOSE_ON_CONTINUE"), thumbnail_warning, playlistEditor_enterNewBouquetNameConfirm, pArg);
		return 1;
	}

	interface_getText(pMenu, _T("ENTER_BOUQUET_NAME"), "\\w+", playlistEditor_createNewBouquet, NULL, inputModeABC, pArg);
	return 0;
}

static int32_t playlistEditor_setBouquet(interfaceMenu_t *pMenu, playlistEditorMenuParam_t *pParam)
{
	const char *newBouquetName;
	int32_t number;
	int32_t forceReload = 0;
	typeBouquet_t btype = pParam->type;

	number = interface_getSelectedItem(pMenu) - 4; //TODO: remove number

	newBouquetName = strList_get(bouquet_getNameList(btype), number);

	if(newBouquetName == NULL) {
		eprintf("ERROR: New bouquet name is NULL!\n", __func__);
		return -1;
	}
	gfx_stopVideoProvider(screenMain, 1, 1);

	if(!bouquet_isExist(btype, newBouquetName)) {
		interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
		bouquet_update(btype, newBouquetName);
		interface_hideMessageBox();
		forceReload = 1;
	}

	if(bouquet_open(btype, newBouquetName, forceReload) == 0) {
		if(btype == eBouquet_digital) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistDigital);
		} else if(btype == eBouquet_analog) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistAnalog);
		}
	} else {
		output_redrawMenu(pMenu);
		interface_showMessageBox(_T("PLAYLIST_CANT_OPEN"), thumbnail_error, 3000);
		return -2;
	}

	return 0;
}

static int32_t playlistEditor_changeBouquetConfirm(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			playlistEditor_setBouquet(pMenu, (playlistEditorMenuParam_t *)pArg);
			return 0;
			break;
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
		default:
			return 0;
			break;
	}
	return 1;
}

static int32_t bouquets_setBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	if(pParam->isChanged) {
		interface_showConfirmationBox(_T("PLAYLIST_CHANGES_LOSE_ON_CONTINUE"), thumbnail_warning, playlistEditor_changeBouquetConfirm, pArg);
		return 1;
	}

	return playlistEditor_setBouquet(pMenu, pParam);
}

static int32_t playlistEditor_enterNameListMenu(interfaceMenu_t *nameListMenu, void *pArg)
{
	int32_t i = 0;
	const char *name;
	const char *bouquet_name;
	const char *title;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	typeBouquet_t btype = ((playlistEditorMenuParam_t *)pArg)->type;
	
	if(btype == eBouquet_digital) {
		title = _T("PLAYLIST_AVAILABLE_DIGITAL_NAMES");
	} else if(btype == eBouquet_analog) {
		title = _T("PLAYLIST_AVAILABLE_ANALOG_NAMES");
	} else {
		return -1;
	}

	bouquet_name = bouquet_getCurrentName(btype);
	interface_clearMenuEntries(nameListMenu);

	snprintf(channelEntry, sizeof(channelEntry), "%s: %s", _T("PLAYLIST_NOW_SELECT"), bouquet_name ? bouquet_name : _T("PLAYLIST_NOT_SELECTED"));
	interface_addMenuEntryDisabled(nameListMenu, channelEntry, 0);
	interface_addMenuEntry(nameListMenu, _T("PLAYLIST_NEW_BOUQUETS"), playlistEditor_enterNewBouquetName, pArg, settings_interface);
	interface_addMenuEntry(nameListMenu, _T("PLAYLIST_UPDATE_LIST"), bouquet_updateBouquetList, pArg, settings_interface);
	interface_addMenuEntryDisabled(nameListMenu, title, 0);

	while((name = strList_get(bouquet_getNameList(btype), i)) != NULL) {
		snprintf(channelEntry, sizeof(channelEntry), "%02d %s", i + 1, name);
		interface_addMenuEntry(nameListMenu, channelEntry, bouquets_setBouquet, pArg, thumbnail_epg);

		if(bouquet_name && (strcasecmp(name, bouquet_name) == 0)) {
			interfaceMenuEntry_t *entry = menu_getLastEntry(nameListMenu);
			if(entry) {
				interface_setMenuEntryLabel(entry, _T("SELECTED"));
			}
		}
		i++;
	}
	if(i == 0) {
		interface_addMenuEntryDisabled(nameListMenu, "NULL", 0);
	}
	return 0;
}

int32_t playlistEditor_enterIntoAnalog(interfaceMenu_t *interfaceMenu, void *pArg)
{
	if(needRefillAnalog == 0) {
		return 0;
	}
	editorList_release(eBouquet_analog, &editor_analogPlayList);
	load_channels(eBouquet_analog, &editor_analogPlayList);


	playlistEditor_fillAnalog(interfaceMenu, pArg);
	needRefillAnalog = 0;

/*	if((appControlInfo.dvbInfo.channel >= 0) && (appControlInfo.dvbInfo.channel < interface_getMenuEntryCount(interfaceMenu))) {
		interface_setSelectedItem(interfaceMenu, appControlInfo.dvbInfo.channel);
	}*/
	return 0;
}

static int32_t playlistEditor_fillAnalog(interfaceMenu_t *interfaceMenu, void* pArg)
{
	interfaceMenu_t *channelMenu = interfaceMenu;
	struct list_head *pos;
	int32_t i = 0;
	(void)pArg;

	interface_clearMenuEntries(channelMenu);

	list_for_each(pos, &editor_analogPlayList) {
		char channelEntry[MENU_ENTRY_INFO_LENGTH];
		playListEditorAnalog_t *element = list_entry(pos, playListEditorAnalog_t, list);

		if(element == NULL) {
			continue;
		}

		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->data.channelsName);
		interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->data.visible ? thumbnail_tvstandard : thumbnail_not_selected);

		interfaceMenuEntry_t *entry;
		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			char desc[20];
			snprintf(desc, sizeof(desc), "%s", element->data.visible ? "VISIBLE" : "INVISIBLE");
			interface_setMenuEntryLabel(entry, desc);
		}
		i++;
	}
	if(i == 0) {
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	}
	return 0;
}

static int32_t playlistEditor_fillDigital(interfaceMenu_t *interfaceMenu, void* pArg)
{
	interfaceMenu_t *channelMenu = interfaceMenu;
	struct list_head *pos;
	int32_t i = 0;
	(void)pArg;

	interface_clearMenuEntries(channelMenu);

	list_for_each(pos, &editor_digitalPlayList) {
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

int32_t playlistEditor_enterIntoDigital(interfaceMenu_t *interfaceMenu, void *pArg)
{
// 	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;
	if(needRefillDigital == 0) {
		return 0;
	}

	editorList_release(eBouquet_digital, &editor_digitalPlayList);
	load_channels(eBouquet_digital, &editor_digitalPlayList);
	playlistEditor_fillDigital(interfaceMenu, pArg);
	needRefillDigital = 0;

	if((appControlInfo.dvbInfo.channel >= 0) && (appControlInfo.dvbInfo.channel < interface_getMenuEntryCount(interfaceMenu))) {
		interface_setSelectedItem(interfaceMenu, appControlInfo.dvbInfo.channel);
	}
	return 0;
}

static int32_t playlistEditor_wantSaveConfirm(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			playlistEditor_save(pArg);
			return 0;
			break;
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
		default:
			return 0;
			break;
	}
	return 1;
}

static int32_t playlistEditor_onExit(interfaceMenu_t *interfaceMenu, void* pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	if(pParam->isChanged) {
		interface_showConfirmationBox(_T("PLAYLIST_CHANGED_DO_YOU_WANT_SAVE"), thumbnail_warning, playlistEditor_wantSaveConfirm, pArg);
	}

	return 0;
}

static int32_t playlistEditor_saveParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if(parentControl_savePass(value) != 0) {
		interface_showMessageBox(_T("PARENTCONTROL_CANT_SAVE_PASS"), thumbnail_error, 3000);
		return -1;
	}
	return 0;
}

static int32_t playlistEditor_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	if(parentControl_checkPass(value) == 0) {
		const char *mask = "\\d{6}";
		interface_getText(pMenu, _T("PARENTCONTROL_ENTER_NEW_PASSWORD"), mask, playlistEditor_saveParentControlPass, NULL, inputModeDirect, pArg);
		return 1;
	}
	return 0;
}

static int32_t playlistEditor_changeParentControlPass(interfaceMenu_t* pMenu, void* pArg)
{
	const char *mask = "\\d{6}";
	interface_getText(pMenu, _T("PARENTCONTROL_CHECK_PASSWORD"), mask, playlistEditor_checkParentControlPass, NULL, inputModeDirect, pArg);
	return 0;
}

static int32_t playlistEditor_enterMainMenu(interfaceMenu_t *interfaceMenu, void* notused)
{
	char str[MENU_ENTRY_INFO_LENGTH];
	int32_t enabled = bouquet_isEnable();
	interface_clearMenuEntries(interfaceMenu);
	snprintf(str, sizeof(str), "%s: %s", _T("PLAYLIST_ENABLE"), enabled ? "ON" : "OFF");
	interface_addMenuEntry(interfaceMenu, str, playlistEditor_toggleEnable, NULL, settings_interface);
	
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_ANALOG"), enabled, interface_menuActionShowMenu, &InterfacePlaylistAnalog, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_DIGITAL"), enabled, interface_menuActionShowMenu, &InterfacePlaylistDigital, settings_interface);
	return 0;
}

static int32_t output_enterPlaylistAnalog(interfaceMenu_t *interfaceMenu, void *pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	const char *bouquetName;
	int32_t enabled;
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	interface_clearMenuEntries(interfaceMenu);
	bouquetName = bouquet_getCurrentName(eBouquet_analog);
	enabled = bouquetName ? 1 : 0;

	snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquetName ? bouquetName : _T("PLAYLIST_NOT_SELECTED"));
	interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectAnalog, settings_interface);

	if(pParam->isChanged) {
		snprintf(buf, sizeof(buf), "%s (%s)", _T("PLAYLIST_EDITOR"), _T("PLAYLIST_MDIFIED"));
	} else {
		snprintf(buf, sizeof(buf), "%s", _T("PLAYLIST_EDITOR"));
	}

	interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistEditorAnalog, settings_interface);

	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_UPDATE"), enabled, bouquet_updateCurent, (void *)eBouquet_analog, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), enabled, bouquet_sendBouquetOnServer, (void *)eBouquet_analog, settings_interface);

	return 0;
}

static int32_t output_enterPlaylistDigital(interfaceMenu_t *interfaceMenu, void *pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	const char *bouquet_name;
	int32_t enabled;
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	interface_clearMenuEntries(interfaceMenu);

	bouquet_name = bouquet_getCurrentName(eBouquet_digital);
	enabled = bouquet_name ? 1 : 0;

	snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquet_name ? bouquet_name : _T("PLAYLIST_NOT_SELECTED"));
	interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectDigital, settings_interface);

	if(pParam->isChanged) {
		snprintf(buf, sizeof(buf), "%s (%s)", _T("PLAYLIST_EDITOR"), _T("PLAYLIST_MDIFIED"));
	} else {
		snprintf(buf, sizeof(buf), "%s", _T("PLAYLIST_EDITOR"));
	}
	interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistEditorDigital, settings_interface);

	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_UPDATE"), enabled, bouquet_updateCurent, (void *)eBouquet_digital, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), enabled, bouquet_sendBouquetOnServer, (void *)eBouquet_digital, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_REMOVE"), enabled, bouquet_removeBouquet, NULL, settings_interface);

	interface_addMenuEntry(interfaceMenu, _T("PARENTCONTROL_CHANGE"), playlistEditor_changeParentControlPass, NULL, settings_interface);
	return 0;
}

static char* interface_getChannelCaption(int32_t dummy, void* pArg)
{
	(void)dummy;
	char * ptr = strstr(output_getSelectedNamePlaylistEditor(), ". ");
	if(ptr) {
		ptr += 2;
		return ptr;
	}
	return NULL;
}

static int32_t interface_saveChannelCaption(interfaceMenu_t *pMenu, char *pStr, void* pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pMenu->pArg;
	if(pStr == NULL) {
		return -1;
	}
	playList_saveName(pParam->type, pMenu->selectedItem, pMenu->menuEntry[pMenu->selectedItem].info, pStr);
	snprintf(pMenu->menuEntry[pMenu->selectedItem].info, MENU_ENTRY_INFO_LENGTH, "%s. %s", offair_getChannelNumberPrefix(pMenu->selectedItem), pStr);
	pParam->isChanged = 1;
	return 0;
}

static void playlistEditor_moveElement(typeBouquet_t index, int32_t elementId, int32_t move)
{
	struct list_head *el;
	int32_t i = 0;

	dprintf("%s[%d] elementId=%d\n", __func__, __LINE__, elementId);
	if(index == eBouquet_digital) {
		list_for_each(el, &editor_digitalPlayList) {
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
					if(newPos == &editor_digitalPlayList) {
	//					eprintf("%s(): Warning, something wrong!!!\n", __func__);
						break;
					}
				}
				list_add(el, newPos);
				break;
			}
			i++;
		}
	} else if(index == eBouquet_analog) {
		list_for_each(el, &editor_analogPlayList) {
			playListEditorAnalog_t *secElement = list_entry(el, playListEditorAnalog_t, list);
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
					if(newPos == &editor_digitalPlayList) {
	//					eprintf("%s(): Warning, something wrong!!!\n", __func__);
						break;
					}
				}
				list_add(el, newPos);
				break;
			}
			i++;
		}
	}
}

static int32_t interface_moveMenuEntry(interfaceMenu_t *pMenu, int32_t srcId, int32_t move)
{
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
	playlistEditorMenuParam_t *pParam = pMenu->pArg;
	playlistEditor_moveElement(pParam->type, srcId, move);

	if(pParam->type == eBouquet_digital) {
		playlistEditor_fillDigital(pMenu, NULL);
	} else if(pParam->type == eBouquet_analog) {
		playlistEditor_fillAnalog(pMenu, NULL);
	}

	return 0;
}

static int32_t playlistEditor_processCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pMenu->pArg;

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
				int32_t itemHeight;
				int32_t maxVisibleItems;
				interface_listMenuGetItemInfo((interfaceListMenu_t *)pMenu, &itemHeight, &maxVisibleItems);

				table_IntInt_t moves[] = {
					{interfaceCommandUp,       -1},
					{interfaceCommandDown,      1},
					{interfaceCommandPageUp,   -maxVisibleItems},
					{interfaceCommandPageDown,  maxVisibleItems},
					TABLE_INT_INT_END_VALUE
				};

				if(interface_moveMenuEntry(pMenu, pMenu->selectedItem, table_IntIntLookup(moves, cmd->command, 0)) != 0) {
					return 1;
				}
				pParam->isChanged = 1;
				break;
			}
			case interfaceCommandEnter:
			case interfaceCommandOk:
				break;
			default:
				return 0;
		}
	}

	if(cmd->command == interfaceCommandGreen) {
		playlistEditor_save(pMenu->pArg);
		return 0;
	} else if(cmd->command == interfaceCommandYellow) {//edit name
		if(pParam->type == eBouquet_digital) {
			interface_getText(pMenu, _T("DVB_ENTER_CAPTION"), "\\w+", interface_saveChannelCaption, interface_getChannelCaption, inputModeABC, pMenu->pArg);
		} else if(pParam->type == eBouquet_analog) {
			if(!analogNames_isExist()) {
				interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
				analogNames_download();
				interface_hideMessageBox();
			}
			if(analogNames_isExist()) {
				analogNames_load();
				interface_listBoxGetText(pMenu, _T("DVB_ENTER_CAPTION"), "\\w+", interface_saveChannelCaption,
										 interface_getChannelCaption, inputModeABC, pMenu->pArg, analogNames_getList());
			} else {
				interface_getText(pMenu, _T("DVB_ENTER_CAPTION"), "\\w+", interface_saveChannelCaption, interface_getChannelCaption, inputModeABC, pMenu->pArg);
			}
		}
		return 0;
	} else if(cmd->command == interfaceCommandRight) {
		int32_t n = pMenu->selectedItem;
		if((n >= 0) && (n < interface_getMenuEntryCount(pMenu))) {
			playList_nextChannelState(&pMenu->menuEntry[n], pParam->type, n);
			interface_displayMenu(1);
			pParam->isChanged = 1;
			return 1;
		}
	}

	return interface_listMenuProcessCommand(pMenu, cmd);
}

static int32_t playlistEditor_analogChangeCallback(void *pArg)
{
	(void)pArg;
	needRefillAnalog = 1;
	playlistEditorMenu_analogParam.isChanged = 0;
	return 0;
}

static int32_t playlistEditor_dvbChannelsChangeCallback(void *pArg)
{
	(void)pArg;
	needRefillDigital = 1;
	playlistEditorMenu_digitalParam.isChanged = 0;
	return 0;
}


int32_t playlistEditor_init(void)
{
	int32_t playlistEditor_icons[4] = { statusbar_f1_cancel, statusbar_f2_ok, statusbar_f3_edit, 0};

	createListMenu(&InterfacePlaylistMain, _T("PLAYLIST_MAIN"), settings_interface, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, playlistEditor_enterMainMenu, NULL, NULL);

	createListMenu(&InterfacePlaylistAnalog, _T("PLAYLIST_ANALOG"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistAnalog, NULL, (void *)&playlistEditorMenu_analogParam);

	createListMenu(&InterfacePlaylistDigital, _T("PLAYLIST_DIGITAL"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistDigital, NULL, (void *)&playlistEditorMenu_digitalParam);

	createListMenu(&InterfacePlaylistSelectAnalog, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, playlistEditor_enterNameListMenu, NULL, (void *)&playlistEditorMenu_analogParam);

	createListMenu(&InterfacePlaylistSelectDigital, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, playlistEditor_enterNameListMenu, NULL, (void *)&playlistEditorMenu_digitalParam);

	createListMenu(&InterfacePlaylistEditorDigital, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, playlistEditor_enterIntoDigital, playlistEditor_onExit, (void *)&playlistEditorMenu_digitalParam);

	createListMenu(&InterfacePlaylistEditorAnalog, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, playlistEditor_enterIntoAnalog, playlistEditor_onExit, (void *)&playlistEditorMenu_analogParam);

	//trick for early handle command
	InterfacePlaylistEditorDigital.baseMenu.processCommand = playlistEditor_processCommand;
	InterfacePlaylistEditorAnalog.baseMenu.processCommand = playlistEditor_processCommand;

	//Register callback on dvbChannels chanched
	dvbChannel_registerCallbackOnChange(playlistEditor_dvbChannelsChangeCallback, NULL);
	analogtv_registerCallbackOnChange(playlistEditor_analogChangeCallback, NULL);

	return 0;
}

int32_t playlistEditor_terminate(void)
{
	editorList_release(eBouquet_digital, &editor_digitalPlayList);
	editorList_release(eBouquet_analog, &editor_digitalPlayList);
	return 0;
}

playListEditorAnalog_t *editorAnalogList_add(struct list_head *listHead)
{
	playListEditorAnalog_t *element;
	element = malloc(sizeof(playListEditorAnalog_t));
	if(!element) {
		eprintf("%s(): Allocation error!\n", __func__);
		return NULL;
	}
	list_add_tail(&element->list, listHead);

	return element;
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

void editorAnalogList_release(struct list_head *listHead)
{
	struct list_head *pos;
	struct list_head *n;

	list_for_each_safe(pos, n, listHead) {
		playListEditorAnalog_t *el = list_entry(pos, playListEditorAnalog_t, list);

		list_del(&el->list);
		free(el);
	}
}

void editorList_release(typeBouquet_t index, struct list_head *listHead)
{
	struct list_head *pos;
	struct list_head *n;

	if(index == eBouquet_digital) {
		list_for_each_safe(pos, n, listHead) {
			editorDigital_t *el = list_entry(pos, editorDigital_t, list);

			list_del(&el->list);
			free(el);
		}
	} else if(index == eBouquet_analog) {
		list_for_each_safe(pos, n, listHead) {
			playListEditorAnalog_t *el = list_entry(pos, playListEditorAnalog_t, list);

			list_del(&el->list);
			free(el);
		}
	}
}

playListEditorAnalog_t *editorAnalogList_get(struct list_head *listHead, uint32_t number)
{
	struct list_head *pos;
	uint32_t id = 0;
	if(!listHead) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return NULL;
	}
	list_for_each(pos, listHead) {
		if(id == number) {
			playListEditorAnalog_t *el = list_entry(pos, playListEditorAnalog_t, list);
			return el;
		}
		id++;
	}
	return NULL;
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
