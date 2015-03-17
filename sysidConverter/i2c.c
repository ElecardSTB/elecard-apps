/*
 * Copyright (C) 2012 by Elecard-CTP.
 * Written by Anton Sergeev <Anton.Sergeev@elecard.ru>
 * 
 * It's related to i2c api.
 * 
 * Modified for sysidConverter tool by Victoria Peshkova <Victoria.Peshkova@elecard.ru>
 */
/**
 @File   i2c.c
 @brief
*/

#include "i2c.h"

#define I2C_PREFIX		"/dev/i2c-"
#define I2C_BUS			"/dev/i2c-3"
#define EEPROM_ADDR		0x50
#define MAGIC_NUM		0x53544231
#define GARB_MAGIC		"AB0515100"

#define eprintf(x...) \
	do { \
		time_t __ts__ = time(NULL); \
		struct tm *__tsm__ = localtime(&__ts__); \
		printf("[%02d:%02d:%02d]: ", __tsm__->tm_hour, __tsm__->tm_min, __tsm__->tm_sec); \
		printf(x); \
	} while(0)


typedef struct serialId {
	uint8_t		name[3];	// name in ASCII "840" = 0x38 0x34 0x30
	uint8_t		majorRev;	// major version 0x01
	uint8_t		minorRev;	// minor version, 0x00
	uint8_t		year;		//0x00 = 2000, 0x01 = 2001....0x0c = 2012
	uint8_t		month;		//0x01=January, .... 0x0a=October, 0x0b=November, 0x0c=December
	uint8_t		factory;	//0x02=Avit
	uint32_t	serial;	// max=999999=0xF423F
} serialId_t;

struct eeprom_data {
	uint32_t	magicNum;		// ="STB1"=0x53 0x54 0x42 0x31
	serialId_t	serialId;
	uint32_t	date;			//Date of soldering in the form unix time (seconds since 1970-01-01 00:00:00 UTC)
	uint8_t		macAddr1[6];
	uint8_t		macAddr2[6];	// It's for future model (not use in current version)
};
/*
typedef enum g_board_type_e {
	eSTB_unknown      = -1,
	eSTB830           = 0,
	eSTB840_PromSvyaz = 1,
	eSTB840_PromWad   = 2,
	eSTB840_ch7162    = 3,
	eSTB830_reference = 4,
	eSTB_pioneer      = 5,
	eSTB850           = 6
} g_board_type_t;
*/
int32_t isValidMagicNum(struct eeprom_data *data)
{
	if(ntohl(data->magicNum) != MAGIC_NUM)
		return 0;
	return 1;
}

static inline __s32 i2c_smbus_access(int32_t file, char read_write, __u8 command, 
                                     int32_t size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args;

	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;
	return ioctl(file,I2C_SMBUS,&args);
}

static inline __s32 i2c_smbus_read_byte_data(int32_t file, __u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_access(file,I2C_SMBUS_READ,command,
	                     I2C_SMBUS_BYTE_DATA,&data))
		return -1;
	else
		return 0x0FF & data.byte;
}

char * getI2CPath()
{
	FILE *fd;
	int boardName = eSTB_unknown;
	if((fd = fopen("/proc/board/id", "r")) == 0) {
		eprintf("ERROR --> Can't get board id.");
		return NULL;
	} 
	fscanf(fd, "%d", &boardName);
	fclose(fd);

	if (boardName == eSTB850){
		return I2C_PREFIX"3";
	}
	if (boardName == eSTB840_PromSvyaz ||
		boardName == eSTB840_PromWad   ||
		boardName == eSTB840_ch7162)
	{
		return I2C_PREFIX"2";
	}
	eprintf("ERROR --> Unsupported board.\n");
	return NULL;
}

int32_t getEepromData()
{
	int32_t		i2c_bus;
	uint32_t	i;
	time_t		date;
	struct eeprom_data	eepromData;
	char				*p;
	char				mac1[32], serial[64], date_str[64];
	int32_t ret;

	i2c_bus = open(I2C_BUS, O_RDWR);
	if(!i2c_bus) {
		eprintf ("ERROR --> Cant open " I2C_BUS);
		return -1;
	}

	if (ioctl(i2c_bus, I2C_SLAVE, EEPROM_ADDR) < 0) {
		if (errno == EBUSY) {
			eprintf("ERROR --> eeprom is busy");
		} else {
			eprintf("ERROR --> Could not set address to 0x%02x: %s",
								EEPROM_ADDR, strerror(errno));
		}
		close(i2c_bus);
		return -1;
	}

	p = (char *)&eepromData;
	for(i = 0; i < sizeof(struct eeprom_data); i++){
		ret = i2c_smbus_read_byte_data(i2c_bus, i);
		if (ret == -1){
			char errMessage[256];
			snprintf (errMessage, 256, "ERROR --> I2C bus or EEPROM is corrupted (%s)", strerror(errno));
			eprintf(errMessage);
			close(i2c_bus);
			return -1;
		}
		p[i] = ret;
	}

	if(!isValidMagicNum(&eepromData)) {
		eprintf ("ERROR --> bad data in eeprom");
		close(i2c_bus);
		return -1;
	}

	p = mac1;
	for(i = 0; i < 6; i++) {
		sprintf(p, "%02x:", eepromData.macAddr1[i]);
		p += 3;
	}
	mac1[17] = 0;
	snprintf(serial, 64, "%c%c%c%01d%01d%02d%02d%01d%06d",
		eepromData.serialId.name[0],
		eepromData.serialId.name[1],
		eepromData.serialId.name[2],
		eepromData.serialId.majorRev,
		eepromData.serialId.minorRev,
		eepromData.serialId.year,
		eepromData.serialId.month,
		eepromData.serialId.factory,
		ntohl(eepromData.serialId.serial));
	date = (time_t)ntohl(eepromData.date);
	strncpy(date_str, ctime(&date), 64);
	date_str[strlen(date_str) - 1] = 0;
	eprintf ("serialId=%s, mac=%s, %s", serial, mac1, date_str);
	close(i2c_bus);
	return 0;
}


int32_t getSerial(char * i2c_path)
{
	int32_t		i2c_bus;
	uint32_t	i;
	struct eeprom_data	eepromData;
	char				*p;
	char				serial[128];
	int32_t ret;

	if (!i2c_path || !strlen(i2c_path)){
		return -1;
	}

	i2c_bus = open(i2c_path, O_RDWR);
	if(!i2c_bus) {
		eprintf("ERROR --> Cant open %s", i2c_path);
		return -1;
	}

	if (ioctl(i2c_bus, I2C_SLAVE, EEPROM_ADDR) < 0) {
		if (errno == EBUSY) {
			eprintf("ERROR --> eeprom is busy");
		} else {
			eprintf ("ERROR --> Could not set address to 0x%02x: %s",
								EEPROM_ADDR, strerror(errno));
		}
		close(i2c_bus);
		return -1;
	}

	p = (char *)&eepromData;
	for(i = 0; i < sizeof(struct eeprom_data); i++){
		ret = i2c_smbus_read_byte_data(i2c_bus, i);
		if (ret == -1){
			char errMessage[256];
			snprintf (errMessage, 256, "ERROR --> I2C bus or EEPROM is corrupted (%s)", strerror(errno));
			eprintf(errMessage);
			close(i2c_bus);
			return -1;
		}
		p[i] = ret;
	}

	if(!isValidMagicNum(&eepromData)) {
		eprintf("ERROR --> bad data in eeprom");
		close(i2c_bus);
		return -1;
	}

	snprintf(serial, 16, "%c%c%c%01d%01d%02d%02d%01d%06d",
		eepromData.serialId.name[0],
		eepromData.serialId.name[1],
		eepromData.serialId.name[2],
		eepromData.serialId.majorRev,
		eepromData.serialId.minorRev,
		eepromData.serialId.year,
		eepromData.serialId.month,
		eepromData.serialId.factory,
		ntohl(eepromData.serialId.serial));

	printf ("%s\n", serial);
	close(i2c_bus);
	return 0;
}
