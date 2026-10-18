#pragma once

#include <Windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <cstring>
#include <cstdint>

#include "nvsp_manip_data.h"

namespace NvspInject {

    static constexpr SIZE_T kMinPrivateBytes = 190ull * 1024ull * 1024ull;

    inline SIZE_T g_LastBestPrivateBytes = 0;
    inline int    g_LastNvContainerCount = 0;

    inline DWORD    g_LastLoadedPid   = 0;
    inline uint64_t g_LastLoadedBase  = 0;
    inline uint64_t g_LastLoadedEntry = 0;

    inline DWORD FindNvContainerPid() {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        DWORD  bestPid = 0;
        SIZE_T bestPrivate = 0;
        int    count = 0;

        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"nvcontainer.exe") != 0) continue;
                ++count;
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                       FALSE, pe.th32ProcessID);
                if (!h) continue;
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(h,
                        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                        sizeof(pmc)))
                {
                    if (pmc.PrivateUsage > bestPrivate) {
                        bestPrivate = pmc.PrivateUsage;
                        bestPid     = pe.th32ProcessID;
                    }
                }
                CloseHandle(h);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);

        g_LastBestPrivateBytes = bestPrivate;
        g_LastNvContainerCount = count;

        if (bestPrivate < kMinPrivateBytes) return 0;
        return bestPid;
    }

    inline bool ManualMap(HANDLE hProc,
                          const unsigned char* dllBytes,
                          size_t dllSize)
    {
        if (!hProc || !dllBytes || dllSize < sizeof(IMAGE_DOS_HEADER))
            return false;

        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dllBytes);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > dllSize)
            return false;
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            dllBytes + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return false;

        const DWORD imgSize = nt->OptionalHeader.SizeOfImage;
        const DWORD hdrSize = nt->OptionalHeader.SizeOfHeaders;

        LPVOID base = VirtualAllocEx(
            hProc,
            (LPVOID)nt->OptionalHeader.ImageBase,
            imgSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE);
        if (!base) {
            base = VirtualAllocEx(
                hProc, nullptr, imgSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE);
        }
        if (!base) return false;

        std::vector<unsigned char> img(imgSize, 0);

        std::memcpy(img.data(), dllBytes, hdrSize);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (sec[i].SizeOfRawData == 0) continue;
            if ((size_t)sec[i].PointerToRawData + sec[i].SizeOfRawData > dllSize)
                continue;
            if ((size_t)sec[i].VirtualAddress + sec[i].SizeOfRawData > imgSize)
                continue;
            std::memcpy(
                img.data() + sec[i].VirtualAddress,
                dllBytes + sec[i].PointerToRawData,
                sec[i].SizeOfRawData);
        }

        const uint64_t delta =
            (uint64_t)base - nt->OptionalHeader.ImageBase;
        if (delta != 0) {
            const auto& reloc = nt->OptionalHeader.DataDirectory[
                IMAGE_DIRECTORY_ENTRY_BASERELOC];
            if (reloc.Size > 0 && reloc.VirtualAddress != 0 &&
                reloc.VirtualAddress + reloc.Size <= imgSize)
            {
                DWORD off = reloc.VirtualAddress;
                const DWORD end = reloc.VirtualAddress + reloc.Size;
                while (off + sizeof(IMAGE_BASE_RELOCATION) <= end) {
                    auto* blk = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                        img.data() + off);
                    if (blk->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) break;
                    DWORD count = (blk->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION))
                                / sizeof(WORD);
                    WORD*  entries = reinterpret_cast<WORD*>(blk + 1);
                    for (DWORD i = 0; i < count; ++i) {
                        WORD type       = entries[i] >> 12;
                        WORD offInPage  = entries[i] & 0xFFF;
                        if (type == IMAGE_REL_BASED_DIR64) {
                            uint64_t* p = reinterpret_cast<uint64_t*>(
                                img.data() + blk->VirtualAddress + offInPage);
                            *p += delta;
                        }
                    }
                    off += blk->SizeOfBlock;
                }
            }
        }

        {
            const auto& imp = nt->OptionalHeader.DataDirectory[
                IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (imp.Size > 0 && imp.VirtualAddress != 0 &&
                imp.VirtualAddress + imp.Size <= imgSize)
            {
                auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                    img.data() + imp.VirtualAddress);
                while (desc->Name != 0) {
                    const char* modName =
                        reinterpret_cast<const char*>(img.data() + desc->Name);
                    HMODULE mod = LoadLibraryA(modName);
                    if (!mod) {
                        VirtualFreeEx(hProc, base, 0, MEM_RELEASE);
                        return false;
                    }
                    uint64_t* origThunk = reinterpret_cast<uint64_t*>(
                        img.data() + (desc->OriginalFirstThunk
                                      ? desc->OriginalFirstThunk
                                      : desc->FirstThunk));
                    uint64_t* firstThunk = reinterpret_cast<uint64_t*>(
                        img.data() + desc->FirstThunk);
                    for (; *origThunk; ++origThunk, ++firstThunk) {
                        FARPROC proc = nullptr;
                        if (*origThunk & IMAGE_ORDINAL_FLAG64) {
                            proc = GetProcAddress(
                                mod, (LPCSTR)(*origThunk & 0xFFFF));
                        } else {
                            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                                img.data() + *origThunk);
                            proc = GetProcAddress(mod, byName->Name);
                        }
                        if (!proc) {
                            VirtualFreeEx(hProc, base, 0, MEM_RELEASE);
                            return false;
                        }
                        *firstThunk = (uint64_t)proc;
                    }
                    ++desc;
                }
            }
        }

        SIZE_T wrote = 0;
        if (!WriteProcessMemory(hProc, base, img.data(), imgSize, &wrote) ||
            wrote != imgSize)
        {
            VirtualFreeEx(hProc, base, 0, MEM_RELEASE);
            return false;
        }

        uint64_t entryAddr =
            (uint64_t)base + nt->OptionalHeader.AddressOfEntryPoint;
        unsigned char stub[] = {
            0x48, 0xB9, 0,0,0,0,0,0,0,0,
            0xBA, 0x01, 0x00, 0x00, 0x00,
            0x4D, 0x31, 0xC0,
            0x48, 0xB8, 0,0,0,0,0,0,0,0,
            0x48, 0x83, 0xEC, 0x28,
            0xFF, 0xD0,
            0x48, 0x83, 0xC4, 0x28,
            0xC3
        };
        uint64_t baseU64 = (uint64_t)base;
        std::memcpy(&stub[ 2], &baseU64,  8);
        std::memcpy(&stub[20], &entryAddr, 8);

        LPVOID stubMem = VirtualAllocEx(
            hProc, nullptr, sizeof(stub),
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE);
        if (!stubMem) {
            VirtualFreeEx(hProc, base, 0, MEM_RELEASE);
            return false;
        }
        if (!WriteProcessMemory(hProc, stubMem, stub, sizeof(stub), &wrote) ||
            wrote != sizeof(stub))
        {
            VirtualFreeEx(hProc, stubMem, 0, MEM_RELEASE);
            VirtualFreeEx(hProc, base,    0, MEM_RELEASE);
            return false;
        }

        HANDLE hThread = CreateRemoteThread(
            hProc, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(stubMem),
            nullptr, 0, nullptr);
        if (!hThread) {
            VirtualFreeEx(hProc, stubMem, 0, MEM_RELEASE);
            VirtualFreeEx(hProc, base,    0, MEM_RELEASE);
            return false;
        }

        WaitForSingleObject(hThread, 5000);
        CloseHandle(hThread);

        VirtualFreeEx(hProc, stubMem, 0, MEM_RELEASE);

        g_LastLoadedBase  = (uint64_t)base;
        g_LastLoadedEntry = entryAddr;
        return true;
    }

    inline bool Load() {
        DWORD pid = FindNvContainerPid();
        if (!pid) return false;
        HANDLE h = OpenProcess(
            PROCESS_CREATE_THREAD  |
            PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION   |
            PROCESS_VM_READ        |
            PROCESS_VM_WRITE,
            FALSE, pid);
        if (!h) return false;
        bool ok = ManualMap(h, kNvspManipDll, kNvspManipDllSize);
        if (ok) g_LastLoadedPid = pid;
        CloseHandle(h);
        return ok;
    }

    inline bool Unload() {
        if (g_LastLoadedPid == 0 || g_LastLoadedBase == 0 || g_LastLoadedEntry == 0)
            return false;

        HANDLE h = OpenProcess(
            PROCESS_CREATE_THREAD  |
            PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION   |
            PROCESS_VM_READ        |
            PROCESS_VM_WRITE,
            FALSE, g_LastLoadedPid);
        if (!h) return false;

        unsigned char stub[] = {
            0x48, 0xB9, 0,0,0,0,0,0,0,0,
            0xBA, 0x00, 0x00, 0x00, 0x00,
            0x4D, 0x31, 0xC0,
            0x48, 0xB8, 0,0,0,0,0,0,0,0,
            0x48, 0x83, 0xEC, 0x28,
            0xFF, 0xD0,
            0x48, 0x83, 0xC4, 0x28,
            0xC3
        };
        std::memcpy(&stub[ 2], &g_LastLoadedBase,  8);
        std::memcpy(&stub[20], &g_LastLoadedEntry, 8);

        LPVOID stubMem = VirtualAllocEx(
            h, nullptr, sizeof(stub),
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE);
        if (!stubMem) { CloseHandle(h); return false; }

        SIZE_T wrote = 0;
        if (!WriteProcessMemory(h, stubMem, stub, sizeof(stub), &wrote) ||
            wrote != sizeof(stub))
        {
            VirtualFreeEx(h, stubMem, 0, MEM_RELEASE);
            CloseHandle(h);
            return false;
        }

        HANDLE hThread = CreateRemoteThread(
            h, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(stubMem),
            nullptr, 0, nullptr);
        if (!hThread) {
            VirtualFreeEx(h, stubMem, 0, MEM_RELEASE);
            CloseHandle(h);
            return false;
        }

        WaitForSingleObject(hThread, 5000);
        CloseHandle(hThread);
        VirtualFreeEx(h, stubMem, 0, MEM_RELEASE);
        CloseHandle(h);

        g_LastLoadedPid   = 0;
        g_LastLoadedBase  = 0;
        g_LastLoadedEntry = 0;
        return true;
    }

}
