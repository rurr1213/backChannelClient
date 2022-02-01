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

        class PacketQWithLock : std::deque<Packet::UniquePtr> {
            std::mutex qLock;
        public:
            void deinit(void);
            void push(std::unique_ptr<Packet>& rpacket);
            bool pop(std::unique_ptr<Packet>& rpacket);
            bool isEmpty(void);
        };

        class SignallingObject : CstdThread {
            MSerDes mserdes;
            CstdConditional eventDisconnectedFromServer;
            std::atomic<bool> connected = false;
//            std::atomic<int> connectionAttempts = 0;

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

        class RecvActivity : CstdThread, RecvPacketBuilder::IReadDataObject {
        private:
            Ctcp::Client& rclient;
            SignallingObject& rsignallingObject;
            RecvPacketBuilder recvPacketBuilder;
            PacketQWithLock inPacketQ;
            std::unique_ptr<Packet> pinputPacket = 0;
            virtual bool threadFunction(void) {
                while (!checkIfShouldExit()) {
                    if (readPackets()) {
                        rsignallingObject.notifyRecvdData();
                    }
                    else {
                        rsignallingObject.notifySocketClosed();
                        Sleep(1000); //wait 1 second and try again
                    }
                };
                exiting();
                return true;
            }
            bool readPackets(void);
        public:
            RecvActivity(Ctcp::Client& _rclient, SignallingObject& _signallingObject) :
                CstdThread(this),
                rclient{ _rclient},
                rsignallingObject{ _signallingObject },
                recvPacketBuilder(*this, COMMON_PACKETSIZE_MAX)
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

            int readData(void* pdata, int dataLen)
            {
                return rclient.recv((char*)pdata, dataLen);
            }

            bool recvPacket(Packet::UniquePtr& rppacket) {
                bool stat = inPacketQ.pop(rppacket);
                return stat;
            }
        };

        class SendActivity : public CstdThread {
        private:
            Ctcp::Client& rclient;
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
            SendActivity(Ctcp::Client& _rclient) :
                CstdThread(this),
                rclient{ _rclient},
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
            int sendData(const void* pdata, const int dataLen)
            {
                return rclient.send((char*)pdata, dataLen);
            }

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
