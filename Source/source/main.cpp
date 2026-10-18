#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <timeapi.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "winmm.lib")

#include <thread>
#include <chrono>
#include <mutex>
#include <functional>
#include <memory>
#include <cstdio>
#include <cstdarg>
#include <dbghelp.h>
#include <immintrin.h>
#include <tlhelp32.h>
#include <psapi.h>

#pragma comment(lib, "dbghelp.lib")


#include <core/globals.h>
#include <core/diagnostics/bootlog.h>
#include <sdk/sdk.h>
#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <core/logger/logger.h>
#include <features/visuals/geometry/wallcheck.h>
#include <features/visuals/geometry/visualcheck.h>
#include <ui/render/render.h>
#include <features/system/performance/performance.h>
#include <core/scanner/rescan.h>

#include <core/protection/protection.h>

#include <features/combat/aimbot/aimbot.h>
#include <features/combat/trigger/trigger.h>
#include <features/system/config/config.h>
#include <features/system/settings/settings.h>
#include <features/system/notifications/notifications.h>

#include <core/utility/creator/creator.h>
#include <core/protection/stealth/stealth.h>
#include <core/system/eject.h>
#include <core/utility/nvstream/nvstream.h>
#include "build_stamp.h"
#include <features/mesh/assetmesh.h>
#include <features/mesh/avatarmesh.h>
#include <ui/menu/settings/functions.h>

volatile const char* kBuildStampUse = kBuildStamp;
#include <ui/loader/loader.h>
#include <features/system/site/site.h>

namespace {
	bool should_render_ui() {
		if (var->gui.menu_opened || loader::open) {
			return true;
		}
		HWND hwnd = GetForegroundWindow();
		HWND roblox_window = game::get_roblox_window();
		HWND overlay_window = (render && render->detail) ? render->detail->window : nullptr;

		if (roblox_window != nullptr && IsWindow(roblox_window)) {
			return (hwnd == roblox_window || hwnd == overlay_window);
		}
		return true;
	}

void setup_console() {
	// Injected DLL: never attach a console to the host process. AllocConsole
	// spawns conhost and grafts a console onto the target, which both
	// interferes with the host and leaves a visible trace. Only create one when
	// the user explicitly turns console hiding off (debugging), and never if
	// the host already owns a console.
	if (settings::stealth::hide_console || GetConsoleWindow() != nullptr)
		return;

	AllocConsole();
	FILE* pCout;
	FILE* pCin;
	FILE* pCerr;
	freopen_s(&pCout, "CONOUT$", "w", stdout);
	freopen_s(&pCin, "CONIN$", "r", stdin);
	freopen_s(&pCerr, "CONOUT$", "w", stderr);

	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut != INVALID_HANDLE_VALUE) {
		DWORD dwMode = 0;
		GetConsoleMode(hOut, &dwMode);
		dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode(hOut, dwMode);
	}

	HWND console_window = GetConsoleWindow();
	if (console_window) {
		ShowWindow(console_window, SW_SHOW);
	}
}

static void ResetGlobals()
{
	game::datamodel = { 0 };
	game::visualengine = { 0 };
}

static void EnableAntiCapture()
{
	HWND overlay_window = (render && render->detail) ? render->detail->window : nullptr;
	if (overlay_window && IsWindow(overlay_window))
	{
		SetWindowDisplayAffinity(overlay_window, settings::streamproof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
	}
}



static void WaitForRoblox()
{
	if (memory->is_attached() && memory->is_alive())
		return;

	if (memory->is_attached())
		memory->detach();

	Cheat::Console::Log(Cheat::Console::Color::Yellow, "waiting for roblox");
	while (!memory->attach_to_process(BINARY_NAME))
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

static void OnRobloxAttached(bool reattached)
{
	memory->find_module_address(BINARY_NAME);
	Cheat::Console::Clear();
	if (reattached)
		Cheat::Console::DumpLastCrash();

	// In-process Raycast resolver disabled for stability - use external tools/raycast_auto_updater.py --patch
	// To re-enable, ensure all dereferences are guarded with IsBadReadPtr/VirtualQuery checks
	(void)0;
}

// simple append-only boot log - lives in core/diagnostics/bootlog.h now

LONG WINAPI ApplicationCrashHandler(EXCEPTION_POINTERS* exception_info)
{
	// last-ditch diagnostics: append to the boot log so a silent Discord
	// exit can be traced back to the faulting stage/address
	if (exception_info && exception_info->ExceptionRecord)
	{
		bootlog("SEH UNHANDLED: code=0x%08X addr=0x%p",
			exception_info->ExceptionRecord->ExceptionCode,
			exception_info->ExceptionRecord->ExceptionAddress);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}
}

namespace {
	// plain C trampoline so SEH can wrap the C++ task without C2712
	// (an AV in a feature thread would otherwise fail-fast and kill Discord silently)
	static thread_local void* t_seh_task = nullptr;
	static thread_local const char* t_seh_task_name = "unknown";

	static void run_task_seh()
	{
		__try
		{
			(*static_cast<std::function<void()>*>(t_seh_task))();
		}
		__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
			GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
			? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
		{
			bootlog("FATAL: thread '%s' crashed (SEH 0x%08X) - host kept alive", t_seh_task_name, GetExceptionCode());
		}
	}

	template <typename Fn>
	void guarded_thread(const char* name, Fn&& fn)
	{
		auto task = std::make_shared<std::function<void()>>([name, fn = std::forward<Fn>(fn)]() {
			sys_eject::register_current_thread();
			if (settings::stealth::name_threads)
				SetThreadDescription(GetCurrentThread(), L"Windows Audio Session");
			try { fn(); }
			catch (const std::exception& e) { bootlog("thread '%s' died: %s", name, e.what()); }
			catch (...) { bootlog("thread '%s' died: unknown exception", name); }
		});

		std::thread([name, task]() {
			t_seh_task = task.get();
			t_seh_task_name = name;
			bootlog("thread '%s' started", name);
			run_task_seh();
			bootlog("thread '%s' finished", name);
		}).detach();
	}
}

// DLL entry point - the whole init + render loop runs on its own thread so
// DllMain returns immediately (no loader lock).
// SEH wrapper: an access violation on this thread would otherwise take
// Discord down silently - catch it, log the stage, keep the host alive.
static void cheat_main();
static LONG WINAPI cheat_crash_filter(EXCEPTION_POINTERS* ei)
{
	if (ei && ei->ExceptionRecord)
	{
		// name the module that contains the crashing address
		char module_info[520] = "unknown";
		HMODULE crash_mod = nullptr;
		if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(ei->ExceptionRecord->ExceptionAddress), &crash_mod) && crash_mod)
		{
			char path[MAX_PATH] = {};
			if (GetModuleFileNameA(crash_mod, path, MAX_PATH))
			{
				const uintptr_t base = reinterpret_cast<uintptr_t>(crash_mod);
				const uintptr_t off = reinterpret_cast<uintptr_t>(ei->ExceptionRecord->ExceptionAddress) - base;
				snprintf(module_info, sizeof(module_info), "%s + 0x%p", path, (void*)off);
			}
		}
		bootlog("FATAL: crash code=0x%08X addr=0x%p in [%s]",
			ei->ExceptionRecord->ExceptionCode,
			ei->ExceptionRecord->ExceptionAddress, module_info);

		if (ei->ExceptionRecord->ExceptionCode == 0xC0000005 && ei->ExceptionRecord->NumberParameters >= 2)
		{
			bootlog("FATAL: access violation %s address 0x%p",
				ei->ExceptionRecord->ExceptionInformation[0] == 0 ? "READING" :
				ei->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITING" : "EXECUTING",
				(void*)ei->ExceptionRecord->ExceptionInformation[1]);
		}

		// (no minidump: %TEMP%\ivory_crash.dmp is a forensic artifact - the
		// scanner flags unsigned crashed executables via %LOCALAPPDATA%
		// CrashDumps + EVTX 1000 correlation. A crash dump containing the
		// cheat's full memory would be a YARA jackpot. The stack walk below
		// goes to the in-memory bootlog only.)

		// stack walk with symbols - names our exact function
		if (SymInitialize(GetCurrentProcess(), nullptr, TRUE))
		{
			CONTEXT ctx = *ei->ContextRecord;
			STACKFRAME64 frame{};
			frame.AddrPC.Offset = ctx.Rip;
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = ctx.Rbp;
			frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Offset = ctx.Rsp;
			frame.AddrStack.Mode = AddrModeFlat;
			for (int i = 0; i < 14; i++)
			{
				if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(),
					&frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
					break;
				if (frame.AddrPC.Offset == 0)
					break;
				const DWORD64 addr = frame.AddrPC.Offset;
				char where[420] = "?";
				HMODULE fm = nullptr;
				if (GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(addr), &fm) && fm)
				{
					char fpath[MAX_PATH] = {};
					if (GetModuleFileNameA(fm, fpath, MAX_PATH))
					{
						const char* fn = strrchr(fpath, '\\');
						fn = fn ? fn + 1 : fpath;
						const uintptr_t base = reinterpret_cast<uintptr_t>(fm);
						snprintf(where, sizeof(where), "%s+0x%llx", fn,
							(unsigned long long)(addr - base));
					}
				}
				SYMBOL_INFO* sym = (SYMBOL_INFO*)calloc(1, sizeof(SYMBOL_INFO) + 160);
				if (sym)
				{
					sym->SizeOfStruct = sizeof(SYMBOL_INFO);
					sym->MaxNameLen = 159;
					DWORD64 disp = 0;
					if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym))
						bootlog("  #%d %s  [%s]", i, where, sym->Name);
					else
						bootlog("  #%d %s", i, where);
					free(sym);
				}
			}
			SymCleanup(GetCurrentProcess());
		}
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD WINAPI cheat_main_seh(LPVOID)
{
	__try
	{
		cheat_main();
	}
	__except (cheat_crash_filter(GetExceptionInformation()))
	{
		bootlog("FATAL: cheat_main crashed - host kept alive");
	}
	return 0;
}

static void cheat_main()
{
	using namespace std::chrono_literals;

	bootlog("boot: thread started");
	sys_eject::register_current_thread();

	bootlog("boot: paw_guard (anti-debug patches)");
	paw_guard::init();

	// NVIDIA streamproof: neutralise ShadowPlay's protected-content detector
	std::thread([]() {
		sys_eject::register_current_thread(true);
		nvstream::run();
	}).detach();

	SetProcessDPIAware();
	setup_console();
	SetUnhandledExceptionFilter(ApplicationCrashHandler);
	timeBeginPeriod(1);

	SetConsoleCtrlHandler([](DWORD dwCtrlType) -> BOOL {
		return FALSE;
	}, TRUE);

	std::string console_title = "Ivory";
	SetConsoleTitleA(console_title.c_str());

	// Scan / manual-check resistance: hide our DLL from module enumeration and
	// announce innocuous thread names. Applied before any feature threads start.
	{
	switch (settings::stealth::module_mode)
	{
	case 1:
	case 2:
		// module renaming / unlinking disabled: renaming to "dxgi.dll"
		// collided with the real dxgi.dll's own module tracking and crashed
		// a dxgi worker thread (strlen on a null record). The loader-list
		// tricks are not worth the instability in the standalone build.
		break;
	}
	}

	Offsets::update();
	bootlog("boot: offsets updated");

	Cheat::Console::Log(Cheat::Console::Color::Gray, "configs");
	orok_config.SetupValues();
	config::ensure_config_directory();
	settings::misc::hide_console = settings::stealth::hide_console;
	if (settings::stealth::hide_console)
	{
		// no console window for a manual checker to read
		SetConsoleTitleA("Windows Command Processor");
		if (HWND console_window = GetConsoleWindow())
			ShowWindow(console_window, SW_HIDE);
	}
	assetmesh::initialize();
	avatarmesh::initialize();

	bootlog("boot: creating overlay window");
	{ 
		if (!render->create_window())
		{
			bootlog("boot: FAILED create_window");
			Cheat::Console::Log(Cheat::Console::Color::Red, "failed to window handle");
			std::this_thread::sleep_for(10s);
			std::exit(0);
		}

		bootlog("boot: creating D3D11 device");
		if (!render->create_device())
		{
			bootlog("boot: FAILED create_device");
			std::this_thread::sleep_for(10s);
			std::exit(0);
		}

		bootlog("boot: creating imgui context");
		if (!render->create_imgui())
		{
			bootlog("boot: FAILED create_imgui");
			std::this_thread::sleep_for(10s);
			std::exit(0);
		}
		bootlog("boot: imgui ready");

		config::auto_load_theme();

		if (render && render->detail && render->detail->device && render->detail->device_context)
		{
			(void)0;
		}
	}

	creator::install_console_handler();
	auto start_cheat = []() {
		loader::stage = 1;
		loader::progress = 5.0f;
		loader::status_text = "Waiting for Roblox...";
		bootlog("start_cheat: waiting for roblox");
		WaitForRoblox();

		loader::stage = 2;
		loader::progress = 45.0f;
		loader::status_text = "Roblox attached - reading game state";
		bootlog("start_cheat: roblox attached");
		OnRobloxAttached(false);

		loader::stage = 3;
		loader::progress = 70.0f;
		loader::status_text = "Starting features";

		game::datamodel = { rbx::c_datamodel::get() };
		game::visualengine = { rbx::c_visualengine::get() };

		Cheat::Console::DumpWorld();

		guarded_thread("task-01", rescan::rescan_game);
		guarded_thread("task-02", rescan::rescan_process);

		guarded_thread("task-03", cache::run);
		guarded_thread("task-04", wallcheck::run);
		guarded_thread("task-vc", visualcheck::run);

		guarded_thread("task-05", []()
		{
			guarded_thread("task-06", aimbot::run);
			guarded_thread("task-07", triggerbot::run);
		});

		// control panel: applies this account's web settings to the live cheat
		guarded_thread("task-site", site::run);

		config::auto_load_theme();

		loader::is_loaded = true;
		loader::stage = 4;
		loader::progress = 100.0f;
		loader::status_text = "Ready";
		bootlog("start_cheat: all feature threads running");

		// ready - the web panel is the only UI: the in-game menu never opens
		notifications::add("loaded - roblox attached");
	};

	// no auto-start: the loader's Load button calls start_cheat, which lets
	// the progress screen show the real attach stages
	loader::on_load_callback = start_cheat;

	render->running = true;
	// the external host (Ivory.exe / IvoryLoader) already asked for the load -
	// the in-DLL loader screen is skipped entirely: start_cheat runs right
	// away and the menu opens itself once Roblox is attached
	loader::open = false;
	notifications::add("waiting for roblox...");
	std::thread(start_cheat).detach();
	var->gui.menu_opened = false;
	var->gui.menu_alpha = 0.0f;

	bootlog("boot: render loop starting");

	static bool s_first_frame_logged = false;

	render->menu_open = false;
	render->main_window_open = false;

	// loader boot overrides: plain REG_DWORD values under the shared key.
	// memory configs use hex-only value names, so these can never collide.
	// applied here (first, before any config load/render) so they win over
	// compiled defaults and saved setups.
	{
		HKEY hkey = nullptr;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\RiotClientCrashHandler", 0, KEY_READ, &hkey) == ERROR_SUCCESS)
		{
			DWORD v = 0, n = sizeof(v), t = 0;
			if (RegQueryValueExA(hkey, "loader_autostreamproof", nullptr, &t, (BYTE*)&v, &n) == ERROR_SUCCESS && t == REG_DWORD && v)
				settings::streamproof = true;
			v = 0; n = sizeof(v); t = 0;
			if (RegQueryValueExA(hkey, "loader_irl", nullptr, &t, (BYTE*)&v, &n) == ERROR_SUCCESS && t == REG_DWORD && v)
				settings::general::irl_mode = true;
			RegCloseKey(hkey);
		}
	}

	while (!sys_eject::requested)
	{
		// global hotkeys: eject only. Loading is handled by the host -
		// auto-load methods and usage notes live in Build\readme.txt
		{
			static bool s_f3_prev = false;
			const bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
			if (f3 && !s_f3_prev && loader::is_loaded)
			{
				bootlog("hotkey: F3 - eject");
				sys_eject::soft_unload();
				cursor_capture::restore();
				loader::is_loaded = false;
				loader::is_loading = false;
				loader::stage = 0;
				loader::progress = 0.0f;
				loader::status_text = "Unloaded";
				bootlog("hotkey: F3 - ejected");
			}
			s_f3_prev = f3;
		}

		if (!s_first_frame_logged)
		{
			s_first_frame_logged = true;
			bootlog("render: first frame");
		}
		auto frame_start = std::chrono::high_resolution_clock::now();

		// irl hot-reload: the loader owns this flag (the menu has no irl
		// control by design) - re-read periodically so toggling it in the
		// loader settings takes effect live without reloading
		{
			static int s_irl_tick = 0;
			if (++s_irl_tick >= 60)
			{
				s_irl_tick = 0;
				HKEY hkey = nullptr;
				if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\RiotClientCrashHandler", 0, KEY_READ, &hkey) == ERROR_SUCCESS)
				{
					DWORD v = 0, n = sizeof(v), t = 0;
					if (RegQueryValueExA(hkey, "loader_irl", nullptr, &t, (BYTE*)&v, &n) == ERROR_SUCCESS && t == REG_DWORD)
						settings::general::irl_mode = (v != 0);
					RegCloseKey(hkey);
				}
			}
		}

		if (loader::is_loaded && !memory->is_alive())
		{
			DWORD code = 0;
			if (!memory->get_exit_code(&code))
				code = 0xFFFFFFFF;

			ResetGlobals();
			creator::notify_datamodel_lost();
			Cheat::Console::Clear();
			Cheat::Console::DumpCrash(code);

			memory->detach();
			WaitForRoblox();
			OnRobloxAttached(true);
			game::datamodel = { rbx::c_datamodel::get() };
			game::visualengine = { rbx::c_visualengine::get() };
		}

		try
		{
		render->start_render();

		HWND foreground_window = GetForegroundWindow();
		HWND roblox_window = game::get_roblox_window();
		HWND overlay_window = (render && render->detail) ? render->detail->window : nullptr;
		sys_eject::overlay_window = overlay_window;
		static HWND s_last_foreground = nullptr;

		static LONG s_current_exstyle = 0;
		static bool s_last_interactive = false;

		if (foreground_window != s_last_foreground)
		{
			s_last_foreground = foreground_window;
		}

		if (!should_render_ui())
		{
			if (overlay_window && IsWindow(overlay_window)) {
				// DComp path keeps WS_EX_NOREDIRECTIONBITMAP; legacy path needs WS_EX_LAYERED
				const bool dcomp_path = render && render->detail && render->detail->use_dcomp;
				LONG hide_style = WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST | (dcomp_path ? WS_EX_NOREDIRECTIONBITMAP : WS_EX_LAYERED);
				SetWindowLong(overlay_window, GWL_EXSTYLE, hide_style);
				s_current_exstyle = hide_style;
			// streamproof content hiding - WDA is the core mechanism, never drop it
			SetWindowDisplayAffinity(overlay_window, settings::streamproof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
		}
		render->end_render();
			std::this_thread::sleep_for(20ms);
			continue;
		}

		// Panic: overlay is hidden - skip all ESP/menu drawing entirely
		if (settings::stealth::panic_active)
		{
			render->end_render();
			std::this_thread::sleep_for(20ms);
			continue;
		}

		render->render_visuals();


		static auto last_frame_time = std::chrono::high_resolution_clock::now();
		auto cur_frame_time = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration<float>(cur_frame_time - last_frame_time).count();
		last_frame_time = cur_frame_time;

		// Panic key: instant self-destruct. The overlay, payload and this host
		// process all die immediately - nothing stays running to find. The
		// exe itself is untouched on disk, so simply running it again
		// restores everything.
		if (settings::stealth::panic_key != 0)
		{
			static bool s_panic_key_was_down = false;
			bool panic_key_down = (GetAsyncKeyState(settings::stealth::panic_key) & 0x8000) != 0;
			if (panic_key_down && !s_panic_key_was_down)
			{
				bootlog("panic: self-destruct");
				cursor_capture::restore();
				config::wipe_traces();
				std::exit(0);
			}
			s_panic_key_was_down = panic_key_down;
		}

		bool cursor_over_loader = false;
		if (loader::open && !var->gui.menu_opened)
		{
			POINT cp;
			GetCursorPos(&cp);
			ImVec2 lpos = loader::pos;
			ImVec2 lsz = loader::get_current_window_size();
			if (cp.x >= (lpos.x - 5.f) && cp.x <= (lpos.x + lsz.x + 5.f) &&
				cp.y >= (lpos.y - 5.f) && cp.y <= (lpos.y + lsz.y + 5.f))
			{
				cursor_over_loader = true;
			}
		}

		const bool interactive_ui = var->gui.menu_opened || cursor_over_loader || loader::is_dragging;

		static bool s_last_menu_state = false;
		if (interactive_ui != s_last_menu_state)
		{
			s_last_menu_state = interactive_ui;
			if (var->gui.menu_opened && overlay_window)
			{
				SetForegroundWindow(overlay_window);
			}
			else if (!interactive_ui && roblox_window && IsWindow(roblox_window))
			{
				SetForegroundWindow(roblox_window);
			}
		}

		if (overlay_window && IsWindow(overlay_window))
		{
			// DComp path keeps WS_EX_NOREDIRECTIONBITMAP; legacy path needs WS_EX_LAYERED
			const bool dcomp_path = render && render->detail && render->detail->use_dcomp;
			LONG base_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | (dcomp_path ? WS_EX_NOREDIRECTIONBITMAP : WS_EX_LAYERED);
			LONG target_style = interactive_ui ? base_style : (base_style | WS_EX_TRANSPARENT);
			if (s_current_exstyle != target_style)
			{
				SetWindowLong(overlay_window, GWL_EXSTYLE, target_style);
				SetWindowPos(overlay_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
				s_current_exstyle = target_style;
			}

			// Keep streamproof active — re-assert after any style change
			SetWindowDisplayAffinity(overlay_window, settings::streamproof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);

			render->render_menu();

			static auto s_last_hb = std::chrono::steady_clock::now();
			if (std::chrono::steady_clock::now() - s_last_hb > std::chrono::seconds(5))
			{
				s_last_hb = std::chrono::steady_clock::now();
				bootlog("render: heartbeat (frame loop alive, roblox=%s, menu=%d)",
					roblox_window ? "yes" : "no", (int)var->gui.menu_opened);
			}

			if (settings::misc::performance_window)
			{
				performance::render_window(&settings::misc::performance_window);
			}

		}

		render->end_render();

		if (settings::performance::main_loop_delay > 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(settings::performance::main_loop_delay));
		}
		else if (!settings::vsync && settings::misc::unlock_fps && settings::misc::fps_cap > 0)
		{
			auto target_frame_duration = std::chrono::microseconds(1000000 / settings::misc::fps_cap);
			while (std::chrono::high_resolution_clock::now() - frame_start < target_frame_duration)
			{
				std::this_thread::yield();
			}
		}
		}
		catch (...)
		{
			bootlog("render: exception in frame loop");
			logger->log<ERR>("render error ");
			std::this_thread::sleep_for(50ms);
		}
	}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(instance);
		bootlog("dll: attached to host process");
		std::thread(cheat_main_seh, nullptr).detach();
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		if (render)
			render->running = false;
	}
	return TRUE;
}

// Standalone build: the cheat runs as its own process - no injection, no
// host. It creates its own overlay window and reads Roblox externally.
int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	bootlog("exe: started (standalone - no injection needed)");
	cheat_main_seh(nullptr);
	cursor_capture::restore();  // never leave the user's mouse setting modified
	return 0;
}

// Diagnostic export: the loader polls this and relays the boot log to the
// control panel. Init faults inside cheat_main are caught by its SEH filter
// ("host kept alive"), so without this the failure is invisible.
extern "C" __declspec(dllexport) const char* ivory_bootlog()
{
	static std::string s;
	s = bootlog_dump();
	return s.c_str();
}
