#include <atomic>
#include "HyperCubeClient.h"

#define BACKCHANNEL_DEFAULT_SERVER_IP "127.0.0.1"

class BackChannelClient : public HyperCubeClient
{
//	bool readPackets(void);
//	bool writePackets(void);
//	virtual bool connectionClosed(void);
public:
	BackChannelClient();
	~BackChannelClient();
	bool init(std::string serverIpAddress = BACKCHANNEL_DEFAULT_SERVER_IP, bool reInit = false);
	bool setIP(std::string ipAddress);
	bool deinit(void);
};

