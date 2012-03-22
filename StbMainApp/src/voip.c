
/*
 voip.c

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

#include "voip.h"

#ifdef ENABLE_VOIP

#include "debug.h"
#include "app_info.h"
#include "m3u.h"
#include "l10n.h"
#include "playlist.h"
#include "menu_app.h"
#include "semaphore.h"
#include "client.h"

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define LAST_DIALED 0xffff
//#define VOIP_APP "/usr/local/bin/pjsua --use-pipe --dis-codec=iLBC --dis-codec=speex --dis-codec=G722 --dis-codec=GSM &"
//#define VOIP_APP "/pjsua-mips-unknown-linux-gnu"

#define LIST_ALL      (-1)
#define LIST_MISSED    (0)
#define LIST_ANSWERED  (1)
#define LIST_DIALED    (2)
#define LIST_ADDRESS   (3)

#define PARAM_SERVER   (1)
#define PARAM_LOGIN    (2)
#define PARAM_PASSWD   (3)
#define PARAM_REALM    (4)

#define ENTRY_INFO_SET(list, index) ((list << 16) | (index))
#define ENTRY_INFO_GET_LIST(info)     (info >> 16)
#define ENTRY_INFO_GET_INDEX(info)    (info & 0xFFFF)

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static char *voip_getLastDialedURI(int field, void* pArg);
static int voip_enterURI(interfaceMenu_t *pMenu, void* pArg);
static int voip_enterURICallback(interfaceMenu_t *pMenu, char *value, void* pArg);
static int voip_fillAccountMenu(interfaceMenu_t *pMenu, void *pArg);
static int voip_fillAddressBookMenu(interfaceMenu_t *pMenu, void *pArg);
static int voip_fillMissedCallsMenu(interfaceMenu_t *pMenu, void *pArg);
static int voip_fillAnsweredCallsMenu(interfaceMenu_t *pMenu, void *pArg);
static int voip_fillDialedNumbersMenu(interfaceMenu_t *pMenu, void *pArg);
static int voip_call_event(void* pArg);
static int voip_newAddressBookURI(interfaceMenu_t *pMenu, void* pArg);
static int voip_addNewAddressBookEntry(interfaceMenu_t *pMenu, char *uri, char *nickname);
static int voip_newURICallback(interfaceMenu_t *pMenu, char *value, void* pArg);
static int voip_clearList(interfaceMenu_t *pMenu, void* pArg);
static int voip_deleteEntryFromList( interfaceMenu_t *pMenu, void *pArg );
static int voip_getEntryInfo( void *pArg );
static int voip_showEntryInfo( interfaceMenu_t *pMenu, void *pArg );
static int voip_addEntryToList( interfaceMenu_t *pMenu, void* pArg );
static int voip_logoutFromServer(interfaceMenu_t *pMenu, void* pArg);
static int voip_reloginToServer(interfaceMenu_t *pMenu, void* pArg);
static char *voip_getBuddyURI(int field, void* pArg);
static char *voip_getBuddyName(int field, void* pArg);
static int voip_changeURI(interfaceMenu_t *pMenu, char *value, void* pArg);
static int voip_renameBuddy(interfaceMenu_t *pMenu, char *value, void* pArg);

static int voip_addressBookKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int voip_callsKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static int voip_menuKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
static char *voip_getParam(int field, void* pArg);
static int voip_changeParam(interfaceMenu_t *pMenu, char *value, void* pArg);
static int voip_toggleParam(interfaceMenu_t *pMenu, void* pArg);
static int voip_toggleEnabled(interfaceMenu_t *pMenu, void* pArg);

static int voip_sendLoginInfo();
static int voip_loginToServer();
static void *voip_readPipeThread(void *pArg);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static socketClient_t voip_socket;
static int        voip_loggingIn = 0;
static pthread_t  voip_threadPipe;
static time_t     voip_lastCallTime;
static interfaceListMenu_t AccountMenu;

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

interfaceListMenu_t VoIPMenu;
interfaceListMenu_t AddressBookMenu;
interfaceListMenu_t MissedCallsMenu;
interfaceListMenu_t AnsweredCallsMenu;
interfaceListMenu_t DialedNumbersMenu;

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

static int voip_socket_connected(socketClient_t *s)
{
	(void)s;
	// These can be changed from outside, so reload
	loadVoipSettings();

	// pjsua does this by itself now...
	/*voip_logoutFromServer(0,0);
	if( appControlInfo.voipInfo.server[0] != 0 )
		voip_sendLoginInfo();*/
	return 0;
}

static int voip_socket_disconnected(socketClient_t *s)
{
	(void)s;
	/* Clear states... */
	appControlInfo.voipInfo.connected = 0;
	appControlInfo.voipInfo.status = voipStatus_idle;
	voip_loggingIn = 0;
	return 0;
}

void voip_init()
{
	appControlInfo.voipInfo.active = 0;

	client_create(&voip_socket, VOIP_SOCKET, voip_socket_disconnected, voip_socket_connected, voip_socket_disconnected, NULL);

	/* logging in to server when perfoming socket connect */
	voip_setBuzzer();

	appControlInfo.voipInfo.active = 1;
	if( pthread_create(&voip_threadPipe, NULL, voip_readPipeThread, NULL) < 0)
	{
		appControlInfo.voipInfo.active = 0;
		return;
	}
	pthread_detach(voip_threadPipe);
	//
	//voip_loginToServer();
}

static void *voip_readPipeThread(void *pArg)
{
	char buffer[BUFFER_SIZE], *buf, *str, *pos;
	int bytes_left, bytes_total;
	dprintf("%s: in\n", __FUNCTION__);
	
	while( appControlInfo.voipInfo.active )
	{
		//dprintf("%s: read loop\n", __FUNCTION__);
		if( (bytes_total = client_read(&voip_socket, buffer, sizeof(buffer))) > 0)
		{
			bytes_left = bytes_total;
			while (bytes_left > 0)
			{
				buf = &buffer[bytes_total-bytes_left];
				dprintf("%s: recieved '%s' %d/%d\n", __FUNCTION__,buf, bytes_left, bytes_total);
				bytes_left -= strlen(buf)+1;
				switch( buf[0] )
				{
					case 'A':
						if( strncasecmp( buf, "Account deleted", 15 ) == 0)
						{
							appControlInfo.voipInfo.connected = 0;
							voip_loggingIn = 0;
							voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
							voip_fillAccountMenu(interfaceInfo.currentMenu, NULL);
						}
						break;
					case 'I':
						str = &buf[2];
						pos = index(str,'\n');
						if(pos!=NULL)
						{
							time(&voip_lastCallTime);
							*pos = 0;
							strcpy(appControlInfo.voipInfo.lastSip,str); //from
							saveAppSettings();
							str = &pos[1];
							appControlInfo.voipInfo.status = voipStatus_incoming;
							voip_call_event(NULL);
							voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
							// to (ignored)
						}
						break;
					case 'D': // Disconnect
						voip_hangup(interfaceInfo.currentMenu,NULL);
						break;
					case 'C':
						if(strncasecmp(buf,"Calling",7) == 0)
						{
							appControlInfo.voipInfo.status = voipStatus_dialing;
							voip_call_event(NULL);
							voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
						} else if(strncasecmp(buf,"Connecting",10) == 0)
						{
							appControlInfo.voipInfo.status = voipStatus_talking;
							voip_call_event(NULL);
							voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
						}
						break;
					case 'L'://Logged In/Logged out
						if( strncasecmp(buf,"Logged ",7) == 0)
						{
							appControlInfo.voipInfo.connected = buf[7] == 'I';// In/Out
							voip_loggingIn = 0;
							voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
							voip_fillAccountMenu(interfaceInfo.currentMenu, NULL);
						} else if( strncasecmp(buf,"Logging ",8) == 0)
						{
							appControlInfo.voipInfo.connected = 0;
							voip_loggingIn = 1;
							voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
							voip_fillAccountMenu(interfaceInfo.currentMenu, NULL);
						}
						break;
					case 'O':
						strcpy(appControlInfo.voipInfo.lastSip, &buf[1]);
						voip_addEntryToList(interfaceInfo.currentMenu, (void*)LIST_DIALED);
						break;
					case 'R': //Registration Failed
						if( strncasecmp( buf, "Registration Failed", 19) == 0 )
						{
							appControlInfo.voipInfo.connected = 0;
							voip_loggingIn = 0;
							voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
							voip_fillAccountMenu(interfaceInfo.currentMenu, NULL);
							interface_showMessageBox(_T("ERR_VOIP_REGISTER"), thumbnail_error, 3000);
						}
						break;
					case 'E':
						break;
					default: ; // ignore
				}
				interface_displayMenu(1);
			}
		} else
		{
			//eprintf("VoIP: Error reading NOTIFY pipe\n");
			/*voip_cleanup();
			break;*/
			usleep(1000000);
		}
	}
	dprintf("%s: done\n", __FUNCTION__);
	//close(voip_socket);
	return NULL;
}


void voip_cleanup()
{
	//if(appControlInfo.voipInfo.active)
	{
		interface_removeEvent(voip_call_event,NULL);
		//voip_hangup(interfaceInfo.currentMenu, NULL);
		appControlInfo.voipInfo.active = 0;
		appControlInfo.voipInfo.connected = 0;
		voip_loggingIn = 0;
		//client_write(&voip_socket, "q", 2);
		client_destroy(&voip_socket);
		/*
		int res;
		do
		{
			usleep(1000);
			res = system( "killall -0 pjsua" );
		} while ( WIFEXITED(res) == 1 && WEXITSTATUS(res) == 0 );*/
	}
}

void voip_buildMenu(interfaceMenu_t *pParent)
{
	int voip_icons[4] = { 0, 0, 0, 0 };

	createListMenu(&VoIPMenu, _T("VOIP"), thumbnail_voip, voip_icons, pParent,
		/* interfaceInfo.clientX, interfaceInfo.clientY,
		interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
		NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&VoIPMenu, voip_menuKeyCallback);

	createListMenu(&AccountMenu, _T("ACCOUNT"), 0, NULL, (interfaceMenu_t *)&VoIPMenu, interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	voip_icons[0] = statusbar_f1_delete;
	voip_icons[1] = statusbar_f2_info;
	voip_icons[2] = statusbar_f3_edit;
	voip_icons[3] = statusbar_f4_rename;
	createListMenu(&AddressBookMenu, _T("ADDRESS_BOOK"), thumbnail_address_book, voip_icons, (interfaceMenu_t *)&VoIPMenu,
	interfaceListMenuIconThumbnail, NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&AddressBookMenu, voip_addressBookKeyCallback);
	
	voip_icons[2] = statusbar_f3_abook;
	voip_icons[3] = 0;
	createListMenu(&MissedCallsMenu, _T("MISSED_CALLS"), thumbnail_missed_calls, voip_icons, (interfaceMenu_t *)&VoIPMenu,
	interfaceListMenuIconThumbnail, NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&MissedCallsMenu, voip_callsKeyCallback);
	
	createListMenu(&AnsweredCallsMenu, _T("ANSWERED_CALLS"), thumbnail_answered_calls, voip_icons, (interfaceMenu_t *)&VoIPMenu,
	interfaceListMenuIconThumbnail, NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&AnsweredCallsMenu, voip_callsKeyCallback);
	
	createListMenu(&DialedNumbersMenu, _T("DIALED_NUMBERS"), thumbnail_dialed_numbers, voip_icons, (interfaceMenu_t *)&VoIPMenu,
	interfaceListMenuIconThumbnail, NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&DialedNumbersMenu, voip_callsKeyCallback);
}

int voip_fillMenu(interfaceMenu_t *pMenu, void *pArg)
{
	char *str;
	char buffer[MENU_ENTRY_INFO_LENGTH];
	int redial_available = 0;
	menuActionFunction pAction;
	int show = (int)pArg;

	interface_clearMenuEntries((interfaceMenu_t*)&VoIPMenu);

	if(appControlInfo.voipInfo.active)
	{
		switch(appControlInfo.voipInfo.status)
		{
			case voipStatus_incoming: str = _T("ANSWER_CALL"); pAction = voip_answerCall; break;
			case voipStatus_dialing:
			case voipStatus_talking:  str = _T("HANGUP");      pAction = voip_hangup; break;
			default: str = _T("DIAL"); pAction = voip_enterURI;
		}
		interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, str, pAction, NULL, thumbnail_dial);
#ifdef ENABLE_VOIP_CONFERENCE
		if (appControlInfo.voipInfo.status == voipStatus_talking)
		{
			str = _T("DIAL_CONFERENCE");
			interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, str,voip_enterURI, NULL, thumbnail_dial);
		}
#endif
		if( appControlInfo.voipInfo.lastSip[4] != 0 )
		{
			sprintf(buffer,"%s (%s)",_T("REDIAL"), appControlInfo.voipInfo.lastSip);
			str = buffer;
			redial_available = appControlInfo.voipInfo.status == voipStatus_idle;
		} else
		{
			str = _T("REDIAL");
		}
		interface_addMenuEntryCustom((interfaceMenu_t*)&VoIPMenu, interfaceMenuEntryText, str, strlen(str)+1, redial_available, voip_dialNumber, NULL, NULL, NULL, (void*)ENTRY_INFO_SET(-1,LAST_DIALED), thumbnail_redial);
	
		str = _T("ADDRESS_BOOK");
		interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, str, voip_fillAddressBookMenu, NULL, thumbnail_address_book);
		str = _T("MISSED_CALLS");
		interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, str, voip_fillMissedCallsMenu, NULL, thumbnail_missed_calls);
		str = _T("ANSWERED_CALLS");
		interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, str, voip_fillAnsweredCallsMenu, NULL, thumbnail_answered_calls);
		str = _T("DIALED_NUMBERS");
		interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, str, voip_fillDialedNumbersMenu, NULL, thumbnail_dialed_numbers);
		str = _T("ACCOUNT");
		interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, str, voip_fillAccountMenu, (void*)-1, appControlInfo.voipInfo.connected ? thumbnail_account_active : thumbnail_account_inactive);
	} else
	{
		voip_init();

		if(appControlInfo.voipInfo.active)
			return voip_fillMenu(pMenu, pArg);

		/*str = _T("RECONNECT");
		interface_addMenuEntry((interfaceMenu_t*)&VoIPMenu, interfaceMenuEntryText, str, strlen(str), 1, voip_enterURI, NULL, NULL, NULL, thumbnail_dial);*/
		str = _T("VOIP_UNAVAILABLE");
		interface_addMenuEntryDisabled((interfaceMenu_t*)&VoIPMenu, str, -1);
	}
	if( show || interfaceInfo.currentMenu == (interfaceMenu_t*)&VoIPMenu )
	{
		interface_menuActionShowMenu(pMenu, (void*)&VoIPMenu);
	}	
	return 0;
}

int voip_realDialNumber(interfaceMenu_t *pMenu, void *pArg)
{
	int entryIndex = ENTRY_INFO_GET_INDEX((int)pArg), listIndex = ENTRY_INFO_GET_LIST((int)pArg), i;
	char *str = NULL;
	char uri[MAX_URL];

	dprintf("%s: Dialing #%d from %d\n", __FUNCTION__,entryIndex, listIndex);

	if(entryIndex != LAST_DIALED)
	{
		switch(listIndex)
		{
			case LIST_ADDRESS:  str = ADDRESSBOOK_FILENAME; break;
			case LIST_MISSED:   str = MISSED_FILENAME;      break;
			case LIST_ANSWERED: str = ANSWERED_FILENAME;    break;
			case LIST_DIALED:   str = DIALED_FILENAME;      break;
			default :;
		}
		if( str == NULL )
		{
			eprintf("VoIP: invalid list index\n");
			return -2;
		}
		if( (i = m3u_getEntry( str, entryIndex)) != 0)
		{
			eprintf("VoIP: invalid entry index %d\n",entryIndex);
			return -2;
		} else
		{
			strcpy(appControlInfo.voipInfo.lastSip, m3u_url);
		}
	}
	saveAppSettings();
	voip_addEntryToList(pMenu,(void*)LIST_DIALED);

#ifdef ENABLE_VOIP_CONFERENCE
	sprintf(uri, "%c%s", appControlInfo.voipInfo.status == voipStatus_talking ? 'y' : 'm', appControlInfo.voipInfo.lastSip);
#else
	sprintf(uri, "m%s", appControlInfo.voipInfo.lastSip);
#endif
	eprintf("VoIP: Dialing '%s'\n",uri);
	client_write(&voip_socket, uri, strlen(uri)+1 );

	return 0;
}

#ifdef ENABLE_VOIP_CONFERENCE
static int voip_dialNumberCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	if (cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft)
	{
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk)
	{
		voip_realDialNumber(pMenu, pArg);
		return 0;
	}

	return 1;
}
#endif

int voip_dialNumber(interfaceMenu_t *pMenu, void *pArg)
{
#ifdef ENABLE_VOIP_CONFERENCE
	int entryIndex = ENTRY_INFO_GET_INDEX((int)pArg);

	if(entryIndex != LAST_DIALED)
	{
		char *str;

		str =_T("DIAL_CONFERENCE_CONFIRM");

		interface_showConfirmationBox(str, thumbnail_question, voip_dialNumberCallback, pArg);

		return 0;
	}
#endif
	return voip_realDialNumber(pMenu, pArg);
}

static int voip_newAddressBookURI(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_SIP_URI"), "\\w+", voip_newURICallback, voip_getLastDialedURI, inputModeABC, pArg);
	return 0;
}

static int voip_newURICallback(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if( value == NULL )
		return 1;

	if (strstr(appControlInfo.voipInfo.lastSip, "sip:") != NULL)
	{
		strcpy(appControlInfo.voipInfo.lastSip, value);
	} else if (strstr(value, "@") != NULL)
	{
		sprintf(appControlInfo.voipInfo.lastSip, "sip:%s", value);
	} else
	{
		sprintf(appControlInfo.voipInfo.lastSip, "sip:%s@%s", value, appControlInfo.voipInfo.server);
	}
	voip_addNewAddressBookEntry(pMenu,appControlInfo.voipInfo.lastSip,NULL);
	return 0;
}

static int voip_addNewAddressBookEntry(interfaceMenu_t *pMenu, char *uri, char *nickname)
{
	FILE *file = m3u_initFile(ADDRESSBOOK_FILENAME, "r");
	if (file == NULL )
	{
		if( m3u_createFile(ADDRESSBOOK_FILENAME) != 0)
		{
			interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
			return -1;
		}
	} else
	{
		fclose(file);
	}

	if(m3u_findUrl(ADDRESSBOOK_FILENAME, uri) == 0)
	{
		interface_showMessageBox(_T("ADDED_TO_ADDRESS_BOOK"), thumbnail_yes, 3000);
		return 0;
	}

	if (m3u_addEntry(ADDRESSBOOK_FILENAME, uri, nickname) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
		return -1;
	}

	voip_fillAddressBookMenu(pMenu,NULL);
	interface_showMessageBox(_T("ADDED_TO_ADDRESS_BOOK"), thumbnail_yes, 3000);
	return 0;
}

static int voip_enterURI(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_SIP_URI"), "\\w+", voip_enterURICallback, voip_getLastDialedURI, inputModeABC, pArg);
	return 0;
}

static int voip_enterURICallback(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if( value == NULL || value[0] == 0 )
	{
		return -1;
	}
	if( strstr( value, "sip:" ) != NULL )
	{
		strcpy(appControlInfo.voipInfo.lastSip, value);
	} else
	{
		if( appControlInfo.voipInfo.connected && index(value, '@') == NULL && (index(value, '.') == NULL || inet_addr(value) == INADDR_NONE || inet_addr(value) == INADDR_ANY) && appControlInfo.voipInfo.server[0] != 0 )
		{
			sprintf(appControlInfo.voipInfo.lastSip, "sip:%s@%s", value, appControlInfo.voipInfo.server);
		} else
		{
			sprintf(appControlInfo.voipInfo.lastSip, "sip:%s", value);
		}
	}
	return voip_dialNumber(pMenu,(void*)ENTRY_INFO_SET(-1,LAST_DIALED));
}

static char *voip_getLastDialedURI(int field, void* pArg)
{
	if( field == 0 )
	{
		if( appControlInfo.voipInfo.lastSip[0] == 0 )
		{
			strcpy( appControlInfo.voipInfo.lastSip, "sip:" );
		}
		return appControlInfo.voipInfo.lastSip;
	} else
		return NULL;
}

static int voip_fillAccountMenu(interfaceMenu_t *pMenu, void *pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH], *str;

	interface_clearMenuEntries((interfaceMenu_t*)&AccountMenu);

	snprintf(buf,MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("VOIP_ENABLE"), appControlInfo.voipInfo.enabled ? _T("ON") : _T("OFF"));
	interface_addMenuEntry((interfaceMenu_t*)&AccountMenu, buf, voip_toggleEnabled, NULL, appControlInfo.voipInfo.enabled ? thumbnail_yes : thumbnail_no);

	if (appControlInfo.voipInfo.enabled)
	{
		//snprintf(buf,MENU_ENTRY_INFO_LENGTH, "SIP: %s", appControlInfo.voipInfo.sip);
		//interface_addMenuEntry((interfaceMenu_t*)&AccountMenu, interfaceMenuEntryText, buf, strlen(buf), 0, voip_toggleParam, NULL, NULL, (void*)PARAM_SIP, thumbnail_account);
		str = _T( voip_loggingIn ? "SERVER_QUERY" : (appControlInfo.voipInfo.connected || appControlInfo.voipInfo.server[0] == 0 ? "DISCONNECT" : "LOGIN_TO_SERVER") );
		interface_addMenuEntryCustom((interfaceMenu_t*)&AccountMenu, interfaceMenuEntryText,
			str, strlen(str), appControlInfo.voipInfo.connected || appControlInfo.voipInfo.server[0] != 0,
			appControlInfo.voipInfo.connected || appControlInfo.voipInfo.server[0] == 0 ? voip_logoutFromServer : voip_reloginToServer,
			NULL, NULL, NULL, NULL, appControlInfo.voipInfo.connected ? thumbnail_account_active : thumbnail_account_inactive);
		snprintf(buf,MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("SERVER"), appControlInfo.voipInfo.server[0] != 0 ? appControlInfo.voipInfo.server : _T("NONE") );
		interface_addMenuEntry((interfaceMenu_t*)&AccountMenu, buf, voip_toggleParam, (void*)PARAM_SERVER, thumbnail_account);
		if( appControlInfo.voipInfo.server[0] != 0 )
		{
			snprintf(buf,MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("LOGIN"), appControlInfo.voipInfo.login);
			interface_addMenuEntry((interfaceMenu_t*)&AccountMenu, buf, voip_toggleParam, (void*)PARAM_LOGIN, thumbnail_account);
			snprintf(buf,MENU_ENTRY_INFO_LENGTH, "%s: ***", _T("PASSWORD")/*, appControlInfo.voipInfo.passwd*/);
			interface_addMenuEntry((interfaceMenu_t*)&AccountMenu, buf, voip_toggleParam, (void*)PARAM_PASSWD, thumbnail_account);
			/*snprintf(buf,MENU_ENTRY_INFO_LENGTH, "%s: %s", _T("REALM"), appControlInfo.voipInfo.realm);
			interface_addMenuEntry((interfaceMenu_t*)&AccountMenu, buf, voip_toggleParam, (void*)PARAM_REALM, thumbnail_account);*/
		}
	}

	if( appControlInfo.voipInfo.connected == 0 && appControlInfo.voipInfo.server[0] == 0 && interface_getSelectedItem((interfaceMenu_t*)&AccountMenu) == 0 )
	{
		interface_setSelectedItem((interfaceMenu_t*)&AccountMenu, MENU_ITEM_BACK);
	}

	interface_setMenuLogo((interfaceMenu_t*)&AccountMenu, appControlInfo.voipInfo.connected ? thumbnail_account_active : thumbnail_account_inactive, -1, 0, 0, 0);

	if( pArg != NULL || (interfaceInfo.currentMenu == (interfaceMenu_t*)&AccountMenu) )
	{
		interface_menuActionShowMenu(pMenu, (void*)&AccountMenu);
		interface_displayMenu(1);
	}

	return 0;
}

static int voip_fillAddressBookMenu(interfaceMenu_t *pMenu, void *pArg)
{
	int i = 0;
	char *str;
	FILE *file;
	
	interface_clearMenuEntries((interfaceMenu_t*)&AddressBookMenu);

	str = _T("ADD");
	interface_addMenuEntry((interfaceMenu_t*)&AddressBookMenu, str, voip_newAddressBookURI, (void*)ENTRY_INFO_SET(LIST_ADDRESS, LAST_DIALED), thumbnail_dial);

	file = m3u_initFile(ADDRESSBOOK_FILENAME, "r");
	if(file != NULL)
	{
		while ( m3u_readEntry(file) == 0 )
		{
			interface_addMenuEntry((interfaceMenu_t*)&AddressBookMenu, m3u_description, voip_dialNumber, (void*)ENTRY_INFO_SET(LIST_ADDRESS, i), thumbnail_account_buddy );
			i++;
		}
	}
	if(i == 0)
	{
		str = _T("ADDRESS_BOOK_EMPTY");
		interface_addMenuEntryDisabled((interfaceMenu_t*)&AddressBookMenu, str, -1 );
	}
	if( interface_getSelectedItem( (interfaceMenu_t*)&AddressBookMenu ) >= i )
	{
		interface_setSelectedItem( (interfaceMenu_t*)&AddressBookMenu, i );
	}

	interface_menuActionShowMenu(pMenu, (void*)&AddressBookMenu);
	interface_displayMenu(1);
	return 0;
}

static int voip_clearList(interfaceMenu_t *pMenu, void* pArg)
{
	switch(ENTRY_INFO_GET_LIST((int)pArg))
	{
		case LIST_ALL:
			system("rm -f " MISSED_FILENAME);
			system("rm -f " ANSWERED_FILENAME);
			system("rm -f " DIALED_FILENAME);
			break;
		case LIST_MISSED:
			system("rm -f " MISSED_FILENAME);
			voip_fillMissedCallsMenu(pMenu, NULL);
			break;
		case LIST_ANSWERED:
			system("rm -f " ANSWERED_FILENAME);
			voip_fillAnsweredCallsMenu(pMenu, NULL);
			break;
		case LIST_DIALED:
			system("rm -f " DIALED_FILENAME);
			voip_fillDialedNumbersMenu(pMenu, NULL);
			break;
		case LIST_ADDRESS:
			system("rm -f " ADDRESSBOOK_FILENAME);
			voip_fillAddressBookMenu(pMenu, NULL);
			break;
		default: return 0;
	}
	return 0;
}

static int voip_addEntryToList( interfaceMenu_t *pMenu, void* pArg )
{
	char info[MENU_ENTRY_INFO_LENGTH], *filename = "";
	struct tm *t;
	FILE *file;

	switch((int)pArg)
	{
		case LIST_MISSED:
		case LIST_ANSWERED:
		case LIST_DIALED:
			switch((int)pArg)
			{
				case LIST_MISSED:   filename = MISSED_FILENAME;   break;
				case LIST_ANSWERED: filename = ANSWERED_FILENAME; break;
				case LIST_DIALED:   filename = DIALED_FILENAME;   break;
				default: break;
			}
			file = m3u_initFile(filename, "r");
			if (file == NULL )
			{
				if( m3u_createFile(filename) != 0)
				{
					interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
					return -1;
				}
			} else
			{
				fclose(file);
			}
			if((int)pArg == LIST_DIALED)
			{
				time(&voip_lastCallTime);
			}
			t = localtime(&voip_lastCallTime);
			strftime(info, MENU_ENTRY_INFO_LENGTH, _T("DATETIME_FORMAT"), t);
			snprintf(&info[strlen(info)], MENU_ENTRY_INFO_LENGTH - strlen(info), " %s", appControlInfo.voipInfo.lastSip);
			if (m3u_addEntry(filename, appControlInfo.voipInfo.lastSip, info) != 0)
			{
				interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
				return -1;
			}
			break;
		case LIST_ADDRESS:
			file = m3u_initFile(ADDRESSBOOK_FILENAME, "r");
			if (file == NULL )
			{
				if( m3u_createFile(ADDRESSBOOK_FILENAME) != 0)
				{
					interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
					return -1;
				}
			} else
			{
				fclose(file);
			}
			if (m3u_addEntry(ADDRESSBOOK_FILENAME, appControlInfo.voipInfo.lastSip, NULL) != 0)
			{
				interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
				return -1;
			}
			break;
		default: return 0;
	}
	return 0;
}

static int voip_deleteEntryFromList( interfaceMenu_t *pMenu, void *pArg )
{
	char *str = NULL;
	int entryIndex = ENTRY_INFO_GET_INDEX((int)pArg), listIndex = ENTRY_INFO_GET_LIST((int)pArg), res;
	if(entryIndex < 0)
	{
		eprintf("VoIP: Can't delete %d\n", entryIndex);
		return -2;
	}
	switch(listIndex)
	{
		case LIST_ADDRESS:  str = ADDRESSBOOK_FILENAME; break;
		case LIST_MISSED:   str = MISSED_FILENAME;      break;
		case LIST_ANSWERED: str = ANSWERED_FILENAME;    break;
		case LIST_DIALED:   str = DIALED_FILENAME;      break;
		default :;
	}
	if( str == NULL )
	{
		eprintf("VoIP: Can't delete %d: invalid list index %d\n", entryIndex, listIndex);
		return -2;
	}
	if( (res = m3u_deleteEntryByIndex( str, entryIndex)) == 0)
	{
		switch(listIndex)
		{
			case LIST_ADDRESS:  voip_fillAddressBookMenu(pMenu,NULL); break;
			case LIST_MISSED:   voip_fillMissedCallsMenu(pMenu,NULL); break;
			case LIST_ANSWERED: voip_fillAnsweredCallsMenu(pMenu,NULL); break;
			case LIST_DIALED:   voip_fillDialedNumbersMenu(pMenu,NULL); break;
			default :;
		}
	} else
	{
		eprintf("VoIP: Failed to delete %d from %s\n", entryIndex, str);
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
	}
	return res;
}

static int voip_getEntryInfo( void *pArg )
{
	char *str = NULL;
	int entryIndex = ENTRY_INFO_GET_INDEX((int)pArg), listIndex = ENTRY_INFO_GET_LIST((int)pArg);
	if(entryIndex < 0)
	{
		eprintf("VoIP: Failed to get %d entry info\n", entryIndex);
		return -2;
	}
	switch(listIndex)
	{
		case LIST_ADDRESS:  str = ADDRESSBOOK_FILENAME; break;
		case LIST_MISSED:   str = MISSED_FILENAME;      break;
		case LIST_ANSWERED: str = ANSWERED_FILENAME;    break;
		case LIST_DIALED:   str = DIALED_FILENAME;      break;
		default :;
	}
	if( str == NULL )
	{
		eprintf("VoIP: Failed to get %d entry info: invalid list index %d\n", entryIndex, listIndex);
		return -2;
	}
	return m3u_getEntry(str, entryIndex);
}

static int voip_showEntryInfo( interfaceMenu_t *pMenu, void *pArg )
{
	int res;
	if( (res = voip_getEntryInfo( pArg )) == 0 )
	{
		interface_showMessageBox(m3u_url, thumbnail_info, 5000);
	}
	return res;
}

static char *voip_getBuddyURI(int field, void* pArg)
{
	if( field == 0 &&  voip_getEntryInfo(pArg) == 0 )
	{
		return m3u_url;
	} else
	{
		return NULL;
	}
}

static char *voip_getBuddyName(int field, void* pArg)
{
	if( field == 0 && voip_getEntryInfo(pArg) == 0 )
	{
		return m3u_description;
	} else
	{
		return NULL;
	}
}

static int voip_fillMissedCallsMenu(interfaceMenu_t *pMenu, void *pArg)
{
	FILE *file;
	char *str;
	int i = 0;

	interface_clearMenuEntries((interfaceMenu_t*)&MissedCallsMenu);

	str = _T("CLEARLIST");
	interface_addMenuEntry((interfaceMenu_t*)&MissedCallsMenu, str, voip_clearList, (void*)ENTRY_INFO_SET(LIST_MISSED,LAST_DIALED), thumbnail_dial);

	file = m3u_initFile(MISSED_FILENAME, "r");
	if(file != NULL)
	{
		while ( m3u_readEntry(file) == 0 )
		{
			interface_addMenuEntry((interfaceMenu_t*)&MissedCallsMenu, m3u_description, voip_dialNumber, (void*)ENTRY_INFO_SET(LIST_MISSED,i), thumbnail_voip );
			i++;
		}
	}
	if( i == 0)
	{
		str = _T("NO_MISSED_CALLS");
		interface_addMenuEntryDisabled((interfaceMenu_t*)&MissedCallsMenu, str, -1 );
	}

	interface_menuActionShowMenu(pMenu, (void*)&MissedCallsMenu);
	interface_displayMenu(1);
	return 0;
}

static int voip_fillAnsweredCallsMenu(interfaceMenu_t *pMenu, void *pArg)
{
	FILE *file;
	char *str;
	int i = 0;

	interface_clearMenuEntries((interfaceMenu_t*)&AnsweredCallsMenu);

	str = _T("CLEARLIST");
	interface_addMenuEntry((interfaceMenu_t*)&AnsweredCallsMenu, str, voip_clearList, (void*)ENTRY_INFO_SET(LIST_ANSWERED,LAST_DIALED), thumbnail_dial);

	file = m3u_initFile(ANSWERED_FILENAME, "r");
	if(file != NULL)
	{
		while ( m3u_readEntry(file) == 0 )
		{
			interface_addMenuEntry((interfaceMenu_t*)&AnsweredCallsMenu, m3u_description, voip_dialNumber, (void*)ENTRY_INFO_SET(LIST_ANSWERED,i), thumbnail_voip );
			i++;
		}
	}
	if( i == 0)
	{
		str = _T("NO_ANSWERED_CALLS");
		interface_addMenuEntryDisabled((interfaceMenu_t*)&AnsweredCallsMenu, str, -1 );
	}
	interface_menuActionShowMenu(pMenu, (void*)&AnsweredCallsMenu);
	interface_displayMenu(1);
	return 0;
}

static int voip_fillDialedNumbersMenu(interfaceMenu_t *pMenu, void *pArg)
{
	FILE *file;
	char *str;
	int i = 0;

	interface_clearMenuEntries((interfaceMenu_t*)&DialedNumbersMenu);
	str = _T("CLEARLIST");
	interface_addMenuEntry((interfaceMenu_t*)&DialedNumbersMenu, str, voip_clearList, (void*)ENTRY_INFO_SET(LIST_DIALED,LAST_DIALED), thumbnail_dial);

	file = m3u_initFile(DIALED_FILENAME, "r");
	if(file != NULL)
	{
		while ( m3u_readEntry(file) == 0 )
		{
			interface_addMenuEntry((interfaceMenu_t*)&DialedNumbersMenu, m3u_description, voip_dialNumber, (void*)ENTRY_INFO_SET(LIST_DIALED,i), thumbnail_voip );
			i++;
		}
	}
	if( i == 0)
	{
		str = _T("NO_DIALED_NUMBERS");
		interface_addMenuEntryDisabled((interfaceMenu_t*)&DialedNumbersMenu, str, -1 );
	}
	interface_menuActionShowMenu(pMenu, (void*)&DialedNumbersMenu);
	interface_displayMenu(1);
	return 0;
}

static int voip_call_event(void* pArg)
{
	if( appControlInfo.voipInfo.status == voipStatus_idle)
	{
		interfaceInfo.showIncomingCall = 0;
		interface_displayMenu(1);
		return 0;
	}
	interface_addEvent(voip_call_event, NULL, interfaceInfo.showIncomingCall ? 100 : 300, 1);
	interfaceInfo.showIncomingCall = 1 - interfaceInfo.showIncomingCall;
	interface_displayMenu(1);
	return 0;
}

int voip_answerCall(interfaceMenu_t *pMenu, void *pArg)
{
	if(appControlInfo.voipInfo.status != voipStatus_incoming)
	{
		return 0;
	}
	client_write(&voip_socket, "a200", 5 );
	return 0;
}

int voip_hangup(interfaceMenu_t *pMenu, void *pArg)
{
	if( appControlInfo.voipInfo.status > voipStatus_idle )
	{
		client_write(&voip_socket, "ha", 3 );
		voip_addEntryToList(pMenu, (void*) (appControlInfo.voipInfo.status > voipStatus_incoming ? LIST_ANSWERED : LIST_MISSED) );
		interface_removeEvent(voip_call_event,NULL);
		interfaceInfo.showIncomingCall = 0;
		appControlInfo.voipInfo.status = voipStatus_idle;
		voip_fillMenu(interfaceInfo.currentMenu, (void*)0);
	}
	return 0;
}

static int voip_addressBookKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int selectedIndex = ENTRY_INFO_GET_INDEX((int)pArg);

	if (selectedIndex >= 0)
	{
		switch(cmd->command)
		{
			case interfaceCommandRed:
				// delete
				if( voip_deleteEntryFromList(pMenu,pArg) == 0)
				{
					interface_displayMenu(1);
				}
				return 0;
			case interfaceCommandGreen:
				// info
				voip_showEntryInfo( pMenu, pArg );
				return 0;
			case interfaceCommandYellow:
				// edit
				interface_getText(pMenu, _T("ENTER_SIP_URI"), "\\w+", voip_changeURI, voip_getBuddyURI, inputModeABC, pArg);
				return 0;
			case interfaceCommandBlue:
				// rename
				interface_getText(pMenu, _T("ENTER_TITLE"), "\\w+", voip_renameBuddy, voip_getBuddyName, inputModeABC, pArg);
				return 0;
			default: ;
		}
	}
	return 1;
}

static int voip_menuKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch( cmd->command)
	{
		case interfaceCommandRed:
			voip_fillMissedCallsMenu(pMenu, pArg);
			return 0;
		case interfaceCommandGreen:
			voip_fillAnsweredCallsMenu(pMenu, pArg);
			return 0;
		case interfaceCommandYellow:
			voip_fillDialedNumbersMenu(pMenu, pArg);
			return 0;
		case interfaceCommandBlue:
			voip_fillAddressBookMenu(pMenu, pArg);
			return 0;
		default:
			return 1;
	}
}

static int voip_callsKeyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	int res, selectedIndex = ENTRY_INFO_GET_INDEX((int)pArg), listIndex = ENTRY_INFO_GET_LIST((int)pArg);
	char *str = NULL;
	
	if (selectedIndex >= 0)
	{
		switch(cmd->command)
		{
			case interfaceCommandRed:
				// delete
				if( voip_deleteEntryFromList(pMenu,pArg) == 0)
				{
					interface_displayMenu(1);
				}
				return 0;
			case interfaceCommandGreen:
				// info
				voip_showEntryInfo( pMenu, pArg );
				return 0;
			case interfaceCommandYellow:
				// address book
				//ENTRY_INFO_SET(LIST_MISSED,selectedIndex)
				switch(listIndex)
				{
					case LIST_MISSED:   str = MISSED_FILENAME;   break;
					case LIST_ANSWERED: str = ANSWERED_FILENAME; break;
					case LIST_DIALED:   str = DIALED_FILENAME;   break;
					default :;
				}
				if( str == NULL )
				{
					return 0;
				}
				if( (res = m3u_getEntry( str, selectedIndex)) == 0)
				{
					strcpy(appControlInfo.voipInfo.lastSip, m3u_url);
					voip_addNewAddressBookEntry(pMenu, appControlInfo.voipInfo.lastSip, NULL);
				}
				return 0;
			default:;
		}
	}
	return 1;
}

static char *voip_getParam(int field, void* pArg)
{
	if( field == 0 )
	{
		switch((int)pArg)
		{
			case PARAM_SERVER: return appControlInfo.voipInfo.server;
			case PARAM_LOGIN:  return appControlInfo.voipInfo.login;
			case PARAM_PASSWD: return appControlInfo.voipInfo.passwd;
			case PARAM_REALM:  return appControlInfo.voipInfo.realm;
			default: 
				return NULL;
		}
	} else
		return NULL;
}

static int voip_changeParam(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	char *dest;
	if( value == NULL )
		return 1;
	switch((int)pArg)
	{
		case PARAM_SERVER: dest = appControlInfo.voipInfo.server; break;
		case PARAM_LOGIN:  dest = appControlInfo.voipInfo.login; break;
		case PARAM_PASSWD: dest = appControlInfo.voipInfo.passwd; break;
		case PARAM_REALM:  dest = appControlInfo.voipInfo.realm; break;
		default: dest = NULL;
	}
	if( dest == NULL )
	{
		return -2;
	}
	strcpy(dest,value);
	if( saveAppSettings() != 0)
	{
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
	}
	
	voip_fillAccountMenu(pMenu, (void*)-1);
	interface_displayMenu(1);

	return 0;
}

static int voip_toggleParam(interfaceMenu_t *pMenu, void* pArg)
{
	char *str;
	switch( (int)pArg )
	{
		case PARAM_SERVER: str = _T("ENTER_VOIP_SERVER_ADDR"); break;
		case PARAM_LOGIN:  str = _T("ENTER_VOIP_LOGIN"); break;
		case PARAM_PASSWD: str = _T("ENTER_VOIP_PASSWORD"); break;
		case PARAM_REALM:  str = _T("ENTER_VOIP_REALM"); break;
		default: str = NULL;
	}
	if( str == NULL )
	{
		return -2;
	}
	interface_getText(pMenu, str, "\\w+", voip_changeParam, voip_getParam, inputModeABC, pArg);

	return 0;
}

static int voip_toggleEnabled(interfaceMenu_t *pMenu, void* pArg)
{
	int showinfo = 1;

	appControlInfo.voipInfo.enabled = !appControlInfo.voipInfo.enabled;

	if( saveAppSettings() != 0)
	{
		interface_showMessageBox(_T("SETTINGS_SAVE_ERROR"), thumbnail_warning, 0);
		showinfo = 0;
	}

	voip_fillAccountMenu(pMenu, (void*)-1);
	interface_displayMenu(1);

	if (showinfo)
	{
		interface_showMessageBox(_T("PLEASE_WAIT"), thumbnail_info, 0);
	}

	if (appControlInfo.voipInfo.enabled)
	{
		system("/usr/local/etc/init.d/S95voip reload");
		sleep(5);
	} else
	{
		system("/usr/local/etc/init.d/S95voip stop");
		sleep(1);
	}

	if (showinfo)
	{
		interface_hideMessageBox();
	}

	return 0;
}


static int voip_logoutFromServer(interfaceMenu_t *pMenu, void* pArg)
{
	client_write(&voip_socket, "-a", 3);
	return 0;
}

static int voip_sendLoginInfo()
{
	char buf[3+MAX_URL+3*MAX_SIP_STRING+256], *str;
	//client_write(&voip_socket, "-a", 3);
	//fsync(voip_socket);
	buf[0] = '+';
	buf[1] = 'a';
	buf[2] = '\0';
	client_write(&voip_socket, buf, 3);
	str = buf;
	/* sip = sip:<login>@<server> */
	strcpy(str,"sip:");
	str = &str[4];
	strcpy(str,appControlInfo.voipInfo.login);
	str = &str[strlen(appControlInfo.voipInfo.login)];
	str[0] = '@';
	str++;
	strcpy(str,appControlInfo.voipInfo.server);
	str = &str[strlen(appControlInfo.voipInfo.server)+1];
	/* server = sip:<server> */
	strcpy(str,"sip:");
	str = &str[4];
	strcpy(str,appControlInfo.voipInfo.server);
	str = &str[strlen(appControlInfo.voipInfo.server)+1];
	strcpy(str,appControlInfo.voipInfo.realm);
	str = &str[strlen(appControlInfo.voipInfo.realm)+1];
	strcpy(str,appControlInfo.voipInfo.login);
	str = &str[strlen(appControlInfo.voipInfo.login)+1];
	strcpy(str,appControlInfo.voipInfo.passwd);
	str = &str[strlen(appControlInfo.voipInfo.passwd)+1];
	client_write(&voip_socket, buf, str-buf);
#ifdef DEBUG
	int i;
	for ( i = 0; i < str - buf; i++)
		if( buf[i] == 0 ) buf[i] = '!';
	buf[str-buf] = 0;
	eprintf("%s: Account sequence: %s\n", __FUNCTION__, buf);
#endif
	return 0;
}

static int voip_loginToServer()
{
	voip_sendLoginInfo();
	voip_loggingIn = 1;
	voip_fillAccountMenu(interfaceInfo.currentMenu, (void*)0);
	return 0;
}

static int voip_reloginToServer(interfaceMenu_t *pMenu, void* pArg)
{
	if(strlen( appControlInfo.voipInfo.server ) * strlen( appControlInfo.voipInfo.realm ) * strlen ( appControlInfo.voipInfo.login ) * strlen(appControlInfo.voipInfo.passwd) == 0 )
	{
		interface_showMessageBox(_T("ERR_NOT_ALL_FIELDS"), thumbnail_info, 3000);
		return -2;
	}

	voip_logoutFromServer(pMenu,pArg);
	usleep(100000); // wait for pjsua response
	voip_loginToServer();

	return 0;
}

static int voip_changeURI(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int res, selectedIndex = ENTRY_INFO_GET_INDEX((int)pArg);
	char description[MENU_ENTRY_INFO_LENGTH];

	if (value == NULL)
	{
		return 1;
	}

	if((res = m3u_getEntry( ADDRESSBOOK_FILENAME, selectedIndex)) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
		return res;
	}
	strcpy(description, m3u_description);
	if((res = m3u_replaceEntryByIndex( ADDRESSBOOK_FILENAME, selectedIndex, value, description )) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
	} else
	{
		voip_fillAddressBookMenu(pMenu, NULL);
	}
	return res;
}

static int voip_renameBuddy(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	int res, selectedIndex = ENTRY_INFO_GET_INDEX((int)pArg);
	char uri[MAX_URL];

	if( value == NULL )
		return 1;

	if((res = m3u_getEntry( ADDRESSBOOK_FILENAME, selectedIndex )) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
		return 1;
	}
	strcpy(uri, m3u_url);
	if((res = m3u_replaceEntryByIndex( ADDRESSBOOK_FILENAME, selectedIndex, uri, value )) != 0)
	{
		interface_showMessageBox(_T("ERR_SAVE_FAILED"), thumbnail_error, 0);
	} else
	{
		voip_fillAddressBookMenu(pMenu, NULL);
	}
	return res;
}

void voip_setBuzzer()
{
	char buf[3] = "B+";
	buf[1] = appControlInfo.voipInfo.buzzer ? '+' : '-';
	client_write(&voip_socket, buf, 3 );
}

#endif // ENABLE_VOIP
