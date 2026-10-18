#pragma once
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <string>

// Lightweight scan / manual-check resistance, in the style FiveM bypassers use:
// anti-cheat scans and staff PC checks don't see your screen - they read what
// Windows already recorded (loaded module lists, threads, window titles,
// registry, recent files). This module removes the readable traces:
//
//   * module_mode 1 - rename our DLL inside the PEB LDR entry, so module
//     enumeration shows an innocuous system-DLL name instead of Ivory.dll
//   * module_mode 2 - fully unlink our DLL from the PEB loader lists, so
//     module enumeration (Process Hacker, toolhelp, module scans) cannot see
//     it at all. Exception handling is unaffected - it uses the separate
//     LdrpInvertedFunctionTable, not these lists.
//   * thread name spoofing - our threads carry innocuous names
//   * console hiding - no console window for a manual checker to read
//
// Deliberately NOT done (too heavy / too fragile): PE header wipes (breaks the
// exception directory), USN journal edits, driver-level tricks.

namespace stealth {

	// complete loader structs - winternl.h's are opaque/incomplete
	struct ldr_entry_t {
		LIST_ENTRY InLoadOrderLinks;
		LIST_ENTRY InMemoryOrderLinks;
		LIST_ENTRY InInitializationOrderLinks;
		PVOID DllBase;
		PVOID EntryPoint;
		ULONG SizeOfImage;
		UNICODE_STRING FullDllName;
		UNICODE_STRING BaseDllName;
	};

	struct peb_ldr_data_t {
		ULONG Length;
		BOOLEAN Initialized;
		PVOID SsHandle;
		LIST_ENTRY InLoadOrderModuleList;
		LIST_ENTRY InMemoryOrderModuleList;
		LIST_ENTRY InInitializationOrderModuleList;
	};

	inline HMODULE this_module()
	{
		HMODULE mod = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&this_module), &mod);
		return mod;
	}

	inline ldr_entry_t* find_own_ldr_entry()
	{
		HMODULE mod = this_module();
		if (!mod)
			return nullptr;

		auto* peb = reinterpret_cast<PEB*>(__readgsqword(0x60));
		if (!peb || !peb->Ldr)
			return nullptr;

		auto* ldr = reinterpret_cast<peb_ldr_data_t*>(peb->Ldr);
		auto* head = &ldr->InLoadOrderModuleList;
		for (auto* cur = head->Flink; cur && cur != head; cur = cur->Flink)
		{
			auto* entry = CONTAINING_RECORD(cur, ldr_entry_t, InLoadOrderLinks);
			if (reinterpret_cast<HMODULE>(entry->DllBase) == mod)
				return entry;
		}
		return nullptr;
	}

	// rename the module in the loader entry (keeps it linked - threads still
	// resolve to a "backed" module, but it no longer reads as Ivory.dll)
	inline void rename_module(const wchar_t* new_name)
	{
		auto* entry = find_own_ldr_entry();
		if (!entry)
			return;

		static wchar_t name_buffer[128] = { 0 };
		wcsncpy_s(name_buffer, new_name, _TRUNCATE);

		entry->BaseDllName.Buffer = name_buffer;
		entry->BaseDllName.Length = (USHORT)(wcslen(name_buffer) * sizeof(wchar_t));
		entry->BaseDllName.MaximumLength = (USHORT)sizeof(name_buffer);
		entry->FullDllName = entry->BaseDllName;
	}

	// fully unlink the module from all three PEB loader lists
	inline void unlink_module()
	{
		auto* entry = find_own_ldr_entry();
		if (!entry)
			return;

		entry->InLoadOrderLinks.Blink->Flink = entry->InLoadOrderLinks.Flink;
		entry->InLoadOrderLinks.Flink->Blink = entry->InLoadOrderLinks.Blink;
		entry->InMemoryOrderLinks.Blink->Flink = entry->InMemoryOrderLinks.Flink;
		entry->InMemoryOrderLinks.Flink->Blink = entry->InMemoryOrderLinks.Blink;
		entry->InInitializationOrderLinks.Blink->Flink = entry->InInitializationOrderLinks.Flink;
		entry->InInitializationOrderLinks.Flink->Blink = entry->InInitializationOrderLinks.Blink;
	}

	inline void set_name(const wchar_t* name)
	{
		HMODULE mod = this_module();
		if (!mod)
			return;
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(mod);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return;
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(mod) + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
			return;

		ULONG_PTR lo = reinterpret_cast<ULONG_PTR>(mod);
		ULONG_PTR hi = lo + nt->OptionalHeader.SizeOfImage;

		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snap == INVALID_HANDLE_VALUE)
			return;

		const DWORD pid = GetCurrentProcessId();
		THREADENTRY32 te{};
		te.dwSize = sizeof(te);
		if (Thread32First(snap, &te))
		{
			do
			{
				if (te.th32OwnerProcessID != pid)
					continue;

				HANDLE thread = OpenThread(THREAD_SET_LIMITED_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
				if (!thread)
					continue;

				// only rename threads that started inside our module
				PVOID start = nullptr;
				ULONG ret = 0;
				bool ours = false;
				{
					using fnNtQit = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
					static fnNtQit nt_qit = nullptr;
					if (!nt_qit)
						nt_qit = reinterpret_cast<fnNtQit>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread"));
					if (nt_qit && nt_qit(thread, 9 /*ThreadQuerySetWin32StartAddress*/, &start, sizeof(start), &ret) == 0)
					{
						ULONG_PTR s = reinterpret_cast<ULONG_PTR>(start);
						ours = (s >= lo && s < hi);
					}
				}

				// our std::thread bodies may live in the CRT, so also treat
				// unnamed threads in this process as ours
				if (ours || start == nullptr)
					SetThreadDescription(thread, name);

				CloseHandle(thread);
			} while (Thread32Next(snap, &te));
		}
		CloseHandle(snap);
	}

} // namespace stealth
