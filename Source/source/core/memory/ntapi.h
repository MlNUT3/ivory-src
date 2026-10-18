#pragma once
#include <windows.h>

// Win32-signature-compatible wrappers that resolve the process-memory APIs
// dynamically at runtime. Keeping ReadProcessMemory / WriteProcessMemory /
// VirtualAllocEx out of the import table removes the classic "external cheat
// or injector" import signature that AV heuristics - and FiveM-style scanners
// reading module imports - look for.

namespace ntapi
{
	inline BOOL(WINAPI* pReadProcessMemory)(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T*) = nullptr;
	inline BOOL(WINAPI* pWriteProcessMemory)(HANDLE, LPVOID, LPCVOID, SIZE_T, SIZE_T*) = nullptr;
	inline LPVOID(WINAPI* pVirtualAllocEx)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD) = nullptr;

	inline void resolve()
	{
		static bool done = false;
		if (done)
			return;
		done = true;

		HMODULE k = GetModuleHandleA("kernel32.dll");
		if (!k)
			return;

		pReadProcessMemory = reinterpret_cast<decltype(pReadProcessMemory)>(GetProcAddress(k, "ReadProcessMemory"));
		pWriteProcessMemory = reinterpret_cast<decltype(pWriteProcessMemory)>(GetProcAddress(k, "WriteProcessMemory"));
		pVirtualAllocEx = reinterpret_cast<decltype(pVirtualAllocEx)>(GetProcAddress(k, "VirtualAllocEx"));
	}

	inline BOOL WINAPI ReadProcessMemory(HANDLE process, LPCVOID base, LPVOID buffer, SIZE_T size, SIZE_T* read)
	{
		resolve();
		return pReadProcessMemory ? pReadProcessMemory(process, base, buffer, size, read) : FALSE;
	}

	inline BOOL WINAPI WriteProcessMemory(HANDLE process, LPVOID base, LPCVOID buffer, SIZE_T size, SIZE_T* written)
	{
		resolve();
		return pWriteProcessMemory ? pWriteProcessMemory(process, base, buffer, size, written) : FALSE;
	}

	inline LPVOID WINAPI VirtualAllocEx(HANDLE process, LPVOID address, SIZE_T size, DWORD type, DWORD protect)
	{
		resolve();
		return pVirtualAllocEx ? pVirtualAllocEx(process, address, size, type, protect) : nullptr;
	}
}
