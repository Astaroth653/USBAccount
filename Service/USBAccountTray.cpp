#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001

NOTIFYICONDATA nid;
bool lastUnlocked = false;

std::string readStatus() {
    std::ifstream f("C:\\usb_key_status.txt");
    if (!f) return "LOCKED";
    std::string s;
    std::getline(f, s);
    return s;
}

void updateTray(bool unlocked) {
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.hIcon = LoadIcon(NULL, unlocked ? IDI_APPLICATION : IDI_ERROR);
    lstrcpyW(nid.szTip, unlocked ? L"Compte User: déverrouillé" : L"Compte User: verrouillé");
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void workerThread(HWND hwnd) {
    while (true) {
        bool unlocked = (readStatus() == "UNLOCKED");
        if (unlocked != lastUnlocked) {
            updateTray(unlocked);
            lastUnlocked = unlocked;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void ShowContextMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Quitter"));
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            ShowContextMenu(hwnd, pt);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"USBKeyTray";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(L"USBKeyTray", L"USB Key Tray",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
        NULL, NULL, hInstance, NULL);

    // Init tray
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_ERROR);
    lstrcpyW(nid.szTip, L"Compte User: verrouillé");
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Thread de surveillance
    std::thread(workerThread, hwnd).detach();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
