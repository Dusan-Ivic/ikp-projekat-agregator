#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "5000"
#define MAX_SOCKET_LIMIT 100

#define SAFE_CLOSE_HANDLE(handle) if(handle) { CloseHandle(handle); }

SOCKET acceptedSockets[MAX_SOCKET_LIMIT];
int socketIndex = 0;

int totalSum = 0;

// Variable used to store function return value
int iResult;

struct Message {
    int value;
    bool isImportant;
};

struct ThreadData {
    SOCKET socket;
    char buffer[DEFAULT_BUFLEN];
    int instancePort;
    Message* message;
};

HANDLE FinishSignal;

CRITICAL_SECTION TotalSumAccess;

bool InitializeWindowsSockets();
DWORD WINAPI acceptSockets(LPVOID lpParam);
DWORD WINAPI receiveMessages(LPVOID lpParam);

int main(void) 
{
    // Socket used for listening for new clients
    SOCKET listeningSocket = INVALID_SOCKET;

    // Socket used for connecting to new client
    SOCKET acceptedSocket = INVALID_SOCKET;

    if (InitializeWindowsSockets() == false)
    {
        return 1;
    }

    // Prepare address information structures
    addrinfo *resultingAddress = NULL;
    addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4 address
    hints.ai_socktype = SOCK_STREAM; // Provide reliable data streaming
    hints.ai_protocol = IPPROTO_TCP; // Use TCP protocol
    hints.ai_flags = AI_PASSIVE;     // 

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &resultingAddress);
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

	printf("Server initialized, waiting for clients...\n");
    printf("==========================================\n");

    // Create all semaphores
    FinishSignal = CreateSemaphore(0, 0, 2, NULL);

    // Create all threads
    DWORD SocketAcceptorID, ReceiverID;
    HANDLE hSocketAcceptor, hReceiver;

    if (FinishSignal)
    {
        InitializeCriticalSection(&TotalSumAccess);

        // Thread za prihvatanje socketa
        ThreadData acceptSocketsThreadData;
        acceptSocketsThreadData.socket = listeningSocket;
        hSocketAcceptor = CreateThread(NULL, 0, &acceptSockets, &acceptSocketsThreadData, 0, &SocketAcceptorID);

        // Thread za primanje podataka sa prethodne instance
        ThreadData receiveMessagesThreadData;
        receiveMessagesThreadData.socket = listeningSocket;
        hReceiver = CreateThread(NULL, 0, &receiveMessages, &receiveMessagesThreadData, 0, &ReceiverID);

        if (!hSocketAcceptor || !hReceiver)
        {
            ReleaseSemaphore(FinishSignal, 2, NULL);
        }

        if (hSocketAcceptor)
        {
            WaitForSingleObject(hSocketAcceptor, INFINITE);
        }

        if (hReceiver)
        {
            WaitForSingleObject(hReceiver, INFINITE);
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
    closesocket(listeningSocket);
    closesocket(acceptedSocket);
    for (int i = 0; i < socketIndex; i++) {
        closesocket(acceptedSockets[i]);
    }

    SAFE_CLOSE_HANDLE(hSocketAcceptor);
    SAFE_CLOSE_HANDLE(hReceiver);
    SAFE_CLOSE_HANDLE(FinishSignal);

    DeleteCriticalSection(&TotalSumAccess);

    WSACleanup();

    return 0;
}

bool InitializeWindowsSockets()
{
    WSADATA wsaData;

	// Initialize windows sockets library for this process
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
    {
        printf("WSAStartup failed with error: %d\n", WSAGetLastError());
        return false;
    }

	return true;
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
                // Poruka je primljena bez gresaka

                printf("Received from previous instance\n");

                Message* message = (Message *)receiveMessagesThreadData.buffer;

                printf("[%s]: %d\n", message->isImportant ? "IMPORTANT" : "STANDARD", message->value);

                EnterCriticalSection(&TotalSumAccess);

                totalSum += message->value;
                printf("Total count: %d\n", totalSum);

                LeaveCriticalSection(&TotalSumAccess);

                printf("==========================================\n");
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