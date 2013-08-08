/*
 frontpaneld.c

Copyright (C) 2012  Elecard Devices
Vdovchenko Vladimir

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

#include <sys/time.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "table_raw_gray41_b.h"

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define LISTEN_PATH				"/var/run/frontpanel" //socket

#define FONT_CHAR_WIDTH				20
#define FONT_CHAR_HEIGHT			22
#define FONT_COLUMN_NUMBER			16

#define FRAMEBUFFER_WIDTH				128
#define FRAMEBUFFER_HEIGHT				32
#define FRAMEBUFFER_SIZE				512

#define FRAMEBUFFER_DEVICE_NAME		"/dev/fb0"

#define DEVICE_TM1668_PATH		"/sys/devices/platform/tm1668"
#define DEVICE_CT1628_PATH		"/sys/devices/platform/ct1628"

#define MAX_TIMEOUT				60
#define DELAY_TIMER_MIN			100
#define DELAY_TIMER_MAX			5000
#define TIME_FIRST_SLEEP_TEXT	600
#define TIME_OTHER_SLEEP_TEXT	300
#define TIME_INTERVAL			1000, 1000
#define TIME_STOP				0, 0
#define BUF_SIZE				256
#define LITTLE_INTEVAL			60000
#define ADDITION_INTERVAL		100000

#define ARRAY_SIZE(arr)			(sizeof(arr) / sizeof(arr[0]))


/*#define DBG(x...) \
do { \
	time_t ts = time(NULL); \
	struct tm tsm; \
	localtime_r(&ts, &tsm); \
	printf("%02d:%02d:%02d: ", tsm.tm_hour, tsm.tm_min, tsm.tm_sec); \
	printf(x); \
} while (0)*/

#ifndef DBG
#define DBG(x...)
#endif


/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef enum {NONE, TIME, STATUS, NOTIFY} MessageType_t;
typedef enum {UNKNOWN, PROMSVYAZ, CH7162, STB850} BoardType_t;
typedef int (*ArgHandler)(char *, char *);
struct argHandler_s {
	char		*argName;
	ArgHandler	handler;
};
typedef struct {
	char	text[BUF_SIZE];
	int32_t	textLength;
	int32_t	textOffset;
} rollTextInfo_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int ArgHandler_Time(char *input, char *output);
static int ArgHandler_Delays(char *input, char *output);
static int ArgHandler_Busy(char *input, char *output);
static int ArgHandler_Notify(char *input, char *output);
static int ArgHandler_Status(char *input, char *output);
static int ArgHandler_Pulse(char *input, char *output);
static int ArgHandler_Brightness(char *input, char *output);

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

timer_t			g_delayTimer;
timer_t			g_messageTimer;

uint32_t		g_t1 = TIME_FIRST_SLEEP_TEXT;
uint32_t		g_t2 = TIME_OTHER_SLEEP_TEXT;

BoardType_t		g_board = UNKNOWN;
char			*g_frontpanelPath = NULL;
char			g_frontpanelText[256];
char			g_frontpanelBrightness[256];

const char		g_digitsWithColon[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
int32_t			g_brightness = 4;
int32_t			g_timeEnabled = true;
rollTextInfo_t	g_rollTextInfo;
MessageType_t	g_messageType = TIME;
MessageType_t	g_beQuiet = 0;

typedef struct {
	uint32_t 				fd;
	unsigned char 				mask[FRAMEBUFFER_SIZE];
} fbInfo_t;

fbInfo_t				g_fbInfo;

struct argHandler_s handlers[] = {
	{"time",	ArgHandler_Time},
	{"t",		ArgHandler_Time},
	{"delays",	ArgHandler_Delays},
	{"d",		ArgHandler_Delays},
	{"busy",	ArgHandler_Busy},
	{"?",		ArgHandler_Busy},
	{"notify",	ArgHandler_Notify},
	{"n",		ArgHandler_Notify},
	{"status",	ArgHandler_Status},
	{"s",		ArgHandler_Status},
	{"pulse",	ArgHandler_Pulse},
	{"p",		ArgHandler_Pulse},
	{"bright",	ArgHandler_Brightness},
	{"b",		ArgHandler_Brightness},
};

/******************************************************************
* FUNCTION IMPLEMENTATION                                         *
*******************************************************************/

int32_t FrameBufferInit(void)
{
	g_fbInfo.fd = open(FRAMEBUFFER_DEVICE_NAME, O_RDWR);

	return 0;
}

void FrameBufferClose(void)
{
	close(g_fbInfo.fd);
}

void UpdateDisplay(void)
{	
	FrameBufferInit();
	write(g_fbInfo.fd, g_fbInfo.mask, FRAMEBUFFER_SIZE);	
	FrameBufferClose();
}

void SetPixel(uint32_t x, uint32_t y)
{
	g_fbInfo.mask[y*FONT_COLUMN_NUMBER+x/8] = g_fbInfo.mask[y*FONT_COLUMN_NUMBER+x/8] | 1 << x%8;	
}


void AddChar(uint32_t x, uint32_t y, uint8_t ch)
{
	uint32_t line, byteInLine;

	uint32_t startBite = (ch % FONT_COLUMN_NUMBER) * FONT_CHAR_WIDTH +
				(ch / FONT_COLUMN_NUMBER) * FONT_CHAR_HEIGHT * FONT_CHAR_WIDTH * FONT_COLUMN_NUMBER;

	for (line = y; line < y+FONT_CHAR_HEIGHT; line++)
	{
		for (byteInLine = x; byteInLine < x+FONT_CHAR_WIDTH; byteInLine++)
		{
			if (table_raw_gray41_on_bits[startBite/8] & (1 << startBite%8))
			{
				SetPixel(byteInLine, line);
			}
			startBite++;
		}
		startBite += FONT_CHAR_WIDTH * (FONT_COLUMN_NUMBER - 1);
	}
}

void AddString(uint32_t x0, uint32_t y0, const char* str)
{
	uint32_t i;

	for(i = 0; i < FRAMEBUFFER_SIZE; i++)
		g_fbInfo.mask[i] = 0x00;

	i = 0;
	// Draw glyphs one by one
	while(*str) {
		AddChar(x0 + i, y0, *str);
		str++;
		i += FONT_CHAR_WIDTH >> 1;
	}
	UpdateDisplay();
}

int32_t StringWidth(const char* str)
{
	uint32_t	len;
	if(str == NULL)
		return 0;
	len = strlen(str);
	if(len == 0)
		return 0;

	return ((len - 1) * (FONT_CHAR_WIDTH >> 1) + FONT_CHAR_WIDTH);
}

int32_t CheckBoardType(void)
{
	char *boardName;
	struct {
		BoardType_t	boardType;
		char		*name;
//		uint32_t	nameLen;
	} boards[] = {
		{PROMSVYAZ,	"stb840_promSvyaz"},
		{CH7162,	"stb840_ch7162"},
		{STB850,	"stb850"},
	};

	boardName = getenv("board_name");
	if(boardName) {
		uint32_t i;
		for(i = 0; i < ARRAY_SIZE(boards); i++) {
			if(strncmp(boardName, boards[i].name, strlen(boards[i].name)) == 0) {
//				fprintf(stderr, "frontpaneld: Detected board=%s\n", boards[i].name);
				g_board = boards[i].boardType;
				break;
			}
		}
	}
	if(g_board == UNKNOWN) {
		fprintf(stderr, "frontpaneld: Cant detect board!!! board_name=%s\n", boardName ? boardName : "(null)");
		return -1;
	}
	return 0;
}

int32_t CheckControllerType(void)
{
	uint32_t	i;
	struct {
		char *name;
		char *path;
	} ctrl_paths[] = {
		{"ct1628", DEVICE_CT1628_PATH},
		{"tm1668", DEVICE_TM1668_PATH},
	};

	for(i = 0; i < sizeof(ctrl_paths) / sizeof(*ctrl_paths); i++) {
		struct stat st;
		if(stat(ctrl_paths[i].path, &st) == 0) {
			fprintf(stdout, "frontpaneld: found %s controller\n", ctrl_paths[i].name);
			g_frontpanelPath = ctrl_paths[i].path;
			snprintf(g_frontpanelBrightness, sizeof(g_frontpanelBrightness), "%s/brightness", g_frontpanelPath);
			snprintf(g_frontpanelText, sizeof(g_frontpanelText), "%s/text", g_frontpanelPath);
			return 0;
		}
	}
	fprintf(stderr, "frontpaneld: not found supported frontpanel conrollers!!!\n");
	return -1;
}

timer_t CreateTimer(int signo)
{
	timer_t timerid;
	struct sigevent se;
	se.sigev_notify = SIGEV_SIGNAL;
	se.sigev_signo = signo;
	if(timer_create(CLOCK_REALTIME, &se, &timerid) == -1) {
		perror("Failed to create timer");
		exit(-1);
	}
	return timerid;
}

void SetTimer(timer_t timerid, int mcSecValue, int mcSecInterval)
{
	struct itimerspec timervals;

	timervals.it_value.tv_sec     =  mcSecValue / 1000;
	timervals.it_value.tv_nsec    = (mcSecValue % 1000) * 1000000;
	timervals.it_interval.tv_sec  =  mcSecInterval / 1000;
	timervals.it_interval.tv_nsec = (mcSecInterval % 1000) * 1000000;
	if(timer_settime(timerid, 0, &timervals, NULL) == -1) {
		perror("Failed to start timer");
//		exit(-1);
	}
}

void StopTimer(timer_t timerid)
{
	struct itimerspec timervals = {{0, 0}, {0, 0}};
	if(timer_settime(timerid, 0, &timervals, NULL) == -1) {
		perror("Failed to stop timer");
//		exit(-1);
	}
}

void AddColon(char *buf)
{
	if(g_board == PROMSVYAZ) {
		if(buf[1] >= '0' && buf[1] <= '9') {
			buf[1] = g_digitsWithColon[buf[1] - '0'];
		}
	} else if (g_board == CH7162) {//CH7162 board
		int32_t	temp;
		int32_t	textLen = strlen(buf);

		for(temp = textLen; temp < 4; temp++) {
			buf[temp] = ' ';
		}
		buf[4] = '8';
		buf[5] = 0;
	} else if(g_board == STB850) { //STB850			
		buf[2] = ':';
	}
}

void GetCurrentTime(char *buf)
{
	time_t		t;
	struct tm	*area;
	static bool	colon = true;

	t = time(NULL);
	area = localtime(&t);
	
	if(g_board == STB850){
		sprintf(buf, "%02d %02d", area->tm_hour, area->tm_min);
	} else {		
		sprintf(buf, "%02d%02d", area->tm_hour, area->tm_min);
	}
	

	if(buf[0] == '0')
		buf[0] = ' ';

	if(colon) {
		AddColon(buf);
	}
	colon = !colon;
	
}

void SetFrontpanelText(const char *buffer)
{
	if(g_board == STB850){
		AddString(5, 15, buffer);
	}
	else {
		FILE *f;
		f = fopen(g_frontpanelText, "w");
		if(f != 0) {
			fwrite(buffer, 5, 5, f);
			fclose(f);
		}
	}	
}

void DisplayCurentText()
{
	int32_t bufSize;

	if(g_board == STB850){
		bufSize = ((FRAMEBUFFER_WIDTH - (FONT_CHAR_WIDTH))/(FONT_CHAR_WIDTH >> 1))+1;
	} else {
		bufSize = 6;
	}

	char	tmpBuf[bufSize];
	
	switch(g_messageType) {
		case NOTIFY:
		case STATUS:
			g_rollTextInfo.textOffset++;
			if(g_rollTextInfo.textOffset > g_rollTextInfo.textLength) {
				g_rollTextInfo.textOffset = 0;
			}
			if(g_board == STB850){
				strncpy(tmpBuf, g_rollTextInfo.text + g_rollTextInfo.textOffset, bufSize);
			}
			else {
				strncpy(tmpBuf, g_rollTextInfo.text + g_rollTextInfo.textOffset, 4);
			}
			
			SetFrontpanelText(tmpBuf);
			break;
		case TIME:
			GetCurrentTime(tmpBuf);
			SetFrontpanelText(tmpBuf);
			break;
		case NONE:
			strncpy(tmpBuf, "     ", bufSize);
			SetFrontpanelText(tmpBuf);
			break;
		default:
			break;
	}
	if(g_board != STB850)
		tmpBuf[5] = 0;
	DBG("%s: type %d text=\"%s\"\n", __func__, g_messageType, tmpBuf);
}

void ShowTime()
{
	if(g_timeEnabled) {
		g_messageType = TIME;
		SetTimer(g_delayTimer, TIME_INTERVAL);
	} else {
		g_messageType = NONE;
		StopTimer(g_delayTimer);
	}
	DisplayCurentText();
}

void Quit()
{
	if(!g_beQuiet)
		SetFrontpanelText("byE");
	exit(0);
}

static void SetBrightness(int brightness)
{
	FILE	*f;
	f = fopen(g_frontpanelBrightness, "w");
	if(f != 0) {
		fprintf(f, "%d", brightness);
		fclose(f);
	}
}

static void Helper_TerminateText(char *text, char terminateSymbol)
{
	char	*ptr;
	ptr = strchr(text, terminateSymbol);
	if(ptr)
		*ptr = 0;
}

static int32_t SetMessage(char *newText, int32_t timeout)
{
	if(newText == NULL)
		return -1;

	StopTimer(g_delayTimer);
	StopTimer(g_messageTimer);
	if(timeout > 0) {
				
		int32_t	textLen;

		/*while((*newText == ' ') || (*newText == '\t')) {
			newText++;
		}*/
		//if message incapsulate into '' or "", skip this characters
		/*if((*newText == '\'') || (*newText == '\"')) {
			newText++;
		}
		Helper_TerminateText(newText, '\'');
		Helper_TerminateText(newText, '\"');*/

		textLen = strlen(newText);
		DBG("%s: '%s' (%d)\n", __func__, newText, textLen);
		
		int32_t bufSize;		

		if(g_board == STB850){
			bufSize = ((FRAMEBUFFER_WIDTH - (FONT_CHAR_WIDTH))/(FONT_CHAR_WIDTH >> 1))+1;;
		} else {
			bufSize = 6;
		}
		
		char	tmpBuf[bufSize];
				
		if((textLen > 2) && (newText[2] == ':') && (textLen < 6)) {//"xx:xx" format
			memcpy(tmpBuf, newText, 2);
			strcpy(tmpBuf + 2, newText + 3);
			AddColon(tmpBuf);
		} else {
			if(g_board == STB850){
				if (StringWidth(newText) < FRAMEBUFFER_WIDTH)
				{
					strncpy(tmpBuf, newText, textLen);
				}
				else {
					strncpy(tmpBuf, newText, bufSize);

					if(textLen > bufSize) { 
						g_rollTextInfo.textLength = textLen;
						g_rollTextInfo.textOffset = 0;
						strcpy(g_rollTextInfo.text, newText);
						SetTimer(g_delayTimer, g_t1, g_t2);
					}
				}
					
			} else {
				strncpy(tmpBuf, newText, 4);

				if(textLen > 4) { //roll the text if it contain more than 4 characters
					g_rollTextInfo.textLength = textLen;
					g_rollTextInfo.textOffset = 0;
					strcpy(g_rollTextInfo.text, newText);
					SetTimer(g_delayTimer, g_t1, g_t2);
				}
			}			
		}
		SetFrontpanelText(tmpBuf);

		if(timeout > MAX_TIMEOUT) {
			timeout = MAX_TIMEOUT;
		}
		//set timer for disabling message
		SetTimer(g_messageTimer, timeout * 1000, 0);
	} else if(timeout == 0) {
		//stop showing message, show time
		ShowTime();
	}

	return 0;
}


#define pthread_setcancelstate_my(flag) \
	do { \
		int err = pthread_setcancelstate(flag, NULL); \
		if(err != 0) { \
			errno = err; \
			perror("pthread_setcancelstate"); \
		} \
	} while (0)

void *PulseThread(void *arg)
{
	int br_prev = g_brightness;
	(void)arg;

	while(1) {
		int i;

		for(i = br_prev; i < 9; i++) {
			pthread_setcancelstate_my(PTHREAD_CANCEL_DISABLE);
			SetBrightness(i);
			pthread_setcancelstate_my(PTHREAD_CANCEL_ENABLE);
			usleep(LITTLE_INTEVAL);
		}

		for(i = 8; i >= 0; i--) {
			pthread_setcancelstate_my(PTHREAD_CANCEL_DISABLE);
			SetBrightness(i);
			pthread_setcancelstate_my(PTHREAD_CANCEL_ENABLE);
			usleep(LITTLE_INTEVAL);
		}

		br_prev = 0;
		usleep(ADDITION_INTERVAL);
	}
	
	return NULL;
}

static int ArgHandler_Time(char *input, char *output)
{
	(void)output;
	g_timeEnabled = atol(input);

	if(g_messageType != NOTIFY && g_messageType != STATUS) {
		//apply enable/disable displaying time
		ShowTime();
	}
	return 0;
}

static int ArgHandler_Delays(char *input, char *output)
{
	(void)output;
	g_t1 = 0;
	g_t2 = 0;

	sscanf(input, " %d %d", &g_t1, &g_t2);

	if(g_t1 < DELAY_TIMER_MIN) {
		g_t1 = DELAY_TIMER_MIN;
	}
	if(g_t2 < DELAY_TIMER_MIN) {
		g_t2 = DELAY_TIMER_MIN;
	}

	if(g_t1 > DELAY_TIMER_MAX) {
		g_t1 = DELAY_TIMER_MAX;
	}
	if(g_t2 > DELAY_TIMER_MAX) {
		g_t2 = DELAY_TIMER_MAX;
	}

	return 0;
}

static int ArgHandler_Busy(char *input, char *output)
{
	int temp_time = 0;

	(void)input;
	if(g_messageType == STATUS) {
		struct itimerspec timervals;
		timer_gettime(g_messageTimer , &timervals);
		temp_time = (int)timervals.it_value.tv_sec * 1000 + (int)timervals.it_value.tv_nsec / 1000000;
	}
	sprintf(output, "%d", temp_time);

	return 1;
}

static int ArgHandler_Notify(char *input, char *output)
{
	int32_t		timeout;
	int32_t		ret = 0;
	char		*text_start = NULL;

	(void)output;
	if(g_messageType == STATUS) {
		return -1;
	}

	timeout = strtol(input, &text_start, 10);
	g_messageType = NOTIFY;
	ret = SetMessage(text_start, timeout);

	return ret;
}

static int ArgHandler_Status(char *input, char *output)
{
	int32_t			timeout;
	int32_t			id;
	int32_t			ret = 0;
	char			*text_start = NULL;
	static int32_t	statusId = -1;

	(void)output;
	id = strtol(input, &text_start, 10);
	if((id < 0) || (g_messageType == STATUS && id != statusId)) {
		return -1;
	}

	timeout = MAX_TIMEOUT;
	if(text_start && *text_start == ':') {
		timeout = strtol(text_start + 1, &text_start, 10);
	}
	statusId = id;
	g_messageType = STATUS;
	ret = SetMessage(text_start, timeout);

	return ret;
}

static int ArgHandler_Pulse(char *input, char *output)
{
	int					enable;
	static int			pulseStarted = 0;
	static pthread_t	pulse_thread;

	(void)output;
	enable = strtol(input, NULL, 10);
	if(enable) {
		if(pulseStarted == 0) {
			if(pthread_create(&pulse_thread, NULL, PulseThread, NULL)) {
				perror("server: PulseThread");
			} else {
				pulseStarted = 1;
			}
		}
	} else {
		if(pulseStarted) {
			pthread_cancel(pulse_thread);
			pthread_join(pulse_thread, NULL);
			pulseStarted = 0;
		}
		SetBrightness(g_brightness);
	}

	return 0;
}

static int ArgHandler_Brightness(char *input, char *output)
{
	(void)output;
	g_brightness = strtol(input, NULL, 10);
	SetBrightness(g_brightness);

	return 0;
}


int MainLoop()
{
	struct sockaddr_un sa, ca;
	int len, socket_id;

	g_delayTimer = CreateTimer(SIGUSR1);
	g_messageTimer = CreateTimer(SIGUSR2);

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, Quit);
	signal(SIGQUIT, Quit);
	signal(SIGINT,  Quit);
	signal(SIGUSR1, DisplayCurentText);
	signal(SIGUSR2, ShowTime);

	if((socket_id = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		perror("client: socket");
		exit(1);
	}

	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, LISTEN_PATH);
	unlink(LISTEN_PATH);
	len = sizeof(sa.sun_family) + strlen(sa.sun_path);

	if(bind(socket_id, (struct sockaddr *) &sa, len) < 0) {
		perror("server: bind");
		exit(1);
	}

	if(listen(socket_id, 5) < 0) {
		perror("server: listen");
		exit(1);
	}

	SetBrightness(g_brightness);
	ShowTime();

	for(;;) {
		int		accept_id;
		int		ca_len;
		unsigned int	i;
		char	buffer[BUF_SIZE];

		if((accept_id = accept(socket_id, (struct sockaddr *) &ca, (socklen_t *)&ca_len)) < 0) {
			perror("server: accept");
			exit(1);
		}

		ca_len = read(accept_id, buffer, sizeof(buffer) - 1);
		if(ca_len > 0) {
			buffer[ca_len] = 0;
		} else {
			continue;
		}

		//we dont need CR and CN symbols, that adds StbCommandClient
		Helper_TerminateText(buffer, '\r');
		Helper_TerminateText(buffer, '\n');

		DBG("%s: << '%s'\n", __func__, buffer);

		for(i = 0; i < sizeof(handlers)/sizeof(*handlers); i++) {
			int argLen = strlen(handlers[i].argName);
			if(strncmp(handlers[i].argName, buffer, argLen) == 0) {
				char ch = buffer[argLen];

				if(	(ch == '\t') ||
					(ch == ' ') ||
					(ch == '\n') ||
					(ch == '\r') ||
					(ch == '\0') ||
					((ch >= '0') && (ch <= '9')) )
				{
					if(handlers[i].handler) {
//						int		ret;
						char	reply[BUF_SIZE] = "";

						handlers[i].handler(buffer + argLen, reply);
						if(reply[0]) {
							send(accept_id, reply, strlen(reply) + 1, 0);
						}
					}
					break;
				}
			}
		}
		close(accept_id);
	}

	return 1;
}


int main(int argc, char **argv)
{
	int32_t	opt;
	int32_t	daemonize = 1;

	while((opt = getopt(argc, argv, "qd")) != -1) {
		switch(opt) {
			case 'q':
				g_timeEnabled = false;
				g_beQuiet = 1;
				break;
			case 'd':
				daemonize = 0;
				break;
			default:
				break;
		}
	}

	CheckBoardType();
	//if(g_board == STB850) {//until not released frontpanel oled driver
	//	exit(1);
	//}
	if(CheckControllerType() != 0) {
		exit(1);
	}
	if(daemonize) {
		if(daemon(0, 0)) {
			perror("Failed to daemonize");
			exit(1);
		}
	}
	return MainLoop();
}
