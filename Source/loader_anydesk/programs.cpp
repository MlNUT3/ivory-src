// Loader backend: loads the cheat module into this process.
// The AnyDesk UI is the disguise; this file is the actual mechanism.
//
// Anti-forensics (detect.ac-aware):
//  - the payload never loads under a cheat-meaningful name: it is copied to
//    %TEMP% under a random key first, loaded from there, and the copy is
//    POSIX-deleted (delete-while-running) right after - a random-named USN
//    delete record that no name rule matches
//  - the on-disk payload next to the exe is TRUNCATED in place after load
//    (zeroed, never deleted - a delete record is the permanent artifact)
//  - at scan time nothing named like a cheat exists on any volume, and no
//    YARA-scannable content remains

#include "programs.h"
#include "obf/lazy_importer.hpp"
#include "obf/xorstr.hpp"

#include <string>
#include <filesystem>
#include <random>

std::vector<Program>        g_programs;
std::vector<TrackedProcess> g_tracked;
int                         g_launch_result = 0;
bool                        g_launching = false;
DWORD                       g_last_error = 0;

static DWORD g_launch_counter = 0;
static HMODULE g_cheat_module = nullptr;
static std::wstring g_loaded_copy_path;

// ---------------------------------------------------------------------------
// POSIX-semantics delete (Win10 1803+): marks a file for immediate removal
// from the namespace even while it is mapped/loaded by this process. Works
// where DeleteFile fails on in-use images.
// ---------------------------------------------------------------------------
static bool posix_delete_file(const std::wstring& path)
{
    // NtSetInformationFile(FileDispositionInformationEx)
    using nt_set_info_t = LONG(__stdcall*)(void*, void*, void*, unsigned long, unsigned long);
    HMODULE ntdll = LI_FN(GetModuleHandleW)(xorstr_(L"ntdll.dll"));
    if (!ntdll)
        return false;
    auto nt_set = (nt_set_info_t)(void*)LI_FN(GetProcAddress)(ntdll, xorstr_("NtSetInformationFile"));
    if (!nt_set)
        return false;

    HANDLE h = LI_FN(CreateFileW)(path.c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    struct EX_DISP { unsigned long Flags; };
    EX_DISP info{};                 // FILE_DISPOSITION_INFORMATION_EX
    info.Flags = 0x1 | 0x2;         // DELETE | POSIX_SEMANTICS

    LONG status = nt_set(h, nullptr, &info, sizeof(info), 60);
    LI_FN(CloseHandle)(h);
    return status >= 0;
}

// truncate a file to zero WITHOUT deleting it (no USN delete record is
// written for the directory entry; content is gone for content scanners)
static void truncate_in_place(const std::wstring& path)
{
    HANDLE h = LI_FN(CreateFileW)(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    LI_FN(SetFilePointer)(h, 0, nullptr, FILE_BEGIN);
    LI_FN(SetEndOfFile)(h);
    LI_FN(FlushFileBuffers)(h);
    LI_FN(CloseHandle)(h);
}

static std::wstring random_key(int len)
{
    static const wchar_t alphabet[] = L"abcdefghijklmnopqrstuvwxyzABCDEF0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 33);
    std::wstring out;
    for (int i = 0; i < len; i++)
        out += alphabet[dist(gen)];
    return out;
}

bool nv_load_cheat(DWORD* out_error)
{
    g_last_error = 0;
    if (out_error) *out_error = 0;

    if (g_cheat_module)
        return true; // already loaded

    // resolve our exe dir + the payload next to it
    wchar_t exe_path[MAX_PATH] = {};
    LI_FN(GetModuleFileNameW)(nullptr, exe_path, MAX_PATH);
    std::wstring dir(exe_path);
    const size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir = dir.substr(0, pos + 1);

    const std::wstring payload = dir + xorstr_(L"nvldumdx.dll");

    // 1. copy to %TEMP% under a random key - the load name carries no meaning
    wchar_t temp_dir[MAX_PATH] = {};
    if (LI_FN(GetTempPathW)((unsigned long)MAX_PATH, temp_dir) == 0)
    {
        g_last_error = 1;
        if (out_error) *out_error = g_last_error;
        return false;
    }
    const std::wstring copy_path = std::wstring(temp_dir) + random_key(14) + xorstr_(L".dll");
    if (!LI_FN(CopyFileW)(payload.c_str(), copy_path.c_str(), FALSE))
    {
        g_last_error = LI_FN(GetLastError)();
        if (out_error) *out_error = g_last_error;
        return false;
    }

    // 2. load from the random-named copy
    HMODULE mod = LI_FN(LoadLibraryW)(copy_path.c_str());
    if (!mod)
    {
        g_last_error = LI_FN(GetLastError)();
        LI_FN(DeleteFileW)(copy_path.c_str());
        if (out_error) *out_error = g_last_error;
        return false;
    }

    g_cheat_module = mod;
    g_loaded_copy_path = copy_path;

    // 3. the copy is now mapped - remove it from the namespace immediately
    //    (delete-pending with POSIX semantics: gone even while loaded)
    posix_delete_file(copy_path);

    // 4. the payload next to the exe served its purpose - truncate it in
    //    place so no YARA-scannable content remains, without writing a
    //    delete record for a cheat-named file
    truncate_in_place(payload);

    return true;
}

bool nv_unload_cheat(DWORD* out_error)
{
    if (out_error) *out_error = 0;
    if (!g_cheat_module)
        return false;

    // the dll ejects itself cleanly (FreeLibraryAndExitThread on a worker);
    // from the loader side we just drop our reference after a grace period
    HMODULE mod = g_cheat_module;
    g_cheat_module = nullptr;

    LI_FN(FreeLibrary)(mod);

    // belt & suspenders: the copy should already be delete-pending; make
    // sure the entry is actually gone now that our reference dropped
    if (!g_loaded_copy_path.empty())
    {
        LI_FN(DeleteFileW)(g_loaded_copy_path.c_str());
        g_loaded_copy_path.clear();
    }
    return true;
}

bool nv_cheat_loaded()
{
    return g_cheat_module != nullptr;
}

void cleanup_temp_files()
{
    // stray copy from a crashed run: delete-pending again (random name)
    if (!g_loaded_copy_path.empty())
    {
        posix_delete_file(g_loaded_copy_path);
        g_loaded_copy_path.clear();
    }
}

void programs_init()
{
    g_programs.clear();
    g_programs.push_back(Program{
        "Ivory", "External loader", "1.0",
        0, nullptr, 0, ""
    });
}
