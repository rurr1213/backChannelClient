#include <stdio.h>

#include "EConnection.h"
#include "ThreadMgr.h"
#include "tcp.h"
#include "TcpStringClientServer.h"
#include "hyperCubeClient.h"
#include "kbhit.h"



class HyperCubeClientShell : public HyperCubeClient
{
    bool doShell(void);
    bool doEchoTest(void);
}

bool HyperCubeClientShell::doEchoTest(void)
{
    cout << "Echo Test \n";

    // TODO increase this to 10000 to cause errors
    int dataStringBytes = 1000;
    for (int i = 0; i < dataStringBytes; i++) {
        dataString += "D";
    }

    static int msgNum = 0;

    ClockGetTime cgt;
    double numTests = 50;
    static int totalTests = 0;
    for (int i = 0; i < (int)numTests; i++) {
        cgt.start();
        string command = "ECHO"; command += dataString + std::to_string(msgNum++);
        MsgCmd cmdMsg(command);
        sendMsg(cmdMsg);
        while (!recvPackets()) {
            usleep(1);
        }
        cgt.end();
        totalTime += cgt.change();
        printRcvdMsgCmds(command);
    }
    totalTests += (int)numTests;
    double avgTime = (totalTime / totalTests) * 1000000;
    double avgBytes = ((double)totalBytesSent / (double)totalTests);
    double totalBPS = ((double)(totalBytesSent * 8) / totalTime) / 1000000.0;
    LOG_INFOD("HyperCubeClient::doEchoTest()", "Avg time per test (us): " + std::to_string(avgTime), 0);
    LOG_INFOD("HyperCubeClient::doEchoTest()", "Avg bytes per test : " + std::to_string((int)avgBytes), 0);
    LOG_INFOD("HyperCubeClient::doEchoTest()", "MBps: " + std::to_string(totalBPS), 0);
    return true;
}

bool HyperCubeClientShell::doShell(void)
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
                sendMsg(cmdMsg);
            }
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



HyperCubeClientShell client;

int main() {
	printf("BackChannel Client\n");
	printf("------------------\n");

    string ipAddress = "127.0.0.1";

    cout << "Connecting to " << ipAddress << endl;
    client.init(ipAddress);
    client.doShell();

}
