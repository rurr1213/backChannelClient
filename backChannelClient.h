#include <atomic>
#include "HyperCubeClient.h"

#define BACKCHANNEL_DEFAULT_SERVER_IP "127.0.0.1"

class BackChannelClient : public HyperCubeClient
{
public:
	BackChannelClient();
	~BackChannelClient();
	bool init(std::string serverName, bool reInit = false);
	bool setIP(std::string ipAddress);
	bool deinit(void);
};

