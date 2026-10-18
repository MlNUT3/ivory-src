#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/render/render.h>
#include <features/system/playertools/avatars.h>

#include <ui/menu/settings/functions.h>
#include <ui/menu/data/fonts.h>
#include <imgui_freetype.h>
#include <dwmapi.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")
#include <cstdio>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <core/system/eject.h>
#include <map>
#include <filesystem>
#include <ui/assets/fonts/fontexo2medium.h>
#include <ui/assets/icons/tabicon.h>
#include <ui/assets/fonts/orokfonts.h>
#include <ui/assets/fonts/smallestpixel7.h>
#include <ui/assets/icons/cursorbytes.h>
#include <core/globals.h>
#include <core/utility/nvspinject/NvspInject.hpp>
#include <features/visuals/esp/esp.h>
#include <features/visuals/geometry/wallcheck.h>
#include <core/scanner/rescan.h>
#include <features/system/settings/settings.h>
#include <features/combat/aimbot/aimbot.h>
#include <features/system/keybind/keybind.h>
#include <features/system/config/config.h>
#include <features/system/playerlist/playerlist.h>
#include "sdk/math/math.h"
#include <sdk/game/game.h>
#include <core/logger/logger.h>
#include <features/system/notifications/notifications.h>
#include <sdk/cache/core/frame.h>
#include <ui/loader/loader.h>
#include <ui/menu/Menu.h>
#include <ui/menu/assets/nav_icons.h>
#include <ui/menu/assets/nav_bytes.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#define MENU_TITLE      "Ivory"
#define BUILD_DATE      __DATE__
#include <vector>
#include <d3d11.h>
#include <core/protection/xorstr.h>
static ID3D11ShaderResourceView* menu_icon_srv = nullptr;
static ID3D11ShaderResourceView* g_custom_cursor_srv = nullptr;
static bool InitCustomCursorTexture(ID3D11Device* device)
{
    if (g_custom_cursor_srv)
        return true;
    if (!device)
        return false;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = c_cursor_asset::width;
    desc.Height = c_cursor_asset::height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = c_cursor_asset::pixels;
    init_data.SysMemPitch = c_cursor_asset::width * 4;
    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &init_data, &texture);
    if (FAILED(hr))
        return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(texture, &srv_desc, &g_custom_cursor_srv);
    texture->Release();
    return SUCCEEDED(hr);
}
static void draw_custom_cursor()
{
    if (!var->gui.custom_cursor)
        return;
    if (!var->gui.menu_opened || var->gui.menu_alpha <= 0.01f)
        return;
    if (!g_custom_cursor_srv && render && render->detail && render->detail->device)
        InitCustomCursorTexture(render->detail->device);
    if (!g_custom_cursor_srv)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMousePosValid(&io.MousePos))
        return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const float dpi_scale = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
    const float target_height = 14.0f * dpi_scale;
    float scale = (static_cast<float>(c_cursor_asset::height) > 0.0f) ? (target_height / static_cast<float>(c_cursor_asset::height)) : 1.0f;
    scale = ImClamp(scale, 0.014f, 1.5f);
    ImVec2 cursor_size = ImVec2(static_cast<float>(c_cursor_asset::width) * scale, static_cast<float>(c_cursor_asset::height) * scale);
    ImVec2 cursor_min = io.MousePos;
    ImVec2 cursor_max = ImVec2(cursor_min.x + cursor_size.x, cursor_min.y + cursor_size.y);
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    const ImVec2 shadow_offset(1.0f, -2.0f);
    ImVec2 shadow_min = ImVec2(cursor_min.x + shadow_offset.x, cursor_min.y + shadow_offset.y);
    ImVec2 shadow_max = ImVec2(shadow_min.x + cursor_size.x, shadow_min.y + cursor_size.y);
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(g_custom_cursor_srv),
        shadow_min,
        shadow_max,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f * var->gui.menu_alpha))
    );
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(g_custom_cursor_srv),
        cursor_min,
        cursor_max,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImGui::ColorConvertFloat4ToU32(ImVec4(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, var->gui.menu_alpha))
    );
}
static ID3D11ShaderResourceView* load_icon_to_srv(ID3D11Device* device, const std::string& filepath)
{
	HICON hIcon = (HICON)LoadImageA(nullptr, filepath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
	if (!hIcon) return nullptr;
	ICONINFO iconInfo;
	if (!GetIconInfo(hIcon, &iconInfo))
	{
		DestroyIcon(hIcon);
		return nullptr;
	}
	BITMAP bmpColor;
	if (!GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmpColor))
	{
		DeleteObject(iconInfo.hbmColor);
		DeleteObject(iconInfo.hbmMask);
		DestroyIcon(hIcon);
		return nullptr;
	}
	int width = bmpColor.bmWidth;
	int height = bmpColor.bmHeight;
	std::vector<DWORD> pixels(width * height);
	HDC hdc = GetDC(nullptr);
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	if (!GetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS))
	{
		ReleaseDC(nullptr, hdc);
		DeleteObject(iconInfo.hbmColor);
		DeleteObject(iconInfo.hbmMask);
		DestroyIcon(hIcon);
		return nullptr;
	}
	ReleaseDC(nullptr, hdc);
	bool has_alpha = false;
	for (int i = 0; i < width * height; ++i)
	{
		if ((pixels[i] & 0xFF000000) != 0)
		{
			has_alpha = true;
			break;
		}
	}
	if (!has_alpha && iconInfo.hbmMask)
	{
		std::vector<DWORD> maskBits(width * height);
		BITMAPINFO bmiMask = {};
		bmiMask.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmiMask.bmiHeader.biWidth = width;
		bmiMask.bmiHeader.biHeight = -height;
		bmiMask.bmiHeader.biPlanes = 1;
		bmiMask.bmiHeader.biBitCount = 32;
		bmiMask.bmiHeader.biCompression = BI_RGB;
		hdc = GetDC(nullptr);
		if (GetDIBits(hdc, iconInfo.hbmMask, 0, height, maskBits.data(), &bmiMask, DIB_RGB_COLORS))
		{
			for (int i = 0; i < width * height; ++i)
			{
				if (maskBits[i] == 0)
				{
					pixels[i] |= 0xFF000000;
				} else {
					pixels[i] &= 0x00FFFFFF;
				}
			}
		}
		ReleaseDC(nullptr, hdc);
	}
	DeleteObject(iconInfo.hbmColor);
	DeleteObject(iconInfo.hbmMask);
	DestroyIcon(hIcon);
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA subResource = {};
	subResource.pSysMem = pixels.data();
	subResource.SysMemPitch = width * sizeof(DWORD);
	ID3D11Texture2D* texture = nullptr;
	HRESULT hr = device->CreateTexture2D(&desc, &subResource, &texture);
	if (FAILED(hr)) return nullptr;
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	ID3D11ShaderResourceView* srv = nullptr;
	hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
	texture->Release();
	if (FAILED(hr)) return nullptr;
	return srv;
}
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    {
        return true;
    }
    switch (msg)
    {
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
        {
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCPAINT:
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
    {
        // Overlay is only interactive when the menu or loader are open;
        // otherwise forward all hits to the windows beneath (the game).
        if (var->gui.menu_opened || loader::open)
            break;
        return HTTRANSPARENT;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
render_t::render_t()
{
    detail = std::make_unique<detail_t>();
}
render_t::~render_t()
{
	if (menu_icon_srv)
	{
		menu_icon_srv->Release();
		menu_icon_srv = nullptr;
	}
    destroy_imgui();
    destroy_window();
    destroy_device();
}
bool render_t::create_window(bool no_redirection_bitmap)
{
    detail->window_class.cbSize = sizeof(detail->window_class);
    detail->window_class.style = CS_CLASSDC;
    detail->window_class.lpszClassName = "T4";
    detail->window_class.hInstance = GetModuleHandleA(0);
    detail->window_class.lpfnWndProc = wnd_proc;
    detail->window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    detail->window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    detail->window_class.hIcon = LoadIconA(detail->window_class.hInstance, MAKEINTRESOURCEA(1));
    detail->window_class.hIconSm = LoadIconA(detail->window_class.hInstance, MAKEINTRESOURCEA(1));
    RegisterClassExA(&detail->window_class);
    // DirectComposition content only displays on windows created with
    // WS_EX_NOREDIRECTIONBITMAP; the legacy bitblt path needs a redirection
    // surface (WS_EX_LAYERED recipe) instead.
    LONG exstyle = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
    if (no_redirection_bitmap)
        exstyle |= WS_EX_NOREDIRECTIONBITMAP;
    else
        exstyle |= WS_EX_LAYERED;
    detail->window = CreateWindowExA(
        exstyle,
        detail->window_class.lpszClassName,
        "T4",
        WS_POPUP,
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        0,
        0,
        detail->window_class.hInstance,
        0
    );
    if (!detail->window)
    {
        return false;
    }
    if (!no_redirection_bitmap)
        SetLayeredWindowAttributes(detail->window, RGB(0, 0, 0), BYTE(255), LWA_ALPHA);
    RECT client_area{};
    RECT window_area{};
    GetClientRect(detail->window, &client_area);
    GetWindowRect(detail->window, &window_area);
    POINT diff{};
    ClientToScreen(detail->window, &diff);
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(detail->window, &margins);
    ShowWindow(detail->window, SW_HIDE);
    UpdateWindow(detail->window);
    return true;
}
// DirectComposition flip-model presentation. DWM can promote the overlay's
// content to a hardware overlay plane (MPO) - such content never enters the
// composed desktop image that NVIDIA ShadowPlay's driver-level capture reads,
// so the overlay stays visible on the monitor but absent from recordings.
bool render_t::create_device_dcomp()
{
	D3D_FEATURE_LEVEL feature_level;
	D3D_FEATURE_LEVEL feature_level_list[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	if (FAILED(D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0,
		feature_level_list,
		2,
		D3D11_SDK_VERSION,
		&detail->device,
		&feature_level,
		&detail->device_context
	)))
	{
		return false;
	}
	// DComp targets are not allowed on layered windows - strip the style (restored on failure)
	LONG original_exstyle = GetWindowLong(detail->window, GWL_EXSTYLE);
	SetWindowLong(detail->window, GWL_EXSTYLE, original_exstyle & ~WS_EX_LAYERED);
	IDXGIDevice* dxgi_device = nullptr;
	IDXGIFactory2* factory = nullptr;
	IDXGISwapChain1* swap_chain1 = nullptr;
	bool success = false;
	if (SUCCEEDED(detail->device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) &&
		SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
	{
		DXGI_SWAP_CHAIN_DESC1 scd{};
		scd.Width = GetSystemMetrics(SM_CXSCREEN);
		scd.Height = GetSystemMetrics(SM_CYSCREEN);
		scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scd.SampleDesc.Count = 1;
		scd.SampleDesc.Quality = 0;
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scd.BufferCount = 2;
		scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		scd.Scaling = DXGI_SCALING_STRETCH;
		// streamproof: create the swapchain as a HARDWARE-PROTECTED surface.
		// Protected scan-out planes are composited by the display engine, not
		// DWM - screen capture APIs (NvFBC/ShadowPlay, BitBlt, WGC) never see
		// their content while the user still sees the menu. Falls back to a
		// standard swapchain when the hardware/driver refuses the flag.
		bool hw_protected = false;
		for (int attempt = 0; attempt < 2 && !swap_chain1; ++attempt)
		{
			scd.Flags = (attempt == 0) ? DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED : 0u;
			if (SUCCEEDED(factory->CreateSwapChainForComposition(detail->device, &scd, nullptr, &swap_chain1)))				hw_protected = (attempt == 0);
		}
		bootlog("overlay: swapchain %s", hw_protected ? "hardware-protected (streamproof)" : "standard (hw-protected refused)");
		if (swap_chain1 &&
			SUCCEEDED(swap_chain1->QueryInterface(IID_PPV_ARGS(&detail->swap_chain3))) &&
			SUCCEEDED(DCompositionCreateDevice2(dxgi_device, IID_PPV_ARGS(&detail->dcomp_device))) &&
			SUCCEEDED(detail->dcomp_device->CreateTargetForHwnd(detail->window, TRUE, &detail->dcomp_target)) &&
			SUCCEEDED(detail->dcomp_device->CreateVisual(&detail->dcomp_visual)) &&
			SUCCEEDED(detail->dcomp_visual->SetContent(swap_chain1)) &&
			SUCCEEDED(detail->dcomp_target->SetRoot(detail->dcomp_visual)) &&
			SUCCEEDED(detail->dcomp_device->Commit()))
		{
			detail->swap_chain = swap_chain1;
			detail->buffer_count = scd.BufferCount;
			for (UINT i = 0; i < detail->buffer_count && i < 3; i++)
			{
				ID3D11Texture2D* back_buffer = nullptr;
				if (SUCCEEDED(swap_chain1->GetBuffer(i, IID_PPV_ARGS(&back_buffer))))
				{
					detail->device->CreateRenderTargetView(back_buffer, nullptr, &detail->flip_rtvs[i]);
					back_buffer->Release();
				}
			}
			if (detail->flip_rtvs[0] && detail->flip_rtvs[1])
			{
				detail->use_dcomp = true;
				success = true;
			}
		}
	}
	if (!success)
	{
		if (detail->flip_rtvs[0]) { detail->flip_rtvs[0]->Release(); detail->flip_rtvs[0] = nullptr; }
		if (detail->flip_rtvs[1]) { detail->flip_rtvs[1]->Release(); detail->flip_rtvs[1] = nullptr; }
		if (detail->flip_rtvs[2]) { detail->flip_rtvs[2]->Release(); detail->flip_rtvs[2] = nullptr; }
		if (detail->swap_chain) { detail->swap_chain->Release(); detail->swap_chain = nullptr; }
		if (detail->swap_chain3) { detail->swap_chain3->Release(); detail->swap_chain3 = nullptr; }
		if (detail->dcomp_visual) { detail->dcomp_visual->Release(); detail->dcomp_visual = nullptr; }
		if (detail->dcomp_target) { detail->dcomp_target->Release(); detail->dcomp_target = nullptr; }
		if (detail->dcomp_device) { detail->dcomp_device->Release(); detail->dcomp_device = nullptr; }
		if (detail->device_context) { detail->device_context->Release(); detail->device_context = nullptr; }
		if (detail->device) { detail->device->Release(); detail->device = nullptr; }
		// restore layered style so the legacy bitblt path works as before
		SetWindowLong(detail->window, GWL_EXSTYLE, original_exstyle);
	}
	if (factory)
		factory->Release();
	if (dxgi_device)
		dxgi_device->Release();
	return success;
}
bool render_t::create_device()
{
	// Prefer the DComp flip-model path (hardware overlay promotion hides the
	// overlay from NVIDIA's driver-level capture). Fall back to the legacy
	// bitblt path if composition is unsupported.
	if (create_device_dcomp())
		return true;
	// The legacy bitblt path needs a redirection surface - recreate the window
	// without WS_EX_NOREDIRECTIONBITMAP if the DComp attempt used one.
	if (detail->window)
	{
		LONG exstyle = GetWindowLong(detail->window, GWL_EXSTYLE);
		if (exstyle & WS_EX_NOREDIRECTIONBITMAP)
		{
			destroy_window();
			create_window(false);
		}
	}
    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    swap_chain_desc.BufferCount = 1;
    swap_chain_desc.BufferDesc.Width = 0;
    swap_chain_desc.BufferDesc.Height = 0;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.OutputWindow = detail->window;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swap_chain_desc.Windowed = 1;
    swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    D3D_FEATURE_LEVEL feature_level;
    D3D_FEATURE_LEVEL feature_level_list[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        feature_level_list,
        2,
        D3D11_SDK_VERSION,
        &swap_chain_desc,
        &detail->swap_chain,
        &detail->device,
        &feature_level,
        &detail->device_context
    );
    if (result == DXGI_ERROR_UNSUPPORTED)
    {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            feature_level_list,
            2,
            D3D11_SDK_VERSION,
            &swap_chain_desc,
            &detail->swap_chain,
            &detail->device,
            &feature_level,
            &detail->device_context
        );
    }
    if (result != S_OK)
    {
        MessageBoxA(nullptr, "This software can not run on your computer.", "Critical Problem", MB_ICONERROR | MB_OK);
    }
    ID3D11Texture2D* back_buffer{ nullptr };
    detail->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer)
    {
        detail->device->CreateRenderTargetView(back_buffer, nullptr, &detail->render_target_view);
        back_buffer->Release();
        return true;
    }
    return false;
}
static ImFont* tab_icon_font = nullptr;
bool render_t::create_imgui()
{
    if (!detail->window || !detail->device || !detail->device_context)
        return false;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(detail->window))
        return false;
    if (!ImGui_ImplDX11_Init(detail->device, detail->device_context))
        return false;
    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 3;
    cfg.PixelSnapH = true;
    cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    cfg.FontDataOwnedByAtlas = false;
    var->font.icons[0] = io.Fonts->AddFontFromMemoryTTF(section_icons_hex, sizeof(section_icons_hex), 15.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    var->font.icons[1] = io.Fonts->AddFontFromMemoryTTF(icons_hex, sizeof(icons_hex), 5.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    var->font.tahoma = io.Fonts->AddFontFromMemoryTTF(tahoma_hex, sizeof(tahoma_hex), 13.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    io.FontDefault = var->font.tahoma;
    float m_flDpiScale = 1.0f;
    esp_font_tahoma = io.Fonts->AddFontFromMemoryTTF(fs_tahoma_8px_ttf, fs_tahoma_8px_ttf_len, 12.0f * m_flDpiScale, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    esp_font_undefeated = io.Fonts->AddFontFromMemoryTTF(undefeated_ttf, undefeated_ttf_len, 22.0f * m_flDpiScale, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    esp_font_smallest_pixel = io.Fonts->AddFontFromMemoryTTF((void*)smallest_pixel_7_ttf, smallest_pixel_7_ttf_len, 11.0f * m_flDpiScale, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    // Load FontAwesome 6 icons for Linoria v2
    ImFontConfig font_cfg_icons;
    font_cfg_icons.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint;
    font_cfg_icons.GlyphOffset = ImVec2(0, 0);
    font_cfg_icons.SizePixels = 16.0f;
    font_cfg_icons.FontDataOwnedByAtlas = false;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    Menu::m_pIconFont = io.Fonts->AddFontFromMemoryCompressedTTF(fa6_solid_compressed_data, fa6_solid_compressed_size, 16.0f, &font_cfg_icons, icon_ranges);
    // Apply Linoria v2 style & default theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 0;
    style.PopupRounding = 0;
    style.GrabRounding = 0;
    style.ScrollbarRounding = 0;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;
    style.WindowPadding = ImVec2(6, 6);
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(5.0f, 4.0f);
    style.CellPadding = ImVec2(1.5f, 1.5f);
    style.ItemSpacing = ImVec2(4, 4);
    style.ItemInnerSpacing = ImVec2(4, 3);
    style.WindowMinSize = ImVec2(0, 0);
    style.ScrollbarSize = 6.0f;
    var->gui.menu_opened = false;   // the panel is the only UI - menu stays closed
    return true;
}
void render_t::destroy_device()
{
	if (detail->flip_rtvs[0]) detail->flip_rtvs[0]->Release();
	if (detail->flip_rtvs[1]) detail->flip_rtvs[1]->Release();
	if (detail->flip_rtvs[2]) detail->flip_rtvs[2]->Release();
	if (detail->swap_chain3) detail->swap_chain3->Release();
	if (detail->dcomp_visual) detail->dcomp_visual->Release();
	if (detail->dcomp_target) detail->dcomp_target->Release();
	if (detail->dcomp_device) detail->dcomp_device->Release();
	if (detail->render_target_view) detail->render_target_view->Release();
	if (detail->swap_chain) detail->swap_chain->Release();
	if (detail->device_context) detail->device_context->Release();
	if (detail->device) detail->device->Release();
}
void render_t::destroy_window()
{
    DestroyWindow(detail->window);
    UnregisterClassA(detail->window_class.lpszClassName, detail->window_class.hInstance);
}
void render_t::destroy_imgui()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
	// --------------------------------------------------------------------------
	// Smooth capture-visible cursor (carrier window)
	//
	// The hardware cursor is composited on a separate scan-out plane by the
	// display engine - it never lands in the framebuffer that driver-level
	// capture (NvFBC / ShadowPlay) reads, and the OS strips the pointer over
	// WDA-excluded windows for composited capture too.
	//
	// Solution: a tiny UNPROTECTED layered window that mirrors the REAL system
	// cursor (GetCursorInfo/GetIconInfo) at the mouse position. Recordings
	// cannot see the overlay (protected) but they capture this window, so its
	// content IS the cursor in recordings - exact appearance, no afterimage.
	// On the live screen the carrier sits pixel-exact underneath the identical
	// hardware cursor image, so it is invisible during normal play. When the
	// game hides the cursor (Roblox mouse lock) the carrier hides too.
	// Unlike the mouse-trails trick there is zero afterimage.
	// --------------------------------------------------------------------------
	namespace cursor_capture
	{
		inline HWND s_hwnd = nullptr;
		inline std::atomic<bool> s_stop{ false };
		inline std::thread s_worker;
		inline std::mutex s_bm_mtx;

		// rendered cursor bitmap state (guarded by s_bm_mtx)
		inline HCURSOR s_last_cursor = nullptr;
		inline HBITMAP s_dib = nullptr;
		inline BYTE* s_bits = nullptr;
		inline HDC s_hdc = nullptr;
		inline int s_bw = 0, s_bh = 0;
		inline int s_hot_x = 0, s_hot_y = 0;
		inline bool s_visible = false;

		inline bool create_window()
		{
			WNDCLASSEXA wc = {};
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = DefWindowProcA;
			wc.hInstance = GetModuleHandleA(nullptr);
			wc.lpszClassName = "IvoryCursor";
			RegisterClassExA(&wc);

			s_hwnd = CreateWindowExA(
				WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
				wc.lpszClassName, wc.lpszClassName, WS_POPUP,
				0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
			if (!s_hwnd)
			{
				bootlog("cursor: carrier window creation failed (gle %lu)", GetLastError());
				return false;
			}
			return true;
		}

		// render the given HCURSOR into the premultiplied-BGRA layered bitmap
		inline bool build_cursor_bitmap(HCURSOR hcur)
		{
			ICONINFO ii{};
			if (!GetIconInfo(hcur, &ii))
				return false;

			bool ok = false;
			BITMAP bm_mask{}, bm_color{};
			bool has_color = ii.hbmColor != nullptr;
			std::vector<BYTE> mask_px, color_px;
			int w = 0, h = 0;

			auto read_bitmap = [&](HBITMAP hbmp, BITMAP& bm, std::vector<BYTE>& out) -> bool
			{
				if (!hbmp || !GetObject(hbmp, sizeof(BITMAP), &bm))
					return false;
				BITMAPINFO bi = {};
				bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bi.bmiHeader.biWidth = bm.bmWidth;
				bi.bmiHeader.biHeight = -bm.bmHeight; // top-down
				bi.bmiHeader.biPlanes = 1;
				bi.bmiHeader.biBitCount = 32;
				bi.bmiHeader.biCompression = BI_RGB;
				out.resize((size_t)bm.bmWidth * bm.bmHeight * 4);
				HDC scr = GetDC(nullptr);
				int got = GetDIBits(scr, hbmp, 0, bm.bmHeight, out.data(), &bi, DIB_RGB_COLORS);
				ReleaseDC(nullptr, scr);
				return got == bm.bmHeight;
			};

			if (!read_bitmap(ii.hbmMask, bm_mask, mask_px))
				goto cleanup;

			if (has_color)
			{
				if (!read_bitmap(ii.hbmColor, bm_color, color_px))
					goto cleanup;
				w = bm_color.bmWidth;
				h = bm_color.bmHeight;
			}
			else
			{
				w = bm_mask.bmWidth;
				h = bm_mask.bmHeight / 2; // mono mask: AND (top) + XOR (bottom)
			}

			if (w > 0 && h > 0 && w < 256 && h < 256)
			{
				// The OS stores cursor transparency in different places: true
				// ARGB cursors (game cursors) carry an alpha channel, while
				// classic system cursors come back from GetDIBits with an
				// all-zero alpha and rely on the AND mask - the alpha must be
				// synthesized per-pixel from the mask or the cursor renders as
				// a black silhouette (only the opaque-black fallback pixels).
				bool has_alpha = false;
				for (size_t i = 3; i < color_px.size(); i += 4)
				{
					if (color_px[i]) { has_alpha = true; break; }
				}

				// (re)allocate the DIB
				if (s_dib && (s_bw != w || s_bh != h))
				{
					DeleteObject(s_dib); s_dib = nullptr; s_bits = nullptr;
					if (s_hdc) { DeleteDC(s_hdc); s_hdc = nullptr; }
				}
				if (!s_dib)
				{
					BITMAPINFO bi = {};
					bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
					bi.bmiHeader.biWidth = w;
					bi.bmiHeader.biHeight = -h;
					bi.bmiHeader.biPlanes = 1;
					bi.bmiHeader.biBitCount = 32;
					bi.bmiHeader.biCompression = BI_RGB;
					void* pv = nullptr;
					s_dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pv, nullptr, 0);
					if (!s_dib)
						goto cleanup;
					s_bits = (BYTE*)pv;
					s_bw = w; s_bh = h;
					s_hdc = CreateCompatibleDC(nullptr);
					SelectObject(s_hdc, s_dib);
				}

				memset(s_bits, 0, (size_t)w * h * 4);

				// Empty color bitmaps: some color cursors (hand, busy, resize on
				// certain schemes) read back as all-zero RGB - synthesizing alpha
				// from the AND mask then paints a pure-black silhouette. The OS
				// knows how these should look: rasterize with DrawIconEx and use
				// its composited output instead (straight alpha -> premultiplied).
				bool color_empty = has_color;
				if (has_color)
				{
					color_empty = true;
					for (size_t i = 0; i + 3 < color_px.size(); i += 4)
						if (color_px[i] | color_px[i + 1] | color_px[i + 2]) { color_empty = false; break; }
				}
				if (has_color && color_empty && DrawIconEx(s_hdc, 0, 0, hcur, w, h, 0, nullptr, DI_NORMAL))
				{
					for (int y = 0; y < h; y++)
					{
						for (int x = 0; x < w; x++)
						{
							BYTE* d = s_bits + ((size_t)y * w + x) * 4;
							const BYTE a = d[3];
							d[0] = (BYTE)((d[0] * a) / 255);
							d[1] = (BYTE)((d[1] * a) / 255);
							d[2] = (BYTE)((d[2] * a) / 255);
						}
					}
					s_hot_x = (int)ii.xHotspot;
					s_hot_y = (int)ii.yHotspot;
					s_last_cursor = hcur;
					ok = true;
					goto cleanup;
				}
				const int mask_stride = ((bm_mask.bmWidth * 32 + 31) / 32) * 4; // GetDIBits 32bpp stride

				for (int y = 0; y < h; y++)
				{
					for (int x = 0; x < w; x++)
					{
						BYTE* d = s_bits + ((size_t)y * w + x) * 4;
						BYTE R = 0, G = 0, B = 0, A = 0;

						if (has_color)
						{
							const BYTE* px = color_px.data() + ((size_t)y * w + x) * 4;
							B = px[0]; G = px[1]; R = px[2]; A = px[3];
							if (!has_alpha)
							{
								// synthesize alpha from the AND mask
								// (AND=0 -> opaque, AND=1 -> transparent)
								const BYTE* and_px = mask_px.data() + ((size_t)y * mask_stride + x * 4);
								A = and_px[0] ? 0 : 255;
							}
						}
						else
						{
							// classic monochrome cursor: AND half (top) + XOR
							// half (bottom). XOR is the actual pixel COLOR -
							// AND=0/XOR=1 is WHITE, not black (the old code
							// rendered every classic cursor as a black blob)
							const BYTE* and_px = mask_px.data() + ((size_t)y * mask_stride + x * 4);
							const BYTE* xor_px = mask_px.data() + ((size_t)(h + y) * mask_stride + x * 4);
							bool and_set = and_px[0] != 0;
							bool xor_set = xor_px[0] != 0;
							if (!and_set) { R = xor_set ? 255 : 0; G = R; B = R; A = 255; } // XOR color
							else if (xor_set) { R = G = B = 255; A = 200; }                // inverted -> white approx
							else { R = G = B = 0; A = 0; }                                 // transparent
						}

						// premultiply for UpdateLayeredWindow(ULW_ALPHA)
						d[0] = (BYTE)((B * A) / 255);
						d[1] = (BYTE)((G * A) / 255);
						d[2] = (BYTE)((R * A) / 255);
						d[3] = A;
					}
				}

				s_hot_x = (int)ii.xHotspot;
				s_hot_y = (int)ii.yHotspot;
				s_last_cursor = hcur;
				ok = true;
			}

		cleanup:
			if (ii.hbmColor) DeleteObject(ii.hbmColor);
			if (ii.hbmMask) DeleteObject(ii.hbmMask);
			return ok;
		}

		inline void update()
		{
			CURSORINFO ci{};
			ci.cbSize = sizeof(ci);
			if (!GetCursorInfo(&ci))
				return;

			bool showing = (ci.flags & CURSOR_SHOWING) && ci.hCursor != nullptr;
			if (!showing)
			{
				if (s_visible)
				{
					ShowWindow(s_hwnd, SW_HIDE);
					s_visible = false;
				}
				return;
			}

			{
				std::lock_guard<std::mutex> lock(s_bm_mtx);
				if (ci.hCursor != s_last_cursor && !build_cursor_bitmap(ci.hCursor))
				{
					// unreadable cursor shape - hide rather than draw stale
					if (s_visible) { ShowWindow(s_hwnd, SW_HIDE); s_visible = false; }
					return;
				}
				if (!s_dib)
					return;

				const int x = ci.ptScreenPos.x - s_hot_x;
				const int y = ci.ptScreenPos.y - s_hot_y;

				POINT src = { 0, 0 };
				SIZE  size = { s_bw, s_bh };
				POINT pos = { x, y };
				// pblend is REQUIRED with ULW_ALPHA - passing nullptr made every
				// call fail with ERROR_GEN_FAILURE (31)
				BLENDFUNCTION blend = {};
				blend.BlendOp = AC_SRC_OVER;
				blend.SourceConstantAlpha = 255;
				blend.AlphaFormat = AC_SRC_ALPHA;  // bitmap is premultiplied
				if (!UpdateLayeredWindow(s_hwnd, nullptr, &pos, &size, s_hdc, &src, 0, &blend, ULW_ALPHA))
				{
					static thread_local ULONG s_fail_once = 0;
					if (s_fail_once++ < 3)
						bootlog("cursor: UpdateLayeredWindow failed (gle %lu)", GetLastError());
					return;
				}
			}

			// stay above everything, never steal focus
			SetWindowPos(s_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			if (!s_visible)
			{
				ShowWindow(s_hwnd, SW_SHOWNOACTIVATE);
				s_visible = true;
			}
		}

		inline void worker_loop()
		{
			// resident: we manage this thread's lifetime ourselves (join on
			// disable) - soft_unload must not TerminateThread it mid-ULW
			sys_eject::register_current_thread(true);

			// the window AND every resource live on THIS thread - ULW fails
			// with ERROR_GEN_FAILURE (31) when called cross-thread (verified)
			if (!create_window())
				return;
			bootlog("cursor: carrier window active");

			while (!s_stop.load(std::memory_order_relaxed))
			{
				update();
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}

			DestroyWindow(s_hwnd);  // owning thread - allowed here
			s_hwnd = nullptr;
			s_visible = false;
			s_last_cursor = nullptr;
			{
				std::lock_guard<std::mutex> lock(s_bm_mtx);
				if (s_dib) { DeleteObject(s_dib); s_dib = nullptr; s_bits = nullptr; }
				if (s_hdc) { DeleteDC(s_hdc); s_hdc = nullptr; }
				s_bw = s_bh = 0;
			}
			bootlog("cursor: carrier window disabled");
		}

		inline void apply(bool enable)
		{
			if (enable)
			{
				if (!s_worker.joinable())
				{
					s_stop.store(false, std::memory_order_relaxed);
					s_worker = std::thread(worker_loop);
				}
			}
			else if (s_worker.joinable())
			{
				s_stop.store(true, std::memory_order_relaxed);
				s_worker.join();
				s_worker = std::thread();
			}
		}
	}

	void cursor_capture::restore()
	{
		apply(false);
	}

	void render_t::start_render()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
	static bool s_last_streamproof = !settings::streamproof;
	static bool s_last_cursor_capture = !settings::stealth::cursor_in_capture;
	if (settings::streamproof != s_last_streamproof || settings::stealth::cursor_in_capture != s_last_cursor_capture)
	{
		s_last_streamproof = settings::streamproof;
		s_last_cursor_capture = settings::stealth::cursor_in_capture;
		// WDA_EXCLUDEFROMCAPTURE IS the streamproof mechanism for composited
		// capture paths - dropping it to WDA_NONE breaks the protection
		// entirely (verified in testing). Cursor visibility in recordings is
		// handled by the software-cursor path below instead.
		SetWindowDisplayAffinity(detail->window, settings::streamproof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
		// keep the pointer visible in recordings while streamproof is on:
		// force software cursor composition (saved/restored system setting)
		cursor_capture::apply(settings::streamproof && settings::stealth::cursor_in_capture);
		// NVIDIA container manip: inject/unject the payload into nvcontainer.exe
		// (side thread - the manual map can block for up to 5 seconds)
		std::thread([](bool enable)
		{
			if (enable)
			{
				if (NvspInject::Load())
					bootlog("streamproof: nvsp manip loaded into nvcontainer (pid %lu)",
						(unsigned long)NvspInject::g_LastLoadedPid);
				else
					bootlog("streamproof: nvsp inject failed (nvcontainer not running, payload error, or access denied - try running as admin)");
			}
			else
			{
				if (NvspInject::Unload())
					bootlog("streamproof: nvsp manip unloaded from nvcontainer");
			}
		}, settings::streamproof).detach();
	}

	// Direct input polling - feeds mouse pos/buttons straight from
	// GetCursorPos/GetAsyncKeyState so the UI never depends on message
	// delivery alone.
	{
		ImGuiIO& poll_io = ImGui::GetIO();
		POINT p{};
		GetCursorPos(&p);
		POINT client = p;
		ScreenToClient(detail->window, &client);
		poll_io.AddMousePosEvent(static_cast<float>(client.x), static_cast<float>(client.y));
		poll_io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		poll_io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
	}
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    frame_cache::tick(game::visualengine.get());
    RECT rect;
    if (GetClientRect(detail->window, &rect))
    {
        g::iWidth = rect.right - rect.left;
        g::iHeight = rect.bottom - rect.top;
    }
}
void render_t::end_render(){
    float clear_color[4]{
 0, 0, 0, 0 };
    ID3D11RenderTargetView* rtv = detail->render_target_view;
    if (detail->use_dcomp && detail->swap_chain3)
    {
        UINT index = detail->swap_chain3->GetCurrentBackBufferIndex();
        rtv = detail->flip_rtvs[index % (detail->buffer_count ? detail->buffer_count : 1)];
    }
    detail->device_context->OMSetRenderTargets(1, &rtv, nullptr);
    detail->device_context->ClearRenderTargetView(rtv, clear_color);
    if (game::visualengine && game::visualengine->address != 0)
    {
        const auto fc   = frame_cache::get_for_thread();
        const math::matrix4& view = fc.view;
        const math::vector2& dims = fc.dims;
        if (dims.x > 0.0f && dims.y > 0.0f)
        {
            // Prediction dot: draws at the predicted aim point
            if (settings::aimbot::prediction_dot && settings::aimbot::enable_prediction)
            {
                math::vector3 aim3d{};
                if (aimbot::get_aim_point(aim3d))
                {
                    math::vector2 scr{};
                    if (game::visualengine->world_to_screen(view, dims, aim3d, scr))
                    {
                        ImDrawList* dl = ImGui::GetBackgroundDrawList();
                        const ImVec2 c(scr.x, scr.y);
                        const float r = (std::max)(1.5f, settings::aimbot::prediction_size * 0.5f);
                        const float* pc = settings::aimbot::prediction_color;
                        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(pc[0], pc[1], pc[2], pc[3]));
                        dl->AddCircleFilled(c, r, col);
                        if (settings::aimbot::prediction_outline)
                            dl->AddCircle(c, r, IM_COL32(0, 0, 0, 255), 0, 1.5f);
                    }
                }
            }
        }
    }

    draw_custom_cursor();
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    detail->swap_chain->Present(0, 0);
    if (settings::vsync)        DwmFlush();
    static bool window_revealed = false;
    if (!window_revealed)
    {
        ShowWindow(detail->window, SW_SHOW);
        SetForegroundWindow(detail->window);
        BringWindowToTop(detail->window);
        window_revealed = true;
    }
}
static void DrawTabIcon(ImDrawList* draw_list, ImVec2 pos, int tab_index, ImU32 color, float size)
{
	pos.x = (float)(int)(pos.x);
	pos.y = (float)(int)(pos.y);
    float r = size * 0.5f;
    ImVec2 center = pos + ImVec2(r, r);
	center.x = (float)(int)(center.x);
	center.y = (float)(int)(center.y);
        if (tab_index == 0)
        {
        draw_list->AddCircle(center, r * 0.65f, color, 32, 1.5f);
        draw_list->AddLine(center - ImVec2(0, r * 0.95f), center - ImVec2(0, r * 0.72f), color, 1.5f);
        draw_list->AddLine(center + ImVec2(0, r * 0.72f), center + ImVec2(0, r * 0.95f), color, 1.5f);
        draw_list->AddLine(center - ImVec2(r * 0.95f, 0), center - ImVec2(r * 0.72f, 0), color, 1.5f);
        draw_list->AddLine(center + ImVec2(r * 0.72f, 0), center + ImVec2(r * 0.95f, 0), color, 1.5f);
        draw_list->AddCircleFilled(center, 1.5f, color, 16);
    }
    else if (tab_index == 1)
    {
        ImVec2 p1 = center - ImVec2(r * 0.8f, 0);
        ImVec2 p2 = center + ImVec2(r * 0.8f, 0);
                ImVec2 cp_top1 = center + ImVec2(-r * 0.4f, -r * 0.45f);
        ImVec2 cp_top2 = center + ImVec2(r * 0.4f, -r * 0.45f);
        ImVec2 cp_bot1 = center + ImVec2(-r * 0.4f, r * 0.45f);
        ImVec2 cp_bot2 = center + ImVec2(r * 0.4f, r * 0.45f);
                draw_list->AddBezierCubic(p1, cp_top1, cp_top2, p2, color, 1.5f);
        draw_list->AddBezierCubic(p1, cp_bot1, cp_bot2, p2, color, 1.5f);
                draw_list->AddCircleFilled(center, r * 0.28f, color, 32);
    }
    else if (tab_index == 2)
    {
        draw_list->AddCircle(center, r * 0.48f, color, 32, 1.5f);
        draw_list->AddCircle(center, r * 0.18f, color, 32, 1.2f);
        for (int i = 0; i < 8; ++i)
        {
            float angle = i * (IM_PI / 4.0f);
            ImVec2 dir(cosf(angle), sinf(angle));
            draw_list->AddLine(center + dir * (r * 0.46f), center + dir * (r * 0.78f), color, 2.2f
	);
	}
}
			else
 if (tab_index == 3)
 {
        ImVec2 head_center = center - ImVec2(0, r * 0.25f);
        draw_list->AddCircle(head_center, r * 0.28f, color, 32, 1.5f);
                draw_list->PathClear();
        draw_list->PathArcTo(center + ImVec2(0, r * 0.8f), r * 0.65f, IM_PI + 0.2f, IM_PI * 2.0f - 0.2f, 32);
        draw_list->PathStroke(color, 0, 1.5f);
                draw_list->AddLine(center + ImVec2(-r * 0.12f, r * 0.05f), center + ImVec2(-r * 0.12f, r * 0.2f), color, 1.5f);
        draw_list->AddLine(center + ImVec2(r * 0.12f, r * 0.05f), center + ImVec2(r * 0.12f, r * 0.2f), color, 1.5f);
    }
    else if (tab_index == 4)
    {
		ImVec2 b_min = center - ImVec2(r * 0.75f, r * 0.35f);
		ImVec2 b_max = center + ImVec2(r * 0.75f, r * 0.6f);
		b_min.x = (float)(int)(b_min.x);
		b_min.y = (float)(int)(b_min.y);
		b_max.x = (float)(int)(b_max.x);
		b_max.y = (float)(int)(b_max.y);
        draw_list->AddRect(b_min, b_max, color, 1.5f, 0, 1.5f);
		ImVec2 t_min = center - ImVec2(r * 0.75f, r * 0.6f);
		ImVec2 t_max = center - ImVec2(r * 0.25f, r * 0.35f);
		t_min.x = (float)(int)(t_min.x);
		t_min.y = (float)(int)(t_min.y);
		t_max.x = (float)(int)(t_max.x);
		t_max.y = (float)(int)(t_max.y);
        draw_list->AddRectFilled(t_min, t_max, color, 1.0f);
    }
    else if (tab_index == 5)
    {
        float y1 = (float)(int)(center.y - r * 0.35f);
        float y2 = (float)(int)(center.y + r * 0.35f);
                ImVec4 track_col = ImGui::ColorConvertU32ToFloat4(color);
        track_col.w *= 0.4f;
        ImU32 faded_color = ImGui::ColorConvertFloat4ToU32(track_col);
                draw_list->AddLine(ImVec2((float)(int)(center.x - r * 0.8f), y1), ImVec2((float)(int)(center.x + r * 0.8f), y1), faded_color, 1.5f);
        draw_list->AddLine(ImVec2((float)(int)(center.x - r * 0.8f), y2), ImVec2((float)(int)(center.x + r * 0.8f), y2), faded_color, 1.5f);
                draw_list->AddCircleFilled(ImVec2((float)(int)(center.x - r * 0.25f), y1), 2.5f, color, 32);
        draw_list->AddCircleFilled(ImVec2((float)(int)(center.x + r * 0.3f), y2), 2.5f, color, 32);
    }
}
static bool render_vertical_tab(const char* label, int* current_page, int page_id, int icon_index)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)        return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
        ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 38.0f);
    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id))        return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)        *current_page = page_id;
    bool active = (*current_page == page_id);
    bool icon_only = (size.x < 60.0f);
    if (active)
    {
        window->DrawList->AddRectFilled(            pos + ImVec2(4.0f, 2.0f),            pos + size - ImVec2(4.0f, 2.0f),            IM_COL32(42, 42, 42, static_cast<int>(220 * style.Alpha)),            style.ChildRounding        );
    }
    ImU32 tab_color;
    if (active)        tab_color = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    else if (hovered)        tab_color = ImGui::GetColorU32(ImGuiCol_Text);
    else        tab_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    float icon_size = 18.0f;
    ImVec2 icon_pos;
    if (icon_only)        icon_pos = pos + ImVec2((size.x - icon_size) * 0.5f, (size.y - icon_size) * 0.5f);
    else        icon_pos = pos + ImVec2(12.0f, (size.y - icon_size) * 0.5f);
    if (tab_icon_font)
    {
        char text_str[2] = {
 (char)('A' + icon_index), '\0' };
        ImVec2 glyph_size = tab_icon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, text_str);
        ImVec2 centered_pos = icon_pos + ImVec2((icon_size - glyph_size.x) * 0.5f, (icon_size - glyph_size.y) * 0.5f);
        window->DrawList->AddText(tab_icon_font, icon_size, centered_pos, tab_color, text_str);
    }
    else    {
        DrawTabIcon(window->DrawList, icon_pos, icon_index, tab_color, icon_size);
    }
    if (!icon_only)
    {
        ImVec2 label_size = ImGui::CalcTextSize(label);
        ImVec2 text_pos   = pos + ImVec2(12.0f + icon_size + 10.0f, (size.y - label_size.y) * 0.5f);
        window->DrawList->AddText(text_pos, tab_color, label);
    }
    return pressed;
}
struct snow_particle_t{
	ImVec2 pos;
	float speed;
	float size;
	float drift;
};
struct rain_particle_t{
	ImVec2 pos;
	float speed;
	float length;
};
namespace ImAdd {
    inline bool TabButtonAccent(const char* label, bool active, const ImVec4& accent_col)
    {
        ImVec2 size = ImGui::CalcTextSize(label) + ImVec2(12.0f, 4.0f);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, accent_col);
        }
 else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(140 / 255.f, 140 / 255.f, 140 / 255.f, 1.0f));
        }
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(12 / 255.f, 16 / 255.f, 25 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(23 / 255.f, 28 / 255.f, 41 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(23 / 255.f, 28 / 255.f, 41 / 255.f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(23 / 255.f, 28 / 255.f, 41 / 255.f, 1.0f));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
        return pressed;
    }
}
void render_t::render_menu(){
    // irl mode: nothing renders - features keep running silently
    if (settings::general::irl_mode) { var->gui.menu_opened = false; return; }
    if (loader::open)        loader::render_loader();
    // the in-game menu is fully removed (the web panel is the only UI) -
    // only the non-menu drawing that used to ride along with it stays
    wallcheck::render();
    notifications::render();
}
static void draw_fov_circle(float fov_radius, float circle_colour[4], float outline_colour[4], bool fill_fov = false, float fill_colour[4] = nullptr, bool follow_mouse = false)
{
	if (fov_radius <= 0.f)		return;
	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	if (!draw)		return;
	float cx = 0.f;
	float cy = 0.f;
	POINT overlay_pt{
 0, 0 };
	if (render && render->detail && render->detail->window)		ClientToScreen(render->detail->window, &overlay_pt);
	if (follow_mouse)
	{
		POINT cursor{ };
		if (GetCursorPos(&cursor))
		{
			cx = static_cast<float>(cursor.x - overlay_pt.x);
			cy = static_cast<float>(cursor.y - overlay_pt.y);
		}
	}
	if (cx <= 0.f || cy <= 0.f)
	{
		HWND game_hwnd = game::get_roblox_window();
		if (game_hwnd && IsWindow(game_hwnd))
	{
			RECT rect{};
			if (GetClientRect(game_hwnd, &rect))
			{
				POINT center_pt = {
 (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
				if (ClientToScreen(game_hwnd, &center_pt))
				{
					cx = static_cast<float>(center_pt.x - overlay_pt.x);
					cy = static_cast<float>(center_pt.y - overlay_pt.y
	);
	}
	}
}
}
	if (cx <= 0.f || cy <= 0.f)
	{
		ImGuiIO& io = ImGui::GetIO();
		cx = io.DisplaySize.x * 0.5f;
		cy = io.DisplaySize.y * 0.5f;
	}
	ImVec2 center(cx, cy);
	if (fill_fov && fill_colour != nullptr)
	{
		ImU32 fill_col = IM_COL32(			static_cast<int>(fill_colour[0] * 255.f),			static_cast<int>(fill_colour[1] * 255.f),			static_cast<int>(fill_colour[2] * 255.f),			static_cast<int>(fill_colour[3] * 255.f)		);
		draw->AddCircleFilled(center, fov_radius, fill_col, 64);
	}
	ImU32 outline_col = IM_COL32(		static_cast<int>(outline_colour[0] * 255.f),		static_cast<int>(outline_colour[1] * 255.f),		static_cast<int>(outline_colour[2] * 255.f),		static_cast<int>(outline_colour[3] * 255.f)	);
	ImU32 circle_col = IM_COL32(		static_cast<int>(circle_colour[0] * 255.f),		static_cast<int>(circle_colour[1] * 255.f),		static_cast<int>(circle_colour[2] * 255.f),		static_cast<int>(circle_colour[3] * 255.f)	);
	draw->AddCircle(center, fov_radius, outline_col, 64, 2.5f);
	draw->AddCircle(center, fov_radius, circle_col, 64, 1.2f);
}
static void draw_keybind_indicator()
{
	if (!settings::misc::keybind_indicator)		return;
	ImGuiIO& io = ImGui::GetIO();
	float cx = io.DisplaySize.x * 0.5f;
	float cy = io.DisplaySize.y * 0.5f;
	struct bind_check_t {
		const char* display_name;
		bool enabled;
		int vk;
		int mode;
		const char* id;
	};
	bind_check_t binds[] = {
		{
 "Aim Assist",    settings::aimbot::enabled,                   settings::aimbot::keybind,                                   settings::aimbot::activation_mode,                              "aimbot" }
,		{
 "Triggerbot",    settings::triggerbot::enabled,               settings::triggerbot::keybind,                               settings::triggerbot::activation_mode,                          "triggerbot" }
,	};
	std::vector<std::pair<std::string, std::string>> active_bind_names;
	for (const auto& b : binds)
	{
		if (b.enabled && keybind::is_key_active(b.vk, b.mode, b.id))
		{
			const char* mstr = (b.mode == 0) ? " [toggle]" : (b.mode == 1) ? " [hold]" : " [always]";
			active_bind_names.push_back({
 std::string(b.display_name), std::string(mstr)
		}
	);
	}
	}
	if (active_bind_names.empty())		return;
	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list)		return;
	float start_offset_y = 22.0f;
	ImU32 feature_col = IM_COL32(		static_cast<int>(settings::misc::keybind_indicator_feature_colour[0] * 255.f),		static_cast<int>(settings::misc::keybind_indicator_feature_colour[1] * 255.f),		static_cast<int>(settings::misc::keybind_indicator_feature_colour[2] * 255.f),		static_cast<int>(settings::misc::keybind_indicator_feature_colour[3] * 255.f)	);
	ImU32 mode_col = IM_COL32(		static_cast<int>(settings::misc::keybind_indicator_mode_colour[0] * 255.f),		static_cast<int>(settings::misc::keybind_indicator_mode_colour[1] * 255.f),		static_cast<int>(settings::misc::keybind_indicator_mode_colour[2] * 255.f),		static_cast<int>(settings::misc::keybind_indicator_mode_colour[3] * 255.f)	);
	ImU32 shadow_col = IM_COL32(0, 0, 0, 220);
	float draw_y = cy + start_offset_y;
	float line_height = 16.0f;
	for (const auto& [feat, mode_tag] : active_bind_names)
	{
		std::string full = feat + mode_tag;
		ImVec2 full_size = ImGui::CalcTextSize(full.c_str());
		ImVec2 feat_size = ImGui::CalcTextSize(feat.c_str());
		float text_x = cx - full_size.x * 0.5f;
		ImVec2 bg_min(text_x - 6.0f, draw_y - 1.0f);
		ImVec2 bg_max(text_x + full_size.x + 6.0f, draw_y + line_height - 1.0f);
		draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(10, 10, 14, 160), 3.0f);
		draw_list->AddRect(bg_min, bg_max, IM_COL32(255, 255, 255, 25), 3.0f);
		draw_list->AddText(ImVec2(text_x - 1.0f, draw_y), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x + 1.0f, draw_y), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x, draw_y - 1.0f), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x, draw_y + 1.0f), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x, draw_y), feature_col, feat.c_str());
		float tag_x = text_x + feat_size.x;
		draw_list->AddText(ImVec2(tag_x - 1.0f, draw_y), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x + 1.0f, draw_y), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x, draw_y - 1.0f), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x, draw_y + 1.0f), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x, draw_y), mode_col, mode_tag.c_str());
		draw_y += line_height;
	}
}
static float get_watermark_cpu_usage()
{
	static FILETIME prev_sys_idle{
}
, prev_sys_kernel{
}
, prev_sys_user{ };
	static FILETIME prev_proc_creation{
}
, prev_proc_exit{
}
, prev_proc_kernel{
}
, prev_proc_user{ };
	static float last_cpu = 0.6f;
	static auto last_calc = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_calc).count() < 500)		return last_cpu;
	last_calc = now;
	FILETIME sys_idle{
}
, sys_kernel{
}
, sys_user{ };
	FILETIME proc_creation{
}
, proc_exit{
}
, proc_kernel{
}
, proc_user{ };
	if (!GetSystemTimes(&sys_idle, &sys_kernel, &sys_user) ||		!GetProcessTimes(GetCurrentProcess(), &proc_creation, &proc_exit, &proc_kernel, &proc_user))
	{
		return last_cpu;
	}
	auto ft_to_uint64 = [](const FILETIME& ft) -> uint64_t {
		return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	};
	uint64_t sys_k = ft_to_uint64(sys_kernel);
	uint64_t sys_u = ft_to_uint64(sys_user);
	uint64_t prev_sys_k = ft_to_uint64(prev_sys_kernel);
	uint64_t prev_sys_u = ft_to_uint64(prev_sys_user);
	uint64_t proc_k = ft_to_uint64(proc_kernel);
	uint64_t proc_u = ft_to_uint64(proc_user);
	uint64_t prev_proc_k = ft_to_uint64(prev_proc_kernel);
	uint64_t prev_proc_u = ft_to_uint64(prev_proc_user);
	uint64_t total_sys = (sys_k - prev_sys_k) + (sys_u - prev_sys_u);
	uint64_t total_proc = (proc_k - prev_proc_k) + (proc_u - prev_proc_u);
	prev_sys_idle = sys_idle;
	prev_sys_kernel = sys_kernel;
	prev_sys_user = sys_user;
	prev_proc_creation = proc_creation;
	prev_proc_exit = proc_exit;
	prev_proc_kernel = proc_kernel;
	prev_proc_user = proc_user;
	if (total_sys > 0)
	{
		last_cpu = (static_cast<float>(total_proc) / static_cast<float>(total_sys)) * 100.0f;
		if (last_cpu < 0.0f) last_cpu = 0.0f;
		if (last_cpu > 100.0f) last_cpu = 100.0f;
	}
	return last_cpu;
}
static size_t get_watermark_ram_mb(){
	PROCESS_MEMORY_COUNTERS_EX pmc{ };
	if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
	{
		return pmc.WorkingSetSize / (1024 * 1024);
	}
	return 82;
}
void RenderWatermark(const char* menu_name, float pos_x, float pos_y, ImU32 accent_color, ImU32 bg_color)
{
	render->render_watermark();
}
void render_t::render_topbar(){
}
void render_t::render_watermark(){
}
void RenderKeybindList(float pos_x, float pos_y, ImU32 accent_color, ImU32 bg_color)
{
	struct anim_state_t {
		float alpha = 0.0f;
		float current_y = 0.0f;
	};
	static std::map<std::string, anim_state_t> item_anims;
	static float no_active_alpha = 1.0f;
	static float current_box_height = 42.0f;
	static auto last_time = std::chrono::high_resolution_clock::now();
	auto now = std::chrono::high_resolution_clock::now();
	float dt = std::chrono::duration<float>(now - last_time).count();
	last_time = now;
	if (dt <= 0.0001f || dt > 0.1f) dt = 1.0f / 60.0f;
	struct bind_spec_t {
		const char* name;
		bool enabled;
		int vk;
		int mode;
		const char* id;
	};
	bind_spec_t specs[] = {
		{
 "Aim Assist",    settings::aimbot::enabled,                   settings::aimbot::keybind,                                   settings::aimbot::activation_mode,                              "aimbot" }
,		{
 "Triggerbot",    settings::triggerbot::enabled,               settings::triggerbot::keybind,                               settings::triggerbot::activation_mode,                          "triggerbot" }
,	};
	struct render_entry_t {
		std::string name;
		std::string mode_str;
		float alpha;
		float render_y;
		bool is_active;
	};
	std::vector<render_entry_t> draw_entries;
	int active_count = 0;
	float slot_index = 0.0f;
	float line_height = 18.0f;
	for (const auto& spec : specs)
	{
		bool is_act = spec.enabled && keybind::is_key_active(spec.vk, spec.mode, spec.id);
		auto& anim = item_anims[spec.id];
		if (is_act)
		{
			active_count++;
			float target_y = slot_index * line_height;
			slot_index += 1.0f;
			anim.alpha += (1.0f - anim.alpha) * (1.0f - std::exp(-18.0f * dt));
			anim.current_y += (target_y - anim.current_y) * (1.0f - std::exp(-16.0f * dt));
		}
		else		{
			anim.alpha += (0.0f - anim.alpha) * (1.0f - std::exp(-14.0f * dt));
		}
		if (anim.alpha > 0.005f)
		{
			const char* mstr = (spec.mode == 0) ? "[toggle]" : (spec.mode == 1) ? "[hold]" : "[always]";
			draw_entries.push_back({
 spec.name, mstr, anim.alpha, anim.current_y, true
	}
	);
}
	}
	float no_active_target = (active_count == 0 && draw_entries.empty()) ? 1.0f : 0.0f;
	no_active_alpha += (no_active_target - no_active_alpha) * (1.0f - std::exp(-14.0f * dt));
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	if (!dl) return;
	float width = 175.0f;
	float header_height = 22.0f;
	float padding = 6.0f;
	float target_content_height = (active_count == 0) ? line_height : (active_count * line_height);
	float target_box_height = header_height + target_content_height + padding;
	current_box_height += (target_box_height - current_box_height) * (1.0f - std::exp(-18.0f * dt));
	ImGuiIO& io = ImGui::GetIO();
	if ((pos_x < 0.0f || pos_y < 0.0f) && io.DisplaySize.x > 100.0f)
	{
		pos_x = 15.0f;
		pos_y = (io.DisplaySize.y - current_box_height) * 0.5f;
		settings::misc::keybind_list_pos_x = pos_x;
		settings::misc::keybind_list_pos_y = pos_y;
	}
	ImVec2 wmin = ImVec2(pos_x, pos_y);
	ImVec2 wmax = ImVec2(pos_x + width, pos_y + current_box_height);
	POINT mouse_pt{ };
	GetCursorPos(&mouse_pt);
	POINT overlay_pt{
 0, 0 };
	if (render && render->detail && render->detail->window)		ClientToScreen(render->detail->window, &overlay_pt);
	ImVec2 mouse_pos = ImVec2(static_cast<float>(mouse_pt.x - overlay_pt.x), static_cast<float>(mouse_pt.y - overlay_pt.y));
	bool hovered = (mouse_pos.x >= pos_x && mouse_pos.x <= pos_x + width && mouse_pos.y >= pos_y && mouse_pos.y <= pos_y + current_box_height);
	if (hovered)
	{
		if (orok_config.custom_cursor)			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		else			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}
	bool lbutton_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	static bool prev_lbutton_down = false;
	static bool is_dragging = false;
	static ImVec2 drag_offset = ImVec2(0, 0);
	if (hovered && lbutton_down && !prev_lbutton_down)
	{
		is_dragging = true;
		drag_offset = ImVec2(mouse_pos.x - pos_x, mouse_pos.y - pos_y);
	}
	prev_lbutton_down = lbutton_down;
	if (is_dragging)
	{
		if (lbutton_down)
		{
			pos_x = mouse_pos.x - drag_offset.x;
			pos_y = mouse_pos.y - drag_offset.y;
			if (pos_x < 0.0f) pos_x = 0.0f;
			if (pos_y < 0.0f) pos_y = 0.0f;
			if (pos_x + width > io.DisplaySize.x) pos_x = io.DisplaySize.x - width;
			if (pos_y + current_box_height > io.DisplaySize.y) pos_y = io.DisplaySize.y - current_box_height;
			settings::misc::keybind_list_pos_x = pos_x;
			settings::misc::keybind_list_pos_y = pos_y;
			wmin = ImVec2(pos_x, pos_y);
			wmax = ImVec2(pos_x + width, pos_y + current_box_height);
		}
		else		{
			is_dragging = false;
		}
	}
	g_keybind_list_is_dragging = is_dragging;
	ImU32 current_accent = accent_color;
	ImU32 title_color = IM_COL32(255, 255, 255, 255);
	ImU32 border_col = IM_COL32(		static_cast<int>(orok_config.menu_color_border[0] * 255.f),		static_cast<int>(orok_config.menu_color_border[1] * 255.f),		static_cast<int>(orok_config.menu_color_border[2] * 255.f),		static_cast<int>(orok_config.menu_color_border[3] * 255.f)	);
	dl->AddRect(ImVec2(wmin.x - 3, wmin.y - 3), ImVec2(wmax.x + 3, wmax.y + 3), IM_COL32(0, 0, 0, 255));
	dl->AddRect(ImVec2(wmin.x - 2, wmin.y - 2), ImVec2(wmax.x + 2, wmax.y + 2), border_col);
	dl->AddRect(ImVec2(wmin.x - 1, wmin.y - 1), ImVec2(wmax.x + 1, wmax.y + 1), IM_COL32(0, 0, 0, 255));
	dl->AddRectFilled(wmin, wmax, bg_color);
	dl->AddLine(ImVec2(wmin.x, wmin.y), ImVec2(wmax.x, wmin.y), current_accent);
	const char* title = "Keybinds";
	ImVec2 title_size = ImGui::CalcTextSize(title);
	ImVec2 title_pos = ImVec2(pos_x + (width - title_size.x) * 0.5f, pos_y + 3.0f);
	dl->AddText(title_pos, title_color, title);
	//  line
	dl->AddLine(ImVec2(pos_x + 2, pos_y + header_height), ImVec2(pos_x + width - 2, pos_y + header_height), IM_COL32(60, 60, 65, 255));
	dl->PushClipRect(wmin, wmax, true);
	float base_y = pos_y + header_height + 2.0f;
	if (no_active_alpha > 0.005f)
	{
		const char* empty_str = "No active keybinds";
		ImU32 empty_col = IM_COL32(150, 150, 150, static_cast<int>(255.0f * no_active_alpha));
		dl->AddText(ImVec2(pos_x + 8.0f, base_y), empty_col, empty_str);
	}
	for (const auto& item : draw_entries)
	{
		float item_y = base_y + item.render_y;
		float slide_x = (1.0f - item.alpha) * -6.0f;
		ImU32 name_col = item.is_active			? IM_COL32(255, 255, 255, static_cast<int>(255.0f * item.alpha))			: IM_COL32(110, 110, 115, static_cast<int>(200.0f * item.alpha));
		ImU32 mode_col = item.is_active			? IM_COL32(180, 180, 190, static_cast<int>(255.0f * item.alpha))			: IM_COL32(70, 70, 75,   static_cast<int>(180.0f * item.alpha));
		dl->AddText(ImVec2(pos_x + 8.0f + slide_x, item_y), name_col, item.name.c_str());
		ImVec2 val_size = ImGui::CalcTextSize(item.mode_str.c_str());
		dl->AddText(ImVec2(pos_x + width - val_size.x - 8.0f - slide_x, item_y), mode_col, item.mode_str.c_str());
	}
	dl->PopClipRect();
}
void render_t::render_keybind_list(){
	if (!settings::misc::keybind_list)		return;
	ImU32 accent_col = IM_COL32(		static_cast<int>(orok_config.menu_color[0] * 255.f),		static_cast<int>(orok_config.menu_color[1] * 255.f),		static_cast<int>(orok_config.menu_color[2] * 255.f),		static_cast<int>(orok_config.menu_color[3] * 255.f)	);
	ImU32 bg_col = IM_COL32(		static_cast<int>(orok_config.menu_color_secondary[0] * 255.f),		static_cast<int>(orok_config.menu_color_secondary[1] * 255.f),		static_cast<int>(orok_config.menu_color_secondary[2] * 255.f),		static_cast<int>(orok_config.menu_color_secondary[3] * 255.f)	);
	RenderKeybindList(settings::misc::keybind_list_pos_x, settings::misc::keybind_list_pos_y, accent_col, bg_col
	);
}
static std::string get_short_key_name(int vk_code)
{
	if (vk_code == VK_XBUTTON1) return "xb1";
	if (vk_code == VK_XBUTTON2) return "xb2";
	if (vk_code == VK_LBUTTON) return "mb1";
	if (vk_code == VK_RBUTTON) return "mb2";
	if (vk_code == VK_MBUTTON) return "mb3";
	if (vk_code == VK_SHIFT) return "shift";
	if (vk_code == VK_CONTROL) return "ctrl";
	if (vk_code == VK_MENU) return "alt";
	if (vk_code == VK_CAPITAL) return "caps";
	if (vk_code == VK_SPACE) return "space";
	if (vk_code == VK_TAB) return "tab";
	if (vk_code == VK_INSERT) return "ins";
	if (vk_code == VK_DELETE) return "del";
	if (vk_code == VK_ESCAPE) return "esc";
	std::string name = keybind::get_key_name(vk_code);
	std::transform(name.begin(), name.end(), name.begin(), ::tolower);
	return name;
}
static float get_fov_scale()
{
	uint64_t cam = 0;
	if (game::datamodel && game::datamodel->address >= 0x10000)
	{
		uint64_t ws = memory->read<uint64_t>(game::datamodel->address + Offsets::DataModel::Workspace);
		if (ws >= 0x10000)
		{
			cam = memory->read<uint64_t>(ws + Offsets::Workspace::CurrentCamera);
		}
	}
	if (cam < 0x10000)
	{
		cam = game::camera;
	}
	if (cam < 0x10000)
		return 1.0f;
	float cur = memory->read<float>(cam + Offsets::Camera::FieldOfView);
	if (cur <= 0.001f || std::isnan(cur) || std::isinf(cur))
		return 1.0f;
	float cur_deg = (cur < 3.2f) ? (cur * (180.0f / 3.14159265358979323846f)) : cur;
	if (cur_deg <= 1.0f || cur_deg > 170.0f)
		return 1.0f;
	float scale = 70.0f / cur_deg;
	if (scale < 0.1f) scale = 0.1f;
	if (scale > 10.0f) scale = 10.0f;
	return scale;
}
void render_t::render_visuals()
{
	if (!loader::is_loaded)
		return;
	// irl mode: no esp, no fov, no indicators - features keep running silently
	if (settings::general::irl_mode)
		return;
	rbx::g_in_render_context = true;
	esp::run();
	if (settings::aimbot::draw_fov)
	{
		if (settings::aimbot::ring_fov_enabled)
		{
			float inner = settings::aimbot::ring_fov_inner;
			float outer = settings::aimbot::ring_fov_outer;
			if (settings::aimbot::dynamic_fov)
			{
				float scale = get_fov_scale();
				inner *= scale;
				outer *= scale;
			}
			draw_fov_circle(inner, settings::aimbot::fov_circle_colour, settings::aimbot::fov_outline_colour, settings::aimbot::fill_fov, settings::aimbot::fov_fill_colour, true);
			draw_fov_circle(outer, settings::aimbot::fov_circle_colour, settings::aimbot::fov_outline_colour, settings::aimbot::fill_fov, settings::aimbot::fov_fill_colour, true);
		}
		else
		{
			float fov_radius = settings::aimbot::fov;
			if (settings::aimbot::dynamic_fov)
			{
				fov_radius *= get_fov_scale();
			}
			draw_fov_circle(fov_radius, settings::aimbot::fov_circle_colour, settings::aimbot::fov_outline_colour, settings::aimbot::fill_fov, settings::aimbot::fov_fill_colour, true);
		}
	}
	if (settings::triggerbot::draw_fov)
	{
		draw_fov_circle(settings::triggerbot::threshold, settings::triggerbot::fov_circle_colour, settings::triggerbot::fov_outline_colour, settings::triggerbot::fill_fov, settings::triggerbot::fov_fill_colour, true);
	}
	draw_keybind_indicator();
	notifications::render();
	rbx::g_in_render_context = false;
}
