
/*
 smilparser.cpp

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

#include "smil.h"
#include "debug.h"
#include "xmlconfig.h"
#include <tinyxml.h>

#include <string.h>
#include <stdio.h>

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int smil_parseRTMPStreams(const char *data, char *rtmp_url, size_t url_size)
{
	TiXmlDocument *smil = NULL;
	TiXmlNode *node = NULL;
	TiXmlNode *head = NULL;
	TiXmlNode *body = NULL;
	TiXmlNode *child = NULL;
	TiXmlElement *element = NULL;
	const char *base = NULL;
	const char *stream = NULL;
	char *str;
	size_t base_length = 0, stream_length = 0;

	if( data == NULL || rtmp_url == NULL || url_size < sizeof("rtmp://xx.xx")-1)
	{
		return -2;
	}

	smil = new TiXmlDocument();
	smil->Parse(data);
	if (smil->Error() != 0)
	{
		delete smil;
		return -2;
	}

	node = (TiXmlNode *)xmlConfigGetElement(smil, "smil", 0);
	if( node == NULL )
	{
		delete smil;
		return -1;
	}

	head = (TiXmlNode *)xmlConfigGetElement(node, "head", 0);
	if( head != NULL && head->Type() == TiXmlNode::TINYXML_ELEMENT || head->Type() == TiXmlNode::TINYXML_DOCUMENT )
	{
		for ( child = head->FirstChild(); child != 0; child = child->NextSibling() )
		{
			if (child->Type() == TiXmlNode::TINYXML_ELEMENT && strcmp(child->Value(), "meta") == 0)
			{
				element = (TiXmlElement *)child;
				if((base = element->Attribute("base")) != NULL )
					break;
			}
		}
	}
	body = (TiXmlNode *)xmlConfigGetElement(node, "body", 0);
	if( body != NULL && body->Type() == TiXmlNode::TINYXML_ELEMENT || body->Type() == TiXmlNode::TINYXML_DOCUMENT )
	{
		for ( child = body->FirstChild(); child != 0; child = child->NextSibling() )
		{
			if (child->Type() == TiXmlNode::TINYXML_ELEMENT && strcmp(child->Value(), "video") == 0)
			{
				element = (TiXmlElement *)child;
				if((stream = element->Attribute("src")) != NULL )
					break;
			}
		}
	}
	if( stream == NULL )
	{
		delete smil;
		return -1;
	}
	stream_length = strlen( stream ) + 1;
	if( strncmp(stream, "rtmp://", sizeof("rtmp://")-1 ) == 0 )
	{
		if( stream_length > url_size )
		{
			delete smil;
			return -1;//url_size - stream_length;
		}
		memcpy( rtmp_url, stream, stream_length );
	} else if( base != NULL && strncmp(base, "rtmp://", sizeof("rtmp://")-1 ) == 0 )
	{
		//str = strstr(base, "/_definst_");
		//if( str != NULL )
		//	*str = 0;
		base_length = strlen( base );
		if( stream_length+1+base_length > url_size )
		{
			delete smil;
			return -1;//url_size - (stream_length + base_length + 1);
		}
		sprintf( rtmp_url, "%s/%s", base, stream );
	} else
	{
		delete smil;
		return -1;
	}
	delete smil;
	return 0;
}
