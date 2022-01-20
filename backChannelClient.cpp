#include <stdio.h>

#include "Logger.h"
#include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
#include <errno.h>

#include "backChannelClient.h"

#include "json.hpp"
using json = nlohmann::json;

using namespace std;

#ifdef _WIN64
#define poll WSAPoll
#else
#endif

BackChannelClient::BackChannelClient() :
	HyperCubeClient(), 
	stdThread(this),
	serverIpAddress{ BACKCHANNEL_SERVER_IP }
{
}

BackChannelClient::~BackChannelClient()
{
}

bool BackChannelClient::init(void)
{
	stdThread.init();
	stdThread.start();
	return true;
}

bool BackChannelClient::deinit(void)
{
	stdThread.setShouldExit();
	stdThread.stop();
	stdThread.deinit();
	return true;
}

bool BackChannelClient::setIP(std::string ipAddress)
{
	serverIpAddress = ipAddress;
	return true;
}

bool BackChannelClient::connectIfNotConnected(void)
{
	bool stat = true;
	if (!socketValid()) {
		if (connectionAttempts > 0) {
			Sleep(BACKCHANNEL_CONNECTIONINTERVAL_MS);
			connectionAttempts = 0;
		}
		stat = connect(serverIpAddress);
		if (stat) {
			LOG_INFOD("BackChannelClient::connectIfNotConnected()", "connected to " + serverIpAddress, 0);
			setupConnection();
		} 
		else
			LOG_WARNING("BackChannelClient::connectIfNotConnected()", "connection failed to " + serverIpAddress, 0);
		connectionAttempts++;
	}
	return stat;
}

bool BackChannelClient::threadFunction(void) 
{
	LOG_INFO("BackChannelClient::threadFunction(), ThreadStarted", 0);
	while (!stdThread.checkIfShouldExit()) {
		if (connectIfNotConnected())
			if (!processConnectionEvents()) {
				Sleep(1000);
			}
	}
	stdThread.exiting();
	return true;
}

bool BackChannelClient::setupConnection(void)
{
	createGroup("Matrix group");
	sendEcho();
	return true;
}

bool BackChannelClient::processConnectionEvents(void)
{
	struct pollfd pollFds[1];
	memset(pollFds, 0, sizeof(pollfd));
	int numFds = 1;

	pollFds[0].fd = getSocket();
	LOG_ASSERT(pollFds[0].fd > 0);
	pollFds[0].events = POLLOUT | POLLIN;
	pollFds[0].revents = 0;

	//int res = poll(pollFds, numFds, -1);
	/// set a timeout for this poll, so that it will exit after this timeout and loop around again. 
	/// This is so that, if new sockets are added while the current poll is blocked, any data sent on the new sockets will not be 
	/// received until, the current poll is unblocked and a new poll is started with the new sockets in pollFds. 
	/// So, we timeout every 1 second, so that if any new sockets are added during a poll that is blocked, it will exit and 
	/// come around again with the new socket added in pollFds.
	int res = poll(pollFds, numFds, 1000);
	if (res < 0) {
		LOG_WARNING("BackChannelClient::processConnectionEvents()", "poll returned - 1", res);
		return false;
	}
	if ((pollFds[0].fd > 0) && (pollFds[0].revents != 0)) { // if not being ignored
	// check socket index match. It should even if a new connection was added.
		int revents = pollFds[0].revents;
		if ((revents & POLLRDNORM) || (revents & POLLRDBAND)) {
			recvPackets();
		}
		if (revents & POLLWRNORM) {
			writePackets();
		}
		if (revents & POLLPRI) {
			LOG_INFO("BackChannelClient::processConnectionEvents(), poll returned POLLPRI", pollFds[0].revents);
		}
		if ((revents & POLLHUP) || (revents & POLLERR) || (revents & POLLNVAL)) {
			connectionClosed();
			LOG_INFO("BackChannelClient::processConnectionEvents(), TCP connection close", (int)pollFds[0].fd);
		}
	}
	return true;
};

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
