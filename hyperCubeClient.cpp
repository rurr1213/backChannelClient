#include <stdio.h>

#include "Logger.h"
#include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
#include <errno.h>

#include "hyperCubeClient.h"
#include "Common.h"
#include "Packet.h"
#include "mserdes.h"
#include "kbhit.h"
#include "clockGetTime.h"
#include "MsgExt.h"

using namespace std;

#ifdef _WIN64
#define poll WSAPoll
#else
#endif

// ------------------------------------------------------------------------------------------------

void HyperCubeClientCore::PacketQWithLock::init(void) 
{
    deinit();
}

void HyperCubeClientCore::PacketQWithLock::deinit(void) {
    std::lock_guard<std::mutex> lock(qLock);
    while (std::deque<Packet::UniquePtr>::size() > 0) {
        std::unique_ptr<Packet> rpacket = std::move(std::deque<Packet::UniquePtr>::front());
        Packet* packet = rpacket.release();
        if (packet) delete packet;
        pop_front();
    }
}

void HyperCubeClientCore::PacketQWithLock::push(std::unique_ptr<Packet>& rpacket) {
    std::lock_guard<std::mutex> lock(qLock);
    push_back(std::move(rpacket));
}

bool HyperCubeClientCore::PacketQWithLock::pop(std::unique_ptr<Packet>& rpacket) {
    std::lock_guard<std::mutex> lock(qLock);
    if (empty()) return false;
    rpacket = std::move(std::deque<Packet::UniquePtr>::front());
    pop_front();
    return true;
}

bool HyperCubeClientCore::PacketQWithLock::isEmpty(void) 
{
    std::lock_guard<std::mutex> lock(qLock);
    return empty();
}

// ----------------------------------------------------------------------


HyperCubeClientCore::RecvActivity::RecvActivity(IHyperCubeClientCore* pIHyperCubeClientCore, SignallingObject& _signallingObject) :
    CstdThread(this),
    pIHyperCubeClientCore{ pIHyperCubeClientCore },
    recvPacketBuilder(*this, COMMON_PACKETSIZE_MAX)
{};

bool HyperCubeClientCore::RecvActivity::init(void) 
{
    eventReadyToRead.reset();
    std::lock_guard<std::mutex> lock(recvPacketBuilderLock);
    recvPacketBuilder.init();
    pinputPacket = std::make_unique<Packet>();
    CstdThread::init(true);
    return true;
}
bool HyperCubeClientCore::RecvActivity::deinit(void) 
{
    eventReadyToRead.notify();
    // locking here causes a deadlock during shutdown as readPackets() cannot get lock to shutdown
    // No need to lock as its all being deinited anyway
    //   std::lock_guard<std::mutex> lock(recvPacketBuilderLock);
    recvPacketBuilder.deinit();
    Packet* packet = pinputPacket.release();
    if (packet) delete packet;
    inPacketQ.deinit();
    CstdThread::deinit(true);
    return true;
}

bool HyperCubeClientCore::RecvActivity::threadFunction(void)
{
    do {
        eventReadyToRead.wait();
        if (checkIfShouldExit()) break;
        RecvPacketBuilder::READSTATUS readStatus = readPackets();
        switch (readStatus) {
            case RecvPacketBuilder::READSTATUS::NEEDEDDATAREAD:
                pIHyperCubeClientCore->onReceivedData();
                break;
            case RecvPacketBuilder::READSTATUS::READERROR:
                LOG_WARNING("HyperCubeClientCore::RecvActivity::threadFunction()", "peer error", (int)readStatus);
            case RecvPacketBuilder::READSTATUS::PEERSHUTDOWN:
                pIHyperCubeClientCore->onDisconnect();
                eventReadyToRead.reset();
                break;
            case RecvPacketBuilder::READSTATUS::MOREDATANEEDED:
                break;
            default:
                LOG_WARNING("HyperCubeClientCore::RecvActivity::threadFunction()", "invalid state", (int)readStatus);
                break;
        }
    } while (!checkIfShouldExit());
    exiting();
    return true;
}

RecvPacketBuilder::READSTATUS HyperCubeClientCore::RecvActivity::readPackets(void)
{
    std::lock_guard<std::mutex> lock(recvPacketBuilderLock);

    RecvPacketBuilder::READSTATUS readStatus = recvPacketBuilder.readPacket(*pinputPacket);
    if (readStatus== RecvPacketBuilder::READSTATUS::NEEDEDDATAREAD) {
        if (!pIHyperCubeClientCore->isSignallingMsg(pinputPacket)) {
            inPacketQ.push(pinputPacket);
            pinputPacket = std::make_unique<Packet>();
            pIHyperCubeClientCore->onReceivedData();
        }
    }
    return readStatus;
}

int HyperCubeClientCore::RecvActivity::readData(void* pdata, int dataLen)
{
    int res = pIHyperCubeClientCore->tcpRecv((char*)pdata, dataLen);
    return res;
}

bool HyperCubeClientCore::RecvActivity::onConnect(void)
{
    eventReadyToRead.notify();
    return true;
}

bool HyperCubeClientCore::RecvActivity::onDisconnect(void)
{
    eventReadyToRead.reset();
    return true;
}


bool HyperCubeClientCore::RecvActivity::receiveIn(Packet::UniquePtr& rppacket) {
    bool stat = inPacketQ.pop(rppacket);
    return stat;
}

// ------------------------------------------------------------------

HyperCubeClientCore::SendActivity::SendActivity(IHyperCubeClientCore* _pIHyperCubeClientCore) :
    CstdThread(this),
    pIHyperCubeClientCore{ _pIHyperCubeClientCore },
    writePacketBuilder(COMMON_PACKETSIZE_MAX)
{};

HyperCubeClientCore::SendActivity::~SendActivity() {};

bool HyperCubeClientCore::SendActivity::init(void) {
    std::lock_guard<std::mutex> lock(writePacketBuilderLock);
    writePacketBuilder.init();
    eventPacketsAvailableToSend.reset();
    CstdThread::init(true);
    return true;
}

bool HyperCubeClientCore::SendActivity::deinit(void) {
    CstdThread::setShouldExit();
    eventPacketsAvailableToSend.notify();
    CstdThread::deinit(true);
    std::lock_guard<std::mutex> lock(writePacketBuilderLock);
    writePacketBuilder.deinit();
    outPacketQ.deinit();
    return true;
}


bool HyperCubeClientCore::SendActivity::threadFunction(void) 
{
    do {
        eventPacketsAvailableToSend.wait();
        eventPacketsAvailableToSend.reset();
        if (checkIfShouldExit()) break;
        if (!writePackets()) {
            LOG_WARNING("HyperCubeClientCore::SendActivity::threadFunction()", "writePackets failed", 0);
        }
    } while (!checkIfShouldExit());
    exiting();
    return true;
}

bool HyperCubeClientCore::SendActivity::writePacket(void)
{
    std::lock_guard<std::mutex> lock(writePacketBuilderLock);
    Packet* packet = 0;

    // load packet builder if needed
    if (writePacketBuilder.empty()) {

        Packet::UniquePtr ppacket = 0;
        bool stat = outPacketQ.pop(ppacket);

        if (!stat) return true; // all sent, nothing to send

        packet = ppacket.get();
        writePacketBuilder.addNew(*packet);
    }

    // send whats in packet builder
    int numSent = sendDataOut(writePacketBuilder.getpData(), writePacketBuilder.getLength());

    if (numSent < 0) numSent = 0;
    totalBytesSent += numSent;
    bool sendDone = writePacketBuilder.setNumSent(numSent);

    return sendDone;
}

bool HyperCubeClientCore::SendActivity::writePackets(void)
{
    bool sendDone = false;
    do {
        sendDone = writePacket();
    } while (!outPacketQ.isEmpty());
    return sendDone;
}

bool HyperCubeClientCore::SendActivity::sendOut(Packet::UniquePtr& rppacket) 
{
    outPacketQ.push(rppacket);
    eventPacketsAvailableToSend.notify();
    return true;
}

int HyperCubeClientCore::SendActivity::sendDataOut(const void* pdata, const int dataLen)
{
    return pIHyperCubeClientCore->tcpSend((char*)pdata, dataLen);
}

bool HyperCubeClientCore::SendActivity::onConnect(void)
{
    std::lock_guard<std::mutex> lock(writePacketBuilderLock);
    writePacketBuilder.init();
    outPacketQ.init();
    return true;
}

bool HyperCubeClientCore::SendActivity::onDisconnect(void)
{
    std::lock_guard<std::mutex> lock(writePacketBuilderLock);
    writePacketBuilder.deinit();
    outPacketQ.deinit();
    return true;
}


// ------------------------------------------------------------------------------------------------

HyperCubeClientCore::SignallingObject::SignallingObject(IHyperCubeClientCore* _pIHyperCubeClientCore) :
    pIHyperCubeClientCore{ _pIHyperCubeClientCore },
    CstdThread(this)
{};

void HyperCubeClientCore::SignallingObject::init(std::string _serverIpAddress) 
{
    serverIpAddress = _serverIpAddress;
    if (!isStarted()) {
        CstdThread::init(true);
        eventDisconnectedFromServer.reset();
    }
}

void HyperCubeClientCore::SignallingObject::deinit(void) 
{
    if (isStarted()) {
        while (!isExited()) {
            setShouldExit();
            eventDisconnectedFromServer.notify();
        }
    }
    CstdThread::deinit(true);
}

bool HyperCubeClientCore::SignallingObject::threadFunction(void)
{
    LOG_INFO("HyperCubeClientCore::threadFunction()", "ThreadStarted", 0);
    while (!checkIfShouldExit()) {
        connectIfNotConnected();
        eventDisconnectedFromServer.waitUntil(HYPERCUBE_CONNECTIONINTERVAL_MS);
    }
    exiting();
    return true;
}

bool HyperCubeClientCore::SignallingObject::connectIfNotConnected(void)
{
    bool stat = true;
    if (!socketValid()) {
        if (justDisconnected) {
            // wait another second to give time, if just after a disconnect. 
            // This is a seperate signalling thread, so ok to sleep here
            Sleep(2000);    
            justDisconnected = false;
        }
        stat = connect();
        LOG_STATESTRING("HyperCubeClientCore-ServerIP", serverIpAddress);
        if (stat) {
            LOG_INFO("HyperCubeClientCore::connectIfNotConnected()", "connected to " + serverIpAddress, 0);
            pIHyperCubeClientCore->onConnect();
            setupConnection();
            connected = true;
            alreadyWarnedOfFailedConnectionAttempt = false;
            LOG_STATEINT("HyperCubeClientCore-NumSuccessfullConnectionAttempts", ++numSuccessfullConnectionAttempts);
        } else {
            LOG_STATESTRING("HyperCubeClientCore-state", "disconnected");
            LOG_STATEINT("HyperCubeClientCore-NumFailedConnectionAttempts", ++numFailedConnectionAttempts);
            if (!alreadyWarnedOfFailedConnectionAttempt) {
                LOG_WARNING("HyperCubeClientCore::connectIfNotConnected()", "connection failed to " + serverIpAddress, 0);
                alreadyWarnedOfFailedConnectionAttempt = true;
            }
        }
        eventDisconnectedFromServer.reset();

    }
    return stat;
}

bool HyperCubeClientCore::SignallingObject::isSignallingMsg(std::unique_ptr<Packet>& rppacket)
{
    bool sigMsg = false;
    Msg msg;
    const Packet* ppacket = rppacket.get();
    assert(mserdes.packetToMsg(ppacket, msg));
    bool msgProcessed = false;
    switch (msg.subSys) {
    case SUBSYS_SIG:
        sigMsg = true;
        switch (msg.command) {
        case CMD_JSON:
            processSigMsgJson(ppacket);
            break;
        default:
            assert(false); // should not get here
        }
        break;
    default:
        break;
    }
    return sigMsg;
}

bool HyperCubeClientCore::SignallingObject::onConnectionInfoAck(const json& jsonData)
{
    bool status = jsonData["status"];
    std::string jsonDataString = jsonData.dump();
    if (status) {
        LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "onConnectionInfoAck, status:success", 0);
    }
    else {
        LOG_WARNING("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "onConnectionInfoAckstatus:Failed - Duplicate name? " + jsonDataString, 0);
    }
    return true;
}

bool HyperCubeClientCore::SignallingObject::onCreateGroupAck(const json& jsonData)
{
    bool status = jsonData["status"];
    std::string jsonDataString = jsonData.dump();
    if (status) {
        LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "createGroupAck, status:success", 0);
    }
    else {
        LOG_WARNING("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "createGroupAck status:Failed - Duplicate name? " + jsonDataString, 0);
    }
    return true;
}

bool HyperCubeClientCore::SignallingObject::onRemotePing(const json& jsonData)
{
    std::string pingData = jsonData["data"];
    bool ack = jsonData["ack"];
    if (ack) {
        LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "onRemotePing ack", 0);
    } else {
        LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "onRemotePing command", 0);
        remotePing(true, pingData);
    }
    return true;
}

bool HyperCubeClientCore::SignallingObject::onEchoData(const json& jsonData)
{
    std::string data = jsonData["data"];
    bool status = echoData(data);
    std::string jsonDataString = jsonData.dump();
    if (status) {
        LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "onEchoData, success", 0);
    }
    else {
        LOG_WARNING("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "onEchoDat Failed" + jsonDataString, 0);
    }
    return true;
}

bool HyperCubeClientCore::SignallingObject::processSigMsgJson(const Packet* ppacket)
{
    MsgJson msgJson;
    json jsonData;
    assert(mserdes.packetToMsgJson(ppacket, msgJson, jsonData));
    bool msgProcessed = false;

    try {
        std::string logLineData = msgJson.jsonData;
        //        LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "received " + line, 0);
        std::string command = jsonData["command"];
        if (command == "localPing") {
            LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "received LocalPing" + logLineData, 0);
            msgProcessed = true;
        }
        if (command == "connectionInfo") {
            onConnectionInfoAck(jsonData);
            msgProcessed = true;
        }
        if (command == "createGroupAck") {
            onCreateGroupAck(jsonData);
            msgProcessed = true;
        }
        if (command == "echoData") {
            onEchoData(jsonData);
            msgProcessed = true;
        }
        if (command == "remotePing") {
            onRemotePing(jsonData);
            msgProcessed = true;
        }
        if (command == "subscribeAck") {
            LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "received subscribeAck" + logLineData, 0);
            msgProcessed = true;
        }
        if (command == "unsubscribeAck") {
            LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "received unsubscribeAck" + logLineData, 0);
            msgProcessed = true;
        }
        if (command == "subscriber") {
            LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "received subscriber" + logLineData, 0);
            onOpenForData();
            msgProcessed = true;
        }
        if (command == "unsubscriber") {
            LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "received unsubscriber" + logLineData, 0);
            onClosedForData();
            msgProcessed = true;
        }
        if (command == "closedForData") {
            LOG_INFO("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "received onClosedForData" + logLineData, 0);
            onClosedForData();
            msgProcessed = true;
        }
    }
    catch (std::exception& e) {
        LOG_WARNING("HyperCubeClientCore::SignallingObject::processSigMsgJson()", "Failed to decode json" + std::string(e.what()), 0);
    }
    return msgProcessed;
}

bool HyperCubeClientCore::SignallingObject::connect(void)
{
    bool stat = pIHyperCubeClientCore->tcpConnect(serverIpAddress, SERVER_PORT);
    return stat;
}

bool HyperCubeClientCore::SignallingObject::onConnect(void)
{
    LOG_STATESTRING("HyperCubeClientCore-state", "connected");
    return true;
}

bool HyperCubeClientCore::SignallingObject::onDisconnect(void)
{
    if (connected) {
        LOG_STATESTRING("HyperCubeClientCore-state", "disconnected");
        connected = false;
        justDisconnected = true;
        eventDisconnectedFromServer.notify();
    }
    return true;
}

bool HyperCubeClientCore::SignallingObject::onOpenForData(void)
{
    LOG_STATESTRING("HyperCubeClientCore-state", "openForData");
    pIHyperCubeClientCore->onOpenForData();
    return true;
}

bool HyperCubeClientCore::SignallingObject::onClosedForData(void)
{
    LOG_STATESTRING("HyperCubeClientCore-state", "closedForData");
    pIHyperCubeClientCore->onClosedForData();
    return true;
}

bool HyperCubeClientCore::SignallingObject::setupConnection(void)
{
    //sendEcho();
    sendConnectionInfo("Matrix");
    createGroup("TeamPegasus");
    localPing();
    LOG_INFO("HyperCubeClientCore::SignallingObject::setupConnection()", "done setup", 0);
    return true;
}


bool HyperCubeClientCore::SignallingObject::echoData(std::string echoData)
{
    if (echoData.length() == 0) echoData = "echoDataData";
    json j = {
        { "command", "echoData" },
        { "data", echoData }
    };

    string command = j.dump();
    MsgCmd msgCmd(command);
    LOG_INFO("HyperCubeClientCore::echoData()", "", 0);
    return sendMsgOut(msgCmd);
}

bool HyperCubeClientCore::SignallingObject::localPing(bool ack, std::string data)
{
    json j = {
        { "command", "localPing" },
        { "ack", ack },
        { "data", data }
    };

    string command = j.dump();
    SigMsg msgCmd(command);
    LOG_INFO("HyperCubeClientCore::localPing()", "", 0);
    return sendMsgOut(msgCmd);
}

bool HyperCubeClientCore::SignallingObject::remotePing(bool ack, std::string data)
{
    json j = {
        { "command", "remotePing" },
        { "ack", ack },
        { "data", data }
    };

    string command = j.dump();
    SigMsg msgCmd(command);
    LOG_INFO("HyperCubeClientCore::remotePing()", "", 0);
    return sendMsgOut(msgCmd);
}


bool HyperCubeClientCore::SignallingObject::publish(void)
{
    string command; 
    uint64_t _groupId = 1;

    json j = {
        { "command", "publish" },
        { "groupId", _groupId }
    };

    //cout << to_string(connectionId);
    command = j.dump();

    //cout << "Send Publish sid:" << to_string(connectionId) << " gid:" << to_string(_groupId) << "\n";

    SigMsg signallingMsg(command);
    LOG_INFO("HyperCubeClientCore::publish()", "", 0);
    return sendMsgOut(signallingMsg);
}

bool HyperCubeClientCore::SignallingObject::sendConnectionInfo(std::string _connectionName)
{
/*
    char capplicationInstanceUUID[17];
    char* p = (char*)&applicationInstanceUUID;
    for (int i = 0; i < 16; i++) {
        capplicationInstanceUUID[i] = *p;
    }
    capplicationInstanceUUID[16]=0;
    string sapplicationInstanceUUID = capplicationInstanceUUID;
*/
    json jconnectionInfo = connectionInfo.to_json();
    json j = {
        { "command", "connectionInfo" },
        { "connectionInfo", jconnectionInfo }
    };

    string command = j.dump();
    SigMsg signallingMsg(command);
    LOG_INFO("HyperCubeClientCore::sendConnectionInfo()", "", 0);
    return sendMsgOut(signallingMsg);
}

bool HyperCubeClientCore::SignallingObject::createGroup(std::string _groupName)
{
    string command;
    json j = {
        { "command", "createGroup" },
        { "groupName", _groupName }
    };
    command = j.dump();

    SigMsg signallingMsg(command);
    LOG_INFO("HyperCubeClientCore::createGroup()", "", 0);
    return sendMsgOut(signallingMsg);
}

bool HyperCubeClientCore::SignallingObject::subscribe(std::string _groupName)
{
    string command; 
    uint64_t _groupId = 1;

    json j = {
        { "command", "subscribe" },
        { "groupName", _groupName }
    };

    command = j.dump();
    SigMsg signallingMsg(command);
    LOG_INFO("HyperCubeClientCore::subscribe()", "", 0);
    return sendMsgOut(signallingMsg);
}

// ------------------------------------------------------------------------------------------------

HyperCubeClientCore::HyperCubeClientCore() :
    IHyperCubeClientCore{ client },
    signallingObject{ this },
    receiveActivity{ this, signallingObject },
    sendActivity{ this }
{
};

HyperCubeClientCore::~HyperCubeClientCore() {

};

bool HyperCubeClientCore::init(std::string _serverIpAddress, bool reInit)
{
    receiveActivity.init();
    sendActivity.init();
    signallingObject.init(_serverIpAddress);
    return true;
}

bool HyperCubeClientCore::deinit(void)
{
    signallingObject.deinit();
    client.close();
    receiveActivity.deinit();
    sendActivity.deinit();
    return true;
};


/*
bool HyperCubeClientCore::sendOut(Packet::UniquePtr& ppacket)
{
    Packet* packet = ppacket.get();

    if (writePacketBuilder.empty()) {
        writePacketBuilder.addNew(*packet);
    } else return false;

    int numSent = client.send(writePacketBuilder.getpData(), writePacketBuilder.getLength());
    totalBytesSent += numSent;
    bool allSent = writePacketBuilder.setNumSent(numSent);

    if (numSent!=packet->getLength()) return false;

    return allSent;
}
*/

bool HyperCubeClientCore::onConnect(void)
{
    std::string line = "connected on socket# " + std::to_string(client.getSocket());
    LOG_INFO("HyperCubeClientCore::onConnect()", line, 0);
    signallingObject.onConnect();
    receiveActivity.onConnect();
    sendActivity.onConnect();
    return true;
}

bool HyperCubeClientCore::onDisconnect(void)
{
    std::string line = "disconnect on socket# " + std::to_string(client.getSocket());
    LOG_WARNING("HyperCubeClientCore::onDisconnect()", line, 0);
    client.close();
    signallingObject.onDisconnect();
    receiveActivity.onDisconnect();
    sendActivity.onDisconnect();
    onClosedForData();
    return true;
}

bool HyperCubeClientCore::onOpenForData(void)
{
    return true;
}

bool HyperCubeClientCore::onClosedForData(void)
{
    return true;
}

bool HyperCubeClientCore::isSignallingMsg(std::unique_ptr<Packet>& rppacket)
{
    return signallingObject.isSignallingMsg(rppacket);
}

bool HyperCubeClientCore::onReceivedData(void)
{
    LOG_STATEINT("HyperCubeClientCore-numInputMsgs", ++numInputMsgs);
    return true;
}


bool HyperCubeClientCore::sendMsgOut(Msg& msg) {
    Packet::UniquePtr ppacket = 0;
    ppacket = Packet::create();
    mserdes.msgToPacket(msg, ppacket);
    bool stat = sendActivity.sendOut(ppacket);
    LOG_STATEINT("HyperCubeClientCore-numOutputMsgs", ++numOutputMsgs);
    return stat;
}

/*
bool HyperCubeClientCore::peekMsg(Msg& msg) {
    if (inPacketQ.size()<=0) return false;
    mserdes.packetToMsg(inPacketQ.front().get(), msg);
    return true;
}
*/

/*
bool HyperCubeClientCore::recvMsg(Msg& msg) {
    Packet::UniquePtr ppacket = 0;
    bool stat = receiveActivity.receiveIn(ppacket);
    if (stat) {
        mserdes.packetToMsg(ppacket.get(), msg);
    }
    return stat;
}
*/
bool HyperCubeClientCore::getPacket(Packet& packet) 
{
    Packet::UniquePtr ppacket = 0;
    bool stat = receiveActivity.receiveIn(ppacket);
    if (stat) packet = std::move(*ppacket);
    return stat;
}

/*
bool HyperCubeClientCore::printRcvdMsgCmds(std::string sentString) {
    bool stat = false;
    while (!PacketQ.isEmpty()) {
        MsgCmd msgCmd("");
        mserdes.packetToMsg(inPacketQ.front().get(), msgCmd);
        std::cout << "Received cmdString: " + msgCmd.jsonData + "\n";
        if(sentString.size()>0) {
            if (msgCmd.jsonData!=sentString) {
                std::cout << "Error strings did not match " + msgCmd.jsonData +  " vs " + sentString + "\n";
            }
        }
        inPacketQ.pop_front();
    }
    return stat;
}


bool HyperCubeClientCore::processInputMsgs(std::string sentString) {
    bool stat = false;
    while (inPacketQ.size()>0) {
        MsgCmd msgCmd("");
        Packet::UniquePtr& rppacket = inPacketQ.front();
        assert(mserdes.packetToMsg(rppacket.get(), msgCmd));
        bool msgProcessed = false;
        switch(msgCmd.subSys) {
            case SUBSYS_CMD:
                switch(msgCmd.command) {
                    case CMD_JSON:
                    {
                        MsgJson msgJson;
                        json jsonData;
                        assert(mserdes.packetToMsgJson(rppacket.get(), msgJson, jsonData));
                        if (jsonData["command"]=="echoData") {
                            std::string data = jsonData["data"];
                            jsonData["command"]="echoAck";
                            MsgCmd msgCmd(jsonData.dump());
                            stat = sendMsgOut(msgCmd);
                            std::cout << "received " + mserdes.to_string(msgCmd) + jsonData.dump() + "\n";
                            msgProcessed = true;
                            break;
                        }
                        if (jsonData["command"]=="echoAck") {
                            std::string data = jsonData["data"];
                            std::cout << "received " + mserdes.to_string(msgCmd) + jsonData.dump() + "\n";
                            msgProcessed = true;
                            break;
                        }
                    }
                    break;
                }
            break;
        }
        if (!msgProcessed) cout << "ERROR! Unknown msg received " + mserdes.to_string(msgCmd) + "\n";
        inPacketQ.pop_front();
    }
    return stat;
}
*/

/*
bool HyperCubeClientCore::processConnectionEvents(void)
{
    struct pollfd pollFds[1];
    memset(pollFds, 0, sizeof(pollfd));
    int numFds = 1;

    pollFds[0].fd = getSocket();
    LOG_ASSERT(pollFds[0].fd > 0);
    pollFds[0].events = POLLOUT | POLLIN;
    pollFds[0].revents = 0;

    //int res = poll(pollFds, numFds, -1);
    /// set a timeout for this poll, so that it will exit after this timeout and loop around again.
    /// This is so that, if new sockets are added while the current poll is blocked, any data sent on the new sockets will not be
    /// received until, the current poll is unblocked and a new poll is started with the new sockets in pollFds.
    /// So, we timeout every 1 second, so that if any new sockets are added during a poll that is blocked, it will exit and
    /// come around again with the new socket added in pollFds.
    int res = poll(pollFds, numFds, 1000);
    if (res < 0) {
        LOG_WARNING("HyperCubeClientCore::processConnectionEvents()", "poll returned - 1", res);
        return false;
    }
    if ((pollFds[0].fd > 0) && (pollFds[0].revents != 0)) { // if not being ignored
    // check socket index match. It should even if a new connection was added.
        int revents = pollFds[0].revents;
        if ((revents & POLLRDNORM) || (revents & POLLRDBAND)) {
            recvPackets();
        }
        if (revents & POLLWRNORM) {
            writePackets();
        }
        if (revents & POLLPRI) {
            LOG_INFO("HyperCubeClientCore::processConnectionEvents(), poll returned POLLPRI", pollFds[0].revents);
        }
        if ((revents & POLLHUP) || (revents & POLLERR) || (revents & POLLNVAL)) {
            connectionClosed();
            LOG_INFO("HyperCubeClientCore::processConnectionEvents(), TCP connection close", (int)pollFds[0].fd);
        }
    }
    return true;
};
*/


/*
bool HyperCubeClient::doShell(void)
{
    client.init();

    if (!client.connect(serverIpAddress, SERVER_PORT)) {
        std::cout << "HyperCubeClient(): Server not available\n\r";
        return false;
    }
    bool exitNow = false;

    std::cout << "Client Interactive Mode\n\r";
    cout << "q/ESC - quit, x - exit, e - echo, s - send, r - recv, l - echo loop\n\r";
#ifdef _WIN64
    DWORD processID = GetCurrentProcessId();
    cout << "ProcessId: " << processID << endl;
#endif

    std::string dataString = "Hi There:";

    Msg inMsg;
    int msgNum = 0;

    while (!exitNow) {
        if (kbhit()) {
            char ch = getchar();
            switch (ch) {
            case 27:
            case 'q':
                exitNow = true;
                break;
            case 'p':
                publish();
                break;
            case 's':
                subscribe();
                break;
            case 'c':
                createGroup("Test groupName ");
                break;
            case 'r':
            {
                cout << "Sent SEND\n";
                string command = "SEND "; command += dataString + std::to_string(msgNum++);
                MsgCmd cmdMsg(command);
                sendMsgOut(cmdMsg);
            }
            break;
            case 'e':
                sendEcho();
                break;
            case 'x':
            {
                exitNow = true;
                MsgCmd cmdMsg("EXIT");
                sendMsgOut(cmdMsg);
                usleep(100000);
            }
            break;
            case 'l':
            {
                string command = "ECHO"; command += to_string(100) + ",";
                while (true) {
                    MsgCmd cmdMsg(command);
                    sendMsgOut(cmdMsg);
                    usleep(100000);
                }
            }
            break;
            }

        }

        recvPackets();

        printRcvdMsgCmds("");

        usleep(10000);
    }

    client.deinit();
    return true;
}


*/

// ------------------------------------------------------------

HyperCubeClient::HyperCubeClient() :
    HyperCubeClientCore{}
{
    std::srand((unsigned int)std::time(nullptr));
    signallingObject.connectionId = std::rand();
}

HyperCubeClient::~HyperCubeClient()
{
}
