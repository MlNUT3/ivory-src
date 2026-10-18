#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_freetype.h"

#include "antidebug.h"
#include "programs.h"

#include "imgui_edited.hpp"
#define JM_XORSTR_DISABLE_AVXINTRIN
#include "obf/xorstr.hpp"
#include "obf/lazy_importer.hpp"

#include <D3DX11tex.h>
#pragma comment(lib, "D3DX11.lib")

#include "image.h"
#include "font.h"

#include "resource.h"

#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <thread>

#include <vector>
#include <random>
#include <math.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <shlobj.h>
#include <iphlpapi.h>
#include <intrin.h>
#pragma comment(lib, "iphlpapi.lib")

static const std::string ALLOWED_HWID = "04D9F5CF0E22-d8c63158-1c6e-46f4-bae8-b80684fc5163-178BFBFF00A20F10";

static constexpr uint32_t FNV_PRIME = 0x01000193u;
static constexpr uint32_t FNV_OFFSET = 0x811C9DC5u;

constexpr uint32_t fnv1a_hash(const char* s, uint32_t h = FNV_OFFSET) {
    return (*s == '\0') ? h : fnv1a_hash(s + 1, (h ^ (unsigned char)*s) * FNV_PRIME);
}

static constexpr uint32_t VALID_USERNAME_HASH = fnv1a_hash("admin");
static constexpr uint32_t VALID_PASSWORD_HASH = fnv1a_hash("123");

static int   login_fail_count = 0;
static float login_cooldown = 0.f;
static const int   MAX_FAILS = 5;
static const float COOLDOWN_SEC = 15.f;

bool check_credentials(const char* user, const char* pass) {
    uint32_t uh = FNV_OFFSET, ph = FNV_OFFSET;
    for (const char* c = user; *c; c++) uh = (uh ^ (unsigned char)*c) * FNV_PRIME;
    for (const char* c = pass; *c; c++) ph = (ph ^ (unsigned char)*c) * FNV_PRIME;
    return (uh == VALID_USERNAME_HASH) && (ph == VALID_PASSWORD_HASH);
}

std::string get_remember_file_path() {
    char appdata[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        std::string path = std::string(appdata) + "\\discord\\session.dat";
        CreateDirectoryA((std::string(appdata) + "\\discord").c_str(), NULL);
        return path;
    }
    return "session.dat";
}

std::string hwid_derive_key(const std::string& hwid) {
    std::string key = hwid;
    if (key.empty()) key = "fallback_key_9x";
    while (key.size() < 32) key += key;
    return key;
}

std::string xor_obfuscate(const std::string& input) {
    std::string key = hwid_derive_key(ALLOWED_HWID);
    std::string result = input;
    for (size_t i = 0; i < result.size(); i++)
        result[i] ^= key[i % key.size()] ^ (unsigned char)(i % 17);
    return result;
}

std::string get_mac_address() {
    std::string mac = "UNKNOWN";
    IP_ADAPTER_INFO adapterInfo[32];
    DWORD bufLen = sizeof(adapterInfo);
    if (GetAdaptersInfo(adapterInfo, &bufLen) != ERROR_SUCCESS) return mac;
    for (IP_ADAPTER_INFO* p = adapterInfo; p; p = p->Next) {
        if (p->AddressLength == 6) {
            std::ostringstream oss;
            oss << std::hex << std::uppercase << std::setfill('0');
            for (UINT i = 0; i < p->AddressLength; i++)
                oss << std::setw(2) << (int)p->Address[i];
            mac = oss.str();
            break;
        }
    }
    return mac;
}

std::string get_machine_guid() {
    HKEY hKey;
    char guidBuf[256] = { 0 };
    DWORD guidSize = sizeof(guidBuf);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "MachineGuid", NULL, NULL, (LPBYTE)guidBuf, &guidSize);
        RegCloseKey(hKey);
    }
    return std::string(guidBuf);
}

std::string get_cpu_id() {
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 1);
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    oss << std::setw(8) << cpuInfo[3] << std::setw(8) << cpuInfo[0];
    return oss.str();
}

std::string get_hwid() {
    std::string mac = get_mac_address();
    std::string guid = get_machine_guid();
    std::string cpu = get_cpu_id();
    return mac + "-" + guid + "-" + cpu;
}

bool check_hwid() {
    if (ALLOWED_HWID == "REPLACE_WITH_HWID_HERE") return true;
    std::string current = get_hwid();
    return current == ALLOWED_HWID;
}

void save_remember_me(const std::string& username, const std::string& password) {
    std::string raw = username + "|" + password;
    std::string obf = xor_obfuscate(raw);
    std::ofstream f(get_remember_file_path(), std::ios::binary);
    if (f.is_open()) {
        f.write(obf.c_str(), obf.size());
        f.close();
    }
}

bool load_remember_me(std::string& out_user, std::string& out_pass) {
    std::ifstream f(get_remember_file_path(), std::ios::binary);
    if (!f.is_open()) return false;
    std::string obf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (obf.empty()) return false;
    std::string raw = xor_obfuscate(obf);
    size_t sep = raw.find('|');
    if (sep == std::string::npos) return false;
    out_user = raw.substr(0, sep);
    out_pass = raw.substr(sep + 1);
    return true;
}

void delete_remember_me() {
    DeleteFileA(get_remember_file_path().c_str());
}

std::vector<ImVec2> circles_pos;
std::vector<ImVec2> circles_dir;
std::vector<int> circles_radius;

void draw_circle(ImVec2 pos, int radius, ImU32 color)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    unsigned char* color_ptr = (unsigned char*)&color;
    draw_list->AddCircleFilled(pos, radius - 1, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], 75));
}

void draw_line(ImVec2 pos1, ImVec2 pos2, ImU32 color, int radius)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float distance = std::sqrt(
        std::pow(pos2.x - pos1.x, 2) +
        std::pow(pos2.y - pos1.y, 2)
    );
    float alpha;
    if (distance <= 20.0f) {
        alpha = 255.0f;
    }
    else {
        alpha = (1.0f - ((distance - 20.0f) / 25.0f)) * 255.0f;
    }

    unsigned char* color_ptr = (unsigned char*)&color;

    draw_list->AddLine(pos1, pos2, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], static_cast<int>(alpha)), 1.0f);
    if (distance >= 40.0f) {
        draw_list->AddCircleFilled(pos1, radius - 0.96f, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], 30));
        draw_list->AddCircleFilled(pos2, radius - 0.96f, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], 30));
    }
    else if (distance <= 20.0f) {
        draw_list->AddCircleFilled(pos1, radius, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], 30));
        draw_list->AddCircleFilled(pos2, radius, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], 30));
    }
    else {
        float radius_factor = 1.0f - ((distance - 20.0f) / 20.0f);
        float offset_factor = 1.0f - radius_factor;
        float offset = (radius - radius * radius_factor) * offset_factor;
        draw_list->AddCircleFilled(pos1, radius - offset, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], 30));
        draw_list->AddCircleFilled(pos2, radius - offset, IM_COL32(color_ptr[0], color_ptr[1], color_ptr[2], 30));
    }
}

void move_circles()
{
    static std::mt19937 gen((std::random_device{})());
    static std::uniform_int_distribution<> dis(0, 360);

    for (int i = 0; i < (int)circles_pos.size(); i++)
    {
        ImVec2& pos = circles_pos[i];
        ImVec2& dir = circles_dir[i];
        int radius = circles_radius[i];

        pos.x += dir.x * 0.4f;
        pos.y += dir.y * 0.4f;

        if (pos.x - radius < 0 || pos.x + radius > ImGui::GetWindowWidth())
        {
            dir.x = -dir.x;
            dir.y = dis(gen) % 2 == 0 ? -1.0f : 1.0f;
        }

        if (pos.y - radius < 0 || pos.y + radius > ImGui::GetWindowHeight())
        {
            dir.y = -dir.y;
            dir.x = dis(gen) % 2 == 0 ? -1.0f : 1.0f;
        }
    }
}

void draw_circles_and_lines(ImU32 color)
{
    move_circles();

    for (int i = 0; i < (int)circles_pos.size(); i++)
    {
        draw_circle(circles_pos[i], circles_radius[i], color);

        for (int j = i + 1; j < (int)circles_pos.size(); j++)
        {
            float distance = ImGui::GetIO().FontGlobalScale * std::sqrt(std::pow(circles_pos[j].x - circles_pos[i].x, 2) + std::pow(circles_pos[j].y - circles_pos[i].y, 2));

            if (distance <= 45.0f)
            {
                draw_line(circles_pos[i], circles_pos[j], color, circles_radius[i]);
            }
        }
    }
}

void setup_circles()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 1);
    std::uniform_int_distribution<> pos_dis(0, static_cast<int>(1980));
    std::uniform_int_distribution<> pos_dis_y(0, static_cast<int>(1080));

    for (int i = 0; i < 150; i++)
    {
        circles_pos.push_back(ImVec2((float)pos_dis(gen), (float)pos_dis_y(gen)));
        circles_dir.push_back(ImVec2(dis(gen) == 0 ? -1.0f : 1.0f, dis(gen) == 0 ? -1.0f : 1.0f));
        circles_radius.push_back(3);
    }
}

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int tabs = 0;

int quest_count;
std::vector<std::string> quest_text;
char quest[250] = { "" };

float timer = 0;

using namespace ImGui;

namespace font
{
    inline ImFont* lexend_regular = nullptr;
    inline ImFont* lexend_medium = nullptr;
    inline ImFont* lexend_medium_x = nullptr;
    inline ImFont* lexend_bold = nullptr;

    inline ImFont* icomoon[41];
}

namespace texture
{
    ID3D11ShaderResourceView* background = nullptr;
    ID3D11ShaderResourceView* spoof_icon = nullptr;
    ID3D11ShaderResourceView* logo = nullptr;
}

void CricleProgress(const char* name, float progress, float max, float radius)
{
    static float tickness = 3.f;
    static float position = 0.f;
    static float alpha_text = 1.f;

    ImVec4 circle_loading = ImColor(0, 0, 0, 0);

    position = progress / max * 6.28f;

    ImVec2 vecCenter = { ImGui::GetContentRegionMax() / 2 };

    ImGui::GetForegroundDrawList()->PathClear();
    ImGui::GetForegroundDrawList()->PathArcTo(vecCenter, radius, 0.f, 2.f * IM_PI, 120.f);
    ImGui::GetForegroundDrawList()->PathStroke(ImGui::GetColorU32(circle_loading), 0, tickness);

    ImGui::GetForegroundDrawList()->PathClear();
    ImGui::GetForegroundDrawList()->PathArcTo(vecCenter, radius, IM_PI * 1.5f, IM_PI * 1.5f + position, 120.f);
    ImGui::GetForegroundDrawList()->PathStroke(ImGui::GetColorU32(c::accent), 0, tickness);

    int procent = (int)(progress / (int)max * 100);

    std::string procent_str = std::to_string(procent) + "%";

    ImGui::PushFont(font::lexend_regular);

    ImGui::GetForegroundDrawList()->AddText(vecCenter - ImGui::CalcTextSize(procent_str.c_str()) / 2, ImGui::GetColorU32(c::text::text_active), procent_str.c_str());

    alpha_text = ImLerp(alpha_text, procent > 80 ? 0.f : 1.f, ImGui::GetIO().DeltaTime * 6);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha_text);
    if (procent != 0) ImGui::GetForegroundDrawList()->AddText(ImVec2((ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(name).x) / 2, ImGui::GetContentRegionMax().y - alpha_text * 60), ImGui::GetColorU32(c::accent), name);
    ImGui::PopStyleVar();

    ImGui::PopFont();
}

HWND hwnd;
RECT rc;

void move_window() {
    ImGui::SetCursorPos(ImVec2(0, 0));
    if (ImGui::InvisibleButton("Move_detector", ImVec2(c::background::size.x, 50)));
    if (ImGui::IsItemActive()) {
        GetWindowRect(hwnd, &rc);
        MoveWindow(hwnd, rc.left + (int)ImGui::GetMouseDragDelta().x, rc.top + (int)ImGui::GetMouseDragDelta().y, (int)c::background::size.x, (int)c::background::size.y, TRUE);
    }
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    if (!check_hwid()) {
        MessageBoxA(NULL, "This software is not licensed for this machine.", "Access Denied", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::thread protection_thread(debugger_detection);
    protection_thread.detach();

    WNDCLASSEXW wc;
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = NULL;
    wc.cbWndExtra = NULL;
    wc.hInstance = nullptr;
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = L"ImGui";
    wc.lpszClassName = L"Example";
    wc.hIconSm = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));

    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(NULL, wc.lpszClassName, L"Example", WS_POPUP, (GetSystemMetrics(SM_CXSCREEN) / 2) - ((int)c::background::size.x / 2), (GetSystemMetrics(SM_CYSCREEN) / 2) - ((int)c::background::size.y / 2), (int)c::background::size.x, (int)c::background::size.y, 0, 0, 0, 0);

    SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    POINT mouse;
    rc = { 0 };
    GetWindowRect(hwnd, &rc);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImFontConfig cfg;
    cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_LightHinting | ImGuiFreeTypeBuilderFlags_LoadColor | ImGuiFreeTypeBuilderFlags_Bitmap;

    font::lexend_medium = io.Fonts->AddFontFromMemoryTTF(lexend_medium, sizeof(lexend_medium), 14.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    font::lexend_regular = io.Fonts->AddFontFromMemoryTTF(lexend_regular, sizeof(lexend_regular), 14.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    font::lexend_medium_x = io.Fonts->AddFontFromMemoryTTF(lexend_medium, sizeof(lexend_medium), 13.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());

    for (int i = 14; i <= 40; i++) font::icomoon[i] = io.Fonts->AddFontFromMemoryTTF(icomoon, sizeof(icomoon), (float)i, &cfg, io.Fonts->GetGlyphRangesCyrillic());

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    D3DX11_IMAGE_LOAD_INFO info; ID3DX11ThreadPump* pump{ nullptr };
    if (texture::background == nullptr) D3DX11CreateShaderResourceViewFromMemory(g_pd3dDevice, background, sizeof(background), &info, pump, &texture::background, 0);
    if (texture::spoof_icon == nullptr) D3DX11CreateShaderResourceViewFromMemory(g_pd3dDevice, spoof_icon, sizeof(spoof_icon), &info, pump, &texture::spoof_icon, 0);
    if (texture::logo == nullptr) D3DX11CreateShaderResourceViewFromMemory(g_pd3dDevice, logo, sizeof(logo), &info, pump, &texture::logo, 0);

    bool done = false;
    bool logged_in = false;
    bool login_failed = false;
    bool preloaded_login = false;

    static char login_buf[16] = { "" };
    static char password_buf[16] = { "" };
    static bool remember_me = false;

    {
        std::string saved_user, saved_pass;
        if (load_remember_me(saved_user, saved_pass)) {
            if (check_credentials(saved_user.c_str(), saved_pass.c_str())) {
                logged_in = true;
                preloaded_login = true;
                tabs = 1;
            }
        }
    }

    setup_circles();
    programs_init();

    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(c::background::size));

            ImGuiStyle* style = &ImGui::GetStyle();

            style->WindowPadding = ImVec2(0, 0);
            style->ItemSpacing = ImVec2(0, 0);
            style->WindowBorderSize = 0;
            style->ScrollbarSize = 7.f;

            Begin("IMGUI", nullptr, ImGuiWindowFlags_NoDecoration);
            {
                const ImVec2& pos = ImGui::GetWindowPos();
                const ImVec2& region = ImGui::GetContentRegionMax();
                const ImVec2& spacing = style->ItemSpacing;

                GetBackgroundDrawList()->AddImage(texture::background, ImVec2(0, 0), ImVec2(c::background::size), ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255));

                static float tab_alpha = 0.f;
                static float tab_add = 0.f;
                static int active_tab = 0;
                static bool closing = false;
                static float close_progress = 0.f;
                static float close_overlay_alpha = 0.f;
                static bool has_logged_in_once = false;
                static float open_progress = 0.f;
                static bool open_done = false;
                if (preloaded_login) { has_logged_in_once = true; preloaded_login = false; }

                tab_alpha = ImClamp(tab_alpha + (4.f * ImGui::GetIO().DeltaTime * (tabs == active_tab ? 1.f : -1.f)), 0.f, 1.f);

                if (!open_done) {
                    open_progress = ImClamp(open_progress + ImGui::GetIO().DeltaTime * 2.2f, 0.f, 1.f);
                    float max_r = std::sqrt(std::pow(c::background::size.x, 2.f) + std::pow(c::background::size.y, 2.f)) * 0.6f;
                    ImVec2 center = pos + ImVec2(c::background::size) / 2;
                    float inv = 1.f - open_progress;
                    GetWindowDrawList()->AddRectFilled(pos, pos + ImVec2(c::background::size), ImColor(0, 0, 0, (int)(inv * 255)));
                    GetWindowDrawList()->AddCircle(center, open_progress * max_r * 0.35f, GetColorU32(c::accent, open_progress * 0.4f), 100, 3.f);
                    GetWindowDrawList()->AddCircle(center, open_progress * max_r * 0.65f, GetColorU32(c::accent, open_progress * 0.6f), 100, 3.f);
                    GetWindowDrawList()->AddCircle(center, open_progress * max_r * 0.90f, GetColorU32(c::accent, open_progress * 0.8f), 100, 4.f);
                    GetWindowDrawList()->AddCircle(center, open_progress * max_r * 1.10f, GetColorU32(c::accent, open_progress), 100, 4.f);
                    if (open_progress >= 1.f) open_done = true;
                }

                if (tab_alpha == 0.f && tab_add == 0.f)
                {
                    if (!has_logged_in_once) {
                        timer++;
                        CricleProgress("Loading...", timer, 300, 60);
                        if (timer > 300) {
                            active_tab = tabs;
                            timer = 0;
                        }
                    }
                    else {
                        active_tab = tabs;
                    }
                }

                PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha * style->Alpha);

                if (active_tab == 0)
                {
                    {
                        float logo_size = 64.f;
                        ImVec2 logo_pos = pos + ImVec2((c::background::size.x - logo_size) / 2, 55);
                        GetWindowDrawList()->AddImage(texture::logo, logo_pos, logo_pos + ImVec2(logo_size, logo_size), ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255));
                    }

                    GetWindowDrawList()->AddText(pos + ImVec2((c::background::size.x - CalcTextSize("Welcome, please log in").x) / 2, 130), GetColorU32(c::accent), "Welcome, please log in");

                    if (login_cooldown > 0.f) {
                        login_cooldown -= ImGui::GetIO().DeltaTime;
                        if (login_cooldown < 0.f) login_cooldown = 0.f;
                        char cooldown_buf[64];
                        snprintf(cooldown_buf, sizeof(cooldown_buf), "Too many attempts. Wait %.0fs", login_cooldown);
                        GetWindowDrawList()->AddText(pos + ImVec2((c::background::size.x - CalcTextSize(cooldown_buf).x) / 2, 155), GetColorU32(c::red), cooldown_buf);
                    }
                    else if (login_failed) {
                        char fail_buf[64];
                        snprintf(fail_buf, sizeof(fail_buf), "Invalid credentials. Attempts: %d/%d", login_fail_count, MAX_FAILS);
                        GetWindowDrawList()->AddText(pos + ImVec2((c::background::size.x - CalcTextSize(fail_buf).x) / 2, 155), GetColorU32(c::red), fail_buf);
                    }

                    SetCursorPos(ImVec2((c::background::size.x - 280) / 2, 175));

                    edited::BeginChild("Container##2", ImVec2(280, 250), ImVec2(0, 15));
                    {
                        bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);

                        InputTextEx("a", "Login", login_buf, 16, ImVec2(280, 40), NULL);
                        InputTextEx("b", "Password", password_buf, 16, ImVec2(280, 40), ImGuiInputTextFlags_Password);

                        edited::Checkbox("Remember me", &remember_me);

                        bool can_attempt = (login_cooldown <= 0.f);

                        if ((edited::Button("LAUNCH", ImVec2(280, 40)) || enter_pressed) && can_attempt) {
                            if (check_credentials(login_buf, password_buf)) {
                                login_failed = false;
                                login_fail_count = 0;
                                login_cooldown = 0.f;
                                logged_in = true;
                                has_logged_in_once = true;
                                if (remember_me) {
                                    save_remember_me(std::string(login_buf), std::string(password_buf));
                                }
                                else {
                                    delete_remember_me();
                                }
                                tabs = 1;
                            }
                            else {
                                login_failed = true;
                                login_fail_count++;
                                if (login_fail_count >= MAX_FAILS) {
                                    login_cooldown = COOLDOWN_SEC;
                                    login_fail_count = 0;
                                }
                            }
                        }

                        SetCursorPosX((290 - CalcTextSize("Forgot your password?  Contact staff").x) / 2);
                        TextColored(ImColor(GetColorU32(c::text::text_hov)), "Forgot your password?");
                        SameLine(0, 5);
                        TextColored(ImColor(GetColorU32(c::accent)), "Contact staff");
                    }
                    edited::EndChild();
                }
                else if (active_tab >= 1 && active_tab < 3)
                {
                    if (!logged_in) { tabs = 0; }

                    GetWindowDrawList()->AddRectFilled(pos, pos + ImVec2(c::background::size.x, 50), GetColorU32(c::element::panel), c::element::rounding, ImDrawFlags_RoundCornersTop);
                    GetWindowDrawList()->AddRectFilled(pos + ImVec2(0, 50), pos + ImVec2(100, c::background::size.y), GetColorU32(c::element::panel), c::element::rounding, ImDrawFlags_RoundCornersBottomLeft);

                    {
                        float logo_size = 36.f;
                        ImVec2 logo_pos = pos + ImVec2((100 - logo_size) / 2, (50 - logo_size) / 2);
                        GetWindowDrawList()->AddImage(texture::logo, logo_pos, logo_pos + ImVec2(logo_size, logo_size), ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255));
                    }

                    GetWindowDrawList()->AddText(pos + (ImVec2(c::background::size.x + 50, 48) - CalcTextSize("Sabaru")) / 2, GetColorU32(c::accent), "Sabaru");

                    SetCursorPos(ImVec2((100 - 60) / 2, 70));

                    BeginGroup();
                    {
                        if (edited::page(1 == tabs, "d", ImVec2(60, 60))) tabs = 1;
                        if (edited::page(2 == tabs, "e", ImVec2(60, 60))) tabs = 2;

                        SetCursorPosY(370);

                        if (edited::page(0 == tabs, "c", ImVec2(60, 60))) {
                            logged_in = false;
                            delete_remember_me();
                            tabs = 0;
                        }
                    }
                    EndGroup();

                    ImGui::SetCursorPos(ImVec2(120, 70));

                    if (active_tab == 1)
                    {
                        static int selected_program = 0;

                        edited::BeginChild("Container##1", ImVec2(c::background::size.x / 2 - 140, c::background::size.y - 90), ImVec2(18, 18), true);
                        {
                            for (int i = 0; i < (int)g_programs.size(); i++)
                            {
                                if (edited::Spoof_panel(texture::spoof_icon, i == selected_program,
                                    g_programs[i].name,
                                    g_programs[i].description,
                                    g_programs[i].version,
                                    ImVec2(GetContentRegionMax().x - style->WindowPadding.x, 90),
                                    ImVec2(36, 36),
                                    g_programs[i].status))
                                {
                                    selected_program = i;
                                }
                            }

                            if (g_programs.empty())
                            {
                                float cy = (c::background::size.y - 90 - CalcTextSize("No programs added yet.").y) / 2;
                                SetCursorPosY(cy);
                                SetCursorPosX((GetContentRegionMax().x - CalcTextSize("No programs added yet.").x) / 2);
                                TextColored(ImColor(GetColorU32(c::text::text_hov)), "No programs added yet.");
                            }
                        }
                        edited::EndChild();

                        SameLine(0, 20);

                        edited::BeginChild("Container##2", ImVec2((c::background::size.x - 160) / 2, c::background::size.y - 90), ImVec2(18, 18), true);
                        {
                            if (!g_programs.empty() && selected_program < (int)g_programs.size())
                            {
                                const Program& prog = g_programs[selected_program];

                                edited::info_bar("Name:", prog.name);
                                edited::Separator_line();
                                edited::info_bar("Version:", prog.version);
                                edited::Separator_line();
                                edited::info_bar("Status:", prog.status == 0 ? "Ready" : prog.status == 1 ? "Updating" : "Offline");
                                edited::Separator_line();
                                edited::info_bar("Size:", (std::to_string(prog.bin_size / 1024) + " KB").c_str());
                                edited::Separator_line();

                                ImGui::SetCursorPosY(GetCursorPosY() + 20);

                                static std::string launch_status = "";
                                static float       status_timer = 0.f;

                                if (status_timer > 0.f) {
                                    status_timer -= ImGui::GetIO().DeltaTime;
                                    SetCursorPosX((GetContentRegionMax().x - CalcTextSize(launch_status.c_str()).x) / 2 + style->WindowPadding.x / 2);
                                    TextColored(ImColor(GetColorU32(c::accent, ImClamp(status_timer, 0.f, 1.f))), "%s", launch_status.c_str());
                                    ImGui::SetCursorPosY(GetCursorPosY() + 8);
                                }

                                if (edited::Button(nv_cheat_loaded() ? "UNLOAD" : "LAUNCH", ImVec2(GetContentRegionMax().x - style->WindowPadding.x, 36)))
                                {
                                    DWORD err = 0;
                                    if (!nv_cheat_loaded()) {
                                        if (nv_load_cheat(&err)) {
                                            launch_status = "Loaded - the overlay menu opens with F1 over the game.";
                                        }
                                        else {
                                            char errbuf[64];
                                            sprintf_s(errbuf, sizeof(errbuf), "Load failed. (err %lu)", err);
                                            launch_status = errbuf;
                                        }
                                    }
                                    else {
                                        if (nv_unload_cheat(&err)) {
                                            launch_status = "Unloaded.";
                                        }
                                        else {
                                            launch_status = "Unload failed.";
                                        }
                                    }
                                    status_timer = 5.f;
                                }
                            }
                            else
                            {
                                float cy = (c::background::size.y - 90 - CalcTextSize("Select a program.").y) / 2;
                                SetCursorPosY(cy);
                                SetCursorPosX((GetContentRegionMax().x - CalcTextSize("Select a program.").x) / 2);
                                TextColored(ImColor(GetColorU32(c::text::text_hov)), "Select a program.");
                            }
                        }
                        edited::EndChild();
                    }
                    else if (active_tab == 2)
                    {
                        edited::BeginChild("Container##1", ImVec2((c::background::size.x - 160) / 2, c::background::size.y - 90), ImVec2(18, 18), true);
                        {
                            edited::info_bar("User:", "admin");
                            edited::Separator_line();
                            edited::info_bar("Version:", "1.0.0");
                            edited::Separator_line();
                            edited::info_bar("HWID:", ALLOWED_HWID.substr(0, 18).append("...").c_str());
                            edited::Separator_line();
                            edited::info_bar("License:", "Valid");
                            edited::Separator_line();
                            edited::info_bar("Programs:", std::to_string(g_programs.size()).c_str());
                        }
                        edited::EndChild();

                        SameLine(0, 20);

                        edited::BeginChild("Container##2", ImVec2((c::background::size.x - 160) / 2, c::background::size.y - 90), ImVec2(18, 18), true);
                        {
                            edited::info_bar("Build:", __DATE__);
                            edited::Separator_line();
                            edited::info_bar("Platform:", "Windows x64");
                            edited::Separator_line();
                            edited::info_bar("Renderer:", "DirectX 11");
                            edited::Separator_line();
                            edited::info_bar("Session:", "Active");

                            ImGui::SetCursorPosY(GetCursorPosY() + 20);

                            if (edited::Button("Logout", ImVec2(GetContentRegionMax().x - style->WindowPadding.x, 30))) {
                                logged_in = false;
                                delete_remember_me();
                                tabs = 0;
                            }
                        }
                        edited::EndChild();
                    }
                }
                else if (active_tab == 3)
                {
                    static float circle_size = 0.f;

                    circle_size = ImLerp(circle_size, active_tab == 3 ? 170.f : 0.f, ImGui::GetIO().DeltaTime * 3.f);

                    GetWindowDrawList()->AddCircle(pos + ImVec2(c::background::size) / 2, circle_size, GetColorU32(c::accent, 0.3f), 100.f, 4.f);
                    GetWindowDrawList()->AddCircle(pos + ImVec2(c::background::size) / 2, circle_size * 2, GetColorU32(c::accent, 0.6f), 100.f, 4.f);
                    GetWindowDrawList()->AddCircle(pos + ImVec2(c::background::size) / 2, circle_size * 3, GetColorU32(c::accent), 100.f, 4.f);
                    GetWindowDrawList()->AddCircle(pos + ImVec2(c::background::size) / 2, circle_size * 4, GetColorU32(c::accent), 100.f, 4.f);

                    GetWindowDrawList()->AddText(pos + (ImVec2(c::background::size) - CalcTextSize("Operation completed successfully.") - ImVec2(0, 20)) / 2, GetColorU32(c::accent), "Operation completed successfully.");
                    GetWindowDrawList()->AddText(pos + (ImVec2(c::background::size) - CalcTextSize("You may close the application.") + ImVec2(0, 20)) / 2, GetColorU32(c::accent, 0.3f), "You may close the application.");

                    if (circle_size > 169.f) circle_size = 0.f;
                }
                else if (active_tab == 4)
                {
                }

                PopStyleVar();

                SetCursorPos(ImVec2(c::background::size.x - 50, 0));
                if (edited::IconButton("k", ImVec2(50, 50), NULL)) {
                    closing = true;
                }

                if (closing)
                {
                    close_progress = ImClamp(close_progress + ImGui::GetIO().DeltaTime * 1.8f, 0.f, 1.f);
                    close_overlay_alpha = ImClamp(close_overlay_alpha + ImGui::GetIO().DeltaTime * 3.5f, 0.f, 1.f);

                    float max_radius = std::sqrt(
                        std::pow(c::background::size.x, 2.f) +
                        std::pow(c::background::size.y, 2.f)
                    ) * 0.6f;

                    float r1 = close_progress * max_radius * 0.35f;
                    float r2 = close_progress * max_radius * 0.65f;
                    float r3 = close_progress * max_radius * 0.90f;
                    float r4 = close_progress * max_radius * 1.10f;

                    ImVec2 center = pos + ImVec2(c::background::size) / 2;

                    GetWindowDrawList()->AddRectFilled(pos, pos + ImVec2(c::background::size), ImColor(0, 0, 0, (int)(close_overlay_alpha * 210)));

                    GetWindowDrawList()->AddCircle(center, r1, GetColorU32(c::accent, close_progress * 0.4f), 100, 3.f);
                    GetWindowDrawList()->AddCircle(center, r2, GetColorU32(c::accent, close_progress * 0.6f), 100, 3.f);
                    GetWindowDrawList()->AddCircle(center, r3, GetColorU32(c::accent, close_progress * 0.8f), 100, 4.f);
                    GetWindowDrawList()->AddCircle(center, r4, GetColorU32(c::accent, close_progress), 100, 4.f);

                    float text_alpha = ImClamp((close_progress - 0.4f) / 0.6f, 0.f, 1.f);
                    const char* bye_text = "Closing...";
                    GetWindowDrawList()->AddText(
                        center - CalcTextSize(bye_text) / 2,
                        GetColorU32(c::accent, text_alpha),
                        bye_text
                    );

                    if (close_progress >= 1.f && close_overlay_alpha >= 1.f) {
                        cleanup_temp_files();
                        ShowWindow(hwnd, SW_HIDE);
                        PostQuitMessage(0);
                    }
                }

                draw_circles_and_lines(GetColorU32(c::accent, 0.2f));
                move_window();
            }
            End();
        }
        Render();

        const float clear_color_with_alpha[4] = { 0.f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}