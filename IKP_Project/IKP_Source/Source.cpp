#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <time.h>

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT 5000

#define SAFE_CLOSE_HANDLE(handle) if(handle) { CloseHandle(handle); }

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

bool InitializeWindowsSockets();
DWORD WINAPI sendMessages(LPVOID lpParam);

int __cdecl main(int argc, char **argv) 
{
    srand(time(NULL));

    char nextInstanceType[DEFAULT_BUFLEN];
    printf("Next instance type (Agregator or Destination): \n");
    scanf("%s", nextInstanceType);

    int nextInstancePort;

    if (strcmp(nextInstanceType, "Destination") == 0)
    {
        nextInstancePort = 5000;
    }
    else if (strcmp(nextInstanceType, "Agregator") == 0)
    {
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

    // Socket used to communicate with server
    SOCKET connectSocket = INVALID_SOCKET;

    if(InitializeWindowsSockets() == false)
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
    serverAddress.sin_port = htons(nextInstancePort);

    // Connect to server specified in serverAddress and connectSocket
    if (connect(connectSocket, (SOCKADDR*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
    {
        printf("Unable to connect to next instance.\n");
        closesocket(connectSocket);
        WSACleanup();
    }

    printf("Source connected to next instance!\n");
    printf("==========================================\n");

    // Create all semaphores
    FinishSignal = CreateSemaphore(0, 0, 1, NULL);

    // Create all threads
    DWORD SenderID;
    HANDLE hSender;

    if (FinishSignal)
    {
        // Thread za slanje poruka na sledecu instancu
        ThreadData sendMessagesThreadData;
        sendMessagesThreadData.socket = connectSocket;
        sendMessagesThreadData.instancePort = nextInstancePort;
        hSender = CreateThread(NULL, 0, &sendMessages, &sendMessagesThreadData, 0, &SenderID);

        if (!hSender)
        {
            ReleaseSemaphore(FinishSignal, 1, NULL);
        }

        if (hSender)
        {
            WaitForSingleObject(hSender, INFINITE);
        }
    }

    _getch();

    // Cleanup
    closesocket(connectSocket);

    SAFE_CLOSE_HANDLE(hSender);
    SAFE_CLOSE_HANDLE(FinishSignal);

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

DWORD WINAPI sendMessages(LPVOID lpParam)
{
    ThreadData sendMessagesThreadData = *(ThreadData*)lpParam;
    
    while (true)
    {
        Message message;
        message.value = (rand() % 100) + 1;  // Random number from 1 to 100
        message.isImportant = rand() & 1;    // Random boolean value

        // Send data to server
        iResult = send(sendMessagesThreadData.socket, (const char*)&message, sizeof(message), 0);
        if (iResult == SOCKET_ERROR)
        {
            printf("send failed with error: %d\n", WSAGetLastError());
            closesocket(sendMessagesThreadData.socket);
            WSACleanup();
            return 1;
        }

        printf("Sent to %s!\n", sendMessagesThreadData.instancePort == 5000 ? "DESTINATION" : "AGREGATOR");
        printf("[%s]: %d\n", message.isImportant ? "IMPORTANT" : "STANDARD", message.value);
        printf("==========================================\n");

        Sleep(1000); // Testiranje
    }

    return 0;
}
