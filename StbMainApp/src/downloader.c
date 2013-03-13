
/*
 downloader.c

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

#include "downloader.h"
#include "defines.h"
#include "debug.h"
#include "output.h"
#include "sem.h"
#include <platform.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define CHUNK_SIZE           (10*1024)
#define DOWNLOAD_POOL_SIZE   (60)
//#define DOWNLOAD_POOL_SIZE   (10)	// orig
#define DNLD_PATH            "/tmp/XXXXXXXX/"
#define DNLD_PATH_LENGTH     (1+3+1+8+1)
#define DNLD_CONNECT_TIMEOUT (5)
#define DNLD_TIMEOUT         (20)

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct
{
	const char *url;
	int    timeout;
	char  *filename;
	size_t filename_size;       // capacity of filename buffer
	size_t quota;               // limit of downloaded data
	downloadCallback pCallback; // called after succesfull download
	void  *pArg;

	FILE  *out_file;
	size_t write_size;
	int    index;               // Index of download in pool
	//threadHandle_t thread;
	pthread_t thread;
	int  stop_thread;
} curlDownloadInfo_t;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static curlDownloadInfo_t downloader_pool[DOWNLOAD_POOL_SIZE];
static int gstop_downloads = 0;
static pmysem_t  downloader_semaphore;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static size_t downloader_headerCallback(char* ptr, size_t size, size_t nmemb, void* userp);
static size_t downloader_writeCallback(char *buffer, size_t size, size_t nmemb, void *userp);
static void downloader_acquireFileName(char *filename, curlDownloadInfo_t *info);
static DECLARE_THREAD_FUNC(downloader_thread_function);
static int downloader_exec(curlDownloadInfo_t* info);

static void downloader_free( int index );

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

void downloader_init()
{
	mysem_create(&downloader_semaphore);
	system("rm -rf /tmp/XX*"); // clean up previous downloads in case of crash
}

static void downloader_acquireFileName(char *filename, curlDownloadInfo_t *info)
{
	char *name_end;
	size_t filename_length;
	/*int offset, i;
	char *dot = NULL;*/

	//dprintf("%s: '%s'\n", __FUNCTION__, filename);
	
	if( *filename == '"' )
		filename++;
	name_end = index(filename, '"');
	if( !name_end )
		name_end = index( filename, ';' );
	filename_length = name_end ? (size_t)(name_end - filename) : strlen(filename);
	if(filename_length+DNLD_PATH_LENGTH >= info->filename_size )
	{
		filename_length = info->filename_size - DNLD_PATH_LENGTH - 1;
		/*dot = rindex(filename,'.'); // moving file extension
		if( dot )
		{
			offset = name_end-dot;
			if( filename_length < offset )
			{
				filename_length = dot-filename;
			}
		}*/
	}
	/*if( dot )
	{
		strncpy( info->filename, filename, filename_length - offset );
		strncpy( &info->filename[filename_length - offset], dot, offset);
	} else */
	{
		strncpy( &info->filename[DNLD_PATH_LENGTH], filename, filename_length );
		dprintf("%s: Acquired '%s'\n", __FUNCTION__, info->filename);
	}
	info->filename[DNLD_PATH_LENGTH+filename_length] = 0;
}

static size_t downloader_headerCallback(char* buffer, size_t size, size_t nmemb, void* userp)
{
	curlDownloadInfo_t *info = (curlDownloadInfo_t *)userp;
	char *filename;

	//dprintf("%s %s\n", __FUNCTION__, buffer);
	if( info )
	{
		if( strncasecmp(buffer, "Content-Type: ", sizeof("Content-Type: ")-1) )
		{
			filename = strstr(buffer, "name=");
			if( filename )
			{
				filename += 5;
				downloader_acquireFileName(filename, info);
			}
		} else if( strncasecmp(buffer, "Content-Disposition: ", sizeof("Content-Disposition: ")-1) )
		{
			filename = strstr(buffer, "filename=");
			if( filename )
			{
				filename += 9;
				downloader_acquireFileName(filename, info);
			}
		}
	}
	return size*nmemb;
}

static size_t downloader_writeCallback(char *buffer, size_t size, size_t nmemb, void *userp)
{
	curlDownloadInfo_t *info = (curlDownloadInfo_t*)userp;
	size_t read_size = 0;
	size_t write_size;
	size_t offset;
	size_t chunk_size,chunk_offset;

	if( info != NULL && (info->quota == 0 || info->write_size < info->quota) )
	{
		read_size = size*nmemb;
		if( read_size + info->write_size > info->quota )
		{
			eprintf("%s[!]: %s is greater than %u bytes, download truncated\n", __FUNCTION__, info->url, info->quota);
			read_size = info->quota-info->write_size;
		}
		offset = 0;
		chunk_size	=	CHUNK_SIZE;
		while( offset < read_size )
		{
			chunk_offset	=	0;
			if(chunk_size>(read_size-offset))
				chunk_size	=	(read_size-offset);
			while(chunk_offset<chunk_size)
			{
				usleep(1000);//1ms
				write_size = fwrite(&buffer[offset], 1, chunk_size-chunk_offset, info->out_file);
				if(write_size>0)
				{
					offset				+=	write_size;
					chunk_offset		+=	write_size;
					info->write_size	+=	write_size;
				}
				if(gstop_downloads || info->stop_thread)
					break;
			}
			if(gstop_downloads || info->stop_thread)
				break;
		}
	}
	return read_size;
}

static int downlader_guessName(const char* content_type, char *filename, size_t filename_size)
{
	if( strcmp(content_type, "image/jpeg") == 0 && filename_size > 5 )
	{
		strcpy(filename, "1.jpg");
		return 0;
	} else if( strcmp(content_type, "image/png") == 0 && filename_size > 5 )
	{
		strcpy(filename, "1.png");
		return 0;
	} else
		return -1;
}

static int downloader_exec(curlDownloadInfo_t* info)
{
	CURL 			*curl;
	CURLcode 		res;
	char errbuff[CURL_ERROR_SIZE];
	int noproxy = 0;
	
	curl = curl_easy_init();
	if(!curl)
		return  -1;

	if (info->filename[0] == 0)
	{
		strncpy( info->filename, DNLD_PATH, DNLD_PATH_LENGTH-1 );
		info->filename[DNLD_PATH_LENGTH-1] = 0;
		info->filename[DNLD_PATH_LENGTH]   = 0;
		if( mkdtemp( info->filename ) == NULL )
		{
			eprintf("downloader: Failed to create temp dir '%s': %s\n", info->filename, strerror(errno));
			goto failure;
		}
		info->filename[DNLD_PATH_LENGTH-1] = '/';
		//dprintf("%s: Downloading to temp dir: '%s'\n", __FUNCTION__, info->filename);
		
		curl_easy_setopt(curl, CURLOPT_URL, info->url);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuff);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, info->timeout > 0 ? info->timeout : DNLD_CONNECT_TIMEOUT );
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, info->timeout > 0 ? info->timeout : DNLD_TIMEOUT );
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
		curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, info);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, downloader_headerCallback);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);	// nessesary, we are in m/t environment
		appInfo_setCurlProxy(curl);
		res = curl_easy_perform(curl);
		
		if(res != CURLE_OK && res != CURLE_WRITE_ERROR && appControlInfo.networkInfo.proxy[0] != 0 )
		{
			noproxy = 1;
			dprintf("downloader: Retrying download without proxy (res=%d)\n", res);
			curl_easy_setopt(curl, CURLOPT_URL, info->url);
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuff);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, info->timeout > 0 ? info->timeout : DNLD_CONNECT_TIMEOUT );
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, info->timeout > 0 ? info->timeout : DNLD_TIMEOUT );
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
			curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
			curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, info);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, downloader_headerCallback);
			curl_easy_setopt(curl, CURLOPT_PROXY, "");
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);	// nessesary, we are in m/t environment
			res = curl_easy_perform(curl);
		}
		if(res != CURLE_OK && res != CURLE_WRITE_ERROR)
		{
			eprintf("downloader: Failed to get header from '%s': %s\n", info->url, errbuff);
			goto failure;
		}
		if( info->filename[DNLD_PATH_LENGTH] == 0 )
		{
			char *name_ptr = rindex( info->url, '/')+1;
			if( *name_ptr != 0 )
			{
				size_t name_length = strlen( info->url ) - (name_ptr - info->url);
				char *ptr = index( name_ptr, '?' );
				if( ptr ) name_length = (ptr - name_ptr);
				if( (ptr = index(name_ptr, '.')) == NULL || ptr >= name_ptr+name_length )
				{
					char *content_type;
					if( CURLE_OK != curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) ||
					    downlader_guessName(content_type,
					                        &info->filename[DNLD_PATH_LENGTH],
					                         info->filename_size - DNLD_PATH_LENGTH) != 0 )
					{
						info->filename[DNLD_PATH_LENGTH] = '0';
						info->filename[DNLD_PATH_LENGTH+1] = 0;
					}
				} else
				{
					if( DNLD_PATH_LENGTH + name_length >= info->filename_size )
					{
						int offset   = DNLD_PATH_LENGTH + name_length - info->filename_size + 1;
						name_ptr   += offset;
						name_length -= offset;
					}
					strncpy(&info->filename[DNLD_PATH_LENGTH], name_ptr, name_length);
					info->filename[DNLD_PATH_LENGTH+name_length] = 0;
				}
			} else
			{
				char *content_type;
				if( CURLE_OK != curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) ||
				    downlader_guessName(content_type,
					                    &info->filename[DNLD_PATH_LENGTH],
					                     info->filename_size - DNLD_PATH_LENGTH) != 0 )
				{
					info->filename[DNLD_PATH_LENGTH] = '0';
					info->filename[DNLD_PATH_LENGTH+1] = 0;
				}
			}
		}
	}

	dprintf("downloader: Performing download to '%s'\n", info->filename);	
	if( !(info->out_file = fopen( info->filename, "w" )) )
	{
		eprintf("downloader: Failed to open temp file '%s' for writing!\n", info->filename);
		goto failure;
	}
	info->write_size = 0;

	curl_easy_setopt(curl, CURLOPT_URL, info->url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuff);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, info->timeout > 0 ? info->timeout : DNLD_CONNECT_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, info->timeout > 0 ? info->timeout : DNLD_TIMEOUT);	
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, info);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, downloader_writeCallback);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);	// nessesary, we are in m/t environment
	if( noproxy )
		curl_easy_setopt(curl, CURLOPT_PROXY, "");
	else
		appInfo_setCurlProxy(curl);

	res = curl_easy_perform(curl);
	if(res != CURLE_OK && res != CURLE_WRITE_ERROR)
	{
		eprintf("downloader: Failed to download '%s': %s\n", info->url, errbuff);
		goto failure;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res);
	if ( res != 200 )
	{
		eprintf("downloader: Failed to download '%s': wrong response code %d\n", info->url, res);
		goto failure;
	}
	//double 			contentLength = 0;
	//res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength);
	//if(res)
	//{
	//	eprintf("downloader: Failed to download '%s': can't get content length (error %d)\n", info->url, res);
	//	goto failure;
	//}

	curl_easy_cleanup(curl);
	fclose(info->out_file);
	info->out_file = NULL;

	return 0;

failure:
	curl_easy_cleanup(curl);
	if( info->out_file )
	{
		fclose(info->out_file);
		info->out_file = NULL;
	}
	unlink( info->filename );

	return -1;
}

int downloader_get(const char* url, int timeout, char *filename, size_t fn_size, size_t quota)
{
	curlDownloadInfo_t info;

	if( !url || !filename || fn_size <= DNLD_PATH_LENGTH )
		return -2;

	info.url = url;
	info.timeout = timeout;
	info.filename = filename;
	info.filename_size = fn_size;
	info.quota = quota;
	info.pCallback = NULL;
	info.stop_thread = 0;
	info.index = -1;
	info.out_file = 0;

	return downloader_exec(&info);
}

#if 0
int downloader_start(char* url, size_t quota, char *filename, size_t fn_size, void *pArg, downloadCallback pCallback)
{
	curlDownloadInfo_t info;

	if( !url || !filename || fn_size <= DNLD_PATH_LENGTH)
		return -2;

	info.url = url;
	info.timeout = 0;
	info.filename = filename;
	info.filename_size = fn_size;
	info.quota = quota;
	info.pArg = pArg;
	info.pCallback = pCallback;
	info.stop_thread = 0;
	info.index = -1;

	CREATE_THREAD(info.thread, downloader_thread_function, (void*)&info);

	return info.thread != NULL ? 0 : -1;
}
#endif

void downloader_cleanupTempFile(char *file)
{
	unlink( file );
	file[DNLD_PATH_LENGTH-1] = 0;
	rmdir( file );
}

void downloader_cleanup()
{
	int i, bRunning;
	gstop_downloads = 1;
	do
	{
		usleep(10000);
		bRunning = 0;
		for( i = 0; i < DOWNLOAD_POOL_SIZE; i++ )
		{
			if( downloader_pool[i].thread != 0 )
			{
				bRunning = 1;
				break;
			}
		}
	} while(bRunning);
	mysem_destroy(downloader_semaphore);
}

static DECLARE_THREAD_FUNC(downloader_thread_function)
{
	curlDownloadInfo_t *info = (curlDownloadInfo_t *)pArg;
	if( !info )
		return NULL;
	
	downloader_exec(info);

	if( info->pCallback )
	{			
		info->pCallback( info->index, info->pArg );
		// info->filename and info->url shouldn't be used after this
	}

	downloader_free( info->index );

	//DESTROY_THREAD(info->thread);
	info->thread = 0;
	return NULL;
}

/**
 * @brief Free download info from pool (stops download if it is still active).
 *
 * @param  index	I	Index of download in pool
 */
static void downloader_free( int index )
{
	mysem_get(downloader_semaphore);
	downloader_pool[index].url = NULL;
	downloader_pool[index].stop_thread = 1;
	mysem_release(downloader_semaphore);
}

int downloader_find(const char *url)
{
	int i;
	mysem_get(downloader_semaphore);
	for( i = 0; i < DOWNLOAD_POOL_SIZE; i++ )
	{
		if( downloader_pool[i].url != NULL && strcasecmp( url, downloader_pool[i].url ) == 0 )
		{
			mysem_release(downloader_semaphore);
			return i;
		}
	}
	mysem_release(downloader_semaphore);
	return -1;
}

int  downloader_push(const char *url, char *filename,  size_t fn_size, size_t quota, downloadCallback pCallback, void *pArg )
{
	int index;

	if( !url || !filename || fn_size <= DNLD_PATH_LENGTH)
		return -2;

	mysem_get(downloader_semaphore);
	for( index = 0; index < DOWNLOAD_POOL_SIZE; index++ )
	{
		if( downloader_pool[index].url == NULL )
		{
			//eprintf("downloader: pushing %d, '%s'\n", index, url);
		
			downloader_pool[index].url = url;
			downloader_pool[index].timeout = 0;
			downloader_pool[index].filename = filename;
			downloader_pool[index].filename_size = fn_size;
			downloader_pool[index].quota = quota;
			downloader_pool[index].pArg = pArg;
			downloader_pool[index].pCallback = pCallback;
			downloader_pool[index].index = index;
			downloader_pool[index].stop_thread = 0;
			//CREATE_THREAD( downloader_pool[index].thread, downloader_thread_function, (void*)&downloader_pool[index] );
			
			downloader_pool[index].thread = 0;
			if (pthread_create(&downloader_pool[index].thread, NULL, downloader_thread_function, (void*)&downloader_pool[index]) == 0){
				pthread_detach(downloader_pool[index].thread);
			}
			
			if( downloader_pool[index].thread == 0 )
			{
				downloader_pool[index].url = NULL;
				mysem_release(downloader_semaphore);
				return -1;
			}
			mysem_release(downloader_semaphore);
			return index;
		}
	}
	mysem_release(downloader_semaphore);
	eprintf("downloader: Can't start download of '%s': pool is full\n", url);
	return -1;
}

int  downloader_getInfo( int index, char **url, char **filename, size_t *fn_size, size_t *quota)
{
	if (index < 0 || index >= DOWNLOAD_POOL_SIZE || downloader_pool[index].url == NULL)
		return -1;

	mysem_get(downloader_semaphore);
	if (url)
		*url = (char*)downloader_pool[index].url;
	if (filename)
		*filename = downloader_pool[index].filename;
	if (fn_size)
		*fn_size = downloader_pool[index].filename_size;
	if (quota)
		*quota = downloader_pool[index].quota;
	mysem_release(downloader_semaphore);

	return 0;
}
