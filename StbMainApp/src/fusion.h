#if !defined(__FUSION_H)
#define __FUSION_H

//
// Created:  2013/07/30
// File name:  fusion.h
//
//////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2013 Elecard Devices
// All rights are reserved.  Reproduction in whole or in part is 
//  prohibited without the written consent of the copyright owner.
//
// Elecard Devices reserves the right to make changes without
// notice at any time. Elecard Ltd. makes no warranty, expressed,
// implied or statutory, including but not limited to any implied
// warranty of merchantability of fitness for any particular purpose,
// or that the use will not infringe any third party patent, copyright
// or trademark.
//
// Elecard Devices must not be liable for any loss or damage arising
// from its use.
//
//////////////////////////////////////////////////////////////////////////
//
// Authors: Victoria Peshkova <Victoria.Peshkova@elecard.ru>
// 
// Purpose: Fusion header.
//
//////////////////////////////////////////////////////////////////////////

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "defines.h"
#include <curl/curl.h>
#include <common.h>
#include <pthread.h>

#include "debug.h"
#include "StbMainApp.h"
#include "media.h"
#include "gfx.h"
#include "interface.h"
#include <cJSON.h>
#include <string.h>


/***********************************************
* LOCAL MACROS                                 *
************************************************/
#ifndef STBxx
#define FUSION_CFGDIR 		"/opt/elecard/share/fusion/"
#else
#define FUSION_CFGDIR 		"/usr/local/share/fusion/"
#endif

#define FUSION_HWCONFIG          CONFIG_DIR"/fusion.hwconf"

#define FUSION_MAX_URL_LEN 512

#define E_RETURN(msg) \
		eprintf ("%s: ERROR! "msg"\n", __FUNCTION__); \
		return -1;
/*
#define HLS_STREAM_SIZE	(1024*1024)
#define HLS_PLAYLIST_SIZE	(128*1024)
#define M3U8_VER_SUPPORTED	(2)

#define MAX_URI				(512)
#define M3U8_IV_LEN			(16)

#define EXTM3U  				"#EXTM3U"
#define EXTINF  				"#EXTINF"
#define EXT_X_TARGETDURATION	"#EXT-X-TARGETDURATION"
#define EXT_X_MEDIA_SEQUENCE	"#EXT-X-MEDIA-SEQUENCE"
#define EXT_X_KEY				"#EXT-X-KEY"
#define EXT_X_PROGRAM_DATE_TIME	"#EXT-X-PROGRAM-DATE-TIME"
#define EXT_X_ALLOW_CACHE		"#EXT-X-ALLOW-CACHE"
#define EXT_X_STREAM_INF		"#EXT-X-STREAM-INF"
#define EXT_X_DISCONTINUITY		"#EXT-X-DISCONTINUITY"
#define EXT_X_VERSION			"#EXT-X-VERSION"
#define EXT_X_ENDLIST			"#EXT-X-ENDLIST"

#define VM_YES	(0)
#define VM_NO	(1)

#define MIN_POSITION_DIFF (11)

#ifndef STBxx
#define HD_WIDTH 	(1920 + 40)
#define HD_HEIGHT 	(1080 + 20)
#define HD_OFF_X	(48)
#define HD_OFF_Y	(23)
#endif

#define VM_START_THREAD(handle, threadFunc, param) {\
	if (handle){ \
		pthread_cancel(handle);\
		pthread_join(handle, NULL);\
	}\
	pthread_create(&handle, NULL, threadFunc, (void*)param);\
}

#define VM_WAIT_THREAD(handle)\
	if (handle){ \
		pthread_cancel(handle);	\
		pthread_join(handle, NULL);\
	}

#define VM_START_DETACHED_THREAD(threadFunc, param) {\
	pthread_t handle; \
	\
	pthread_create(&handle, NULL, threadFunc, (void*)param);\
	pthread_detach(handle);\
}
*/
#define FUSION_TEST   1

#define FUSION_STREAM_SIZE    (1024*1024)
#define FUSION_URL_LEN        (512)
#define FUSION_VERSION_LEN    (16)
//#define FUSION_TIMESTAMP_LEN  (32)
#define FUSION_ERR_LEN        (128)
#define FUSION_CMD_LEN        (128)
#define FUSION_KEY_LEN        (33)
#define FUSION_YESNO_LEN      (3)
#define FUSION_SERVER_LEN     FUSION_URL_LEN

#define FUSION_UPDATE_HOUR    2
#define FUSION_UPDATE_MIN     0
#define FUSION_UPDATE_SEC     0

#define FUSION_FILECHECK_TIMEOUT_MS  (10 * 1000)
#define FUSION_FLASHCHECK_TIMEOUT_MS (10 * 1000)
#define FUSION_WAIT_READY_MS         (5  * 1000)

#define FUSION_ERR_EARLY (-1)
#define FUSION_ERR_LATE  (-2)

#define FUSION_DEFAULT_SERVER_PATH   "http://public.tv/clients/index.php"
#define FUSION_TEST_USERKEY          "3a194c01a085e01730cfe5d72976a14f"

#define FUSION_DEFAULT_DUMP_SETTING  "YES"
#define FUSION_DUMP_PATH             "/mnt/sda1/fusion/"
#define USB_ROOT "/mnt/sda1/"
#define YES 1
#define NO  0

#define KILOBYTE 1024
#define MEGABYTE KILOBYTE*KILOBYTE
#define GIGABYTE KILOBYTE*MEGABYTE

#define SAFE_DELETE(x) if(x){free(x);(x)=NULL;}


#define FUSION_START_THREAD(handle, threadFunc, param) {\
	if (handle){ \
		pthread_cancel(handle);\
		pthread_join(handle, NULL);\
	}\
	pthread_create(&handle, NULL, threadFunc, (void*)param);\
}

#define FUSION_WAIT_THREAD(handle)\
	if (handle){ \
		pthread_cancel(handle);	\
		pthread_join(handle, NULL);\
	}

#define FUSION_START_DETACHED_THREAD(threadFunc, param) {\
	pthread_t handle; \
	\
	pthread_create(&handle, NULL, threadFunc, (void*)param);\
	pthread_detach(handle);\
}

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct {
	//char timestamp [FUSION_TIMESTAMP_LEN];
	struct tm timestamp;
	char url[FUSION_MAX_URL_LEN];
	char filename[PATH_MAX];
	char readyToPlay;
} fusion_file_t;

typedef struct {
	//char showTimestamp [FUSION_TIMESTAMP_LEN];
	fusion_file_t * files;
	unsigned int fileCount;
} fusion_playlist_t;

typedef struct {
	char version[FUSION_VERSION_LEN];
	//char timestamp[FUSION_TIMESTAMP_LEN];
	char errorMessage [FUSION_ERR_LEN];
	char cmd [FUSION_CMD_LEN];
	fusion_playlist_t playlist;
} fusion_response_t;

typedef struct {
	char server[FUSION_SERVER_LEN];
	char userKey[FUSION_KEY_LEN];
	unsigned char dumpSetting;
	fusion_response_t response;
	
	pthread_mutex_t mutexUpdatePlaylist;
	pthread_mutex_t mutexCheckFlash;
	pthread_mutex_t mutexDownloadFile;
	pthread_t threadUpdatePlaylist;
	pthread_t threadManagePlayback;
	pthread_t threadCheckFlash;
	pthread_t threadDownloadFiles;
	
	char stopRequested;
	FILE * currentDumpFile;
	char currentDumpPath[PATH_MAX];
	char filesDownloaded;
	
	unsigned long long flashTotalSize;
	unsigned long long flashUsedSize;
	unsigned long long flashAvailSize;
	
} fusion_object_t;


/*
//--------- m3u8 struct ------------------
typedef enum __m3u8_enc_t{
	NONE = 0,
	AES_128 = 1
} m3u8_enc_t;

typedef enum __m3u8_type_t{
	TYPE_TS = 0,
	TYPE_M3U8 = 1
} m3u8_type_t;

typedef struct __m3u8_playlist_t m3u8_playlist_t;

typedef struct __m3u8_entry_t {
	
	char * 	title;
	int 	titlelen;
	int 	duration;
	
	char * 		url;
	int 		urlLen;
	m3u8_type_t urlType;	// ts stream or nested m3u8 file
	
	int 	bandWidth;
	int 	programId;
	int 	resolutionX;
	int 	resolutionY;
	char * 	codecs;
	int 	codeclen;
	
	int 	keyIndex;		// index of key in m3u8_key_t* array
	
	m3u8_playlist_t * innerM3u8;
	int 	playingNow;	
	
} m3u8_entry_t;

typedef struct __m3u8_key_t {
	//A new EXT-X-KEY supersedes any prior EXT-X-KEY.
	m3u8_enc_t		enc_method;
	char  			enc_uri[MAX_URI];
	unsigned int	enc_iv[M3U8_IV_LEN];
	// unsigned char key[16];	// todo: this key is inside a file downloaded via enc_uri
	
	// todo : If the EXT-X-KEY tag does not have the IV attribute, implementations
	// MUST use the sequence number of the media file as the IV. The big-endian binary
	//representation of the sequence number SHALL be placed in a 16-octet
	//buffer and padded (on the left) with zeros.
} m3u8_key_t;

struct __m3u8_playlist_t {
	char 			path[PATH_MAX];
	int 			targetDuration;		// equal to or greater than the EXTINF value
	int 			allDuration;
	int 			mediaSeq;
	
	m3u8_key_t * 	keys;
	int 			keyCount;
	
	int datetime;  // todo
	
	unsigned char	sliding;
	unsigned char 	allow_cache;
	int				version;
	
	// todo : 	DISCONTINUITY
	
	int 	cur_duration;
	
	m3u8_entry_t *	entries;
	int 			entryCount;	
	char 			rootPath[PATH_MAX];
};
//--------- end m3u8 struct ----------------------

//--------- vm specific structs ------------------
typedef void (*vm_xxx_callback_t)(void);

typedef struct __vm_contentRow_t {	
	char 	title[VM_MAX_ATTR_LEN];
	char 	url[VM_MAX_URL_LEN];
	char  	style[16];	
} vm_contentRow_t;

typedef struct __vm_menuItem_t {
	char 				title[VM_MAX_ATTR_LEN];	
	unsigned char 		authReqiured;
	
	char 				onSelect[VM_MAX_ATTR_LEN];	
	char 				selectTitle[VM_MAX_ATTR_LEN];
	char 				selectUrl[VM_MAX_URL_LEN];	
	
	int 			 	nRows;	
	vm_contentRow_t* 	contentRows;		
	char 			 	externalModule[PATH_MAX];	
}vm_menuItem_t;

typedef struct __vm_menu_button_t {
	char 			title[VM_MAX_ATTR_LEN];
	char 			url[VM_MAX_URL_LEN];	
	char 			logo[PATH_MAX];
	unsigned char 	authReqiured;
	unsigned char 	isDefault;	
	
	vm_menuItem_t * items;
	int 			nItems;
	int 			maxItemTitle;
}vm_menu_button_t;

typedef struct __vm_menu_t {
	vm_menu_button_t * 	buttons;
	int 				nButtons;
	int 				cursorPos;  // for 2nd level
	
	int *				pButtonWidth;
	int *				pLeftIndent;
	int *				pLeftCoord;
	int *				pRightCoord;
	int *				pIconWidth;
} vm_menu_t;

typedef enum __vm_dlg_t
{
	VM_DLG_NONE	= 0,
	VM_DLG_INFO,
	VM_DLG_ATTENTION,
	VM_DLG_QUESTION,
	VM_DLG_ERROR,
	VM_DLG_RATING,
	VM_DLG_ABOUT,
	VM_DLG_RUB,
	VM_DLG_PARENT,
	VM_DLG_ACCOUNT,
	//VM_DLG_FILEINFO,	
	//VM_DLG_DOWNLOAD,
	
	VM_DLG_COUNT	
} vm_dlg_t;

typedef enum __vm_action_t
{
	VM_NONE,
	VM_BUY,
	VM_RENT,
	VM_WATCH,
	VM_TRAILER,
	VM_INFO,
	VM_RATE,
	VM_FAVOR_ADD,
	VM_FAVOR_REMOVE,
	VM_FAVOR_PROCESS,
	VM_SEASON_CHANGE,
	VM_SHOW_SEASON,
	VM_SEASON_INDEX,
	
	// todo
	VM_ACTIONS
} vm_action_t;

typedef enum __vm_wnd_t
{
	VM_WND_SPLASH = 0,
	VM_WND_HOME,
	VM_WND_CARO,
	VM_WND_FILM,
	VM_WND_GENRES,
	VM_WND_AUTH,
	VM_WND_AUTH_ERROR,
	//VM_WND_ACCOUNT,
	VM_WND_PARENT,
	VM_WND_EXT,
	
	VM_WND_SEARCH,
	VM_WND_VIDEO,
	VM_WND_BUY,
	VM_WND_RENT,
	
	VM_WND_DLG,
	VM_WND_SLIDESHOW,
	
	VM_WND_COUNT	
} vm_wnd_t;

typedef struct __vm_genre_t{
	int id;
	char name[VM_MAX_ATTR_LEN];
	int sortOrder;
} vm_genre_t;

typedef enum __vm_content_type_t
{
	VM_CONTENT_FILM    = 1,
	VM_CONTENT_SERIAL  = 2,	//
	VM_CONTENT_SEASON  = 3,
	VM_CONTENT_EPISODE  = 4,
	VM_CONTENT_LONGFILM = 5,
	VM_CONTENT_TYPES
}vm_content_type_t;

typedef enum __vm_tariff_type_t
{
	VM_RENT_SD 		= 1,
	VM_RENT_HD 		= 2,
	VM_PURCHASE_SD 	= 3,
	VM_PURCHASE_HD 	= 4,
	VM_TARIFFS,
}vm_tariff_type_t;

typedef struct __vm_tariff_t
{
	vm_tariff_type_t typeId;
	int price;
}vm_tariff_t;

typedef enum __vm_acc_state_t
{
	VM_ACC_INACTIVE = 0,
	VM_ACC_ACTIVE,
	VM_ACC_BLOCKED,
	VM_ACC_REMOVED	
}vm_acc_state_t;

typedef struct __vm_provodka_t
{
	int id;
	long sum;						// в копейках
	char moment[64];				// дата
	char entryTypeName[PATH_MAX];	// наименование операции
	char comment[PATH_MAX];			
}vm_provodka_t;

typedef struct __vm_acc_t
{
	char accountNumber[64];
	vm_acc_state_t accountState;	
	int accountSum;
	vm_provodka_t * provodki;
	int count;
} vm_acc_t;

typedef enum __vm_parent_rate_t
{
	VM_NR	= 0,
	VM_G	= 1,
	VM_PG	= 2,
	VM_PG13	= 3,
	VM_R	= 4,
	VM_NC17	= 5,
	VM_XXX	= 100,
	VM_ALL  = 200
}vm_parent_rate_t;

typedef enum __vm_status_t
{
	VM_DEL = 0,
	VM_SET = 1,
	VM_UNCHANGE = 2
} vm_status_t;

typedef struct __vm_content_t
{
	int 			id;
	char 			name[VM_MAX_ATTR_LEN];
	unsigned char 	wished;
	unsigned char 	purchased;
		
	unsigned short 	year;
	int 			rating;
	char 			logo[VM_MAX_ATTR_LEN];
	vm_content_type_t contentType; // not documented
	
} vm_content_t;

typedef struct __vm_play_button_t
{
	char 			text[64];
	unsigned char	inactive;
	unsigned char	small;
	int				isInfo;
	int				isRating;
	int				audioIndex;
	int				subsIndex;
} vm_play_button_t;

typedef struct __vm_cursor_t{
	int 	count;
	int 	first;
	int 	select;
	int 	visible;
	int 	fixed;
	
	int 	lines;	// multi rows
	int 	tail; 
} vm_cursor_t;

typedef struct __vm_desc_button_t
{
	char 			text[64];
	//char 			price[16];
	int 			price;
	unsigned char 	hd;
	vm_action_t		action;
	unsigned char	inactive;
} vm_desc_button_t;

typedef struct __vm_film_desc_t
{
	// compulsory
	int 	id;
	char 	name[VM_MAX_ATTR_LEN];
	int 	wished;
	int 	purchased;
	unsigned char 	inProcessAdd;
	unsigned char 	inProcessDel;
	
	vm_content_type_t contentType;
	
	// auxiliary
	char 	nameOrig[VM_MAX_ATTR_LEN];
	char 	description[MAX_STRLEN];
	int 	year;
	int 	duration;
	char 	scenario[VM_MAX_ATTR_LEN];
	char 	directors[VM_MAX_ATTR_LEN];
	char 	actors[VM_MAX_ATTR_LEN];
	char 	countries[VM_MAX_ATTR_LEN];
	char 	genres[VM_MAX_ATTR_LEN];
	vm_parent_rate_t parentRatingId;
	float 	rating;
	float 	myRating;
	float 	newRating;
	char 	audio[VM_MAX_ATTR_LEN];
	char 	tags[VM_MAX_ATTR_LEN];	
	
	// todo : not so beauty
	char 	subs[VM_MAX_SUBS][VM_MAX_ATTR_LEN];
	char 	logos[VM_MAX_ATTR_LEN][VM_MAX_LOGOS];
	char 	trailers[VM_MAX_URL_LEN][VM_MAX_TRAILERS];
	
	vm_tariff_t * 	tariffs;
	int 			nTariffs;
	unsigned int 	isRent;
	
	int 	parentId;
	
	int 	descIndents;
	int 	firstIndent;
		
	vm_cursor_t			buttonCursor;
	vm_desc_button_t 	pDescButton[VM_MAX_BUTTONS];
	
	vm_cursor_t		episodeCursor;	
	vm_content_t* 	episodes;	
	
	vm_cursor_t		seasonCursor;	
	vm_content_t* 	seasons;
	unsigned char	seasonType;
	
} vm_film_desc_t;

typedef struct __vm_carousel_t{
	vm_content_t * 	contents;
	vm_cursor_t		caroCursor;
	char  			title[VM_MAX_ATTR_LEN];
	int 			style;
}vm_carousel_t;

typedef struct __vm_dlg_strings_t
{
	char caption[VM_MAX_ATTR_LEN];
	char text[VM_MAX_ATTR_LEN];
	char lbutton[32];
	char rbutton[32];	
} vm_dlg_strings_t;

// todo
typedef struct __vm_dlg_table_t
{
	char leftText1[64];
	char leftText2[64];
	char rightText1[64];
	char rightText2[64];
	
} vm_dlg_table_t;

typedef struct __vm_cache_t
{
	int 			id;
	vm_status_t 	purchased;
	vm_status_t 	wished;
} vm_cache_t;

typedef enum __vm_kbd_t
{
	VM_KBD_SMALL_RU = 0,
	VM_KBD_LARGE_RU,
	VM_KBD_SMALL_EN,
	VM_KBD_LARGE_EN,
	
	VM_KBDS
} vm_kbd_t;

typedef struct __vm_notify_t
{
	char 	action[VM_MAX_ATTR_LEN];
	int 	id;
	int 	position;
	float 	rate;
	int 	doUpdate;
} vm_notify_t;

typedef struct __vm_search_t
{
	vm_menuItem_t *	pItem;
	char 			query[VM_QUERY_BUF];
	unsigned char 	stopCurrentSearch;
	unsigned char 	type;
} vm_search_t;

typedef enum __vm_rewind_t
{
	VM_REW_CLASSIC = 0,
	VM_REW_PARTS = 1,
	VM_REW_NONE = 2
}vm_rewind_t;

typedef struct __vm_videopos_t
{
	int id;
	int position;
}vm_videopos_t;

typedef struct __interfaceVidimaxMenu_t
{
	interfaceMenu_t baseMenu;		
	char 			certificate[VM_MAX_ATTR_LEN];	
	char 			accountCode[VM_CODE_LEN];	
	char 			mac[64];
	
	vm_acc_t		account;
	
	int 			history[VM_MAX_DEPTH];
	int 			curSnapshot;
	
	int 			filmStack[VM_MAX_FILM_STACK];
	int 			lastStackIndex;
	
	vm_cache_t		cache[VM_MAX_CACHE];
	int 			lastCacheIndex;
	
	vm_carousel_t 	caro[VM_MAX_CAROS];
	int 			nCaros;
	vm_cursor_t		rowCursor;			
	int 			cursorCol;
	int 			caroGot;
	
	vm_genre_t* 	genres;
	vm_cursor_t		genreCursor;		
			
	vm_film_desc_t 	film;
	int 			curContentId;
	unsigned char	gotButtons;	
	int				filmTabIndex;
	unsigned char	gotSeasons;
	unsigned char	gotEpisodes;
	
	vm_carousel_t 	recomRibbon;
	vm_carousel_t 	searchRibbon;
			
	vm_carousel_t* 	pRibbons;
	int 			ribbonGot;
	int 			currentRibbon;
	int 			homeTabIndex;
			
	vm_cursor_t		menuCursor;
	vm_menu_t		menuObject;			
	int				menuOpened;  // 0=none, 1= opened selected button	
	unsigned char 	menuGot;	
	
	vm_parent_rate_t 	parentalRating;
		
	vm_dlg_t 			dlgShown;
	int 				dlgTabIndex;	
	vm_dlg_strings_t	dlgStrings;
	vm_dlg_table_t		dlgTable;
	
	int					buyTabIndex;
	int					pinTabIndex;
	int					searchTabIndex;
	char 				enteredCode[VM_CODE_LEN];
	char 				enteredPin[VM_CODE_LEN];
	char 				enteredQuery[VM_QUERY_BUF];
	int 				freshQuery;
	int 				searchInProcess;
	char				blinker[2];
	vm_search_t *		pMainSearch;
	
	vm_kbd_t			kbd;
	int 				kbdRow;
	int 				kbdCol;
	int 				kbdShiftOn;
	int 				kbdLastRowFocus;
	
	char 				videoUrl[VM_MAX_URL_LEN];
	bool				isTrailer;
		
	vm_cursor_t			playCursor;
	unsigned char 		initMenu;
	unsigned char 		showLoading;
	unsigned char 		isBusy;
	unsigned char 		showPlayerMenu;
	unsigned char 		showPlayerPanel;
	unsigned char 		showPlayerState;
	vm_rewind_t			rewindType;
	int 				panelShowTime;
	int 				breakEverThread;
	int 				audioCount;
	int 				audioCurrent;
	
	vm_play_button_t *	playButtons;
	
	pthread_t			threadInit;
	pthread_t			threadSeek;
	pthread_t			threadRefresh;
	pthread_t			threadNotify;
	pthread_t			threadUpdate;
	pthread_t			threadHttpPlay;
	pthread_t			threadFastPlay;	
	pthread_t			threadSeekPartsHls;
		
	int					seekPartDirection;
	
	vm_notify_t			notify;
	vm_xxx_callback_t	xxxCallback;
	vm_xxx_callback_t	savedPosCallback;
	int					oldPlayState;
	stb810_playbackMode	oldPlaybackMode;
	
	vm_videopos_t		positionStack[VM_VIDEOPOS_STACK];
	int					lastVideoPosIndex;
	
	int 				seekWaitCycle;
	long				newApplePosition;
	int					startFromSaved;
	unsigned long 		savedLength;
	unsigned long 		savedPos;
	unsigned long 		paused;
	
	char 				serverApi[PATH_MAX];
	
	char 				serverContent[PATH_MAX];
	char 				serverTestHls[PATH_MAX];
	char 				trailerType[32];
	bool 				sendHlsRequest;
	bool 				sendHlsTest;
	
	int 				isHls;
	m3u8_playlist_t		m3u8;
	int					currentSlide;
	char				trickplayUrl[PATH_MAX];
	int					trickX;
	int					trickY;
	int					trickWidth;
	int					trickHeight;
	int					trickLock;
	
	int					sizeNotified;
} interfaceVidimaxMenu_t;

typedef enum __vm_arrowDir_t
{
	VM_ARROW_LEFT = 0,
	VM_ARROW_RIGHT,
	VM_ARROW_UP,
	VM_ARROW_DOWN,	
	VM_ARROW_DLG_UP,
	VM_ARROW_DLG_DOWN,
	
	VM_ARROW_DIR_COUNT	
} vm_arrowDir_t;

typedef enum __vm_color_t
{
	VM_COLOR_WHITE = 0xffffff,
	VM_COLOR_BLACK = 0x0,	
	VM_COLOR_GREY = 0x969AA2,	// 150 154 162
	VM_COLOR_RED = 0xff0000,	
	VM_COLOR_BLUE = 0x0099ff,  // 0 153 255
	VM_COLOR_SKY = 0x00CCff,  // 0 204 255
	VM_COLOR_COUNT
} vm_color_t;

typedef enum __vm_weight_t
{
	VM_BOLD 	= 0,
	VM_MEDIUM 	= 1,
	VM_REGULAR 	= 2
} vm_weight_t;

typedef struct __vm_fileinfo_line_t
{
	char label[128];
	char labelVal[32];
	char labelTag[16];		
	
	char auxVal[16];
	char auxTag[8];		
} vm_fileinfo_line_t;

typedef enum __vm_error_code_t
{
	E_ACCOUNT_NOT_FOUND = 1,
	E_ACCOUNT_ALREADY_ACTIVATED,
	E_INVALID_ACTIVATION_CODE,
	E_STB_ALREADY_ACTIVATED,
	E_INACTIVE_ACCOUNT,
	E_STB_NOT_FOUND,
	E_STB_BOUND_TO_ANOTHER_ACCOUNT,
	E_SERVICE_NOT_FOUND,
	E_PURCHASE_NOT_FOUND,
	E_MULTIPLE_STBS,
	E_CONTENT_NOT_FOUND,
	E_INCORRECT_PRICE,
	E_EMPTY_ACTIVATION_CODE,
	E_EMPTY_ACTIVATION_PIN,
	E_EMPTY_MAC,
	E_EMPTY_CERT,	
	E_NOT_ENOUGH_MONEY,
	E_MAC_NOT_MATCH_CERT,
	E_NODE_NOT_FOUND,
	E_PROFILE_NOT_FOUND,
	E_ADD_TO_WISH_LIST_ERROR,
	E_DEL_FROM_WISH_LIST_ERROR,
	E_PARAMETER_MISSED,
	E_ALREADY_PURCHASED,
	E_VIDEO_FILE_NOT_FOUND,
	E_TORRENT_FILE_NOT_FOUND,
	E_TORRENT_FILE_READ_ERROR,
	E_ANOTHER_STB_BOUND_TO_ACCOUNT,
	E_CONTENT_NOT_SERIES,
	E_CONTENT_NOT_SEASON_OR_EPISODE,
	E_SERIES_CANT_BE_PURCHASED,
	E_NO_SUCH_OFFER,
	E_PAYMENT_EXTERNAL_NOT_FOUND, 
	E_PAYMENT_EXTERNAL_NOT_PAYED,
	E_INVALID_QUEUE_STATE,
	E_ACCOUNT_ALREADY_IMPORTED,
	E_MULTIPLE_ACCOUNTS,
	E_DUPLICATE_PURCHASE,
	E_ACCOUNT_ALREADY_LOCKED,
	E_ACCOUNT_ALREADY_UNLOCKED,
	E_GENRE_NOT_FOUND,
	E_DEPRECATED_METHOD,
	E_INVALID_URL_TYPE,
	E_INVALID_RESOLUTION,
	E_GROUP_NOT_SUPPORTED = 45,
	
	E_EMPTY_REBILL_ANCHOR = 46,
	E_CURRENT_PROFILE_NOT_FOUND,
	E_PROFILE_AUTH_FAILED,
	E_MASTER_PROFILE_DELETE_ERROR,
	E_PROFILE_ALREADY_EXISTS,
	E_PERMISSION_DENIED,
	E_NOT_AUTHORIZED,
	E_BAD_CREDENTIALS,
	E_INVALID_OPERATION,
	E_INVALID_CONFIGURATION,
	E_EXTERNAL_OSS_CALL_EXCEPTION,
	E_PERSON_NOT_FOUND,
	E_INACTIVE_SERVICE,
	E_INCORRECT_SERVICE_SPEC_TYPE,
	E_INACTIVE_SERVICE_SPEC,

	VM_ERRORS_COUNT
} fusion_error_code_t;

#define VM_MAX_ECODE (99)

typedef enum __vm_error_aux_t
{
	E_UNSPECIFIED = 99,
	E_PARAM_MISSED = 98,
	E_TYPE_MISMATCH = 97,		
		
	VM_AUX_ERROR_LAST = 96
} vm_error_aux_t;

*/
#endif // __FUSION_H
