#if !defined(__DVB_INFO_TABLES_H)
#define __DVB_INFO_TABLES_H

/*
 dvb_info_tables.h

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

#include "lookup.h"

/***********************************************
* EXPORTED DATA                                *
************************************************/

static struct lookup_table description_table[] = { 
	{ 0x10, "Movie / Drama" },
	{ 0x11, "Movie - detective/thriller" },
	{ 0x12, "Movie - adventure/western/war" },
	{ 0x13, "Movie - science fiction/fantasy/horror" },
	{ 0x14, "Movie - comedy" },
	{ 0x15, "Movie - soap/melodrama/folkloric" },
	{ 0x16, "Movie - romance" },
	{ 0x17, "Movie - serious/classical/religious/historical movie/drama" },
	{ 0x18, "Movie - adult movie/drama" },

	{ 0x20, "News / Current Affairs" },
	{ 0x21, "News / Weather Report" },
	{ 0x22, "Mews Magazine" },
	{ 0x23, "Documentary" },
	{ 0x24, "Discussion / Interview / Debate" },

	{ 0x30, "Show / Game Show" },
	{ 0x31, "Game show / Quiz / Contest" },
	{ 0x32, "Variety Show" },
	{ 0x33, "Talk Show" },

	{ 0x40, "Sports" },
	{ 0x41, "Special Events (Olympic Games, World Cup etc.)" },
	{ 0x42, "Sports Magazines" },
	{ 0x43, "Football / Soccer" },
	{ 0x44, "Tennis / Squash" },
	{ 0x45, "Team Sports (Excluding Football)" },
	{ 0x46, "Athletics" },
	{ 0x47, "Motor Sport" },
	{ 0x48, "Water Sport" },
	{ 0x49, "Winter Sports" },
	{ 0x4A, "Equestrian" },
	{ 0x4B, "Martial Sports" },

	{ 0x50, "Childrens / Youth" },
	{ 0x51, "Pre-school Children's Programmes" },
	{ 0x52, "Entertainment Programmes for 6 to14" },
	{ 0x53, "Entertainment Programmes for 10 to 16" },
	{ 0x54, "Informational / Educational / School Programmes" },
	{ 0x55, "Cartoons / Puppets" },

	{ 0x60, "Music / Ballet / Dance" },
	{ 0x61, "Rock / Pop" },
	{ 0x62, "Serious Music / Classical Music" },
	{ 0x63, "Folk / Traditional Music" },
	{ 0x64, "Jazz" },
	{ 0x65, "Musical / Opera" },
	{ 0x66, "Ballet" },

	{ 0x70, "Arts / Culture" },
	{ 0x71, "Performing Arts" },
	{ 0x72, "Fine Arts" },
	{ 0x73, "Religion" },
	{ 0x74, "Popular Culture / Traditional Arts" },
	{ 0x75, "Literature" },
	{ 0x76, "Film / Cinema" },
	{ 0x77, "Experimental Film / Video" },
	{ 0x78, "Broadcasting / Press" },
	{ 0x79, "New Media" },
	{ 0x7A, "Arts / Culture Magazines" },
	{ 0x7B, "Fashion" },

	{ 0x80, "Social / Policical / Economics" },
	{ 0x81, "Magazines / Reports / Documentary" },
	{ 0x82, "Economics / Social Advisory" },
	{ 0x83, "Remarkable People" },

	{ 0x90, "Education / Science / Factual" },
	{ 0x91, "Nature / Animals / Environment" },
	{ 0x92, "Technology / Natural Sciences" },
	{ 0x93, "Medicine / Physiology / Psychology" },
	{ 0x94, "Foreign Countries / Expeditions" },
	{ 0x95, "Social / Spiritual Sciences" },
	{ 0x96, "Further Education" },
	{ 0x97, "Languages" },

	{ 0xA0, "Leisure / Hobbies" },
	{ 0xA1, "Tourism / Travel" },
	{ 0xA2, "Handicraft" },
	{ 0xA3, "Motoring" },
	{ 0xA4, "Fitness & Health" },
	{ 0xA5, "Cooking" },
	{ 0xA6, "Advertizement / Shopping" },
	{ 0xA7, "Gardening" },

	// Special
	{ 0xB0, "Original Language" },
	{ 0xB1, "Black & White" },
	{ 0xB2, "Unpublished" },
	{ 0xB3, "Live Broadcast" },

	// UK Freeview custom id
	{ 0xF0, "Drama" },

	{ -1, NULL }	
};

static struct lookup_table aspect_table[] = {
	{0, "4:3"},   // 4/3
	{1, "16:9"},  // 16/9 WITH PAN VECTORS
	{2, "16:9"},  // 16/9 WITHOUT
	{3, "2.21:1"},  // >16/9 or 2.21/1 XMLTV no likey
	{-1, NULL }
};

static struct lookup_table audio_table[] = {
	{0x01, "Mono" },      //single mono
	{0x02, "Mono" },	  //dual mono - stereo??
	{0x03, "Stereo" },
	{0x05, "Surround" },
	{0x04, "X-multilingual"}, // multilingual/channel
	{0x40, "X-visuallyimpared"}, // visual impared sound
	{0x41, "X-hardofhearing"}, // hard hearing sound
	{-1, NULL }
};

#endif /* __DVB_INFO_TABLES_H      Do not add any thing below this line */

