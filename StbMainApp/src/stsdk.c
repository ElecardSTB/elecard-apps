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

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define RPC_POOL_SIZE (32)

//#define RPC_POOL_TRACE
//#define RPC_TRACE "/var/log/mainapp_rpc.log"

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
	elcdRpcType_t type;
	cJSON        *result;
	char         *pArg;
} rpcResult_t;

typedef struct
{
	unsigned int id;
	rpcResult_t  res;
	sem_t        lock;
} rpcSync_t;

typedef struct
{
	socketClient_t socket;
	rpc_t waiting[RPC_POOL_SIZE];
	pthread_t thread;
} rpcPool_t;

#ifdef ENABLE_DVB

typedef enum
{
	tunerNotFound = -1,
	tunerTypeDVBT = 0,
	tunerTypeDVBC,
	tunerTypeDVBS,
} st_tunerType_t;

#endif

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static void  st_syncCallback(elcdRpcType_t type, cJSON *result, void* pArg);
static void* st_poolThread(void* pArg);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

#ifdef ENABLE_DVB
static int st_tuner = tunerNotFound;
static st_tunerType_t st_tuners[TUNER_MAX_NUMBER];
#endif
static rpcPool_t pool;

#ifdef RPC_TRACE
static int st_rpc_fd = -1;
#endif

/** request_id should never be zero */
static unsigned int request_id = 1;
static inline unsigned int get_id()
{
	return request_id+=2;
}

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

int st_init(void)
{
	int res;

	memset(&pool, 0, sizeof(pool));

#ifdef RPC_TRACE
	st_rpc_fd = open(RPC_TRACE, O_CREAT|O_WRONLY);
	if (st_rpc_fd < 0)
		exit(110);
#endif

	if( (res = client_create(&pool.socket, ELCD_SOCKET_FILE, NULL, NULL, NULL, NULL)) != 0 )
	{
		eprintf("%s: failed to connect elcd socket\n", __FUNCTION__);
		return res;
	}

	res = pthread_create( &pool.thread, NULL, st_poolThread, NULL );
	if( res != 0 )
	{
		eprintf("%s: failed to create pool thread: %s\n", __FUNCTION__, strerror(res));
	}

#ifdef ENABLE_DVB
	elcdRpcType_t type = elcdRpcInvalid;
	cJSON *result = NULL;
	if ( st_rpcSync(elcmd_dvbtuners, NULL, &type, &result ) != 0 || type != elcdRpcResult || result == NULL )
	{
		cJSON_Delete(result);
		return st_tuner;
	}

	if (result->type == cJSON_Array)
	{
		cJSON *t;
		int i;
		for (i = 0; i<TUNER_MAX_NUMBER; i++)
		{
			st_tuners[i] = tunerNotFound;
			t = cJSON_GetArrayItem( result, i );
			if (!t)
				break;
			if (t->type == cJSON_String)
			{
				if (strcasecmp( t->valuestring, "DVB-T" ) == 0)
					st_tuners[i] = tunerTypeDVBT;
				else
				if (strcasecmp( t->valuestring, "DVB-S" ) == 0)
					st_tuners[i] = tunerTypeDVBS;
				else
				if (strcasecmp( t->valuestring, "DVB-C" ) == 0)
					st_tuners[i] = tunerTypeDVBC;

				if (st_tuner == tunerNotFound && st_tuners[i] != tunerNotFound)
					st_tuner = i;
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
#ifdef RPC_TRACE
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
#ifdef RPC_TRACE
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

static int st_syncCancel(void *pArg)
{
	rpcSync_t *s = pArg;
	sem_post(&s->lock);
	return 0;
}

void st_syncCallback(elcdRpcType_t type, cJSON *result, void* pArg)
{
	rpcSync_t *s = pArg;
	interface_removeEvent(st_syncCancel, pArg);
	s->res.type   = type;
	s->res.result = result;
	sem_post(&s->lock);
}

int st_rpcSync(elcdRpcCommand_t cmd, cJSON* params, elcdRpcType_t *type, cJSON **result)
{
	return st_rpcSyncTimeout(cmd, params, RPC_TIMEOUT, type, result);
}

int st_rpcSyncTimeout(elcdRpcCommand_t cmd, cJSON* params, int timeout , elcdRpcType_t *type, cJSON **result)
{
	rpcSync_t s;

	s.res.type = elcdRpcInvalid;
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

	interface_addEvent( st_syncCancel, &s, timeout*1000, 1);
	sem_wait(&s.lock);

	sem_destroy(&s.lock);
	if( s.res.type == elcdRpcInvalid )
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
	if( type )
		*type   = s.res.type;
	if( result )
		*result = s.res.result;
	else
	{
		cJSON_Delete(s.res.result);
	}
	return 0;
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
int st_getDvbTuner(void)
{
	return st_tuner;
}

fe_type_t st_getDvbTunerType(int tuner)
{
	if (st_tuner < 0)
		return 0;

	if (tuner < 0)
		tuner = st_tuner;

	switch (st_tuners[tuner])
	{
		case tunerNotFound: break; 
		case tunerTypeDVBT: return FE_OFDM;
		case tunerTypeDVBC: return FE_QAM;
		case tunerTypeDVBS: return FE_QPSK;
	}
	return 0;
}
#endif // ENABLE_DVB

#endif // STSDK
