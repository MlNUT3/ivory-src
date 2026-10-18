#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <map>
#include <cstring>
#include <cstdint>

#include <features/system/settings/settings.h>
#include <core/utility/nvspinject/NvspInject.hpp>

// NVIDIA streamproof - version-independent patches for ShadowPlay's capture.
//
// Part 1: the protected-content detector. nvspcap64.dll's EnumWindows callback
// calls GetWindowDisplayAffinity on every visible window; any window reporting
// != WDA_NONE marks "protected content" (NVFBC_ERROR_PROTECTED_CONTENT) and
// ShadowPlay refuses / stops recording. Instead of patching the detector at a
// hardcoded RVA (breaks on every GFE update), we patch nvspcap64's **IAT
// entry** for GetWindowDisplayAffinity to a stub that returns TRUE without
// writing the affinity - every window then reports WDA_NONE and no app is
// ever treated as protected. Works on any build.
//
// Part 2: force NvFBC's GDI capture backend. NvFBC's native capture path
// composites everything (ignores WDA exclusion); the GDI path (BitBlt)
// honours it. Located by scanning for the vendor check
//   CMP R9D, 0x10de ; JNZ <gdi-fail>
// and flipping the JNZ to an unconditional JMP. The 0x10de constant is
// stable across builds, so this survives GFE updates too.

namespace nvstream {

	// stub: BOOL GetWindowDisplayAffinity(hwnd, out) -> TRUE (out untouched = 0)
	static const BYTE kAffinityStub[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };

	// --- part 2 pattern: CMP R9D, 0x10de ; JNZ rel32 ---
	static const BYTE kVendorCmp[] = { 0x41, 0x81, 0xF9, 0xDE, 0x10, 0x00, 0x00, 0x0F, 0x85 };

	inline bool write_mem(HANDLE process, void* target, const void* data, size_t len)
	{
		DWORD old_protect = 0;
		if (!VirtualProtectEx(process, target, len, PAGE_EXECUTE_READWRITE, &old_protect))
			return false;
		const BOOL ok = WriteProcessMemory(process, target, data, len, nullptr);
		VirtualProtectEx(process, target, len, old_protect, &old_protect);
		FlushInstructionCache(process, target, len);
		return ok != FALSE;
	}

	inline bool read_mem(HANDLE process, const void* target, void* out, size_t len)
	{
		SIZE_T read = 0;
		return ReadProcessMemory(process, target, out, len, &read) && read == len;
	}

	// ------------------------------------------------------------------
	// Part 1: IAT-patch GetWindowDisplayAffinity in a remote module.
	// Walks the module's import table (via ReadProcessMemory), finds the
	// thunk for user32!GetWindowDisplayAffinity, and repoints it to a stub.
	// ------------------------------------------------------------------
	inline bool iat_patch_affinity(HANDLE process, BYTE* mod_base, DWORD pid)
	{
		// read PE headers from the remote module
		BYTE hdrs[4096] = {};
		if (!read_mem(process, mod_base, hdrs, sizeof(hdrs)))
			return false;

		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hdrs);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return false;
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(hdrs + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
			return false;

		const IMAGE_DATA_DIRECTORY& imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (imp.Size == 0 || imp.VirtualAddress == 0)
			return false;

		bool patched = false;
		BYTE desc_buf[sizeof(IMAGE_IMPORT_DESCRIPTOR)] = {};
		DWORD desc_rva = imp.VirtualAddress;
		while (desc_rva + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp.VirtualAddress + imp.Size)
		{
			if (!read_mem(process, mod_base + desc_rva, desc_buf, sizeof(desc_buf)))
				break;
			auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(desc_buf);
			if (desc->Name == 0)
				break;

			// read the dll name
			char dll_name[64] = {};
			if (!read_mem(process, mod_base + desc->Name, dll_name, sizeof(dll_name) - 1))
				break;
			if (_stricmp(dll_name, "user32.dll") != 0)
			{
				desc_rva += sizeof(IMAGE_IMPORT_DESCRIPTOR);
				continue;
			}

			// walk the original thunks, resolving each import name
			const DWORD oft_rva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
			DWORD oft = oft_rva, ft = desc->FirstThunk;
			while (oft != 0 && ft != 0)
			{
				ULONGLONG thunk = 0;
				if (!read_mem(process, mod_base + oft, &thunk, sizeof(thunk)))
					break;
				if (thunk == 0)
					break;

				if (!(thunk & IMAGE_ORDINAL_FLAG64))
				{
					IMAGE_IMPORT_BY_NAME byname{};
					if (read_mem(process, mod_base + (DWORD)(thunk & 0xFFFFFFFF), &byname, sizeof(byname)))
					{
						if (_stricmp(byname.Name, "GetWindowDisplayAffinity") == 0)
						{
							// found the IAT slot - write the remote stub
							BYTE* stub_remote = reinterpret_cast<BYTE*>(VirtualAllocEx(
								process, nullptr, sizeof(kAffinityStub),
								MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
							if (!stub_remote)
								break;
							SIZE_T wrote = 0;
							if (!WriteProcessMemory(process, stub_remote, kAffinityStub,
								sizeof(kAffinityStub), &wrote) || wrote != sizeof(kAffinityStub))
							{
								VirtualFreeEx(process, stub_remote, 0, MEM_RELEASE);
								break;
							}
							const ULONGLONG stub_addr = reinterpret_cast<ULONGLONG>(stub_remote);
							patched = write_mem(process, mod_base + ft, &stub_addr, sizeof(stub_addr));
							break;
						}
					}
				}
				oft += sizeof(ULONGLONG);
				ft += sizeof(ULONGLONG);
			}
			break; // user32 descriptor handled (or not found)
		}
		return patched;
	}

	// ------------------------------------------------------------------
	// Part 2: pattern-scan nvfbc64 for the vendor check and force the GDI path.
	// ------------------------------------------------------------------
	inline bool force_gdi_capture(HANDLE process, BYTE* fb_base, SIZE_T mod_size)
	{
		if (mod_size == 0 || mod_size > 64ull * 1024ull * 1024ull)
			return false;

		std::vector<unsigned char> image(mod_size);
		if (!read_mem(process, fb_base, image.data(), mod_size))
			return false;

		const SIZE_T pat_len = sizeof(kVendorCmp);
		for (SIZE_T i = 0; i + pat_len + 6 <= mod_size; i++)
		{
			if (memcmp(image.data() + i, kVendorCmp, pat_len) != 0)
				continue;

			// JNZ rel32 follows the CMP
			BYTE* jnz_remote = fb_base + i + 7;
			BYTE jnz_bytes[6] = {};
			if (!read_mem(process, jnz_remote, jnz_bytes, sizeof(jnz_bytes)))
				continue;
			if (jnz_bytes[0] != 0x0F || jnz_bytes[1] != 0x85)
				continue; // not the expected JNZ - keep scanning

			const int32_t rel = *reinterpret_cast<int32_t*>(jnz_bytes + 2);
			const BYTE* next = jnz_remote + 6;
			const BYTE* target = next + rel;

			// rewrite as JMP rel32 to the same target
			BYTE jmp[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
			const int32_t new_rel = static_cast<int32_t>((target - (next + 5)));
			memcpy(jmp + 1, &new_rel, sizeof(new_rel));
			if (!write_mem(process, jnz_remote, jmp, sizeof(jmp)))
				continue;
			return true; // first occurrence only
		}
		return false;
	}

	inline void enable_debug_privilege()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
			return;
		TOKEN_PRIVILEGES tp = { 0 };
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		if (LookupPrivilegeValueA(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid))
			AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
		CloseHandle(token);
	}

	inline std::vector<DWORD> nv_pids()
	{
		std::vector<DWORD> pids;
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE)
			return pids;
		PROCESSENTRY32W pe = { 0 };
		pe.dwSize = sizeof(pe);
		if (Process32FirstW(snap, &pe))
		{
			do
			{
				// capture modules live in NVIDIA processes (nvcontainer, NVIDIA App)
				// but nvspcap64 can also load into the captured host - include it.
				if (_wcsnicmp(pe.szExeFile, L"nv", 2) == 0 ||
					_wcsicmp(pe.szExeFile, L"Discord.exe") == 0 ||
					_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe") == 0 ||
					_wcsicmp(pe.szExeFile, L"explorer.exe") == 0)
					pids.push_back(pe.th32ProcessID);
			} while (Process32NextW(snap, &pe));
		}
		CloseHandle(snap);
		return pids;
	}

	inline BYTE* find_module_base(DWORD pid, const wchar_t* name, SIZE_T* size_out = nullptr)
	{
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
		if (snap == INVALID_HANDLE_VALUE)
			return nullptr;
		MODULEENTRY32W me = { 0 };
		me.dwSize = sizeof(me);
		BYTE* base = nullptr;
		if (Module32FirstW(snap, &me))
			do {
				if (_wcsicmp(me.szModule, name) == 0)
				{
					base = me.modBaseAddr;
					if (size_out) *size_out = me.modBaseSize;
					break;
				}
			} while (Module32NextW(snap, &me));
		CloseHandle(snap);
		return base;
	}

	inline void patch_process(HANDLE process, DWORD pid)
	{
		BYTE* sp_base = find_module_base(pid, L"nvspcap64.dll");
		if (sp_base)
			iat_patch_affinity(process, sp_base, pid);

		SIZE_T fb_size = 0;
		BYTE* fb_base = find_module_base(pid, L"nvfbc64.dll", &fb_size);
		if (fb_base)
			force_gdi_capture(process, fb_base, fb_size);
	}

	// ------------------------------------------------------------------
	// Part 3: protected-app bypass (ported from ShadowPlay_Patcher).
	// Inside the *render* nvcontainer (the one with nvd3dumx.dll loaded),
	// near-JMP hook two APIs so ShadowPlay's protected/conflicting-app
	// detection finds nothing:
	//   user32!GetWindowDisplayAffinity -> xor eax,eax ; ret  (FALSE: no
	//       window is ever reported as protected)
	//   kernel32!Module32FirstW         -> xor eax,eax ; ret  (FALSE: the
	//       module enumeration behind the protected-app scan finds nothing)
	// Near-JMP hooks are version-independent (no hardcoded RVAs) and
	// idempotent (the remote export's first byte marks them as applied).
	// ------------------------------------------------------------------
	inline uintptr_t alloc_near(HANDLE process, uintptr_t desired, SIZE_T size)
	{
		const SIZE_T step = 0x1000;
		const uintptr_t range = 0x20000000ULL - 0x2000ULL; // fit a rel32 JMP
		for (uintptr_t addr = desired - range; addr < desired + range; addr += step)
		{
			void* mem = VirtualAllocEx(process, reinterpret_cast<void*>(addr),
				size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (mem)
				return reinterpret_cast<uintptr_t>(mem);
		}
		return 0;
	}

	inline bool asm_jmp_near(uint8_t* out, uintptr_t source, uintptr_t target)
	{
		const intptr_t rel = static_cast<intptr_t>(target) - static_cast<intptr_t>(source + 5);
		if (rel > 0x7FFFFFFF || rel < -0x7FFFFFFF)
			return false;
		out[0] = 0xE9;
		*reinterpret_cast<int32_t*>(out + 1) = static_cast<int32_t>(rel);
		return true;
	}

	inline bool hook_remote_export(HANDLE process, const wchar_t* module_name, const char* func_name)
	{
		const DWORD pid = GetProcessId(process);
		const uintptr_t remote_base = reinterpret_cast<uintptr_t>(find_module_base(pid, module_name));
		if (!remote_base)
			return false;

		// resolve the export: local offset + remote module base
		HMODULE local = LoadLibraryW(module_name);
		if (!local)
			return false;
		const FARPROC local_fn = GetProcAddress(local, func_name);
		if (!local_fn)
		{
			FreeLibrary(local);
			return false;
		}
		const uintptr_t offset = reinterpret_cast<uintptr_t>(local_fn) - reinterpret_cast<uintptr_t>(local);
		FreeLibrary(local);
		const uintptr_t remote_fn = remote_base + offset;

		// idempotency: our JMP already in place?
		BYTE head = 0;
		if (read_mem(process, reinterpret_cast<const void*>(remote_fn), &head, 1) && head == 0xE9)
			return true;

		// stub: xor eax,eax ; ret  (returns FALSE)
		const uintptr_t stub = alloc_near(process, remote_fn, 0x1000);
		if (!stub)
			return false;
		const unsigned char payload[] = { 0x48, 0x31, 0xC0, 0xC3 };
		SIZE_T wrote = 0;
		if (!WriteProcessMemory(process, reinterpret_cast<void*>(stub), payload,
			sizeof(payload), &wrote) || wrote != sizeof(payload))
		{
			VirtualFreeEx(process, reinterpret_cast<void*>(stub), 0, MEM_RELEASE);
			return false;
		}

		uint8_t jmp[6] = {};
		if (!asm_jmp_near(jmp, remote_fn, stub))
		{
			VirtualFreeEx(process, reinterpret_cast<void*>(stub), 0, MEM_RELEASE);
			return false;
		}
		jmp[5] = 0x90;
		if (!write_mem(process, reinterpret_cast<void*>(remote_fn), jmp, sizeof(jmp)))
		{
			VirtualFreeEx(process, reinterpret_cast<void*>(stub), 0, MEM_RELEASE);
			return false;
		}
		return true;
	}

	inline void patch_protected_app_bypass()
	{
		// the render nvcontainer is the one with nvd3dumx.dll loaded
		DWORD render_container = 0;
		int found = 0;
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap != INVALID_HANDLE_VALUE)
		{
			PROCESSENTRY32W pe = { 0 };
			pe.dwSize = sizeof(pe);
			if (Process32FirstW(snap, &pe))
			{
				do
				{
					if (_wcsicmp(pe.szExeFile, L"nvcontainer.exe") != 0)
						continue;
					if (find_module_base(pe.th32ProcessID, L"nvd3dumx.dll"))
					{
						++found;
						render_container = pe.th32ProcessID;
					}
				} while (Process32NextW(snap, &pe));
			}
			CloseHandle(snap);
		}
		if (found != 1 || render_container == 0)
			return;

		HANDLE process = OpenProcess(
			PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
			FALSE, render_container);
		if (!process)
			return;

		hook_remote_export(process, L"USER32.dll", "GetWindowDisplayAffinity");
		hook_remote_export(process, L"KERNEL32.DLL", "Module32FirstW");
		CloseHandle(process);
	}

	inline void run()
	{
		enable_debug_privilege();

		for (;;)
		{
			patch_protected_app_bypass();

			// NvspInject payload: (re)inject into nvcontainer whenever it is
			// available and streamproof is enabled - retries automatically if
			// nvcontainer starts late or restarts, unloads when disabled
			if (settings::streamproof)
			{
				const DWORD container_pid = NvspInject::FindNvContainerPid();
				if (container_pid != 0 && container_pid != NvspInject::g_LastLoadedPid)
				{
					if (NvspInject::Load())
						bootlog("streamproof: nvsp manip loaded into nvcontainer (pid %lu)",
							(unsigned long)NvspInject::g_LastLoadedPid);
					else
						bootlog("streamproof: nvsp inject failed (pid %lu) - access denied? run as admin",
							(unsigned long)container_pid);
				}
			}
			else if (NvspInject::g_LastLoadedPid != 0)
			{
				if (NvspInject::Unload())
					bootlog("streamproof: nvsp manip unloaded from nvcontainer");
			}

			for (DWORD pid : nv_pids())
			{
				if (pid == 0 || pid == GetCurrentProcessId())
					continue;

				BYTE* sp_base = find_module_base(pid, L"nvspcap64.dll");
				BYTE* fb_base = find_module_base(pid, L"nvfbc64.dll");
				if (!sp_base && !fb_base)
					continue;

				HANDLE process = OpenProcess(
					PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
					FALSE, pid);
				if (!process)
					continue;

				patch_process(process, pid);
				CloseHandle(process);
			}
			Sleep(1500);
		}
	}

} // namespace nvstream
