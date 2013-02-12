#ifdef STSDK

/*
 stsdk.c

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

#include "stsdk.h"

#include "debug.h"
#include "client.h"
#include "app_info.h"
#include "interface.h"
#include "StbMainApp.h"

#include <semaphore.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <gfx.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define RPC_POOL_SIZE (32)

//#define RPC_POOL_TRACE
//#define RPC_TRACE eprintf
#ifndef RPC_TRACE
#define RPC_TRACE(x...)
#endif
//#define RPC_DUMP "/var/log/mainapp_rpc.log"

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct
{
	unsigned int id;
	void      *pArg;
	rpcCallback_t callback;
#ifdef DEBUG
	elcdRpcCommand_t cmd;
#endif
#ifdef RPC_POOL_TRACE
	char      *msg;
#endif
} rpc_t;

typedef struct
{
	unsigned int    id;
	elcdRpcType_t   type;
	cJSON         * jsonResult;
	char          * pArg;
	sem_t           lock;
} rpcSync_t;

typedef struct
{
	socketClient_t socket;
	rpc_t waiting[RPC_POOL_SIZE];
	pthread_t thread;
} rpcPool_t;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static void  st_syncCallback(elcdRpcType_t type, cJSON *result, void* pArg);
static void* st_poolThread(void* pArg);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

#ifdef ENABLE_DVB
static const struct { fe_modulation_t value; const char *name; } modulation_names[] =
{
	{QPSK,    "qpsk"},
	{QAM_16,  "qam16"},
	{QAM_32,  "qam32"},
	{QAM_64,  "qam64"},
	{QAM_128, "qam128"},
	{QAM_256, "qam256"},
	{VSB_8,   "vsb8"},
	{VSB_16,  "vsb16"},
	{PSK_8,   "psk8"},
	{APSK_16, "apsk16"},
	{APSK_32, "apsk32"},
	{DQPSK,   "dqpsk"},
	{0,NULL}
};
#endif
static rpcPool_t pool;

#ifdef RPC_DUMP
static int st_rpc_fd = -1;
#endif

/** request_id should never be zero */
static unsigned int request_id = 1;
static inline unsigned int get_id()
{
	return request_id+=2;
}
static int needRestart = 0;

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

int st_init(void)
{
	int res;

	memset(&pool, 0, sizeof(pool));

#ifdef RPC_DUMP
	st_rpc_fd = open(RPC_DUMP, O_CREAT|O_WRONLY);
	if (st_rpc_fd < 0)
		exit(110);
#endif

	if( (res = client_create(&pool.socket, ELCD_SOCKET_FILE, NULL, NULL, NULL, NULL)) != 0 )
	{
		eprintf("%s: failed to connect elcd socket\n", __FUNCTION__);
		return res;
	}

	res = pthread_create( &pool.thread, NULL, st_poolThread, NULL );
	if (res != 0)
		eprintf("%s: (!) failed to create pool thread: %s\n", __FUNCTION__, strerror(res));

#ifdef ENABLE_DVB
	elcdRpcType_t type = elcdRpcInvalid;
	cJSON *result = NULL;
	if ( st_rpcSync(elcmd_dvbtuners, NULL, &type, &result ) != 0 || type != elcdRpcResult || result == NULL )
	{
		cJSON_Delete(result);
		return -1;
	}

	if (result->type == cJSON_Array)
	{
		tunerFormat tuner;
		for (tuner = inputTuner0; tuner < inputTuners && appControlInfo.tunerInfo[tuner].status != tunerNotPresent; tuner++);
		dprintf("%s: tuner %d\n", __func__, tuner);

		for (int i = 0; i<TUNER_MAX_NUMBER && tuner < inputTuners; i++)
		{
			cJSON *t = cJSON_GetArrayItem( result, i );
			if (!t)
				break;
			if (t->type == cJSON_String)
			{
				if (strcasecmp( t->valuestring, "DVB-T" ) == 0) {
					appControlInfo.tunerInfo[tuner].type = DVBT;
					appControlInfo.tunerInfo[tuner].caps = tunerDVBT;
				} else
				if (strcasecmp( t->valuestring, "DVB-S" ) == 0) {
					appControlInfo.tunerInfo[tuner].type = DVBS;
					appControlInfo.tunerInfo[tuner].caps = tunerDVBS;
				} else
				if (strcasecmp( t->valuestring, "DVB-C" ) == 0) {
					appControlInfo.tunerInfo[tuner].type = DVBC;
					appControlInfo.tunerInfo[tuner].caps = tunerDVBC;
				} else
					continue;

				appControlInfo.tunerInfo[tuner].status = tunerInactive;
				appControlInfo.tunerInfo[tuner].adapter = ADAPTER_COUNT+i;

				tuner++;
			}
		}
	}
	cJSON_Delete(result);
#endif

	return res;
}

void st_terminate(void )
{
	if( pool.thread != 0 )
	{
		pthread_cancel( pool.thread );
		eprintf("%s: waiting pool thread to finish...\n", __FUNCTION__);
		pthread_join( pool.thread, NULL );
		pool.thread = 0;
	}
	client_destroy(&pool.socket);
#ifdef RPC_DUMP
	close(st_rpc_fd);
	st_rpc_fd = -1;
#endif
}

static inline void st_poolFreeAt(int i)
{
	pool.waiting[i].id = 0;
#ifdef RPC_POOL_TRACE
	free(pool.waiting[i].msg);
	pool.waiting[i].msg = NULL;
#endif
}

#ifdef RPC_POOL_TRACE
static void st_poolPrint(void)
{
	int empty = 1;
	int i;
	for( i = 0; i<RPC_POOL_SIZE; i++)
		if( pool.waiting[i].id )
		{
			printf("  pool[%2u]: %6u %s\n", i, pool.waiting[i].id, pool.waiting[i].msg);
			empty = 0;
		}
	if( empty )
		printf("  pool is empty\n");
}
#else
#define st_poolPrint()
#endif

int st_rpcAsync(elcdRpcCommand_t cmd, cJSON* params, rpcCallback_t callback, void *pArg)
{
	int res = -1;

	int i;
	for( i = 0; i<RPC_POOL_SIZE; i++ )
		if( pool.waiting[i].id == 0 )
			break;
	if( i >= RPC_POOL_SIZE )
	{
		eprintf("%s: RPC pool is full\n", __FUNCTION__);
		return -1;
	}

	unsigned int id = get_id();
	char *msg = rpc_request( rpc_cmd_name(cmd), id, params );
	if( !msg )
	{
		eprintf("%s: failed to create RPC message\n", __FUNCTION__);
		return -1;
	}
	pool.waiting[i].id   = id;
	pool.waiting[i].callback = callback;
	pool.waiting[i].pArg = pArg;
#ifdef DEBUG
	pool.waiting[i].cmd  = cmd;
#endif
#ifdef RPC_POOL_TRACE
	pool.waiting[i].msg  = msg;
	dprintf("%s[%2d]: -> %s\n", __FUNCTION__, i, msg);
#endif
	RPC_TRACE("st: -> %s\n", msg);
#ifdef RPC_DUMP
	write(st_rpc_fd, msg, strlen(msg));
	write(st_rpc_fd, "\n", 1);
#endif
	if( client_write( &pool.socket, msg, strlen(msg)+1 ) > 0 )
	{
		res = i;
		st_poolPrint();
	}
	else
	{
		eprintf("%s: failed to write %s\n", __FUNCTION__, rpc_cmd_name(cmd));
		st_poolFreeAt(i);
	}
#ifndef RPC_POOL_TRACE
	free(msg);
#endif
	return res;
}

void st_cancelAsync(int index, int execute)
{
	if (index < 0 || index >= RPC_POOL_SIZE)
	{
		eprintf("%s: index %d is out of range!\n", __FUNCTION__, index);
		return;
	}
#ifdef RPC_POOL_TRACE
	dprintf("%s[%2d]: cancel %s\n", __FUNCTION__, index, pool.waiting[index].msg);
#endif
	if (execute && pool.waiting[index].callback)
	{
		pool.waiting[index].callback(elcdRpcInvalid, NULL, pool.waiting[index].pArg);
	}
	st_poolFreeAt(index);
}

void st_syncCallback(elcdRpcType_t type, cJSON *result, void* pArg)
{
	rpcSync_t *s = pArg;
	s->type       = type;
	s->jsonResult = result;
	sem_post(&s->lock);
}

int st_rpcSync(elcdRpcCommand_t cmd, cJSON* params, elcdRpcType_t *type, cJSON **result)
{
	return st_rpcSyncTimeout(cmd, params, RPC_TIMEOUT, type, result);
}

int st_rpcSyncTimeout(elcdRpcCommand_t cmd, cJSON* params, int timeout , elcdRpcType_t *type, cJSON **result)
{
	rpcSync_t s;

	s.type = elcdRpcInvalid;
	if (sem_init(&s.lock, 0, 0) != 0 )
	{
		eprintf("%s: failed to create semaphore: %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	int i;
	if( (i = st_rpcAsync( cmd, params, st_syncCallback, &s )) < 0 )
	{
		sem_destroy(&s.lock);
		return -1;
	}

	struct timespec t;
	t.tv_sec  = time(NULL)+timeout;
	t.tv_nsec = 0;
	sem_timedwait(&s.lock, &t);

	sem_destroy(&s.lock);
	if( s.type == elcdRpcInvalid )
	{
#ifndef RPC_POOL_TRACE
		dprintf("%s: canceled rpc[%2u]: %6u %s\n", __FUNCTION__, i, pool.waiting[i].id, rpc_cmd_name(pool.waiting[i].cmd));
#else
		dprintf("%s: canceled rpc[%2u]: %s\n", __FUNCTION__, i, pool.waiting[i].msg);
#endif
		st_poolFreeAt(i);
		st_poolPrint();
		return -2;
	}
	if (type) {
		*type = s.type;
	}

	if (!result) {
		cJSON_Delete(s.jsonResult);
	}
	else {
		*result = s.jsonResult;
	}
	return 0;
}

int st_isOk(elcdRpcType_t type, cJSON *res, const char *msg)
{
	if ( type != elcdRpcResult || !res || res->type != cJSON_String ) {
		eprintf("%s failed: %s\n", msg, res&&res->type==cJSON_String?res->valuestring:"unknown error");
		return 0;
	}
	if ( strcmp(res->valuestring, "ok") ) {
		eprintf("%s not successfull: %s\n", msg, res->valuestring);
		return 0;
	}
	return 1;
}

static void st_poolThreadCleanup(void* pArg)
{
	eprintf("%s: out, cancelling waiting RPCs\n", __FUNCTION__);
	st_poolPrint();
	int i;
	for( i = 0; i < RPC_POOL_SIZE; i++ )
		if( pool.waiting[i].id )
		{
			cJSON *err = cJSON_CreateString( "canceled" );
			pool.waiting[i].callback( elcdRpcError, err, pool.waiting[i].pArg );
			st_poolFreeAt(i);
			cJSON_Delete(err);
		}
}

static void* st_poolThread(void* pArg)
{
	char buf[BUFFER_SIZE];
	cJSON *msg = NULL;

	pthread_cleanup_push(st_poolThreadCleanup, NULL);
	for(;;)
	{
		pthread_testcancel();

		if (client_read( &pool.socket, buf, sizeof(buf)) <= 0)
		{
			usleep(50000);
			continue;
		}

		RPC_TRACE("st: <- %s\n", buf);
		msg = cJSON_Parse( buf );
		if( !msg )
		{
			eprintf("%s: failed to parse message: '%s'\n", __FUNCTION__, buf);
			continue;
		}

		elcdRpcType_t type = elcdRpcInvalid;
		cJSON *value = NULL;
		cJSON *id = cJSON_GetObjectItem(msg, "id");
		if( !id || id->type != cJSON_Number || id->valueint == 0 )
		{
			eprintf("%s: missing id\n", __FUNCTION__);
			goto type_known;
		}
		value = cJSON_GetObjectItem( msg, "method" );
		if( value != NULL && value->type == cJSON_String )
		{
			type = elcdRpcRequest;
			goto type_known;
		}
		value = cJSON_DetachItemFromObject( msg, "result" );
		if( value != NULL && value->type != cJSON_NULL )
		{
			type = elcdRpcResult;
			goto type_known;
		}
		value = cJSON_DetachItemFromObject( msg, "error" );
		if( value != NULL && value->type != cJSON_NULL )
		{
			type = elcdRpcError;
		}
type_known:
		dprintf( "%s:   <- %s type %d\n", __FUNCTION__, buf, type);
		switch( type )
		{
			case elcdRpcInvalid:
				eprintf("%s: malformed message: '%s'\n", __FUNCTION__, buf);
				break;
			case elcdRpcRequest:
				/// TODO
				eprintf("%s: don't know what to do with request %s\n", __FUNCTION__, value->valuestring);
				break;
			case elcdRpcError:
			case elcdRpcResult:
			{
				int i;
				for( i = 0; i < RPC_POOL_SIZE; i++ )
					if( (unsigned int)id->valueint == pool.waiting[i].id )
					{
						pool.waiting[i].callback( type, value, pool.waiting[i].pArg );
						st_poolFreeAt(i);
						break;
					}
				if( i >= RPC_POOL_SIZE )
				{
					eprintf("%s: lost message %6u\n", __FUNCTION__, (unsigned int)id->valueint);
					cJSON_Delete(value);
				}
				st_poolPrint();
				break;
			}
		}

		cJSON_Delete(msg);
		msg = NULL;
	}
	pthread_cleanup_pop(1);

	pthread_exit(NULL);
}

#ifdef ENABLE_DVB
void st_setTuneParams(tunerFormat tuner, cJSON *params)
{
	cJSON_AddItemToObject(params, "tuner", cJSON_CreateNumber(st_getTunerIndex(tuner)) );
	switch (appControlInfo.tunerInfo[tuner].type)
	{
		case DVBC:
			for (int i=0; modulation_names[i].name != NULL; i++)
				if (modulation_names[i].value == appControlInfo.dvbcInfo.modulation) {
					cJSON_AddItemToObject(params, "modulation", cJSON_CreateString(modulation_names[i].name));
					break;
				}
			cJSON_AddItemToObject(params, "symbolrate", cJSON_CreateNumber( appControlInfo.dvbcInfo.symbolRate ));
			break;
		case DVBS:
			cJSON_AddItemToObject(params, "symbolrate", cJSON_CreateNumber( appControlInfo.dvbsInfo.symbolRate ));
			if (appControlInfo.dvbsInfo.polarization)
				cJSON_AddItemToObject(params, "vertical", cJSON_CreateTrue());
			break;
		default:;
	}
}
#endif // ENABLE_DVB

static int stHelper_getFormatHeight(const char *format)
{
	const char *s = strchr(format, 'x');
	if(s)
		s++;
	else
		s = format;
	return strtol(s, NULL, 10);
}

static int stHelper_getFormatWidth(int height)
{
	switch (height) {
		case 1080: return 1920;
		case  768: return 1024;
		case  720:
		case 1024: return 1280;
	}
	return 720;
}

static void stHelper_setDirectFBMode(int width, int height)
{
	char mode[MAX_GRAPHICS_MODE_STRING];
	snprintf(mode, sizeof(mode), "%dx%d", width, height);
	DirectFBSetOption("mode", mode);
}

static int stHelper_waitForFBdevice(const char *fb_name)
{
	int i;
	int ret = -1;

	for(i = 0; i < 1000; i ++) {
		if(helperFileExists(fb_name)) {
			ret = 0;
			break;
		}
		usleep(1000);
	}
//	printf("%s[%d]: i=%d\n", __FILE__, __LINE__, i);
//	if(i == 100)
//		printf("%s[%d]: Cant open fb0!!!\n", __FILE__, __LINE__);
	return ret;
}

static int stHelper_sendToSocket(const char *socketPath, const char *cmd)
{
//TODO: use socket client
	char buf[256];
	snprintf(buf, sizeof(buf), "StbCommandClient -f %s '%s'", socketPath, cmd);

	system(buf);
	return 0;
}

static int stHelper_indicatorShowVideoFormat(const char *name)
{
	char buf[16] = "\0";
	int f_width, f_height, f_framerate;
	char f_field;
	
	if(strchr(name, 'x')) { //720x576p50 format
		if(sscanf(name, "%dx%d%c%d", &f_height, &f_width, &f_field, &f_framerate) == 4) {
			sprintf(buf, "n10 %d %d%c", f_width, f_framerate, (f_field == 'i')?'[':'p');
		} else if(sscanf(name, "%dx%d%c", &f_height, &f_width, &f_field) == 3) {
			sprintf(buf, "n10 %d %c", f_width, (f_field == 'i')?'[':'p');
		}
	} else { //1080p60 format
		if(sscanf(name, "%d%c%d", &f_width, &f_field, &f_framerate) == 3) {
			sprintf(buf, "n10 %d %d%c", f_width, f_framerate, (f_field == 'i')?'[':'p');
		}
	}
	if(buf[0] == 0)
		sprintf(buf, "n10 %s", name);

	stHelper_sendToSocket("/tmp/frontpanel", "h 400 200");
	stHelper_sendToSocket("/tmp/frontpanel", buf);

	return 0;
}

int st_setVideoFormat(const char *name)
{
	elcdRpcType_t type;
	cJSON *res  = NULL;
	cJSON *mode = cJSON_CreateString(name);
	int    ret = 1;

	ret = st_rpcSync( elcmd_setvmode, mode, &type, &res );
//	ret = st_rpcSyncTimeout( elcmd_setvmode, mode, 1, &type, &res );

	if( ret == 0 && type == elcdRpcResult && res && res->type == cJSON_String ) {
		if( strcmp(res->valuestring, "ok") ) {
			eprintf("%s: failed: %s\n", __FUNCTION__, res->valuestring);
			ret = 1;
		} else {
			stHelper_indicatorShowVideoFormat(name);
		}
	} else if ( type == elcdRpcError && res && res->type == cJSON_String ) {
		eprintf("%s: error: %s\n", __FUNCTION__, res->valuestring);
		ret = 1;
	}
	cJSON_Delete(res);
	cJSON_Delete(mode);
	return ret;
}

void st_changeOutputMode(char *selectedFormat, char *previousFormat)
{
	int old_height, new_height;
	old_height = stHelper_getFormatHeight(previousFormat);
	new_height = stHelper_getFormatHeight(selectedFormat);

	if (old_height != new_height) {
		gfx_stopEventThread();
		gfx_terminate();
		stHelper_sendToSocket("/tmp/elcd.sock", "{\"method\":\"deinitfb\",\"params\":[],\"id\": 1}");
		st_setVideoFormat(selectedFormat);
		stHelper_sendToSocket("/tmp/elcd.sock", "{\"method\":\"initfb\",\"params\":[],\"id\": 1}");
		stHelper_waitForFBdevice("/dev/fb0");
		if (new_height < 720 && interfaceInfo.screenHeight >= 720) {
			// Reset graphics mode for small resolutions
			appControlInfo.outputInfo.graphicsMode[0] = 0;
			saveAppSettings();
		}
		if (appControlInfo.outputInfo.graphicsMode[0] == 0)
			stHelper_setDirectFBMode(stHelper_getFormatWidth(new_height), new_height);
		gfx_init(0, NULL);
		interface_resize();
		gfx_startEventThread();
		needRestart = 1;
	} else {
		st_setVideoFormat(selectedFormat);
	}

	return;
}

void st_reinitFb(char *currentFormat)
{
	stHelper_sendToSocket("/tmp/elcd.sock", "{\"method\":\"deinitfb\",\"params\":[],\"id\": 1}");
	if (appControlInfo.outputInfo.graphicsMode[0] == 0) {
		int height = stHelper_getFormatHeight(currentFormat);
		stHelper_setDirectFBMode(stHelper_getFormatWidth(height), height);
	}
	stHelper_sendToSocket("/tmp/elcd.sock", "{\"method\":\"initfb\",\"params\":[],\"id\": 1}");
	stHelper_waitForFBdevice("/dev/fb0");
}

void st_getFormatResolution(const char *format, int *width, int *height)
{
	*height = stHelper_getFormatHeight(format);
	*width  = stHelper_getFormatWidth(*height);
}

int st_needRestart(void)
{
	// FIXME: Looks like restart is not necessary, so return false for now
	return 0; //return needRestart;
}

int st_applyZoom(zoomPreset_t preset)
{
	char *name = 0;
	switch (preset) {
		case zoomScale:    name = "scale";    break;
		case zoomFitWidth: name = "fitwidth"; break;
		default:           name = "stretch";
	}
	elcdRpcType_t type;
	cJSON *res  = NULL;
	cJSON *param = cJSON_CreateString(name);

	st_rpcSyncTimeout(elcmd_setzoom, param, 1, &type, &res );
	int ret = !st_isOk(type, res, __FUNCTION__);
	cJSON_Delete(param);
	cJSON_Delete(res);
	return ret;
}

#endif // STSDK
