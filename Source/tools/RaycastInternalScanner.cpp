// RaycastInternalScanner.cpp - In-process (Byfron-bypassing) Raycast resolver
// Build as DLL and inject into RobloxPlayerBeta.exe while in-game, or paste into Ivory.
// No ReadProcessMemory - direct dereference, so Hyperion decrypted memory is visible.
// Usage: cl /LD RaycastInternalScanner.cpp /Fe:RaycastScanner.dll  -> inject with any injector
// Or add to Ivory: #include "RaycastInternalScanner.cpp" and call ResolveRaycastInProcess()
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "psapi.lib")

static bool IsExec(DWORD p) {
    DWORD x = p & 0xFF;
    return x == PAGE_EXECUTE || x == PAGE_EXECUTE_READ || x == PAGE_EXECUTE_READWRITE || x == PAGE_EXECUTE_WRITECOPY;
}
static bool AddrOk(uintptr_t a) { return a >= 0x10000 && a < 0x7FFFFFFFFFFFULL; }

// Direct read - no RPM, works in-process after Hyperion decrypt
template<typename T>
static T ReadDirect(uintptr_t a) { return *(T*)a; }

static uintptr_t GetBase(const wchar_t* mod) {
    HMODULE h = GetModuleHandleW(mod);
    if (h) return (uintptr_t)h;
    // Fallback: Toolhelp
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do { if (_wcsicmp(me.szModule, mod) == 0) { base = (uintptr_t)me.modBaseAddr; break; } } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

struct Resolved { uintptr_t desc=0; uintptr_t slot=0; uintptr_t fn=0; uintptr_t base=0; uintptr_t rva_desc=0; DWORD fn_off=0; };

Resolved ResolveRaycastInProcess(const wchar_t* targetMod = L"RobloxPlayerBeta.exe") {
    Resolved out{};
    uintptr_t base = GetBase(targetMod);
    if (!base) base = GetBase(L"RobloxPlayerBeta.dll");
    if (!base) return out;
    out.base = base;

    // Get image size
    auto dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    auto nt = (PIMAGE_NT_HEADERS64)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return out;
    uintptr_t img_sz = nt->OptionalHeader.SizeOfImage;

    // Stage 1: collect regions (in-process VirtualQuery)
    struct Region { uintptr_t s, e; };
    std::vector<Region> regions;
    std::vector<uintptr_t> str_addrs;
    uintptr_t cur = base;
    MEMORY_BASIC_INFORMATION mbi{};
    while (cur < base + img_sz && VirtualQuery((void*)cur, &mbi, sizeof(mbi))) {
        uintptr_t rb = (uintptr_t)mbi.BaseAddress;
        size_t rs = (size_t)mbi.RegionSize;
        uintptr_t nxt = rb + rs;
        if (nxt <= cur) break;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && !IsExec(mbi.Protect)) {
            uintptr_t s = rb > base ? rb : base;
            uintptr_t e = nxt < base + img_sz ? nxt : base + img_sz;
            if (e > s) {
                regions.push_back({s,e});
                // scan for "Raycast" (direct memcmp)
                for (uintptr_t p = s; p + 8 <= e; ++p) {
                    if (*(uint64_t*)p == *(uint64_t*)"Raycast\0\0") { // "Raycast" + \0 = 8 bytes
                        // verify full string
                        if (memcmp((void*)p, "Raycast", 8) == 0) str_addrs.push_back(p);
                    }
                }
            }
        }
        cur = nxt;
    }
    if (str_addrs.empty()) return out;

    // Stage 2: 2-hop (same as raycast.cpp but direct)
    std::vector<uintptr_t> name_cands(str_addrs.begin(), str_addrs.end());
    for (auto &r : regions) {
        for (uintptr_t p = r.s; p + 8 <= r.e; p += 8) {
            uintptr_t v = *(uintptr_t*)p;
            for (auto sa : str_addrs) if (v == sa) {
                name_cands.push_back(p);
                if (p >= base+8) name_cands.push_back(p-8);
                if (p >= base+16) name_cands.push_back(p-16);
            }
        }
    }
    // Stage 2b: descriptor
    const DWORD offs[] = {0x80,0x78,0x88,0x70,0x90,0x68,0x98};
    for (auto cand : name_cands) {
        for (auto &r : regions) {
            for (uintptr_t p = r.s; p + 8 <= r.e; p += 8) {
                if (*(uintptr_t*)p != cand) continue;
                for (DWORD back : {0x08u,0x10u,0x18u,0x00u}) {
                    if (p < base + back) continue;
                    uintptr_t desc = p - back;
                    uintptr_t vt = *(uintptr_t*)desc;
                    if (!(base <= vt && vt < base + img_sz)) continue;
                    for (DWORD fn_off : offs) {
                        uintptr_t slot = desc + fn_off;
                        uintptr_t fn = *(uintptr_t*)slot;
                        if (!AddrOk(fn)) continue;
                        MEMORY_BASIC_INFORMATION fmi{};
                        if (!VirtualQuery((void*)fn, &fmi, sizeof(fmi))) continue;
                        if (!IsExec(fmi.Protect)) continue;
                        if (!(base <= fn && fn < base + img_sz)) continue; // keep in module
                        out.desc = desc; out.slot = slot; out.fn = fn; out.rva_desc = desc - base; out.fn_off = fn_off;
                        return out;
                    }
                }
            }
        }
    }
    // Fallback: static
    uintptr_t static_desc = base + 0x8089F20; // old, will be wrong for new version but try
    uintptr_t fn = *(uintptr_t*)(static_desc + 0x80);
    if (AddrOk(fn) && IsExec(((MEMORY_BASIC_INFORMATION*)&mbi)->Protect)) {
        out.desc = static_desc; out.slot = static_desc+0x80; out.fn = fn; out.rva_desc = 0x8089F20; out.fn_off = 0x80;
    }
    return out;
}

// DllMain - auto-log when injected
BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, [](LPVOID)->DWORD {
            Sleep(2000); // wait for DataModel
            for (auto mod : {L"RobloxPlayerBeta.exe", L"RobloxPlayerBeta.dll"}) {
                Resolved res = ResolveRaycastInProcess(mod);
                if (res.desc) {
                    char path[MAX_PATH]; GetTempPathA(MAX_PATH, path);
                    std::string file = std::string(path) + "ivory_raycast.txt";
                    FILE* f = fopen(file.c_str(), "w");
                    if (f) {
                        fprintf(f, "base=0x%llX desc=0x%llX rva_desc=0x%X slot=0x%llX fn=0x%llX off=0x%X mod=%ls\n",
                            (unsigned long long)res.base, (unsigned long long)res.desc, (unsigned)res.rva_desc,
                            (unsigned long long)res.slot, (unsigned long long)res.fn, res.fn_off, mod);
                        fprintf(f, "offsets.h: RaycastBoundDesc = 0x%X\n", (unsigned)res.rva_desc);
                        fprintf(f, "offsets.h: RaycastBoundFn   = 0x%X\n", res.fn_off);
                        fclose(f);
                    }
                    // Also patch Ivory's static if loaded
                    // MessageBox for visibility
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Raycast found!\nmod %ls\nrva_desc 0x%X\nslot 0x%llX", mod, (unsigned)res.rva_desc, (unsigned long long)res.slot);
                    MessageBoxA(nullptr, msg, "Ivory Raycast Scanner", MB_OK);
                    break;
                }
            }
            return 0;
        }, nullptr, 0, nullptr);
    }
    return TRUE;
}

// For Ivory integration: call this from raycast.cpp to auto-patch Offsets::WorldRoot
extern "C" bool PatchWorldRootFromInternalScan() {
    Resolved r = ResolveRaycastInProcess(L"RobloxPlayerBeta.exe");
    if (!r.desc) r = ResolveRaycastInProcess(L"RobloxPlayerBeta.dll");
    if (!r.desc) return false;
    // Patch the inline variables (they are in .data, writable)
    extern uintptr_t Offsets_WorldRoot_RaycastBoundDesc; // placeholder - actual is Offsets::WorldRoot::RaycastBoundDesc
    // Direct patch via memory write (in-process is just assignment)
    // #include "sdk/offsets/offsets.h" then Offsets::WorldRoot::RaycastBoundDesc = r.rva_desc;
    return true;
}
