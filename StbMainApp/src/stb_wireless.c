
/*
 stb_wireless.c

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
* LOCAL MACROS                                 *
************************************************/

#include "stb_wireless.h"

#ifdef ENABLE_WIFI

#include "l10n.h"
#include "output.h"

#include <common.h>

#include <iwlib.h>

#include <errno.h>
#include <string.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

// from linux/wireless.h
#define SIOCGIWMODUL    0x8B2F		/* get Modulations settings */
#define IWEVGENIE       0x8C05

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct __wirelessApInfo_t
{
	char   essid[IW_ESSID_MAX_SIZE];
	char   mac[18];
	outputWifiMode_t mode;
	outputWifiAuth_t auth;
	outputWifiEncryption_t encr;
	int    channel;
	double freq;
	int    bitrate;
} wirelessApInfo_t;

/***********************************************
* EXPORTED DATA                                *
************************************************/

interfaceListMenu_t WirelessMenu;

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/

static int wl_skfd = -1;
static pthread_t wl_thread = 0;
static int wl_result_count = 0;
static int wl_selected     = -1;
static wirelessApInfo_t wl_list[IW_MAX_AP];

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static void* wireless_scanThread(void *pArg);
static int wireless_enterMenu(interfaceMenu_t* pMenu, void *pArg);
static int wireless_scanNetworks(interfaceMenu_t* pMenu, void *pArg);
static int wireless_changeNetwork(interfaceMenu_t* pMenu, void *pArg);
static int wireless_setNetwork(interfaceMenu_t* pMenu, void *pArg);
static int wireless_fullscan(void);

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int wireless_buildMenu(interfaceMenu_t *pParent )
{
	createListMenu( &WirelessMenu, _T("WIRELESS_LIST"), thumbnail_search, NULL, pParent, interfaceListMenuIconThumbnail,
		wireless_enterMenu, NULL, NULL
	);
	return 0;
}

static int wireles_initSocket(void)
{
	if( wl_skfd < 0 )
	{
		wl_skfd = iw_sockets_open ();
		if( wl_skfd < 0 )
		{
			eprintf("%s: Failed to initialize wireless sockets: %s\n", __FUNCTION__, strerror(errno));
			return -1;
		}
	}
	return 0;
}

void wireless_cleanupMenu(void )
{
	if( wl_thread != 0 )
	{
		eprintf("%s: waiting for wireless scan thread to finish\n", __FUNCTION__);
		pthread_join( wl_thread, NULL );
	}

	if( wl_skfd >= 0 )
	{
		iw_sockets_close(wl_skfd);
		wl_skfd = -1;
	}

	wl_result_count  = 0;
	wl_selected      = -1;
}

const char* wireless_mode_print( outputWifiMode_t mode )
{
	switch( mode )
	{
		case wifiModeAdHoc:  return "Ad-Hoc";
		case wifiModeMaster: return "AP";
		default:;
	}
	return "Managed";
}

const char* wireless_auth_print( outputWifiAuth_t auth )
{
	switch( auth )
	{
		case wifiAuthOpen:    return "OPEN";
		case wifiAuthWEP:     return "WEP";
		case wifiAuthWPAPSK:  return "WPA-PSK";
		case wifiAuthWPA2PSK: return "WPA2-PSK";
		default:;
	}
	return "unknown";
}

const char* wireless_encr_print( outputWifiEncryption_t encr )
{
	switch( encr )
	{
		case wifiEncTKIP:  return "TKIP";
		case wifiEncAES:   return "CCMP";
		default:;
	}
	return "unknown";
}

void *wireless_scanThread(void *pArg)
{
	interfaceMenu_t* pMenu = pArg;
	int icon = thumbnail_workstation;
	int i;

	wl_result_count = 0;
	int res = wireless_fullscan();

	interface_clearMenuEntries(pMenu);
	interface_addMenuEntry(pMenu, _T("WIRELESS_SCAN"), wireless_scanNetworks, NULL, thumbnail_search);

	if( res < 0 )
	{
		eprintf("%s: Failed to scan wireless networks: %s\n", __FUNCTION__, strerror(errno));
		interface_addMenuEntryDisabled(pMenu, _T("WIRELESS_SCAN_FAILED"), thumbnail_error);
		interface_addMenuEntryDisabled(pMenu, _T("SETTINGS_APPLY_REQUIRED"), thumbnail_info);
		goto thread_exit;
	}

	for( i = 0; i < res; i++ )
	{
		if( wl_list[i].mode >= wifiModeCount )
		{
			eprintf("  [%2d]: network %s has invalid mode %d\n", i, wl_list[i].essid, wl_list[i].mode);
			continue;
		}
		if( wl_list[i].auth >= wifiAuthCount )
		{
			eprintf("  [%2d]: network %s has invalid auth %d\n", i, wl_list[i].essid, wl_list[i].auth);
			continue;
		}
		if( wl_list[i].encr >= wifiEncCount )
		{
			eprintf("  [%2d]: network %s has invalid encryption %d\n", i, wl_list[i].essid, wl_list[i].encr);
			continue;
		}

		wl_result_count++;
		eprintf("  [%2d](%c): ch.%2d %s %s %s\n", i,
			wl_list[i].mode == wifiModeAdHoc ? 'A' : 'M',
			wl_list[i].channel,
			wl_list[i].essid,
			wireless_auth_print(wl_list[i].auth),
			wl_list[i].auth <= wifiAuthWEP ? "" : wireless_encr_print(wl_list[i].encr) );

		icon = wl_list[i].mode == wifiModeAdHoc ? thumbnail_workstation : (wl_list[i].auth == wifiAuthOpen ? thumbnail_internet : thumbnail_billed);
		interface_addMenuEntry( pMenu, wl_list[i].essid, wireless_changeNetwork, (void*)i, icon );
	}
	if( wl_result_count == 0 )
	{
		interface_addMenuEntryDisabled(pMenu, _T("WIRELESS_SCAN_NO_RESULTS"), thumbnail_error);
		goto thread_exit;
	}

thread_exit:
	dprintf("%s: scan finished (%d)\n", __FUNCTION__, wl_result_count);
	wl_thread = 0;
	if( interfaceInfo.currentMenu == _M &WirelessMenu )
	{
		interface_displayMenu(1);
	}
	return NULL;
}

int wireless_enterMenu(interfaceMenu_t* pMenu, void *pArg)
{
	if( wl_result_count == 0 )
	{
		return wireless_scanNetworks(pMenu, pArg);
	}
	return 0;
}

int wireless_scanNetworks(interfaceMenu_t* pMenu, void *pArg)
{
	if( wl_thread == 0 )
	{
		interface_clearMenuEntries(pMenu);
		wl_result_count = 0;

		if( wireles_initSocket() < 0 )
		{
			interface_showMessageBox( _T("WIRELESS_SCAN_FAILED"), thumbnail_error, 5000 );
			return -1;
		}

		if( pthread_create( &wl_thread, NULL, wireless_scanThread, pMenu ) < 0 )
		{
			eprintf("%s: Failed to create scan thread: %s\n", __FUNCTION__, strerror(errno));
			interface_showMessageBox( _T("WIRELESS_SCAN_FAILED"), thumbnail_error, 5000 );
			return -1;
		}

		interface_addMenuEntry(pMenu, _T("WIRELESS_SCAN"), wireless_scanNetworks, NULL, thumbnail_search);
		interface_addMenuEntryDisabled(pMenu, _T("WIRELESS_SCAN_IN_PROGRESS"), thumbnail_info);
		interface_displayMenu(1);
	} else
		eprintf("%s: wifi scanning is still in process\n", __FUNCTION__);

	return 0;
}

int wireless_changeNetwork(interfaceMenu_t* pMenu, void *pArg)
{
	wl_selected = (int)pArg;

	wireless_setNetwork(pMenu, pArg);
	interface_menuActionShowMenu( pMenu, &WifiSubMenu );
	if( wl_list[wl_selected].auth != wifiAuthOpen )
	{
		output_changeWifiKey(_M &WifiSubMenu, NULL);
	}

	return 0;
}

int wireless_setNetwork(interfaceMenu_t* pMenu, void *pArg)
{
	output_setESSID         (_M &WifiSubMenu,        wl_list[wl_selected].essid, (void*)ifaceWireless);
	output_setAuthMode      (_M &WifiSubMenu, (void*)wl_list[wl_selected].auth);
	if (wl_list[wl_selected].auth > wifiAuthWEP)
	{
		// We always must call output_changeWifiEncryption only after output_changeAuthMode as it can reset encryption!
		output_setWifiEncryption(_M &WifiSubMenu, (void*)wl_list[wl_selected].encr);
	}
	output_setWifiMode      (_M &WifiSubMenu, (void*)wl_list[wl_selected].mode);
	return 0;
}

#ifdef DEBUG
#define IW_ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))

/* Values for the IW_IE_CIPHER_* in GENIE */
static const char *	iw_ie_cypher_name[] = {
	"none",
	"WEP-40",
	"TKIP",
	"WRAP",
	"CCMP",
	"WEP-104",
};
#define	IW_IE_CYPHER_NUM	IW_ARRAY_LEN(iw_ie_cypher_name)

/* Values for the IW_IE_KEY_MGMT_* in GENIE */
static const char *	iw_ie_key_mgmt_name[] = {
	"none",
	"802.1x",
	"PSK",
};
#define	IW_IE_KEY_MGMT_NUM	IW_ARRAY_LEN(iw_ie_key_mgmt_name)

static void
iw_print_value_name(unsigned int		value,
		    const char *		names[],
		    const unsigned int		num_names)
{
  if(value >= num_names)
    dprintf(" unknown (%d)", value);
  else
    dprintf(" %s", names[value]);
}
#else
#define iw_print_value_name(x...)
#endif

static inline void parse_ie_wpa(unsigned char *iebuf, int buflen, int ap_num)
{
	int			ielen = iebuf[1] + 2;
	int			offset = 2;	/* Skip the IE id, and the length. */
	const unsigned char		wpa1_oui[3] = {0x00, 0x50, 0xf2};
	const unsigned char		wpa2_oui[3] = {0x00, 0x0f, 0xac};
	const unsigned char *	wpa_oui;
	int			i;
	uint16_t		ver = 0;
	uint16_t		cnt = 0;

	if (ielen > buflen)
		ielen = buflen;

	switch (iebuf[0])
	{
		case 0x30:		/* WPA2 */
			/* Check if we have enough data */
			if (ielen < 4)
			{
				dprintf("                        IE 0x30 doesn't have enough data\n");
				return;
			}

			wpa_oui = wpa2_oui;
			break;

		case 0xdd:		/* WPA or else */
			wpa_oui = wpa1_oui;

			/* Not all IEs that start with 0xdd are WPA.
			 * So check that the OUI is valid. Note : offset==2 */
			if ((ielen < 8)
				|| (memcmp(&iebuf[offset], wpa_oui, 3) != 0)
				|| (iebuf[offset + 3] != 0x01))
			{
				dprintf("                        Unknown IE 0xdd\n");
				return;
			}

			/* Skip the OUI type */
			offset += 4;
			break;

		default:
			return;
	}

	/* Pick version number (little endian) */
	ver = iebuf[offset] | (iebuf[offset + 1] << 8);
	(void)ver;

	offset += 2;

	if (iebuf[0] == 0xdd)
	{
		dprintf("WPA Version %d\n", ver);
		wl_list[ap_num].auth = wifiAuthWPAPSK;
	} else
	if (iebuf[0] == 0x30)
	{
		dprintf("IEEE 802.11i/WPA2 Version %d\n", ver);
		wl_list[ap_num].auth = wifiAuthWPA2PSK;
	}
	else
		eprintf("  %s failed to determine WPA version: 0x%02x\n", wl_list[ap_num].essid, iebuf[0]);

	/* From here, everything is technically optional. */

	/* Check if we are done */
	if (ielen < (offset + 4))
	{
		/* We have a short IE.  So we should assume TKIP/TKIP. */
		dprintf("                        Group Cipher : TKIP\n");
		dprintf("                        Pairwise Cipher : TKIP\n");
		wl_list[ap_num].encr = wifiEncTKIP;
		return;
	}

	/* Next we have our group cipher. */
	if (memcmp(&iebuf[offset], wpa_oui, 3) != 0)
	{
		dprintf("                        Group Cipher : Proprietary\n");
		wl_list[ap_num].auth = wifiAuthCount;
		wl_list[ap_num].encr = wifiEncCount;
	}
	else
	{
		dprintf("                        Group Cipher :");
		switch(iebuf[offset+3])
		{
			case 0:
				wl_list[ap_num].auth = wifiAuthOpen;
				break;
			case 1:
			case 5:
				wl_list[ap_num].auth = wifiAuthWEP;
				break;
			case 2:
				wl_list[ap_num].encr = wifiEncTKIP;
				break;
			case 4: // CCMP
				wl_list[ap_num].encr = wifiEncAES;
				break;
			default:
				wl_list[ap_num].encr = wifiEncCount;
		}
		iw_print_value_name(iebuf[offset+3], iw_ie_cypher_name, IW_IE_CYPHER_NUM);
		dprintf("\n");
	}
	offset += 4;

	/* Check if we are done */
	if (ielen < (offset + 2))
	{
		/* We don't have a pairwise cipher, or auth method. Assume TKIP. */
		dprintf("                        Pairwise Ciphers : TKIP\n");
		wl_list[ap_num].encr = wifiEncTKIP;
		return;
	}

	/* Otherwise, we have some number of pairwise ciphers. */
	cnt = iebuf[offset] | (iebuf[offset + 1] << 8);
	offset += 2;
	dprintf("                        Pairwise Ciphers (%d) :", cnt);

	if (ielen < (offset + 4*cnt))
		return;

	for (i = 0; i < cnt; i++)
	{
		if (memcmp(&iebuf[offset], wpa_oui, 3) != 0)
		{
			dprintf(" Proprietary");
		}
		else
		{
			iw_print_value_name(iebuf[offset+3],
				iw_ie_cypher_name, IW_IE_CYPHER_NUM);
		}
		offset+=4;
	}
    dprintf("\n");

	/* Check if we are done */
	if (ielen < (offset + 2))
		return;

	/* Now, we have authentication suites. */
	cnt = iebuf[offset] | (iebuf[offset + 1] << 8);
	offset += 2;
	dprintf("                        Authentication Suites (%d) :", cnt);

	if (ielen < (offset + 4*cnt))
		return;

	for (i = 0; i < cnt; i++)
	{
		if (memcmp(&iebuf[offset], wpa_oui, 3) != 0)
		{
			dprintf(" Proprietary");
		}
		else
		{
			iw_print_value_name(iebuf[offset+3], iw_ie_key_mgmt_name, IW_IE_KEY_MGMT_NUM);
		}
		offset+=4;
	}
	dprintf("\n");

	/* Check if we are done */
	if (ielen < (offset + 1))
		return;

	/* Otherwise, we have capabilities bytes.
	 * For now, we only care about preauth which is in bit position 1 of the
	 * first byte.  (But, preauth with WPA version 1 isn't supposed to be
	 * allowed.) 8-) */
	if (iebuf[offset] & 0x01)
	{
		dprintf("                       Preauthentication Supported\n");
	}
}

static inline void parse_gen_ie(unsigned char *buffer, int buflen, int ap_num)
{
	int offset = 0;

	/* Loop on each IE, each IE is minimum 2 bytes */
	while (offset <= (buflen - 2))
	{
		dprintf("                    IE: ");

		/* Check IE type */
		switch (buffer[offset])
		{
			case 0xdd:	/* WPA1 (and other) */
			case 0x30:	/* WPA2 */
				parse_ie_wpa(buffer + offset, buflen, ap_num);
				break;
			default:
				dprintf("unknown ie 0x%02x\n", buffer[offset]);
				//iw_print_ie_unknown(buffer + offset, buflen);
		}
        /* Skip over this IE to the next one in the list. */
		offset += buffer[offset+1] + 2;
	}
}

static inline void process_scanning_token(
	struct stream_descr *stream,    /* Stream of events */
	struct iw_event     *event,     /* Extracted token */
	int                 *ap_num,
	struct iw_range     *iw_range,  /* Range info */
	int                  has_range)
{
#ifdef DEBUG
	char		buffer[128];	/* Temporary buffer */
#endif
	static int val_index = 0;

	/* Now, let's decode the event */
	switch (event->cmd)
	{
		case SIOCGIWAP:
			(*ap_num)++;
			memset( &wl_list[*ap_num], 0, sizeof(wl_list[0]) );
			iw_saether_ntop(&event->u.ap_addr, wl_list[*ap_num].mac);
			dprintf("          Cell %02d - Address: %s\n", *ap_num,
				   iw_saether_ntop(&event->u.ap_addr, buffer));
			break;
		case SIOCGIWFREQ:
			wl_list[*ap_num].channel = -1;
			wl_list[*ap_num].freq = iw_freq2float(&(event->u.freq));
			if (has_range)
				wl_list[*ap_num].channel = iw_freq_to_channel(wl_list[*ap_num].freq, iw_range);
#ifdef DEBUG
			iw_print_freq(buffer, sizeof(buffer),
						  wl_list[*ap_num].freq, wl_list[*ap_num].channel, event->u.freq.flags);
			dprintf("                    %s\n", buffer);
#endif
		    break;
		case SIOCGIWMODE:
			switch( event->u.mode )
			{
				case IW_MODE_ADHOC:
					wl_list[*ap_num].mode = wifiModeAdHoc;
					if( wl_list[*ap_num].auth == 0 ) // uninitialized?
						wl_list[*ap_num].auth = wifiAuthWEP;
					break;
				case IW_MODE_INFRA:
				case IW_MODE_MASTER:
					wl_list[*ap_num].mode = wifiModeManaged;
					break;
				default:
					wl_list[*ap_num].mode = wifiModeCount;
			}
#ifdef DEBUG
			/* Note : event->u.mode is unsigned, no need to check <= 0 */
			if (event->u.mode >= IW_NUM_OPER_MODE)
				event->u.mode =  IW_NUM_OPER_MODE;
			dprintf("                    Mode:%s\n",
				iw_operation_mode[event->u.mode]);
#endif
			break;
		case SIOCGIWESSID:
			// memset( wl_list[*ap_num].essid, 0, sizeof(wl_list[0].essid) )
			if ((event->u.essid.pointer) && (event->u.essid.length))
				memcpy(wl_list[*ap_num].essid, event->u.essid.pointer, event->u.essid.length);
#ifdef DEBUG
			if (event->u.essid.flags)
			{
				/* Does it have an ESSID index ? */
				if ((event->u.essid.flags & IW_ENCODE_INDEX) > 1)
					dprintf("                    ESSID:\"%s\" [%d]\n", wl_list[*ap_num].essid,
						   (event->u.essid.flags & IW_ENCODE_INDEX));
				else
					dprintf("                    ESSID:\"%s\"\n", wl_list[*ap_num].essid);
			} else
				dprintf("                    ESSID:off/any/hidden\n");
#endif
		    break;
		case SIOCGIWENCODE:
		{
#ifdef DEBUG
			unsigned char	key[IW_ENCODING_TOKEN_MAX];
			if (event->u.data.pointer)
				memcpy(key, event->u.data.pointer, event->u.data.length);
			else
				event->u.data.flags |= IW_ENCODE_NOKEY;
			dprintf("                    Encryption key:");
#endif
			if (event->u.data.flags & IW_ENCODE_DISABLED)
			{
				dprintf("off\n");
				wl_list[*ap_num].auth = wifiAuthOpen;
			}
#ifdef DEBUG
			else
			{
				/* Display the key */
				iw_print_key(buffer, sizeof(buffer), key, event->u.data.length,
							 event->u.data.flags);
				dprintf("%s", buffer);

				/* Other info... */
				if ((event->u.data.flags & IW_ENCODE_INDEX) > 1)
					dprintf(" [%d]", event->u.data.flags & IW_ENCODE_INDEX);
				if (event->u.data.flags & IW_ENCODE_RESTRICTED)
					dprintf("   Security mode:restricted");
				if (event->u.data.flags & IW_ENCODE_OPEN)
					dprintf("   Security mode:open");
				dprintf("\n");
			}
#endif
		}
		    break;
		case SIOCGIWRATE:
			if (val_index == 0)
			{
				dprintf("                    Bit Rates:");
				/* We should use only first declared value */
				wl_list[*ap_num].bitrate = event->u.bitrate.value;
			}
#ifdef DEBUG
			else
				if ((val_index % 5) == 0)
					dprintf("\n                              ");
				else
					dprintf("; ");
			iw_print_bitrate(buffer, sizeof(buffer), event->u.bitrate.value);
			dprintf("%s", buffer);
#endif
			/* Check for termination */
			if (stream->value == NULL)
			{
				dprintf("\n");
				val_index = 0;
			}
	        else
				val_index++;
			break;
		case IWEVGENIE:
			/* Informations Elements are complex, let's do only some of them */
			parse_gen_ie(event->u.data.pointer, event->u.data.length, *ap_num);
			break;
#ifdef DEBUG
		case SIOCGIWNWID:
			if (event->u.nwid.disabled)
				dprintf("                    NWID:off/any\n");
			else
				dprintf("                    NWID:%X\n", event->u.nwid.value);
			break;
		case SIOCGIWNAME:
			dprintf("                    Protocol:%-1.16s\n", event->u.name);
			break;
		case SIOCGIWMODUL:
		{
			unsigned int	modul = event->u.param.value;
			int		i;
			int		n = 0;
			dprintf("                    Modulations :");
			for (i = 0; i < IW_SIZE_MODUL_LIST; i++)
			{
				if ((modul & iw_modul_list[i].mask) == iw_modul_list[i].mask)
				{
					if ((n++ % 8) == 7)
						dprintf("\n                        ");
					else
						dprintf(" ; ");
					dprintf("%s", iw_modul_list[i].cmd);
				}
			}
			dprintf("\n");
		}
		    break;
		case IWEVQUAL:
			iw_print_stats(buffer, sizeof(buffer),
						   &event->u.qual, iw_range, has_range);
			dprintf("                    %s\n", buffer);
			break;
		case IWEVCUSTOM:
		{
			char custom[IW_CUSTOM_MAX+1];
			if ((event->u.data.pointer) && (event->u.data.length))
				memcpy(custom, event->u.data.pointer, event->u.data.length);
			custom[event->u.data.length] = '\0';
			printf("                    Extra:%s\n", custom);
		}
		    break;
#endif // DEBUG
		default:
			dprintf("                    (Unknown Wireless Token 0x%04X)\n", event->cmd)
			;
	}	/* switch(event->cmd) */
}

int wireless_fullscan(void)
{
	struct timeval tv;             /* Select timeout */
	int	timeout = 15000000;        /* 15s */
	struct iwreq wrq;
	unsigned char *buffer = NULL;  /* Results */
	int buflen = IW_SCAN_MAX_DATA; /* Min for compat WE<17 */
	const char *ifname = "wlan0";

	system("ifconfig wlan0 up");

	struct iw_range	range;
	int has_range;
	/* Get range stuff */
	has_range = (iw_get_range_info(wl_skfd, ifname, &range) >= 0);

#ifdef DEBUG
	/* Check if the interface could support scanning. */
	if((!has_range) || (range.we_version_compiled < 14))
	{
		eprintf("%s: %-8.16s  Interface doesn't support scanning.\n",
			__FUNCTION__, ifname);
		return(-1);
	}
#endif

	/* Init timeout value -> 250ms between set and first get */
	tv.tv_sec = 0;
	tv.tv_usec = 250000;

	wrq.u.data.pointer = NULL;
	wrq.u.data.flags = 0;
	wrq.u.data.length = 0;

	if(iw_set_ext(wl_skfd, ifname, SIOCSIWSCAN, &wrq) < 0)
	{
		if((errno != EPERM))
		{
			eprintf("%s: %-8.16s  Interface doesn't support scanning : %s\n",
				__FUNCTION__, ifname, strerror(errno));
			return(-1);
		}
	}
	timeout -= tv.tv_usec;

	/* Forever */
	while(1)
	{
		fd_set		rfds;		/* File descriptors for select */
		int		last_fd;	/* Last fd */
		int		ret;

		/* Guess what ? We must re-generate rfds each time */
		FD_ZERO(&rfds);
		last_fd = -1;

		/* In here, add the rtnetlink fd in the list */

		/* Wait until something happens */
		ret = select(last_fd + 1, &rfds, NULL, NULL, &tv);

		/* Check if there was an error */
		if(ret < 0)
		{
			if(errno == EAGAIN || errno == EINTR)
				continue;
			eprintf("%s: Unhandled signal %d - exiting...\n", __FUNCTION__, errno);
			return(-1);
		}

		/* Check if there was a timeout */
		if(ret == 0)
		{
			unsigned char *	newbuf;
realloc:
			/* (Re)allocate the buffer - realloc(NULL, len) == malloc(len) */
			newbuf = realloc(buffer, buflen);
			if(newbuf == NULL)
			{
				if(buffer)
					free(buffer);
				eprintf("%s: failed to allocate %d bytes\n", __FUNCTION__, buflen);
				return(-1);
			}
			buffer = newbuf;

			/* Try to read the results */
			wrq.u.data.pointer = (void*)buffer;
			wrq.u.data.flags = 0;
			wrq.u.data.length = buflen;
			if(iw_get_ext(wl_skfd, ifname, SIOCGIWSCAN, &wrq) < 0)
			{
				/* Check if buffer was too small (WE-17 only) */
				if(errno == E2BIG)
				{
					  /* Some driver may return very large scan results, either
					   * because there are many cells, or because they have many
					   * large elements in cells (like IWEVCUSTOM). Most will
					   * only need the regular sized buffer. We now use a dynamic
					   * allocation of the buffer to satisfy everybody. Of course,
					   * as we don't know in advance the size of the array, we try
					   * various increasing sizes. Jean II */

					/* Check if the driver gave us any hints. */
					if(wrq.u.data.length > buflen)
						buflen = wrq.u.data.length;
					else
						buflen *= 2;

					/* Try again */
					goto realloc;
				}

				/* Check if results not available yet */
				if(errno == EAGAIN)
				{
					/* Restart timer for only 100ms*/
					tv.tv_sec = 0;
					tv.tv_usec = 100000;
					timeout -= tv.tv_usec;
					if(timeout > 0)
						continue;	/* Try again later */
				}
				/* Bad error */
				free(buffer);
				eprintf("%s: %-8.16s  Failed to read scan data : %s\n",
					__FUNCTION__, ifname, strerror(errno));
				return(-2);
			} else
				/* We have the results, go to process them */
				break;
		}
		/* In here, check if event and event type
		 * if scan event, read results. All errors bad & no reset timeout */
	}

	int ap_index = -1;
	if(wrq.u.data.length)
	{
		struct iw_event		iwe;
		struct stream_descr	stream;
		int ret;

		iw_init_event_stream(&stream, (char *) buffer, wrq.u.data.length);

		do
		{
			/* Extract an event and print it */
			ret = iw_extract_event_stream(&stream, &iwe, range.we_version_compiled);
			if(ret > 0)
				process_scanning_token(&stream, &iwe, &ap_index, &range, has_range);
		} while(ret > 0);
		ap_index++;
		eprintf("%s: Scan completed: %d networks found\n", __FUNCTION__, ap_index);
	} else
		eprintf("%s: %-8.16s  No scan results\n", __FUNCTION__, ifname);
	free(buffer);
	return ap_index;
}

size_t os_strlcpy(char *dest, const char *src, size_t siz)
{
	const char *s = src;
	size_t left = siz;

	if (left) {
		/* Copy string up to the maximum size of the dest buffer */
		while (--left != 0) {
			if ((*dest++ = *s++) == '\0')
				break;
		}
	}

	if (left == 0) {
		/* Not enough room for the string; force NUL-termination */
		if (siz != 0)
			*dest = '\0';
		while (*s++)
			; /* determine total src string length */
	}

	return s - src - 1;
}

#endif // ENABLE_WIFI
