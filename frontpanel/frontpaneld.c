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
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>


/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define LISTEN_PATH				"/var/run/frontpanel" //socket
#define BOARD_NAME_PROMSVYAZ	"stb840_promSvyaz"
#define BOARD_NAME_CH7162		"stb840_ch7162"
#define PATH_PROMSVYAZ			"/sys/devices/platform/tm1668/text"
#define PATH_CH7162				"/sys/devices/platform/ct1628/text"
#define BRIGHTNESS_PROMSVYAZ	"/sys/devices/platform/tm1668/brightness"
#define BRIGHTNESS_CH7162		"/sys/devices/platform/ct1628/brightness"
#define MAX_TIMEOUT 60
#define DELAY_TIMER_MIN 100
#define DELAY_TIMER_MAX 5000
#define TIME_FIRST_SLEEP_TEXT 2000
#define TIME_OTHER_SLEEP_TEXT 800
#define TIME_INTERVAL 1, 1000
#define TIME_STOP 0, 0
#define BUF_SIZE 256
#define LITTLE_INTEVAL			60000
#define ADDITION_INTERVAL		100000

#define DBG(x...) \
do { \
	time_t ts = time(NULL); \
	struct tm tsm; \
	localtime_r(&ts, &tsm); \
	printf("%02d:%02d:%02d: ", tsm.tm_hour, tsm.tm_min, tsm.tm_sec); \
	printf(x); \
} while (0)

#ifndef DBG
#define DBG(x...)
#endif

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef enum {NONE, TIME, STATUS, NOTIFY} MessageTypes;
typedef enum {UNKNOWN, PROMSVYAZ, CH7162} BoardTypes;
typedef int (*ArgHandler)(char *, char *);
struct argHandler_s {
	char		*argName;
	ArgHandler	handler;
};

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

timer_t g_delayTimer;
timer_t g_mainTimer;

bool g_time_display = true;
long g_t1 = TIME_FIRST_SLEEP_TEXT,
	 g_t2 = TIME_OTHER_SLEEP_TEXT;

BoardTypes g_board = UNKNOWN;
static const char *g_frontpanelPath = NULL;
static const char *g_frontpanelBrightness = NULL;

char g_digitsWithColon[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
bool g_colon = true;
char g_time_buffer[6] = "00000\0";
int  g_textLength;
int  g_textOffset = 0;
char g_text[BUF_SIZE] = "";
int  g_brightness = 4;

MessageTypes g_messageType = TIME;
int g_statusId = -1;
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
	struct itimerspec timervals;
	timervals.it_value.tv_sec  = 0;
	timervals.it_value.tv_nsec = 0;
	if(timer_settime(timerid, 0, &timervals, NULL) == -1) {
		perror("Failed to stop timer");
//		exit(-1);
	}
}

void GetCurrentTime()
{
	time_t t;
	struct tm *area;
	t = time(NULL);
	area = localtime(&t);

	sprintf(g_time_buffer, "%02d", area->tm_hour);
	if(g_time_buffer[0] == '0')
		g_time_buffer[0] = ' ';
	sprintf(g_time_buffer + 2, "%02d", area->tm_min);

	if(g_colon) {
		if(g_board == PROMSVYAZ) {
			g_time_buffer[1] = g_digitsWithColon[g_time_buffer[1] - '0'];
			g_time_buffer[4] = 0; //promsvyaz
		} else {
			g_time_buffer[4] = '8'; //CH7162 g_board
		}
	}
}

void SetFrontpanelText(const char *buffer)
{
	FILE *f;
	f = fopen(g_frontpanelPath, "w");
	if(f != 0) {
		fwrite(buffer, 5, 5, f);
		fclose(f);
	}
}

void CheckBoardType()
{
	char *boardName;
	boardName = getenv("board_name");
	if(boardName) {
		if(strncmp(boardName, BOARD_NAME_PROMSVYAZ, sizeof(BOARD_NAME_PROMSVYAZ) - 1) == 0) {
			g_board = PROMSVYAZ;
			g_frontpanelPath = PATH_PROMSVYAZ;
			g_frontpanelBrightness = BRIGHTNESS_PROMSVYAZ;
		} else if(strncmp(boardName, BOARD_NAME_CH7162, sizeof(BOARD_NAME_CH7162) - 1) == 0) {
			g_board = CH7162;
			g_frontpanelPath = PATH_CH7162;
			g_frontpanelBrightness = BRIGHTNESS_CH7162;
		}
	} else {
		fputs("Variable board_name not setted!!!\n", stderr);
	}
	if(g_board == UNKNOWN) {
		fputs("Unknown g_board\n", stderr);
		exit(1);
	}
}

void DisplayText()
{
	DBG("%s: type %d g_text %s\n", __func__, g_messageType, g_text);
	switch(g_messageType) {
		case NOTIFY:
		case STATUS:
			if((g_textLength > 2) && (g_text[2] == ':') && (g_textLength < 6)) {
				StopTimer(g_delayTimer);
				strcpy(g_time_buffer, g_text);
				strcpy(g_time_buffer + 2, g_time_buffer + 3);

				if(g_board == PROMSVYAZ) {
					if(g_time_buffer[1] >= '0' && g_time_buffer[1] <= '9') {
						g_time_buffer[1] = g_digitsWithColon[g_time_buffer[1] - '0'];
					}
				} else {
					int temp;
					for(temp = g_textLength - 1; temp <= 5; temp++) {
						g_time_buffer[temp] = ' ';
					}
					g_time_buffer[4] = '8';
				}
				SetFrontpanelText(g_time_buffer);
			} else {
				if(g_textOffset > g_textLength) {
					g_textOffset = 0;
				}

				strncpy(g_time_buffer, g_text + g_textOffset, 4);
				SetFrontpanelText(g_time_buffer);

				if(g_textLength <= 4) { //dont roll the g_text if it seat in 4 indicator characters
					StopTimer(g_delayTimer);
				} else {
					if(0 == g_textOffset) {
						SetTimer(g_delayTimer, g_t1, g_t2);
					}
					g_textOffset++;
				}
			}
			break;
		case TIME:
			GetCurrentTime();
			SetFrontpanelText(g_time_buffer);
			g_colon = !g_colon;
			break;
		default:
			break;
	}

}

void DisplayTime()
{
	if(g_time_display) {
		g_messageType = TIME;
		SetTimer(g_delayTimer, TIME_INTERVAL);
	} else {
		g_messageType = NONE;
		StopTimer(g_delayTimer);
		SetFrontpanelText("     ");
	}
}

void Quit()
{
	SetFrontpanelText("byE");
	exit(0);
}

inline void StopMainTimer()
{
	g_textOffset = 0;
	StopTimer(g_mainTimer);
}

void SetText(int timeout, const char *new_text)
{
	if(timeout > MAX_TIMEOUT) {
		timeout = MAX_TIMEOUT;
	}

	g_textOffset = 0;
	strcpy(g_text, new_text);
	g_textLength = strlen(g_text);
	DBG("%s: '%s' (%d)\n", __func__, g_text, g_textLength);

	memset(g_time_buffer, 0, sizeof(g_time_buffer));
	DisplayText();
	SetTimer(g_mainTimer, timeout * 1000, 0);
}

void SetNotify(int timeout, const char *new_text)
{
	g_messageType = NOTIFY;

	SetText(timeout, new_text);
}

void SetStatus(int id, int timeout, const char *new_text)
{
	g_statusId = id;
	g_messageType = STATUS;

	SetText(timeout, new_text);
}

void SetBrightness(int brightness)
{
	FILE *f;
	f = fopen(g_frontpanelBrightness, "w");
	if(f != 0) {
		fprintf(f, "%d", brightness);
		fclose(f);
	}
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
	g_time_display = atol(input);
	if(g_messageType != NOTIFY && g_messageType != STATUS) {
		DisplayTime();
	}
	return 0;
}

static int ArgHandler_Delays(char *input, char *output)
{
	(void)output;
	g_t1 = 0;
	g_t2 = 0;

	sscanf(input, " %ld %ld", &g_t1, &g_t2);

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
		timer_gettime(g_mainTimer , &timervals);
		temp_time = (int)timervals.it_value.tv_sec * 1000 + (int)timervals.it_value.tv_nsec / 1000000;
	}
	sprintf(output, "%d", temp_time);

	return 1;
}

static int ArgHandler_Notify(char *input, char *output)
{
	int		timeout;
	char	*text_start = NULL;

	(void)output;
	if(g_messageType == STATUS) {
		return -1;
	}

	timeout = strtol(input, &text_start, 10);
	if(timeout > 0) {
		if(text_start && *text_start) {
			text_start++;
		} else {
			text_start = "";
		}
		SetNotify(timeout, text_start);
	} else if(timeout == 0) {
		StopMainTimer();
		DisplayTime();
	}

	return 0;
}

static int ArgHandler_Status(char *input, char *output)
{
	int		timeout;
	int		id;
	char	*text_start = NULL;

	(void)output;
	id = strtol(input, &text_start, 10);
	if((id < 0) || (g_messageType == STATUS && id != g_statusId)) {
		return -1;
	}

	timeout = MAX_TIMEOUT;
	if(text_start && *text_start == ':') {
		timeout = strtol(text_start + 1, &text_start, 10);
		if(timeout < 0) {
			return -1;
		}
		if(timeout == 0) {
			StopMainTimer();
			DisplayTime();
			return -1;
		}
	}
	if(text_start && *text_start) {
		text_start++;
	} else {
		text_start = "";
	}
	SetStatus(id, timeout, text_start);

	return 0;
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
	int	bright;

	(void)output;
	bright = strtol(input, NULL, 10);
	g_brightness = bright;
	SetBrightness(bright);

	return 0;
}

int MainLoop()
{
	struct sockaddr_un sa, ca;
	int len, socket_id;

	g_delayTimer = CreateTimer(SIGUSR1);
	g_mainTimer  = CreateTimer(SIGUSR2);

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, Quit);
	signal(SIGQUIT, Quit);
	signal(SIGINT,  Quit);
	signal(SIGUSR1, DisplayText);
	signal(SIGUSR2, DisplayTime);

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

	g_messageType = TIME;
	SetBrightness(g_brightness);
	SetTimer(g_delayTimer, TIME_INTERVAL);

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

		DBG("%s: << '%s'\n", __func__, buffer);

		{//we dont need CR and CN symbols
			char	*ptr;
			ptr = strchr(buffer, '\r');
			if(ptr)
				*ptr = 0;
			ptr = strchr(buffer, '\n');
			if(ptr)
				*ptr = 0;
		}

		for(i = 0; i < sizeof(handlers)/sizeof(*handlers); i++) {
			int argLen = strlen(handlers[i].argName);
			if(strncmp(handlers[i].argName, buffer, argLen) == 0) {
				char ch = buffer[argLen];
				if(	(ch == '\t') ||
					(ch == ' ') ||
					(ch == '\n') ||
					(ch == '\r') ||
					(ch == '\0') ||
					((ch > '0') && (ch < '9')) )
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
	(void)argv;

	CheckBoardType();

	if(argc < 2) {
		if(daemon(0, 0)) {
			perror("Failed to daemonize");
			exit(1);
		}
	}
	return MainLoop();
}
