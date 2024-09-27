#include <stdio.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 1024
#define WORKER_THREADS 4

typedef struct {
    OVERLAPPED overlapped;
    SOCKET socket;
    WSABUF buffer;
    char bufferData[BUFFER_SIZE];
} IO_DATA, * PIO_DATA;

// 处理线程函数
DWORD WINAPI WorkerThread(LPVOID lpParam) {
    HANDLE hCompletionPort = (HANDLE)lpParam;
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    PIO_DATA ioData;
    LPOVERLAPPED lpOverlapped;

    while (TRUE) {
        BOOL result = GetQueuedCompletionStatus(hCompletionPort, &bytesTransferred, &completionKey, &lpOverlapped, INFINITE);

        if (!result || bytesTransferred == 0) {
            printf("Client disconnected or error occurred.\n");
            closesocket((SOCKET)completionKey);
            free((PIO_DATA)lpOverlapped);
            continue;
        }

        ioData = (PIO_DATA)lpOverlapped;
        printf("Received data: %s\n", ioData->bufferData);

        // Echo data back to client
        ioData->buffer.len = bytesTransferred;
        WSASend(ioData->socket, &ioData->buffer, 1, NULL, 0, NULL, NULL);

        // Post another receive operation
        ioData->buffer.len = BUFFER_SIZE;
        ZeroMemory(ioData->bufferData, BUFFER_SIZE);
        WSARecv(ioData->socket, &ioData->buffer, 1, NULL, NULL, &ioData->overlapped, NULL);
    }

    return 0;
}

// 设置完成端口并创建线程池
HANDLE SetupCompletionPort(SOCKET listenSocket) {
    HANDLE hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (hCompletionPort == NULL) {
        printf("Failed to create IO Completion Port.\n");
        return NULL;
    }

    // 将监听套接字与完成端口关联
    CreateIoCompletionPort((HANDLE)listenSocket, hCompletionPort, (ULONG_PTR)listenSocket, 0);

    // 创建工作线程
    for (int i = 0; i < WORKER_THREADS; ++i) {
        CreateThread(NULL, 0, WorkerThread, hCompletionPort, 0, NULL);
    }

    return hCompletionPort;
}

int main() {
    WSADATA wsaData;
    SOCKET listenSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int addrLen = sizeof(clientAddr);

    // 初始化WinSock
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // 创建监听套接字
    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) {
        printf("Failed to create listen socket.\n");
        return 1;
    }

    // 设置服务器地址和端口
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    // 绑定套接字
    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Bind failed.\n");
        closesocket(listenSocket);
        return 1;
    }

    // 监听连接
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed.\n");
        closesocket(listenSocket);
        return 1;
    }

    printf("Server is listening on port %d...\n", PORT);

    // 设置完成端口
    HANDLE hCompletionPort = SetupCompletionPort(listenSocket);
    if (hCompletionPort == NULL) {
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // 接受客户端连接
    while (TRUE) {
        clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) {
            printf("Failed to accept client connection.\n");
            continue;
        }

        printf("Accepted connection from client.\n");

        // 分配IO数据结构
        PIO_DATA ioData = (PIO_DATA)malloc(sizeof(IO_DATA));
        if (!ioData) {
            printf("Failed to allocate IO data.\n");
            closesocket(clientSocket);
            continue;
        }

        ioData->socket = clientSocket;
        ioData->buffer.len = BUFFER_SIZE;
        ioData->buffer.buf = ioData->bufferData;
        ZeroMemory(&ioData->overlapped, sizeof(OVERLAPPED));
        ZeroMemory(ioData->bufferData, BUFFER_SIZE);

        // 将客户端socket与完成端口关联
        CreateIoCompletionPort((HANDLE)clientSocket, hCompletionPort, (ULONG_PTR)clientSocket, 0);

        // 开始接收数据
        DWORD flags = 0;
        WSARecv(clientSocket, &ioData->buffer, 1, NULL, &flags, &ioData->overlapped, NULL);
    }

    // 关闭监听套接字
    closesocket(listenSocket);

    // 清理
    WSACleanup();

    return 0;
}
