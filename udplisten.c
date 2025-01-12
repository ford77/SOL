#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 256
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1002

// 全局变量
static struct {
    NOTIFYICONDATA nid;
    HWND hwnd;
    SOCKET sock;
    BOOL running;
    char buffer[BUFFER_SIZE];
} g_app = {0};

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_TRAYICON && lParam == WM_RBUTTONUP) {
        POINT pt;
        GetCursorPos(&pt);
        HMENU hMenu = CreatePopupMenu();
        AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
        return 0;
    }
    
    if (message == WM_COMMAND && LOWORD(wParam) == ID_TRAY_EXIT) {
        g_app.running = FALSE;
        Shell_NotifyIcon(NIM_DELETE, &g_app.nid);
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProc(hwnd, message, wParam, lParam);
}

// UDP监听线程函数
DWORD WINAPI UdpListenThread(LPVOID lpParam) {
    // 设置socket为非阻塞模式
    u_long mode = 1;
    ioctlsocket(g_app.sock, FIONBIO, &mode);
    
    fd_set readfds;
    struct timeval tv = {0, 100000}; // 100ms 超时
    
    while (g_app.running) {
        FD_ZERO(&readfds);
        FD_SET(g_app.sock, &readfds);
        
        if (select(0, &readfds, NULL, NULL, &tv) > 0) {
            struct sockaddr_in client_addr;
            int addr_len = sizeof(client_addr);
            int recv_len = recvfrom(g_app.sock, g_app.buffer, BUFFER_SIZE - 1, 0, 
                                  (struct sockaddr*)&client_addr, &addr_len);
            if (recv_len > 0) {
                g_app.buffer[recv_len] = '\0';
                if (strcmp(g_app.buffer, "shutdown") == 0) {
                    // 发送"OJBK"响应
                    const char* response = "OJBK";
                    sendto(g_app.sock, response, strlen(response), 0,
                          (struct sockaddr*)&client_addr, sizeof(client_addr));
                          
                    system("shutdown /s /t 5 /f /c \"UDP远程关机命令执行\"");
                    g_app.running = FALSE;
                    PostMessage(g_app.hwnd, WM_COMMAND, ID_TRAY_EXIT, 0);
                    break;
                }
            }
        }
        Sleep(1); // 避免CPU占用过高
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_app.running = TRUE;
    
    // 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 1;
    }
    
    // 创建和绑定套接字
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };
    
    if ((g_app.sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET ||
        bind(g_app.sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        WSACleanup();
        return 1;
    }

    // 设置接收缓冲区大小
    int rcvbuf = 65536;
    setsockopt(g_app.sock, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbuf, sizeof(rcvbuf));
    
    // 注册窗口类并创建窗口
    WNDCLASSEXA wc = {
        .cbSize = sizeof(WNDCLASSEXA),
        .lpfnWndProc = WndProc,
        .hInstance = hInstance,
        .lpszClassName = "UdpListenerClass"
    };
    RegisterClassExA(&wc);
    
    g_app.hwnd = CreateWindowExA(0, "UdpListenerClass", "UdpListener",
        WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    // 设置托盘图标
    g_app.nid.cbSize = sizeof(NOTIFYICONDATA);
    g_app.nid.hWnd = g_app.hwnd;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(g_app.nid.szTip, "UDP Listener (8888)");
    Shell_NotifyIcon(NIM_ADD, &g_app.nid);

    // 启动监听线程
    CreateThread(NULL, 0, UdpListenThread, NULL, 0, NULL);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    closesocket(g_app.sock);
    WSACleanup();
    return 0;
}
