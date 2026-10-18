#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <core/diagnostics/bootlog.h>
#include <features/system/config/config.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

// Cleanly eject (unload) this DLL from the host process without touching the
// host itself.
//
// A module can't free itself from inside its own code, and FreeLibraryAndExit-
// Thread only ends the *calling* thread - so the flow is:
//   1. set `requested` so the render loop stops drawing
//   2. a worker thread destroys the overlay window
//   3. terminate every other thread this module spawned (their code is about
//      to be unmapped)
//   4. FreeLibraryAndExitThread unmaps the module and ends the worker
namespace sys_eject
{
	inline std::atomic<bool> requested{ false };
	inline HWND overlay_window = nullptr;

	inline std::mutex thread_mtx;
	inline std::vector<DWORD> thread_ids;
	inline std::vector<DWORD> resident_tids;   // threads that survive a soft unload

	inline void register_current_thread(bool keep_on_soft_unload = false)
	{
		std::lock_guard<std::mutex> lock(thread_mtx);
		const DWORD tid = GetCurrentThreadId();
		thread_ids.push_back(tid);
		if (keep_on_soft_unload)
			resident_tids.push_back(tid);
	}

	// unload the cheat's features but keep the process + UI alive:
	// terminates every per-load thread (aimbot, cache, rescan, wallcheck...)
	// while resident threads (nvstream, mesh workers, watcher, installer) and
	// the caller (the UI thread) keep running. F2 can then re-load.
	inline void soft_unload()
	{
		const DWORD self = GetCurrentThreadId();
		size_t terminated = 0;
		{
			std::lock_guard<std::mutex> lock(thread_mtx);
			for (DWORD tid : thread_ids)
			{
				if (tid == 0 || tid == self)
					continue;
				bool resident = false;
				for (DWORD r : resident_tids)
				{
					if (r == tid) { resident = true; break; }
				}
				if (resident)
					continue;
				HANDLE thread = OpenThread(THREAD_TERMINATE, FALSE, tid);
				if (thread)
				{
					TerminateThread(thread, 0);
					CloseHandle(thread);
					terminated++;
				}
			}
		}
		bootlog("soft unload: terminated %zu per-load thread(s) - host alive", terminated);
	}

	inline HMODULE this_module()
	{
		HMODULE mod = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&this_module), &mod);
		return mod;
	}

	// true when running as the standalone exe (not injected into a host)
	inline bool is_exe()
	{
		return GetModuleHandleW(nullptr) == this_module();
	}

	inline void worker()
	{
		// give the render loop a moment to notice `requested` and stop
		bootlog("eject: worker started");
		Sleep(200);

		if (overlay_window && IsWindow(overlay_window))
		{
			ShowWindow(overlay_window, SW_HIDE);
			DestroyWindow(overlay_window);
			overlay_window = nullptr;
		}

		// standalone exe: ejecting = closing this process cleanly (the cheat
		// IS the process - nothing else to keep alive)
		if (is_exe())
		{
			bootlog("eject: standalone exe - exiting cleanly");
			ExitProcess(0);
		}

		const DWORD self = GetCurrentThreadId();
		size_t terminated = 0;
		{
			std::lock_guard<std::mutex> lock(thread_mtx);
			for (DWORD tid : thread_ids)
			{
				if (tid == 0 || tid == self)
					continue;
				HANDLE thread = OpenThread(THREAD_TERMINATE, FALSE, tid);
				if (thread)
				{
					TerminateThread(thread, 0);
					CloseHandle(thread);
					terminated++;
				}
			}
		}

		bootlog("eject: terminated %zu tracked thread(s), unmapping module", terminated);

		// verify the unmap actually happens (this thread dies at the call below,
		// so a helper checks from outside)
		std::thread([]() {
			for (int i = 0; i < 40; i++)
			{
				Sleep(100);
				HMODULE check = nullptr;
				if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&sys_eject::requested), &check))
				{
					bootlog("eject: module fully unloaded after %d ms", (i + 1) * 100);
					return;
				}
			}
			bootlog("eject: WARNING - module still mapped after 4s (refcount held by host?)");
		}).detach();

		Sleep(50);
		FreeLibraryAndExitThread(this_module(), 0);
	}

	inline void request()
	{
		if (requested.exchange(true))
			return; // already ejecting
		std::thread(worker).detach();
	}

	// =========================================================================
	// self-destruct
	//
	// Anti-forensic exit. Design follows the scanner's own rules: DELETING a
	// file is a permanent USN record that name-rules match forever, so the
	// executable is removed with NT POSIX-semester semantics (delete-while-
	// running, Win10+), data files are TRUNCATED in place (overwrite-class
	// USN records carry no name rule) rather than deleted, and everything
	// in RAM that means anything is zeroed first.
	// =========================================================================
	namespace detail
	{
		// truncate a file to zero WITHOUT deleting it - the directory entry
		// survives (no USN delete record), the content is gone (no YARA hit)
		inline void neutralize_file(const std::wstring& path)
		{
			HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE)
				return;
			SetFilePointer(h, 0, nullptr, FILE_BEGIN);
			SetEndOfFile(h);
			FlushFileBuffers(h);
			CloseHandle(h);
		}

		// POSIX-semantics delete: works on a running executable (Win10 1803+).
		// The image stays mapped; the directory entry is removed immediately.
		// `exe_image` selects the host exe (standalone) or our own dll image
		// (injected) - never the wrong one, and never the innocent host when
		// we are the injected module.
		inline bool posix_delete_self(bool exe_image)
		{
			HMODULE base = exe_image ? GetModuleHandleW(nullptr) : this_module();
			if (!base)
				return false;

			wchar_t path[MAX_PATH] = {};
			if (!GetModuleFileNameW(base, path, MAX_PATH))
				return false;

			// rename to a meaningless random name first so the delete record
			// never carries the distributed name
			std::wstring dir = path;
			size_t slash = dir.find_last_of(L'\\');
			if (slash != std::wstring::npos)
				dir = dir.substr(0, slash + 1);

			wchar_t random_name[24] = {};
			for (int i = 0; i < 16; i++)
				random_name[i] = L"aAbBcCdDeEfF0123456789"[rand() % 22];
			std::wstring new_path = dir + random_name;
			if (!MoveFileW(path, new_path.c_str()))
				new_path = path; // fall back to deleting under the original name

			HANDLE h = CreateFileW(new_path.c_str(), DELETE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE)
				return false;

			using NtSetInformationFile_t = NTSTATUS(NTAPI*)(HANDLE, PVOID*, PVOID, ULONG, ULONG);
			auto nt_set = (NtSetInformationFile_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile");
			if (!nt_set)
			{
				CloseHandle(h);
				return false;
			}

			struct FILE_DISPOSITION_INFORMATION_EX
			{
				ULONG Flags;
			};
			// FILE_DISPOSITION_DELETE (0x1) | FILE_DISPOSITION_POSIX_SEMANTICS (0x2)
			FILE_DISPOSITION_INFORMATION_EX info{};
			info.Flags = 0x1 | 0x2;

			PVOID io_status[2] = {};
			NTSTATUS st = nt_set(h, io_status, &info, sizeof(info), 60 /* FileDispositionInformationEx */);
			CloseHandle(h);
			return NT_SUCCESS(st);
		}

		// neutralize every file we ever dropped next to the running image /
		// in %TEMP%. Truncation only - never delete records.
		inline void neutralize_artifacts()
		{
			const bool exe_image = is_exe();
			HMODULE base = exe_image ? GetModuleHandleW(nullptr) : this_module();
			wchar_t image_path[MAX_PATH] = {};
			GetModuleFileNameW(base, image_path, MAX_PATH);
			std::wstring dir = image_path;
			size_t slash = dir.find_last_of(L'\\');
			if (slash != std::wstring::npos)
				dir = dir.substr(0, slash + 1);

			// the payload dll + loader, if present: truncated, not deleted.
			// (the currently-running module's file can't be opened for write
			// anyway - the attempt is harmless)
			neutralize_file(dir + L"nvldumdx.dll");
			neutralize_file(dir + L"nvldumdx_stage.dll");
			neutralize_file(dir + L"IvoryLoader.exe");

			// our boot log in %TEMP% - truncate (never delete: a delete record
			// of a cheat-named file is permanent)
			wchar_t temp[MAX_PATH] = {};
			if (GetTempPathW(MAX_PATH, temp))
				neutralize_file(std::wstring(temp) + L"ivory_boot.txt");
		}
	}

	// wipe all volatile state, neutralize artifacts, remove the executable
	inline void self_destruct()
	{
		requested.store(true);

		// 1. RAM: config store, boot log ring, notification text
		config::wipe_config_mem();

		// 2. disk: neutralize (truncate) every artifact we ever created
		detail::neutralize_artifacts();

		// 3. stop all per-load worker threads
		soft_unload();

		// 4. the running image itself - POSIX delete works while mapped
		detail::posix_delete_self(is_exe());

		// 5. exit; for the injected case the host stays alive
		if (is_exe())
			ExitProcess(0);
		else
			FreeLibraryAndExitThread(this_module(), 0);
	}
}
