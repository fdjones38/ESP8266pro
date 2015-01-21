// ESP8266pro
// This software distributed under the terms of the MIT license
// (c) Skurydin Alexey, 2014

#include "ESP8266proServer.h"

ESP8266proServer::ESP8266proServer(ESP8266pro& esp, ConnectionDataCallback callback)
	: parent(esp), dataCallback(callback)
{
	for (int i=0; i < ESP_MAX_CONNECTIONS; i++)
	{
		virtualConnection[i] = new ESP8266proServerConection(parent, i);
		receiveCompleted[i] = false;
	}
}

boolean ESP8266proServer::start(int port)
{
	if (!parent.setServer(this)) return false; // May be some other server already started?
	serverPort = port;
	return parent.execute((String)"AT+CIPSERVER=1," + serverPort);
}

void ESP8266proServer::stop()
{
	for (int i=0; i < ESP_MAX_CONNECTIONS; i++)
		receiveCompleted[i] = false;
	parent.execute((String)"AT+CIPSERVER=0," + serverPort);
	parent.setServer(NULL);
	parent.restart();
}

boolean ESP8266proServer::processRequests()
{
	bool ok = false;
	bool processed;
	
	do
	{
		// Receive incoming IP data
		ok |= parent.connectionDataReceive(false);
		processed = false;
		
		// Complete finished requests
		for (int i=0; i < ESP_MAX_CONNECTIONS; i++)
		{
			if (receiveCompleted[i])
			{
				receiveCompleted[i] = false; // Can be updated soon
				ESP8266proServerConection* link = virtualConnection[i];
				link->incrimentUses();
				dataCallback(link, "", 0, true);
				link->decrementUses();
				if (virtualConnection[i] != link)
				{
					delete link;
				}
				processed = true;
			}
		}
	} while(processed);
	
	if (millis() - lastCheck > 30000)
	{
		// Validate server state
		parent.execute("AT+CIPMUX?");
		if (parent.getLine(0) != "1")
		{
			// Something went wrong... :(
			stop();
			parent.restart();
			parent.execute("AT+CIPMUX=1"); // 1 = multiple connection
			start(serverPort);
		}
		else
			closeAllConnections(); // Kill all losted connections
		lastCheck = millis();
	}
	return ok;
}

void ESP8266proServer::closeAllConnections()
{
	if (!parent.execute("AT+CIPSTATUS")) return;
	for (int i=0; i < parent.getLinesCount(); i++)
	{
		int id = parent.getLineItem(i, 0).toInt();
		int isServer = parent.getLineItem(i, 4).toInt() == 1;
		if (isServer)
			virtualConnection[id]->close();
	}
}

void ESP8266proServer::onDataReceive(int connectionId, char* buffer, int length, DataReceiveAction action)
{
	ESP8266proServerConection* link = virtualConnection[connectionId];
	if (action == eDRA_Begin)
	{
		if (link->isUsed())
		{
			link->dispose();
			virtualConnection[connectionId] = new ESP8266proServerConection(parent, connectionId);
		}
		return; // Nothing to process
	}
	
	if (dataCallback != NULL)
	{
		// To prevent stack overflow, we delay user response to future processing
		if (action == eDRA_End)
		{
			lastCheck = millis(); // No problems!
			receiveCompleted[connectionId] = true;
		}
		link->incrimentUses();
		dataCallback(link, buffer, length, false);
		link->decrementUses();
		if (virtualConnection[connectionId] != link)
		{
			delete link;
		}
	}	
}