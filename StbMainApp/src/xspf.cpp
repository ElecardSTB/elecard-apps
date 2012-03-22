
/*
 xspf.cpp

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

#include "xspf.h"

#include <common.h>
#include <tinyxml.h>

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int xspf_parseBuffer(const char *data, xspfEntryHandler pCallback, void *pArg)
{
	TiXmlDocument *xspf;
	TiXmlNode *node;
	TiXmlNode *child;
	TiXmlNode *text;
	TiXmlElement *element;
	const char *url, *name, *rel, *descr, *thumb, *idstr;
	char *endstr;

	if( data == NULL || pCallback == NULL )
	{
		eprintf("%s: invalid arguments (data %p, callback %p)\n", __FUNCTION__, data, pCallback);
		return -2;
	}
	xspf = new TiXmlDocument();
	xspf->Parse(data);
	if (xspf->Error() != 0)
	{
		eprintf("%s: XML parse failed: %s\n", __FUNCTION__, xspf->ErrorDesc());
		delete xspf;
		return -2;
	}

	node = (TiXmlNode *)xmlConfigGetElement(xspf, "playlist", 0);
	if( node == NULL )
	{
		eprintf("%s: Failed to find 'playlist' element\n", __FUNCTION__);
		delete xspf;
		return -1;
	}
	node = (TiXmlNode *)xmlConfigGetElement(node, "trackList", 0);
	if( node == NULL )
	{
		eprintf("%s: Failed to find 'trackList' element\n", __FUNCTION__);
		delete xspf;
		return -1;
	}
	if (node->Type() == TiXmlNode::TINYXML_ELEMENT || node->Type() == TiXmlNode::TINYXML_DOCUMENT)
	{
		for ( child = node->FirstChild(); child != 0; child = child->NextSibling() )
		{
			if (child->Type() == TiXmlNode::TINYXML_ELEMENT && strcasecmp(child->Value(), "track") == 0)
			{
				url = xmlConfigGetText(child, "location");
				name = xmlConfigGetText(child, "title");
				element = (TiXmlElement*)child;
				if( url != NULL )
				{
					pCallback(pArg, url, name, (const xmlConfigHandle_t)child);
				}
			}
		}
	}
	delete xspf;
	return 0;
}
