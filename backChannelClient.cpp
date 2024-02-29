#include <stdio.h>

#include "Logger.h"

#include "backChannelClient.h"

#include "json.hpp"
using json = nlohmann::json;

using namespace std;

BackChannelClient::BackChannelClient() :
	HyperCubeClient()
{
}

BackChannelClient::~BackChannelClient()
{
}

bool BackChannelClient::init(std::string serverName, bool reInit)
{
	HyperCubeClient::init(serverName, reInit);
	return true;
}

bool BackChannelClient::deinit(void)
{
	HyperCubeClient::deinit();
	return true;
}

bool BackChannelClient::setIP(std::string ipAddress)
{
	HyperCubeClient::init(ipAddress, true);
	return true;
}

/*
bool BackChannelClient::readPackets(void)
{
	return true;
}

bool BackChannelClient::writePackets(void)
{
	return true;
}

bool BackChannelClient::connectionClosed(void)
{
	return true;
}
*/