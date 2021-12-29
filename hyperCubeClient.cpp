#include <stdio.h>

#include "Logger.h"
#include "hyperCubeClient.h"
#include "Common.h"
#include "Packet.h"
#include "mserdes.h"
#include "kbhit.h"
#include "clockGetTime.h"

#include "json.hpp"
using json = nlohmann::json;

using namespace std;

HyperCubeClient::HyperCubeClient() :
    recvPacketBuilder(*this, COMMON_PACKETSIZE_MAX),
    threadSafeWritePacketBuilder(COMMON_PACKETSIZE_MAX)
{
    std::srand((unsigned int)std::time(nullptr));
    systemId = std::rand();
};

HyperCubeClient::~HyperCubeClient() {

};

bool HyperCubeClient::init(std::string _serverIpAddress) {
    serverIpAddress = _serverIpAddress;
    threadSafeWritePacketBuilder.init();
    recvPacketBuilder.reset();
    pinputPacket = std::make_unique<Packet>();

    return true;
}

bool HyperCubeClient::deinit(void)
{
    return true;
};

bool HyperCubeClient::connect(std::string _serverIpAddress) {
    serverIpAddress = _serverIpAddress;
    return client.connect(serverIpAddress, SERVER_PORT);
}

int HyperCubeClient::readData(void* pdata, int dataLen)
{
    return client.recv((char*)pdata, dataLen);
}

bool HyperCubeClient::sendPacket(Packet::UniquePtr& ppacket) {

    Packet* packet = ppacket.get();

    if (threadSafeWritePacketBuilder.empty()) {
        threadSafeWritePacketBuilder.addNew(*packet);
    } else return false;

    int numSent = client.send(threadSafeWritePacketBuilder.getpData(), threadSafeWritePacketBuilder.getLength());
    totalBytesSent += numSent;
    bool allSent = threadSafeWritePacketBuilder.setNumSent(numSent);

    if (numSent!=packet->getLength()) return false;

    return allSent;
}

bool HyperCubeClient::recvPackets(void) {
    bool stat = false;
    while(client.isDataAvailable()) {
        if (recvPacketBuilder.readPacket(*pinputPacket)) {
            inPacketQ.push_back(std::move(pinputPacket));
            pinputPacket = std::make_unique<Packet>();
            stat = true;
        }
    }
    return stat;
}

bool HyperCubeClient::sendMsg(Msg& msg) {
    Packet::UniquePtr ppacket = 0;
    ppacket = Packet::create();
    mserdes.msgToPacket(msg, ppacket);
    return sendPacket(ppacket);
}

bool HyperCubeClient::peekMsg(Msg& msg) {
    if (inPacketQ.size()<=0) return false;
    mserdes.packetToMsg(inPacketQ.front().get(), msg);
    return true;
}

bool HyperCubeClient::recvMsg(Msg& msg) {
    if (!peekMsg(msg)) return false;
    inPacketQ.pop_front();
    return true;
}

bool HyperCubeClient::printRcvdMsgCmds(std::string sentString) {
    bool stat = false;
    while (inPacketQ.size()>0) {
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

bool HyperCubeClient::processInputMsgs(std::string sentString) {
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
                            stat = sendMsg(msgCmd);
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

bool HyperCubeClient::sendEcho(void) 
{
    std::string echoData = "";
    json j = {
        { "command", "echoData" },
        { "data", echoData }
    };

    cout << "Send Echo " << echoData << "\n";
    string command = j.dump();
    MsgCmd msgCmd(command);
    return sendMsg(msgCmd);
}


bool HyperCubeClient::publish(void) 
{
    string command; 
    uint64_t _groupId = 1;

    json j = {
        { "command", "publish" },
        { "systemId", systemId },
        { "groupId", _groupId }
    };

    cout << to_string(systemId);
    command = j.dump();

    cout << "Send Publish sid:" << to_string(systemId) << " gid:" << to_string(_groupId) << "\n";

    SigMsg signallingMsg(command);
    return sendMsg(signallingMsg);
}

bool HyperCubeClient::createGroup(std::string _groupName) 
{
    string command; 
    json j = {
        { "command", "createGroup" },
        { "systemId", systemId },
        { "groupName", _groupName }
    };

    cout << "Send CreateGroup sid:" << to_string(systemId) << " gin:" << _groupName << "\n";

    cout << to_string(systemId);
    command = j.dump();

    SigMsg signallingMsg(command);
    return sendMsg(signallingMsg);
}


bool HyperCubeClient::subscribe(void) 
{
    string command; 
    uint64_t _groupId = 1;

    json j = {
        { "command", "subscribe" },
        { "systemId", systemId },
        { "groupId", _groupId }
    };

    cout << "Send Subscribe sid:" << to_string(systemId) << " gid:" << to_string(_groupId) << "\n";
    command = j.dump();
    SigMsg signallingMsg(command);
    return sendMsg(signallingMsg);
}



