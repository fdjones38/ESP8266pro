// ESP8266pro
// This software distributed under the terms of the MIT license
// (c) Skurydin Alexey, 2014

#include "ESP8266pro_Parser.h"

ESP8266pro_Parser::ESP8266pro_Parser(Stream &espStreamReference)
	: espStream(espStreamReference), debugStream(NULL)
{
	state = ePS_NotInitialized;
}

ESP8266pro_Parser::ESP8266pro_Parser(Stream &espStreamReference, Stream &debugStreamReference)
	: espStream(espStreamReference), debugStream(&debugStreamReference)
{
	state = ePS_NotInitialized;
}

ESP8266pro_Parser::~ESP8266pro_Parser()
{
}

bool ESP8266pro_Parser::initializeParser(OutputDebugMode debugOutMode/* = eODM_Data*/)
{
#ifndef NODEBUG
	debugMode = debugOutMode;
#endif
	if (!execute(debugOutMode == eODM_FullDump ? "ATE1" : "ATE0")) // Enable/Disable echo
		return false;
	state = ePS_Ready;
	return execute("AT");
}

void ESP8266pro_Parser::restart()
{
	execute("AT+RST");
	
	delay(3500);
	while (espStream.available())
	{
		char dbg[] = {0, 0};
		dbg[0] = espStream.read();
		
		#ifndef NODEBUG
		if ((debugMode & eODM_Dump) == eODM_Dump)
			debugPrint(dbg, false);
		#endif
		if (!espStream.available())
			delay(180);
	}
	
#ifndef NODEBUG
	initializeParser(debugMode);
#else
	initializeParser(eODM_None);
#endif
	
	#ifndef NODEBUG
	if ((debugMode & eODM_Dump) == eODM_Dump)
		debugPrint("\r\n");
	#endif
}

ProcessingState ESP8266pro_Parser::getState()
{
	return state;
}

bool ESP8266pro_Parser::execute(const String& msg, CommandExecutionMode mode/* = eCEM_DeafaultWithSelector*/)
{
	resetParser();
	
	// First selector start
	int splt = msg.indexOf("+");
	if (splt == -1)
		splt = 0;
	else
		splt++;
	String selector = "";
	for (int i = splt + 1; true; i++)
	{
		if (!isalpha(msg[i]))
		{
			selector = msg.substring(splt, i);
			break;
		}
		else if (i == msg.length() - 1)
		{
			selector = msg.substring(splt);
			break;
		}
	}
	
	#ifndef NODEBUG
	if (debugMode == eODM_Data || debugMode == eODM_Dump)
		debugPrint(msg);
	#endif
	
	espStream.print(msg);
	if (mode != eCEM_NoLineBreak)
		espStream.print(ESP_LINE_WRAPPER);
	delay(3);
	
	int timeOut = 1800;
	if (mode == eCEM_ShortTimeOut)
		timeOut = 1100;
	else if (mode == eCEM_LongTimeOut)
		timeOut = 7500;
	
	if (mode != eCEM_NoResponse)
		parseResponse(selector.c_str(), timeOut);
	else
		state = ePS_OK;

	/*debugPrint("RESP0: " + getLine(0));
	debugPrint("RESP1: " + getLine(1));
	
	debugPrint("RESP00: " + getLineItem(0, 0));
	debugPrint("RESP10: " + getLineItem(1, 0));
	debugPrint("RESP11: " + getLineItem(1, 1));
	debugPrint("RESP04: " + getLineItem(0, 4));*/
	
	return state == ePS_OK || state == ePS_Completed;
}

void ESP8266pro_Parser::resetParser()
{
	response = "";
	state = ePS_Ready;
}

String ESP8266pro_Parser::getLine(uint8_t lineId)
{
	uint8_t line = 0;
	int start = 0;
	
	for (int i = 0; i < response.length(); i++)
	{
		if (response[i] == '\n')
		{
			if (line == lineId)
			{
				return trimResponse(response.substring(start, i));
			}
			line++;
			start = i + 1;
		}
	}
	
	return "";
}

String ESP8266pro_Parser::getLineItem(uint8_t lineId, uint8_t itemId)
{
	String line = getLine(lineId);
	line += ","; // to simplify parsing loop breaking
	
	uint8_t item = 0;
	int start = 0;
	bool insideItem = false;
	
	for (int i = 0; i < line.length(); i++)
	{
		if (line[i] == '\"')
			insideItem = !insideItem;
		if (line[i] == ',' && !insideItem)
		{
			if (item == itemId)
			{
				return trimResponse(line.substring(start, i));
			}
			item++;
			start = i + 1;
		}
	}
	
	return "";
}

int ESP8266pro_Parser::getLinesCount()
{
	int k = 0;
	for (int i = 0; i < response.length(); i++)
		if (response[i] == '\n')
			k++;

	return k;
}

bool ESP8266pro_Parser::connectionDataReceive(bool waitData/* = false*/)
{
	if (!waitData && espStream.available() < 5)
		return false;
	
	if (!espStream.find(ESP_IP_DATA_HANDLER))
		return false;
	
	processConnectionDataReceive();
	return true;
}

void ESP8266pro_Parser::processConnectionDataReceive(bool processData/* = true*/)
{
	String sid = espStream.readStringUntil(',');
	String slen = espStream.readStringUntil(':');
	int id = sid.toInt();
	int len = slen.toInt();
	
#ifndef NODEBUG
	if (debugMode != eODM_None)
	{
		debugPrint("<IP data receive #", false);
		debugPrint(sid, false);
		debugPrint(" size=", false);
		debugPrint(slen, false);
		debugPrint("/>", true);
	}
#endif
	
	onDataReceive(id, NULL, 0, eDRA_Begin);
	char *buffer = (char*)malloc(ESP_RECEIVE_BUFSIZE + 1);
	
	unsigned char repeats = 0;
	
	while (len > 0 && repeats < 2)
	{
		int k = min(len, ESP_RECEIVE_BUFSIZE);
		size_t readed = espStream.readBytes(buffer, k);
		if (readed == 0)
		{
			repeats++;
			continue;
		}
		else
			repeats = 0;
		
		buffer[readed] = 0;
		len -= readed;
		
#ifndef NODEBUG
		if ((debugMode & eODM_Dump) == eODM_Dump)
			debugPrint(buffer, false);
#endif
		
		if (processData)
			onDataReceive(id, buffer, readed, (len == 0 ? eDRA_End : eDRA_Packet));
	}
	
	// We don't receive full data
	if (len > 0)
	{
		// May be enlarge buffer sizes? =)
#ifndef NODEBUG
		debugPrint("\r\n<Data lost>");
#endif
		onDataReceive(id, "", 0, eDRA_End); // Force complete request processing if data corrupted
	}
	
#ifndef NODEBUG
	debugPrint("\r\n</IP data receive>");
#endif
	
	free(buffer);
	buffer = NULL;
}

void ESP8266pro_Parser::onDataReceive(uint8_t connectionId, char* buffer, int length, DataReceiveAction action)
{
}

String ESP8266pro_Parser::trimResponse(String originalLine)
{
	originalLine.trim();
	unsigned char quotesCount = 0;
	unsigned char bracketsCount = 0;
	const char* cstr = originalLine.c_str();
	const char* cend;

	for (const char* p = cstr; *p; p++)
	{
		if (*p == '\"')
			quotesCount++;
		if (*p == '(' || *p == ')')
			bracketsCount++;
		cend = p;
	}
	
	if ( (*cstr == '(' && *cend == ')' && bracketsCount == 2) || (*cstr == '\"' && *cend == '\"' && quotesCount == 2) )
		return trimResponse(originalLine.substring(1, originalLine.length()-1));
	else
		return originalLine;
}

void ESP8266pro_Parser::writeString(const __FlashStringHelper* data)
{
	espStream.print(data);
}

bool ESP8266pro_Parser::parseResponse(const char* selector, int msTimeOut/* = 1500*/)
{
	//String selectorMsg = (String)"+" + selector + ":";
	int sellen = strlen(selector);
	resetParser();
	
	unsigned long startMillis = millis();
	while (millis() - startMillis < msTimeOut)
	{
		String line = readLine();
		const char* lstr = line.c_str();
		if (*lstr) startMillis = millis();
		
		//if (line.startsWith(selectorMsg))
		
		if (lstr[0] == '+' && strncmp(lstr + 1, selector, sellen) == 0 && lstr[1 + sellen] == ':')
		{
			String responseData = line.substring(sellen + 2);
			response += responseData + "\n";
			#ifndef NODEBUG
			if (debugMode == eODM_Data)
				debugPrint(responseData);
			#endif
		}

		#ifndef NODEBUG		
		if ((debugMode & eODM_Dump) == eODM_Dump)
			debugPrint(lstr);
		#endif
		
		if (strcmp(lstr, ESP_STATUS_OK)==0 || strcmp(lstr, ESP_STATUS_NO_CHANGE)==0)
		{
			state = ePS_OK;
			return true;
		}
		else if (strcmp(lstr, ESP_STATUS_SEND_OK)==0)
		{
			state = ePS_Completed;
			return true;
		}
		else if (strcmp(lstr, ESP_STATUS_ERR)==0 || strcmp(lstr, ESP_STATUS_NO_LINK)==0 || strcmp(lstr, ESP_STATUS_ONLINK)==0)
		{
			state = ePS_Error;
			#ifndef NODEBUG
			if (debugMode == eODM_Data)
				debugPrint("<ERROR>");
			#endif
			return false;
		}
	}
	#ifndef NODEBUG
	debugPrint("<NoResponse>");
	#endif
	state = ePS_NoResponse;
	return false;
}

String ESP8266pro_Parser::readLine()
{
	char lineSplitChar = ESP_LINE_WRAPPER[strlen(ESP_LINE_WRAPPER) - 1];
	String data = "";
	if (espStream.peek() == '+')
	{
		char buffer[8] = {0};
		espStream.readBytes(buffer, strlen(ESP_IP_DATA_HANDLER));
		if (strcmp(buffer, ESP_IP_DATA_HANDLER) == 0) // Begin of IP data
		{
			#ifndef NODEBUG
			debugPrint("<WRAP processing>");
			#endif
			processConnectionDataReceive(true);
		}
		else
			data = String(buffer);
	}
    data += espStream.readStringUntil(lineSplitChar);
	if (data.endsWith("\r"))
		data.remove(data.length() - 1);
	return data;
}

#ifndef NODEBUG
void ESP8266pro_Parser::debugPrint(const String& text, bool lineWrap/* = true*/)
{
	debugPrint(text.c_str(), lineWrap);
}

void ESP8266pro_Parser::debugPrint(const char* text, bool lineWrap/* = true*/)
{
	if (debugStream == NULL || debugMode == eODM_None) return;
	if (lineWrap)
		debugStream->println(text);
	else
		debugStream->print(text);
}
#endif