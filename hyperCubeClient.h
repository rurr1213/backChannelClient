#include <stdio.h>
#include <queue>

#include "tcp.h"
#include "Messages.h"
#include "mserdes.h"
#include "Packet.h"

#define HYPERCUBE_CONNECTIONINTERVAL_MS 10000			// connection attempt interval in milliseconds

class ISocketObject : public RecvPacketBuilder::IReadDataObject 
{
public:
    virtual int readData(void* pdata, int dataLen) = 0;
    virtual int sendData(const void* pdata, const int dataLen) = 0;
    virtual bool notifySocketClosed(void) = 0;
    virtual bool notifyRecvdData(void) = 0;
};

class HyperCubeClientCore : public ISocketObject, IwthreadObject
{
    private:

        class PacketQWithLock : std::deque<Packet::UniquePtr> {
            std::mutex qLock;
        public:
            void deinit(void);
            void push(std::unique_ptr<Packet>& rpacket);
            bool pop(std::unique_ptr<Packet>& rpacket);
            bool isEmpty(void);
        };

        class SignallingObject {
            MSerDes mserdes;
        public:
            virtual bool isSignallingMsg(std::unique_ptr<Packet>& rppacket);
            bool processSigMsgJson(const Packet* ppacket);
        };

        class RecvActivity : CstdThread {
        private:
            ISocketObject& rsocketObject;
            SignallingObject& rsignallingObject;
            RecvPacketBuilder recvPacketBuilder;
            PacketQWithLock inPacketQ;
            std::unique_ptr<Packet> pinputPacket = 0;
            virtual bool threadFunction(void) {
                while (!checkIfShouldExit()) {
                    if (readPackets()) {
                        rsocketObject.notifyRecvdData();
                    }
                    else {
                        rsocketObject.notifySocketClosed();
                        Sleep(1000); //wait 1 second and try again
                    }
                };
                exiting();
                return true;
            }
            bool readPackets(void);
        public:
            RecvActivity(ISocketObject& _isocketObject, SignallingObject& _signallingObject) :
                CstdThread(this),
                rsocketObject{ _isocketObject },
                rsignallingObject{ _signallingObject },
                recvPacketBuilder(_isocketObject, COMMON_PACKETSIZE_MAX)
            {};
            bool init(void) {
                recvPacketBuilder.init();
                pinputPacket = std::make_unique<Packet>();
                CstdThread::init(true);
                return true;
            }
            bool deinit(void) {
                recvPacketBuilder.deinit();
                Packet* packet = pinputPacket.release();
                if (packet) delete packet;
                inPacketQ.deinit();
                CstdThread::deinit(true);
                return true;
            }

            bool recvPacket(Packet::UniquePtr& rppacket) {
                bool stat = inPacketQ.pop(rppacket);
                return stat;
            }
        };

        class SendActivity : public CstdThread {
        private:
            ISocketObject& rsocketObject;
            WritePacketBuilder writePacketBuilder;
            PacketQWithLock outPacketQ;
            virtual bool threadFunction(void) {
                while (!checkIfShouldExit()) {
                    eventPacketsAvailableToSend.wait();
                    eventPacketsAvailableToSend.reset();
                    if (writePackets()) {
//                        Sleep(1000); // wait a little and try again
                    }
                };
                exiting();
                return true;
            }
            bool writePacket(void);
            bool writePackets(void);

            CstdConditional eventPacketsAvailableToSend;
            int totalBytesSent = 0;

        public:
            SendActivity(ISocketObject& _isocketObject) :
                CstdThread(this),
                rsocketObject{ _isocketObject },
                writePacketBuilder(COMMON_PACKETSIZE_MAX) 
            {};
            ~SendActivity() {};
            bool init(void) {
                writePacketBuilder.init();
                eventPacketsAvailableToSend.reset();
                CstdThread::init(true);
                return true;
            }
            bool deinit(void) {
                CstdThread::setShouldExit();
                eventPacketsAvailableToSend.notify();
                CstdThread::deinit(true);
                writePacketBuilder.deinit();
                outPacketQ.deinit();
                return true;
            }
            bool sendPacket(Packet::UniquePtr& rppacket) {
                outPacketQ.push(rppacket);
                eventPacketsAvailableToSend.notify();
                return true;
            }
        };

        SignallingObject signallingObject;
        RecvActivity receiveActivity;
        SendActivity sendActivity;

        Ctcp::Client client;
        std::string serverIpAddress;
        static const int SERVER_PORT = 5054;


        virtual int readData(void* pdata, int dataLen);
        virtual int sendData(const void* pdata, const int dataLen);

        virtual bool notifySocketClosed(void);
        virtual bool notifyRecvdData(void);

        MSerDes mserdes;

        double totalTime = 0;
        std::string dataString;

        CstdThread stdThread;
        std::atomic<bool> connected = false;
        std::atomic<int> connectionAttempts = 0;

        virtual bool threadFunction(void);
        bool connectIfNotConnected(void);
//        bool processConnectionEvents(void);
        virtual bool setupConnection(void) = 0;

public:
        HyperCubeClientCore();
        ~HyperCubeClientCore();

        bool init(std::string _serverIpAddress, bool reInit = true);
        bool deinit(void);

        virtual bool connectionClosed(void) { return true; };

        bool sendMsg(Msg& msg);
//        bool peekMsg(Msg& msg);
        bool recvMsg(Msg& msg);

        bool connect(void);
        SOCKET getSocket(void) { return client.getSocket(); }
        bool socketValid(void) { return client.socketValid(); }

        bool printRcvdMsgCmds(std::string sentString);
        bool processInputMsgs(std::string sentString);
};

class HyperCubeClient : public HyperCubeClientCore
{
    uint64_t systemId;
protected:
    bool createGroup(std::string _groupName);
    bool publish(void);
    bool subscribe(void);
    bool sendEcho(void);
    bool sendLocalPing(void);
    bool setupConnection(void);
public:
    HyperCubeClient();
    ~HyperCubeClient();
};
