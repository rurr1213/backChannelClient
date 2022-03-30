#include <stdio.h>
#include <queue>

#include "tcp.h"
#include "sthread.h"
#include "Messages.h"
#include "mserdes.h"
#include "Packet.h"

#define HYPERCUBE_CONNECTIONINTERVAL_MS 8000			// connection attempt interval in milliseconds

#ifdef _WIN64
#define uint128_t   UUID
#else
#define uint128_t   __int128
#endif

class IHyperCubeClientCore
{
    Ctcp::Client& rtcpClient;
public:
    IHyperCubeClientCore(Ctcp::Client& _rtcpClient) :
        rtcpClient{ _rtcpClient } {};

//    virtual bool tcpClose(void) { return rtcpClient.close(); }
    virtual bool tcpConnect(std::string addrString, int port) { return rtcpClient.connect(addrString, port); }
    virtual bool tcpSocketValid(void) { return rtcpClient.socketValid(); }
    virtual int tcpGetSocket(void) { return (int)rtcpClient.getSocket(); }
    int tcpRecv(char* buf, const int bufSize) { return rtcpClient.recv(buf, bufSize); }
    int tcpSend(const char* buf, const int bufSize) { return rtcpClient.send(buf, bufSize); }

    virtual bool sendMsgOut(Msg& msg) = 0;
    virtual bool onReceivedData(void) = 0;
    virtual bool onConnect(void) = 0;   // tcp connection established
    virtual bool onDisconnect(void) = 0;    // tcp connection closed
    virtual bool isSignallingMsg(std::unique_ptr<Packet>& rppacket) = 0;
    virtual bool onOpenForData(void) = 0;  // open for data
    virtual bool onClosedForData(void) = 0; // closed for data
};

class HyperCubeClientCore : IHyperCubeClientCore
{
    private:

        class SignallingObject;

        class PacketQWithLock : std::deque<Packet::UniquePtr> {
            std::mutex qLock;
        public:
            void init(void);
            void deinit(void);
            void push(std::unique_ptr<Packet>& rpacket);
            bool pop(std::unique_ptr<Packet>& rpacket);
            bool isEmpty(void);
        };

        class RecvActivity : CstdThread, RecvPacketBuilder::IReadDataObject {
        private:
            IHyperCubeClientCore* pIHyperCubeClientCore = 0;
            RecvPacketBuilder recvPacketBuilder;
            std::mutex recvPacketBuilderLock;
            PacketQWithLock inPacketQ;
            std::unique_ptr<Packet> pinputPacket = 0;
            CstdConditional eventReadyToRead;
            virtual bool threadFunction(void);
            RecvPacketBuilder::READSTATUS readPackets(void);
            int readData(void* pdata, int dataLen);
        public:
            RecvActivity(IHyperCubeClientCore* pIHyperCubeClientCore, SignallingObject& _signallingObject);
            bool init(void);
            bool deinit(void);
            bool receiveIn(Packet::UniquePtr& rppacket);
            bool onConnect(void);
            bool onDisconnect(void);
        };

        class SendActivity : public CstdThread {
        private:
            IHyperCubeClientCore* pIHyperCubeClientCore = 0;
            WritePacketBuilder writePacketBuilder;
            std::mutex writePacketBuilderLock;
            PacketQWithLock outPacketQ;
            virtual bool threadFunction(void);
            bool writePacket(void);
            bool writePackets(void);

            CstdConditional eventPacketsAvailableToSend;
            int totalBytesSent = 0;
            int sendDataOut(const void* pdata, const int dataLen);

        public:
            SendActivity(IHyperCubeClientCore* _pIHyperCubeClientCore);
            ~SendActivity();
            bool init(void);
            bool deinit(void);
            bool sendOut(Packet::UniquePtr& rppacket);
            bool onConnect(void);
            bool onDisconnect(void);
        };

        class SignallingObject : CstdThread {
            MSerDes mserdes;
            CstdConditional eventDisconnectedFromServer;
            std::atomic<bool> connected = false;
            std::atomic<bool> justDisconnected = false;
            bool alreadyWarnedOfFailedConnectionAttempt = false;

            IHyperCubeClientCore* pIHyperCubeClientCore = 0;
            bool socketValid(void) { return pIHyperCubeClientCore->tcpSocketValid(); }
            bool connect(void);
            std::string serverIpAddress;
            bool connectIfNotConnected(void);
            bool processSigMsgJson(const Packet* ppacket);
            bool threadFunction(void);
            bool sendMsgOut(Msg& msg) {
                return pIHyperCubeClientCore->sendMsgOut(msg);
            }
            bool sendConnectionInfo(std::string _connectionName);
            bool createGroup(std::string _groupName);
            bool publish(void);
            bool subscribe(std::string _groupName);
            bool echoData(std::string data = "");
            bool localPing(bool ack = false, std::string data = "localPingFromMatrix");
            bool remotePing(bool ack = false, std::string data = "remotePingFromMatrix");
            bool setupConnection(void);

            bool onCreateGroupAck(const json& jsonData);
            bool onConnectionInfoAck(const json& jsonData);
            bool onRemotePing(const json& jsonData);
            bool onEchoData(const json& jsonData);

        public:
            uint64_t connectionId;
            uuid_t applicationInstanceUUID;

            SignallingObject(IHyperCubeClientCore* _pIHyperCubeClientCore);
            void init(std::string _serverIpAddress);
            void deinit(void);
            virtual bool isSignallingMsg(std::unique_ptr<Packet>& rppacket);
            virtual bool onConnect(void);
            virtual bool onDisconnect(void);
            virtual bool onOpenForData(void);
            virtual bool onClosedForData(void);
        };

        virtual bool onConnect(void);
        virtual bool onDisconnect(void);
        virtual bool onOpenForData(void);
        virtual bool onClosedForData(void);
        virtual bool isSignallingMsg(std::unique_ptr<Packet>& rppacket);
        virtual bool onReceivedData(void);

protected:

        SignallingObject signallingObject;
        RecvActivity receiveActivity;
        SendActivity sendActivity;

        Ctcp::Client client;

        static const int SERVER_PORT = 5054;

        MSerDes mserdes;

        double totalTime = 0;
        std::string dataString;

        int numOutputMsgs = 0;

protected:
        bool sendMsgOut(Msg& msg);

public:
        HyperCubeClientCore();
        ~HyperCubeClientCore();

        bool init(std::string _serverIpAddress, bool reInit = true);
        bool deinit(void);

        virtual bool connectionClosed(void) { return true; };

        bool recvMsg(Msg& msg);
        bool getPacket(Packet& packet);

//        bool peekMsg(Msg& msg);

        SOCKET getSocket(void) { return client.getSocket(); }

//        bool printRcvdMsgCmds(std::string sentString);
//        bool processInputMsgs(std::string sentString);
};

class HyperCubeClient : public HyperCubeClientCore
{
protected:
public:
    HyperCubeClient();
    ~HyperCubeClient();
};
