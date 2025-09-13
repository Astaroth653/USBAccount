#include <windows.h>
#include <shellapi.h>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <lm.h>   // Pour NetUser API
#pragma comment(lib, "Netapi32.lib")

// =================== CONFIG ====================
std::string ACCOUNT_NAME   = "User";
std::string TOKEN_EXPECTED = "m7zU}0&RH|%&m}Tz+*hlObrpQKO):s~m";
std::string TOKEN_FILENAME = "\\usb_token.dat";
int POLL_INTERVAL_MS       = 5000; // 5s
// ==============================================

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001

HINSTANCE hInst;
NOTIFYICONDATA nid;
bool lastState = false; // token state

// --- Logging ---
void logMsg(const std::string& msg) {
    std::ofstream log("C:\\usb_key_lock.log", std::ios::app);
    SYSTEMTIME st; GetLocalTime(&st);
    log << "[" << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] " << msg << "\n";
}

// --- Vérifier si compte existe ---
bool accountExists(const std::wstring& account) {
    USER_INFO_0* ui = nullptr;
    NET_API_STATUS status = NetUserGetInfo(NULL, account.c_str(), 0, (LPBYTE*)&ui);
    if (ui) NetApiBufferFree(ui);
    return (status == NERR_Success);
}

// --- Créer le compte si besoin (sans mot de passe) ---
bool createAccount(const std::wstring& account) {
    USER_INFO_1 ui;
    ZeroMemory(&ui, sizeof(ui));
    ui.usri1_name     = (LPWSTR)account.c_str();
    ui.usri1_password = NULL; // ⚠️ pas de mot de passe
    ui.usri1_priv     = USER_PRIV_USER;
    ui.usri1_flags    = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;

    DWORD parm_err = 0;
    NET_API_STATUS status = NetUserAdd(NULL, 1, (LPBYTE)&ui, &parm_err);
    return (status == NERR_Success);
}

// --- Registry (hide/unhide user) ---
bool setAccountHidden(const std::string& account, bool hidden) {
    HKEY hKey = nullptr;
    LPCWSTR basePath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\SpecialAccounts\\UserList";
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, basePath, 0, NULL,
                             REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
                             NULL, &hKey, NULL);
    if (r != ERROR_SUCCESS) return false;

    int len = MultiByteToWideChar(CP_UTF8, 0, account.c_str(), -1, NULL, 0);
    std::wstring waccount(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, account.c_str(), -1, &waccount[0], len);

    if (hidden) {
        DWORD val = 0;
        r = RegSetValueExW(hKey, waccount.c_str(), 0, REG_DWORD,
                           (const BYTE*)&val, sizeof(val));
    } else {
        r = RegDeleteValueW(hKey, waccount.c_str());
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }

    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS);
}

// --- Check USB token ---
bool isTokenPresent() {
    DWORD drives = GetLogicalDrives();
    if (drives == 0) return false;
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        char driveLetter = 'A' + i;
        std::string root(1, driveLetter);
        root += ":\\";
        UINT type = GetDriveTypeA(root.c_str());
        if (type != DRIVE_REMOVABLE) continue; // ⚠️ uniquement clés USB
        std::string tokenPath = root + TOKEN_FILENAME;
        std::ifstream ifs(tokenPath);
        if (!ifs) continue;
        std::string content;
        std::getline(ifs, content);
        if (content == TOKEN_EXPECTED) return true;
    }
    return false;
}

// --- Update tray icon ---
void updateTray(bool unlocked) {
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.hIcon = LoadIcon(NULL, unlocked ? IDI_APPLICATION : IDI_ERROR); 
    lstrcpyW(nid.szTip, unlocked ? L"Compte User: visible" : L"Compte User: masqué");
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// --- Background thread ---
void workerThread(HWND hwnd) {
    std::wstring wAccount(ACCOUNT_NAME.begin(), ACCOUNT_NAME.end());

    // Création auto si absent
    if (!accountExists(wAccount)) {
        logMsg("Compte introuvable → création.");
        if (createAccount(wAccount)) {
            logMsg("Compte créé avec succès (sans mot de passe).");
        } else {
            logMsg("Échec création compte !");
        }
    }

    while (true) {
        bool present = isTokenPresent();
        if (present != lastState) {
            if (present) {
                logMsg("Clé USB détectée → affichage du compte.");
                setAccountHidden(ACCOUNT_NAME, false);
                updateTray(true);
            } else {
                logMsg("Clé USB retirée → masquage du compte.");
                setAccountHidden(ACCOUNT_NAME, true);
                updateTray(false);
            }
            lastState = present;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
}

// --- Context menu ---
void ShowContextMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Quitter"));
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// --- WndProc ---
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

// --- Entry point (UNICODE) ---
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    hInst = hInstance;
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"USBKeyApp";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(L"USBKeyApp", L"USB Key Lock",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
        NULL, NULL, hInstance, NULL);

    // Icône tray init
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_ERROR); // 🔒
    lstrcpyW(nid.szTip, L"Compte User: masqué");
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Toujours au départ → masqué
    setAccountHidden(ACCOUNT_NAME, true);
    lastState = false;

    // Thread de surveillance
    std::thread(workerThread, hwnd).detach();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
