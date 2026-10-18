#pragma once
// Lightweight diagnostic log - MEMORY ONLY.
//
// The previous version appended to %TEMP%\ivory_boot.txt, which is exactly
// the artifact class the forensic scanner catches: a cheat-named file on an
// NTFS volume, visible to USN name rules and YARA content scans for the
// rest of the journal's life (and deleting it after the fact is flagged
// too - a delete record is permanent). So: RAM ring buffer, nothing on
// disk. Survives a hidden console; safe to call from any thread.
//
// bootlog_dump() exposes the ring so the loader can relay it to the control
// panel - that is the only way to see where init stopped, since an unhandled
// fault inside cheat_main is swallowed by its SEH filter.

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>

namespace bootlog_detail
{
	inline constexpr int kLines = 256;
	inline constexpr int kWidth = 384;

	struct Ring
	{
		std::mutex mtx;
		char lines[kLines][kWidth]{};
		size_t head = 0;
		size_t count = 0;
	};

	// one instance per process (inline function -> merged externally)
	inline Ring& ring() { static Ring r; return r; }
}

inline void bootlog(const char* fmt, ...)
{
	char msg[bootlog_detail::kWidth];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	SYSTEMTIME st;
	GetLocalTime(&st);

	auto& r = bootlog_detail::ring();
	std::lock_guard<std::mutex> lock(r.mtx);
	snprintf(r.lines[r.head], bootlog_detail::kWidth,
		"[%02d:%02d:%02d.%03d] %s", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
	r.lines[r.head][bootlog_detail::kWidth - 1] = 0;
	r.head = (r.head + 1) % bootlog_detail::kLines;
	if (r.count < bootlog_detail::kLines)
		++r.count;
}

// chronological dump of the ring (oldest first)
inline std::string bootlog_dump()
{
	auto& r = bootlog_detail::ring();
	std::lock_guard<std::mutex> lock(r.mtx);
	std::string out;
	out.reserve(r.count * 64);
	const size_t start = (r.count < bootlog_detail::kLines) ? 0 : r.head;
	for (size_t i = 0; i < r.count; ++i)
	{
		const char* line = r.lines[(start + i) % bootlog_detail::kLines];
		if (line[0]) { out += line; out += '\n'; }
	}
	return out;
}
