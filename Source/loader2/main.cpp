/* Resident loader.
 *
 * Talks to the control panel over a WebSocket. On "load" it decrypts the
 * embedded cheat payload, stages it to a random-named file in %TEMP% for a few
 * milliseconds, LoadLibrary's it into THIS process, then unlinks the file. The
 * image stays mapped after the name is gone.
 *
 * The cheat is not injected into Roblox: it runs here and attaches to Roblox
 * externally, which needs elevation - see loader.manifest (requireAdministrator).
 *
 * Connection model (two threads):
 *   - a receiver thread blocks in WinHttpWebSocketReceive forever (the receive
 *     timeout is NOT honored for WebSocket frames - verified against the live
 *     server - so any single-threaded poll design deadlocks) and handles
 *     commands;
 *   - the main thread sleeps in a 1s loop and relays the payload's exported
 *     boot log (ivory_bootlog) to the panel whenever it changes. Init faults
 *     inside the payload are swallowed by its own SEH filter, so that relay is
 *     the only way to see where it stopped.
 *
 * HWID: the loader binds the account to this machine on first connect
 * (SHA-256 of HKLM MachineGuid). A mismatched device gets {"type":"blocked"}
 * and the loader exits after telling the user. The owner can clear the binding
 * from the panel's Users tab.
 *
 * Registry (HKCU\Software\RiotClientCrashHandler):
 *     Token   REG_SZ   grim-xxxx-xxxx   (required)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstring>

#include "inject.h"
#include "payload_blob_key.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")

static const wchar_t* kKeyPath  = L"Software\\RiotClientCrashHandler";
static const wchar_t* kEndpoint = L"cdn.edge-config.workers.dev";
static const char*    kVer      = "3";

/* ---------------------------------------------------------------- registry */

static std::wstring RegStr(const wchar_t* name, const wchar_t* fallback) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, KEY_READ, &k) != ERROR_SUCCESS)
    return fallback;
  wchar_t buf[1024] = {0};
  DWORD sz = sizeof(buf) - sizeof(wchar_t);
  DWORD type = 0;
  LSTATUS s = RegQueryValueExW(k, name, NULL, &type, (LPBYTE)buf, &sz);
  RegCloseKey(k);
  if (s != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return fallback;
  return buf;
}

static void RegSetStr(const wchar_t* name, const std::wstring& v) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
  }
}

/* ------------------------------------------------------------- disguise */

/* The exe runs under the name of removable Windows bloat (Phone Link). The
 * real app is NOT terminated on purpose: an unsigned binary killing a
 * Microsoft-signed process is a strong behavioral tell for AV heuristics,
 * and a second process with the same name is invisible to anything casual.
 * (The runtime self-signing that used to live here was removed for the same
 * reason - creating a code-signing cert, installing it into Trusted Root
 * and re-signing yourself is a textbook dropper pattern that Defender's
 * behavioral ML flags on the spot. It cost us one exe.) */

/* "grim-xxxx-xxxx" passed as a command-line argument: first run on a new
 * machine. It is saved to the registry automatically, so afterwards the exe
 * can just be double-clicked bare. */
static std::wstring ParseTokenArg(const wchar_t* cmd) {
  if (!cmd) return {};
  const std::wstring s(cmd);
  const size_t p = s.find(L"grim-");
  if (p == std::wstring::npos || s.size() < p + 14) return {};
  const wchar_t* t = s.c_str() + p;
  for (size_t i = 0; i < 14; ++i) {
    const wchar_t c = t[i];
    const bool ok = (i == 4 || i == 9) ? (c == L'-') : (iswalnum(c) != 0);
    if (!ok) return {};
  }
  return std::wstring(t, 14);
}

/* first run with no token anywhere: ask once. A small one-off prompt -
 * paste the token, press ok, it is saved and this never appears again. */
static std::wstring g_token_result;
static HWND g_token_edit = nullptr;

static LRESULT CALLBACK TokenWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_COMMAND:
    if (LOWORD(wp) == 1) {
      wchar_t buf[128] = {0};
      GetWindowTextW(g_token_edit, buf, 128);
      g_token_result = buf;
      DestroyWindow(h);
      return 0;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(h);
    return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

static std::wstring PromptForToken() {
  const wchar_t* cls = L"RiotClientSetup";
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = TokenWndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = cls;
  RegisterClassW(&wc);

  HWND win = CreateWindowExW(WS_EX_TOPMOST, cls, L"setup",
    WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 384, 150,
    nullptr, nullptr, inst, nullptr);

  CreateWindowExW(0, L"STATIC", L"paste your access token and press ok",
    WS_CHILD | WS_VISIBLE, 14, 14, 340, 20, win, nullptr, inst, nullptr);
  g_token_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
    14, 40, 340, 26, win, (HMENU)2, inst, nullptr);
  CreateWindowExW(0, L"BUTTON", L"ok",
    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
    280, 80, 74, 28, win, (HMENU)1, inst, nullptr);

  ShowWindow(win, SW_SHOW);
  SetFocus(g_token_edit);

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(win, &m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  return g_token_result;
}

/* -------------------------------------------------------------------- hwid */

static std::string ComputeHwid() {
  char guid[256] = {0};
  DWORD sz = sizeof(guid);
  if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography",
                   "MachineGuid", RRF_RT_REG_SZ, nullptr, guid, &sz) != ERROR_SUCCESS)
    return {};

  BYTE digest[32];
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string out;

  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
      BCryptHashData(hash, (PUCHAR)guid, (ULONG)strlen(guid), 0) == 0 &&
      BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) { out += hex[digest[i] >> 4]; out += hex[digest[i] & 15]; }
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}

/* ----------------------------------------------------------------- payload */

static const BYTE kPayloadKey[32] = {
  0x9E, 0x21, 0x4C, 0x77, 0x0B, 0x63, 0xA5, 0x3D, 0xF1, 0x58, 0x8C, 0x12, 0xD4, 0x6E, 0x27, 0xBA,
  0x05, 0x94, 0xE8, 0x33, 0x71, 0xCF, 0x46, 0x19, 0xAD, 0x82, 0x5B, 0xE0, 0x2C, 0x69, 0xF7, 0x3B,
};

/* RCDATA resource 101 = the payload, masked with key[i%32] ^ (i*31+7). */
static std::vector<BYTE> LoadPayload() {
  HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(101), RT_RCDATA);
  if (!r) return {};
  HGLOBAL g = LoadResource(NULL, r);
  if (!g) return {};
  const DWORD n = SizeofResource(NULL, r);
  const BYTE* p = (const BYTE*)LockResource(g);
  if (!p || !n) return {};
  std::vector<BYTE> out(p, p + n);
  for (size_t i = 0; i < out.size(); ++i)
    out[i] ^= (BYTE)(kPayloadKey[i % 32] ^ ((i * 31 + 7) & 0xFF));
  /* payload_src.dll is XOR-encrypted in place by tools\harden_payload.ps1 in
   * the MSBuild post-build, using its own per-build key. Undo that layer too,
   * otherwise what we stage is still ciphertext. */
  for (size_t i = 0; i < out.size(); ++i)
    out[i] ^= (BYTE)(kBlobKey[i % 32] ^ ((i * 31 + 7) & 0xFF));
  return out;
}

/* ------------------------------------------------------------------ socket */

static std::string JsonStr(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 16);
  o += '"';
  for (unsigned char c : s) {
    switch (c) {
    case '"':  o += "\\\""; break;
    case '\\': o += "\\\\"; break;
    case '\n': o += "\\n";  break;
    case '\r': o += "\\r";  break;
    case '\t': o += "\\t";  break;
    default:
      if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
      else o += (char)c;
    }
  }
  o += '"';
  return o;
}

struct Site {
  HINTERNET session = NULL, connect = NULL, ws = NULL;
  std::mutex sendMtx;

  bool Open(const std::wstring& token, const std::string& hwid) {
    session = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    connect = WinHttpConnect(session, kEndpoint, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) return false;

    std::wstring path = L"/api/ws?token=" + token;
    if (!hwid.empty()) path += L"&hwid=" + std::wstring(hwid.begin(), hwid.end());

    HINTERNET req = WinHttpOpenRequest(connect, L"GET", path.c_str(), NULL,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) return false;
    int timeout = 10000;
    WinHttpSetOption(req, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    bool ok = WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
              WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
              WinHttpReceiveResponse(req, NULL);
    if (ok) ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    WinHttpCloseHandle(req);
    return ws != NULL;
  }

  bool Send(const std::string& m) {
    if (!ws) return false;
    std::lock_guard<std::mutex> lock(sendMtx);
    return WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                (PVOID)m.data(), (DWORD)m.size()) == ERROR_SUCCESS;
  }

  /* 1 = message, 0 = closed, -1 = error. Blocks until data arrives. */
  int Recv(std::string& out) {
    out.clear();
    if (!ws) return -1;
    BYTE buf[8192];
    for (;;) {
      DWORD read = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
      const DWORD rc = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &read, &type);
      if (rc != ERROR_SUCCESS) return -1;
      if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return 0;
      out.append((const char*)buf, read);
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
          type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) return 1;
    }
  }

  void Close() {
    if (ws) { WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0); WinHttpCloseHandle(ws); ws = NULL; }
    if (connect) { WinHttpCloseHandle(connect); connect = NULL; }
    if (session) { WinHttpCloseHandle(session); session = NULL; }
  }
};

/* ------------------------------------------------------------------- state */

typedef const char* (*BootlogFn)();

// self-destruct: zero + delete every value under the cheat key, then the key
// itself. Values are overwritten with zeros first because a deleted registry
// cell stays recoverable from hive slack by forensic tools. After this the
// exe needs its token passed on the command line once more to come back.
static void WipeRegistry() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, KEY_READ | KEY_WRITE, &k) != ERROR_SUCCESS)
    return;
  for (DWORD i = 0;;) {
    wchar_t name[256];
    DWORD name_len = 256;
    const LSTATUS s = RegEnumValueW(k, i, name, &name_len, nullptr, nullptr, nullptr, nullptr);
    if (s == ERROR_NO_MORE_ITEMS) break;
    if (s != ERROR_SUCCESS) { ++i; continue; }
    DWORD size = 0, type = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0 && size < 4096) {
      std::vector<BYTE> zeros(size, 0);
      RegSetValueExW(k, name, 0, type, zeros.data(), size);
    }
    RegDeleteValueW(k, name);
    // the next value shifted into this index - don't advance i
  }
  RegCloseKey(k);
  RegDeleteKeyW(HKEY_CURRENT_USER, kKeyPath);
}

struct Loaded {
  HMODULE mod = NULL;
  std::atomic<BootlogFn> bootlog{nullptr};
  bool attached() const { return mod != NULL; }
};

static Loaded g_st;
static Site* g_site = nullptr;
static std::atomic<bool> g_up{false};
static std::atomic<bool> g_blocked{false};
static std::atomic<bool> g_selfdestruct{false};
static std::mutex g_loadMtx;

static void Report(const char* state) {
  if (!g_site) return;
  g_site->Send(std::string("{\"type\":\"set\",\"params\":{\"client.state\":\"") + state +
               "\",\"client.ver\":\"" + kVer + "\"}}");
}

/* called only from the main thread */
static void RelayLog(bool force) {
  if (!g_site) return;
  const BootlogFn fn = g_st.bootlog.load();
  if (!fn) return;
  std::lock_guard<std::mutex> lock(g_loadMtx);   /* don't call into a DLL being unloaded */
  const char* text = fn();
  static std::string last;
  if (!text || (!force && last == text)) return;
  last = text;
  g_site->Send(std::string("{\"type\":\"logs\",\"text\":") + JsonStr(text) + "}");
}

static void RandomName(wchar_t* buf, size_t n) {
  static unsigned counter = 0;
  unsigned long long x = GetTickCount64() ^ ((unsigned long long)GetCurrentProcessId() << 32);
  x ^= (unsigned long long)(++counter) * 0x9E3779B97F4A7C15ull;
  _snwprintf_s(buf, n, _TRUNCATE, L"%08x%08x", (unsigned)(x >> 32), (unsigned)x);
}

static bool DoLoad() {
  if (g_st.attached()) return true;

  std::vector<BYTE> payload = LoadPayload();
  if (payload.empty() || !ImageIsDll(payload.data(), payload.size())) return false;

  wchar_t dir[MAX_PATH];
  if (!GetTempPathW(MAX_PATH, dir)) return false;

  wchar_t name[64];
  RandomName(name, sizeof(name) / sizeof(name[0]));
  std::wstring path = std::wstring(dir) + name + L".dll";

  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return false;

  DWORD written = 0;
  BOOL ok = WriteFile(f, payload.data(), (DWORD)payload.size(), &written, NULL);
  CloseHandle(f);
  if (!ok || written != (DWORD)payload.size()) { DeleteFileW(path.c_str()); return false; }

  /* Load, then unlink: an image mapping keeps the file content alive after the
   * directory entry is gone, so the payload never persists on disk. */
  HMODULE m = LoadLibraryW(path.c_str());
  DeleteFileW(path.c_str());
  if (!m) return false;

  std::lock_guard<std::mutex> lock(g_loadMtx);
  g_st.mod = m;
  g_st.bootlog = (BootlogFn)GetProcAddress(m, "ivory_bootlog");
  return true;
}

static void DoUnload() {
  std::lock_guard<std::mutex> lock(g_loadMtx);
  if (g_st.mod) { FreeLibrary(g_st.mod); g_st.mod = NULL; g_st.bootlog = nullptr; }
}

/* -------------------------------------------------------------- rx thread */

static void RxLoop() {
  std::string msg;
  for (;;) {
    const int r = g_site->Recv(msg);
    if (r != 1) break;

    if (msg.find("\"type\":\"blocked\"") != std::string::npos) { g_blocked = true; break; }
    if (msg.find("\"type\":\"command\"") == std::string::npos) continue;

    if (msg.find("\"cmd\":\"load\"") != std::string::npos) {
      Report(DoLoad() ? "injected" : "load_failed");
    } else if (msg.find("\"cmd\":\"unload\"") != std::string::npos) {
      DoUnload();
      Report("unloaded");
    } else if (msg.find("\"cmd\":\"reinject\"") != std::string::npos) {
      DoUnload();
      Report(DoLoad() ? "injected" : "load_failed");
    } else if (msg.find("\"cmd\":\"close\"") != std::string::npos) {
      Report("gone");
      g_selfdestruct = true;
      break;
    } else if (msg.find("\"cmd\":\"selfdestruct\"") != std::string::npos) {
      DoUnload();
      g_site->Send("{\"type\":\"set\",\"params\":{\"client.state\":\"gone\",\"client.ver\":\"" + std::string(kVer) + "\"}}");
      WipeRegistry();
      g_selfdestruct = true;
      break;
    }
  }
  g_up = false;
}

/* -------------------------------------------------------------------- main */

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR cmdline, int) {
  std::wstring token = RegStr(L"Token", L"");

  // a token given on the command line is adopted and auto-saved, so a new
  // machine only ever needs one run with the argument
  const std::wstring arg_token = ParseTokenArg(cmdline);
  if (!arg_token.empty() && arg_token != token) {
    token = arg_token;
    RegSetStr(L"Token", token);
  }

  // no token anywhere yet: ask once, save, never again
  if (token.empty()) {
    token = PromptForToken();
    if (token.empty()) return 2;
    RegSetStr(L"Token", token);
  }

  const std::string hwid = ComputeHwid();

  for (;;) {
    Site* site = new Site();
    if (!site->Open(token, hwid)) { delete site; Sleep(3000); continue; }
    g_site = site;
    g_blocked = false;
    g_up = true;

    Report(g_st.attached() ? "injected" : "idle");
    RelayLog(true);   /* resume the panel's log view on reconnect */

    std::thread rx(RxLoop);
    while (g_up) {
      Sleep(1000);
      RelayLog(false);
    }
    rx.join();

    const bool blocked = g_blocked;
    const bool bye = g_selfdestruct;
    g_site = nullptr;
    site->Close();
    delete site;

    if (blocked) {
      MessageBoxW(nullptr,
                  L"This device is not linked to your account.\n"
                  L"Ask the owner to reset your HWID from the panel.",
                  L"grim", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
      return 3;
    }
    if (bye) return 0;
    Sleep(2000);
  }
}
