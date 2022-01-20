#include <stdio.h>

#include "Logger.h"
#include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
#include "backChannelClient.h"

#include "json.hpp"
using json = nlohmann::json;

using namespace std;

#ifdef _WIN64
#define poll WSAPoll
#else
#endif

BackChannelClient::BackChannelClient() :
	HyperCubeClient(), CstdThread(this)
{
}

BackChannelClient::~BackChannelClient()
{
}

bool BackChannelClient::init(void)
{
	checkConnection();
	return true;
}

bool BackChannelClient::deinit(void)
{
	return true;
}

bool BackChannelClient::checkConnection(void)
{
	if (!socketValid()) {
		if (connectionAttempts++ > 0) {
			Sleep(10000);
		}
		connect(serverIpAddress);
	}
	return true;
}

bool BackChannelClient::threadFunction(void) 
{
	LOG_INFO("recvThreadStarted", 0);
	while (!checkIfShouldExit()) {
		if (checkConnection())
			processConnectionEvents();
	}
	exiting();
	return true;
}

bool BackChannelClient::processConnectionEvents(void)
{
	struct pollfd pollFds[1];
	memset(pollFds, 0, 1);
	int numFds = 0;

	pollFds[0].fd = getSocket();
	pollFds[0].events = POLLOUT | POLLIN;
	pollFds[0].revents = 0;

	//int res = poll(pollFds, numFds, -1);
	/// set a timeout for this poll, so that it will exit after this timeout and loop around again. 
	/// This is so that, if new sockets are added while the current poll is blocked, any data sent on the new sockets will not be 
	/// received until, the current poll is unblocked and a new poll is started with the new sockets in pollFds. 
	/// So, we timeout every 1 second, so that if any new sockets are added during a poll that is blocked, it will exit and 
	/// come around again with the new socket added in pollFds.
	int res = poll(pollFds, numFds, 1000);

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
			LOG_INFO("poll returned POLLPRI", pollFds[0].revents);
		}
		if ((revents & POLLHUP) || (revents & POLLERR) || (revents & POLLNVAL)) {
			connectionClosed();
			LOG_INFO("TCP connection close", (int)pollFds[0].fd);
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
