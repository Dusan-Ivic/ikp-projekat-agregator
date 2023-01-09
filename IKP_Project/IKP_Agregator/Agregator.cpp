#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include <conio.h>
#include "RingBuffer/RingBuffer.h"

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "5000"
#define MAX_SOCKET_LIMIT 100

#define SAFE_CLOSE_HANDLE(handle) if(handle) { CloseHandle(handle); }

SOCKET acceptedSockets[MAX_SOCKET_LIMIT];
int socketIndex = 0;

// Variable used to store function return value
int iResult;

struct ThreadData {
    SOCKET socket;
    char buffer[DEFAULT_BUFLEN];
    int instancePort;
    Message* message;
};

RingBuffer ImportantBuffer;
RingBuffer StandardBuffer;

CRITICAL_SECTION ImportantBufferAccess;
CRITICAL_SECTION StandardBufferAccess;
CRITICAL_SECTION ConsoleAccess;

HANDLE ImportantBufferEmpty;
HANDLE ImportantBufferFull;
HANDLE StandardBufferEmpty;
HANDLE StandardBufferFull;
HANDLE FinishSignal;

bool InitializeWindowsSockets();
SOCKET InitializeAgregatorAsServer(int port);
SOCKET InitializeAgregatorAsClient(int port);
DWORD WINAPI acceptSockets(LPVOID lpParam);
DWORD WINAPI receiveMessages(LPVOID lpParam);
DWORD WINAPI sendImportant(LPVOID lpParam);
DWORD WINAPI sendStandard(LPVOID lpParam);

int main(void)
{
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

    printf("==========================================\n");

    // Create all semaphores
    ImportantBufferEmpty = CreateSemaphore(0, RING_SIZE, RING_SIZE, NULL);
    ImportantBufferFull = CreateSemaphore(0, 0, RING_SIZE, NULL);
    StandardBufferEmpty = CreateSemaphore(0, RING_SIZE, RING_SIZE, NULL);
    StandardBufferFull = CreateSemaphore(0, 0, RING_SIZE, NULL);
    FinishSignal = CreateSemaphore(0, 0, 4, NULL);

    // Create all threads
    DWORD SocketAcceptorID, ReceiverID, ImportantSenderID, StandardSenderID;
    HANDLE hSocketAcceptor, hReceiver, hImportantSender, hStandardSender;

    if (ImportantBufferEmpty && ImportantBufferFull &&
        StandardBufferEmpty && StandardBufferFull &&
        FinishSignal)
    {
        InitializeCriticalSection(&ImportantBufferAccess);
        InitializeCriticalSection(&StandardBufferAccess);
        InitializeCriticalSection(&ConsoleAccess);

        // Thread za prihvatanje socketa
        ThreadData acceptSocketsThreadData;
        acceptSocketsThreadData.socket = receiverSocket;
        hSocketAcceptor = CreateThread(NULL, 0, &acceptSockets, &acceptSocketsThreadData, 0, &SocketAcceptorID);

        // Thread za primanje podataka sa prethodne instance
        ThreadData receiveMessagesThreadData;
        receiveMessagesThreadData.socket = receiverSocket;
        hReceiver = CreateThread(NULL, 0, &receiveMessages, &receiveMessagesThreadData, 0, &ReceiverID);

        // Thread za slanje hitnih poruka na sledecu instancu
        ThreadData sendImportantThreadData;
        sendImportantThreadData.socket = senderSocket;
        sendImportantThreadData.instancePort = nextInstancePort;
        hImportantSender = CreateThread(NULL, 0, &sendImportant, &sendImportantThreadData, 0, &ImportantSenderID);

        // Thread za slanje standardnih poruka na sledecu instancu
        ThreadData sendStandardThreadData;
        sendStandardThreadData.socket = senderSocket;
        sendStandardThreadData.instancePort = nextInstancePort;
        hStandardSender = CreateThread(NULL, 0, &sendStandard, &sendStandardThreadData, 0, &StandardSenderID);
    
        if (!hSocketAcceptor || !hReceiver || !hImportantSender || !hStandardSender)
        {
            ReleaseSemaphore(FinishSignal, 4, NULL);
        }

        if (hSocketAcceptor)
        {
            WaitForSingleObject(hSocketAcceptor, INFINITE);
        }

        if (hReceiver)
        {
            WaitForSingleObject(hReceiver, INFINITE);
        }

        if (hImportantSender)
        {
            WaitForSingleObject(hImportantSender, INFINITE);
        }

        if (hStandardSender)
        {
            WaitForSingleObject(hStandardSender, INFINITE);
        }
    }

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
    for (int i = 0; i < socketIndex; i++)
    {
        closesocket(acceptedSockets[i]);
    }

    SAFE_CLOSE_HANDLE(hSocketAcceptor);
    SAFE_CLOSE_HANDLE(hReceiver);
    SAFE_CLOSE_HANDLE(hImportantSender);
    SAFE_CLOSE_HANDLE(hStandardSender);
    SAFE_CLOSE_HANDLE(ImportantBufferEmpty);
    SAFE_CLOSE_HANDLE(ImportantBufferFull);
    SAFE_CLOSE_HANDLE(StandardBufferEmpty);
    SAFE_CLOSE_HANDLE(StandardBufferFull);
    SAFE_CLOSE_HANDLE(FinishSignal);

    DeleteCriticalSection(&ImportantBufferAccess);
    DeleteCriticalSection(&StandardBufferAccess);
    DeleteCriticalSection(&ConsoleAccess);

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

DWORD WINAPI acceptSockets(LPVOID lpParam)
{
    ThreadData acceptSocketsThreadData = *(ThreadData*)lpParam;

    while (true)
    {
        SOCKET acceptedSocket = accept(acceptSocketsThreadData.socket, NULL, NULL);

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

DWORD WINAPI receiveMessages(LPVOID lpParam)
{
    ThreadData receiveMessagesThreadData = *(ThreadData*)lpParam;

    while (true)
    {
        for (int i = 0; i < socketIndex; i++)
        {
            iResult = recv(acceptedSockets[i], receiveMessagesThreadData.buffer, DEFAULT_BUFLEN, 0);

            if (iResult > 0)
            {
                // TODO - Naredni deo koda (unutar iResult > 0)
                // obraditi u novoj niti preko processMessage funkcije
                // u nit kao parametar poslati receiveMessagesThreadData zbog citanja buffera
                // Nit ce aktivirati jedan od dva semafora da bi se pokrenulo slanje

                Message message = *(Message*)receiveMessagesThreadData.buffer;

                EnterCriticalSection(&ConsoleAccess);
                printf("==========================================\n");
                printf("Received from previous instance\n");
                printf("[%s]: %d\n", message.isImportant ? "IMPORTANT" : "STANDARD", message.value);
                LeaveCriticalSection(&ConsoleAccess);

                if (message.isImportant)
                {
                    EnterCriticalSection(&ImportantBufferAccess);

                    addMessageToBuffer(&ImportantBuffer, message);

                    LeaveCriticalSection(&ImportantBufferAccess);

                    ReleaseSemaphore(ImportantBufferFull, 1, NULL);
                }
                else
                {
                    EnterCriticalSection(&StandardBufferAccess);

                    addMessageToBuffer(&StandardBuffer, message);

                    LeaveCriticalSection(&StandardBufferAccess);

                    ReleaseSemaphore(StandardBufferFull, 1, NULL);
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

    return 0;
}

DWORD WINAPI sendImportant(LPVOID lpParam)
{
    ThreadData sendImportantThreadData = *(ThreadData*)lpParam;

    const int semaphoreNum = 2;
    HANDLE semaphores[semaphoreNum] = { FinishSignal, ImportantBufferFull };

    while (WaitForMultipleObjects(semaphoreNum, semaphores, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        EnterCriticalSection(&ImportantBufferAccess);

        Message message = getMessageFromBuffer(&ImportantBuffer);

        LeaveCriticalSection(&ImportantBufferAccess);

        //ReleaseSemaphore(ImportantBufferEmpty, 1, NULL);

        EnterCriticalSection(&ConsoleAccess);
        printf("==========================================\n");
        printf("Forwarding IMPORTANT message to %s\n", sendImportantThreadData.instancePort == 5000 ? "DESTINATION" : "AGREGATOR");
        printf("= %d\n", message.value);
        LeaveCriticalSection(&ConsoleAccess);

        iResult = send(sendImportantThreadData.socket, (const char*)&message, sizeof(message), 0);
        if (iResult == SOCKET_ERROR)
        {
            printf("send failed with error: %d\n", WSAGetLastError());
            closesocket(sendImportantThreadData.socket);
            WSACleanup();
            return 1;
        }
    }

    return 0;
}

DWORD WINAPI sendStandard(LPVOID lpParam)
{
    ThreadData sendStandardThreadData = *(ThreadData*)lpParam;

    const int semaphoreNum = 2;
    HANDLE semaphores[semaphoreNum] = { FinishSignal, StandardBufferFull };

    while (WaitForMultipleObjects(semaphoreNum, semaphores, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        Sleep(3000); // Samo za testiranje 3s; posle promeniti u 1s

        EnterCriticalSection(&ConsoleAccess);
        printf("==========================================\n");
        printf("Forwarding STANDARD messages to %s\n", sendStandardThreadData.instancePort == 5000 ? "DESTINATION" : "AGREGATOR");

        EnterCriticalSection(&StandardBufferAccess);

        while (!isBufferEmpty(&StandardBuffer))
        {
            Message message = getMessageFromBuffer(&StandardBuffer);

            printf("= %d\n", message.value);

            iResult = send(sendStandardThreadData.socket, (const char*)&message, sizeof(message), 0);
            if (iResult == SOCKET_ERROR)
            {
                printf("send failed with error: %d\n", WSAGetLastError());
                closesocket(sendStandardThreadData.socket);
                WSACleanup();
                return 1;
            }

            //ReleaseSemaphore(StandardBufferEmpty, 1, NULL);
        }

        LeaveCriticalSection(&StandardBufferAccess);

        LeaveCriticalSection(&ConsoleAccess);
    }
    
    return 0;
}
