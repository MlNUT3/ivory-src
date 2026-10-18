#include "inject.h"

#include <vector>
#include <cstring>

static_assert(sizeof(void*) == 8, "x64 only");

/*
 * Remote stub that invokes the image entry point with the correct x64 calling
 * convention:
 *     mov rcx, <imageBase>      ; HINSTANCE
 *     mov edx, <reason>         ; fdwReason
 *     mov rax, <entry>
 *     sub rsp, 28h
 *     call rax
 *     add rsp, 28h
 *     xor eax, eax
 *     ret
 * CreateRemoteThread only forwards one argument, which is why a stub is used
 * instead of pointing the thread straight at DllMain.
 */
static const size_t kStubSize = 38;
static void BuildEntryStub(BYTE* s, uintptr_t imageBase, DWORD reason, uintptr_t entry) {
  s[0] = 0x48; s[1] = 0xB9;                       // mov rcx, imm64
  memcpy(s + 2, &imageBase, 8);
  s[10] = 0xBA;                                   // mov edx, imm32
  memcpy(s + 11, &reason, 4);
  s[15] = 0x48; s[16] = 0xB8;                     // mov rax, imm64
  memcpy(s + 17, &entry, 8);
  s[25] = 0x48; s[26] = 0x83; s[27] = 0xEC; s[28] = 0x28;  // sub rsp, 28h
  s[29] = 0xFF; s[30] = 0xD0;                     // call rax
  s[31] = 0x48; s[32] = 0x83; s[33] = 0xC4; s[34] = 0x28;  // add rsp, 28h
  s[35] = 0x33; s[36] = 0xC0;                     // xor eax, eax
  s[37] = 0xC3;                                   // ret
}

/* Run the entry point remotely and wait for it. */
static bool CallRemoteEntry(HANDLE hProc, uintptr_t imageBase, DWORD reason, uintptr_t entry, DWORD timeoutMs) {
  BYTE stub[kStubSize];
  BuildEntryStub(stub, imageBase, reason, entry);

  LPVOID mem = VirtualAllocEx(hProc, NULL, kStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!mem) return false;
  if (!WriteProcessMemory(hProc, mem, stub, kStubSize, NULL)) {
    VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
    return false;
  }
  HANDLE th = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)mem, NULL, 0, NULL);
  bool ok = th != NULL;
  if (th) {
    WaitForSingleObject(th, timeoutMs);
    CloseHandle(th);
  }
  VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
  return ok;
}

bool ImageIsDll(const void* image, SIZE_T imageSize) {
  if (!image || imageSize < sizeof(IMAGE_DOS_HEADER)) return false;
  auto dos = (const IMAGE_DOS_HEADER*)image;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  if ((SIZE_T)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > imageSize) return false;
  auto nt = (const IMAGE_NT_HEADERS64*)((const BYTE*)image + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  return (nt->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0;
}

bool ManualMapDll(HANDLE hProc, const void* image, SIZE_T imageSize, uintptr_t* outBase) {
  if (outBase) *outBase = 0;
  if (!hProc || !image || imageSize < sizeof(IMAGE_DOS_HEADER)) return false;

  auto dos = (const IMAGE_DOS_HEADER*)image;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  if ((SIZE_T)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > imageSize) return false;
  auto nt = (const IMAGE_NT_HEADERS64*)((const BYTE*)image + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;

  const SIZE_T sizeOfImage = nt->OptionalHeader.SizeOfImage;
  const SIZE_T sizeOfHeaders = nt->OptionalHeader.SizeOfHeaders;
  if (!sizeOfImage || sizeOfImage < sizeOfHeaders) return false;

  // Prefer the image's own base so relocations can be skipped.
  LPVOID remote = VirtualAllocEx(hProc, (LPVOID)(uintptr_t)nt->OptionalHeader.ImageBase,
                                 sizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if (!remote)
    remote = VirtualAllocEx(hProc, NULL, sizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if (!remote) return false;
  const uintptr_t base = (uintptr_t)remote;

  // Assemble the image locally, then commit it in a single write.
  std::vector<BYTE> img(sizeOfImage, 0);
  memcpy(img.data(), image, sizeOfHeaders);

  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const IMAGE_SECTION_HEADER& s = sec[i];
    if (!s.SizeOfRawData) continue;
    if ((SIZE_T)s.PointerToRawData + s.SizeOfRawData > imageSize) { VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); return false; }
    SIZE_T n = s.SizeOfRawData;
    if ((SIZE_T)s.VirtualAddress + n > sizeOfImage) n = sizeOfImage - s.VirtualAddress;
    memcpy(img.data() + s.VirtualAddress, (const BYTE*)image + s.PointerToRawData, n);
  }

  // Base relocations.
  const intptr_t delta = (intptr_t)base - (intptr_t)nt->OptionalHeader.ImageBase;
  if (delta) {
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (dir.Size) {
      BYTE* p = img.data() + dir.VirtualAddress;
      BYTE* end = p + dir.Size;
      while (p + sizeof(IMAGE_BASE_RELOCATION) <= end) {
        auto blk = (const IMAGE_BASE_RELOCATION*)p;
        if (!blk->SizeOfBlock) break;
        const DWORD count = (blk->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        const WORD* e = (const WORD*)(blk + 1);
        for (DWORD i = 0; i < count; ++i) {
          const WORD type = e[i] >> 12;
          const WORD off = e[i] & 0x0FFF;
          BYTE* at = img.data() + blk->VirtualAddress + off;
          if (at >= img.data() + sizeOfImage) { VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); return false; }
          if (type == IMAGE_REL_BASED_DIR64 && at + 8 <= img.data() + sizeOfImage)
            *(uint64_t*)at += (uint64_t)delta;
          else if (type == IMAGE_REL_BASED_HIGHLOW && at + 4 <= img.data() + sizeOfImage)
            *(uint32_t*)at += (uint32_t)delta;
          else if (type != IMAGE_REL_BASED_ABSOLUTE) { VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); return false; }
        }
        p += blk->SizeOfBlock;
      }
    }
  }

  // Imports, resolved against the local loader (same session/arch, so the
  // addresses are valid inside the target too).
  const IMAGE_DATA_DIRECTORY& imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (imp.Size) {
    auto desc = (const IMAGE_IMPORT_DESCRIPTOR*)(img.data() + imp.VirtualAddress);
    for (; desc->Name; ++desc) {
      const char* mod = (const char*)(img.data() + desc->Name);
      HMODULE h = LoadLibraryA(mod);
      if (!h) { VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); return false; }
      auto thunk = (IMAGE_THUNK_DATA64*)(img.data() + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
      auto iat = (IMAGE_THUNK_DATA64*)(img.data() + desc->FirstThunk);
      for (; thunk->u1.AddressOfData; ++thunk, ++iat) {
        FARPROC fn = NULL;
        if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
          fn = GetProcAddress(h, (LPCSTR)(uintptr_t)(thunk->u1.Ordinal & 0xFFFF));
        else {
          auto ibn = (const IMAGE_IMPORT_BY_NAME*)(img.data() + thunk->u1.AddressOfData);
          fn = GetProcAddress(h, (LPCSTR)ibn->Name);
        }
        if (!fn) { VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); return false; }
        iat->u1.Function = (uint64_t)fn;
      }
    }
  }

  if (!WriteProcessMemory(hProc, remote, img.data(), sizeOfImage, NULL)) {
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    return false;
  }

  // Per-section protections.
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const IMAGE_SECTION_HEADER& s = sec[i];
    const DWORD c = s.Characteristics;
    const bool x = (c & IMAGE_SCN_MEM_EXECUTE) != 0;
    const bool r = (c & IMAGE_SCN_MEM_READ) != 0;
    const bool w = (c & IMAGE_SCN_MEM_WRITE) != 0;
    DWORD prot;
    if (x) prot = w ? PAGE_EXECUTE_READWRITE : (r ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
    else   prot = w ? PAGE_READWRITE : (r ? PAGE_READONLY : PAGE_NOACCESS);
    DWORD old = 0;
    VirtualProtectEx(hProc, (LPVOID)(base + s.VirtualAddress),
                     s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData, prot, &old);
  }

  const uintptr_t entry = base + nt->OptionalHeader.AddressOfEntryPoint;
  if (!CallRemoteEntry(hProc, base, DLL_PROCESS_ATTACH, entry, 20000)) {
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    return false;
  }

  if (outBase) *outBase = base;
  return true;
}

bool UnloadMappedDll(HANDLE hProc, uintptr_t base) {
  if (!hProc || !base) return false;

  BYTE hdr[0x1000] = {0};
  SIZE_T got = 0;
  if (!ReadProcessMemory(hProc, (LPCVOID)base, hdr, sizeof(hdr), &got) || got < 0x100) return false;
  auto dos = (const IMAGE_DOS_HEADER*)hdr;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  if ((SIZE_T)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > sizeof(hdr)) return false;
  auto nt = (const IMAGE_NT_HEADERS64*)(hdr + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;

  CallRemoteEntry(hProc, base, DLL_PROCESS_DETACH, base + nt->OptionalHeader.AddressOfEntryPoint, 20000);
  return VirtualFreeEx(hProc, (LPVOID)base, 0, MEM_RELEASE) != FALSE;
}
