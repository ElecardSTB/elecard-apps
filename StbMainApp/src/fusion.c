//
// Created:  2013/07/30
// File name:  fusion.c
//
//////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2013 Elecard Devices
// All rights are reserved.  Reproduction in whole or in part is 
//  prohibited without the written consent of the copyright owner.
//
// Elecard Devices reserves the right to make changes without
// notice at any time. Elecard Ltd. makes no warranty, expressed,
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
// Authors: Victoria Peshkova <Victoria.Peshkova@elecard.ru>
// 
// Purpose: Fusion project file.
//
//////////////////////////////////////////////////////////////////////////

/***********************************************
* INCLUDE FILES                                *
************************************************/
#ifdef ENABLE_FUSION

#include "fusion.h"

/***********************************************
* LOCAL MACROS                                 *
************************************************/


/******************************************************************
* FUNCTION PROTOTYPES                  <Module>_<Word>+           *
*******************************************************************/
fusion_object_t FusionObject;

void fusion_init();
void fusion_cleanup();
void fusion_configure();
int fusion_checkResponse (char * curlStream, cJSON ** ppRoot);
int fusion_sendRequest(char * url, char * curlStream, cJSON ** ppRoot);
void fusion_getPlaylistFull ();
void * fusion_threadManagePlayback (void * pArg);
void * fusion_threadUpdatePlaylist (void * pArg);

char fusion_errorBuffer[CURL_ERROR_SIZE];
int fusion_curStreamPos = 0; // in bytes
int exitStatus = 0;

extern void media_pauseOrStop(int stop);
/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

void fusion_init()
{
	memset (&FusionObject, 0, sizeof(fusion_object_t));
	
	pthread_mutex_init(&FusionObject.mutexUpdatePlaylist, NULL);
	
	fusion_configure();
	
	FUSION_START_THREAD(FusionObject.threadUpdatePlaylist, fusion_threadUpdatePlaylist, NULL);

	return;
}

void fusion_cleanup()
{
	FusionObject.stopRequested = 1;
	FUSION_WAIT_THREAD(FusionObject.threadUpdatePlaylist);
	FUSION_WAIT_THREAD(FusionObject.threadManagePlayback);
	
	pthread_mutex_destroy(&FusionObject.mutexUpdatePlaylist);
	if (FusionObject.response.playlist.fileCount > 0){
		SAFE_DELETE(FusionObject.response.playlist.files);
		FusionObject.response.playlist.fileCount = 0;
	}
	return;
}

void fusion_configure()
{
	char * ptr;
	char line [FUSION_SERVER_LEN + 7]; // strlen ("SERVER ") == 7
	FILE * f = fopen(FUSION_HWCONFIG, "rt");
	if (!f)
	{
		eprintf ("%s: ERROR! opening %s file.  Create default one.\n", __FUNCTION__, FUSION_HWCONFIG);
		f = fopen(FUSION_HWCONFIG, "wt");
		if (f){
			fprintf(f, "SERVER %s\n", FUSION_DEFAULT_SERVER_PATH);
			fprintf(f, "USERKEY %s\n", FUSION_TEST_USERKEY);
			fclose (f);
		} 
	}
	f = fopen(FUSION_HWCONFIG, "rt");
	if (f){
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
				eprintf ("%s: server = %s\n", __FUNCTION__, FusionObject.server);
			} else if ((ptr = strcasestr((const char*)line, (const char*)"USERKEY ")) != NULL){
				ptr += 8;
				snprintf (FusionObject.userKey, FUSION_KEY_LEN, "%s", ptr);
				eprintf ("%s: userKey = %s\n", __FUNCTION__, FusionObject.userKey);
			}
		}
		fclose (f);
	}
	else {
		eprintf ("%s: ERROR! opening %s file. \n", __FUNCTION__, FUSION_HWCONFIG);
	}
	return;
}
//-----------------------------------------------------------------------------------------------
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

double fusion_difftime(struct timespec * t1, struct timespec * t0)
{
	double diff = 0;
	
	if (t1->tv_sec > t0->tv_sec){		
		diff = (t1->tv_sec - t0->tv_sec);
	}
	if (t1->tv_nsec >= 0 && t0->tv_nsec >= 0){
		diff = diff + (t1->tv_nsec - t0->tv_nsec) / 1000000000.0;
	}
	return diff;// in sec
}

static void fusion_wait (unsigned int timeout_ms)
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
//-----------------------------------------------------------------------------------------------

int fusion_curlWriter(char * data, size_t size, size_t nmemb, void * stream)
{
	int writtenBytes = size * nmemb;
	
	if (fusion_curStreamPos + writtenBytes >= FUSION_STREAM_SIZE) {
		eprintf ("%s: WARNING! truncating received buffer from %d to %d bytes.\n", 
			__FUNCTION__, writtenBytes, FUSION_STREAM_SIZE - fusion_curStreamPos - 1);
		writtenBytes = FUSION_STREAM_SIZE - fusion_curStreamPos - 1;
	}
	memcpy ((char*)stream + fusion_curStreamPos, data, writtenBytes);
	fusion_curStreamPos += writtenBytes;
	return writtenBytes;
}

//-----------------------------------------------------------------------------------------
int fusion_sendRequest(char * url, char * curlStream, cJSON ** ppRoot)
{
	CURLcode retCode;
	CURL * curl = NULL;
	
	if (!url || !strlen(url)) return -1;
	
	curl = curl_easy_init();
	if (!curl) return -1;
	
	fusion_curStreamPos = 0;
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, fusion_errorBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curlStream);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fusion_curlWriter);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);	// nessesary, we are in m/t environment

	eprintf ("rq: %s\n", url);

	retCode = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

#ifdef FUSION_TEST
	eprintf ("ans: %s\n", curlStream);
#endif
	
	if(retCode != CURLE_OK && retCode != CURLE_WRITE_ERROR)
	{
		eprintf ("%s: ERROR! %s\n", __FUNCTION__, fusion_errorBuffer);
		return -1;
	}
	
	if (!curlStream || strlen(curlStream) == 0) {
		eprintf ("%s: ERROR! empty response.\n", __FUNCTION__);
		return -1;
	}
	return fusion_checkResponse(curlStream, ppRoot);
}

int fusion_checkResponse (char * curlStream, cJSON ** ppRoot)
{
	*ppRoot = cJSON_Parse(curlStream);
	if (!(*ppRoot)) {
		eprintf ("%s: ERROR! Not a JSON response. %s\n", __FUNCTION__, curlStream);
		return -1;
	}

	if (cJSON_GetObjectItem(*ppRoot, "success")){
		if (strcmp("false", cJSON_GetObjectItem(*ppRoot, "success")->valuestring) == 0){
			char * errorDesc;
			if (cJSON_GetObjectItem(*ppRoot, "error")){
				errorDesc = cJSON_GetObjectItem(*ppRoot, "error")->valuestring;
				eprintf ("%s: ERROR! %s\n", __FUNCTION__, errorDesc);
				cJSON_Delete(*ppRoot);
				return -1;
			}
			cJSON_Delete(*ppRoot);
			return -1;
		}
		return 0;
	}
	eprintf ("%s: ERROR! No \"success\" tag in answer. %s\n", __FUNCTION__, curlStream);
	cJSON_Delete(*ppRoot);
	return -1;
}
//-----------------------------------------------------------------------------------------

unsigned int fusion_getTimeToWait()
{
	time_t now;
	struct tm timeToUpdate;
	double timeToWaitInMs;

	time(&now);
	timeToUpdate = *gmtime (&now);

	if (timeToUpdate.tm_hour > FUSION_UPDATE_HOUR){
		timeToUpdate.tm_mday += 1;
	}
	timeToUpdate.tm_hour = FUSION_UPDATE_HOUR;
	timeToUpdate.tm_min = FUSION_UPDATE_MIN;
	timeToUpdate.tm_sec = FUSION_UPDATE_SEC;
	
#ifdef FUSION_TEST
	eprintf ("%s(%d): timeToUpdate = %d-%02d-%02d %02d-%02d-%02d, time = %d, now = %d\n", __FUNCTION__, __LINE__,
		timeToUpdate.tm_year + 1900, timeToUpdate.tm_mon + 1, timeToUpdate.tm_mday,
		timeToUpdate.tm_hour, timeToUpdate.tm_min, timeToUpdate.tm_sec, mktime(&timeToUpdate), now);
#endif
	
	timeToWaitInMs = difftime(mktime(&timeToUpdate), now) * 1000;
#ifdef FUSION_TEST
	eprintf ("%s: %s(%d): timeToWaitInMs = %f\n", __FILE__, __FUNCTION__, __LINE__, timeToWaitInMs);
#endif
	return (unsigned int)timeToWaitInMs;
}

void * fusion_threadUpdatePlaylist (void * pArg)
{
	while (!FusionObject.stopRequested)
	{
		pthread_mutex_lock(&FusionObject.mutexUpdatePlaylist);
		fusion_getPlaylistFull();
		pthread_mutex_unlock(&FusionObject.mutexUpdatePlaylist);
		
		if (FusionObject.response.playlist.fileCount == 0){
			fusion_wait (FUSION_FILECHECK_TIMEOUT_MS);
			continue;
		}
		else {
			if (FusionObject.threadManagePlayback == 0){
				FUSION_START_THREAD(FusionObject.threadManagePlayback, fusion_threadManagePlayback, NULL);
			}
			fusion_wait (fusion_getTimeToWait());
		}
	}
#ifdef FUSION_TEST
	eprintf ("%s(%d): exit thread.\n", __FUNCTION__, __LINE__);
#endif
	return NULL;
}

int fusion_selectStreamToPlayNow()
{
	time_t now;
	unsigned int i;
	double diff0, diff1;
	
	time(&now);
	diff0 = difftime(now, mktime(&FusionObject.response.playlist.files[0].timestamp));
	if (diff0 < 0){
		return FUSION_ERR_EARLY;
	}
	
	for (i=0; i<FusionObject.response.playlist.fileCount-1; i++)
	{
		diff0 = difftime(now, mktime(&FusionObject.response.playlist.files[i].timestamp));
		diff1 = difftime(now, mktime(&FusionObject.response.playlist.files[i+1].timestamp));

#ifdef FUSION_TEST
		eprintf ("%s(%d): diff0 = %f, diff1 = %f\n", __FUNCTION__, __LINE__, diff0, diff1);
#endif

		if (diff0 > 0 && diff1 < 0){ 
			return i;  //if we are inside this file
		}
	}
	return FUSION_ERR_LATE;
}

void * fusion_threadManagePlayback (void * pArg)
{
	int currentIndex;
	char playingUrl[FUSION_MAX_URL_LEN];
	
#ifdef FUSION_TEST
	eprintf ("%s(%d): Start managing playback.\n", __FUNCTION__, __LINE__);
#endif

	memset(playingUrl, 0, FUSION_MAX_URL_LEN);

	while (!FusionObject.stopRequested)
	{
		if (FusionObject.response.playlist.fileCount > 0)
		{
			pthread_mutex_lock(&FusionObject.mutexUpdatePlaylist);
			currentIndex = fusion_selectStreamToPlayNow();
			pthread_mutex_unlock(&FusionObject.mutexUpdatePlaylist);

			if (currentIndex == FUSION_ERR_EARLY){
#ifdef FUSION_TEST
				eprintf ("%s(%d): WARNING! Too early to play any files\n", __FUNCTION__, __LINE__);
#endif
			}
			else if (currentIndex == FUSION_ERR_LATE){
#ifdef FUSION_TEST
				eprintf ("%s(%d): WARNING! Too late to play any files. Exit thread.\n", __FUNCTION__, __LINE__);
#endif
				FusionObject.threadManagePlayback = 0;
				pthread_exit((void*)&exitStatus);
				return NULL;
			}
			else {
				pthread_mutex_lock(&FusionObject.mutexUpdatePlaylist);
#ifdef FUSION_TEST
				eprintf ("%s(%d): Play file %d, %s\n", __FUNCTION__, __LINE__, currentIndex, 
					FusionObject.response.playlist.files[currentIndex].url);
#endif
				if (strcmp(playingUrl, FusionObject.response.playlist.files[currentIndex].url) != 0){
					snprintf (playingUrl, FUSION_MAX_URL_LEN, FusionObject.response.playlist.files[currentIndex].url);
					
#ifdef FUSION_TEST
					eprintf ("%s(%d): Try to stop any playing..\n", __FUNCTION__, __LINE__);
#endif
					media_pauseOrStop(GFX_STOP);	// test
					
					int result = media_playURL(0, playingUrl, NULL, NULL);
					if (result == 0){
#ifdef FUSION_TEST
						eprintf ("%s(%d): Started playing %s\n", __FUNCTION__, __LINE__, playingUrl);
#endif
					}
				}
				pthread_mutex_unlock(&FusionObject.mutexUpdatePlaylist);
			}
		}
		fusion_wait (FUSION_FILECHECK_TIMEOUT_MS);
	}
#ifdef FUSION_TEST
	eprintf ("%s(%d): exit thread.\n", __FUNCTION__, __LINE__);
#endif
	FusionObject.threadManagePlayback = 0;
	pthread_exit((void*)&exitStatus);
	return NULL;
}

void fusion_getPlaylistFull ()
{
	cJSON * root;
	char request[FUSION_URL_LEN];
	char stream[FUSION_STREAM_SIZE];

	sprintf (request, "%s/?s=%s&c=playlist_full", FusionObject.server, FusionObject.userKey);

	if (fusion_sendRequest(request, stream, &root) != 0) {
		eprintf ("%s: fusion_sendRequest ERROR.\n", __FUNCTION__);
		return;
	}
	
	/* "version": "0.1",
    "date": "2013-07-31 09:55:37",
    "success": "true",
    "playlist": {
        "date": "2013-07-31 00-00-00",
        "files": {
            "00:00:00": "http://public.tv/data/videos/0000/0001/sq.mp4",
            "00:00:53": "http://public.tv/data/videos/0000/0002/sq.mp4",
            "00:03:30": "http://public.tv/data/videos/0000/0003/sq.mp4",
            "00:05:53": "http://public.tv/data/videos/0000/0006/sq.mp4"
        }
    }
	*/
	
	cJSON * jsonPlaylist = cJSON_GetObjectItem(root, "playlist");
	if (jsonPlaylist){
		cJSON * jsonDate = cJSON_GetObjectItem(jsonPlaylist, "date");
		cJSON * jsonFiles = cJSON_GetObjectItem(jsonPlaylist, "files");
		if (jsonDate && jsonDate->valuestring && jsonFiles)
		{
			struct tm datestart;
			sscanf(jsonDate->valuestring,"%04d-%02d-%02d %02d-%02d-%02d", &datestart.tm_year, &datestart.tm_mon, &datestart.tm_mday,
				&datestart.tm_hour, &datestart.tm_min, &datestart.tm_sec);
			datestart.tm_year -= 1900;
			datestart.tm_mon -= 1;
			
			FusionObject.response.playlist.fileCount = cJSON_GetArraySize(jsonFiles);
			FusionObject.response.playlist.files = malloc (sizeof(fusion_file_t) * FusionObject.response.playlist.fileCount);
			for (unsigned int i=0; i<FusionObject.response.playlist.fileCount; i++){
				cJSON * jsonItem = cJSON_GetArrayItem(jsonFiles, i);
				if (jsonItem){
					struct tm timestart;
					snprintf (FusionObject.response.playlist.files[i].url, FUSION_MAX_URL_LEN, jsonItem->valuestring);
					
					sscanf(jsonItem->string,"%02d:%02d:%02d", &timestart.tm_hour, &timestart.tm_min, &timestart.tm_sec);
					datestart.tm_hour = timestart.tm_hour;
					datestart.tm_min = timestart.tm_min;
					datestart.tm_sec = timestart.tm_sec;
					mktime(&datestart);

#ifdef FUSION_TEST
					eprintf ("%s: %s(%d): timestamp %04d-%02d-%02d %02d-%02d-%02d, file = %s\n", __FILE__, __FUNCTION__, __LINE__, 
						datestart.tm_year + 1900, datestart.tm_mon + 1, datestart.tm_mday, datestart.tm_hour, datestart.tm_min, datestart.tm_sec,
						jsonItem->valuestring);
#endif
					FusionObject.response.playlist.files[i].timestamp = datestart;
				}
			}
		}
	}
	
	cJSON_Delete(root);
	return;
}
#endif // ENABLE_FUSION


/*
 * time_t now;
	struct tm t;
	static struct tm saved_t = {
		.tm_mday = 0,
		.tm_mon = 0,
		.tm_year = 0
	};
	static char curStatisticsFilename[256];

	time(&now);
	now -= 2 * 3600;
	localtime_r(&now, &t);
	if((t.tm_mday != saved_t.tm_mday) || (t.tm_mon != saved_t.tm_mon) || (t.tm_year != saved_t.tm_year)) {
		saved_t = t;
		snprintf(curStatisticsFilename, sizeof(curStatisticsFilename), GARB_DIR "raw_%s_%s_%4d%02d%02d",
					garb_info.hh.number, garb_info.hh.device, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
	}

	sscanf(str,"%4d%2d%2d %2d%2d%2d",&tm1.tm_year,&tm1.tm_mon,&tm1.tm_mday,
			&tm1.tm_hour,&tm1.tm_min,&tm1.tm_sec);
				}
			}
		}
	}
 * */
