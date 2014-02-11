
/*
 rutube.cpp

Copyright (C) 2012  Elecard Devices

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Elecard Devices nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ELECARD DEVICES BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "rutube.h"

#ifdef ENABLE_RUTUBE

#include "app_info.h"
#include "debug.h"
#include "downloader.h"
#include "gfx.h"
#include "interface.h"
#include "l10n.h"
#include "media.h"
#include "StbMainApp.h"
#include "xmlconfig.h"
#include <tinyxml.h>

#include <common.h>
#include <cstring>
#include <stdio.h>
#include <time.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define RUTUBE_FILENAME_LENGTH 48
#define RUTUBE_INFO_BUFFER_SIZE  (512*2)
#define RUTUBE_FILESIZE_MAX    (4*1024*1024)
#define MENU_ENTRY_INFO_LENGTH 256
#define KEYWORDS_MAX_NUMBER 10
#define SECONDS_PER_DAY (24*60*60)

/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/

/** Function to be called for each playlist entry on parsing
 *  Optional xmlConfigHandle_t parameter may contain pointer to XML structure of a given track.
 */
typedef void (*rutubeEntryHandler)(void *, const char *, const char *, const xmlConfigHandle_t);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

/**
 A list, which consists of categories' names.
 Each menuEntry of RutubeCategories uses pArg as pointer to first asset in this category.
 Each asset has pointer to next asset in it's category.
 */
interfaceListMenu_t RutubeCategories;

/**
 A list, keeping subcategories.
 Each menuEntry uses pArg as pointer to first asset in this subcategory.
 */
interfaceListMenu_t RutubeSubCategories;

/**
 A list, which consists of movies' titles.
 It's a one menu for all categories. It redraws for each category.
 Also uses as menu for found movies.
 */
interfaceListMenu_t MoviesMenu;

static time_t start_time;

/**
 Index of a selected RutubeCategories element.
*/
static int selectedCategory;

/**
 Keeps information about movie, which is playing on the background.
*/
static rutube_asset_t *playingMovie;

/**************************************************************************
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

static char *rutube_url = "http://rutube.ru/export/xml/elecard.xml";
// FIXME: filesize greater than RUTUBE_FILESIZE_MAX
//static char *rutube_url = "http://rutube.ru/export/xml/elecard_hd.xml";
// extern char *rutube_url = "http://rutube.ru/export/xml/mp4h264.xml";
// extern char *rutube_url = "http://rutube.ru/export/sony/";

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static int rutube_enterCategoriesMenu( interfaceMenu_t* pMenu, void *pArg );
static int rutube_enterSubCategoriesMenu( interfaceMenu_t* pMenu, void *pArg );
static int rutube_fillMoviesMenu( interfaceMenu_t* pMenu, void *pArg );
static int rutube_playMovie( interfaceMenu_t* pMenu, void *pArg  );
static int rutube_exitMenu( interfaceMenu_t* pMenu, void *pArg );
/**
 Shows user information about chosen movie.
*/
static int movie_infoCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
/**
 Parses a xml file, memorizes data of movies (title, url, description, thumbnail).
 Fulls RutubeCategories menu with categories' names.
*/
static void rutube_playlist_parser( int index, void *pArg );
/**
 Gives user a text field for typing a search string.
*/
static int rutube_videoSearch(interfaceMenu_t *pMenu, void* pArg);
static int rutube_startVideoSearch(interfaceMenu_t *pMenu, char *value, void* pArg);
/**
 Shows user the example of search string.
*/
static char *rutube_getLastSearch(int index, void* pArg);

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<WoTrd>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

static void rutube_playlist_parser( int index, void *pArg)
{
	xmlConfigHandle_t rt_xml;
	xmlConfigHandle_t trebuchet_node;
	rutube_asset_t *asset, *tail_asset;
	TiXmlNode *asset_node;
	TiXmlNode *category_node;
	TiXmlElement *elem;
	const char *str_title, *str_url, *str_des, *str_thumb, *str_category_thumb;
	char *str;
	char *filename = (char*)pArg;


	interface_clearMenuEntries( (interfaceMenu_t*)&RutubeCategories );
	str = _T("COLLECTING_INFO");
	interface_addMenuEntry( (interfaceMenu_t*)&RutubeCategories, str, rutube_videoSearch, NULL, thumbnail_loading );
	interface_displayMenu(1);/* quick show menu */

	if(filename == NULL)
	{
		eprintf("%s: Can't start parse - there is no rutube file!\n", __FUNCTION__);
		interface_showMessageBox(_T("ERR_PLAY_FILE"), thumbnail_error, 3000);
		return;
	}

/* write xml from file to rt_xml object */
	eprintf("%s start parsing...\n", __FUNCTION__);

	rt_xml = xmlConfigOpen(filename);
// rt_xml = xmlConfigParse(data);
	if ( rt_xml == NULL
		|| (trebuchet_node = xmlConfigGetElement(rt_xml, "trebuchet", 0)) == NULL )
	{
		eprintf("%d\n", __LINE__);
		if (rt_xml)
		{
			eprintf("%d\n", __LINE__);
			xmlConfigClose(rt_xml);
		}
		interface_showMessageBox(_T("ERR_STREAM_UNAVAILABLE"), thumbnail_error, 0);
		eprintf("%s: Can't parse %s!\n", __FUNCTION__, filename);
		return;
	}

	interface_clearMenuEntries( (interfaceMenu_t*)&RutubeCategories );
/* first element of menu is video search */
	str = _T("VIDEO_SEARCH");
	interface_addMenuEntry( (interfaceMenu_t*)&RutubeCategories, str, rutube_videoSearch, NULL, thumbnail_search );

/* moving along assets, getting movie information: title, url, description, thumbnail, category */
	for ( asset_node = ((TiXmlNode *)trebuchet_node)->FirstChild(); asset_node != 0; asset_node = asset_node->NextSibling() )
	{
		if ( asset_node->Type() == TiXmlNode::TINYXML_ELEMENT && strcasecmp(asset_node->Value(), "asset") == 0 )
		{
			str_title = (char*)xmlConfigGetText(asset_node, "title");
			str_url = (char*)xmlConfigGetText(asset_node, "assetUrl");
			str_thumb = (char*)xmlConfigGetText(asset_node, "imageUrl");
			if( str_thumb == NULL || !*str_thumb )
				str_thumb = (const char*)thumbnail_internet;
			str_des = (char*)xmlConfigGetText(asset_node, "description");
			if( str_des == NULL || !*str_des )
				str_des = _T("ERR_NO_DATA");

			if( (str_title != NULL && *str_title) && ( str_url != NULL && *str_url) )
			{
			/* search for a category/saving category's name */
				for ( category_node = ((TiXmlNode *)asset_node)->FirstChild();
				      category_node != 0;  category_node = category_node->NextSibling() )
				{
					if (category_node->Type() == TiXmlNode::TINYXML_ELEMENT && strcasecmp(category_node->Value(), "category") == 0)
					{
						TiXmlNode *subcategory_node;
						const char*  str_category;

						elem = (TiXmlElement *)category_node;
						str_category = elem->Attribute("name");
						str_category_thumb = elem->Attribute("image_std");
						if( str_category_thumb == NULL || !*str_category_thumb )
							str_category_thumb = (const char*)thumbnail_folder;
						if( str_category != NULL && *str_category )
						{
							int i;
							asset = (rutube_asset_t*) dmalloc( sizeof(rutube_asset_t) );
							if(asset != NULL)
							{
								asset->title = NULL;
								asset->url = NULL;
								asset->description = NULL;
								asset->thumbnail = NULL;
								asset->nextInCategory = NULL;

							/* write data to asset's elements */
								helperSafeStrCpy( &( asset->title ), str_title );
								helperSafeStrCpy( &( asset->url ), str_url );
								helperSafeStrCpy( &( asset->thumbnail ), str_thumb );
								helperSafeStrCpy( &( asset->description ), str_des );

							/* checks is there any subcategory */
								if( ( subcategory_node = ((TiXmlNode *)category_node)->FirstChild() ) != NULL )
								{
									const char *subcategory, *subcategory_thumb;
							/* fills menu with elements */
									elem = (TiXmlElement *)subcategory_node;
									subcategory = elem->Attribute("name");
									subcategory_thumb = elem->Attribute("image_std");
									if( subcategory_thumb == NULL || !*subcategory_thumb )
										subcategory_thumb = (const char*)thumbnail_folder;

									if(RutubeSubCategories.baseMenu.menuEntryCount != 0)
									{
										int k;
							/* wchecking for being the same subcategory in RutubeSubCategories */
										for(k = 0; k < RutubeSubCategories.baseMenu.menuEntryCount; k++ )
										{
											if(strcmp(RutubeSubCategories.baseMenu.menuEntry[k].info, subcategory) == 0)
												break;
										}
							/* adding new subcategory it in the menu */
										if(k >= RutubeSubCategories.baseMenu.menuEntryCount)
										{
											interface_addMenuEntry( (interfaceMenu_t*)&RutubeSubCategories, subcategory,  rutube_fillMoviesMenu, (void*)asset, thumbnail_folder ) - 1;
											helperSafeStrCpy(&RutubeCategories.baseMenu.menuEntry[RutubeCategories.baseMenu.menuEntryCount-1].image, subcategory_thumb);
										}
									}
									else
									{
										interface_addMenuEntry( (interfaceMenu_t*)&RutubeSubCategories, subcategory,  rutube_fillMoviesMenu, (void*)asset, thumbnail_folder ) - 1;
										helperSafeStrCpy(&RutubeSubCategories.baseMenu.menuEntry[RutubeSubCategories.baseMenu.menuEntryCount-1].image, subcategory_thumb);
									}
								}

						/* wchecking for being the same category in RutubeCategories */
								for( i = 1; i < RutubeCategories.baseMenu.menuEntryCount; i++ )
								{
									if( strcmp(RutubeCategories.baseMenu.menuEntry[i].info , str_category) == 0 )
									{
										tail_asset->nextInCategory = asset;
										tail_asset = asset;
										break;
									}
								}
						/* found a new category; adding it in category menu */
								if( i >= RutubeCategories.baseMenu.menuEntryCount )
								{
									tail_asset = asset;
									if(subcategory_node != NULL)/* if there is a subcategory, pActivate is enter to subcategory menu */
										interface_addMenuEntry( (interfaceMenu_t*)&RutubeCategories, str_category, rutube_enterSubCategoriesMenu, (void*)tail_asset, thumbnail_folder ) - 1;
									else
										interface_addMenuEntry( (interfaceMenu_t*)&RutubeCategories, str_category,  rutube_fillMoviesMenu, (void*)tail_asset, thumbnail_folder ) - 1;
									helperSafeStrCpy(&RutubeCategories.baseMenu.menuEntry[RutubeCategories.baseMenu.menuEntryCount-1].image, str_category_thumb);
								}
							}
							else
							{
								eprintf("%s: Memory error!\n", __FUNCTION__);
								break;
							}
						}
						break;
					}
				}
			} else
			{
				dprintf("%s: Can't find informations - url or title is NULL!\n", __FUNCTION__);
			}
		}
	}
	if(rt_xml)
		xmlConfigClose(rt_xml);
	eprintf("%s done parsing\n", __FUNCTION__);
	interface_displayMenu(1);/* quick show menu */
	return;
}

void rutube_buildMenu(interfaceMenu_t* pParent)
{
	int rutube_icons[4] = { 0, statusbar_f2_info, 0, 0 };

/* creates menu of categories */
	createListMenu(&RutubeCategories, _T("VIDEO_CATEGORIES"), thumbnail_rutube, NULL, pParent,
		interfaceListMenuIconThumbnail,  rutube_enterCategoriesMenu, rutube_exitMenu, SET_NUMBER(1));

/* creates menu of subcategories */
	createListMenu(&RutubeSubCategories, _T("TITLE"), thumbnail_rutube, NULL, (interfaceMenu_t*)&RutubeCategories,
		interfaceListMenuIconThumbnail,  interface_menuActionShowMenu, rutube_exitMenu, SET_NUMBER(1));

/* creates menu of movies */
	createListMenu(&MoviesMenu, _T("TITLE"), thumbnail_rutube, rutube_icons, (interfaceMenu_t*)&RutubeCategories,
		interfaceListMenuIconThumbnail, interface_menuActionShowMenu, rutube_exitMenu, SET_NUMBER(1));

	interface_setCustomKeysCallback((interfaceMenu_t*)&MoviesMenu, movie_infoCallback);
}

static int rutube_enterCategoriesMenu( interfaceMenu_t* pMenu, void *pArg )
{
	int url_index;
	time_t current_time;
	double time_after_start;
	char *str;
	static char rt_filename[RUTUBE_FILENAME_LENGTH] = { 0 };

/* get current time and count difference in seconds between start_time and current_time */
	time(&current_time);
	if(start_time > 0)
		time_after_start = difftime(current_time, start_time);

/* download xml from rutube, if it's the first entry into menu or if the last download was more than twenty-four hours ago */
	if(RutubeCategories.baseMenu.menuEntryCount <= 1 || time_after_start > SECONDS_PER_DAY)
	{
	/* memorize time of the first entry */
		time(&start_time);

		interface_clearMenuEntries((interfaceMenu_t *)&RutubeCategories);

		str = _T("LOADING");
		interface_addMenuEntryDisabled((interfaceMenu_t *)&RutubeCategories, str, thumbnail_loading);
		eprintf("%s: rutube url %s\n", __FUNCTION__, rutube_url);
		eprintf("%s: loading rutube xml file...\n", __FUNCTION__);

		snprintf(rt_filename, sizeof(rt_filename), RUTUBE_XML);

		url_index = downloader_find(rutube_url); /* search url in pool */
		if ( url_index < 0 )
		{
			if ( downloader_push(rutube_url, rt_filename, sizeof(rt_filename), RUTUBE_FILESIZE_MAX, rutube_playlist_parser, (void*)rt_filename ) < 0 )
			{
				eprintf("%s: Can't start download: pool is full!\n", __FUNCTION__);
				interface_showMessageBox(_T("ERR_DEFAULT_STREAM"), thumbnail_error, 3000);
				return 1;
			}
		}
//		rutube_playlist_parser( 0, (void*)"rutube_export.txt" );
	}
	return 0;
}

static int rutube_enterSubCategoriesMenu( interfaceMenu_t* pMenu, void *pArg )
{
	eprintf("%s: enter subcategories menu\n", __FUNCTION__);
/* set menu name, when enter from RutubeCategories menu */
	if(interfaceInfo.currentMenu == (interfaceMenu_t*)&RutubeCategories)
	{
		int selectedIndex = interfaceInfo.currentMenu->selectedItem;
		char* name = interfaceInfo.currentMenu->menuEntry[selectedIndex].info;

		interface_setMenuName((interfaceMenu_t*)&RutubeSubCategories, name, MENU_ENTRY_INFO_LENGTH);
	}

	interfaceInfo.currentMenu = (interfaceMenu_t*)&RutubeSubCategories;
	interfaceInfo.currentMenu->selectedItem = 0; /* set focus on the first menu element */
	return 0;
}

static int rutube_fillMoviesMenu( interfaceMenu_t* pMenu, void *pArg  )
{
	eprintf("%s: start fill movies menu\n", __FUNCTION__);

	int selectedIndex;
	char* name;
/* clears last list menu */
	interface_clearMenuEntries( (interfaceMenu_t*)&MoviesMenu );
	rutube_asset_t *asset, *check_asset;
/* fills list menu with new elements */
	selectedIndex = interfaceInfo.currentMenu->selectedItem;

	if(selectedIndex >=0)
	{
		asset = (rutube_asset_t *)interfaceInfo.currentMenu->menuEntry[selectedIndex].pArg;
		name = interfaceInfo.currentMenu->menuEntry[selectedIndex].info;
		interface_setMenuName((interfaceMenu_t*)&MoviesMenu, name, MENU_ENTRY_INFO_LENGTH);

		if(interfaceInfo.currentMenu == (interfaceMenu_t*)&RutubeSubCategories)
		{
	/* an element, which allows to return to RutubeSubCategoriesMenu */
			interface_addMenuEntry((interfaceMenu_t*)&MoviesMenu, "..", rutube_enterSubCategoriesMenu, NULL, thumbnail_folder);
			MoviesMenu.baseMenu.menuEntry[0].image = NULL;


	/* if selected element is not the first element of subcategories menu */
			if(selectedIndex != interfaceInfo.currentMenu->menuEntryCount-1)
			{
				check_asset = (rutube_asset_t *)interfaceInfo.currentMenu->menuEntry[selectedIndex+1].pArg;
		/* comparison between current asset and the fisrt asset of next subcategory */
				while(asset->url != check_asset->url)
				{
					interface_addMenuEntry( (interfaceMenu_t*)&MoviesMenu, asset->title, rutube_playMovie, (void*)asset, thumbnail_internet ) - 1;
					helperSafeStrCpy(&MoviesMenu.baseMenu.menuEntry[MoviesMenu.baseMenu.menuEntryCount-1].image, asset->thumbnail);
					asset = (rutube_asset_t *)asset->nextInCategory;
				}
			}
	/* if selected element is the first element of subcategories menu */
			else
			{
		/* add elements until the end of assets */
				while(asset != NULL)
				{
					interface_addMenuEntry( (interfaceMenu_t*)&MoviesMenu, asset->title, rutube_playMovie, (void*)asset, thumbnail_internet ) - 1;
					helperSafeStrCpy(&MoviesMenu.baseMenu.menuEntry[MoviesMenu.baseMenu.menuEntryCount-1].image, asset->thumbnail);
					asset = (rutube_asset_t *)asset->nextInCategory;
				}
			}
		}
		else
		{
			while(asset != NULL)
			{
				interface_addMenuEntry( (interfaceMenu_t*)&MoviesMenu, asset->title, rutube_playMovie, (void*)asset, thumbnail_internet ) - 1;
				helperSafeStrCpy(&MoviesMenu.baseMenu.menuEntry[MoviesMenu.baseMenu.menuEntryCount-1].image, asset->thumbnail);
				asset = (rutube_asset_t *)asset->nextInCategory;
			}
		}
	}
	interfaceInfo.currentMenu = (interfaceMenu_t*)&MoviesMenu;
	interfaceInfo.currentMenu->selectedItem = 0; /* set focus on the first menu element */
	eprintf("%s: filling movies menu is done\n", __FUNCTION__);
	return 0;
}

char *rutube_getCurrentURL()
{
	static char url[MENU_ENTRY_INFO_LENGTH];
	rutube_asset_t *asset;
	int selectedIndex = MoviesMenu.baseMenu.selectedItem;
	asset = (rutube_asset_t *)MoviesMenu.baseMenu.menuEntry[selectedIndex].pArg;

	if( asset == NULL)
		url[0] = 0;
	else
		snprintf(url, sizeof(url), asset->url);

	return url;
}

int rutube_startNextChannel(int direction, void* pArg)
{
	eprintf("%s[%d]: next\n", __FUNCTION__, __LINE__);
	int selectedIndex = MoviesMenu.baseMenu.selectedItem;
	int inSubmenu = 0; /* if movie menu is from subcategories, is 0; if movie menu is from categories menu, is 1 */
	int indexChange = (direction?-1:1);

/* if it's another MoviesMenu or if another movie is selected */
	if( MoviesMenu.baseMenu.pParentMenu->selectedItem == selectedCategory &&
		strcmp(MoviesMenu.baseMenu.menuEntry[selectedIndex].info, playingMovie->title) == 0 )
	{
		eprintf("%s[%d]: you are in playing movie's menu\n", __FUNCTION__, __LINE__);
		if( strcmp(MoviesMenu.baseMenu.menuEntry[0].info, "..") == 0 )
			inSubmenu = 1;

	/* if this movie is the first in the menu list and 'prev' button is chosen */
		if(selectedIndex == inSubmenu && indexChange == -1)
			return 1;

	/* if this movie is the last in the menu list and 'next' button is chosen */
		if(selectedIndex == MoviesMenu.baseMenu.menuEntryCount-1 && indexChange == 1)
			return 1;

		playingMovie = (rutube_asset_t *)MoviesMenu.baseMenu.menuEntry[selectedIndex + indexChange].pArg;
		MoviesMenu.baseMenu.selectedItem = selectedIndex + indexChange;/* do prev/next menu element be selected */
	}
	else
		playingMovie = playingMovie->nextInCategory;

	if( playingMovie == NULL)
		return 1;

	appControlInfo.playbackInfo.playlistMode = playlistModeRutube;
	appControlInfo.playbackInfo.streamSource = streamSourceRutube;
	media_playURL(screenMain, playingMovie->url, playingMovie->title, playingMovie->thumbnail);
	return 0;
}

int  rutube_setChannel(int channel, void* pArg)
{
	if( channel < 1 || channel > MoviesMenu.baseMenu.menuEntryCount )
		return 1;

	int n = 1; /* if movie menu is from categories menu */

	if( strcmp(MoviesMenu.baseMenu.menuEntry[0].info, "..") == 0 )
		n = 0; /* if movie menu is from subcategories menu (the zero element is way back to subcategories menu) */

	rutube_asset_t *asset = (rutube_asset_t *)MoviesMenu.baseMenu.menuEntry[channel-n].pArg;
	if(asset == NULL)
		return 1;

	appControlInfo.playbackInfo.playlistMode = playlistModeRutube;
	appControlInfo.playbackInfo.streamSource = streamSourceRutube;
	media_playURL(screenMain, asset->url, asset->title, asset->thumbnail);
	return 0;
}

static int rutube_playMovie( interfaceMenu_t* pMenu, void *pArg  )
{
	int index = GET_NUMBER(pArg);
	rutube_asset_t *asset;
	int selectedIndex = MoviesMenu.baseMenu.selectedItem;
/* memorize index of selected category */
	selectedCategory = MoviesMenu.baseMenu.pParentMenu->selectedItem;

	if(selectedIndex >=0)
	{
		asset = (rutube_asset_t *)MoviesMenu.baseMenu.menuEntry[selectedIndex].pArg;
		appControlInfo.playbackInfo.playlistMode = playlistModeRutube;
		appControlInfo.playbackInfo.streamSource = streamSourceRutube;

	/* memorize link to this movie */
		playingMovie = asset;

		media_playURL(screenMain, asset->url, asset->title, asset->thumbnail);
	}
	return 1;
}

static int movie_infoCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if (cmd->command == interfaceCommandSearch)
		return rutube_videoSearch(pMenu, NULL);

	int selectedIndex = MoviesMenu.baseMenu.selectedItem;
	if (selectedIndex < 0)
		return 1;

	if (cmd->command == interfaceCommandGreen ||
	    cmd->command == interfaceCommandInfo)
	{
		rutube_asset_t *asset = (rutube_asset_t *)MoviesMenu.baseMenu.menuEntry[selectedIndex].pArg;
		interface_showMessageBox(asset != NULL ? asset->description : _T("ERR_NO_DATA"), 0, 0);
		return 0;
	}
	return 1;
}

static int rutube_videoSearch(interfaceMenu_t *pMenu, void* pArg)
{
	eprintf("%s: user typing something...\n", __FUNCTION__);
	interface_getText(pMenu, _T("ENTER_TITLE"), "\\w+", rutube_startVideoSearch, rutube_getLastSearch, inputModeABC, pArg);
	return 0;
}

static int rutube_startVideoSearch(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	eprintf("%s: start video search...\n", __FUNCTION__);
	rutube_asset_t *asset;
	bool found = false;
	char *keywords[KEYWORDS_MAX_NUMBER], *result_str;

	if( value == NULL )
	{
		eprintf("%s: exit\n", __FUNCTION__);
		return 1;
	}

	if(MoviesMenu.baseMenu.menuEntryCount > 0)
		interface_clearMenuEntries( (interfaceMenu_t*)&MoviesMenu );

/* split string into tokens, which will be key words */
	keywords[0] = strtok(value, " ,.-=+_?*!@/^:;#");
	//printf("[%d]: keyword[0] is %s\n", __LINE__, keywords[0]);
	for(int i = 1; i < KEYWORDS_MAX_NUMBER; i++)
	{
		keywords[i] = strtok(NULL, " ,.-=+_?*!@/^:;#");
		//printf("[%d]: keyword[%d] is %s\n", __LINE__, i, keywords[i]);
	}

	interface_setMenuName((interfaceMenu_t*)&MoviesMenu, _T("SEARCH_RESULT"), MENU_ENTRY_INFO_LENGTH);
/* finding movies... */
	int i = 0;
	while(keywords[i] != NULL && *keywords[i])
	{
		for( int j = 1; j < RutubeCategories.baseMenu.menuEntryCount; j++)
		{
			asset = (rutube_asset_t*)RutubeCategories.baseMenu.menuEntry[j].pArg;
		/* moving along assets in current category */
			while(asset != NULL)
			{
				result_str = strcasestr(asset->title, keywords[i]); /* unfortunately, the search function is case sensitive in almost all cases, except English language */
			/*...and filling MoviesMenu with found movies if exists */
				if(result_str != NULL)
				{
				/* test for existing of the same element in menu */
					if(MoviesMenu.baseMenu.menuEntryCount != 0)
					{
						int k = 0;
						bool find = false;
						rutube_asset_t *tmp_asset;
						while(find == false && k != MoviesMenu.baseMenu.menuEntryCount-1)/* search an element with the same title in MoviesMenu */
						{
							tmp_asset = (rutube_asset_t *)MoviesMenu.baseMenu.menuEntry[k].pArg;
							if(tmp_asset != NULL)
							{
								if(strcmp(asset->title, tmp_asset->title) == 0)
								{
									find = true;
								}
							}
							k++;
						}
						if(find == false)/* if the element wasn't found, add it to menu */
						{
							interface_addMenuEntry( (interfaceMenu_t*)&MoviesMenu, asset->title, rutube_playMovie, (void*)asset, thumbnail_internet ) - 1;
							strcat(asset->thumbnail, "\0");
							helperSafeStrCpy(&MoviesMenu.baseMenu.menuEntry[MoviesMenu.baseMenu.menuEntryCount-1].image, asset->thumbnail);
							//printf("[%d]: added an element %s\n", __LINE__,  asset->title);
						}
					}
					else
					{
						interface_addMenuEntry( (interfaceMenu_t*)&MoviesMenu, asset->title, rutube_playMovie, (void*)asset, thumbnail_internet ) - 1;
						strcat(asset->thumbnail, "\0");
						helperSafeStrCpy(&MoviesMenu.baseMenu.menuEntry[MoviesMenu.baseMenu.menuEntryCount-1].image, asset->thumbnail);
						//printf("[%d]: added an element %s\n", __LINE__, asset->title);
					}
				}
				asset = (rutube_asset_t *)asset->nextInCategory;/* go to the next asset element */
			}
		}
		i++;
	}

	if(MoviesMenu.baseMenu.menuEntryCount == 0)
	{
		interface_addMenuEntryDisabled((interfaceMenu_t*)&MoviesMenu, _T("NO_FILES"), thumbnail_info);
		MoviesMenu.baseMenu.menuEntry[0].image = NULL;
		eprintf("%s: no results\n", __FUNCTION__);
	}
	else
		eprintf("%s: done\n", __FUNCTION__);

	interfaceInfo.currentMenu = (interfaceMenu_t*)&MoviesMenu;
	interface_displayMenu(1);/* quick show menu */
	return 0;
}

static char *rutube_getLastSearch(int index, void* pArg)
{
	char *example = "...";/* you may put here example string */
	return example;
}

void clean_list(rutube_asset_t *asset)
{
/* recursively clears linked list */
	if ( asset->nextInCategory!=NULL )
	{
		clean_list((rutube_asset_t *) asset->nextInCategory);
	}

	FREE(asset->title); //printf("%s[%d]: free title\n", __FILE__, __LINE__);
	FREE(asset->url); //printf("%s[%d]: free url\n", __FILE__, __LINE__);
	FREE(asset->description); //printf("%s[%d]: free description\n", __FILE__, __LINE__);
	FREE(asset->thumbnail); //printf("%s[%d]: free image\n", __FILE__, __LINE__);
	dfree(asset); //printf("%s[%d]: free asset\n", __FILE__, __LINE__);
}

static void rutube_freeAssets()
{
	rutube_asset_t *tmp;
	dprintf("%s: start function...\n", __FUNCTION__);
/* the first element of RutubeCategories menu is search */
	for(int i = 1; i < RutubeCategories.baseMenu.menuEntryCount; i++)
	{
		tmp = (rutube_asset_t *)RutubeCategories.baseMenu.menuEntry[i].pArg;
		FREE(RutubeCategories.baseMenu.menuEntry[i].image);
		clean_list(tmp);
	}
	for(int i = 1; i < MoviesMenu.baseMenu.menuEntryCount; i++)
		FREE(MoviesMenu.baseMenu.menuEntry[i].image);
	for(int i = 1; i < RutubeSubCategories.baseMenu.menuEntryCount; i++)
		FREE(RutubeSubCategories.baseMenu.menuEntry[i].image);

	dprintf("%s: done\n", __FUNCTION__);
}

void rutube_cleanupMenu()
{
	rutube_freeAssets();
}

static int rutube_exitMenu( interfaceMenu_t* pMenu, void *pArg )
{
	eprintf("%s: exit\n", __FUNCTION__);
	return 0;
}

#endif // ENABLE_RUTUBE
