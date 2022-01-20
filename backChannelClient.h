#include <atomic>
#include "HyperCubeClient.h"

#define BACKCHANNEL_CONNECTIONINTERVAL_MS 10000			// connection attempt interval in milliseconds

#define BACKCHANNEL_SERVER_IP "127.0.0.1"

class BackChannelClient : public HyperCubeClient, IwthreadObject
{
	CstdThread stdThread;
	std::atomic<bool> connected = false; 
	std::atomic<int> connectionAttempts = 0;
	std::string serverIpAddress;

	virtual bool threadFunction(void);
	bool connectIfNotConnected(void);
	bool processConnectionEvents(void);
	bool readPackets(void);
	bool writePackets(void);
	bool connectionClosed(void);
public:
	BackChannelClient();
	~BackChannelClient();
	bool init(void);
	bool deinit(void);
};

