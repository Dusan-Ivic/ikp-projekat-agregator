#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <WIndows.h>
#include <conio.h>

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "5000"
#define MAX_SOCKET_LIMIT 100

SOCKET acceptedSockets[MAX_SOCKET_LIMIT];
int socketIndex = 0;

// Variable used to store function return value
int iResult;
HANDLE importantSemaphore;
HANDLE standardSemaphore;
CRITICAL_SECTION csBufferImp;
CRITICAL_SECTION csBufferStd;

struct Message {
    int value;
    bool isImportant;
};
Message buffStandard[500 * sizeof(Message)];
Message buffImportant[500 * sizeof(Message)];
int countStd = 0;
int countImp = 0;

struct SocketAcceptorThreadData {
    SOCKET listeningSocket;
};

struct Structure
{
    SOCKET senderSocketStruct;
    char recvbuf[DEFAULT_BUFLEN];
    int nextInstancePortStruct;
    Message* msg;
};
Structure recvStructInit;

bool InitializeWindowsSockets();
SOCKET InitializeAgregatorAsServer(int port);
SOCKET InitializeAgregatorAsClient(int port);
DWORD WINAPI socketAcceptor(LPVOID lpParam);
DWORD WINAPI  recvMsg(LPVOID lpParam);
DWORD WINAPI  sendImportant(LPVOID lpParam);
DWORD WINAPI  sendStandard(LPVOID lpParam);

int main(void)
{
    InitializeCriticalSection(&csBufferImp);
    InitializeCriticalSection(&csBufferStd);

    int currentInstanceID;
    printf("Current instance ID (integer): ");
    scanf("%d", &currentInstanceID);

    // Na primer, ID = 3: Agregator3 se pokreæe na portu 5003
    int currentInstancePort = 5000 + currentInstanceID;

    char nextInstanceType[DEFAULT_BUFLEN];
    printf("Next instance type (Agregator or Destination): \n");
    scanf("%s", nextInstanceType);

    int nextInstancePort;

    if (strcmp(nextInstanceType, "Destination") == 0)
    {
        // Ako je Agregator zakacen na Destination

        nextInstancePort = 5000;
    }
    else if (strcmp(nextInstanceType, "Agregator") == 0)
    {
        // Ako je Agregator zakacen na drugi Agregator

        int nextInstanceID;
        printf("Next instance ID (integer): ");
        scanf("%d", &nextInstanceID);

        nextInstancePort = 5000 + nextInstanceID;
    }
    else
    {
        printf("Wrong instance type!");
        return 1;
    }

    // Socket preko kojeg æe agregator slati poruke na višu instancu (na koju je nakaèen)
    SOCKET senderSocket = InitializeAgregatorAsClient(nextInstancePort);

    // Socket preko kojeg æe agregator primati poruke od nižih instanci
    SOCKET receiverSocket = InitializeAgregatorAsServer(currentInstancePort);

    // Socket used for connecting to new client
    SOCKET acceptedSocket = INVALID_SOCKET;

    // Buffer used for storing incoming data
    char recvbuf[DEFAULT_BUFLEN];

    SocketAcceptorThreadData data;
    data.listeningSocket = receiverSocket;

    DWORD SocketAcceptorID;
    HANDLE hSocketAcceptor = CreateThread(NULL, 0, &socketAcceptor, &data, 0, &SocketAcceptorID);

    printf("==========================================\n");

    // Struct for recv thread
    recvStructInit.senderSocketStruct = senderSocket;
    recvStructInit.nextInstancePortStruct = nextInstancePort;

    // Thread for recv
    DWORD recvID, importantMsgId, standardMsgId;
    HANDLE hrecv, hImportantMsg, hStandardMsg;
    //PTP_POOL test;
    //test = CreateThreadpool(hrecv);

    importantSemaphore = CreateSemaphore(0, 0, 1, NULL);
    standardSemaphore = CreateSemaphore(0, 0, 1, NULL);

    hrecv = CreateThread(NULL, 0, &recvMsg, &recvStructInit, 0, &recvID);

    hImportantMsg = CreateThread(NULL, 0, &sendImportant, &recvStructInit, 0, &importantMsgId);
    hStandardMsg = CreateThread(NULL, 0, &sendStandard, &recvStructInit, 0, &standardMsgId);

    _getch();

    // Shut down the connection
    iResult = shutdown(acceptedSocket, SD_SEND);
    if (iResult == SOCKET_ERROR)
    {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(acceptedSocket);
        WSACleanup();
        return 1;
    }

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    closesocket(acceptedSocket);
    closesocket(acceptedSockets[socketIndex]);

    DeleteCriticalSection(&csBufferImp);
    DeleteCriticalSection(&csBufferStd);

    //CloseThreadpool(test);
    WSACleanup();

    return 0;
}



bool InitializeWindowsSockets()
{
    WSADATA wsaData;
    // Initialize windows sockets library for this process
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("WSAStartup failed with error: %d\n", WSAGetLastError());
        return false;
    }
    return true;
}
SOCKET InitializeAgregatorAsServer(int port)
{
    // Socket used for listening for new clients 
    SOCKET listeningSocket = INVALID_SOCKET;

    if (InitializeWindowsSockets() == false)
    {
        return 1;
    }

    // Prepare address information structures
    addrinfo* resultingAddress = NULL;
    addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4 address
    hints.ai_socktype = SOCK_STREAM; // Provide reliable data streaming
    hints.ai_protocol = IPPROTO_TCP; // Use TCP protocol
    hints.ai_flags = AI_PASSIVE;     // 

    char strPort[DEFAULT_BUFLEN];
    itoa(port, strPort, 10);

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, strPort, &hints, &resultingAddress);
    if (iResult != 0)
    {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a socket for listening
    listeningSocket = socket(AF_INET,      // IPv4 address famly
        SOCK_STREAM,  // stream socket
        IPPROTO_TCP); // TCP

    if (listeningSocket == INVALID_SOCKET)
    {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(resultingAddress);
        WSACleanup();
        return 1;
    }

    // Bind PORT and Local Address to listeningSocket
    iResult = bind(listeningSocket, resultingAddress->ai_addr, (int)resultingAddress->ai_addrlen);
    if (iResult == SOCKET_ERROR)
    {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(resultingAddress);
        closesocket(listeningSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(resultingAddress);

    // Set listeningSocket to listening mode
    iResult = listen(listeningSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR)
    {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(listeningSocket);
        WSACleanup();
        return 1;
    }

    printf("Agregator initialized as server, waiting for requests...\n");

    return listeningSocket;
}
SOCKET InitializeAgregatorAsClient(int port)
{
    // Socket used to communicate with server
    SOCKET connectSocket = INVALID_SOCKET;

    if (InitializeWindowsSockets() == false)
    {
        return 1;
    }

    // Create a socket for connecting
    connectSocket = socket(AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP);

    if (connectSocket == INVALID_SOCKET)
    {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Create and initialize address structure
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddress.sin_port = htons(port);

    // Connect to server specified in serverAddress and connectSocket
    if (connect(connectSocket, (SOCKADDR*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
    {
        printf("Unable to connect to next instance.\n");
        closesocket(connectSocket);
        WSACleanup();
    }

    printf("Agregator connected to next instance!\n");

    return connectSocket;
}
DWORD WINAPI socketAcceptor(LPVOID lpParam)
{
    SocketAcceptorThreadData* data = (SocketAcceptorThreadData*)lpParam;

    while (true)
    {
        SOCKET acceptedSocket = accept(data->listeningSocket, NULL, NULL);

        if (acceptedSocket != INVALID_SOCKET)
        {
            acceptedSockets[socketIndex] = acceptedSocket;

            // Set acceptedSocket to non-blocking mode
            unsigned long mode = 1;
            iResult = ioctlsocket(acceptedSockets[socketIndex], FIONBIO, &mode);
            if (iResult != NO_ERROR)
            {
                printf("ioctlsocket failed with error: %ld\n", iResult);
            }

            socketIndex++;
        }
        else
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
            {
                Sleep(2000);
            }
            else
            {
                printf("accept failed with error: %d\n", WSAGetLastError());
                continue;
            }
        }
    }

    return 0;
}
DWORD WINAPI  recvMsg(LPVOID lpParam)
{
    Structure recvStruct = *(Structure*)lpParam;
    while (true)
    {
        for (int i = 0; i < socketIndex; i++)
        {
            iResult = recv(acceptedSockets[i], recvStruct.recvbuf, DEFAULT_BUFLEN, 0);

            if (iResult > 0)
            {
                // Poruka je primljena bez gresaka
                printf("Received from previous instance\n");

                Message* message = (Message*)recvStruct.recvbuf;
                recvStruct.msg = message;
                printf("[%s]: %d\n", message->isImportant ? "IMPORTANT" : "STANDARD", message->value);


                if (message->isImportant)
                {
                    EnterCriticalSection(&csBufferImp);
                    buffImportant[countImp].isImportant = true;
                    buffImportant[countImp].value = message->value;
                    ReleaseSemaphore(importantSemaphore, 1, NULL);     //upali semafor  importantSemaphore
                    LeaveCriticalSection(&csBufferImp);
                    countImp++;
                }
                else
                {
                    EnterCriticalSection(&csBufferStd);
                    buffStandard[countStd].isImportant = false;
                    buffStandard[countStd].value = message->value;
                    ReleaseSemaphore(standardSemaphore, 1, NULL);     //upali semafor  standardSemaphore
                    LeaveCriticalSection(&csBufferStd);
                    countStd++;
                }
            }
            else if (iResult == 0)
            {
                printf("Connection with previous instance closed.\n");
                closesocket(acceptedSockets[i]);
                continue;
            }
            else
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {

                }
                else
                {
                    closesocket(acceptedSockets[i]);
                    continue;
                }
            }
        }
    }
}
DWORD WINAPI  sendImportant(LPVOID lpParam)
{    
    Structure recvStruct = *(Structure*)lpParam;

    //wait for semaphore
    while (WaitForSingleObject(importantSemaphore,INFINITE)== WAIT_OBJECT_0)
    {   
        // Send data to next instance
        EnterCriticalSection(&csBufferImp);
        Message msg = buffImportant[countImp];

        iResult = send(recvStruct.senderSocketStruct, (const char*)&msg, sizeof(msg), 0);
        if (iResult == SOCKET_ERROR)
        {
            printf("send failed with error: %d\n", WSAGetLastError());
            closesocket(recvStruct.senderSocketStruct);
            WSACleanup();
            return 1;
        }

        printf("Forwarded to %s!\n", recvStruct.nextInstancePortStruct == 5000 ? "DESTINATION" : "AGREGATOR");
        printf("==========================================\n");

        countImp--;
        LeaveCriticalSection(&csBufferImp);
    }    
}
DWORD WINAPI  sendStandard(LPVOID lpParam)
{    
    Structure recvStruct = *(Structure*)lpParam;

    //wait for semaphore
    while (WaitForSingleObject(standardSemaphore, INFINITE) == WAIT_OBJECT_0)
    {
        // Send data to next instance
        EnterCriticalSection(&csBufferStd);
        for (int i = 0; i < countStd; i++) 
        {
            Message msg = buffStandard[i];
            iResult = send(recvStruct.senderSocketStruct, (const char*)&msg, sizeof(msg), 0);

            if (iResult == SOCKET_ERROR)
            {
                printf("send failed with error: %d\n", WSAGetLastError());
                closesocket(recvStruct.senderSocketStruct);
                WSACleanup();
                return 1;
            }

            printf("Forwarded to %s!\n", recvStruct.nextInstancePortStruct == 5000 ? "DESTINATION" : "AGREGATOR");
            printf("==========================================\n");            
        }    
        countStd = 0;
        LeaveCriticalSection(&csBufferStd);
        Sleep(1000);
    }
    
}