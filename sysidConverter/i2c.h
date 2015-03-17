/*
 * Copyright (C) 2012 by Elecard-CTP.
 * Written by Anton Sergeev <Anton.Sergeev@elecard.ru>
 * 
 * i2c header.
 * 
 */
/**
 @File   i2c.h
 @brief
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <linux/fb.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <linux/board_id.h> 

#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#if !(defined __I2C_H__)
#define __I2C_H__

int getEepromData();
char * getBoardName();
int getSerial(char * i2c_path);

#endif //#if !(defined __I2C_H__)
