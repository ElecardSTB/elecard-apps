#ifdef ENABLE_FUSION
#include "fusion.h"
//#include "helper.h"

#define eprintf(x...) \
	do { \
		time_t __ts__ = time(NULL); \
		struct tm *__tsm__ = localtime(&__ts__); \
		printf("[%02d:%02d:%02d]: ", __tsm__->tm_hour, __tsm__->tm_min, __tsm__->tm_sec); \
		printf(x); \
	} while(0)

static char g_usbRoot[PATH_MAX] = {0};
static int gStatus = 0;

interfaceFusionObject_t FusionObject;

typedef size_t (*curlWriteCB)(void *buffer, size_t size, size_t nmemb, void *userp);

int32_t fusion_checkDirectory(const char *path);
int fusion_checkFileIsDownloaded(char * remotePath, char * localPath);
int fusion_checkLastModified (char * url);
int fusion_checkResponse (char * curlStream, cJSON ** ppRoot);
int fusion_checkSavedPlaylist();
int fusion_createMarksDirectory(char * folderPath);
size_t fusion_curlCallback(char * data, size_t size, size_t nmemb, void * stream);
size_t fusion_curlCallbackVideo(char * data, size_t size, size_t nmemb, void * stream);
int fusion_downloadMarkFile(int index, char * folderPath);
int fusion_downloadPlaylist(char * url, cJSON ** ppRoot);
int fusion_getCommandOutput (char * cmd, char * result);
int fusion_getCreepAndLogo ();
CURLcode fusion_getDataByCurl (char * url, char * curlStream, int * pStreamLen, curlWriteCB cb);
void fusion_getLocalFirmwareVer();
long fusion_getRemoteFileSize(char * url);
int fusion_getSecret ();
int fusion_getUsbRoot();
int fusion_getUtc (char * utcBuffer, int size);
CURLcode fusion_getVideoByCurl (char * url, void * fsink/*char * curlStream*/, int * pStreamLen, curlWriteCB cb);
void fusion_ms2timespec(int ms, struct timespec * ts);
int fusion_readConfig();
int fusion_refreshDtmfEvent(void *pArg);
int fusion_removeFirmwareFormFlash();
int fusion_removeOldMarkVideo(char * folderPath);
int fusion_setMoscowDateTime();
void * fusion_threadCheckReboot (void * param);
void * fusion_threadCreepline(void * param);
void * fusion_threadDownloadFirmware(void * param);
void * fusion_threadFlipCreep (void * param);
void fusion_wait (unsigned int timeout_ms);

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

void fusion_wait (unsigned int timeout_ms)
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

int fusion_getCommandOutput (char * cmd, char * result)
{
	FILE *fp;
	char line[PATH_MAX];
	char * movingPtr;

	if (cmd == NULL) return -1;
	fp = popen(cmd, "r");
	if (fp == NULL) {
		eprintf("%s: Failed to run command %s\n", __FUNCTION__, cmd);
		return -1;
	}

	movingPtr = result;
	while (fgets(line, sizeof(line)-1, fp) != NULL) {
		sprintf(movingPtr, "%s", line);
		movingPtr += strlen(line);
		//eprintf ("%s: result = %s\n", __FUNCTION__, result);
	}
	pclose(fp);
	return 0;
}

void * fusion_threadFlipCreep (void * param)
{
	while (1)
	{
		interface_updateFusionCreepSurface();
		fusion_wait(10);
	}
	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

void * fusion_threadDownloadFirmware(void * param)
{
	char filepath[PATH_MAX];

	char * url = (char*)param;
	if (!url || !strlen(url)){
		eprintf ("%s: ERROR! Invalid url.\n", __FUNCTION__);
		pthread_exit((void *)&gStatus);
		return (void*)NULL;
	}
	if (strlen(g_usbRoot) == 0){
		if (fusion_getUsbRoot() == 0){
			eprintf ("%s: ERROR! No flash found.\n", __FUNCTION__);
			pthread_exit((void *)&gStatus);
			return (void*)NULL;
		}
	}
	char cmd [PATH_MAX];
	char * ptrFilename = strstr(url, "STB8");
	if (!ptrFilename){
		eprintf ("%s: ERROR! Incorrect firmware url (%s).\n", __FUNCTION__, url);
		pthread_exit((void *)&gStatus);
		return (void*)NULL;
	}

	sprintf (filepath, "%s/%s", g_usbRoot, ptrFilename);

	// check if firmware already downloaded - not nessesary, 
	// because we check existance on FusionObject.firmware before thread starts
/*	FILE * f = fopen(filepath, "rb");
	if (f){
		long int localFileSize = 0;
		fseek(f, 0, SEEK_END);
		localFileSize = ftell(f);
		fclose(f);

		if (localFileSize){
			// check if filesize is eaual to remote filesize
			int remoteSize = fusion_getRemoteFileSize(url);
			if (localFileSize == remoteSize){
				eprintf ("%s: Firmware is already downloaded.\n", __FUNCTION__);
				pthread_exit((void *)&gStatus);
				return (void*)NULL;
			}
		}
	}
	*/
	eprintf ("%s: Save %s to %s ...\n", __FUNCTION__, url, filepath);
	sprintf (cmd, "wget -c -q \"%s\" -O %s ", url, filepath); // 2>/dev/null, quiet
	system(cmd);

	eprintf ("%s(%d): Exit.\n", __FUNCTION__, __LINE__);

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}


void * fusion_threadCheckReboot (void * param)
{
	time_t now;
	struct tm nowDate, rebootDate;

	while (1)
	{
		if (strlen(FusionObject.reboottime) && (strncmp(FusionObject.localFirmwareVer, FusionObject.remoteFirmwareVer, FUSION_FIRMWARE_VER_LEN) != 0))
		{
			// todo : check if remote firmware is older 
			time (&now);
			nowDate = *localtime (&now);

			rebootDate.tm_year = nowDate.tm_year;
			rebootDate.tm_mon = nowDate.tm_mon;
			rebootDate.tm_mday = nowDate.tm_mday;
			sscanf(FusionObject.reboottime, "%02d:%02d:%02d", &rebootDate.tm_hour, &rebootDate.tm_min, &rebootDate.tm_sec);
/*
			eprintf ("%s(%d): Reboot date is %04d-%02d-%02d %02d:%02d:%02d, now = %02d:%02d:%02d %02d:%02d:%02d, checktime = %d\n", 
						  __FUNCTION__, __LINE__, 
						rebootDate.tm_year, rebootDate.tm_mon, rebootDate.tm_mday,
						rebootDate.tm_hour, rebootDate.tm_min, rebootDate.tm_sec,
						nowDate.tm_year, nowDate.tm_mon, nowDate.tm_mday,
						nowDate.tm_hour, nowDate.tm_min, nowDate.tm_sec, 
						FusionObject.checktime);
*/
			double diff = difftime(mktime(&rebootDate), mktime(&nowDate));
			//eprintf ("%s(%d): diff = %f\n", __FUNCTION__, __LINE__, diff);
			if ((diff > 0) && (diff <= 60)){
				//eprintf ("%s(%d): remoteTimestamp = %s, localTimestamp = %s, compare res = %d\n", __FUNCTION__, __LINE__, FusionObject.remoteFirmwareVer, FusionObject.localFirmwareVer,
				//	strncmp(FusionObject.localFirmwareVer, FusionObject.remoteFirmwareVer, FUSION_FIRMWARE_VER_LEN));
				eprintf ("%s(%d): Reboot NOW.\n", __FUNCTION__, __LINE__);
				system ("reboot");
				break;
			}
		}
		fusion_wait(20 * 1000);
	}

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

void * fusion_threadCreepline(void * param)
{
	int result;
	struct timeval tv;

	fusion_readConfig();

	while (1){
		//fusion_readConfig(); // test
		result = fusion_getCreepAndLogo();
		if (result == FUSION_FAIL){
			fusion_wait(FusionObject.checktime * 1000);
			continue;
		}
		else if (result == FUSION_NEW_CREEP){
			gettimeofday(&tv, NULL);
			FusionObject.creepStartTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
			eprintf ("%s(%d): New creep got. Start it.\n", __FUNCTION__, __LINE__);
			FusionObject.deltaTime = 0;
		}
		else if (result == FUSION_SAME_CREEP && FusionObject.creepShown)
		{
			//eprintf ("%s(%d): creepShown got. Wait pause and start again.\n", __FUNCTION__, __LINE__);
			fusion_wait(FusionObject.pause * 1000);
			gettimeofday(&tv, NULL);
			FusionObject.creepStartTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
			FusionObject.creepShown = 0;
			FusionObject.deltaTime = 0;
		}
		fusion_wait(FusionObject.checktime * 1000);
	}

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

int fusion_readConfig()
{
	char * ptr;
	char line [512];
	FILE * f;

	FusionObject.checktime = FUSION_DEFAULT_CHECKTIME;
	sprintf (FusionObject.server, "%s", FUSION_DEFAULT_SERVER_PATH);

	FusionObject.demoUrl[0] = '\0'; // test

	f = fopen(FUSION_HWCONFIG, "rt");
	if (!f) return -1;

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
			eprintf (" %s: server = %s\n",   __FUNCTION__, FusionObject.server);
		}else if ((ptr = strcasestr((const char*) line, (const char*)"CHECKTIME ")) != NULL){
			ptr += 10;
			FusionObject.checktime = atoi(ptr);
			eprintf (" %s: checktime = %d\n",   __FUNCTION__, FusionObject.checktime);
		}else if ((ptr = strcasestr((const char*) line, (const char*)"DEMO ")) != NULL){
			ptr += 5;
			sprintf (FusionObject.demoUrl, "%s", ptr);
			eprintf (" %s: demo url = %s\n",   __FUNCTION__, FusionObject.demoUrl);
		}
	}
	fclose (f);
	return 0;
}

int fusion_refreshDtmfEvent(void *pArg)
{
	if (FusionObject.audHandle == 0){
		FILE * faudhandle = fopen ("/tmp/fusion.audhandle", "rb");
		if (faudhandle){
			fread (&FusionObject.audHandle, sizeof(unsigned int), 1, faudhandle);
			fclose (faudhandle);
			if (FusionObject.audHandle) eprintf("%s(%d): Got FusionObject.audHandle = %d.\n", __FUNCTION__, __LINE__, FusionObject.audHandle);
		}
	}
	else {
		int staudlx_fd;
		STAUD_Ioctl_GetDtmf_t UserData;

		if ((staudlx_fd = open("/dev/stapi/staudlx_ioctl", O_RDWR)) < 0) {
			eprintf ("%s(%d): ERROR! /dev/stapi/staudlx_ioctl open failed.\n",   __FUNCTION__, __LINE__);
		}
		else {
			memset (&UserData, 0, sizeof(STAUD_Ioctl_GetDtmf_t));
			UserData.Handle = FusionObject.audHandle;
			UserData.ErrorCode = 0;
			UserData.count = 0;

			if (ioctl (staudlx_fd, 0xc004c0e4, &UserData)){   // _IOWR(0X16, 228, NULL)   // STAUD_IOC_GETDTMF
				eprintf ("%s(%d): ERROR! ioctl failed.\n",   __FUNCTION__, __LINE__);
			}
			else {
				if ((UserData.ErrorCode == 0) && (UserData.digits[0] != ' '))
				{
					//eprintf ("%s(%d): ioctl rets %c\n", __FUNCTION__, __LINE__, UserData.digits[0]);	// todo : return it!

					pthread_mutex_lock(&FusionObject.mutexDtmf);
					FusionObject.currentDtmfDigit = UserData.digits[0];
					pthread_mutex_unlock(&FusionObject.mutexDtmf);
/*
					pthread_mutex_lock(&FusionObject.mutexDtmf);
					if (strlen(FusionObject.currentDtmfMark) == 0){
						struct timeval tv;
						gettimeofday(&tv, NULL);
						FusionObject.dtmfStartTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
					}
					// add char to end
					if (strlen(FusionObject.currentDtmfMark) >= 5){
						memset (FusionObject.currentDtmfMark, 0, sizeof(FusionObject.currentDtmfMark));
					}
					FusionObject.currentDtmfMark[strlen(FusionObject.currentDtmfMark)] = FusionObject.currentDtmfDigit;

					pthread_mutex_unlock(&FusionObject.mutexDtmf);
					eprintf ("%s(%d): currentDtmfMark = %s\n", __FUNCTION__, __LINE__, FusionObject.currentDtmfMark);
					* */
				}
				else 
				{
					pthread_mutex_lock(&FusionObject.mutexDtmf);
					FusionObject.currentDtmfDigit = '_';
					pthread_mutex_unlock(&FusionObject.mutexDtmf);
				}
				interface_displayDtmf();
			}
		}
		close (staudlx_fd);
	}

	interface_addEvent(fusion_refreshDtmfEvent, (void*)NULL, FUSION_REFRESH_DTMF_MS, 1);
	return 0;
}

int fusion_getUsbRoot()
{
	int hasDrives = 0;
	DIR *usbDir = opendir(USB_ROOT);
	if (usbDir != NULL) {
		struct dirent *first_item = NULL;
		struct dirent *item = readdir(usbDir);
		while (item) {
			if(strncmp(item->d_name, "sd", 2) == 0) {
				hasDrives++;
				if(!first_item)
					first_item = item;
			}
			item = readdir(usbDir);
		}
		if (hasDrives == 1) {
			sprintf(g_usbRoot, "%s%s", USB_ROOT, first_item->d_name);
			eprintf("%s: Found %s\n", __FUNCTION__, g_usbRoot);
			closedir(usbDir);
			return 1;  // yes
		}
		closedir(usbDir);
	}
	else {
		eprintf("%s: opendir %s failed\n", __FUNCTION__, USB_ROOT);
	}
	return 0; // no
}


int fusion_removeFirmwareFormFlash()
{
	char command[PATH_MAX];
	if (strlen(g_usbRoot) == 0){
		if (fusion_getUsbRoot() == 0) return 0; // no
	}
	int result = fusion_checkDirectory(g_usbRoot);
	if (result == 1) // yes
	{
		// remove all *.efp to this folder to prevent firmware update on reboot
		sprintf (command, "rm %s/*.efp", g_usbRoot);
		system (command);
	}
	return result;
}

void fusion_getLocalFirmwareVer()
{
	//FusionObject.localFirmwareVer[0] = '\0';
	memset(FusionObject.localFirmwareVer, '\0', FUSION_FIRMWARE_VER_LEN);
	fusion_getCommandOutput ("cat /firmwareDesc | grep \"pack name:\" | tr -s ' ' | cut -d'.' -f3", FusionObject.localFirmwareVer);  // eg. 201406111921
	FusionObject.localFirmwareVer[strlen (FusionObject.localFirmwareVer)-1] = '\0';
	eprintf ("%s(%d): local firmware version = %s\n", __FUNCTION__, __LINE__, FusionObject.localFirmwareVer);
}

void fusion_startup()
{
	system ("echo 3 >/proc/sys/vm/drop_caches");

	if (fusion_setMoscowDateTime() != 0){
		return;
	}

	memset(&FusionObject, 0, sizeof(interfaceFusionObject_t));

	fusion_getSecret();
	fusion_removeFirmwareFormFlash();
	fusion_getLocalFirmwareVer();

	pthread_mutex_init(&FusionObject.mutexCreep, NULL);
	pthread_mutex_init(&FusionObject.mutexLogo, NULL);
	pthread_mutex_init(&FusionObject.mutexDtmf, NULL);

	//interface_addEvent(fusion_refreshDtmfEvent, (void*)NULL, FUSION_REFRESH_DTMF_MS, 1);
	FusionObject.currentDtmfDigit = '_';

	pthread_create(&FusionObject.threadCreepHandle, NULL, fusion_threadCreepline, (void*)NULL);
	pthread_create(&FusionObject.threadCheckReboot, NULL, fusion_threadCheckReboot, (void*)NULL);
	pthread_create(&FusionObject.threadFlipCreep, NULL, fusion_threadFlipCreep, (void*)NULL);

	return;
}

int fusion_getSecret ()
{
	char mac [16];
	char input[64];
	char output[64];
	int i;
	
	mac[0] = 0;
	input[0] = 0;
	output[0] = 0;
	memset (FusionObject.secret, 0, sizeof(FusionObject.secret));

	FILE *f = popen ("cat /sys/class/net/eth0/address | tr -d ':'", "r");
	if (!f) return -1;

	fgets(mac, sizeof(mac), f);
	pclose(f);
	mac[strlen(mac) - 1] = '\0';

	strcpy(input, FUSION_SECRET);
	strcat(input, mac);

	/* Get MD5 sum of input and convert it to hex string */
	md5((unsigned char*)input, strlen(input), (unsigned char*)output);
	for (i = 0; i < 16; i++)
	{
		//sprintf(&FusionObject.secret[i*2], "%02hhx", output[i]);
		sprintf((char*)&FusionObject.secret[i*2], "%02hhx", output[i]);
	}

	return 0;
}

static char fusion_curlError[CURL_ERROR_SIZE];
int fusion_streamPos;
int fusion_videoStreamPos;

size_t fusion_curlCallback(char * data, size_t size, size_t nmemb, void * stream)
{
	int writtenBytes = size * nmemb;
	memcpy ((char*)stream + fusion_streamPos, data, writtenBytes);
	fusion_streamPos += writtenBytes;
	return writtenBytes;
}

size_t fusion_curlCallbackVideo(char * data, size_t size, size_t nmemb, void * stream)
{
	int writtenBytes = size * nmemb;
	FILE * f = (FILE*)stream;
	fwrite(data, 1, writtenBytes, f);
	fusion_videoStreamPos += writtenBytes;
	return writtenBytes;
}

CURLcode fusion_getDataByCurl (char * url, char * curlStream, int * pStreamLen, curlWriteCB cb)
{
	CURLcode retCode = CURLE_OK;
	CURL * curl = NULL;

	if (!url || !strlen(url)) return -1;

	curl = curl_easy_init();
	if (!curl) return -1;

	*pStreamLen = 0;
	fusion_streamPos = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, fusion_curlError);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curlStream);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);  // 15
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);  // 15
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	//eprintf ("%s: rq: %s\n", __FUNCTION__, url);

	retCode = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl = NULL;
/*#ifdef FUSION_TEST
	char cuttedStream[256];
	snprintf (cuttedStream, 256, "%s", curlStream);
	eprintf ("ans: %s ...\n", cuttedStream);
	//eprintf ("ans: %s\n", playlistBuffer);
#endif
*/

	if (retCode != CURLE_OK && retCode != CURLE_WRITE_ERROR)
	{
		eprintf ("%s: ERROR! %s\n", __FUNCTION__, fusion_curlError);
		return retCode;
	}
	if (!curlStream || strlen(curlStream) == 0) {
		eprintf ("%s: ERROR! empty response.\n", __FUNCTION__);
		return -2;
	}
	*pStreamLen = fusion_streamPos;
	return CURLE_OK;
}

int fusion_downloadPlaylist(char * url, cJSON ** ppRoot)
{
	char * playlistBuffer = NULL;
	int res;

	int buflen;

	if (!url || !strlen(url)) return -1;
	playlistBuffer = (char *)malloc(FUSION_STREAM_SIZE);
	if (!playlistBuffer){
		eprintf ("%s(%d): ERROR! Malloc failed.\n",   __FUNCTION__, __LINE__);
		return -1;
	}
	memset(playlistBuffer, 0, FUSION_STREAM_SIZE);

	eprintf ("%s(%d): rq: %s ...\n",   __FUNCTION__, __LINE__, url);
	fusion_getDataByCurl(url, playlistBuffer, &buflen, (curlWriteCB)fusion_curlCallback);

#ifdef FUSION_TEST
	char cuttedStream[256];
	snprintf (cuttedStream, 256, "%s", playlistBuffer);
	eprintf ("ans: %s ...\n", cuttedStream);
	//eprintf ("ans: %s\n", playlistBuffer);
#endif
	
	if (strlen(playlistBuffer) == 0) {
		eprintf (" %s: ERROR! empty response.\n",   __FUNCTION__);
		free(playlistBuffer);
		return -1;
	}
	res = fusion_checkResponse(playlistBuffer, ppRoot);
	free(playlistBuffer);
	return res;
}

int fusion_checkResponse (char * curlStream, cJSON ** ppRoot)
{
	*ppRoot = cJSON_Parse(curlStream);
	if (!(*ppRoot)) {
		char cuttedStream[256];
		snprintf (cuttedStream, 256, curlStream);
		eprintf ("%s: ERROR! Not a JSON response. %s\n",   __FUNCTION__, cuttedStream);
		return -1;
	}
	return 0;
}

int fusion_setMoscowDateTime()
{
	char setDateString[64];
	char utcBuffer [1024];
	memset(utcBuffer, 0, 1024);

	if (fusion_getUtc(utcBuffer, 1024) != 0) {
		eprintf ("%s(%d): WARNING! Couldn't get UTC datetime.\n", __FUNCTION__, __LINE__);
		return -1;
	}
	// format UTC: 2014-04-04 11:43:48
	sprintf (setDateString, "date -u -s \"%s\"", utcBuffer);
	eprintf ("%s: Command: %s\n", __FUNCTION__, setDateString);
	system(setDateString);

	// set timezone
	system("rm /var/etc/localtime");
	system("ln -s /usr/share/zoneinfo/Russia/Moscow /var/etc/localtime");

	return 0;
}


int fusion_getUtc (char * utcBuffer, int size)
{
	FILE * f;
	char request[256];
	char * ptrStartUtc, * ptrEndUtc;
	char utcData[64];
	memset(utcData, 0, 64);

	if (!utcBuffer) {
		eprintf ("%s(%d): WARNING! Invalid arg.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	// use earthtools instead timeapi.org which is currently dead
	//sprintf (request, "wget \"http://www.timeapi.org/utc/now?format=%%25Y-%%25m-%%25d%%20%%20%%25H:%%25M:%%25S\" -O /tmp/utc.txt");  // 2>/dev/null
	sprintf (request, "wget \"http://www.earthtools.org/timezone/0/0\" -O /tmp/utc.txt");
	eprintf ("%s(%d): rq:  %s...\n",   __FUNCTION__, __LINE__, request);
	system (request);

	f = fopen("/tmp/utc.txt", "rt");
	if (!f) {
		eprintf ("%s(%d): ERROR! Couldn't open /tmp/utc.txt.\n",   __FUNCTION__, __LINE__);
		return -1;
	}
	fread(utcBuffer, size, 1, f);
	fclose(f);

	eprintf ("ans: %s\n", utcBuffer);

	ptrStartUtc = strstr(utcBuffer, "<utctime>");
	if (ptrStartUtc) {
		ptrStartUtc += 9;
		ptrEndUtc = strstr(ptrStartUtc, "</utctime>");
		if (ptrEndUtc){
			snprintf (utcData, (int)(ptrEndUtc - ptrStartUtc) + 1, "%s", ptrStartUtc);
		}
	}
	sprintf (utcBuffer, "%s", utcData);

	if (!strlen(utcBuffer) || !strchr(utcBuffer, ' ')) {
		eprintf ("%s(%d): WARNING! Incorrect answer: %s\n", __FUNCTION__, __LINE__, utcBuffer);
		return -1;
	}
	return 0;
}


int32_t fusion_checkDirectory(const char *path)
{
	DIR *d;

	d = opendir(path);
	if (d == NULL)
		return 0;
	closedir(d);
	return 1;
}

int fusion_checkLastModified (char * url)
{
	CURL *curl;
	CURLcode res;
	long lastModified;
	if (!url || strlen(url) == 0) 
		return FUSION_ERR_FILETIME;

	curl = curl_easy_init();
	if (!curl) return FUSION_ERR_FILETIME;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
	curl_easy_setopt(curl, CURLOPT_FILETIME, 1);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	curl_easy_perform(curl);
	res = curl_easy_getinfo(curl, CURLOPT_FILETIME, &lastModified);
	curl_easy_cleanup(curl);
	if (res != 0) {
		eprintf ("%s: WARNING! Couldn't get filetime for %s\n", __FUNCTION__, url);
		return FUSION_ERR_FILETIME;
	}
	eprintf ("%s: lastModified = %ld for %s...\n", __FUNCTION__, lastModified, url);

	if (FusionObject.lastModified == lastModified)
		return FUSION_NOT_MODIFIED;

	FusionObject.lastModified = lastModified;
	return FUSION_MODIFIED;
}

long fusion_getRemoteFileSize(char * url)
{
	CURL *curl;
	CURLcode res;
	double remoteFileSize = 0;
	if (!url || strlen(url) == 0) return 0;

	curl = curl_easy_init();
	if (!curl) return 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	curl_easy_perform(curl);
	res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &remoteFileSize);
	if (res != 0) {
		remoteFileSize = -1;
	}
	curl_easy_cleanup(curl);

	//eprintf ("%s: RemoteFileSize = %ld for %s...\n", __FUNCTION__, (long)remoteFileSize, url);
	return (long)remoteFileSize;
}

int fusion_checkFileIsDownloaded(char * remotePath, char * localPath)
{
	long int localFileSize = 0;
	int remoteSize = 0;
	if (!remotePath || !localPath) return 0;

	// get local size
	FILE * f = fopen(localPath, "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	localFileSize = ftell(f);
	fclose(f);
	if (localFileSize == 0) return 0;

	// get remote size
	remoteSize = fusion_getRemoteFileSize(remotePath);

	//eprintf ("%s: remoteFileSize = %ld, localFileSize = %ld\n", __FUNCTION__, remoteSize, localFileSize);
	if (remoteSize == localFileSize) return 1;
	return 0;
}

int fusion_createMarksDirectory(char * folderPath)
{
	if (!folderPath) return -1;
	if (fusion_checkDirectory("/tmp/fusionmarks") != 1){
		system ("mkdir /tmp/fusionmarks");
		sprintf (folderPath, "/tmp/fusionmarks");
		eprintf ("%s(%d): Created folderToSaveMarks = %s.\n", __FUNCTION__, __LINE__, folderPath);
	}
	return 0;
}

CURLcode fusion_getVideoByCurl (char * url, void * fsink/*char * curlStream*/, int * pStreamLen, curlWriteCB cb)
{
	CURLcode retCode = CURLE_OK;
	CURL * curl = NULL;

	if (!url || !strlen(url)) return -1;

	curl = curl_easy_init();
	if (!curl) return -1;

	*pStreamLen = 0;
	fusion_videoStreamPos = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, fusion_curlError);
	//curl_easy_setopt(curl, CURLOPT_WRITEDATA, curlStream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fsink);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);  // 15
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);  // 15
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	//eprintf ("%s: rq: %s\n", __FUNCTION__, url);

	retCode = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl = NULL;

	if (retCode != CURLE_OK && retCode != CURLE_WRITE_ERROR)
	{
		eprintf ("%s: ERROR! %s\n", __FUNCTION__, fusion_curlError);
		return retCode;
	}
	*pStreamLen = fusion_videoStreamPos;
	return CURLE_OK;
}

int fusion_downloadMarkFile(int index, char * folderPath)
{
	char url[PATH_MAX];
	char localTsPath[PATH_MAX];
	char localPathInProgress[PATH_MAX];
	char symlinkPath[PATH_MAX];
	char command[PATH_MAX];
	int buflen = 0;
	char * ptr, *ptrSlash;
	FILE * f;

	if (index < 0 || index > FUSION_MAX_MARKS) return -1;
	if (!strlen(FusionObject.marks[index].link)) return 0;

	// make filename from url
	sprintf (url, "%s", FusionObject.marks[index].link);
	ptr = strstr(url, "http:");
	if (ptr == NULL) return -1;
	ptr += 7;

	// replace / with _
	while ((ptrSlash = strchr(ptr, '/')) != NULL){
		ptrSlash[0] = '_';
	}

	sprintf (localTsPath, "%s/%s", folderPath, ptr);
	sprintf (symlinkPath, "%s/%d_%d.ts", folderPath, index, FusionObject.marks[index].duration);
	//eprintf ("%s(%d): localTsPath is %s.\n", __FUNCTION__, __LINE__, localTsPath);
	if (fusion_checkFileIsDownloaded(FusionObject.marks[index].link, localTsPath) != 1)  // file is ok
	{
		// make name with *.part to indicate that it is not ready
		sprintf (localPathInProgress, "%s.part", localTsPath);
		f = fopen (localPathInProgress, "wb");
		if (!f){
			eprintf ("%s(%d): ERROR! Couldn't open file %s for writing.\n", __FUNCTION__, __LINE__, localPathInProgress);
			return -1;
		}
		fusion_getVideoByCurl(FusionObject.marks[index].link, (void*)f, &buflen, (curlWriteCB)fusion_curlCallbackVideo);
		eprintf ("%s(%d): Got %s file of size %d.\n", __FUNCTION__, __LINE__, FusionObject.marks[index].link, buflen);
		fclose (f);
		// rename file to indicate it is ready
		snprintf (command, PATH_MAX, "mv %s %s", localPathInProgress, localTsPath);
	}

	// create symlink, remove old one if exists
	//eprintf ("%s(%d): create new symlink %s to %s.\n", __FUNCTION__, __LINE__, symlinkPath, localTsPath);
	sprintf (command, "ln -sf %s %s", localTsPath, symlinkPath);
	system (command);
	return 0;
}


int fusion_removeOldMarkVideo(char * folderPath)
{
	DIR *d;
	struct dirent *dir;
	if (!folderPath) return -1;

	d = opendir(folderPath);
	if (!d) return -1;

	while ((dir = readdir(d)) != NULL)
	{
		if (dir->d_type == DT_REG && strstr(dir->d_name, ".ts"))
		{
			char filepath [PATH_MAX];
			sprintf (filepath, "%s/%s", folderPath, dir->d_name);

			// find index from dir->d_name
			char * ptrTs = strstr(dir->d_name, ".ts");
			if (ptrTs){
				char indexString[PATH_MAX];
				int index;
				snprintf (indexString, (int)(ptrTs - dir->d_name + 1), "%s", dir->d_name);
				//eprintf ("%s(%d): TS found: %s\n", __FUNCTION__, __LINE__, indexString);
				index = atoi(indexString);
				if ((index > 0) && (index < FUSION_MAX_MARKS)){
					if (FusionObject.marks[index].duration == 0)
					{
						char cmd[PATH_MAX];
						snprintf (cmd, PATH_MAX, "rm -f %s", filepath);
						eprintf ("%s(%d): Remove %s\n", __FUNCTION__, __LINE__, cmd);
						system (cmd);
					}
				}
			}
		}
	}
	closedir(d);

	return 0;
}

int fusion_checkSavedPlaylist()
{
	FILE * f;
	char cmd[PATH_MAX];
	char savedPath [PATH_MAX];
	sprintf (savedPath, "%s/fusion/"FUSION_PLAYLIST_NAME, g_usbRoot);

	memset(FusionObject.savedPlaylist, 0, FUSION_STREAM_SIZE);

	f = fopen(savedPath, "rt");
	if (!f) {
		eprintf ("%s(%d): No saved playlist yet.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	fread(FusionObject.savedPlaylist, FUSION_STREAM_SIZE, 1, f);
	fclose(f);

	if (strlen(FusionObject.savedPlaylist) == 0) {
		eprintf ("%s(%d): WARNING! Empty saved playlist. Remove it.\n", __FUNCTION__, __LINE__);
		memset(FusionObject.savedPlaylist, 0, FUSION_STREAM_SIZE);
		sprintf (cmd, "rm %s", savedPath);
		system(cmd);
		return -1;
	}
	return 1;
}

int fusion_getCreepAndLogo ()
{
	cJSON * root;
	char request[FUSION_URL_LEN];
	int i;
	time_t now;
	struct tm nowDate;
	int result = 0;
	int oldMode;

	if (!fusion_checkDirectory("/tmp/fusion")) {
		system("mkdir /tmp/fusion");
	}

	time (&now);
	nowDate = *localtime (&now);

	if (strlen(FusionObject.demoUrl)){
		sprintf (request, "%s", FusionObject.demoUrl);
	}
	else {
		sprintf (request, "%s/?s=%s&c=playlist_full&date=%04d-%02d-%02d", FusionObject.server, FusionObject.secret, 
		         nowDate.tm_year + 1900, nowDate.tm_mon+1, nowDate.tm_mday);
	}
/*
	result = fusion_checkLastModified(request);
	if (result == FUSION_NOT_MODIFIED){
		eprintf ("%s(%d): Playlist not modified.\n",   __FILE__, __LINE__);
		return FUSION_SAME_CREEP;
	}
	else if (result == FUSION_ERR_FILETIME){
		eprintf ("%s(%d): WARNING! Problem getting playlist modification time.\n",   __FILE__, __LINE__);
		//return FUSION_SAME_CREEP;
	}
	else eprintf ("%s(%d): Playlist modified.\n",   __FILE__, __LINE__);
	*/

	if (fusion_downloadPlaylist(request, &root) != 0) {
		eprintf ("%s(%d): WARNING! download playlist failed. Search for saved one.\n",   __FILE__, __LINE__);
		
		// test - use saved playlist
		// test - we may have usb here already
		if (strlen(g_usbRoot) == 0){
			if (fusion_getUsbRoot() == 0){
				eprintf ("%s: ERROR! No flash found.\n", __FUNCTION__);
				return FUSION_FAIL;
			}
		}
		// end test usb
		if (fusion_checkSavedPlaylist() == 1){
			root = cJSON_Parse(FusionObject.savedPlaylist);
			if (!root) {
				char cmd[PATH_MAX];
				char savedPath [PATH_MAX];
				sprintf (savedPath, "%s/fusion/"FUSION_PLAYLIST_NAME, g_usbRoot);
				eprintf ("%s: WARNING! Incorrect format in saved playlist. Remove it.\n",   __FUNCTION__);
				memset(FusionObject.savedPlaylist, 0, FUSION_STREAM_SIZE);
				sprintf (cmd, "rm %s", savedPath);
				system(cmd);
				return FUSION_FAIL;
			}
		}
		// end test saved
	}

	oldMode = FusionObject.mode;
	cJSON* jsonMode = cJSON_GetObjectItem(root, "mode");
	if (jsonMode)
	{
		if (jsonMode->valuestring){
			if (strncmp("ondemand", jsonMode->valuestring, 8) == 0){
				FusionObject.mode = FUSION_MODE_FILES;
				snprintf(FusionObject.streamUrl, PATH_MAX, FUSION_STUB);
				//eprintf ("%s(%d): On-demand stub = %s.\n", __FUNCTION__, __LINE__, FusionObject.streamUrl);
			}
			else if (strncmp("stream", jsonMode->valuestring, 6) == 0){
				FusionObject.mode = FUSION_MODE_HLS;

				cJSON * jsonStreamUrl =  cJSON_GetObjectItem(root, "stream");
				if (jsonStreamUrl && jsonStreamUrl->valuestring){
					snprintf(FusionObject.streamUrl, PATH_MAX, jsonStreamUrl->valuestring);
				}
			}
			else if (strncmp("tv", jsonMode->valuestring, 2) == 0){
				//eprintf ("%s(%d): Mode satellite.\n", __FUNCTION__, __LINE__);
				FusionObject.mode = FUSION_MODE_TV;
			}
		}
	}
	if (oldMode != FusionObject.mode)
	{
		// stop old video
		if (appControlInfo.mediaInfo.active){
			eprintf ("%s(%d): WARNING! Mode changed. Stop current playback.\n", __FUNCTION__, __LINE__);
			media_stopPlayback();
			appControlInfo.mediaInfo.filename[0] = 0;
		}
		// start new video
		switch (FusionObject.mode){
			case FUSION_MODE_FILES:
			case FUSION_MODE_HLS:				// test to see how fusion ondemand stops
				snprintf (appControlInfo.mediaInfo.filename, PATH_MAX, "%s", FusionObject.streamUrl);
				appControlInfo.playbackInfo.playingType = media_getMediaType(appControlInfo.mediaInfo.filename);
				appControlInfo.mediaInfo.bHttp = 1;

				eprintf ("%s(%d): Start playing in new mode %s.\n", __FUNCTION__, __LINE__, (FusionObject.mode == FUSION_MODE_FILES)?"ondemand":"hls");
				int result = media_startPlayback();
				if (result == 0){
					eprintf ("%s(%d): Started %s\n", __FUNCTION__, __LINE__, FusionObject.streamUrl);
				}
				else {
					eprintf ("%s(%d): ERROR! media_startPlayback rets %d\n", __FUNCTION__, __LINE__, result);
				}
			break;
			default:
				eprintf ("%s(%d): WARNING! Mode %d unsupported.\n", __FUNCTION__, __LINE__, FusionObject.mode);
			break;
		}
	}
	else {
		// do nothing
	}

	cJSON * jsonFirmware = cJSON_GetObjectItem(root, "firmware");
	cJSON * jsonReboot = cJSON_GetObjectItem(root, "reboot_time");
	if (jsonFirmware && jsonReboot){
		if (strcmp(FusionObject.firmware, jsonFirmware->valuestring)){
			char cmd [PATH_MAX];
			snprintf (FusionObject.firmware, PATH_MAX, jsonFirmware->valuestring);

			if (strcmp(FusionObject.reboottime, jsonReboot->valuestring)){
				snprintf (FusionObject.reboottime, PATH_MAX, jsonReboot->valuestring);
			}

			// get datetime of remote firmware
			memset(FusionObject.remoteFirmwareVer, '\0', FUSION_FIRMWARE_VER_LEN);
			char * ptrDev = strstr(FusionObject.firmware, "dev");
			if (ptrDev){
				char * ptrFirstDot = strchr(ptrDev, '.');
				if (ptrFirstDot){
					ptrFirstDot ++;
					char * ptrLastDot = strchr(ptrFirstDot, '.');
					if (ptrLastDot){
						snprintf (FusionObject.remoteFirmwareVer, min(32, (int)(ptrLastDot - ptrFirstDot + 1)), ptrFirstDot);
						eprintf ("%s(%d): remoteTimestamp = %s\n", __FUNCTION__, __LINE__, FusionObject.remoteFirmwareVer);
					}
				}
			}
			// check if remoteFirmwareVer is older then installed one
			// then dont save params in hwconfig to prevent overwriting
			int remoteMon, remoteDay, remoteYear, remoteHour, remoteMin;
			int localMon, localDay, localYear, localHour, localMin;
			int scannedItemsRemote, scannedItemsLocal;
			// eg. 201407251445
			scannedItemsRemote = sscanf(FusionObject.remoteFirmwareVer, "%04d%02d%02d%02d%02d", &remoteYear, &remoteMon, &remoteDay, &remoteHour, &remoteMin);
			scannedItemsLocal = sscanf(FusionObject.localFirmwareVer,  "%04d%02d%02d%02d%02d", &localYear, &localMon, &localDay, &localHour, &localMin);
			do {
				if (scannedItemsRemote != 5 || scannedItemsLocal != 5) break;
				if (remoteYear < localYear) break;
				if (remoteYear == localYear && remoteMon < localMon) break;
				if (remoteYear == localYear && remoteMon == localMon && remoteDay < localDay) break;
				if (remoteYear == localYear && remoteMon == localMon && remoteDay == localDay && remoteHour < localHour) break;
				if (remoteYear == localYear && remoteMon == localMon && remoteDay == localDay && remoteHour == localHour && remoteMin < localMin) break;

				eprintf ("%s(%d): Remote firmware is newer than installed one.\n", __FUNCTION__, __LINE__);
				eprintf ("%s(%d): Firmware path is %s\n", __FUNCTION__, __LINE__, FusionObject.firmware);
				eprintf ("%s(%d): Reboottime is %s\n", __FUNCTION__, __LINE__, FusionObject.reboottime);
				system("hwconfigManager s 0 UPFOUND 1");
				//system("hwconfigManager s 0 UPNOUSB 1");	// switch off usb check
				system("hwconfigManager f 0 UPNOUSB");	// remove no-checking usb on reboot
				system("hwconfigManager s 0 UPNET 1");	// check remote firmware every reboot
				sprintf (cmd, "hwconfigManager l 0 UPURL '%s'", FusionObject.firmware);
				system (cmd);

				// check if we have wifi connection
				char wifiStatus[8];
				fusion_getCommandOutput ("edcfg /var/etc/ifcfg-wlan0 get WAN_MODE", wifiStatus);
				if (strncmp(wifiStatus, "1", 1) == 0){
					// wifi is on and is used as external interface
					eprintf ("%s(%d): Wifi detected.\n", __FUNCTION__, __LINE__);
					pthread_t handle;
					pthread_create(&handle, NULL, fusion_threadDownloadFirmware, (void*)FusionObject.firmware);
					pthread_detach(handle);
				}

			} while (0);
		}
	}else {
		FusionObject.firmware[0] = '\0';
		FusionObject.reboottime[0] = '\0';
		system("hwconfigManager f 0 UPURL 1");	// remove update url
	}

	cJSON * jsonLogo = cJSON_GetObjectItem(root, "logo");
	if (jsonLogo){
		pthread_mutex_lock(&FusionObject.mutexLogo);

		FusionObject.logoCount = cJSON_GetArraySize(jsonLogo);
		if (FusionObject.logoCount > FUSION_MAX_LOGOS){
			FusionObject.logoCount = FUSION_MAX_LOGOS;
		}
		for (i=0; i<FusionObject.logoCount; i++){
			cJSON * jsonItem = cJSON_GetArrayItem(jsonLogo, i);
			if (!jsonItem || !jsonItem->string || !jsonItem->valuestring) continue;

			if (!strcasecmp(jsonItem->string, FUSION_TOP_LEFT_STR)){
				FusionObject.logos[i].position = FUSION_TOP_LEFT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_TOP_RIGHT_STR)){
				FusionObject.logos[i].position = FUSION_TOP_RIGHT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_BOTTOM_LEFT_STR)){
				FusionObject.logos[i].position = FUSION_BOTTOM_LEFT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_BOTTOM_RIGHT_STR)){
				FusionObject.logos[i].position = FUSION_BOTTOM_RIGHT;
			}
			else continue;

			sprintf (FusionObject.logos[i].url, "%s", jsonItem->valuestring);
			//eprintf ("%s(%d): logo[%d] = %s\n", __FUNCTION__, __LINE__, i, FusionObject.logos[i].url);

			long logoFileSize = fusion_getRemoteFileSize(FusionObject.logos[i].url);

			if (logoFileSize <= 0){
				eprintf ("%s(%d): WARNING! Failed to get remote logo size.\n", __FUNCTION__, __LINE__);
				FusionObject.logos[i].filepath[0] = '\0';
				continue;
			}
			// remove and wget only if we got remote size

			char tmpStr[PATH_MAX];
			char * ptr, *ptrSlash;
			sprintf (tmpStr, "%s", FusionObject.logos[i].url);
			ptr = strstr(tmpStr, "http:");
			if ((ptr != NULL) && (logoFileSize > 0)) {
				ptr += 7;
				while ((ptrSlash = strchr(ptr, '/')) != NULL) ptrSlash[0] = '_';
				sprintf (FusionObject.logos[i].filepath, "/tmp/fusion/logo_%ld_%s", logoFileSize, ptr);
			}
			else {
				eprintf ("%s(%d): WARNING! Incorrect logo url\n", __FUNCTION__, __LINE__); 
				FusionObject.logos[i].filepath[0] = '\0';
			}
			//eprintf ("%s(%d): logo[%d] = %s, position = %d, path = %s\n", __FUNCTION__, __LINE__, 
			//	i, FusionObject.logos[i].url, FusionObject.logos[i].position, FusionObject.logos[i].filepath);
			
			// get local file size and compare
			// dont donwload if we have logo on flash
			long int localFileSize = 0;
			FILE * f = fopen(FusionObject.logos[i].filepath, "rb");
			if (f) {
				fseek(f, 0, SEEK_END);
				localFileSize = ftell(f);
				fclose(f);
			}
			if (localFileSize != logoFileSize){
				char cmd[128];
				//sprintf (cmd, "wget \"%s\" -O %s 2>/dev/null", url, logoPath);
				sprintf (cmd, "rm -f \"%s\"", FusionObject.logos[i].filepath);
				system(cmd);
				sprintf (cmd, "wget \"%s\" -O %s", FusionObject.logos[i].url, FusionObject.logos[i].filepath);
				system(cmd);
			}
		}
		pthread_mutex_unlock(&FusionObject.mutexLogo);
		interface_displayMenu(1);
	}

	result = FUSION_SAME_CREEP;
	//cJSON * jsonCreep = cJSON_GetObjectItem(root, "creep\u00adline");
	cJSON * jsonCreep = cJSON_GetObjectItem(root, "creep-line");
	if (jsonCreep){
		int creepCount = cJSON_GetArraySize(jsonCreep);
		int allCreepsLen = 0;
		int allCreepsWithSpaceLen = 0;
		char * allCreeps;
		for (i=0; i<creepCount; i++){
			cJSON * jsonItem = cJSON_GetArrayItem(jsonCreep, i);
			if (!jsonItem) continue;
			cJSON * jsonText = cJSON_GetObjectItem(jsonItem, "text");
			if (!jsonText) continue;
			allCreepsLen += FUSION_MAX_CREEPLEN;
		}
		allCreepsWithSpaceLen = allCreepsLen + (creepCount-1)*strlen(FUSION_CREEP_SPACES);
		allCreeps = (char*)malloc(allCreepsWithSpaceLen);
		allCreeps[0] = '\0';
		if (!allCreeps) {
			eprintf ("%s(%d): ERROR! Couldn't malloc %d bytes.\n", __FUNCTION__, __LINE__, allCreepsLen);
			cJSON_Delete(root);
			return FUSION_SAME_CREEP;
		}
		for (i=0; i<creepCount; i++){
			cJSON * jsonItem = cJSON_GetArrayItem(jsonCreep, i);
			if (!jsonItem) continue;
			cJSON * jsonText = cJSON_GetObjectItem(jsonItem, "text");
			if (!jsonText) continue;

			// todo : now count and pause are rewritten
			// do something with it
			cJSON * jsonPause = cJSON_GetObjectItem(jsonItem, "pause");
			if (!jsonPause) FusionObject.pause = FUSION_DEFAULT_CREEP_PAUSE;
			else {
				FusionObject.pause = atoi(jsonPause->valuestring);
			}
			cJSON * jsonRepeats = cJSON_GetObjectItem(jsonItem, "count");
			if (!jsonRepeats) FusionObject.repeats = FUSION_DEFAULT_CREEP_REPEATS;
			else {
				FusionObject.repeats = atoi(jsonRepeats->valuestring);
			}

			if (i) strncat(allCreeps, FUSION_CREEP_SPACES, strlen(FUSION_CREEP_SPACES));
			strncat(allCreeps, jsonText->valuestring, FUSION_MAX_CREEPLEN /*strlen(jsonText->valuestring)*/);
		}
		for (int k=0; k<allCreepsWithSpaceLen; k++){
			if (allCreeps[k] == '\n' || allCreeps[k] == '\r') allCreeps[k] = ' ';
		}

		if (!FusionObject.creepline){
			result = FUSION_NEW_CREEP;

			pthread_mutex_lock(&FusionObject.mutexCreep);
			FusionObject.creepline = (char*)malloc(allCreepsWithSpaceLen);
			if (!FusionObject.creepline) {
				eprintf ("%s(%d): ERROR! Couldn't malloc %d bytes\n", __FUNCTION__, __LINE__, allCreepsWithSpaceLen);
				cJSON_Delete(root);
				pthread_mutex_unlock(&FusionObject.mutexCreep);
				return FUSION_SAME_CREEP;
			}
			FusionObject.creepline[0] = '\0';
			snprintf (FusionObject.creepline, allCreepsWithSpaceLen, "%s", allCreeps);
			pthread_mutex_unlock(&FusionObject.mutexCreep);
			//eprintf ("%s(%d): creepline = %s, pause = %d, repeats = %d\n", __FUNCTION__, __LINE__, FusionObject.creepline, FusionObject.pause, FusionObject.repeats);
		}
		else if (strcmp(FusionObject.creepline, allCreeps)) {
			result = FUSION_NEW_CREEP;
			pthread_mutex_lock(&FusionObject.mutexCreep);
			if (FusionObject.creepline) {
				free (FusionObject.creepline);
				FusionObject.creepline = NULL;
			}
			FusionObject.creepline = (char*)malloc(allCreepsWithSpaceLen);
			if (!FusionObject.creepline) {
				eprintf ("%s(%d): ERROR! Couldn't malloc %d bytes\n", __FUNCTION__, __LINE__, allCreepsWithSpaceLen);
				cJSON_Delete(root);
				pthread_mutex_unlock(&FusionObject.mutexCreep);
				return FUSION_SAME_CREEP;
			}
			snprintf (FusionObject.creepline, allCreepsWithSpaceLen, "%s", allCreeps);
			pthread_mutex_unlock(&FusionObject.mutexCreep);
			//eprintf ("%s(%d): creepline = %s, pause = %d, repeats = %d\n", __FUNCTION__, __LINE__, FusionObject.creepline, FusionObject.pause, FusionObject.repeats);
		}
		fusion_font->GetStringWidth(fusion_font, FusionObject.creepline, -1, &FusionObject.creepWidth);

		if (FusionObject.creepWidth && (result == FUSION_NEW_CREEP)){

			int surfaceHeight = FUSION_FONT_HEIGHT * 2;
			int surfaceWidth = FusionObject.creepWidth + interfaceInfo.screenWidth;

			pthread_mutex_lock(&FusionObject.mutexDtmf);
			if (FusionObject.preallocSurface){
				free (FusionObject.preallocSurface);
				FusionObject.preallocSurface = NULL;
			}
			//eprintf ("%s(%d): malloc preallocSurface of size %d bytes...\n", __FUNCTION__, __LINE__, FusionObject.creepWidth * interfaceInfo.screenHeight * 4);
			FusionObject.preallocSurface = malloc (surfaceWidth * surfaceHeight * 4);
			if (FusionObject.preallocSurface)
			{
				DFBSurfaceDescription fusion_desc;
				fusion_desc.flags = DSDESC_PREALLOCATED | DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
				//fusion_desc.caps = DSCAPS_SYSTEMONLY | DSCAPS_STATIC_ALLOC | DSCAPS_DOUBLE | DSCAPS_FLIPPING;
				fusion_desc.caps = DSCAPS_NONE;
				fusion_desc.pixelformat = DSPF_ARGB;
				fusion_desc.width = surfaceWidth;
				fusion_desc.height = surfaceHeight;

				fusion_desc.preallocated[0].data = FusionObject.preallocSurface;
				fusion_desc.preallocated[0].pitch = fusion_desc.width * 4;
				fusion_desc.preallocated[1].data = NULL;
				fusion_desc.preallocated[1].pitch = 0;

				if (fusion_surface){
					fusion_surface->Release(fusion_surface);
					fusion_surface = NULL;
				}

				DFBCHECK (pgfx_dfb->CreateSurface (pgfx_dfb, &fusion_desc, &fusion_surface));
				fusion_surface->GetSize (fusion_surface, &fusion_desc.width, &fusion_desc.height);

				int x = 0;
				int y = FUSION_FONT_HEIGHT;
				gfx_drawText(fusion_surface, fusion_font, 255, 255, 255, 255, x, y, FusionObject.creepline, 0, 1);
				// clear fusion_surface after creep tail
				gfx_drawRectangle(fusion_surface, 0x0, 0x0, 0x0, 0x0, x+FusionObject.creepWidth, 0, fusion_desc.width - (x + FusionObject.creepWidth), fusion_desc.height);
			}
			else {
				eprintf("%s(%d): ERROR malloc %d bytes\n", __FUNCTION__, __LINE__, FusionObject.creepWidth * surfaceHeight * 4);
			}
			pthread_mutex_unlock(&FusionObject.mutexDtmf);
		}
	}
	else {
		//eprintf("%s(%d): No creepline field on playlist.\n", __FUNCTION__, __LINE__);
		pthread_mutex_lock(&FusionObject.mutexCreep);
		if (FusionObject.creepline) {
			free (FusionObject.creepline);
			FusionObject.creepline = NULL;
		}
		pthread_mutex_unlock(&FusionObject.mutexCreep);
	}

	cJSON * jsonMarks = cJSON_GetObjectItem(root, "mark");
	if (jsonMarks){
		char folderToSaveMarks[PATH_MAX];
		fusion_createMarksDirectory(folderToSaveMarks);

		int markCount = cJSON_GetArraySize(jsonMarks);
		for (i=0; i<markCount; i++)
		{
			cJSON * jsonItem = cJSON_GetArrayItem(jsonMarks, i);
			if (jsonItem){
				long fileIndex = -1;
				cJSON * jsonMarkLink = cJSON_GetObjectItem(jsonItem, "link");
				cJSON * jsonMarkDuration = cJSON_GetObjectItem(jsonItem, "duration");

				if (jsonItem->string){
					char * err;
					fileIndex = strtol(jsonItem->string, &err, 10);
					if ((*err) || (fileIndex < 0) || (fileIndex > FUSION_MAX_MARKS)){
						eprintf ("%s(%d): WARNING! file index in marks is incorrect: %s.\n", __FUNCTION__, __LINE__, jsonItem->string);
						continue;
					}
					fileIndex = fileIndex - 1; // because it starts from 1
				}

				if (jsonMarkDuration && jsonMarkDuration->valuestring){
					int markIndex = atoi(jsonMarkDuration->valuestring);
					if (markIndex > 0){
						FusionObject.marks[fileIndex].duration = markIndex;
					}
					else FusionObject.marks[fileIndex].duration = 0;
				}

				if (jsonMarkLink && jsonMarkLink->valuestring){
					if (strcmp(FusionObject.marks[fileIndex].link, jsonMarkLink->valuestring))
					{
						sprintf (FusionObject.marks[fileIndex].link, "%s", jsonMarkLink->valuestring);
						fusion_downloadMarkFile(fileIndex, folderToSaveMarks);
					}
				}
			}
		}
		//fusion_removeOldMarkVideo(folderToSaveMarks);		// todo : remove symlinks and old video more correct
	} // if (jsonMarks)

	cJSON_Delete(root);
	return result;
}

void fusion_cleanup()
{
	if (FusionObject.threadCreepHandle){
		pthread_cancel(FusionObject.threadCreepHandle);
		pthread_join(FusionObject.threadCreepHandle, NULL);
	}
	if (FusionObject.threadCheckReboot){
		pthread_cancel(FusionObject.threadCheckReboot);
		pthread_join(FusionObject.threadCheckReboot, NULL);
	}
	if (FusionObject.threadFlipCreep){
		pthread_cancel(FusionObject.threadFlipCreep);
		pthread_join(FusionObject.threadFlipCreep, NULL);
	}
	if (FusionObject.creepline) {
		free (FusionObject.creepline);
		FusionObject.creepline = NULL;
	}

	if (fusion_surface){
		fusion_surface->Release(fusion_surface);
		fusion_surface = NULL;
	}

	pthread_mutex_lock(&FusionObject.mutexDtmf);
	if (FusionObject.preallocSurface){
		free(FusionObject.preallocSurface);
		FusionObject.preallocSurface = 0;
	}
	pthread_mutex_unlock(&FusionObject.mutexDtmf);

	pthread_mutex_destroy(&FusionObject.mutexCreep);
	pthread_mutex_destroy(&FusionObject.mutexLogo);
	pthread_mutex_destroy(&FusionObject.mutexDtmf);
}
#endif // ENABLE_FUSION
