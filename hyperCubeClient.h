#include <stdio.h>
#include <queue>

#include "tcp.h"
#include "Messages.h"
#include "mserdes.h"
#include "Packet.h"

#define HYPERCUBE_CONNECTIONINTERVAL_MS 10000			// connection attempt interval in milliseconds


class IHyperCubeClientCore 
{
public:
    virtual bool sendMsg(Msg& msg) = 0;
};

class HyperCubeClientCore : IHyperCubeClientCore
{
    private:

        class SignallingObject;

        class PacketQWithLock : std::deque<Packet::UniquePtr> {
            std::mutex qLock;
        public:
            void deinit(void);
            void push(std::unique_ptr<Packet>& rpacket);
            bool pop(std::unique_ptr<Packet>& rpacket);
            bool isEmpty(void);
        };

        class RecvActivity : CstdThread, RecvPacketBuilder::IReadDataObject {
        private:
            Ctcp::Client& rclient;
            SignallingObject& rsignallingObject;
            RecvPacketBuilder recvPacketBuilder;
            PacketQWithLock inPacketQ;
            std::unique_ptr<Packet> pinputPacket = 0;
            virtual bool threadFunction(void);
            bool readPackets(void);
        public:
            RecvActivity(Ctcp::Client& _rclient, SignallingObject& _signallingObject);
            bool init(void);
            bool deinit(void);
            int readData(void* pdata, int dataLen);
            bool recvPacket(Packet::UniquePtr& rppacket);
        };

        class SendActivity : public CstdThread {
        private:
            Ctcp::Client& rclient;
            WritePacketBuilder writePacketBuilder;
            PacketQWithLock outPacketQ;
            virtual bool threadFunction(void);
            bool writePacket(void);
            bool writePackets(void);

            CstdConditional eventPacketsAvailableToSend;
            int totalBytesSent = 0;

        public:
            SendActivity(Ctcp::Client& _rclient);
            ~SendActivity();
            bool init(void);
            bool deinit(void);
            bool sendPacket(Packet::UniquePtr& rppacket);
            int sendData(const void* pdata, const int dataLen);
        };

        class SignallingObject : CstdThread {
            MSerDes mserdes;
            CstdConditional eventDisconnectedFromServer;
            std::atomic<bool> connected = false;

            Ctcp::Client& rclient;
            IHyperCubeClientCore* pIHyperCubeClientCore = 0;
            bool socketValid(void) { return rclient.socketValid(); }
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

            SignallingObject(IHyperCubeClientCore* _pIHyperCubeClientCore, Ctcp::Client& _rclient) :
                pIHyperCubeClientCore{ _pIHyperCubeClientCore },
                CstdThread(this),
                rclient{ _rclient } {};
            void init(std::string _serverIpAddress) {
                serverIpAddress = _serverIpAddress;
                if (!isStarted()) {
                    CstdThread::init(true);
                    eventDisconnectedFromServer.reset();
                }
            }
            void deinit(void) {
                if (isStarted()) {
                    while (!isExited()) {
                        setShouldExit();
                        eventDisconnectedFromServer.notify();
                    }
                    rclient.close(); // close after thread exits, to avoid reconnecting before exit
                }
                CstdThread::deinit(true);
            }
            virtual bool isSignallingMsg(std::unique_ptr<Packet>& rppacket);
            virtual bool notifySocketClosed(void);
            virtual bool notifyRecvdData(void);
        };

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
