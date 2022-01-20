#include <atomic>
#include "HyperCubeClient.h"

class BackChannelClient : public HyperCubeClient , public CstdThread
{
	std::atomic<bool> connected = false; 
	std::atomic<int> connectionAttempts = 0;
	std::string serverIpAddress;

	virtual bool threadFunction(void);
	bool checkConnection(void);
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

