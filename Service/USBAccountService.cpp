#include <windows.h>
#include <lm.h>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>

#pragma comment(lib, "Netapi32.lib")

// =================== CONFIG ====================
std::string ACCOUNT_NAME   = "User";
std::string TOKEN_EXPECTED = "m7zU}0&RH|%&m}Tz+*hlObrpQKO):s~m";
std::string TOKEN_FILENAME = "\\usb_token.dat";
int POLL_INTERVAL_MS       = 5000; // 5s
// ==============================================

SERVICE_STATUS ServiceStatus;
SERVICE_STATUS_HANDLE hStatus;
bool serviceRunning = true;
bool lastState = false;

// --- Logging dans fichier ---
void logMsg(const std::string& msg) {
    std::ofstream log("C:\\usb_key_lock_service.log", std::ios::app);
    SYSTEMTIME st; GetLocalTime(&st);
    log << "[" << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] " << msg << "\n";
}

// --- Écrire état dans fichier partagé ---
void writeStatusFile(bool unlocked) {
    std::ofstream f("C:\\usb_key_status.txt", std::ios::trunc);
    if (f.is_open()) {
        f << (unlocked ? "UNLOCKED" : "LOCKED");
    }
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
        if (type != DRIVE_REMOVABLE) continue;
        std::string tokenPath = root + TOKEN_FILENAME;
        std::ifstream ifs(tokenPath);
        if (!ifs) continue;
        std::string content;
        std::getline(ifs, content);
        if (content == TOKEN_EXPECTED) return true;
    }
    return false;
}

// --- Thread principal du service ---
void serviceWorker() {
    std::wstring wAccount(ACCOUNT_NAME.begin(), ACCOUNT_NAME.end());

    if (!accountExists(wAccount)) {
        logMsg("Compte introuvable → création.");
        if (createAccount(wAccount)) {
            logMsg("Compte créé avec succès (sans mot de passe).");
        } else {
            logMsg("Échec création compte !");
        }
    }

    // Par défaut masqué
    setAccountHidden(ACCOUNT_NAME, true);
    writeStatusFile(false);
    lastState = false;

    while (serviceRunning) {
        bool present = isTokenPresent();
        if (present != lastState) {
            if (present) {
                logMsg("Clé USB détectée → affichage du compte.");
                setAccountHidden(ACCOUNT_NAME, false);
                writeStatusFile(true);
            } else {
                logMsg("Clé USB retirée → masquage du compte.");
                setAccountHidden(ACCOUNT_NAME, true);
                writeStatusFile(false);
            }
            lastState = present;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
}

// --- Handler des commandes du service ---
void WINAPI ServiceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
    case SERVICE_CONTROL_STOP:
        ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(hStatus, &ServiceStatus);
        serviceRunning = false;
        return;
    default:
        break;
    }
    SetServiceStatus(hStatus, &ServiceStatus);
}

// --- Entrée principale du service ---
void WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    ServiceStatus.dwWin32ExitCode = 0;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = 0;
    ServiceStatus.dwWaitHint = 0;

    hStatus = RegisterServiceCtrlHandler(L"USBKeyService", ServiceCtrlHandler);
    if (!hStatus) return;

    ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(hStatus, &ServiceStatus);

    logMsg("Service démarré.");
    serviceWorker();
    logMsg("Service arrêté.");

    ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(hStatus, &ServiceStatus);
}

// --- Point d’entrée global ---
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        { (LPWSTR)L"USBKeyService", (LPSERVICE_MAIN_FUNCTION)ServiceMain },
        { NULL, NULL }
    };
    StartServiceCtrlDispatcher(ServiceTable);
    return 0;
}
