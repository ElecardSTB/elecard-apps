
/*
 sem.c

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

#include "sem.h"
#include "debug.h"

#include <errno.h>

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

int mysem_get(pmysem_t semaphore)
{
	if(semaphore == NULL)
	{
		eprintf("Error: while trying get semaphore\n");
		return -1;
	}
	int rc;
	if ( (rc = pthread_mutex_lock(&(semaphore->mutex)))!=0 )
	{
		return rc;
	}

	while ( semaphore->semCount <= 0 )
	{
		rc = pthread_cond_wait(&(semaphore->condition), &(semaphore->mutex));
		if ( (rc!=0) && (errno != EINTR) )
			break;
	}
	semaphore->semCount--;

	if ( (rc = pthread_mutex_unlock(&(semaphore->mutex)))!=0 )
	{
		return rc;
	}

	return 0;
}

/********************************************************************************/
int mysem_release(pmysem_t semaphore)
{
	int rc;
	if(semaphore == NULL)
	{
		eprintf("Error: while trying release semaphore\n");
		return -1;
	}
	if ( (rc = pthread_mutex_lock(&(semaphore->mutex)))!=0 )
	{
		return rc;
	}

	semaphore->semCount ++;

	if ( (rc = pthread_mutex_unlock(&(semaphore->mutex)))!=0 )
	{
		return rc;
	}

	if ( (rc = pthread_cond_signal(&(semaphore->condition)))!=0 )
	{
		return rc;
	}

	return 0;
}

/********************************************************************************/
int mysem_create(pmysem_t* semaphore)
{
	int rc;
	pmysem_t thisSemaphore;
	thisSemaphore = (pmysem_t) dmalloc(sizeof(mysem_t));

	if ( (rc = pthread_mutex_init(&(thisSemaphore->mutex), NULL))!=0 )
	{
		dfree(thisSemaphore);
		return rc;
	}

	if ( (rc = pthread_cond_init(&(thisSemaphore->condition), NULL))!=0 )
	{
		pthread_mutex_destroy( &(thisSemaphore->mutex) );
		dfree(thisSemaphore);
		return rc;
	}

	thisSemaphore->semCount = 1;
	*semaphore = thisSemaphore;
	return 0;
}

/********************************************************************************/
int mysem_destroy(pmysem_t semaphore)
{
	if(semaphore == NULL)
	{
		eprintf("Error: while trying release semaphore\n");
		return -1;
	}
	pthread_mutex_destroy(&(semaphore->mutex));
	pthread_cond_destroy(&(semaphore->condition));
	dfree(semaphore);
	return 0;
}

#ifdef STB225
/********************************************************************************/
int event_send(pmysem_t semaphore)
{
    int rc;

    rc = pthread_mutex_lock(&(semaphore->mutex));
    if (rc != 0)
    {
        //DBG_PRINT2(MY_DBG_UNIT, DBG_WARNING, "%s ERROR: pthread_mutex_lock => %d!\n", __FUNCTION__, rc);
        return rc;
    }

    semaphore->semCount = 1;
    if ((rc = pthread_cond_signal(&(semaphore->condition)))!=0)
    {
        //DBG_PRINT2(MY_DBG_UNIT, DBG_WARNING, "%s ERROR: pthread_cond_signal => %d!\n", __FUNCTION__, rc);
        pthread_mutex_unlock(&(semaphore->mutex));
        return rc;
    }

    rc = pthread_mutex_unlock(&(semaphore->mutex));
    if (rc != 0)
    {
        //DBG_PRINT2(MY_DBG_UNIT, DBG_WARNING, "%s ERROR: pthread_mutex_unlock => %d!\n", __FUNCTION__, rc);
        return rc;
    }

    return 0;
}

int event_wait(pmysem_t semaphore)
{
    int rc;
    int locked = 0;

    //DBG_PRINT1(MY_DBG_UNIT, DBG_LEVEL_1, "%s event wait\n", __FUNCTION__);
    if (pthread_mutex_trylock( &semaphore->mutex ) == 0) 
    {
        /* Check for a pending event */
        if (semaphore->semCount == 1) 
        {
            /* Clear the event */
            semaphore->semCount = 0;
            pthread_mutex_unlock ( &semaphore->mutex );
            return 0;
        }
        locked = 1;
    }

    if (!locked)
    {
        rc = pthread_mutex_lock(&(semaphore->mutex));
        if (rc != 0)
        {
            //DBG_PRINT2(MY_DBG_UNIT, DBG_WARNING, "%s ERROR: pthread_mutex_lock => %d!\n", __FUNCTION__, rc);
            return rc;
        }
    }

    if (semaphore->semCount == 0)
    {
        //DBG_PRINT1(MY_DBG_UNIT, DBG_LEVEL_1, "%s semCount==0, so waiting!\n", __FUNCTION__);

        /* Wait for the event to occur */
        rc = pthread_cond_wait( &semaphore->condition,
                                &semaphore->mutex);

        //DBG_PRINT1(MY_DBG_UNIT, DBG_LEVEL_1, "%s semCount==0, Got event\n", __FUNCTION__);
    }

    /* Clear the event */
    semaphore->semCount = 0;

    rc = pthread_mutex_unlock(&(semaphore->mutex));
    if (rc != 0)
    {
        //DBG_PRINT2(MY_DBG_UNIT, DBG_WARNING, "%s ERROR: pthread_mutex_unlock => %d!\n", __FUNCTION__, rc);
        return rc;
    }

    return 0;
}

/********************************************************************************/
int event_create(pmysem_t* semaphore)
{
    int retValue;

    retValue = mysem_create(semaphore);

    if (retValue == 0) 
    {
        (*semaphore)->semCount = 0;
    }

    return retValue;
}

/********************************************************************************/
int event_destroy(pmysem_t semaphore)
{
    return mysem_destroy(semaphore);
}

#endif
