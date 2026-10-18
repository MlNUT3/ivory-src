#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <iostream>

DWORD FindPid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do { if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; } } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main() {
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    std::wstring exeDir(dllPath);
    size_t pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos+1);
    std::wstring targetDll = exeDir + L"RaycastScanner.dll";

    // Also try absolute
    if (GetFileAttributesW(targetDll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        targetDll = L"C:\\Users\\ceram\\Downloads\\Ivory\\1\\1\\Source\\tools\\RaycastScanner.dll";
    }

    DWORD pid = FindPid(L"RobloxPlayerBeta.exe");
    if (!pid) { std::cout << "RobloxPlayerBeta.exe not found. Launch a game first.\n"; return 1; }
    std::wcout << L"Found PID " << pid << L" injecting " << targetDll << L"\n";

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) { std::cout << "OpenProcess failed " << GetLastError() << " - run as admin\n"; return 1; }

    size_t sz = (targetDll.size()+1)*sizeof(wchar_t);
    LPVOID mem = VirtualAllocEx(hProc, nullptr, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { std::cout << "VirtualAllocEx failed\n"; return 1; }
    WriteProcessMemory(hProc, mem, targetDll.c_str(), sz, nullptr);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC load = GetProcAddress(k32, "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)load, mem, 0, nullptr);
    if (!hThread) { std::cout << "CreateRemoteThread failed " << GetLastError() << " - Byfron may block\n"; return 1; }
    WaitForSingleObject(hThread, 5000);
    DWORD code=0; GetExitCodeThread(hThread, &code);
    std::cout << "Injected, remote LoadLibrary returned 0x" << std::hex << code << std::dec << "\n";
    if (code) std::cout << "Check %TEMP%\\ivory_raycast.txt and MessageBox in Roblox.\n";
    else std::cout << "LoadLibrary returned 0 - check dll path and run as admin.\n";
    CloseHandle(hThread); VirtualFreeEx(hProc, mem, 0, MEM_RELEASE); CloseHandle(hProc);
    std::cout << "\nPress Enter to close..."; std::cin.get();
    return 0;
}
