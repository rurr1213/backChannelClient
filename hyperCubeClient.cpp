#include <stdio.h>

#include "hyperCubeClient.h"
#include "Common.h"
#include "Packet.h"
#include "mserdes.h"
#include "kbhit.h"
#include "clockGetTime.h"
#include "Logger.h"

#include "json.hpp"
using json = nlohmann::json;

using namespace std;

HyperCubeClient::HyperCubeClient() :
    recvPacketBuilder(*this, COMMON_PACKETSIZE_MAX),
    threadSafeWritePacketBuilder(COMMON_PACKETSIZE_MAX)
{
    //std::srand(std::time(nullptr));
    connectionId = std::rand();
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
        if (recvPacketBuilder.readPacket(*pinputPacket)==RecvPacketBuilder::READSTATUS::NEEDEDDATAREAD) {
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

bool HyperCubeClient::doEchoTest(void) 
{
    cout << "Echo Test \n";

    // TODO increase this to 10000 to cause errors
    int dataStringBytes = 1000;
    for(int i=0; i< dataStringBytes; i++) {
        dataString += "D";
    }

    static int msgNum=0;

    ClockGetTime cgt;
    double numTests = 50;
    static int totalTests = 0;
    for(int i=0; i<(int)numTests; i++) {
        cgt.start();
        string command = "ECHO"; command += dataString + std::to_string(msgNum++);
        MsgCmd cmdMsg(command);
        sendMsg(cmdMsg);
        while(!recvPackets()) {
            usleep(1);
        }
        cgt.end();
        totalTime += cgt.change();
        printRcvdMsgCmds(command);
    }
    totalTests += numTests;
    double avgTime = (totalTime/totalTests)*1000000;
    double avgBytes = (totalBytesSent/totalTests);
    double totalBPS = ((double) (totalBytesSent*8) / totalTime)/1000000.0;
 //   LOG_INFO("HyperCubeClient::doEchoTest()", "Avg time per test (us): " + std::to_string(avgTime), 0);
 //   LOG_INFO("HyperCubeClient::doEchoTest()", "Avg bytes per test : " + std::to_string((int)avgBytes), 0);
 //   LOG_INFO("HyperCubeClient::doEchoTest()", "MBps: " + std::to_string(totalBPS), 0);
    return true;
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

bool HyperCubeClient::sendLocalPing(void) 
{
    std::string pingData = "";
    json j = {
        { "command", "localPing" },
        { "connectionId", connectionId },
        { "data", pingData }
    };

    cout << "Send Local Ping " << pingData << "\n";
    string command = j.dump();
    SigMsg msgCmd(command);
    return sendMsg(msgCmd);
}


bool HyperCubeClient::publish(void) 
{
    string command; 
    uint64_t _groupId = 1;

    json j = {
        { "command", "publish" },
        { "connectionId", connectionId },
        { "groupId", _groupId }
    };

    cout << to_string(connectionId);
    command = j.dump();

    cout << "Send Publish sid:" << to_string(connectionId) << " gid:" << to_string(_groupId) << "\n";

    SigMsg signallingMsg(command);
    return sendMsg(signallingMsg);
}

bool HyperCubeClient::createGroup(std::string _groupName) 
{
    string command; 
    json j = {
        { "command", "createGroup" },
        { "connectionId", connectionId },
        { "groupName", _groupName }
    };

    cout << "Send CreateGroup sid:" << to_string(connectionId) << " gin:" << _groupName << "\n";

    cout << to_string(connectionId);
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
        { "connectionId", connectionId },
        { "groupId", _groupId }
    };

    cout << "Send Subscribe sid:" << to_string(connectionId) << " gid:" << to_string(_groupId) << "\n";
    command = j.dump();
    SigMsg signallingMsg(command);
    return sendMsg(signallingMsg);
}


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
    int msgNum=0;

    while (!exitNow) {
        if(kbhit()) {
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
                        sendMsg(cmdMsg);
                    }
                    break;
                case 't':
                    sendLocalPing();
                    break;
                case 'e':
                    sendEcho();
//                    doEchoTest();
                    /*
                    {
                        cout << "Sent ";
                        string command = "ECHO"; command += dataString + std::to_string(msgNum++);
                        MsgCmd cmdMsg(command);
                        sendMsg(cmdMsg);
                    }*/
                    break;
                case 'x':
                    {
                        exitNow = true;
                        MsgCmd cmdMsg("EXIT");
                        sendMsg(cmdMsg);
                        usleep(100000);
                    }
                    break;
                case 'l':
                    {
                        string command = "ECHO"; command += to_string(100) + ",";
                        while (true) {
                            MsgCmd cmdMsg(command);
                            sendMsg(cmdMsg);
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

