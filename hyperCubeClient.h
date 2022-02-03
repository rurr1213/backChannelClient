#include <stdio.h>
#include <queue>

#include "tcp.h"
#include "sthread.h"
#include "Messages.h"
#include "mserdes.h"
#include "Packet.h"

#define HYPERCUBE_CONNECTIONINTERVAL_MS 8000			// connection attempt interval in milliseconds


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

    virtual bool sendMsg(Msg& msg) = 0;
    virtual bool onReceivedData(void) = 0;
    virtual bool onConnect(void) = 0;
    virtual bool onDisconnect(void) = 0;
    virtual bool isSignallingMsg(std::unique_ptr<Packet>& rppacket) = 0;
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
            PacketQWithLock inPacketQ;
            std::unique_ptr<Packet> pinputPacket = 0;
            CstdConditional eventReadyToRead;
            virtual bool threadFunction(void);
            bool readPackets(void);
            int readData(void* pdata, int dataLen);
        public:
            RecvActivity(IHyperCubeClientCore* pIHyperCubeClientCore, SignallingObject& _signallingObject);
            bool init(void);
            bool deinit(void);
            bool recvPacket(Packet::UniquePtr& rppacket);
            bool onConnect(void);
            bool onDisconnect(void);
        };

        class SendActivity : public CstdThread {
        private:
            IHyperCubeClientCore* pIHyperCubeClientCore = 0;
            WritePacketBuilder writePacketBuilder;
            PacketQWithLock outPacketQ;
            virtual bool threadFunction(void);
            bool writePacket(void);
            bool writePackets(void);

            CstdConditional eventPacketsAvailableToSend;
            int totalBytesSent = 0;
            int sendData(const void* pdata, const int dataLen);

        public:
            SendActivity(IHyperCubeClientCore* _pIHyperCubeClientCore);
            ~SendActivity();
            bool init(void);
            bool deinit(void);
            bool sendPacket(Packet::UniquePtr& rppacket);
            bool onConnect(void);
            bool onDisconnect(void);
        };

        class SignallingObject : CstdThread {
            MSerDes mserdes;
            CstdConditional eventDisconnectedFromServer;
            std::atomic<bool> connected = false;
            std::atomic<bool> justDisconnected = false;

            IHyperCubeClientCore* pIHyperCubeClientCore = 0;
            bool socketValid(void) { return pIHyperCubeClientCore->tcpSocketValid(); }
            bool connect(void);
            std::string serverIpAddress;
            bool connectIfNotConnected(void);
            bool processSigMsgJson(const Packet* ppacket);
            bool threadFunction(void);
            bool sendMsg(Msg& msg) {
                return pIHyperCubeClientCore->sendMsg(msg);
            }
            bool createGroup(std::string _groupName);
            bool publish(void);
            bool subscribe(void);
            bool sendEcho(void);
            bool sendLocalPing(void);
            bool setupConnection(void);

        public:
            uint64_t systemId;

            SignallingObject(IHyperCubeClientCore* _pIHyperCubeClientCore);
            void init(std::string _serverIpAddress);
            void deinit(void);
            virtual bool isSignallingMsg(std::unique_ptr<Packet>& rppacket);
            virtual bool onConnect(void);
            virtual bool onDisconnect(void);
        };

        virtual bool onConnect(void);
        virtual bool onDisconnect(void);
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

public:
        HyperCubeClientCore();
        ~HyperCubeClientCore();

        bool init(std::string _serverIpAddress, bool reInit = true);
        bool deinit(void);

        virtual bool connectionClosed(void) { return true; };

        virtual bool sendMsg(Msg& msg);
        bool recvMsg(Msg& msg);

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
