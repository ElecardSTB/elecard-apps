
/*
 StbMainApp.c

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

#ifdef ENABLE_GSTREAMER
// Should be included before <directfb.h>!
#include <gst/gst.h>
#endif

#include "StbMainApp.h"

#include "debug.h"
#include "rtp.h"
#include "rtsp.h"
#include "tools.h"
#include "rtp_func.h"
#include "list.h"
#include "app_info.h"
#include "sem.h"
#include "gfx.h"
#include "interface.h"
#include "helper.h"
#include "l10n.h"
#include "sound.h"
#include "dvb.h"
#include "pvr.h"
#include "rtsp.h"
#include "rutube.h"
#include "off_air.h"
#include "output.h"
#include "voip.h"
#include "media.h"
#include "playlist.h"
#include "menu_app.h"
#include "watchdog.h"
#include "downloader.h"
#include "dlna.h"
#include "youtube.h"
#include "messages.h"
#include "stsdk.h"
#include "gstreamer.h"
#ifdef ENABLE_VIDIMAX
#include "vidimax.h" 
#endif

#ifdef STB225
#include "Stb225.h"
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#endif
#ifdef STB82
#include <phStbSystemManager.h>
#include "stb820.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>

#include <directfb.h>
#include <pthread.h>
#include <wait.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <linux/input.h>

#include "../../src/md5.h"

/***********************************************
* LOCAL MACROS                                 *
************************************************/

/** How many keypresses skip before starting repeating */
#define REPEAT_THRESHOLD (2)
/** Timeout in microseconds after which repeating threshold is activated */
#ifdef ENABLE_VIDIMAX
#define REPEAT_TIMEOUT (40000)
#else
#define REPEAT_TIMEOUT (400000)
#endif
/** Timeout in microseconds after which events are considered outdated and ignored */
#define OUTDATE_TIMEOUT  (50000)

/** Timeout in minutes, during which the user will be warned of broadcasting playback stop */
#define AUTOSTOP_WARNING_TIMEOUT  (5)

#define ADD_HANDLER(key, command) case key: cmd = command; dprintf("%s: Got command: %s\n", __FUNCTION__, #command); break;
#define ADD_FP_HANDLER(key, command) case key: cmd = command; event->input.device_id = DID_FRONTPANEL; dprintf("%s: Got command (FP): %s\n", __FUNCTION__, #command); break;
#define ADD_FALLTHROUGH(key) case key:

/** MMIO application path */
#define INFO_MMIO_APP "/usr/lib/luddite/mmio"
/** MMIO address template */
#define INFO_ADDR_TEMPLATE " 0x"

/** Temp file for text processing */
#define INFO_TEMP_FILE "/tmp/info.txt"

//#define kprintf dprintf
#ifndef kprintf
#define kprintf(x...)
#endif


/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
#ifndef STSDK
static int helper_confirmFirmwareUpdate(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);
#endif
#ifdef ENABLE_FUSION
int fusion_getCreepAndLogo ();
int fusion_checkResponse (char * curlStream, cJSON ** ppRoot);
int fusion_readConfig();
long fusion_getRemoteFileSize(char * url);
int fusion_getSecret ();
int fusion_setMoscowDateTime();
int fusion_getUtc (char * utcBuffer);
#endif

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static int inStandbyActiveVideo;

/***********************************************
* EXPORTED DATA                                *
************************************************/

volatile int keepCommandLoopAlive;
volatile int keyThreadActive;

volatile int flushEventsFlag;

char startApp[PATH_MAX] = "";

interfaceCommand_t lastRecvCmd = interfaceCommandNone;
int captureInput = 0;

#ifdef ENABLE_TEST_SERVER
	pthread_t server_thread;
#endif

int gAllowConsoleInput = 0;
int gAllowTextMenu = 0;
int gIgnoreEventTimestamp = 0;

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int helperReadLine(int file, char* buffer)
{
	if ( file )
	{
		int index = 0;
		while ( 1 )
		{
			char c;

			if ( read(file, &c, 1) < 1 )
			{
				if ( index > 0 )
				{
					buffer[index] = '\0';
					return 0;
				}
				return -1;
			}

			if ( c == '\n' )
			{
				buffer[index] = '\0';
				return 0;
			} else if ( c == '\r' )
			{
				continue;
			} else
			{
				buffer[index] = c;
				index++;
			}
		}
	}
	return -1;
}

int helperParseMmio(int addr)
{
	int file, num;
	char buf[BUFFER_SIZE];
	char *pos;

	sprintf(buf, INFO_MMIO_APP " 0x%X > " INFO_TEMP_FILE, addr);
	system(buf);

	num = 0;

	file = open(INFO_TEMP_FILE, O_RDONLY);
	if (file > 0)
	{
		if (helperReadLine(file, buf) == 0 && strlen(buf) > 0)
		{
			pos = strstr(buf, INFO_ADDR_TEMPLATE);
			if (pos != NULL)
			{
				pos += sizeof(INFO_ADDR_TEMPLATE)-1;
				num = strtol(pos, NULL, 16);
			}
		}
		close(file);
	}

	return num;
}



int helperParseLine(const char *path, const char *cmd, const char *pattern, char *out, char stopChar )
{
	int file, res;
	char buf[BUFFER_SIZE];
	char *pos, *end;

	if (cmd != NULL)
	{
		/* empty output file */
		sprintf(buf, "echo -n > %s", path);
		system(buf);

		sprintf(buf, "%s > %s", cmd, path);
		system(buf);
	}

	res = 0;
	file = open(path, O_RDONLY);
	if (file > 0)
	{
		if (helperReadLine(file, buf) == 0 && strlen(buf) > 0)
		{
			if (pattern != NULL)
			{
				pos = strstr(buf, pattern);
			} else
			{
				pos = buf;
			}
			if (pos != NULL)
			{
				if (out == NULL)
				{
					res = 1;
				} else
				{
					if (pattern != NULL)
					{
						pos += strlen(pattern);
					}
					while(*pos == ' ')
					{
						pos++;
					}
					if ((end = strchr(pos, stopChar)) != NULL || (end = strchr(pos, '\r')) != NULL || (end = strchr(pos, '\n')) != NULL)
					{
						*end = 0;
					}
					strcpy(out, pos);
					res = 1;
				}
			}
		}
		close(file);
	}

	return res;
}

/* Handle any registered signal by requesting a graceful exit */
void signal_handler(int signal)
{
	static int caught = 0;

	//dprintf("%s: in\n", __FUNCTION__);

	if ( caught == 0 )
	{
		caught = 1;
		eprintf("App: Stopping Command Loop ... (signal %d)\n", signal);
		keepCommandLoopAlive = 0;
	}
}

static void stub_signal_handler(int signal)
{
	dprintf("App: %s Error (signal %d), ignore!\n", signal == SIGBUS ? "BUS" : "PIPE", signal);
}

static void usr1_signal_handler(int sig)
{
	eprintf("App: Got USR1 (signal %d), updating USB!\n", sig);
	media_storagesChanged();
#ifdef ENABLE_PVR
	pvr_storagesChanged();
#endif
}

static void hup_signal_handler(int sig)
{
	eprintf("App: Got HUP (signal %d), reloading config!\n", sig);
	loadAppSettings();
	loadVoipSettings();
#ifdef STSDK
	output_readInterfacesFile();
#endif
#if (defined ENABLE_MESSAGES) && (defined MESSAGES_NAGGING)
	if (appControlInfo.messagesInfo.newMessage)
		messages_showFile(appControlInfo.messagesInfo.newMessage);
#endif
}

static void parse_commandLine(int argc, char *argv[])
{
	int i;
	static char infoFiles[1][1024];

	for ( i=0;i<argc;i++ )
	{
		if ( strcmp(argv[i], "-stream_info_url") == 0 )
		{
			strcpy(appControlInfo.rtspInfo.streamInfoIP, argv[i+1]);
			i++;
		} else if ( strcmp(argv[i], "-stream_url") == 0 )
		{
			strcpy(appControlInfo.rtspInfo.streamIP, argv[i+1]);
			i++;
		} else if ( strcmp(argv[i], "-rtsp_port") == 0 )
		{
			appControlInfo.rtspInfo.RTSPPort = atoi(argv[i+1]);
			i++;
		} else if ( strcmp(argv[i], "-stream_info_file") == 0 )
		{
			strcpy(infoFiles[0], argv[i+1]);
			appControlInfo.rtspInfo.streamInfoFiles = (char**)infoFiles;
			i++;
		} else if ( strcmp(argv[i], "-stream_file") == 0 )
		{
			strcpy(appControlInfo.rtspInfo.streamFile, argv[i+1]);
			i++;
		} else if ( !strcmp(argv[i], "-i2s1") )
		{
			appControlInfo.soundInfo.rcaOutput = 1;
		} else if ( !strcmp(argv[i], "-console") )
		{
			gAllowConsoleInput = 1;
		} else if ( !strcmp(argv[i], "-no-event-timestamp") )
		{
			gIgnoreEventTimestamp = 1;
		} else if ( !strcmp(argv[i], "-text_menu") )
		{
			gAllowTextMenu = 1;
		}
	}
}

interfaceCommand_t parseEvent(DFBEvent *event)
{
	interfaceCommand_t cmd = interfaceCommandNone;
#ifdef STBPNX
	if( event->input.device_id != DID_KEYBOARD )
#ifdef STB225
		switch ( event->input.key_id )
		{
			ADD_FP_HANDLER(DIKI_KP_4, interfaceCommandUp)
			ADD_FP_HANDLER(DIKI_KP_5, interfaceCommandDown)
			ADD_FP_HANDLER(DIKI_KP_0, interfaceCommandLeft)
			ADD_FP_HANDLER(DIKI_KP_6, interfaceCommandRight)
			ADD_FP_HANDLER(DIKI_KP_3, interfaceCommandEnter)
			ADD_FP_HANDLER(DIKI_KP_1, interfaceCommandToggleMenu)
			ADD_FP_HANDLER(DIKI_KP_9, interfaceCommandBack)
#else
		switch ( event->input.button )
		{
			ADD_FP_HANDLER(3, interfaceCommandUp)
			ADD_FP_HANDLER(4, interfaceCommandDown)
			ADD_FP_HANDLER(1, interfaceCommandLeft)
			ADD_FP_HANDLER(2, interfaceCommandRight)
			ADD_FP_HANDLER(5, interfaceCommandEnter)
#endif
		default:;
	}
#endif // STBPNX

	if (cmd == interfaceCommandNone)
	{
		//dprintf("%s: key symbol %d '%c'\n", __FUNCTION__, event->input.key_symbol, event->input.key_symbol);
		switch ( event->input.key_symbol )
		{
			//ADD_FALLTHROUGH(DIKS_ENTER)
			//ADD_HANDLER(DIKS_OK, interfaceCommandOk)
			//ADD_FALLTHROUGH(DIKS_SPACE)
			ADD_HANDLER(DIKS_MENU, interfaceCommandToggleMenu)
			//ADD_HANDLER(DIKS_EXIT, interfaceCommandExit)
			ADD_HANDLER(DIKS_EXIT, interfaceCommandExit)
			//ADD_FALLTHROUGH(DIKS_BACKSPACE)
			ADD_HANDLER(DIKS_BACK, interfaceCommandBack)
			//ADD_HANDLER(DIKS_ESCAPE, interfaceCommandMainMenu)
			ADD_HANDLER(DIKS_VOLUME_UP, interfaceCommandVolumeUp)
			ADD_HANDLER(DIKS_VOLUME_DOWN, interfaceCommandVolumeDown)
			ADD_HANDLER(DIKS_MUTE, interfaceCommandVolumeMute)
			//ADD_FALLTHROUGH('z')
			ADD_HANDLER(DIKS_PREVIOUS, interfaceCommandPrevious)
			//ADD_FALLTHROUGH('x')
			ADD_HANDLER(DIKS_REWIND, interfaceCommandRewind)
			//ADD_FALLTHROUGH('c')
			ADD_HANDLER(DIKS_STOP, interfaceCommandStop)
#if !(defined STB225) // DIKS_PAUSE on STB225 mapped to wrong button
			//ADD_FALLTHROUGH('v')
			ADD_HANDLER(DIKS_PAUSE, interfaceCommandPause)
#endif
			//ADD_FALLTHROUGH('b')
			ADD_HANDLER(DIKS_PLAY, interfaceCommandPlay)
			//ADD_FALLTHROUGH('n')
			ADD_HANDLER(DIKS_FASTFORWARD, interfaceCommandFastForward)
			//ADD_FALLTHROUGH('m')
			ADD_HANDLER(DIKS_NEXT, interfaceCommandNext)
			//ADD_FALLTHROUGH(DIKS_TAB)
#if (defined STBPNX) // STB820/225 remotes don't have EPG button
			ADD_HANDLER(DIKS_EPG, interfaceCommandChangeMenuStyle)
#endif
			//ADD_FALLTHROUGH('i')
			//ADD_FALLTHROUGH(DIKS_CUSTOM1) // 'i' on stb remote
#if (defined STBPNX)
			ADD_HANDLER(DIKS_INFO, interfaceCommandServices)
			ADD_HANDLER(DIKS_FAVORITES, interfaceCommandWeb)
			ADD_HANDLER(DIKS_AUX, interfaceCommandTeletext)
#endif
			
			ADD_HANDLER(DIKS_TEXT, interfaceCommandTeletext)
			
			ADD_HANDLER(DIKS_RED, interfaceCommandRed)
			ADD_HANDLER(DIKS_GREEN, interfaceCommandGreen)
			ADD_HANDLER(DIKS_YELLOW, interfaceCommandYellow)
			ADD_HANDLER(DIKS_BLUE, interfaceCommandBlue)
			case DIKS_PLAYPAUSE:
				if ( gfx_videoProviderIsActive(screenMain) && !gfx_videoProviderIsPaused(screenMain) )
					cmd = interfaceCommandPause;
				else
					cmd = interfaceCommandPlay;
				dprintf("%s: play/pause: %s\n", __FUNCTION__, cmd == interfaceCommandPause ? "interfaceCommandPause" : "interfaceCommandPlay");
				break;
		default:
			//cmd = interfaceCommandNone;
			cmd = event->input.key_symbol;
		}
	}

	lastRecvCmd = cmd;

#ifdef ENABLE_TEST_MODE
	if (captureInput)
	{
		return interfaceCommandCount;
	}
#endif

	return cmd;
}

static int helperIsEventValid(DFBEvent *event)
{
	int valid = 1;

	if (!gIgnoreEventTimestamp) {
		int timediff;
		struct timeval curTime;
		gettimeofday(&curTime, NULL);
		timediff = (curTime.tv_sec - event->input.timestamp.tv_sec) * 1000000 + (curTime.tv_usec - event->input.timestamp.tv_usec);
		if (timediff > OUTDATE_TIMEOUT)	{
			dprintf("%s: ignore event, age %d\n", __FUNCTION__, timediff);
			valid = 0;
		}
	}
	return valid;
}

static int32_t Helper_IsTimeGreater(struct timeval t1, struct timeval t2)
{
	if(t1.tv_sec > t2.tv_sec)
		return 1;
	if(t1.tv_sec < t2.tv_sec)
		return 0;
	if(t1.tv_usec > t2.tv_usec)
		return 1;

	return 0;
}

#define POWEROFF_TIMEOUT	3 //seconds
#define STANDBY_TIMEOUT		3 //seconds

static int PowerOff(void *pArg)
{
	interface_showMessageBox(_T("POWER_OFF"), thumbnail_warning, 0);
	return system("poweroff");
}

static int toggleStandby(void)
{
	interfaceCommandEvent_t cmd;

	//dprintf("%s: switch standby\n", __FUNCTION__);
	/* Standby button was pressed once. Switch standby mode.  */
	if (appControlInfo.inStandby == 0) {
		//dprintf("%s: go to standby\n", __FUNCTION__);
		appControlInfo.inStandby = 1;

#if (defined ENABLE_PVR && defined ENABLE_DVB && defined STBPNX)
		if(pvr_isPlayingDVB(screenMain)) {
			offair_stopVideo(screenMain, 1);
		}
#endif

		inStandbyActiveVideo = gfx_videoProviderIsActive(screenMain);

		if(inStandbyActiveVideo) {
			memset(&cmd, 0, sizeof(interfaceCommandEvent_t));
			cmd.source = DID_STANDBY;
			cmd.command = interfaceCommandStop;
			interface_processCommand(&cmd);
		}
#ifdef ENABLE_DVB
		if (appControlInfo.dvbInfo.active) {
			inStandbyActiveVideo = -appControlInfo.dvbInfo.channel;
			offair_stopVideo(screenMain, 1);
		}
#endif
		interface_displayMenu(1);

		system("standbyon");
		return 1;
	} else {
		interface_displayMenu(1);
		system("standbyoff");

		if(inStandbyActiveVideo) {
			memset(&cmd, 0, sizeof(interfaceCommandEvent_t));
			cmd.command = interfaceCommandPlay;
			interface_processCommand(&cmd);
		}
#ifdef ENABLE_DVB
		if(inStandbyActiveVideo<0)
			offair_channelChange(interfaceInfo.currentMenu, CHANNEL_INFO_SET(screenMain, appControlInfo.dvbInfo.channel));
#endif
		//dprintf("%s: return from standby\n", __FUNCTION__);
		appControlInfo.inStandby = 0;
#ifndef STSDK
		if(helperCheckUpdates()) {
			interface_showConfirmationBox(_T("CONFIRM_FIRMWARE_UPDATE"), thumbnail_question, helper_confirmFirmwareUpdate, NULL);
		}
#endif
		return 2;
	}
}

static int checkPowerOff(DFBEvent *pEvent)
{
#if (!defined DISABLE_STANDBY)
	int isFrontpanelPower = 0;
#ifdef STSDK
	//for STB840 PromSvyaz frontpanel POWER button
	isFrontpanelPower = (pEvent->input.device_id == DIDID_KEYBOARD) && (pEvent->input.key_code == KEY_POWER);
#endif

	//dprintf("%s: check power\n", __FUNCTION__);
	// Power/Standby button. Go to standby.
	if((pEvent->input.key_symbol == DIKS_POWER) || isFrontpanelPower) {
		static struct timeval	validStandbySwitchTime = {0, 0};
		static struct timeval	poweroffTriggerTime = {0, 0};
		struct timeval			currentPress = {0, 0};
		int						repeat = 0;
#ifdef STSDK
		static int isPowerReleased = 1;

		gettimeofday(&currentPress, NULL);
		if((pEvent->input.type == DIET_KEYRELEASE) || (pEvent->input.type == DIET_BUTTONRELEASE)) {
			isPowerReleased = 1;
			interface_removeEvent(PowerOff, NULL);
		} else {
			if(appControlInfo.inStandby)
				return 0; //dont check pressed more than 3 seconds POWER button in standby
			if(isPowerReleased) {
				memcpy(&poweroffTriggerTime, &currentPress, sizeof(struct timeval));
				poweroffTriggerTime.tv_sec += POWEROFF_TIMEOUT;
				isPowerReleased = 0;
				interface_addEvent(PowerOff, NULL, POWEROFF_TIMEOUT * 1000, 1);
			}
			//try to check if button pressed more than 3 seconds
			repeat = 1;
		}
#else
		static struct timeval prevPressTime = {0, 0};
		int timediff;

		gettimeofday(&currentPress, NULL);
		if(poweroffTriggerTime.tv_sec == 0 || prevPressTime.tv_sec == 0) {
			timediff = 0;
		} else {
			timediff = (currentPress.tv_sec - prevPressTime.tv_sec) * 1000000 + (currentPress.tv_usec - prevPressTime.tv_usec);
		}

		if(timediff == 0 || timediff > REPEAT_TIMEOUT) {
			//dprintf("%s: reset\n, __FUNCTION__");
			memcpy(&poweroffTriggerTime, &currentPress, sizeof(struct timeval));
			poweroffTriggerTime.tv_sec += POWEROFF_TIMEOUT;
		} else {
			//dprintf("%s: repeat detected\n", __FUNCTION__);
			repeat = 1;
		}
		memcpy(&prevPressTime, &currentPress, sizeof(struct timeval));
#endif
		//dprintf("%s: got DIKS_POWER\n", __FUNCTION__);
		if(repeat && Helper_IsTimeGreater(currentPress, poweroffTriggerTime)) {
			//dprintf("%s: repeat 3 sec - halt\n", __FUNCTION__);
			/* Standby button has been held for 3 seconds. Power off. */
//			PowerOff(NULL);
		} else if(!repeat && Helper_IsTimeGreater(currentPress, validStandbySwitchTime))
		{
			int ret = toggleStandby();

			memcpy(&validStandbySwitchTime, &currentPress, sizeof(struct timeval));
			validStandbySwitchTime.tv_sec += STANDBY_TIMEOUT;
			return ret;
		}
	} else if(pEvent->input.button == 9) {// PSU button, just do power off
		PowerOff(NULL);
	}
#endif
	return 0;
}

void helperFlushEvents()
{
	flushEventsFlag = 1;
}

interfaceCommand_t helperGetEvent(int flush)
{
	IDirectFBEventBuffer *eventBuffer = appEventBuffer;
	DFBEvent event;

	if(eventBuffer == NULL)
		return interfaceCommandNone;

	if(eventBuffer->HasEvent(eventBuffer) == DFB_OK) {
		eventBuffer->GetEvent(eventBuffer, &event);
		if(flush) {
			eventBuffer->Reset(eventBuffer);
		}

		if(helperIsEventValid(&event)) {
			if(checkPowerOff(&event)) {
				eventBuffer->Reset(eventBuffer);
				return interfaceCommandNone;
			}
		}

		if ( event.clazz == DFEC_INPUT && (event.input.type == DIET_KEYPRESS || event.input.type == DIET_BUTTONPRESS) ) {
			return parseEvent(&event);
		}
		return interfaceCommandCount;
	}

	return interfaceCommandNone;
}

#ifdef ENABLE_TEST_SERVER
#define SERVER_SOCKET "/tmp/app_server.sock"
void *testServerThread(void *pArg)
{
	int sock = -1, s, res, pos;
	unsigned int salen;
	char ibuf[2048], obuf[2048], *ptr, *ptr1;
#ifdef TEST_SERVER_INET
	struct sockaddr_in sa;
#else
	struct sockaddr_un sa;
#endif

	eprintf("App: Start test server receiver\n");

#ifdef TEST_SERVER_INET
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sock < 0)
	{
		eprintf("App: Failed to create socket!\n");
		return NULL;
	}

	res = 1;
	if ( setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&res, sizeof(res)) != 0 )
	{
		eprintf("App: Failed to set socket options!\n");
		close(sock);
		return NULL;
	}

	memset(&sa, 0, sizeof(sa));

	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(12304);

	res = bind(sock, (struct sockaddr*)&sa, sizeof(sa));

	if (res < 0)
	{
		eprintf("App: Failed bind socket!\n");
		close(sock);
		return NULL;
	}

#else
	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		eprintf("App: Failed to create socket");
		return NULL;
	}

	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, SERVER_SOCKET);
	unlink(sa.sun_path);
	salen = strlen(sa.sun_path) + sizeof(sa.sun_family);
	if (bind(sock, (struct sockaddr *)&sa, salen) == -1)
	{
		eprintf("App: Failed to bind Unix socket");
		close(sock);
		return NULL;
	}

	if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1)
	{
		eprintf("App: Failed to set non-blocking mode");
		close(sock);
		return NULL;
	}
#endif

	if (listen(sock, 5) == -1)
	{
		eprintf("App: Failed to listen socket");
		close(sock);
		return NULL;
	}

	eprintf("App: Wait for server connection\n");

	while(keepCommandLoopAlive)
	{
		fd_set reads;
		struct timeval tv;

		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		FD_ZERO(&reads);
		FD_SET(sock, &reads);

		if (select(sock+1, &reads, NULL, NULL, &tv) < 1)
		{
			continue;
		}

		salen = sizeof(sa);
		s = accept(sock, (struct sockaddr*)&sa, &salen);

		eprintf("App: New client connection!\n");

#ifdef TEST_SERVER_INET
		strcpy(obuf, "Elecard STB820 App Server\r\n");
		write(s, obuf, strlen(obuf));
#endif

		pos = 0;

		while(keepCommandLoopAlive)
		{
			tv.tv_sec = 0;
			tv.tv_usec = 100000;

			FD_ZERO(&reads);
			FD_SET(s, &reads);

			if (select(s+1, &reads, NULL, NULL, &tv) < 1)
			{
				continue;
			}

			res = read(s, &ibuf[pos], sizeof(ibuf)-pos-1);

			dprintf("%s: read %d bytes\n", __FUNCTION__, res);

			if (!res)
			{
				break;
			}

			pos += res;
			ibuf[pos] = 0;

			if ((ptr = strchr(ibuf, '\n')) != NULL)
			{
				pos = 0;

				obuf[0] = 0;

				ptr1 = strchr(ibuf, '\r');

				if (ptr1 != NULL && ptr1 < ptr)
				{
					ptr = ptr1;
				}

				*ptr = 0;

				if (strcmp(ibuf, "sysid") == 0)
				{
					if (!helperParseLine(INFO_TEMP_FILE, "stmclient 5", NULL, obuf, '\n'))
					{
						strcpy(obuf, "ERROR: Cannot get System ID");
					}
					strcat(obuf, "\r\n");
				} else if (strcmp(ibuf, "serial") == 0)
				{
					systemId_t sysid;
					systemSerial_t serial;

					if (helperParseLine(INFO_TEMP_FILE, "cat /dev/sysid", "SERNO: ", obuf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
					{
						serial.SerialFull = strtoul(obuf, NULL, 16);
					} else {
						serial.SerialFull = 0;
					}

					if (helperParseLine(INFO_TEMP_FILE, NULL, "SYSID: ", obuf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
					{
						sysid.IDFull = strtoul(obuf, NULL, 16);
					} else {
						sysid.IDFull = 0;
					}

					get_composite_serial(sysid, serial, obuf);

					strcat(obuf, "\r\n");
				} else if (strcmp(ibuf, "stmfw") == 0)
				{
					unsigned long stmfw;

					if (helperParseLine(INFO_TEMP_FILE, "cat /dev/sysid", "VER: ", obuf, ',')) // SYSID: 04044020, SERNO: 00000039, VER: 0107
					{
						stmfw = strtoul(obuf, NULL, 16);
					} else {
						stmfw = 0;
					}

					sprintf(obuf, "%lu.%lu", (stmfw >> 8)&0xFF, (stmfw)&0xFF);
					strcat(obuf, "\r\n");
				} else if (strcmp(ibuf, "mac1") == 0)
				{
					if (!helperParseLine(INFO_TEMP_FILE, "stmclient 7", NULL, obuf, '\n'))
					{
						strcpy(obuf, "ERROR: Cannot get MAC 1");
					} else
					{
						int a = 0, b = 0;
						while(obuf[a] != 0)
						{
							if (obuf[a] != ':')
							{
								obuf[b] = obuf[a];
								b++;
							}
							a++;
						}
						obuf[b] = 0;
					}
					strcat(obuf, "\r\n");
				} else if (strcmp(ibuf, "mac2") == 0)
				{
					if (!helperParseLine(INFO_TEMP_FILE, "stmclient 8", NULL, obuf, '\n'))
					{
						strcpy(obuf, "ERROR: Cannot get MAC 2");
					} else
					{
						int a = 0, b = 0;
						while(obuf[a] != 0)
						{
							if (obuf[a] != ':')
							{
								obuf[b] = obuf[a];
								b++;
							}
							a++;
						}
						obuf[b] = 0;
					}
					strcat(obuf, "\r\n");
				} else if (strcmp(ibuf, "fwversion") == 0)
				{
					strcpy(obuf, RELEASE_TYPE);
					strcat(obuf, "\r\n");
				} else if (strcmp(ibuf, "getinput") == 0)
				{
					lastRecvCmd = interfaceCommandNone;

					captureInput = 1;

					while (lastRecvCmd != interfaceCommandBack)
					{
						if (lastRecvCmd != interfaceCommandNone)
						{
							switch (lastRecvCmd)
							{
								case interfaceCommandUp:
									ptr = "UP";
									break;
								case interfaceCommandDown:
									ptr = "DOWN";
									break;
								case interfaceCommandLeft:
									ptr = "LEFT";
									break;
								case interfaceCommandRight:
									ptr = "RIGHT";
									break;
								case interfaceCommandEnter:
									ptr = "OK";
									break;
								case interfaceCommandChannelDown:
									ptr = "CH DOWN";
									break;
								case interfaceCommandChannelUp:
									ptr = "CH UP";
									break;
								case interfaceCommandVolumeDown:
									ptr = "VOL DOWN";
									break;
								case interfaceCommandVolumeUp:
									ptr = "VOL UP";
									break;
								default:
									sprintf(ibuf, "MISC(%d)", lastRecvCmd);
									ptr = ibuf;
							}

							lastRecvCmd = interfaceCommandNone;

							sprintf(obuf, "EVENT: %s\r\n", ptr);
							write(s, obuf, strlen(obuf));
						} else
						{
							usleep(10000);
						}
					}

					captureInput = 0;

					strcpy(obuf, "Done with input\r\n");
				} else if (strstr(ibuf, "seterrorled ") == ibuf)
				{
					int todo = -1;

					ptr = ibuf+strlen("seterrorled ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					todo = atoi(ptr);

					if (todo >= 0 && todo <= 1)
					{
						sprintf(obuf, "stmclient %d", 11-todo);
						system(obuf);
						sprintf(obuf, "Error led is %s\r\n", todo == 1 ? "on" : "off");
					} else
					{
						sprintf(obuf, "ERROR: Error led cannot be set to %d\r\n", todo);
					}
				} else if (strstr(ibuf, "setstandbyled ") == ibuf)
				{
					int todo = -1;

					ptr = ibuf+strlen("setstandbyled ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					todo = atoi(ptr);

					if (todo >= 0 && todo <= 1)
					{
						sprintf(obuf, "stmclient %d", 4-todo);
						system(obuf);
						sprintf(obuf, "Standby led is %s\r\n", todo == 1 ? "on" : "off");
					} else
					{
						sprintf(obuf, "ERROR: Standby led cannot be set to %d\r\n", todo);
					}
				} else if (strstr(ibuf, "outputmode ") == ibuf)
				{
					int format = -1;

					ptr = ibuf+strlen("outputmode ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					if (strcmp(ptr, "cvbs") == 0 || strcmp(ptr, "composite") == 0)
					{
						format = DSOS_CVBS;
					} else if (strcmp(ptr, "s-video") == 0 || strcmp(ptr, "yc") == 0)
					{
						format = DSOS_YC;
					}

					if (format >= 0)
					{
						gfx_changeOutputFormat(format);
						appControlInfo.outputInfo.format = format;
						sprintf(obuf, "Output mode set to %s\r\n", ptr);
					} else
					{
						sprintf(obuf, "ERROR: Output mode cannot be set to %s\r\n", ptr);
					}
				}
#ifdef ENABLE_DVB
				else if (strcmp(ibuf, "gettunerstatus") == 0)
				{
					if ( offair_tunerPresent() )
					{
						strcpy(obuf, "Tuner present\r\n");
					} else
					{
						strcpy(obuf, "ERROR: Tuner not found\r\n");
					}
				} else if (strstr(ibuf, "dvbchannel ") == ibuf)
				{
					int channel = -1;

					ptr = ibuf+strlen("dvbchannel ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					channel = atoi(ptr);

					if (channel >= 0)
					{
						int index = 0, item = 0;
						int t = 0;

						while (item < dvb_getNumberOfServices())
						{
							EIT_service_t* service = dvb_getService(index);
							if (service == NULL)
							{
								break;
							}
							if (dvb_hasMedia(service))
							{
								if(item == channel) {
									t = offair_getServiceIndex(service);
									if(t < MAX_MEMORIZED_SERVICES) {
										offair_channelChange(interfaceInfo.currentMenu, CHANNEL_INFO_SET(screenMain, t));
										sprintf(obuf, "DVB Channel set to %s - %s\r\n", ptr, dvb_getServiceName(service));
									} else {
										sprintf(obuf, "ERROR: DVB Channel cannot be set to %s\r\n", ptr);
									}
									break;
								}
								item++;
							}
							index++;
						}
					} else
					{
						sprintf(obuf, "ERROR: DVB Channel cannot be set to %s\r\n", ptr);
					}
				} else if (strstr(ibuf, "dvbclear ") == ibuf)
				{
					int todo = -1;

					ptr = ibuf+strlen("dvbclear ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					todo = atoi(ptr);

					if (todo > 0)
					{
						offair_clearServiceList(0);
						sprintf(obuf, "DVB Service list cleared\r\n");
					} else
					{
						sprintf(obuf, "ERROR: Incorrect value %d\r\n", todo);
					}
				} else if (strstr(ibuf, "dvbscan ") == ibuf)
				{
					unsigned long from, to, step, speed;

					ptr = ibuf+strlen("dvbscan ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					if (sscanf(ptr, "%lu %lu %lu %lu", &from, &to, &step, &speed) == 4 &&
						from > 1000 && to > 1000 && step >= 1000 && speed <= 10)
					{
						int index = 0, item = 0;

						appControlInfo.dvbtInfo.fe.lowFrequency = from;
						appControlInfo.dvbtInfo.fe.highFrequency = to;
						appControlInfo.dvbtInfo.fe.frequencyStep = step;
						appControlInfo.dvbCommonInfo.adapterSpeed = speed;

						gfx_stopVideoProviders(screenMain);
						offair_serviceScan(interfaceInfo.currentMenu, NULL);
						sprintf(obuf, "DVB Channels scanned with %s\r\n", ptr);

						while (item < dvb_getNumberOfServices())
						{
							EIT_service_t* service = dvb_getService(index);
							if (service == NULL)
							{
								break;
							}
							if (dvb_hasMedia(service))
							{
								sprintf(&obuf[strlen(obuf)], "%d: %s%s\r\n", item, dvb_getServiceName(service), dvb_getScrambled(service) ? " (scrambled)" : "");
								item++;
							}
							index++;
						}

					} else
					{
						sprintf(obuf, "ERROR: DVB Channels cannot be scanned with %s\r\n", ptr);
					}
				} else if (strstr(ibuf, "dvblist") == ibuf)
				{
					int index = 0, item = 0;

					while (item < dvb_getNumberOfServices())
					{
						EIT_service_t* service = dvb_getService(index);
						if (service == NULL)
						{
							break;
						}
						if (dvb_hasMedia(service))
						{
							sprintf(&obuf[strlen(obuf)], "%d: %s%s\r\n", item, dvb_getServiceName(service), dvb_getScrambled(service) ? " (scrambled)" : "");
							item++;
						}
						index++;
					}
					sprintf(&obuf[strlen(obuf)], "Total %d Channels\r\n", dvb_getNumberOfServices());
				} else if (strstr(ibuf, "dvbcurrent") == ibuf)
				{
					if (appControlInfo.dvbInfo.active)
					{
						int index = 0, item = 0;

						while(item < dvb_getNumberOfServices()) {
							EIT_service_t* service = dvb_getService(index);
							if(service == NULL) {
								break;
							}
							if(dvb_hasMedia(service)) {
								if(service == offair_getService(appControlInfo.dvbInfo.channel)) {
									sprintf(obuf, "%d: %s%s\r\n", item, dvb_getServiceName(service), dvb_getScrambled(service) ? " (scrambled)" : "");
									break;
								}
								item++;
							}
							index++;
						}
					} else
					{
						sprintf(obuf, "Not playing\r\n");
					}
				}
#endif /* ENABLE_DVB */
				else if (strstr(ibuf, "playvod ") == ibuf)
				{
					ptr = ibuf+strlen("playvod ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					if (strstr(ptr, "rtsp://") != NULL && rtsp_playURL(screenMain, ptr, NULL, NULL) == 0)
					{
						sprintf(obuf, "VOD URL set to %s\r\n", ptr);
					} else
					{
						sprintf(obuf, "ERROR: VOD URL cannot be set to %s\r\n", ptr);
					}
				} else if (strstr(ibuf, "playrtp ") == ibuf)
				{
					ptr = ibuf+strlen("playrtp ");

					while (*ptr != 0 && *ptr == ' ')
					{
						ptr++;
					}

					if (strstr(ptr, "://") != NULL && rtp_playURL(screenMain, ptr, NULL, NULL) == 0)
					{
						sprintf(obuf, "RTP URL set to %s\r\n", ptr);
					} else
					{
						sprintf(obuf, "ERROR: RTP URL cannot be set to %s\r\n", ptr);
					}
				} else if (strcmp(ibuf, "stop") == 0 || strcmp(ibuf, "quit") == 0)
				{
					gfx_stopVideoProviders(screenMain);
					strcpy(obuf, "stopped\r\n");
					interface_showMenu(1, 1);
				} else if (strcmp(ibuf, "exit") == 0 || strcmp(ibuf, "quit") == 0)
				{
					break;
				} else if (strcmp(ibuf, "isactive") == 0)
				{
					if (gfx_videoProviderIsActive( screenMain )) {
						sprintf(obuf, "active");
					} else {
						sprintf(obuf, "notactive");
					}
				} else if (strcmp(ibuf, "iprenew") == 0)
				{
					interface_showMessageBox(_T("RENEW_IN_PROGRESS"), settings_renew, 5000);
				}

#ifdef ENABLE_MESSAGES
				else if (strncmp(ibuf, "newmsg", 6) == 0)
				{
					appControlInfo.messagesInfo.newMessage = messages_checkNew();
					if (appControlInfo.messagesInfo.newMessage)
#ifdef MESSAGES_NAGGING
					messages_showFile(appControlInfo.messagesInfo.newMessage);
#else
					interface_displayMenu(1);
#endif
				}
#endif // ENABLE_MESSAGES
#ifdef STSDK
				else if (strncmp(ibuf, "has_update", 10) == 0) {
					output_onUpdate(1);
				}
				else if (strncmp(ibuf, "no_update", 9) == 0) {
					output_onUpdate(0);
				}
#endif
				else
				{
#ifdef TEST_SERVER_INET
					strcpy(obuf, "ERROR: Unknown command\r\n");
#endif
				}

				write(s, obuf, strlen(obuf)+1);
			} else if (pos >= (int)sizeof(ibuf)-1)
			{
				pos = 0;
			}
		}

		eprintf("App: Server disconnect client\n");
		close(s);
	}

	eprintf("App: Server close socket\n");
	close(sock);

	dprintf("%s: Server stop\n", __FUNCTION__);

	return NULL;
}
#endif // #ifdef ENABLE_TEST_SERVER

static int getActiveMedia(){

	if (appControlInfo.rtpInfo.active != 0)
		return mediaProtoRTP;
	if (appControlInfo.rtspInfo.active != 0)
		return mediaProtoRTSP;
	if (appControlInfo.mediaInfo.active != 0 && appControlInfo.mediaInfo.bHttp != 0)
		return mediaProtoHTTP;

	return -1;
}

void *keyThread(void *pArg)
{
	IDirectFBEventBuffer *eventBuffer = (IDirectFBEventBuffer *)pArg;
	DFBInputDeviceKeySymbol lastsym;
	int allow_repeat = 0;
	unsigned long timediff;
	struct timeval lastpress, currentpress;
	interfaceCommand_t lastcmd;
	interfaceCommandEvent_t curcmd;

	keyThreadActive = 1;

	lastsym = 0;
	lastcmd = interfaceCommandNone;
	curcmd.command = interfaceCommandNone;
	curcmd.repeat = 0;
	curcmd.source = DID_KEYBOARD;
	memset(&lastpress, 0, sizeof(struct timeval));

//#ifndef DEBUG
	if(appControlInfo.playbackInfo.bResumeAfterStart)
	{
		int res = 0;
		switch( appControlInfo.playbackInfo.streamSource )
		{
#ifdef ENABLE_DVB
			case streamSourceDVB:
				eprintf("App: Autostart %d DVB channel\n", appControlInfo.dvbInfo.channel);
				res = offair_channelChange((interfaceMenu_t*)&interfaceMainMenu,
					CHANNEL_INFO_SET(screenMain,appControlInfo.dvbInfo.channel));
				break;
#endif
			case streamSourceIPTV:
				{
					char desc[MENU_ENTRY_INFO_LENGTH];
					char thumb[MAX_URL];

					eprintf("App: Autostart '%s' from IPTV\n", appControlInfo.rtpMenuInfo.lastUrl);
					rtp_getPlaylist(screenMain);
					getParam(SETTINGS_FILE, "LASTDESCRIPTION", "", desc);
					getParam(SETTINGS_FILE, "LASTTHUMBNAIL", "", thumb);
					res = rtp_playURL(screenMain, appControlInfo.rtpMenuInfo.lastUrl, desc[0] ? desc : NULL, thumb[0] ? thumb : 0);
				}
				break;
			case streamSourceUSB:
				{
					eprintf("App: Autostart '%s' from media\n", appControlInfo.mediaInfo.lastFile );
					strcpy( appControlInfo.mediaInfo.filename, appControlInfo.mediaInfo.lastFile );
					getParam(SETTINGS_FILE, "LASTDESCRIPTION", "", appControlInfo.playbackInfo.description);
					getParam(SETTINGS_FILE, "LASTTHUMBNAIL", "",   appControlInfo.playbackInfo.thumbnail);
					appControlInfo.playbackInfo.playingType = media_getMediaType(appControlInfo.mediaInfo.filename);
					interface_showMenu( 0, 0 );
					res = media_streamStart();
				}
				break;
			case streamSourceFavorites:
				eprintf("App: Autostart '%s' from Favorites\n", playlist_getLastURL());
				res = playlist_streamStart();
				break;
#ifdef ENABLE_YOUTUBE
			case streamSourceYoutube:
				eprintf("App: Autostart '%s' from YouTube\n", appControlInfo.mediaInfo.lastFile );
				getParam(SETTINGS_FILE, "LASTDESCRIPTION", "", appControlInfo.playbackInfo.description);
				getParam(SETTINGS_FILE, "LASTTHUMBNAIL", "",   appControlInfo.playbackInfo.thumbnail);
				res = youtube_streamStart();
				break;
#endif
			default: ;
		}
		if( res != 0 )
		{
			appControlInfo.playbackInfo.streamSource = streamSourceNone;
			saveAppSettings();
			interface_showMenu(1, 1);
			//interface_showSplash(0, 0, interfaceInfo.screenWidth, interfaceInfo.screenHeight, 1, 1);
		}
	}
//#endif

	while(keepCommandLoopAlive && keyThreadActive)
	{
		DFBEvent event;
		DFBResult result;

		//dprintf("%s: WaitForEventWithTimeout\n", __FUNCTION__);
		if (flushEventsFlag)
		{
			eventBuffer->Reset(eventBuffer);
		}

		eventBuffer->WaitForEventWithTimeout(eventBuffer, 3, 0);
		flushEventsFlag = 0;

		result = eventBuffer->HasEvent(eventBuffer);
		if (result == DFB_BUFFEREMPTY)
		{
			if (appControlInfo.playbackInfo.autoStop > 0)
			{
				int active_media = getActiveMedia();

				if (active_media < 0)
					continue;
				else
				{
					struct timeval currenttime;
					gettimeofday(&currenttime, NULL);
					int timeout = currenttime.tv_sec-event.input.timestamp.tv_sec;

					if (timeout >= (appControlInfo.playbackInfo.autoStop-AUTOSTOP_WARNING_TIMEOUT)*60
					&& timeout < appControlInfo.playbackInfo.autoStop*60)
					{
						char msg[MENU_ENTRY_INFO_LENGTH];
						snprintf(msg,sizeof(msg), "%s\n%d %s\n%s", _T("PLAYBACK_STOP_MESSAGE"),AUTOSTOP_WARNING_TIMEOUT, _T("MINUTE_SHORT"), _T("PLAYBACK_TO_CANCEL"));
						interface_showMessageBox(msg, thumbnail_warning, 0);
						continue;
					}
					else if (timeout >= appControlInfo.playbackInfo.autoStop*60)
					{
						gfx_stopVideoProviders(screenMain);
						eprintf("App: Autostop broadcasting playback - timeout over %d min\n", appControlInfo.playbackInfo.autoStop);
						interface_hideMessageBox();
						interface_showMenu(1, 1);
					}
					else
						continue;
				}
			} else
				continue;
		}
		if(result == DFB_OK)
		{
			eventBuffer->GetEvent(eventBuffer, &event);
			eventBuffer->Reset(eventBuffer);

			if(!helperIsEventValid(&event))
				continue;

			kprintf("%s: got event, age %d\n", __FUNCTION__, timediff);

			//dprintf("%s: dev_id %d\n", __FUNCTION__, event.input.device_id);

			if ( event.clazz == DFEC_INPUT && (event.input.type == DIET_KEYPRESS || event.input.type == DIET_BUTTONPRESS) )
			{
				/*if ( event.input.key_symbol == DIKS_ESCAPE )
				{
					eprintf("App: User abort. Exiting.\n");
					keepCommandLoopAlive = 0;
					break;
				}*/

				if(checkPowerOff(&event) || appControlInfo.inStandby) {
					//clear event
					eventBuffer->Reset(eventBuffer);
					continue;
				}
#ifdef STSDK
				if(event.input.key_symbol == DIKS_CUSTOM0) { //VFMT
					output_toggleOutputModes();
					continue;
				}
				if(event.input.key_symbol == DIKS_CUSTOM2) { // Source
					output_toggleInputs();
					continue;
				}

				/// TODO : redo: F1 will always disable inputs first
				if(event.input.key_symbol == DIKS_F1) { // interfaceCommandRed, Cancel all inputs
					output_setInput(NULL, INPUT_NONE);
				}
#endif

				memcpy(&currentpress, &event.input.timestamp, sizeof(struct timeval));

				dprintf("%s: Char: %d (%d) %d, '%c' ('%c') did=%d\n", __FUNCTION__, event.input.key_symbol, event.input.key_id, event.input.button, event.input.key_symbol, event.input.key_id, event.input.device_id);

				interfaceCommand_t cmd = parseEvent(&event);

				if (cmd == interfaceCommandCount) {
					continue;
				}

				curcmd.original = cmd;
				curcmd.command = cmd;
				curcmd.source = event.input.device_id == DIDID_ANY ? DID_KEYBOARD : event.input.device_id;
				//curcmd.repeat = 0;

				timediff = (currentpress.tv_sec-lastpress.tv_sec)*1000000+(currentpress.tv_usec-lastpress.tv_usec);

				kprintf("%s: event %d\n", __FUNCTION__, cmd);
				kprintf("%s: timediff %d\n", __FUNCTION__, timediff);

				if(cmd == lastcmd && timediff < REPEAT_TIMEOUT * 2) {
					curcmd.repeat++;
				} else {
					curcmd.repeat = 0;
				}

				if (appControlInfo.inputMode == inputModeABC &&
					event.input.device_id != DID_KEYBOARD && event.input.device_id != DIDID_ANY && /* not keyboard */
					cmd >= interfaceCommand0 &&
					cmd <= interfaceCommand9 )
				{
					int num = cmd - interfaceCommand0;
					static int offset = 0;

					dprintf("%s: DIGIT: %d = %d repeat=%d offset=%d\n", __FUNCTION__, cmd, num, curcmd.repeat, offset);

					curcmd.command = interface_symbolLookup( num, curcmd.repeat, &offset );
				}
				else if (cmd != interfaceCommandVolumeUp &&
					cmd != interfaceCommandVolumeDown &&
					cmd != interfaceCommandUp &&
					cmd != interfaceCommandDown &&
					cmd != interfaceCommandLeft &&
					cmd != interfaceCommandRight
					)
				{
					if ((event.input.key_symbol == lastsym && cmd == lastcmd) && timediff<REPEAT_TIMEOUT && allow_repeat < REPEAT_THRESHOLD)
					{
						dprintf("%s: skip %d\n", __FUNCTION__, allow_repeat);
						allow_repeat++;
						memcpy(&lastpress, &currentpress, sizeof(struct timeval));
						continue;
					} else if ((event.input.key_symbol != lastsym || cmd == lastcmd) || timediff>=REPEAT_TIMEOUT) {
						dprintf("%s: new key %d\n", __FUNCTION__, allow_repeat);
						allow_repeat = 0;
					}
				} else {
					eventBuffer->Reset(eventBuffer);
				}

				memcpy(&lastpress, &currentpress, sizeof(struct timeval));
				lastsym = event.input.key_symbol;
				lastcmd = cmd;

				kprintf("%s: ---> Char: %d, Command %d\n", __FUNCTION__, event.input.key_symbol, cmd);

				interface_processCommand(&curcmd);
			} else if((event.clazz == DFEC_INPUT) && ((event.input.type == DIET_KEYRELEASE) || (event.input.type == DIET_BUTTONRELEASE))) {
#ifdef STSDK
				if(checkPowerOff(&event)) {
					//clear event
					eventBuffer->Reset(eventBuffer);
					continue;
				}
#endif
				if((event.input.device_id == DID_KEYBOARD) || (event.input.device_id == DIDID_ANY)) {
					//dprintf("%s: release!\n", __FUNCTION__);
					lastsym = 0;
					lastcmd = 0;
				}
			}
		}
	}

	dprintf("%s: Command loop stopped\n", __FUNCTION__);

	keyThreadActive = 0;

	return NULL;
}

static struct termio savemodes;
static int havemodes = 0;

static void set_to_raw(void)
{
	struct termio modmodes;
	if(ioctl(fileno(stdin), TCGETA, &savemodes) < 0)
		return;

	havemodes = 1;
	modmodes = savemodes;
	modmodes.c_lflag &= ~ICANON;
	modmodes.c_cc[VMIN] = 1;
	modmodes.c_cc[VTIME] = 0;
	ioctl(fileno(stdin), TCSETAW, &modmodes);
}

static void set_to_buffered(void)
{
	if(!havemodes)
		return;
	ioctl(fileno(stdin), TCSETAW, &savemodes);
}

#ifdef STB225
static void setupFBResolution(char* fbName, int32_t width, int32_t height)
{
	int32_t fd;
	struct fb_var_screeninfo vinfo;
	int32_t error;
	fd = open(fbName, O_RDWR);

	if (fd >= 0)
	{
		/* Get variable screen information */
		error = ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
		if (error != 0)
		{
			eprintf("App: Error reading variable information for '%s' - error code %d\n", fbName, error);
		}

		vinfo.xres = (uint32_t)width;
		vinfo.yres = (uint32_t)height;
		vinfo.activate = FB_ACTIVATE_FORCE;

		/* Set up the framebuffer resolution */
		error = ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo);
		if (error != 0)
		{
			eprintf("App: Error setting variable information for '%s' - error code %d\n", fbName, error);
		}

		(void)close(fd);
	}
}
static void setupFramebuffers(void)
{
	int32_t fd;
	int32_t width = 0;
	int32_t height = 0;
	int32_t multipleFrameBuffers;

	if (appControlInfo.restartResolution)
	{
		multipleFrameBuffers = helperFileExists("/dev/fb1");

		if (multipleFrameBuffers)
		{
			switch (appControlInfo.restartResolution)
			{
				case(DSOR_720_480) :
					{
						width  = 512;
						height = 480;
					}
					break;
				case(DSOR_720_576) :
					{
						width  = 512;
						height = 576;
					}
					break;
				case(DSOR_1280_720) :
					{
						width  = 720;
						height = 720;
					}
					break;
				case(DSOR_1920_1080) :
					{
						width  = 960;
						height = 540;
					}
					break;
				default :
					{
						width  = 0;
						height = 0;
					}
					break;
			}
		}
		else
		{
			switch (appControlInfo.restartResolution)
			{
				case(DSOR_720_480) :
					{
						width  = 720;
						height = 480;
					}
					break;
				case(DSOR_720_576) :
					{
						width  = 720;
						height = 576;
					}
					break;
				case(DSOR_1280_720) :
					{
						width  = 1280;
						height = 720;
					}
					break;
				case(DSOR_1920_1080) :
					{
						width  = 1920;
						height = 1080;
					}
					break;
				default :
					{
						width  = 0;
						height = 0;
					}
					break;
			}
		}

		/* Set up the Directfb display mode file */
		fd = open("/etc/directfbrc", O_RDWR);
		if (fd >= 0)
		{
			char text[128];

			(void)sprintf(text, "mode=%dx%d\n", width, height);
			(void)write(fd, text, strlen(text));
			(void)strcpy(text, "pixelformat=ARGB\n");
			(void)write(fd, text, strlen(text));
			(void)strcpy(text, "depth=32\n");
			(void)write(fd, text, strlen(text));
			(void)strcpy(text, "no-vt-switch\n");
			(void)write(fd, text, strlen(text));
			(void)strcpy(text, "\n");
			(void)write(fd, text, strlen(text));

			(void)close(fd);
		}

		setupFBResolution("/dev/fb0", width, height);

		setupFBResolution("/dev/fb1", width, height);

	}
	{
		char text[256];

		getParam("/etc/init.d/S35pnxcore.sh", "resolution", "1280x720x60p", text);
		if (strstr(text, "1920x1080")!=0 && interfaceInfo.enable3d) {
			width = 1920; height = 1080;
//printf("%s[%d]: %dx%d\n", __FILE__, __LINE__, width, height);
			Stb225ChangeDestRect("/dev/fb1", width/2, 0, width/2, height);
			Stb225ChangeDestRect("/dev/fb0", 0, 0, width, height);
		} /*else 
		if (strstr(text, "1280x720")!=0) {
			width = 1280; height = 720;
		}*/
	}

}
#endif

#ifdef ENABLE_FUSION
#define FUSION_STUB "fusion://stub"
#define FUSION_MIN_USLEEP 40
static int gStatus = 0;

interfaceFusionObject_t FusionObject;

void fusion_ms2timespec(int ms, struct timespec * ts)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec + ms / 1000;
    ts->tv_nsec = tv.tv_usec * 1000 + (ms % 1000) * 1000000;
    if (ts->tv_nsec >= 1000000000)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
}

void fusion_wait (unsigned int timeout_ms)
{
	struct timespec ts;
	pthread_cond_t condition;
	pthread_mutex_t mutex;

	pthread_cond_init(&condition, NULL);
	pthread_mutex_init(&mutex, NULL);

	pthread_mutex_lock(&mutex);

	fusion_ms2timespec(timeout_ms, &ts);
	pthread_cond_timedwait(&condition, &mutex, &ts);
	pthread_mutex_unlock(&mutex);

	pthread_cond_destroy(&condition);
	pthread_mutex_destroy(&mutex);
	return;
}

void * fusion_threadCreepline(void * param)
{
	int result;
	struct timeval tv;

	fusion_readConfig();

	while (1){
		result = fusion_getCreepAndLogo();
		if (result == FUSION_FAIL){
			fusion_wait(FusionObject.checktime * 1000);
			continue;
		}
		else if (result == FUSION_NEW_CREEP){
			gettimeofday(&tv, NULL);
			FusionObject.creepStartTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
			eprintf ("%s(%d): New creep got. Start it.\n", __FUNCTION__, __LINE__);
		}
		else if (result == FUSION_SAME_CREEP && FusionObject.creepShown)
		{
			eprintf ("%s(%d): creepShown got. Wait pause and start again.\n", __FUNCTION__, __LINE__);
			fusion_wait(FusionObject.pause * 1000);
			gettimeofday(&tv, NULL);
			FusionObject.creepStartTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
		}
		fusion_wait(FusionObject.checktime * 1000);
	}

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

#define FUSION_DEFAULT_CHECKTIME     (300)
#define FUSION_DEFAULT_SERVER_PATH   "http://public.tv/api"
#define FUSION_CFGDIR         "/var/etc/elecard/StbMainApp"
#define FUSION_HWCONFIG       FUSION_CFGDIR"/fusion.hwconf"

int fusion_readConfig()
{
	char * ptr;
	char line [512];
	FILE * f;

	FusionObject.checktime = FUSION_DEFAULT_CHECKTIME;
	sprintf (FusionObject.server, "%s", FUSION_DEFAULT_SERVER_PATH);

	f = fopen(FUSION_HWCONFIG, "rt");
	if (!f) return -1;

	while (!feof(f)){
		fgets(line, 256, f);
		if (feof(f)){
			break;
		}
		line[strlen(line)-1] = '\0';
		ptr = NULL;
		if ((ptr = strcasestr((const char*)line, (const char*)"SERVER ")) != NULL){
			ptr += 7;
			sprintf (FusionObject.server, "%s", ptr);
			eprintf (" %s: server = %s\n",   __FUNCTION__, FusionObject.server);
		}else if ((ptr = strcasestr((const char*) line, (const char*)"CHECKTIME ")) != NULL){
			ptr += 10;
			FusionObject.checktime = atoi(ptr);
			eprintf (" %s: checktime = %d\n",   __FUNCTION__, FusionObject.checktime);
		}
	}
	fclose (f);
	return 0;
}

int fusion_refreshEvent(void *pArg)
{
	pthread_mutex_lock(&FusionObject.mutexCreep);
	int needUpdate = (strlen(FusionObject.creepline) > 0) ? 1:0;
	pthread_mutex_unlock(&FusionObject.mutexCreep);

	if (needUpdate){
// 		interface_displayMenu(1);
		interface_displayCreepline();
	}
	interface_addEvent(fusion_refreshEvent, (void*)NULL, 10, 1);
	return 0;
}

void fusion_startup()
{
	fusion_setMoscowDateTime();

	sprintf (appControlInfo.mediaInfo.filename, "%s", FUSION_STUB);

	appControlInfo.playbackInfo.playingType = media_getMediaType(appControlInfo.mediaInfo.filename);
	appControlInfo.mediaInfo.bHttp = 1;

	memset(&FusionObject, 0, sizeof(interfaceFusionObject_t));

	fusion_getSecret();

// 	int result = media_startPlayback();
// 	if (result == 0){
// 		eprintf ("%s(%d): Started %s\n", __FUNCTION__, __LINE__, FUSION_STUB);
// 	}
// 	else {
// 		eprintf ("%s(%d): ERROR! media_startPlayback rets %d\n", __FUNCTION__, __LINE__, result);
// 	}

	pthread_mutex_init(&FusionObject.mutexCreep, NULL);
	pthread_mutex_init(&FusionObject.mutexLogo, NULL);

	interface_addEvent(fusion_refreshEvent, (void*)NULL, 10, 1);

	pthread_create(&FusionObject.threadCreepHandle, NULL, fusion_threadCreepline, (void*)NULL);

	return;
}

int fusion_getSecret ()
{
	char mac [16];
	char input[64];
	char output[64];
	int i;
	
	mac[0] = 0;
	input[0] = 0;
	output[0] = 0;
	memset (FusionObject.secret, 0, sizeof(FusionObject.secret));

	FILE *f = popen ("cat /sys/class/net/eth0/address | tr -d ':'", "r");
	if (!f) return -1;

	fgets(mac, sizeof(mac), f);
	pclose(f);
	mac[strlen(mac) - 1] = '\0';

	strcpy(input, FUSION_SECRET);
	strcat(input, mac);

	/* Get MD5 sum of input and convert it to hex string */
	md5((unsigned char*)input, strlen(input), (unsigned char*)output);
	for (i = 0; i < 16; i++)
	{
		sprintf(&FusionObject.secret[i*2], "%02hhx", output[i]);
	}

	return 0;
}

static char fusion_curlError[CURL_ERROR_SIZE];
int fusion_streamPos; 

size_t fusion_curlCallback(char * data, size_t size, size_t nmemb, void * stream)
{
	int writtenBytes = size * nmemb;
	memcpy ((char*)stream + fusion_streamPos, data, writtenBytes);
	fusion_streamPos += writtenBytes;
	return writtenBytes;
}

typedef size_t (*curlWriteCB)(void *buffer, size_t size, size_t nmemb, void *userp);

CURLcode fusion_getDataByCurl (char * url, char * curlStream, int * pStreamLen, curlWriteCB cb)
{
	CURLcode retCode = CURLE_OK;
	CURL * curl = NULL;
	struct curl_slist  *headers = NULL;

	if (!url || !strlen(url)) return -1;

	curl = curl_easy_init();
	if (!curl) return -1;

	*pStreamLen = 0;
	fusion_streamPos = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, fusion_curlError);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curlStream);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	eprintf ("%s: rq: %s\n", __FUNCTION__, url);

	retCode = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl = NULL;
#ifdef FUSION_TEST
	char cuttedStream[256];
	snprintf (cuttedStream, 256, "%s", curlStream);
	eprintf ("ans: %s ...\n", cuttedStream);
	//eprintf ("ans: %s\n", playlistBuffer);
#endif

	if (retCode != CURLE_OK && retCode != CURLE_WRITE_ERROR)
	{
		eprintf ("%s: ERROR! %s\n", __FUNCTION__, fusion_curlError);
		return retCode;
	}
	if (!curlStream || strlen(curlStream) == 0) {
		eprintf ("%s: ERROR! empty response.\n", __FUNCTION__);
		return -2;
	}
	*pStreamLen = fusion_streamPos;
	return CURLE_OK;
}

int fusion_downloadPlaylist(char * url, cJSON ** ppRoot)
{
	char * playlistBuffer = NULL;
	int res;

	int buflen;

	if (!url || !strlen(url)) return -1;
	playlistBuffer = (char *)malloc(FUSION_STREAM_SIZE);
	if (!playlistBuffer){
		eprintf ("%s(%d): ERROR! Malloc failed.\n",   __FUNCTION__, __LINE__);
		return -1;
	}
	memset(playlistBuffer, 0, FUSION_STREAM_SIZE);

	eprintf ("%s(%d): rq: %s ...\n",   __FUNCTION__, __LINE__, url);
	fusion_getDataByCurl(url, playlistBuffer, &buflen, (curlWriteCB)fusion_curlCallback);

#ifdef FUSION_TEST
	char cuttedStream[256];
	snprintf (cuttedStream, 256, "%s", playlistBuffer);
	eprintf ("ans: %s ...\n", cuttedStream);
	//eprintf ("ans: %s\n", playlistBuffer);
#endif
	
	if (strlen(playlistBuffer) == 0) {
		eprintf (" %s: ERROR! empty response.\n",   __FUNCTION__);
		free(playlistBuffer);
		return -1;
	}
	res = fusion_checkResponse(playlistBuffer, ppRoot);
	free(playlistBuffer);
	return res;
}

int fusion_checkResponse (char * curlStream, cJSON ** ppRoot)
{
	*ppRoot = cJSON_Parse(curlStream);
	if (!(*ppRoot)) {
		char cuttedStream[256];
		snprintf (cuttedStream, 256, curlStream);
		eprintf ("%s: ERROR! Not a JSON response. %s\n",   __FUNCTION__, cuttedStream);
		return -1;
	}
	return 0;
}

int fusion_setMoscowDateTime()
{
	char setDateString[64];
	char dateString[64];
	char utcBuffer [64];
	memset(utcBuffer, 0, 64);

	if (fusion_getUtc(utcBuffer) != 0) {
		eprintf ("%s(%d): WARNING! Couldn't get UTC datetime.\n", __FUNCTION__, __LINE__);
		return -1;
	}
	// format UTC: 2014-04-04 11:43:48
	sprintf (setDateString, "date -u -s \"%s\"", utcBuffer);
	eprintf ("%s: Command: %s\n", __FUNCTION__, setDateString);
	system(setDateString);

	// set timezone
	system("rm /var/etc/localtime && ln -s /usr/share/zoneinfo/Russia/Moscow /var/etc/localtime");

	return 0;
}


int fusion_getUtc (char * utcBuffer)
{
	FILE * f;
	char request[256];

	if (!utcBuffer) {
		eprintf ("%s(%d): WARNING! Invalid arg.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	sprintf (request, "wget \"http://www.timeapi.org/utc/now?format=%%25Y-%%25m-%%25d%%20%%20%%25H:%%25M:%%25S\" -O /tmp/utc.txt");  // 2>/dev/null
	eprintf ("%s(%d): rq:  %s...\n",   __FUNCTION__, __LINE__, request);
	system (request);

	f = fopen("/tmp/utc.txt", "rt");
	if (!f) {
		eprintf ("%s(%d): ERROR! Couldn't open /tmp/utc.txt.\n",   __FUNCTION__, __LINE__);
		return -1;
	}
	fread(utcBuffer, 64, 1, f);
	fclose(f);

	eprintf ("ans: %s\n", utcBuffer);

	if (!strlen(utcBuffer) || !strchr(utcBuffer, ' ')) {
		eprintf ("%s(%d): WARNING! Incorrect answer: %s\n", __FUNCTION__, __LINE__, utcBuffer);
		return -1;
	}
	return 0;
}

int32_t fusion_checkDirectory(const char *path)
{
	DIR *d;

	d = opendir(path);
	if (d == NULL)
		return 0;
	closedir(d);
	return 1;
}

long fusion_getRemoteFileSize(char * url)
{
	CURL *curl;
	CURLcode res;
	double remoteFileSize = 0;
	if (!url || strlen(url) == 0) return 0;

	curl = curl_easy_init();
	if (!curl) return 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

	curl_easy_perform(curl);
	res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &remoteFileSize);
	if (res != 0) {
		remoteFileSize = -1;
	}
	curl_easy_cleanup(curl);

	eprintf ("%s: RemoteFileSize = %ld for %s...\n", __FUNCTION__, (long)remoteFileSize, url);
	return (long)remoteFileSize;
}

int fusion_getCreepAndLogo ()
{
	cJSON * root;
	char request[FUSION_URL_LEN];
	unsigned int i;
	time_t now;
	struct tm nowDate;
	int result = 0;

	if (!fusion_checkDirectory("/tmp/fusion")) {
		system("mkdir /tmp/fusion");
	}

	time (&now);
	nowDate = *localtime (&now);
	nowDate.tm_year += 1900;
	nowDate.tm_mon += 1;
	sprintf (request, "%s/?s=%s&c=playlist_full&date=%04d-%02d-%02d", 
	         FusionObject.server, FusionObject.secret, 
	         nowDate.tm_year, nowDate.tm_mon, nowDate.tm_mday);

	if (fusion_downloadPlaylist(request, &root) != 0) {
		eprintf ("%s(%d): WARNING! fusion_downloadPlaylist rets error.\n",   __FILE__, __LINE__);
		return FUSION_FAIL;
	}

	cJSON * jsonLogo = cJSON_GetObjectItem(root, "logo");
	if (jsonLogo){
		pthread_mutex_lock(&FusionObject.mutexLogo);

		FusionObject.logoCount = cJSON_GetArraySize(jsonLogo);
		if (FusionObject.logoCount > FUSION_MAX_LOGOS){
			FusionObject.logoCount = FUSION_MAX_LOGOS;
		}
		for (i=0; i<FusionObject.logoCount; i++){
			cJSON * jsonItem = cJSON_GetArrayItem(jsonLogo, i);
			if (!jsonItem || !jsonItem->string || !jsonItem->valuestring) continue;

			if (!strcasecmp(jsonItem->string, FUSION_TOP_LEFT_STR)){
				FusionObject.logos[i].position = FUSION_TOP_LEFT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_TOP_RIGHT_STR)){
				FusionObject.logos[i].position = FUSION_TOP_RIGHT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_BOTTOM_LEFT_STR)){
				FusionObject.logos[i].position = FUSION_BOTTOM_LEFT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_BOTTOM_RIGHT_STR)){
				FusionObject.logos[i].position = FUSION_BOTTOM_RIGHT;
			}
			else continue;

			sprintf (FusionObject.logos[i].url, "%s", jsonItem->valuestring);
			eprintf ("%s(%d): logo[%d] = %s\n", __FUNCTION__, __LINE__, i, FusionObject.logos[i].url);

			long logoFileSize = fusion_getRemoteFileSize(FusionObject.logos[i].url);

			if (logoFileSize == -1){	// failed to get remote size
				FusionObject.logos[i].filepath[0] = '\0';
				continue;
			}
			char tmpStr[PATH_MAX];
			char * ptr, *ptrSlash;
			sprintf (tmpStr, "%s", FusionObject.logos[i].url);
			ptr = strstr(tmpStr, "http:");
			if ((ptr != NULL) && (logoFileSize > 0)) {
				ptr += 7;
				while ((ptrSlash = strchr(ptr, '/')) != NULL) ptrSlash[0] = '_';
				sprintf (FusionObject.logos[i].filepath, "/tmp/fusion/logo_%d_%s", logoFileSize, ptr);
			}
			else {
				eprintf ("%s(%d): WARNING! Incorrect logo url\n", __FUNCTION__, __LINE__); 
				FusionObject.logos[i].filepath[0] = '\0';
			}
			eprintf ("%s(%d): logo[%d] = %s, position = %d, path = %s\n", __FUNCTION__, __LINE__, 
				i, FusionObject.logos[i].url, FusionObject.logos[i].position, FusionObject.logos[i].filepath);
			
			// get local file size and compare
			// dont donwload if we have logo on flash
			long int localFileSize = 0;
			FILE * f = fopen(FusionObject.logos[i].filepath, "rb");
			if (f) {
				fseek(f, 0, SEEK_END);
				localFileSize = ftell(f);
				fclose(f);
			}
			if (localFileSize != logoFileSize){
				char cmd[128];
				//sprintf (cmd, "wget \"%s\" -O %s 2>/dev/null", url, logoPath);
				sprintf (cmd, "rm -f \"%s\"", FusionObject.logos[i].filepath);
				system(cmd);
				sprintf (cmd, "wget \"%s\" -O %s", FusionObject.logos[i].url, FusionObject.logos[i].filepath);
				system(cmd);
			}
		}
		pthread_mutex_unlock(&FusionObject.mutexLogo);
		interface_displayMenu(1);
	}

	result = FUSION_SAME_CREEP;
	//cJSON * jsonCreep = cJSON_GetObjectItem(root, "creep\u00adline");
	cJSON * jsonCreep = cJSON_GetObjectItem(root, "creep-line");
	if (jsonCreep){
		cJSON * jsonItem = cJSON_GetArrayItem(jsonCreep, 0);
		if (jsonItem){
			cJSON * jsonText = cJSON_GetObjectItem(jsonItem, "text");
			if (jsonText) {
				cJSON * jsonPause = cJSON_GetObjectItem(jsonItem, "pause");
				if (!jsonPause) FusionObject.pause = FUSION_DEFAULT_CREEP_PAUSE;
				else {
					FusionObject.pause = atoi(jsonPause->valuestring);
				}
				cJSON * jsonRepeats = cJSON_GetObjectItem(jsonItem, "count");
				if (!jsonRepeats) FusionObject.repeats = FUSION_DEFAULT_CREEP_REPEATS;
				else {
					FusionObject.repeats = atoi(jsonRepeats->valuestring);
				}
				if (strlen(FusionObject.creepline) != strlen(jsonText->valuestring)) result = FUSION_NEW_CREEP;
				snprintf (FusionObject.creepline, FUSION_MAX_CREEPLEN, "%s", jsonText->valuestring);

				for (int k=0; k<FUSION_MAX_CREEPLEN; k++){
					if (FusionObject.creepline[k] == '\n' || FusionObject.creepline[k] == '\r') {
						FusionObject.creepline[k] = ' ';
					}
				}
				pgfx_font->GetStringWidth(pgfx_font, FusionObject.creepline, -1, &FusionObject.creepWidth);
				eprintf ("%s(%d): creepline = %s, pause = %d, repeats = %d\n", __FUNCTION__, __LINE__, FusionObject.creepline, FusionObject.pause, FusionObject.repeats);
			}
		}
	}
	cJSON_Delete(root);
	return result;
}

void fusion_cleanup()
{
	if (FusionObject.threadCreepHandle){
		pthread_cancel(FusionObject.threadCreepHandle);
		pthread_join(FusionObject.threadCreepHandle, NULL);
	}

	pthread_mutex_destroy(&FusionObject.mutexCreep);
	pthread_mutex_destroy(&FusionObject.mutexLogo);
}
#endif

void initialize(int argc, char *argv[])
{

	tzset();
	//dprintf("%s: wait framebuffer\n", __FUNCTION__);

	/* Wait for the framebuffer to be installed */
	while(!helperFileExists(FB_DEVICE)) {
		sleep(1);
	}

	//dprintf("%s: open terminal\n", __FUNCTION__);

	flushEventsFlag = 0;
	keepCommandLoopAlive = 1;

	l10n_init(l10n_currentLanguage);

	parse_commandLine(argc, argv);

#ifdef STB225
	Stb225initIR();
	setupFramebuffers();
#endif
#ifdef STSDK
	st_init();
#endif

#ifdef ENABLE_DVB
	dvb_init();
#endif

	downloader_init();
#ifdef ENABLE_GSTREAMER
	gst_init (&argc, &argv);
	gstreamer_init();
#endif
	gfx_init(argc, argv);

	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);
	//signal(SIGSEGV, signal_handler);

	signal(SIGBUS, stub_signal_handler);
	signal(SIGPIPE, stub_signal_handler);
	signal(SIGHUP, hup_signal_handler);

#ifdef STB82
	if(appControlInfo.watchdogEnabled)
	{
		watchdog_init();
	}
#endif
	sound_init();

	interface_init();
	menu_init();

	// Menus should already be initialized
	signal(SIGUSR1, usr1_signal_handler);

#ifdef ENABLE_PVR
	pvr_init();
#endif

	gfx_startEventThread();

#ifdef ENABLE_TEST_SERVER
	pthread_create(&server_thread, NULL, testServerThread, NULL);
#endif

	{
		FILE *f = fopen(APP_LOCK_FILE, "w");
		if (f != NULL)
		{
			fclose(f);
		}
	}

#ifdef ENABLE_SECUREMEDIA
	SmPlugin_Init("");
#endif

#ifdef ENABLE_VOIP
	voip_init();
#endif

#ifdef ENABLE_FUSION
	fusion_startup();
#endif

	if(gAllowConsoleInput) {
		set_to_raw();
	}
}

void cleanup()
{
#ifdef ENABLE_TEST_SERVER
	pthread_join(server_thread, NULL);
#endif

	interfaceInfo.cleanUpState	=	1;

	dprintf("%s: Show main menu\n", __FUNCTION__);

	interface_menuActionShowMenu(interfaceInfo.currentMenu, (void*)&interfaceMainMenu);

	dprintf("%s: release event buffer\n", __FUNCTION__);

	gfx_stopEventThread();

	dprintf("%s: close video providers\n", __FUNCTION__);

	gfx_stopVideoProviders(screenMain);
	media_slideshowStop(1);

	/*
	gfx_clearSurface(pgfx_frameBuffer, interfaceInfo.screenWidth, interfaceInfo.screenHeight);
	gfx_flipSurface(pgfx_frameBuffer);
	gfx_clearSurface(pgfx_frameBuffer, interfaceInfo.screenWidth, interfaceInfo.screenHeight);
	gfx_flipSurface(pgfx_frameBuffer);
	*/
#ifdef ENABLE_VOIP
	voip_cleanup();
#endif

#ifdef ENABLE_SECUREMEDIA
	SmPlugin_Finit();
#endif

	menu_cleanup();

	sound_term();

#ifdef ENABLE_DLNA
	dlna_stop();
#endif

	dprintf("%s: terminate gfx\n", __FUNCTION__);

	gfx_terminate();
#ifdef STSDK
	st_terminate();
#endif
#ifdef ENABLE_GSTREAMER
	gstreamer_terminate();
#endif

	dprintf("%s: terminate interface\n", __FUNCTION__);

	interface_destroy();

#ifdef ENABLE_PVR
	pvr_cleanup();
#endif

#ifdef ENABLE_DVB
	dprintf("%s: terminate dvb\n", __FUNCTION__);
	dvb_terminate();
#endif

#ifdef ENABLE_VIDIMAX
	eprintf ("vidimax_cleanup\n");
	vidimax_cleanup();	
#endif

	l10n_cleanup();

	downloader_cleanup();

	dprintf("%s: close console\n", __FUNCTION__);
	if(gAllowConsoleInput) {
		set_to_buffered();
	}

#ifdef STB82
	stb820_terminate();

	dprintf("%s: stop watchdog\n", __FUNCTION__);
	if(appControlInfo.watchdogEnabled)
	{
		eprintf("App: Stopping watchdog ...\n");
		watchdog_deinit();
	}
#endif

	unlink(APP_LOCK_FILE);
}

void process()
{
	DFBEvent event;

	event.clazz = DFEC_INPUT;
	event.input.type = DIET_KEYPRESS;
	event.input.flags = DIEF_KEYSYMBOL;
	event.input.key_symbol = 0;

	while (keepCommandLoopAlive)
	{
		//dprintf("%s: process...\n", __FUNCTION__);
		if (!gAllowConsoleInput)
		{
			sleep(1);
		} else
		{
			int value;
			fd_set rdesc;
			struct timeval tv;


			FD_ZERO(&rdesc);
			FD_SET(0, &rdesc); // stdin
			tv.tv_sec = 1;
			tv.tv_usec = 0;

			//dprintf("%s: Waiting for terminal input...\n", __FUNCTION__);

			value = select(1, &rdesc, 0, 0, &tv);

			//dprintf("%s: Select returned %d, keepCommandLoopAlive is %d and 0 isset: %d\n", __FUNCTION__, value, keepCommandLoopAlive, FD_ISSET(0, &rdesc));

			if (value <= 0)
			{
				continue;
			}

			value = getchar();
			value = tolower((char)value);
			event.input.key_symbol = 0;

			//dprintf("%s: Got char: %X\n", __FUNCTION__, value);

			switch (value)
			{
			case(0x1B) : /* escape! */
				{
					int escapeValue[2];

					/* Get next 2 values! */
					escapeValue[0] = getchar();
					if( escapeValue[0] == 0x1B )
					{
						event.input.key_symbol = DIKS_ESCAPE;
						break;
					}
					escapeValue[1] = getchar();

					//dprintf("%s: ESC: %X %X\n", __FUNCTION__, escapeValue[0], escapeValue[1]);

					if (escapeValue[0] == 0x5B)
					{
						switch (escapeValue[1])
						{
						case(0x31) : event.input.key_symbol = DIKS_HOME; break;
						case(0x35) : event.input.key_symbol = DIKS_PAGE_UP; break;
						case(0x36) : event.input.key_symbol = DIKS_PAGE_DOWN; break;
						case(0x41) : event.input.key_symbol = DIKS_CURSOR_UP; break;
						case(0x42) : event.input.key_symbol = DIKS_CURSOR_DOWN; break;
						case(0x43) : event.input.key_symbol = DIKS_CURSOR_RIGHT; break;
						case(0x44) : event.input.key_symbol = DIKS_CURSOR_LEFT; break;
						}
					} else if (escapeValue[0] == 0x4F)
					{
						switch (escapeValue[1])
						{
						case(0x50) : event.input.key_symbol = DIKS_F1; break;
						case(0x51) : event.input.key_symbol = DIKS_F2; break;
						case(0x52) : event.input.key_symbol = DIKS_F3; break;
						case(0x53) : event.input.key_symbol = DIKS_F4; break;
						}
					}
					break;
				}
			case(0x8) :
			case(0x7F): event.input.key_symbol = DIKS_BACKSPACE; break;
			case(0xA) : event.input.key_symbol = DIKS_ENTER; break;
			case(0x20): event.input.key_symbol = DIKS_MENU; break;
			case('+') : event.input.key_symbol = DIKS_VOLUME_UP; break;
			case('-') : event.input.key_symbol = DIKS_VOLUME_DOWN; break;
			case('*') : event.input.key_symbol = DIKS_MUTE; break;
			case('i') : event.input.key_symbol = DIKS_INFO; break;
			case('r') : event.input.key_symbol = DIKS_RECORD; break;
			case('0') : event.input.key_symbol = DIKS_0; break;
			case('1') : event.input.key_symbol = DIKS_1; break;
			case('2') : event.input.key_symbol = DIKS_2; break;
			case('3') : event.input.key_symbol = DIKS_3; break;
			case('4') : event.input.key_symbol = DIKS_4; break;
			case('5') : event.input.key_symbol = DIKS_5; break;
			case('6') : event.input.key_symbol = DIKS_6; break;
			case('7') : event.input.key_symbol = DIKS_7; break;
			case('8') : event.input.key_symbol = DIKS_8; break;
			case('9') : event.input.key_symbol = DIKS_9; break;
			}
			if(event.input.key_symbol != 0) {
				dprintf("%s: '%c' [%d]\n", __FUNCTION__, value, value);
				event.input.flags = DIEF_KEYSYMBOL;
				gettimeofday(&event.input.timestamp, 0);
				appEventBuffer->PostEvent(appEventBuffer, &event);
			}
		}
	}

	dprintf("%s: process finished\n", __FUNCTION__);
}

int helperStartApp(const char* filename)
{
	strcpy(startApp, filename);

	raise(SIGINT);

	return 0;
}

#ifdef ENABLE_BROWSER
int is_open_browserAuto()
{
#ifdef STBPNX
	char buf[MENU_ENTRY_INFO_LENGTH];

	getParam(DHCP_VENDOR_FILE, "A_homepage", "", buf);

	if(strstr(buf,"://") != NULL)
	{
		return 1;
	}

	getParam(BROWSER_CONFIG_FILE, "AutoLoadingMW", "OFF", buf);
	if(buf[0]==0)
		return 0;
	if(strcmp(buf,"ON") == 0)
		return 1;
#endif
	return 0;
}

int open_browserAuto()
{
#ifdef STBPNX
	char buf[MENU_ENTRY_INFO_LENGTH];
	char open_link[MENU_ENTRY_INFO_LENGTH];

	getParam(DHCP_VENDOR_FILE, "A_homepage", "", buf);
	
	if(strstr(buf,"://") == NULL)
	{
		getParam(BROWSER_CONFIG_FILE, "AutoLoadingMW", "OFF", buf);
		if(buf[0]==0)
			return 0;
		if(strcmp(buf,"ON"))
			return 0;

		getParam(BROWSER_CONFIG_FILE, "HomeURL", "", buf);
	}

	if(buf[0]!=0)
	{
		strcpy(&buf[strlen(buf)]," LoadInMWMode");
		sprintf(open_link,"while [ ! -f /usr/local/webkit/_start.sh ]; do echo wait userfs; sleep 1; done; /usr/local/webkit/_start.sh \"%s\"",buf);
	} else
	{
		sprintf(open_link,"while [ ! -f /usr/local/webkit/_start.sh ]; do echo wait userfs; sleep 1; done; /usr/local/webkit/_start.sh");
	}
	eprintf("App: Browser cmd: '%s' \n", open_link);

	{
		FILE *f = fopen(APP_LOCK_FILE, "w");
		if (f != NULL)
		{
			fclose(f);
		}
	}

	system(open_link);
#endif
	return 1;
}
#endif // ENABLE_BROWSER

void tprintf(const char *str, ...)
{
	if (gAllowTextMenu)
	{
		va_list va;
		va_start(va, str);
		vprintf(str, va);
		va_end(va);
	}
}

int main(int argc, char *argv[])
{
	int restart, i;
	FILE *pidfile;

	pidfile = fopen("/var/run/mainapp.pid", "w");
	if(pidfile) {
		fprintf(pidfile, "%d", getpid());
		fclose(pidfile);
	}

	appInfo_init();

#ifdef ENABLE_BROWSER
	open_browserAuto();
#endif
/*
	npt_range range = { 5.0f, 10.0f };
	prtspConnection connection;
	if (smrtsp_easy_init(&connection, "192.168.200.1", 554, "football2.mpg") == 0)
	{
		smrtsp_play_range_scale(connection, &range, 2.0f);
	}
*/
	/* By default send all output to stdout, except for error and warning output */
	for(i = 0; i < errorLevelCount; i++) {
		setoutput((errorLevel_t)i, (i == errorLevelError || i == errorLevelWarning) ? stderr : stdout);
	}
#ifdef DEBUG
	setoutputlevel(errorLevelDebug);
#else
	setoutputlevel(errorLevelNormal);
#endif

	do {
		restart = 0;

		//dprintf("%s: initialize\n", __FUNCTION__);
		initialize(argc, argv);
		//dprintf("%s: process\n", __FUNCTION__);

		process();
		//dprintf("%s: Wait for key thread\n", __FUNCTION__);

		while(keyThreadActive != 0) {
			usleep(100000);
		}

		//dprintf("%s: Do cleanup\n", __FUNCTION__);
		cleanup();

		if(startApp[0] != 0) {
			eprintf("App: Starting %s\n", startApp);
			//sleep(1);
#ifdef ENABLE_BROWSER
			if(!is_open_browserAuto())
#endif
				system(startApp);
			startApp[0] = 0;
			/*restart = 1;
			eprintf("App: Resuming...\n");
			sleep(1);*/
		}

	} while(restart);

	unlink("/var/run/mainapp.pid");
	eprintf("App: Goodbye.\n");

	return 0;
}

char *helperEthDevice(int i)
{
	static char temp[64];

	switch( i )
	{
#ifdef ENABLE_PPP
		case ifacePPP:
			strcpy(temp, "ppp0");
			break;
#endif
#ifdef ENABLE_WIFI
		case ifaceWireless:
			strcpy(temp, "wlan0");
			break;
#endif
#ifdef STSDK
		case ifaceLAN:
			sprintf(temp, "br0");
			break;
#endif
		case ifaceWAN:
			if( output_isBridge() )
			{
				sprintf(temp, "br%d", i);
				break;
			}
			// fall through
		default:
			sprintf(temp, "eth%d", i);
			break;
	}
	return temp;
}

char *helperStrCpyTrimSystem(char *dst, char *src)
{
	char *ptr = dst;
	while(*src) {
		if((unsigned char)(*src) > 127) {
			*ptr++ = *src;
		} else if( *src >= ' ' ) {
			switch(*src) {
				case '"': case '*': case '/': case ':': case '|':
				case '<': case '>': case '?': case '\\': case 127: break;
				default: *ptr++ = *src;
			}
		}
		src++;
	}
	*ptr = 0;
	return dst;
}

int helperSafeStrCpy( char** dest, const char* src )
{
	if(NULL == dest) {
		return -2;
	}

	if(NULL == src) {
		if(*dest) {
			free(*dest);
			*dest = NULL;
		}
		return 0;
	}

	size_t src_length = strlen(src);
	if(NULL == *dest) {
		if(NULL == (*dest = (char*)dmalloc(src_length + 1))) {
			return 1;
		}
	} else {
		if(strlen(*dest ) < src_length) {
			char *new_str = (char*)drealloc(*dest, src_length + 1);
			if(NULL == new_str) {
				free(*dest);
				*dest = NULL;
				return 1; // we don't want old content to stay
			}
			*dest = new_str;
		}
	}
	memcpy(*dest, src, src_length + 1);

	return 0;
}

int helperCheckUpdates()
{
	char cmd[MAX_URL];
	char *str, *url, *ptr;
	FILE *file = NULL;
	int ret;
	url_desc_t url_desc;

	str = cmd + sprintf(cmd, "clientUpdater -cs");
	if(str < cmd) {
		return 1;
	}

	file = popen("hwconfigManager a -1 UPURL 2>/dev/null | grep \"VALUE:\" | sed 's/VALUE: \\(.*\\)/\\1/'","r");
	if(file) {
		url = str + (sizeof(" -h ")-1);
		if(fgets(url, MAX_URL-sizeof("clientUpdater -cs -h "), file) != NULL && url[0] != 0 && url[0] != '\n') {
			for( ptr = &url[strlen(url)]; *ptr <= ' '; *ptr-- = 0 );
			if(parseURL(url, &url_desc) == 0 && (url_desc.protocol == mediaProtoFTP || url_desc.protocol == mediaProtoHTTP)) {
				strncpy(str, " -h ", sizeof(" -h ")-1);
			}
		}
		dprintf("%s: Update url: '%s'\n", __FUNCTION__, url);
		fclose(file);
	}

	dprintf("%s: Checking updates: '%s'\n", __FUNCTION__, cmd);
	ret = system(cmd);

	return WIFEXITED(ret) == 1 && WEXITSTATUS(ret) == 1;
}

#ifndef STSDK
static int helper_confirmFirmwareUpdate(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	if(cmd->command == interfaceCommandRed || cmd->command == interfaceCommandExit || cmd->command == interfaceCommandLeft) {
		return 0;
	} else if (cmd->command == interfaceCommandGreen || cmd->command == interfaceCommandEnter || cmd->command == interfaceCommandOk) {
		system("sync");
		system("reboot");
		return 0;
	}
	return 1;
}
#endif

inline time_t gmktime(struct tm *t)
{
	time_t local_time;
	struct tm *pt;
	time(&local_time);
	pt = localtime(&local_time);
	return mktime(t) - timezone + (pt && pt->tm_isdst > 0 ? 3600 : 0); // mktime interprets it's argument as local time, to get UTC we must apply timezone fix
}
