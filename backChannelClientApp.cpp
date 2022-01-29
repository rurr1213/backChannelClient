#include <stdio.h>

#include "EConnection.h"
#include "ThreadMgr.h"
#include "tcp.h"
#include "TcpStringClientServer.h"
#include "hyperCubeClient.h"
#include "kbhit.h"

HyperCubeClient client;

int main() {
	printf("BackChannel Client\n");
	printf("------------------\n");

    string ipAddress = "192.168.1.215";

    cout << "Connecting to " << ipAddress << endl;
    client.init(ipAddress);
    client.doShell();

}
