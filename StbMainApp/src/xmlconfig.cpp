
/*
 xmlconfig.cpp

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

#include "xmlconfig.h"
#include <tinyxml.h>

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

xmlConfigHandle_t xmlConfigOpen(const char *path)
{
	TiXmlDocument *config = new TiXmlDocument(path);
	if (config->LoadFile(TIXML_DEFAULT_ENCODING) != true)
	{
		delete config;
		return NULL;
	}

	return config;
}

xmlConfigHandle_t xmlConfigParse(const char *data)
{
	TiXmlDocument *config = new TiXmlDocument();
	config->Parse(data);

	if (config->Error() != 0)
	{
		delete config;
		return NULL;
	}

	return config;
}

xmlConfigHandle_t xmlConfigGetElement(xmlConfigHandle_t parent, const char *name, int index)
{
	TiXmlNode *node = (TiXmlNode*)parent;
	TiXmlNode *child;
	int count = 0;

	if (node->Type() == TiXmlNode::TINYXML_ELEMENT || node->Type() == TiXmlNode::TINYXML_DOCUMENT)
	{
		for ( child = node->FirstChild(); child != 0; child = child->NextSibling() )
		{
			if (child->Type() == TiXmlNode::TINYXML_ELEMENT && strcasecmp(child->Value(), name) == 0)
			{
				if (count == index)
				{
					return child;
				}
				count++;
			}
		}
	}

	return NULL;
}

xmlConfigHandle_t xmlConfigGetNextElement(xmlConfigHandle_t element, const char *name)
{
	TiXmlNode *node = (TiXmlNode*)element;
	TiXmlNode *child;
	int count = 0;

	if (node->Type() == TiXmlNode::TINYXML_ELEMENT || node->Type() == TiXmlNode::TINYXML_DOCUMENT)
	{
		for ( node = node->NextSibling(); node != 0; node = node->NextSibling() )
		{
			if (node->Type() == TiXmlNode::TINYXML_ELEMENT && strcasecmp(node->Value(), name) == 0)
			{
				return node;
			}
		}
	}

	return NULL;
}

const char *xmlConfigGetText(xmlConfigHandle_t parent, const char *name)
{
	TiXmlNode *node = (TiXmlNode*)parent;
	TiXmlNode *child, *text;

	if (node->Type() == TiXmlNode::TINYXML_ELEMENT)
	{
		/* Find element with specified name */
		for ( child = node->FirstChild(); child != 0; child = child->NextSibling() )
		{
			if (child->Type() == TiXmlNode::TINYXML_ELEMENT && strcasecmp(child->Value(), name) == 0)
			{
				/* Find text node inside found element */
				for ( text = child->FirstChild(); text != 0; text = text->NextSibling() )
				{
					if (text->Type() == TiXmlNode::TINYXML_TEXT)
					{
						return text->ToText()->Value();
					}
				}
				/* In case there's no text - return empty string */
				return "";
			}
		}
	}

	return NULL;
}

const char *xmlConfigGetElementText(xmlConfigHandle_t element)
{
	TiXmlNode *node = (TiXmlNode*)element;
	TiXmlNode *text;

	if (node->Type() == TiXmlNode::TINYXML_ELEMENT)
	{
		/* Find text node inside found element */
		for ( text = node->FirstChild(); text != 0; text = text->NextSibling() )
		{
			if (text->Type() == TiXmlNode::TINYXML_TEXT)
			{
				return text->ToText()->Value();
			}
		}
		/* In case there's no text - return empty string */
		return "";
	}

	return NULL;
}

const char *xmlConfigGetAttribute(xmlConfigHandle_t parent, const char *name)
{
	TiXmlElement *node = (TiXmlElement*)parent;
	TiXmlAttribute* attrib;

	if (node->Type() == TiXmlNode::TINYXML_ELEMENT)
	{
		/* Find element with specified name */
		for ( attrib = node->FirstAttribute(); attrib != 0; attrib = attrib->Next() )
		{
			if (strcasecmp(attrib->Name(), name) == 0)
			{
				return attrib->Value();
			}
		}
	}

	return NULL;
}

void xmlConfigClose(xmlConfigHandle_t config)
{
	if (config != NULL)
	{
		delete (TiXmlDocument *)config;
	}
}
