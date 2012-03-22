
/*
 backend.c

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

#ifdef STB82

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "backend.h"

#include "debug.h"
#include "app_info.h"

#include <phStbGpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

void backend_setup(void)
{
    int gpio;
    char gpioValue;

    gpioValue = '1';
    /* Modify the RGB termination by using the logic attached to GPIO4 */
    gpio = open("/dev/gpio4", O_RDWR);
    if (gpio >= 0)
    {
        int ioctlErr;
        /* Set up GPIO4 in gpio mode */
        if ((ioctlErr = ioctl(gpio, PHSTBGPIO_IOCTL_SET_PIN_MODE, phStbGpio_PinModeGpio)) < 0)
        {
            eprintf("backend: Unable to set up gpio4 in gpio mode\n");
        }
        /* Set up the RGB termination appropriately */
        if (write(gpio, &gpioValue, 1) != 1)
        {
            eprintf("backend: Unable to set gpio4 value\n" );
        }
        close(gpio);
    }
    else
    {
        eprintf("backend: Unable to access /dev/gpio4\n" );
    }

    if ((appControlInfo.outputInfo.encConfig[0].out_signals == DSOS_CVBS) ||
        (appControlInfo.outputInfo.encConfig[0].out_signals == DSOS_YCBCR))
    {
        gpioValue = '0';
    }
    else
    {
        gpioValue = '1';
    }
    /* Modify the RGB termination by using the logic attached to GPIO14 */
    gpio = open("/dev/gpio14", O_RDWR);
    if (gpio >= 0)
    {
        int ioctlErr;
        /* Set up GPIO14 in gpio mode */
        if ((ioctlErr = ioctl(gpio, PHSTBGPIO_IOCTL_SET_PIN_MODE, phStbGpio_PinModeGpio)) < 0)
        {
            eprintf("backend: Unable to set up gpio14 in gpio mode\n");
        }
        /* Set up the RGB termination appropriately */
        if (write(gpio, &gpioValue, 1) != 1)
        {
            eprintf("backend: Unable to set gpio14 value\n" );
        }
        close(gpio);
    }
    else
    {
        eprintf("backend: Unable to access /dev/gpio14\n" );
    }
}

#endif // #ifdef STB82
