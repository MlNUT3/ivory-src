#include <windows.h>
#include <string>

// Minimal bootstrap host. Loads the cheat dll into THIS process and stays
// alive until the dll ejects itself - no injection into any other app.
//
//   run Ivory.exe  ->  nvldumdx.dll loads right here  ->  the cheat runs
//   eject (menu)   ->  dll unmaps itself              ->  this exits
//   close Ivory.exe->  dll fully unloads, file released (rebuild freely)
//
// Built with an requireAdministrator manifest: the cheat opens Roblox's
// process handle externally and needs the elevation.

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring dir(exe_path);
    const size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir = dir.substr(0, pos + 1);

    std::wstring dll = dir + L"nvldumdx.dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        // masquerade fallback
        const std::wstring alt = dir + L"Ivory.dll";
        if (GetFileAttributesW(alt.c_str()) != INVALID_FILE_ATTRIBUTES)
            dll = alt;
        else
        {
            MessageBoxA(nullptr, "nvldumdx.dll not found next to Ivory.exe.",
                "Ivory", MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    const HMODULE mod = LoadLibraryW(dll.c_str());
    if (!mod)
    {
        MessageBoxA(nullptr, "Failed to load nvldumdx.dll (missing dependencies? run as admin).",
            "Ivory", MB_OK | MB_ICONERROR);
        return 2;
    }

    // wait until the dll unmaps itself (eject), then exit so the file is
    // released and can be rebuilt without closing anything else
    for (;;)
    {
        Sleep(500);
        HMODULE check = nullptr;
        if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(mod), &check))
        {
            // dll ejected itself - process exits, file is released
            return 0;
        }
    }
}
