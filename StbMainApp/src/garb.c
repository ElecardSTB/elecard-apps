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
#include "output.h"
#include "l10n.h"
#include <cJSON.h>
#include <service.h>
#include <elcd-rpc.h>

#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define CHANNEL_NONE    (-1)
#define WATCH_PERIOD    (30)
#define WATCH_THRESHOLD (15)
#define VIEWERSHIP_TIMEOUT (30*60)

//#define ALLOW_NO_VIEWERSHIP

#define HH_NONE  (-1)
#define HH_GUEST (99)

#define MAX_VIEWERS 16
#define MAX_GUESTS  16

#define GARB_TEST
#define GARB_CONFIG_JSON			CONFIG_DIR "/garb.json"
#define GARB_FDD					CONFIG_DIR "/garb.fdd"

#define CURRENTMETER_I2C_BUS		"/dev/i2c-3"
#define CURRENTMETER_I2C_ADDR		0x1e
//#define CURRENTMETER_I2C_ADDR		0x50
#define CURRENTMETER_I2C_REG_ID		0x00
//#define CURRENTMETER_I2C_REG_VAL	0x04
#define CURRENTMETER_I2C_REG_VAL	0x00
#define CURRENTMETER_I2C_ID			0x1e

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef uint32_t guest_id;

typedef struct
{
	char id;
	char *name;
} houseHoldMember_t;

typedef struct
{
	uint32_t  members;
	int       guest_count;
	guest_id *guests;
	int       channel;
	struct tm start;
	time_t    duration;
} garbWatchHistory_t;

typedef struct
{
	struct hh {
		int number;
		int device;
		int count;
		houseHoldMember_t members[MAX_VIEWERS];
	} hh;

	struct registered {
		uint32_t members;
		guest_id guests[MAX_GUESTS];
		int      guest_count;
	} registered;
	time_t last_stop;

	struct watching {
		int      viewers;
		int      channel;
		time_t   start_time;
		uint32_t members;
		guest_id guests[MAX_GUESTS];
		int      guest_count;
	} watching;

	struct history {
		list_element_t *head;
		list_element_t *tail;
	} history;
	pthread_t thread;
	pthread_t thread_current;
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
static void *currentmeter_thread(void *notused);
static void currentmeter_close(void);

static void garb_printMembers(char text[]);
static int garb_viewershipCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg);
static int garb_quizSexCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg);
static int garb_quizAgeCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg);

static void addWatchHistory(int channel, time_t now, time_t duration);
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
currentmeter_t currentmeter;
/******************************************************************
* FUNCTION IMPLEMENTATION                                         *
*******************************************************************/
static inline int viewer_count(void)
{
	return garb_info.hh.count + garb_info.registered.guest_count;
}

void garb_init(void)
{
	currentmeter.high_value = 0;
	currentmeter.low_value = 0;
	currentmeter.i2c_bus = -1;
	
	memset(&garb_info, 0, sizeof(garb_info));
	garb_info.watching.channel = CHANNEL_NONE;
	garb_resetViewership();
	garb_load();
#ifdef GARB_TEST
	garb_test();
#endif
	pthread_create(&garb_info.thread, 0, garb_thread, 0);
	pthread_create(&garb_info.thread_current, 0, currentmeter_thread, 0);
}

void garb_terminate(void)
{
	pthread_cancel(garb_info.thread_current);
	pthread_join(garb_info.thread_current, NULL);
	currentmeter_close();
	pthread_cancel(garb_info.thread);
	pthread_join(garb_info.thread, NULL);
	garb_save();
	for (int i = 0; i<garb_info.hh.count; i++)
		FREE(garb_info.hh.members[i].name);
	garb_info.history.tail = NULL;
	free_elements(&garb_info.history.head);
}

static int garb_viewershipCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	dprintf("%s: %d %s\n", __func__, interface_commandName(cmd->command));
	switch (cmd->command)
	{
		case interfaceCommand0: // guest
			garb_quiz_index = garb_info.registered.guest_count;
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
			if (index < garb_info.hh.count) {
				uint32_t mask = 1 << index;
				if (garb_info.registered.members & mask)
					garb_info.watching.viewers--;
				else {
					garb_info.watching.viewers++;
					garb_info.watching.members |= mask;
				}
				garb_info.registered.members ^= mask;
			} else
			if (index < viewer_count()) {
				index -= garb_info.hh.count;
				if (index+1 < garb_info.registered.guest_count)
					memmove(&garb_info.registered.guests[index], &garb_info.registered.guests[index+1],
						sizeof(guest_id)*(garb_info.registered.guest_count-index-1));
				garb_info.watching.viewers--;
				garb_info.registered.guest_count--;
#ifdef DEBUG
				eprintf("%s: register guests %d:\n", __func__, garb_info.registered.guest_count);
				for (int i = 0 ; i  < garb_info.registered.guest_count; i++)
					printf("  %u\n", garb_info.registered.guests[i]);
				eprintf("%s: watching guests %d:\n", __func__, garb_info.watching.guest_count);
				for (int i = 0 ; i  < garb_info.watching.guest_count; i++)
					printf("  %u\n", garb_info.watching.guests[i]);
#endif
				if (viewership_offset >= viewer_count())
					viewership_offset = 0;
			} else
				return 1;
			if (garb_info.watching.viewers == 0) {
				garb_askViewership();
				return 1;
			}
			interface_playControlRefresh(0);
			interface_hideMessageBox();
			return 0;
		}
		case DIKS_HOME:
		case interfaceCommandExit:
		case interfaceCommandGreen:
		case interfaceCommandRed:
			if (garb_info.watching.viewers)
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
			garb_info.registered.guests[garb_quiz_index] = 10000 * (cmd->command - interfaceCommand0);
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
			garb_info.registered.guests[garb_quiz_index] += 1000 * (cmd->command - interfaceCommand0);
			garb_info.watching.viewers++;
			garb_info.registered.guest_count++;
			int i = 0;
			for (; i<garb_info.watching.guest_count; i++)
				if (garb_info.registered.guests[garb_quiz_index] == garb_info.watching.guests[i])
					break;
			if (i == garb_info.watching.guest_count)
				garb_info.watching.guests[garb_info.watching.guest_count++] = garb_info.registered.guests[garb_quiz_index];
#ifdef DEBUG
			eprintf("%s: add guest %2d %u\n", __func__, garb_quiz_index, garb_info.registered.guests[garb_quiz_index]);
			eprintf("%s: register guests %d:\n", __func__, garb_info.registered.guest_count);
			for (int i = 0 ; i  < garb_info.registered.guest_count; i++)
				printf("  %u\n", garb_info.registered.guests[i]);
			eprintf("%s: watching guests %d:\n", __func__, garb_info.watching.guest_count);
			for (int i = 0 ; i  < garb_info.watching.guest_count; i++)
				printf("  %u\n", garb_info.watching.guests[i]);
#endif
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
	int display_index = 0;
	int index = viewership_offset;
	while (display_index < 8 && index < garb_info.hh.count) {
		snprintf(line, sizeof(line), "%d. [%s] %s\n", display_index+1,
			garb_info.watching.members & (1 << index) ? "x" : "  ",
			garb_info.hh.members[index].name);
		strcat(text, line);
		index++;
		display_index++;
	}
	while (display_index < 8 && index < count) {
		snprintf(line, sizeof(line), "%d. [x] %u\n", display_index+1,
			garb_info.registered.guests[index-garb_info.hh.count]);
		strcat(text, line);
		index++;
		display_index++;
	}
	if (count > 8)
		strcat(text, "\n9. Next page...");
	if (count < MAX_VIEWERS)
		strcat(text, "\n0. Add guest");
	strcat(text, "\n\nTo answer, use numeric remote buttons\n");
}

void garb_checkViewership(void)
{
	if (time(0) - garb_info.last_stop > VIEWERSHIP_TIMEOUT)
		garb_resetViewership();
	if (garb_info.watching.viewers == 0) {
		viewership_offset = 0;
		garb_askViewership();
	}
}

void garb_askViewership(void)
{
	char text[MAX_MESSAGE_BOX_LENGTH];
	garb_printMembers(text);
	interface_showConfirmationBox(text, thumbnail_account_active, garb_viewershipCallback, NULL);
}

void garb_resetViewership(void)
{
	garb_info.registered.guest_count = 0;
	garb_info.watching.viewers = 0;
	garb_info.watching.members = 0;
	garb_info.watching.guest_count = 0;
	garb_quiz_index = MAX_VIEWERS-1;
}

static void add_member(char id, const char *name)
{
	garb_info.hh.members[garb_info.hh.count].id = id;
	if (name)
		garb_info.hh.members[garb_info.hh.count].name = strdup(name);
	else {
		garb_info.hh.members[garb_info.hh.count].name = strdup(" ");
		garb_info.hh.members[garb_info.hh.count].name[0] = id;
	}
	garb_info.hh.count++;
}

void garb_load(void)
{
	int fd = open(GARB_CONFIG_JSON, O_RDONLY);
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
	garb_info.watching.viewers = 0;
	garb_info.registered.guest_count  = 0;

	cJSON *members = cJSON_GetObjectItem(config, "members");
	cJSON *m;
	for (int i = 0; (m = cJSON_GetArrayItem(members, i)); i++) {
		char  *id = objGetString(m, "id", NULL);
		if (!id)
			continue;
		char *name = objGetString(m, "name", NULL);
		add_member(id[0], name);
	}
	if (garb_info.hh.count == 0)
		eprintf("%s: (!) no HH members specified!\n", __FUNCTION__);

	free(config);
}

#ifdef GARB_TEST
void garb_test(void)
{
	if (garb_info.hh.count > 0)
		return;
	eprintf("%s: in\n", __FUNCTION__);
	if (garb_info.hh.number == 0)
		garb_info.hh.number = 123456;
	if (garb_info.hh.device == 0)
		garb_info.hh.device = 2;
	add_member('A', "Афанасий");
	add_member('B', "Борис");
	add_member('C', "Тимофей");
	for (int i=3; i<MAX_VIEWERS-2; i++)
		add_member('A'+i, NULL);
}
#endif

void garb_save(void)
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
			fprintf(f, "%06d %d %d %c %ld %d 0 %02d ",
				garb_info.hh.number,
				garb_info.hh.device,
				get_start_time(&hist->start),
				'T',
				hist->duration,
				hist->channel,
				0x02);
			for (int i = 0; i < garb_info.hh.count; i++)
				if (hist-> members & (1 << i))
					fprintf(f, "%c", garb_info.hh.members[i].id);
			for (int i = 0; i < hist->guest_count; i++)
				fprintf(f, "%u", hist->guests[i]);
			fprintf(f, "\n");
		}
	}
	fclose(f);
}

void garb_startWatching(int channel)
{
	dprintf("%s: %d\n", __func__, channel);
	time(&garb_info.watching.start_time);
	garb_info.watching.channel = channel;
	garb_info.watching.members = garb_info.registered.members;
	memcpy(garb_info.watching.guests, garb_info.registered.guests, garb_info.registered.guest_count);
	garb_info.watching.guest_count = garb_info.registered.guest_count;
	garb_checkViewership();
}

static inline int sane_check(time_t watched)
{
	return watched > 0 && watched <= 2*WATCH_PERIOD;
}

void garb_stopWatching(int channel)
{
	dprintf("%s: %d\n", __func__, channel);
	time(&garb_info.last_stop);
	time_t duration = garb_info.last_stop - garb_info.watching.start_time;
	if (duration > WATCH_THRESHOLD) {
		struct tm t;
		localtime_r(&garb_info.last_stop, &t);
		addWatchHistory(channel, duration, garb_info.last_stop + WATCH_PERIOD - (t.tm_sec % WATCH_PERIOD));
	}
	garb_info.watching.channel = CHANNEL_NONE;
	garb_info.watching.start_time = 0;
}

static void addWatchHistory(int channel, time_t now, time_t duration)
{
	// Check if watched the same channel
	if (garb_info.history.tail) {
		garbWatchHistory_t *tail = garb_info.history.tail->data;
		if (tail->channel == channel &&
		    tail->members == garb_info.watching.members &&
		    tail->guest_count == garb_info.watching.guest_count &&
		   (tail->guest_count == 0 || memcmp(tail->guests, garb_info.watching.guests, tail->guest_count) == 0))
		{
			tail->duration += duration;
#ifdef DEBUG
			eprintf("%s: upd channel %3d members %08x duration %lu guests %d\n", __func__,
				tail->channel, tail->members, tail->duration, tail->guest_count);
			for (int i = 0; i<tail->guest_count; i++)
				printf("  %u\n", tail->guests[i]);
#endif
			return;
		}
	}
	// Else add new history entry and mark it as last watched for this viewer

	list_element_t *new_tail = allocate_element(sizeof(garbWatchHistory_t));
	if (!new_tail) {
		eprintf("%s: (!) failed to allocate new history entry!\n", __FUNCTION__);
		return;
	}
	garbWatchHistory_t *hist = new_tail->data;

	time_t start_time = now - WATCH_PERIOD;
	localtime_r(&start_time, &hist->start);
	hist->duration    = WATCH_PERIOD;
	hist->channel     = channel;
	hist->members     = garb_info.watching.members;
	if (garb_info.watching.guest_count)
		do {
			hist->guests = malloc(garb_info.watching.guest_count);
			if (!hist->guests) {
				eprintf("%s: out of memory!\n", __FUNCTION__);
				break;
			}
			hist->guest_count = garb_info.watching.guest_count;
			memcpy(hist->guests, garb_info.watching.guests, sizeof(guest_id)*hist->guest_count);
		} while (0);
#ifdef DEBUG
	eprintf("%s: add channel %3d members %08x duration %lu guests %d\n", __func__,
		hist->channel, hist->members, hist->duration, hist->guest_count);
	for (int i = 0; i<hist->guest_count; i++)
		printf("  %u\n", hist->guests[i]);
#endif

	if (garb_info.history.tail)
		garb_info.history.tail->next = new_tail;
	else
		garb_info.history.head = new_tail;
	garb_info.history.tail = new_tail;
}

void garb_gatherStats(time_t now)
{
#ifdef DEBUG
	struct tm broken_time;
	localtime_r(&now, &broken_time);
	eprintf("%s: %02d:%02d:%02d\n", __func__, broken_time.tm_hour, broken_time.tm_min, broken_time.tm_sec);
#endif

	// 1. Update current channel
	if (garb_info.watching.channel != CHANNEL_NONE) {
		time_t duration = now - garb_info.watching.start_time;
		if (duration > WATCH_THRESHOLD)
			addWatchHistory(garb_info.watching.channel, now, duration);
		garb_info.watching.start_time = now;
	}
	// 2. Update watching info
	garb_info.watching.members     = garb_info.registered.members;
	garb_info.watching.guest_count = garb_info.registered.guest_count;
	if (garb_info.watching.guest_count)
		memcpy(garb_info.watching.guests, garb_info.registered.guests,
			sizeof(guest_id)*garb_info.watching.guest_count);
}

void *garb_thread(void *notused)
{
	time_t now, timeout;
	struct tm t;

	time(&now);
	localtime_r(&now, &t);
	timeout = WATCH_PERIOD - (t.tm_sec % WATCH_PERIOD);
	if (timeout  < WATCH_PERIOD)
		timeout += WATCH_PERIOD;
	sleep(timeout);

	for (;;) {
		time(&now);
		garb_gatherStats(now);
		garb_save();
		localtime_r(&now, &t);
		timeout = WATCH_PERIOD - (t.tm_sec % WATCH_PERIOD);
		sleep(timeout);
	}
	return NULL;
}

static int32_t currentmeter_open(void)
{
	if(currentmeter.i2c_bus >= 0) {
		return 0;
	}

	currentmeter.i2c_bus = open(CURRENTMETER_I2C_BUS, O_RDWR);
	if(currentmeter.i2c_bus < 0) {
		fprintf(stderr, "Error: Could not open device `%s': %s\n",
				CURRENTMETER_I2C_BUS, strerror(errno));
		if(errno == EACCES)
			fprintf(stderr, "Run as root?\n");
		return -1;
	}
	return 0;
}

static void currentmeter_close(void)
{
	if(currentmeter.i2c_bus >= 0)
		close(currentmeter.i2c_bus);
}

static int32_t currentmeter_readReg(uint8_t reg_addr, uint8_t *value)
{
	struct i2c_rdwr_ioctl_data	work_queue;
	struct i2c_msg				msg[2];
	int32_t						ret;
	uint8_t						read_reg;

	if(currentmeter_open() != 0) {
		return -1;
	}

	work_queue.nmsgs = 2;
	work_queue.msgs = msg;

	msg[0].addr = CURRENTMETER_I2C_ADDR;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = CURRENTMETER_I2C_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &read_reg;

	ret = ioctl(currentmeter.i2c_bus, I2C_RDWR, &work_queue);
	if(ret < 0) {
		printf("%s:%s()[%d]: ERROR!! while reading i2c: ret=%d\n", __FILE__, __func__, __LINE__, ret);
		return ret;
	}
	*value = read_reg;
	return 0;
}

int32_t currentmeter_isExist(void)
{
	uint8_t val;

	if(currentmeter_open() != 0) {
		return 0;
	}
	if(currentmeter_readReg(CURRENTMETER_I2C_REG_ID, &val) != 0) {
		printf("%s:%s()[%d]: Cant read value!!!\n", __FILE__, __func__, __LINE__);
		return 0;
	}
	return 1; //now we use chip without id, so skip checking
	if(val != CURRENTMETER_I2C_ID) {
		printf("%s:%s()[%d]: Unknown device!!!\n", __FILE__, __func__, __LINE__);
		return 0;
	}
	return 1;
}

int32_t currentmeter_getValue(uint32_t *watt)
{
	uint32_t cur_val;
	uint8_t val;

	if(currentmeter_open() != 0) {
		return -1;
	}

	if(currentmeter_readReg(CURRENTMETER_I2C_REG_VAL, &val) != 0) {
		return -1;
	}
	cur_val = val << 8;
	if(currentmeter_readReg(CURRENTMETER_I2C_REG_VAL + 1, &val) != 0) {
		return -1;
	}
	cur_val += val;

	*watt = ((220 * cur_val) / 1000);
	return 0;
}

void currentmeter_setCalibrateHighValue(uint32_t val)
{
	currentmeter.high_value = val;
}

void currentmeter_setCalibrateLowValue(uint32_t val)
{
	currentmeter.low_value = val;
}

static int32_t currentmeter_hasPower(void)
{
	uint32_t cur_val = 0;
	if(currentmeter_getValue(&cur_val) != 0) {
		printf("%s:%s()[%d]: Cant get current power consumprion\n", __FILE__, __func__, __LINE__);
		return 0;
	}
	return (cur_val > (currentmeter.low_value + ((currentmeter.high_value - currentmeter.low_value) >> 2)));
}

static int32_t currentmeter_isPoweredOn(void)
{
	static uint32_t previousState = 0;
	int32_t has_power;
	int32_t state_changed;

	has_power = currentmeter_hasPower();
	state_changed = previousState ? !has_power : has_power;
	
	if(state_changed) {
		previousState = !previousState;
	}

	if(previousState) {
		return 1;
	}
	return 0;
}

static void *currentmeter_thread(void *notused)
{

	sleep(2);
	if(!currentmeter_isExist()) {
		return NULL;
	}

	while(1) {
		if(currentmeter.high_value == 0 ||
			currentmeter.low_value == 0 ||
			((currentmeter.high_value - currentmeter.low_value) < 5))
		{
			sleep (5);
			continue;
		}

		if(currentmeter_isPoweredOn()) {
			garb_askViewership();
		}
		sleep(1);
	}

	return NULL;
}

void garb_showStats(void)
{
	char text[MAX_MESSAGE_BOX_LENGTH] = {0};
	char line[BUFFER_SIZE];
	garbWatchHistory_t *hist;
	EIT_service_t *service;
	char *name;
	struct tm t;
	time_t end;

	if (garb_info.history.head == NULL) {
		strcpy(text, _T("LOADING"));
	} else {
		for (list_element_t *el = garb_info.history.head; el; el = el->next) {
			hist = el->data;
			service = NULL;
			if (hist->channel != CHANNEL_NONE)
				service = offair_getService(hist->channel);
			name = service ? dvb_getServiceName(service) : "Many";
			end = mktime(&hist->start) + hist->duration;
			localtime_r(&end, &t);
			size_t off = snprintf(line, sizeof(line), "%s: %02d:%02d:%02d-%02d:%02d:%02d ",
									name,
									hist->start.tm_hour, hist->start.tm_min, hist->start.tm_sec,
									t.tm_hour, t.tm_min, t.tm_sec);
			for (int i = 0; i < garb_info.hh.count; i++) {
				if (hist->members & (1 << i))
					line[off++] = garb_info.hh.members[i].id;
			}
			for (int i = 0; i < hist->guest_count; i++)
				off += sprintf(line+off, "%u", hist->guests[i]);
			line[off] = '\n';
			line[off+1] = 0;
			strcat(text, line);
		}
	}
	interface_showMessageBox(text, thumbnail_epg, 0);
}

void garb_drawViewership(void)
{
	if(	!interfaceInfo.showMenu &&
		interfacePlayControl.enabled &&
		interfacePlayControl.visibleFlag)
	{
		char text[MAX_MESSAGE_BOX_LENGTH] = {0};
		if (garb_info.watching.viewers == 0) {
			strcpy(text, _T("LOGIN"));
		} else {
			for (int i = 0; i < garb_info.hh.count; i++)
				if (garb_info.registered.members & (1 << i)) {
					if (text[0])
						strcat(text, "\n");
					strcat(text, garb_info.hh.members[i].name);
				}
		}
		for (int i = 0; i < garb_info.registered.guest_count; i++)
			sprintf(text+strlen(text), "\n%u", garb_info.registered.guests[i]);
		interface_displayTextBox(interfaceInfo.clientX, interfaceInfo.clientY, text, NULL, 0, NULL, 0);
	}
}
