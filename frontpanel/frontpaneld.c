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

#define LISTEN_PATH "/var/run/frontpanel" //socket
#define BOARD_NAME_PROMSVYAZ "stb840_promSvyaz"
#define BOARD_NAME_CH7162    "stb840_ch7162"
#define PATH_PROMSVYAZ "/sys/devices/platform/tm1668/text"
#define PATH_CH7162    "/sys/devices/platform/ct1628/text"
#define MAX_TIMEOUT 60
#define DELAY_TIMER_MIN 100
#define DELAY_TIMER_MAX 5000
#define TIME_FIRST_SLEEP_TEXT 2000
#define TIME_OTHER_SLEEP_TEXT 800
#define TIME_INTERVAL 1, 1000
#define TIME_STOP 0, 0
#define BUF_SIZE 256

/*#define DBG(x...) do {\
{ \
time_t ts = time(NULL); \
struct tm tsm; \
localtime_r(&ts, &tsm); \
printf("%02d:%02d:%02d: ", tsm.tm_hour, tsm.tm_min, tsm.tm_sec); \
} \
printf(x); \
} while (0)
*/
#ifndef DBG
#define DBG(x...)
#endif

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef enum {NONE, TIME, STATUS, NOTIFY} MessageTypes;
typedef enum {UNKNOWN, PROMSVYAZ, CH7162} BoardTypes;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

timer_t delayTimer;
timer_t mainTimer;

bool time_display = true;
long t1 = TIME_FIRST_SLEEP_TEXT,
	 t2 = TIME_OTHER_SLEEP_TEXT;

BoardTypes board = UNKNOWN;
static const char *frontpanelPath = NULL;

char digitsWithColon[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
bool colon = true;
char time_buffer[6] = "00000\0";
int  textLength;
int  textOffset = 0;
char text[BUF_SIZE] = "";

MessageTypes messageType = TIME;
int statusId = -1;

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
		exit(-1);
	}
}

void StopTimer(timer_t timerid)
{
	struct itimerspec timervals;
	timervals.it_value.tv_sec  = 0;
	timervals.it_value.tv_nsec = 0;
	if(timer_settime(timerid, 0, &timervals, NULL) == -1) {
		perror("Failed to stop timer");
		exit(-1);
	}
}

void GetCurrentTime()
{
	time_t t;
	struct tm *area;
	t = time(NULL);
	area = localtime(&t);

	sprintf(time_buffer, "%02d", area->tm_hour);
	if(time_buffer[0] == '0')
		time_buffer[0] = ' ';
	sprintf(time_buffer + 2, "%02d", area->tm_min);

	if(colon) {
		if(board == PROMSVYAZ) {
			time_buffer[1] = digitsWithColon[time_buffer[1] - '0'];
			time_buffer[4] = 0; //promsvyaz
		} else {
			time_buffer[4] = '8'; //CH7162 board
		}
	}
}

void SetFrontpanelText(const char *buffer)
{
	FILE *f;
	f = fopen(frontpanelPath, "w");
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
			board = PROMSVYAZ;
			frontpanelPath = PATH_PROMSVYAZ;
		} else if(strncmp(boardName, BOARD_NAME_CH7162, sizeof(BOARD_NAME_CH7162) - 1) == 0) {
			board = CH7162;
			frontpanelPath = PATH_CH7162;
		}
	} else {
		fputs("Variable board_name not setted!!!\n", stderr);
	}
	if(board == UNKNOWN) {
		fputs("Unknown board\n", stderr);
		exit(1);
	}
}

void DisplayText()
{
	DBG("%s: type %d text %s\n", __func__, messageType, text);
	switch(messageType) {
		case NOTIFY:
		case STATUS:
			if((text[2] == ':') && (textLength < 6) && (textLength > 2)) {
				StopTimer(delayTimer);
				strcpy(time_buffer, text);
				strcpy(time_buffer + 2, time_buffer + 3);

				if(board == PROMSVYAZ) {
					if(time_buffer[1] >= '0' && time_buffer[1] <= '9') {
						time_buffer[1] = digitsWithColon[time_buffer[1] - '0'];
					}
				} else {
					int temp;
					for(temp = textLength - 1; temp <= 5; temp++) {
						time_buffer[temp] = ' ';
					}
					time_buffer[4] = '8';
				}
				SetFrontpanelText(time_buffer);
			} else {
				if(textOffset > textLength) {
					textOffset = 0;
				}

				strncpy(time_buffer, text + textOffset, 4);
				SetFrontpanelText(time_buffer);

				if(0 == textOffset) {
					SetTimer(delayTimer, t1, t2);
				}

				textOffset++;
			}
			break;
		case TIME:
			GetCurrentTime();
			SetFrontpanelText(time_buffer);
			colon = !colon;
			break;
		default:
			break;
	}

}

void DisplayTime()
{
	if(time_display) {
		messageType = TIME;
		SetTimer(delayTimer, TIME_INTERVAL);
	} else {
		messageType = NONE;
		StopTimer(delayTimer);
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
	textOffset = 0;
	StopTimer(mainTimer);
}

void SetText(int timeout, const char *new_text)
{
	if(timeout > MAX_TIMEOUT) {
		timeout = MAX_TIMEOUT;
	}

	textOffset = 0;
	strcpy(text, new_text);
	textLength = strlen(text);
	DBG("%s: '%s' (%d)\n", __func__, text, textLength);

	memset(time_buffer, 0, sizeof(time_buffer));
	DisplayText();
	SetTimer(mainTimer, timeout * 1000, 0);
}

void SetNotify(int timeout, const char *new_text)
{
	messageType = NOTIFY;

	SetText(timeout, new_text);
}

void SetStatus(int id, int timeout, const char *new_text)
{
	statusId = id;
	messageType = STATUS;

	SetText(timeout, new_text);
}


int MainLoop()
{
	struct sockaddr_un sa, ca;

	int len, socket_id, accept_id, ca_len;
	int timeout, id;
	char *text_start;
	char buffer[BUF_SIZE];

	delayTimer = CreateTimer(SIGUSR1);
	mainTimer  = CreateTimer(SIGUSR2);

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

	messageType = TIME;
	SetTimer(delayTimer, TIME_INTERVAL);

	for(;;) {
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
		text_start = NULL;

		switch(buffer[0]) {
			case 't':
				time_display = atol(buffer + 1);
				if(messageType != NOTIFY && messageType != STATUS) {
					DisplayTime();
				}
				break;
			case 'h':
				t1 = 0;
				t2 = 0;

				sscanf(buffer + 1, " %ld %ld", &t1, &t2);

				if(t1 < DELAY_TIMER_MIN) {
					t1 = DELAY_TIMER_MIN;
				}
				if(t2 < DELAY_TIMER_MIN) {
					t2 = DELAY_TIMER_MIN;
				}

				if(t1 > DELAY_TIMER_MAX) {
					t1 = DELAY_TIMER_MAX;
				}
				if(t2 > DELAY_TIMER_MAX) {
					t2 = DELAY_TIMER_MAX;
				}
				break;
			case '?': {
				char send_buffer[11] = "";
				int temp_time = 0;
				if(messageType == STATUS) {
					struct itimerspec timervals;
					timer_gettime(mainTimer , &timervals);
					temp_time = (int)timervals.it_value.tv_sec * 1000 + (int)timervals.it_value.tv_nsec / 1000000;
				}
				sprintf(send_buffer, "%d", temp_time);
				send(accept_id, send_buffer, sizeof(send_buffer), 0);
				break;
			}
			case 'n':
				if(messageType == STATUS) {
					break;
				}
				timeout = strtol(buffer + 1, &text_start, 10);
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
				break;
			case 's':
				id = strtol(buffer + 1, &text_start, 10);
				do {
					if(id < 0 ||
							(messageType == STATUS && id != statusId)) {
						break;
					}

					timeout = MAX_TIMEOUT;
					statusId = id;
					if(text_start && *text_start == ':') {
						timeout = strtol(text_start + 1, &text_start, 10);
						if(timeout < 0) {
							break;
						}
						if(timeout == 0) {
							StopMainTimer();
							DisplayTime();
							break;
						}
					}
					if(text_start && *text_start) {
						text_start++;
					} else {
						text_start = "";
					}

					SetStatus(id, timeout, text_start);
				} while(0);
				break;
			default:
				break;
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
