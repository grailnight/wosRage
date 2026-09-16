// WoS Rage Loader: starts Spider-Man Web of Shadows (or finds the running game)
// and loads WoS_Rage\WoS_Rage.dll into it once the game window exists.
// Place it next to "Spider-Man Web of Shadows.exe".
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>

static const wchar_t kGame[] = L"Spider-Man Web of Shadows.exe";
static const wchar_t kTitle[] = L"WoS Symbiote Rage";

static void message(const std::wstring& text, UINT icon) { MessageBoxW(nullptr, text.c_str(), kTitle, MB_OK | icon); }

static std::wstring folderOfThisExe() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = path;
    return s.substr(0, s.find_last_of(L'\\'));
}

static DWORD runningGame() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (_wcsicmp(e.szExeFile, kGame) == 0) { pid = e.th32ProcessID; break; }
    CloseHandle(snap);
    return pid;
}

static bool moduleLoaded(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W e{};
    e.dwSize = sizeof(e);
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &e); ok; ok = Module32NextW(snap, &e))
        if (_wcsicmp(e.szModule, L"WoS_Rage.dll") == 0) { found = true; break; }
    CloseHandle(snap);
    return found;
}

struct WindowSearch { DWORD pid; HWND found; };
static BOOL CALLBACK findWindow(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == search->pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) { search->found = hwnd; return FALSE; }
    return TRUE;
}

static bool loadInto(DWORD pid, const std::wstring& dll, std::wstring& error) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                                 PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) { error = L"Cannot open the game process (error " + std::to_wstring(GetLastError()) + L"). Run the loader with the same rights as the game."; return false; }
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD result = 0;
    if (remote && WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr)) {
        // Both processes are 32-bit, so LoadLibraryW has the same address in the game.
        auto start = reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, start, remote, 0, nullptr);
        if (thread) {
            if (WaitForSingleObject(thread, 15000) == WAIT_OBJECT_0) GetExitCodeThread(thread, &result);
            CloseHandle(thread);
        }
    }
    if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    if (!result) error = L"The game did not load WoS_Rage.dll. See WoS_Rage\\WoS_Rage.log if it exists.";
    return result != 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring root = folderOfThisExe();
    const std::wstring game = root + L"\\" + kGame;
    const std::wstring dll = root + L"\\WoS_Rage\\WoS_Rage.dll";
    if (GetFileAttributesW(game.c_str()) == INVALID_FILE_ATTRIBUTES) { message(L"Put this loader next to Spider-Man Web of Shadows.exe.", MB_ICONERROR); return 1; }
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) { message(L"WoS_Rage\\WoS_Rage.dll is missing. Extract the whole archive into the game folder.", MB_ICONERROR); return 1; }

    DWORD pid = runningGame();
    if (!pid) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring command = L"\"" + game + L"\"";
        if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &si, &pi)) {
            message(L"Could not start the game (error " + std::to_wstring(GetLastError()) + L").", MB_ICONERROR);
            return 1;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        pid = pi.dwProcessId;
    }
    if (moduleLoaded(pid)) return 0;

    // Wait for the game window: the D3D device and game data come after it.
    WindowSearch search{pid, nullptr};
    for (int i = 0; i < 180 && !search.found; i++) {
        EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
        if (!search.found) Sleep(500);
    }
    if (!search.found) { message(L"The game window did not appear within 90 seconds. Rage was not loaded.", MB_ICONWARNING); return 1; }

    std::wstring error;
    if (!loadInto(pid, dll, error)) { message(error, MB_ICONERROR); return 1; }
    return 0;
}
