#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>

static bool IsProcessAlive(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc)
        return false;
    DWORD exitCode = 0;
    bool alive = GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(hProc);
    return alive;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "R3maJStarter_InstanceMutex");
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH))
        return 1;

    std::string exePath(path);
    auto pos = exePath.find_last_of('\\');
    if (pos == std::string::npos)
        return 1;

    std::string dir = exePath.substr(0, pos);
    std::string R3maJPath = dir + "\\R3maJ.exe";

    if (GetFileAttributesA(R3maJPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return 1;

    bool alreadyRunning = false;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnapshot, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "R3maJ.exe") == 0 &&
                    IsProcessAlive(pe.th32ProcessID)) {
                    alreadyRunning = true;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }

    if (alreadyRunning)
        return 0;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (CreateProcessA(
        R3maJPath.c_str(),
        NULL,
        NULL,
        NULL,
        FALSE,
        CREATE_NEW_CONSOLE,  // R3maJ: give the trainer its own cmd window so it posts its status live
        NULL,
        dir.c_str(),
        &si,
        &pi
    )) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    CloseHandle(hMutex);
    return 0;
}
