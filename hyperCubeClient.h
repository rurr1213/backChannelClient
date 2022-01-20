#include <stdio.h>
#include <queue>

#include "tcp.h"
#include "Messages.h"
#include "mserdes.h"
#include "Packet.h"

class HyperCubeClient : public RecvPacketBuilder::IReadDataObject// hyper cube client
{
    private:
        Ctcp::Client client;
        std::string serverIpAddress;
        static const int SERVER_PORT = 5054;
        RecvPacketBuilder recvPacketBuilder;
        WritePacketBuilder threadSafeWritePacketBuilder;
        std::deque<Packet::UniquePtr> inPacketQ;
        virtual int readData(void* pdata, int dataLen);
        MSerDes mserdes;
        std::unique_ptr<Packet> pinputPacket = 0;

        int totalBytesSent = 0;
        double totalTime = 0;
        std::string dataString;

        bool createGroup(std::string _groupName);
        bool publish(void);
        bool subscribe(void);
        bool sendEcho(void);

        uint64_t systemId;
    protected:
        bool sendPacket(Packet::UniquePtr& ppacket);
        bool recvPackets(void);
public:
        HyperCubeClient();
        ~HyperCubeClient();

        bool init(std::string _serverIpAddress);
        bool deinit(void);

        bool sendMsg(Msg& msg);
        bool peekMsg(Msg& msg);
        bool recvMsg(Msg& msg);

        bool connect(std::string _serverIpAddress);
        SOCKET getSocket(void) { return client.getSocket(); }
        bool socketValid(void) { return client.socketValid(); }

        bool printRcvdMsgCmds(std::string sentString);
        bool processInputMsgs(std::string sentString);
};

