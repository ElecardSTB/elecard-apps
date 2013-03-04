//
// Created:  2013/01/08
// File name:  garb.c
//
//////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012 Elecard Devices
// All rights are reserved.  Reproduction in whole or in part is 
//  prohibited without the written consent of the copyright owner.
//
// Elecard Devices reserves the right to make changes without
// notice at any time. Elecard Devices makes no warranty, expressed,
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
// Authors: Andrey Kuleshov <Andrey.Kuleshov@elecard.ru>
// 
// Purpose: Implements GARB API
//
//////////////////////////////////////////////////////////////////////////

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "garb.h"
#include "debug.h"
#include "StbMainApp.h"
#include "off_air.h"
#include "l10n.h"
#include <cJSON.h>
#include <service.h>
#include <elcd-rpc.h>

#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define CHANNEL_NONE    (-1)
#define WATCH_PERIOD    (30)
#define WATCH_THRESHOLD (15)
#define VIEWERSHIP_TIMEOUT (30*60)

#define GARB_ID_LENGTH  (6)
//#define ALLOW_NO_VIEWERSHIP

#define HH_NONE  (-1)
#define HH_GUEST (99)

#define MAX_VIEWERS 16

#define GARB_TEST
#define GARB_CONFIG CONFIG_DIR "/garb.json"
#define GARB_FDD    CONFIG_DIR "/garb.fdd"

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct
{
	char id[GARB_ID_LENGTH];
	char *name;
} houseHoldMember_t;

typedef enum
{
	stateEmpty = 0,
	stateMember,
	stateMemberWatching,
	stateGuestWatching,
} watchSlotState_t;

typedef struct
{
	watchSlotState_t  state;
	houseHoldMember_t viewer;
} watchSlot_t;

typedef struct
{
	time_t duration;
	time_t start_time;
} garbWatchState_t;

typedef struct
{
	int       channel;
	struct tm start_time;
	time_t    duration;
	char      id[GARB_ID_LENGTH];
} garbWatchHistory_t;

typedef struct
{
	struct {
		int number;
		int device;
		int count;
	} hh;
	watchSlot_t viewership[MAX_VIEWERS];
	int viewers;
	int guests;
	time_t lastStop;

	struct {
		garbWatchState_t state[MAX_MEMORIZED_SERVICES];
		pthread_mutex_t  lock;
		int current;
	} channels;
	struct {
		list_element_t *head;
		list_element_t *tail;
	} history;
	pthread_t thread;
} garbState_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                                      *
*******************************************************************/

#ifdef GARB_TEST
static void garb_test();
#endif
static void garb_save();
static void garb_load();
static void *garb_thread(void *notused);

static void garb_printMembers(char text[]);
static int garb_viewershipCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg);
static int garb_quizSexCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg);
static int garb_quizAgeCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg);

static inline int get_start_time(const struct tm *t)
{
	return t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static garbState_t garb_info;
static int viewership_offset = 0;
static int garb_quiz_index  = MAX_VIEWERS-1;

static inline int viewer_count()
{
	return garb_info.hh.count + garb_info.guests;
}

void garb_init()
{
	pthread_mutex_init(&garb_info.channels.lock, NULL);
	garb_info.channels.current = CHANNEL_NONE;
	memset(&garb_info, 0, sizeof(garb_info));
	garb_resetViewership();
	garb_load();
#ifdef GARB_TEST
	garb_test();
#endif
	pthread_create(&garb_info.thread, 0, garb_thread, 0);
}

void garb_terminate()
{
	pthread_cancel(garb_info.thread);
	pthread_join(garb_info.thread, NULL);
	garb_save();
	for (int i = 0; i<garb_info.hh.count; i++)
		FREE(garb_info.viewership[i].viewer.name);
	garb_info.history.tail = NULL;
	free_elements(&garb_info.history.head);
	pthread_mutex_destroy(&garb_info.channels.lock);
}

static int garb_viewershipCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: %d %s\n", __func__, interface_commandName(cmd->command));
	switch (cmd->command)
	{
		case interfaceCommand0: // guest
			garb_quiz_index = viewer_count();
			interface_showConfirmationBox(
				"Please specify your gender:\n\n"
				"1. Male\n"
				"2. Female\n"
				"\nUse numeric remote buttons",
				thumbnail_question, garb_quizSexCallback, NULL);
			return 1;
		case interfaceCommand9: // next page
			{
				if (viewership_offset + 8 >= viewer_count())
					viewership_offset = 0;
				else
					viewership_offset += 8;
				garb_askViewership();
			}
			return 1;
		case interfaceCommand1: case interfaceCommand2: case interfaceCommand3:
		case interfaceCommand4: case interfaceCommand5: case interfaceCommand6:
		case interfaceCommand7: case interfaceCommand8:
		{
			int index = viewership_offset + cmd->command - interfaceCommand1;
			if (index < viewer_count()) {
				switch (garb_info.viewership[index].state)
				{
					case stateEmpty:
						break;
					case stateMember:
						garb_info.viewership[index].state = stateMemberWatching;
						garb_info.viewers++;
						break;
					case stateMemberWatching:
						garb_info.viewership[index].state = stateMember;
						garb_info.viewers--;
						break;
					case stateGuestWatching:
						garb_info.viewership[index].state = stateEmpty;
						garb_info.viewers--;
						garb_info.guests--;
						if (viewership_offset >= viewer_count())
							viewership_offset = 0;
				}
				if (garb_info.viewers == 0) {
					garb_askViewership();
					return 1;
				}
				interface_playControlRefresh(0);
				interface_hideMessageBox();
				return 0;
			}
			return 1;
		}
		case DIKS_HOME:
		case interfaceCommandExit:
		case interfaceCommandGreen:
		case interfaceCommandRed:
			if (garb_info.viewers)
				return 0;
			// fall through
		default:
			return 1;
	}
}

int garb_quizSexCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: %s\n", __func__, interface_commandName(cmd->command));
	switch (cmd->command)
	{
		case interfaceCommand1:
		case interfaceCommand2:
			garb_info.viewership[garb_quiz_index].viewer.id[0] = '1' + (cmd->command - interfaceCommand1);
			interface_showConfirmationBox(
				"How old are you?\n\n"
				"1. Up to  8 y.o.\n"
				"2. Up to 15 y.o.\n"
				"3. Up to 25 y.o.\n"
				"4. Up to 45 y.o.\n"
				"5. Up to 65 y.o.\n"
				"6. Older than 65\n"
				"\nUse numeric remote buttons",
				thumbnail_question, garb_quizAgeCallback, NULL);
			return 1;
#ifdef ALLOW_NO_VIEWERSHIP
		case interfaceCommandExit:
			garb_resetViewership();
			return 0;
#endif
		default:;
	}
	return 1;
}

int garb_quizAgeCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: %s\n", __func__, interface_commandName(cmd->command));
	switch (cmd->command)
	{
		case interfaceCommand1: case interfaceCommand2: case interfaceCommand3:
		case interfaceCommand4: case interfaceCommand5: case interfaceCommand6:
			garb_info.viewership[garb_quiz_index].viewer.id[1] = '1' + (cmd->command - interfaceCommand1);
			memcpy(&garb_info.viewership[garb_quiz_index].viewer.id[2], "000", 4);
			garb_info.viewership[garb_quiz_index].viewer.name = garb_info.viewership[garb_quiz_index].viewer.id;
			garb_info.viewership[garb_quiz_index].state = stateGuestWatching;
			garb_info.viewers++;
			garb_info.guests++;
			interface_playControlRefresh(0);
			return 0;
#ifdef ALLOW_NO_VIEWERSHIP
		case interfaceCommandExit:
			garb_resetViewership();
			return 0;
#endif
		default:;
	}
	return 1;
}

static void garb_printMembers(char text[])
{
	char line[MENU_ENTRY_INFO_LENGTH];
	strcpy(text, "Please, choose viewership:\n\n");

	int count = viewer_count();
	for (int i = 0; i < 8 && i+viewership_offset < count; i++) {
		snprintf(line, sizeof(line), "%d. [%s] %s\n", i+1,
			garb_info.viewership[i+viewership_offset].state == stateMember ? "  " : "x", garb_info.viewership[i+viewership_offset].viewer.name);
		strcat(text, line);
	}
	if (count > 8)
		strcat(text, "\n9. Next page...");
	if (count < MAX_VIEWERS)
		strcat(text, "\n0. Add guest");
	strcat(text, "\n\nTo answer, use numeric remote buttons\n");
}

void garb_checkViewership()
{
	if (time(0) - garb_info.lastStop > VIEWERSHIP_TIMEOUT)
		garb_resetViewership();
	if (garb_info.viewers == 0) {
		viewership_offset = 0;
		garb_askViewership();
	}
}

void garb_askViewership()
{
	char text[MAX_MESSAGE_BOX_LENGTH];
	garb_printMembers(text);
	interface_showConfirmationBox(text, thumbnail_account_active, garb_viewershipCallback, NULL);
}

void garb_resetViewership()
{
	for (int i = garb_info.hh.count + garb_info.guests; i>=garb_info.hh.count; i--)
		garb_info.viewership[i].state = stateEmpty;
	garb_info.guests = 0;
	for (int i = 0; i<garb_info.hh.count; i++)
		garb_info.viewership[i].state = stateMember;
	garb_info.viewers = 0;
	garb_quiz_index = MAX_VIEWERS-1;
}

void garb_load()
{
	int fd = open(GARB_CONFIG, O_RDONLY);
	if (fd < 0) {
		eprintf("%s: failed to open garb config: %s\n", __FUNCTION__, strerror(errno));
		return;
	}
	char buffer[BUFFER_SIZE];
	ssize_t rd_len = read(fd, buffer, sizeof(buffer)-1);
	if (rd_len <= 0) {
		eprintf("%s: failed to read garb config: %s\n", __FUNCTION__, strerror(errno));
		close(fd);
		return;
	}
	close(fd);
	buffer[rd_len] = 0;
	cJSON *config = cJSON_Parse(buffer);
	if (!config) {
		eprintf("%s: failed to parse garb config\n", __FUNCTION__);
		return;
	}

	garb_info.hh.number = objGetInt(config, "number", 0);
	garb_info.hh.device = objGetInt(config, "device", 0);
	garb_info.hh.count  = 0;
	garb_info.viewers = 0;
	garb_info.guests  = 0;

	cJSON *members = cJSON_GetObjectItem(config, "members");
	cJSON *m;
	for (int i = 0; (m = cJSON_GetArrayItem(members, i)); i++) {
		char  *id = objGetString(m, "id", NULL);
		if (!id)
			continue;

		houseHoldMember_t *viewer = &garb_info.viewership[garb_info.hh.count].viewer;
		strncpy(viewer->id, id, sizeof(viewer->id)-1);

		char *name = objGetString(m, "name", NULL);
		viewer->name = strdup(name ? name : viewer->id);
		garb_info.viewership[garb_info.hh.count].state = stateMember;
		garb_info.hh.count++;
	}
	if (garb_info.hh.count == 0)
		eprintf("%s: (!) no HH members specified!\n", __FUNCTION__);

	free(config);
}

#ifdef GARB_TEST
static void add_member(const char id[GARB_ID_LENGTH], const char *name)
{
	strncpy(garb_info.viewership[garb_info.hh.count].viewer.id, id, GARB_ID_LENGTH);
	garb_info.viewership[garb_info.hh.count].viewer.name =
		strdup(name ? name : garb_info.viewership[garb_info.hh.count].viewer.id);
	garb_info.hh.count++;
}

void garb_test()
{
	if (garb_info.hh.count > 0)
		return;
	eprintf("%s: in\n", __FUNCTION__);
	if (garb_info.hh.number == 0)
		garb_info.hh.number = 123456;
	if (garb_info.hh.device == 0)
		garb_info.hh.device = 2;
	add_member("A",   "Афанасий");
	add_member("B",   "Борис");
	add_member("ABC", "Тимофей");
	char id[GARB_ID_LENGTH];
	for (int i=3; i<MAX_VIEWERS-2; i++) {
		snprintf(id, sizeof(id), "C%d", i);
		add_member(id, NULL);
	}
}
#endif

void garb_save()
{
	rename(GARB_FDD, GARB_FDD ".prev");
	FILE *f = fopen(GARB_FDD, "w");
	if (!f) {
		eprintf("%s: failed to create file: %s\n", __FUNCTION__, strerror(errno));
		return;
	}
	garbWatchHistory_t *hist;
	for (list_element_t *el = garb_info.history.head; el; el = el->next) {
		hist = el->data;
		if (hist->channel != CHANNEL_NONE) {
			fprintf(f, "%06d %d %d %c %ld %d 0 %02d %s\n",
				garb_info.hh.number,
				garb_info.hh.device,
				get_start_time(&hist->start_time),
				hist->id[0] == '1' || hist->id[0] == '2' ? 'G' : 'T',
				hist->duration,
				hist->channel,
				0x02,
				hist->id);
		}
	}
	fclose(f);
}

void garb_startWatching(int channel)
{
	dprintf("%s: %d\n", __func__, channel);
	pthread_mutex_lock(&garb_info.channels.lock);
	if (channel != CHANNEL_CUSTOM) {
		garb_info.channels.state[channel].start_time = time(0);
		garb_info.channels.current = channel;
	}
	pthread_mutex_unlock(&garb_info.channels.lock);
	garb_checkViewership();
}

static inline int sane_check(time_t watched)
{
	return watched > 0 && watched <= WATCH_PERIOD;
}

void garb_stopWatching(int channel)
{
	dprintf("%s: %d\n", __func__, channel);
	pthread_mutex_lock(&garb_info.channels.lock);
	garb_info.channels.current = CHANNEL_NONE;
	if (channel != CHANNEL_CUSTOM) {
		time_t now;
		time(&now);
		time_t watched = now - garb_info.channels.state[channel].start_time;
		dprintf("%s: channel %2d watched %d\n", __func__, channel, watched);
		if (sane_check(watched))
			garb_info.channels.state[channel].duration += watched;
		else
			eprintf("%s: ignore watch value %d for %d channel\n", __FUNCTION__, watched, channel);
	}
	pthread_mutex_unlock(&garb_info.channels.lock);
	time(&garb_info.lastStop);
}

void garb_gatherStats(time_t now)
{
#ifdef DEBUG
	struct tm broken_time;
	localtime_r(&now, &broken_time);
	eprintf("%s: %02d:%02d:%02d\n", __func__, broken_time.tm_hour, broken_time.tm_min, broken_time.tm_sec);
#endif

	pthread_mutex_lock(&garb_info.channels.lock);

	if (garb_info.channels.current != CHANNEL_NONE) {
		time_t watched = now - garb_info.channels.state[garb_info.channels.current].start_time;
		if (sane_check(watched))
			garb_info.channels.state[garb_info.channels.current].duration += watched;
		garb_info.channels.state[garb_info.channels.current].start_time = now;
	}
	int max_channel = CHANNEL_NONE;
	for (int i = 0; i<MAX_MEMORIZED_SERVICES; i++) {
		if (garb_info.channels.state[i].duration >= WATCH_THRESHOLD)
			max_channel = i;
		garb_info.channels.state[i].duration = 0;
	}

#if 0
	garbWatchHistory_t *tail = garb_info.history.tail ? garb_info.history.tail->data : NULL;
	if (tail && tail->channel == max_channel && strcmp(tail->id, garb_info.viewership.id) == 0)
		tail->duration += WATCH_PERIOD;
	else {
		list_element_t *new_tail = allocate_element(sizeof(garbWatchHistory_t));
		if (new_tail) {
			garbWatchHistory_t *hist = new_tail->data;

			now -= WATCH_PERIOD;
			localtime_r(&now, &hist->start_time);
			hist->duration   = WATCH_PERIOD;
			hist->channel    = max_channel;
			memcpy(hist->id, garb_info.viewership.id, sizeof(hist->id));

			if (garb_info.history.tail)
				garb_info.history.tail->next = new_tail;
			else
				garb_info.history.head = new_tail;
			garb_info.history.tail = new_tail;
		} else
			eprintf("%s: (!) failed to allocate new history entry!\n", __FUNCTION__);
	}
#endif

	pthread_mutex_unlock(&garb_info.channels.lock);
}

void *garb_thread(void *notused)
{
	time_t now, timeout;
	struct tm t;

	time(&now);
	localtime_r(&now, &t);
	timeout = 30 - (t.tm_sec % 30);
	sleep(timeout);

	for (;;) {
		time(&now);
		garb_gatherStats(now);
		garb_save();
		localtime_r(&now, &t);
		timeout = 30 - (t.tm_sec % 30);
		sleep(timeout);
	}
	return NULL;
}

void garb_showStats()
{
	char text[MAX_MESSAGE_BOX_LENGTH] = {0};
	char line[BUFFER_SIZE];
	garbWatchHistory_t *hist;
	EIT_service_t *service;
	char *name;
	struct tm t;
	time_t end;

	for (list_element_t *el = garb_info.history.head; el; el = el->next) {
		hist = el->data;
		service = NULL;
		if (hist->channel != CHANNEL_NONE)
			service = offair_getService(hist->channel);
		name = service ? dvb_getServiceName(service) : "Many";
		end = mktime(&hist->start_time) + hist->duration;
		localtime_r(&end, &t);
		snprintf(line, sizeof(line), "%s: %02d:%02d:%02d-%02d:%02d:%02d\n",
			name,
			hist->start_time.tm_hour, hist->start_time.tm_min, hist->start_time.tm_sec,
			t.tm_hour, t.tm_min, t.tm_sec);
		strcat(text, line);
	}
	interface_showMessageBox(text, thumbnail_epg, 0);
}

void garb_drawViewership()
{
	if ( !interfaceInfo.showMenu &&
	     interfacePlayControl.enabled &&
	     interfacePlayControl.visibleFlag)
	{
		char text[MAX_MESSAGE_BOX_LENGTH] = {0};
		int count = viewer_count();
		if (garb_info.viewers == 0)
			strcpy(text, _T("LOGIN"));
		else
		for (int i = 0; i<count; i++) {
			if (garb_info.viewership[i].state == stateGuestWatching ||
			    garb_info.viewership[i].state == stateMemberWatching)
			{
				if (text[0])
					strcat(text, "\n");
				strcat(text, garb_info.viewership[i].viewer.name);
			}
		}
		interface_displayTextBox(interfaceInfo.clientX, interfaceInfo.clientY, text, NULL, 0, NULL, 0);
	}
}
