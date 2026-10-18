//============ Copyright ImMagic, All rights reserved ============//
//
// Purpose: macOS-style card menu - ported from the ImGuiMenu reference
//          design (sidebar + section cards + right-aligned controls)
//
//================================================================//

#include "Menu.h"

// Dear ImGui
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

// Dear ImGui - Backends
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

// Dear ImGui - Addons
#include "imgui/addons/imgui_notification.h"
#include "imgui/addons/imgui_addons.h"

#include <core/system/eject.h>
#include <features/system/keybind/keybind.h>

extern float g_AnimationSpeed;

// Dear ImGui - Misc
#include "imgui/misc/imgui_freetype.h"

#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <unordered_map>
#include <features/system/settings/settings.h>
#include <features/system/config/config.h>
#include <features/system/playerlist/playerlist.h>
#include <features/visuals/geometry/wallcheck.h>
#include <ui/menu/settings/variables.h>
#include <ui/menu/settings/colors.h>
#include <ui/menu/style.h>
#include <ui/render/render.h>

#include "assets/nav_icons.h"
#include "assets/nav_bytes.h"
#include "assets/font_main.h"

#define PROJECT_NAME    "Ivory"

// ============================================================================
// LazyUI visual assets (soft glows, gradients, shadows) - embedded PNGs
// ============================================================================
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "assets/lazyui_assets.h"

namespace lazyui
{
	inline ID3D11ShaderResourceView* tex_module_gradient = nullptr;
	inline ID3D11ShaderResourceView* tex_module_gradient_2 = nullptr;
	inline ID3D11ShaderResourceView* tex_glow = nullptr;
	inline ID3D11ShaderResourceView* tex_glow_2 = nullptr;
	inline ID3D11ShaderResourceView* tex_knob_shadow = nullptr;
	inline ID3D11ShaderResourceView* tex_dot_shadow = nullptr;
	inline ID3D11ShaderResourceView* tex_sidebar_border = nullptr;
	inline ID3D11ShaderResourceView* tex_avatar = nullptr;
	inline ID3D11ShaderResourceView* tex_vector_shadow = nullptr;
	inline bool loaded = false;

	inline ID3D11ShaderResourceView* LoadPngSrv(ID3D11Device* device, const unsigned char* data, unsigned int size)
	{
		if (!device || !data)
			return nullptr;
		int w = 0, h = 0, channels = 0;
		unsigned char* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
		if (!pixels)
			return nullptr;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = (UINT)w;
		desc.Height = (UINT)h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA init{ pixels, w * 4, 0 };
		ID3D11Texture2D* tex = nullptr;
		device->CreateTexture2D(&desc, &init, &tex);
		stbi_image_free(pixels);
		if (!tex)
			return nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		device->CreateShaderResourceView(tex, nullptr, &srv);
		tex->Release();
		return srv;
	}

	inline void LoadAll(ID3D11Device* device)
	{
		if (loaded || !device)
			return;
		tex_module_gradient   = LoadPngSrv(device, lazyui_module_gradient_data, lazyui_module_gradient_size);
		tex_module_gradient_2 = LoadPngSrv(device, lazyui_module_gradient_2_data, lazyui_module_gradient_2_size);
		tex_glow              = LoadPngSrv(device, lazyui_glow_blur_data, lazyui_glow_blur_size);
		tex_glow_2            = LoadPngSrv(device, lazyui_glow_blur_2_data, lazyui_glow_blur_2_size);
		tex_knob_shadow       = LoadPngSrv(device, lazyui_knob_shadow_data, lazyui_knob_shadow_size);
		tex_dot_shadow        = LoadPngSrv(device, lazyui_dot_shadow_data, lazyui_dot_shadow_size);
		tex_sidebar_border    = LoadPngSrv(device, lazyui_sidebar_border_data, lazyui_sidebar_border_size);
		tex_avatar            = LoadPngSrv(device, lazyui_avatar_24x24_data, lazyui_avatar_24x24_size);
		tex_vector_shadow     = LoadPngSrv(device, lazyui_vector_shadow_data, lazyui_vector_shadow_size);
		loaded = true;
		bootlog("lazyui: visual assets loaded");
	}
}

// ============================================================================
// Palette access + color helpers
// ============================================================================
static style::Palette& P() { return style::P; }

inline ImU32 LerpCol(ImU32 a, ImU32 b, float t)
{
	ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
	return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t,
		x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

// ============================================================================
// Card / row widgets (ported from the reference design)
// ============================================================================
static constexpr float ROW_H = 37.f;
static constexpr float PAD = 14.f;
static std::string g_filter;
static std::string g_status_line;

static bool FilterMatch(const char* title)
{
	if (g_filter.empty()) return true;
	std::string t = title;
	std::transform(t.begin(), t.end(), t.begin(), ::tolower);
	return t.find(g_filter) != std::string::npos;
}

inline float Anim(ImGuiID id, bool target, float speed = 14.f)
{
	ImGuiStorage* st = ImGui::GetStateStorage();
	float v = st->GetFloat(id, target ? 1.f : 0.f);
	v += ((target ? 1.f : 0.f) - v) * ImMin(1.f, ImGui::GetIO().DeltaTime * speed);
	st->SetFloat(id, v);
	return v;
}

inline void Chevron(ImDrawList* dl, ImVec2 c, bool up, ImU32 col)
{
	float d = up ? 1.f : -1.f;
	ImVec2 p[3] = { { c.x - 3.f, c.y + 1.5f * d }, { c.x, c.y - 1.5f * d }, { c.x + 3.f, c.y + 1.5f * d } };
	dl->AddPolyline(p, 3, col, ImDrawFlags_None, 1.5f);
}

// section label + card with hairline-separated rows
struct Card { ImDrawListSplitter sp; ImVec2 p0; float w; bool skipped; };
struct RowCtx { ImRect r; float x; ImDrawList* dl; float cy; };

static bool CardBegin(Card& c, const char* title)
{
	c.skipped = !FilterMatch(title);
	if (c.skipped)
		return false;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	dl->AddText(ImGui::GetFont(), 13.f, ImVec2(p.x + 4.f, p.y + 1.f), P().dim, title);
	ImGui::Dummy(ImVec2(1.f, 23.f));
	c.p0 = ImGui::GetCursorScreenPos();
	c.w = ImGui::GetContentRegionAvail().x;
	c.sp.Split(dl, 2);
	c.sp.SetCurrentChannel(dl, 1);
	return true;
}

static void CardEnd(Card& c, const char* footnote = nullptr)
{
	if (c.skipped)
		return;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p1(c.p0.x + c.w, ImGui::GetCursorScreenPos().y);
	c.sp.SetCurrentChannel(dl, 0);
	// gradient card background (LazyUI asset, tinted, slightly translucent) + border
	const ImU32 card_bg = (P().card_bg & 0x00FFFFFFu) | 0xE8u << 24;
	if (lazyui::tex_module_gradient)
		dl->AddImage((ImTextureID)lazyui::tex_module_gradient, c.p0, p1, ImVec2(0, 0), ImVec2(1, 1), card_bg);
	else
		dl->AddRectFilled(c.p0, p1, card_bg, 12.f);
	dl->AddRect(c.p0, p1, P().card_brd, 12.f);
	c.sp.Merge(dl);
	if (footnote)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		dl->AddText(ImGui::GetFont(), 14.f, ImVec2(p.x + 14.f, p.y + 10.f), P().dim, footnote);
		ImGui::Dummy(ImVec2(1.f, 30.f));
	}
	ImGui::Dummy(ImVec2(1.f, 16.f));
}

// standalone section header (groups several cards under one label)
static void SectionLabel(const char* label)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	dl->AddText(ImGui::GetFont(), 13.f, ImVec2(p.x + 4.f, p.y + 1.f), P().dim, label);
	ImGui::Dummy(ImVec2(1.f, 23.f));
}

static RowCtx Row(const char* label, bool first = false)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	RowCtx c;
	c.r = ImRect(p, ImVec2(p.x + w, p.y + ROW_H));
	c.x = c.r.Max.x - PAD;
	c.dl = dl;
	c.cy = p.y + ROW_H * 0.5f;
	if (!first) dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), IM_COL32(255, 255, 255, 18));
	ImVec2 ts = ImGui::CalcTextSize(label);
	dl->AddText(ImVec2(p.x + PAD, c.cy - ts.y * 0.5f), P().text, label);
	ImGui::Dummy(ImVec2(w, ROW_H));
	return c;
}

static bool Switch(RowCtx& c, bool* v)
{
	const float W = 44.f, H = 26.f;
	ImVec2 p0(c.x - W, c.cy - H * 0.5f), save = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(p0);
	ImGui::PushID(v);
	const bool hit = ImGui::InvisibleButton("sw", ImVec2(W, H));
	if (hit) *v = !*v;
	const float t = Anim(ImGui::GetItemID(), *v);
	ImGui::PopID();
	c.dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), LerpCol(IM_COL32(72, 72, 76, 255), P().toggle_on, t), H * 0.5f);
	const ImVec2 k(p0.x + H * 0.5f + (W - H) * t, c.cy);
	if (lazyui::tex_knob_shadow)
		c.dl->AddImage((ImTextureID)lazyui::tex_knob_shadow, ImVec2(k.x - 11.f, k.y - 11.f), ImVec2(k.x + 11.f, k.y + 11.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 90));
	c.dl->AddCircleFilled(ImVec2(k.x, k.y + 1.f), H * 0.5f - 1.f, IM_COL32(0, 0, 0, 50), 24);
	c.dl->AddCircleFilled(k, H * 0.5f - 2.f, IM_COL32_WHITE, 24);
	ImGui::SetCursorScreenPos(save);
	c.x -= W + 10.f;
	return hit;
}

static void ColorDot(RowCtx& c, float* col)
{
	const float D = 24.f;
	ImVec2 ctr(c.x - D * 0.5f, c.cy), save = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(ImVec2(ctr.x - D * 0.5f, ctr.y - D * 0.5f));
	ImGui::PushID(col);
	if (ImGui::InvisibleButton("dot", ImVec2(D, D)))
		ImGui::OpenPopup("cp");
	const ImU32 u = ImGui::ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 1.f));
	if (lazyui::tex_dot_shadow)
		c.dl->AddImage((ImTextureID)lazyui::tex_dot_shadow, ImVec2(ctr.x - 12.f, ctr.y - 11.f), ImVec2(ctr.x + 12.f, ctr.y + 11.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 90));
	c.dl->AddCircleFilled(ctr, 11.f, u, 24);
	c.dl->AddCircle(ctr, 11.f, IM_COL32(255, 255, 255, 50), 24, 1.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
	const bool o = ImGui::BeginPopup("cp");
	ImGui::PopStyleVar();
	if (o)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
		ImGui::ColorPicker3("##p", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
		ImGui::PopStyleVar();
		ImGui::EndPopup();
	}
	ImGui::PopID();
	ImGui::SetCursorScreenPos(save);
	c.x -= D + 10.f;
}

static bool SliderCtl(RowCtx& c, float* v, float mn, float mx, const char* fmt, bool integer = false)
{
	const float TW = 40.f, TRACK = 96.f, KR = 9.f;
	char buf[16];
	snprintf(buf, sizeof buf, fmt, *v);
	const ImVec2 ts = ImGui::CalcTextSize(buf), save = ImGui::GetCursorScreenPos();
	c.dl->AddText(ImVec2(c.x - ts.x, c.cy - ts.y * 0.5f), P().dim, buf);
	const float tx1 = c.x - TW, tx0 = tx1 - TRACK;
	ImGui::SetCursorScreenPos(ImVec2(tx0 - KR, c.cy - KR - 2.f));
	ImGui::PushID(v);
	ImGui::InvisibleButton("sl", ImVec2(TRACK + KR * 2.f, KR * 2.f + 4.f));
	bool changed = false;
	if (ImGui::IsItemActive())
	{
		float t = ImClamp((ImGui::GetIO().MousePos.x - tx0) / TRACK, 0.f, 1.f);
		float nv = mn + (mx - mn) * t;
		if (integer) nv = roundf(nv);
		if (nv != *v) { *v = nv; changed = true; }
	}
	ImGui::PopID();
	const float t = ImClamp((*v - mn) / (mx - mn), 0.f, 1.f), kx = tx0 + TRACK * t;
	c.dl->AddRectFilled(ImVec2(tx0, c.cy - 2.f), ImVec2(tx1, c.cy + 2.f), IM_COL32(255, 255, 255, 40), 2.f);
	c.dl->AddRectFilled(ImVec2(tx0, c.cy - 2.f), ImVec2(kx, c.cy + 2.f), P().accent, 2.f);
	c.dl->AddCircleFilled(ImVec2(kx, c.cy + 1.f), KR, IM_COL32(0, 0, 0, 60), 24);
	c.dl->AddCircleFilled(ImVec2(kx, c.cy), KR, IM_COL32_WHITE, 24);
	ImGui::SetCursorScreenPos(save);
	c.x -= TW + TRACK + 12.f;
	return changed;
}

static bool Segmented(RowCtx& c, const char* const* items, int n, int* sel)
{
	const float H = 30.f, PX = 12.f;
	float w[16], total = 0.f;
	for (int i = 0; i < n; i++) { w[i] = ImGui::CalcTextSize(items[i]).x + PX * 2.f; total += w[i]; }
	const ImVec2 p0(c.x - total - 4.f, c.cy - H * 0.5f), save = ImGui::GetCursorScreenPos();
	c.dl->AddRectFilled(p0, ImVec2(p0.x + total + 4.f, p0.y + H), IM_COL32(0, 0, 0, 70), 8.f);
	bool changed = false;
	float cur = p0.x + 2.f;
	ImGui::PushID(sel);
	for (int i = 0; i < n; i++)
	{
		const ImVec2 a(cur, p0.y + 2.f), b(cur + w[i], p0.y + H - 2.f);
		ImGui::SetCursorScreenPos(a);
		ImGui::PushID(i);
		if (ImGui::InvisibleButton("seg", ImVec2(b.x - a.x, b.y - a.y))) { *sel = i; changed = true; }
		const float t = Anim(ImGui::GetItemID(), *sel == i);
		ImGui::PopID();
		c.dl->AddRectFilled(a, b, LerpCol(IM_COL32(120, 120, 128, 0), IM_COL32(120, 120, 128, 210), t), 6.f);
		const ImVec2 ts = ImGui::CalcTextSize(items[i]);
		c.dl->AddText(ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, c.cy - ts.y * 0.5f), P().text, items[i]);
		cur += w[i];
	}
	ImGui::PopID();
	ImGui::SetCursorScreenPos(save);
	c.x -= total + 4.f + 10.f;
	return changed;
}

static bool ComboCtl(RowCtx& c, const char* const* items, int n, int* sel, float W = 120.f)
{
	// saved configs can carry out-of-range indices (e.g. old theme_index=30)
	// - clamp or items[*sel] reads past the array and crashes CalcTextSize
	if (*sel < 0 || *sel >= n)
		*sel = 0;

	const float H = 28.f;
	const ImVec2 p0(c.x - W, c.cy - H * 0.5f), p1(c.x, c.cy + H * 0.5f), save = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(p0);
	ImGui::PushID(sel);
	if (ImGui::InvisibleButton("cb", ImVec2(W, H)))
		ImGui::OpenPopup("dd");
	c.dl->AddRectFilled(p0, p1, P().field, 8.f);
	c.dl->AddRect(p0, p1, P().card_brd, 8.f);
	const ImVec2 ts = ImGui::CalcTextSize(items[*sel]);
	c.dl->AddText(ImVec2(p0.x + 12.f, c.cy - ts.y * 0.5f), P().text, items[*sel]);
	const ImVec2 b0(p1.x - 24.f, p0.y + 3.f), b1(p1.x - 3.f, p1.y - 3.f);
	c.dl->AddRectFilled(b0, b1, P().accent, 6.f);
	const ImVec2 m((b0.x + b1.x) * 0.5f, c.cy);
	Chevron(c.dl, ImVec2(m.x, m.y - 4.f), true, IM_COL32_WHITE);
	Chevron(c.dl, ImVec2(m.x, m.y + 4.f), false, IM_COL32_WHITE);

	bool changed = false;
	ImGui::SetNextWindowPos(ImVec2(p0.x, p1.y + 4.f));
	ImGui::SetNextWindowSize(ImVec2(W, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(P().card_bg));
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(P().accent_a));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(style::Lerp(P().accent_a, P().accent, 0.45f)));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(P().accent_a));
	const bool o = ImGui::BeginPopup("dd");
	ImGui::PopStyleColor(4);
	if (o)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
		for (int i = 0; i < n; i++)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(i == *sel ? P().accent : P().text));
			if (ImGui::Selectable(items[i], i == *sel)) { *sel = i; changed = true; }
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleVar();
		ImGui::EndPopup();
	}
	ImGui::PopID();
	ImGui::SetCursorScreenPos(save);
	c.x -= W + 10.f;
	return changed;
}

static bool Stepper(RowCtx& c, int* v, int mn, int mx)
{
	const float W = 26.f, H = 32.f;
	const ImVec2 p0(c.x - W, c.cy - H * 0.5f), p1(c.x, c.cy + H * 0.5f), save = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(p0);
	ImGui::PushID(v);
	const bool hit = ImGui::InvisibleButton("st", ImVec2(W, H));
	bool changed = false;
	if (hit)
	{
		int nv = ImGui::GetIO().MousePos.y < c.cy ? *v + 1 : *v - 1;
		nv = ImClamp(nv, mn, mx);
		if (nv != *v) { *v = nv; changed = true; }
	}
	ImGui::PopID();
	c.dl->AddRectFilled(p0, p1, P().field, 8.f);
	c.dl->AddRect(p0, p1, P().card_brd, 8.f);
	Chevron(c.dl, ImVec2(c.x - W * 0.5f, c.cy - 7.f), true, P().text);
	Chevron(c.dl, ImVec2(c.x - W * 0.5f, c.cy + 7.f), false, P().text);
	char buf[8];
	snprintf(buf, sizeof buf, "%d", *v);
	const ImVec2 ts = ImGui::CalcTextSize(buf);
	c.dl->AddText(ImVec2(p0.x - 14.f - ts.x, c.cy - ts.y * 0.5f), P().dim, buf);
	ImGui::SetCursorScreenPos(save);
	c.x -= W + 40.f;
	return changed;
}

// VK keybind capture - click, then press any key/mouse button
static bool KeyBindVK(RowCtx& c, int* key)
{
	static int* waiting = nullptr;
	static bool s_prev[256] = {};

	std::string nm = (waiting == key) ? std::string("...") : keybind::get_key_name(*key);
	const float H = 28.f;
	float W = ImMax(90.f, ImGui::CalcTextSize(nm.c_str()).x + 28.f);
	const ImVec2 p0(c.x - W, c.cy - H * 0.5f), p1(c.x, c.cy + H * 0.5f), save = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(p0);
	ImGui::PushID(key);
	ImGui::InvisibleButton("kb", ImVec2(W, H));
	if (ImGui::IsItemClicked(0))
		waiting = key;
	const float hov = Anim(ImGui::GetItemID(), ImGui::IsItemHovered(), 10.0f);
	ImGui::PopID();
	// match the combo styling exactly: same fill, border, hover brightening
	c.dl->AddRectFilled(p0, p1, LerpCol(P().field, P().card_brd, hov * 0.6f), 8.f);
	c.dl->AddRect(p0, p1, waiting == key ? P().accent : P().card_brd, 8.f);
	const ImVec2 ts = ImGui::CalcTextSize(nm.c_str());
	c.dl->AddText(ImVec2(p0.x + (W - ts.x) * 0.5f, c.cy - ts.y * 0.5f), P().text, nm.c_str());

	bool changed = false;
	if (waiting == key)
	{
		for (int vk = 1; vk < 255; vk++)
		{
			if (vk == VK_ESCAPE || vk == VK_F1)
				continue;
			const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
			if (down && !s_prev[vk])
			{
				*key = vk;
				changed = true;
				waiting = nullptr;
				break;
			}
			s_prev[vk] = down;
		}
	}
	ImGui::SetCursorScreenPos(save);
	c.x -= W + 10.f;
	return changed;
}

// ============================================================================
// Theme application
// ============================================================================
void Menu::ApplyTheme(int index)
{
	style::SetTheme(index);
	// normalize the saved index so an out-of-range config value (e.g. 30)
	// stops re-triggering on every frame
	settings::misc::theme_index = style::theme_index;
}

// ============================================================================
// Init / shutdown / plumbing
// ============================================================================
bool Menu::Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
	bool result = true;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiStyle& style = ImGui::GetStyle();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	io.IniFilename = nullptr;   // Disable INI File

	ImGui::StyleColorsDark();

	style.PopupRounding = 10.f;
	style.PopupBorderSize = 1.f;
	style.FrameRounding = 8.f;
	style.WindowRounding = 14.f;
	style.ChildRounding = 6.f;
	style.GrabRounding = 4.f;
	style.ScrollbarRounding = 6.f;
	style.TabRounding = 6.f;

	style.WindowBorderSize = 1;
	style.FrameBorderSize = 1;
	style.PopupBorderSize = 1;

	style.WindowPadding = ImVec2(6, 6);
	style.ChildPadding = ImVec2(6, 6);
	style.FramePadding = ImVec2(5.0f, 4.0f);
	style.CellPadding = ImVec2(1.5f, 1.5f);
	style.ItemSpacing = ImVec2(4, 4);
	style.ItemInnerSpacing = ImVec2(4, 3);
	style.WindowMinSize = ImVec2(0, 0);

	style.ScrollbarSize = 10.0f;

	// Setup Font - Tahoma, antialiased for clean rendering
	ImFontConfig font_cfg_main;
	font_cfg_main.FontLoaderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
	font_cfg_main.GlyphOffset = ImVec2(0, 0);
	if (!io.Fonts->AddFontFromMemoryTTF(tahoma_hex, sizeof(tahoma_hex), 15.f, &font_cfg_main, io.Fonts->GetGlyphRangesCyrillic()))
		io.Fonts->AddFontDefault(&font_cfg_main); // fallback if tahoma not found
	ImFontConfig font_cfg_icons;
	font_cfg_icons.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_ForceAutoHint;
	font_cfg_icons.GlyphOffset = ImVec2(0, 0);
	font_cfg_icons.SizePixels = 16.0f;
	font_cfg_icons.FontDataOwnedByAtlas = false;
	static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	Menu::m_pIconFont = io.Fonts->AddFontFromMemoryCompressedTTF(fa6_solid_compressed_data, fa6_solid_compressed_size, 14.0f, &font_cfg_icons, icon_ranges);

	// Initialize Dear ImGui - WIN32
	result = ImGui_ImplWin32_Init(hWnd);
	if (!result) return false;

	// Initialize Dear ImGui - DX11
	result = ImGui_ImplDX11_Init(pDevice, pDeviceContext);
	if (!result) return false;

	ApplyTheme(std::clamp(settings::misc::theme_index, 0, style::theme_count - 1));
	Menu::m_bInitialized = true;

	return true;
}

void Menu::Render()
{
	DrawAll(true);
}

void Menu::DrawAll(bool menu_open)
{
	static bool s_last_menu_open = false;
	if (menu_open && !s_last_menu_open)
	{
		m_bShowMenu = true;
	}
	s_last_menu_open = menu_open;

	if (menu_open)
	{
		if (m_bShowMenu)
			DrawMenu();
	}

	if (m_bShowWatermark)
		DrawWatermark();

	wallcheck::render();

	ImNotify::Update();
	ImNotify::Render();
}

void Menu::DrawWatermark()
{
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	char watermark_text[128];
	snprintf(watermark_text, sizeof(watermark_text), "%d FPS", (int)io.Framerate);

	const float dot = 8.0f;
	const float wm_width = ImGui::CalcTextSize(watermark_text).x + dot + 24.0f;
	const float wm_height = ImGui::GetTextLineHeight() + 14.0f;

	ImGui::SetNextWindowSize(ImVec2(wm_width, wm_height), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - wm_width - 10.0f, 10.0f), ImGuiCond_Always);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	const bool main_window = ImGui::Begin("ImMagic - Watermark", (bool*)0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize);
	ImGui::PopStyleVar(2);

	if (main_window)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 wp = ImGui::GetWindowPos();
		const ImVec2 ws = ImGui::GetWindowSize();
		dl->AddRectFilled(wp, wp + ws, P().card_bg, 9.0f);
		dl->AddRect(wp, wp + ws, P().card_brd, 9.0f);

		const float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 2.2f);
		dl->AddCircleFilled(ImVec2(wp.x + 14.0f, wp.y + ws.y * 0.5f), 3.5f + pulse * 1.0f, P().ok);
		dl->AddText(ImVec2(wp.x + 24.0f, wp.y + (ws.y - ImGui::GetTextLineHeight()) * 0.5f), P().text, watermark_text);
	}
	ImGui::End();
}

// ============================================================================
// Sidebar / background / chrome (ported)
// ============================================================================
static int g_tab_index = 0;
static char g_search[96] = "";

// hand-drawn nav icons (reference design)
static void NavIcon(ImDrawList* dl, ImVec2 c, int id, ImU32 bg)
{
	const float h = 11.f;
	const ImU32 w = IM_COL32_WHITE;
	dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), bg, 6.f);
	switch (id)
	{
	case 0: // aim
		dl->AddCircle(c, 5.5f, w, 0, 1.6f);
		dl->AddCircleFilled(c, 1.5f, w);
		dl->AddLine(ImVec2(c.x, c.y - 8.f), ImVec2(c.x, c.y - 4.f), w, 1.6f);
		dl->AddLine(ImVec2(c.x, c.y + 4.f), ImVec2(c.x, c.y + 8.f), w, 1.6f);
		dl->AddLine(ImVec2(c.x - 8.f, c.y), ImVec2(c.x - 4.f, c.y), w, 1.6f);
		dl->AddLine(ImVec2(c.x + 4.f, c.y), ImVec2(c.x + 8.f, c.y), w, 1.6f);
		break;
	case 1: // eye
		dl->AddBezierQuadratic(ImVec2(c.x - 8.f, c.y), ImVec2(c.x, c.y - 9.f), ImVec2(c.x + 8.f, c.y), w, 1.6f, 16);
		dl->AddBezierQuadratic(ImVec2(c.x - 8.f, c.y), ImVec2(c.x, c.y + 9.f), ImVec2(c.x + 8.f, c.y), w, 1.6f, 16);
		dl->AddCircle(c, 2.4f, w, 0, 1.6f);
		break;
	case 2: // misc
		dl->AddLine(ImVec2(c.x - 6.f, c.y - 3.5f), ImVec2(c.x + 6.f, c.y - 3.5f), w, 1.6f);
		dl->AddLine(ImVec2(c.x - 6.f, c.y + 3.5f), ImVec2(c.x + 6.f, c.y + 3.5f), w, 1.6f);
		dl->AddCircleFilled(ImVec2(c.x + 2.f, c.y - 3.5f), 2.2f, w);
		dl->AddCircleFilled(ImVec2(c.x - 2.f, c.y + 3.5f), 2.2f, w);
		break;
	default: // config / folder
		dl->AddRect(ImVec2(c.x - 6.f, c.y - 4.f), ImVec2(c.x + 6.f, c.y + 5.f), w, 2.f, 0, 1.6f);
		dl->AddLine(ImVec2(c.x - 6.f, c.y - 4.f), ImVec2(c.x - 2.f, c.y - 6.f), w, 1.6f);
		break;
	}
}

static bool NavTab(const char* label, int icon, ImU32 col, bool sel, float w)
{
	const float H = 30.f;
	const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + w, p0.y + H);
	const bool hit = ImGui::InvisibleButton(label, ImVec2(w, H));
	const bool hov = ImGui::IsItemHovered();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	if (sel) dl->AddRectFilled(p0, p1, P().accent, 8.f);
	else if (hov) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 16), 8.f);
	NavIcon(dl, ImVec2(p0.x + 19.f, p0.y + H * 0.5f), icon, col);
	const ImVec2 ts = ImGui::CalcTextSize(label);
	dl->AddText(ImVec2(p0.x + 40.f, p0.y + (H - ts.y) * 0.5f), P().text, label);
	return hit;
}

static void DrawBackground(ImDrawList* dl, ImVec2 p, ImVec2 s)
{
	ImVec2 q(p.x + s.x, p.y + s.y);
	const int v0 = dl->VtxBuffer.Size;
	dl->AddRectFilled(p, q, IM_COL32(255, 255, 255, 205), 14.f);
	ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p, q, P().grad_a, P().grad_b);

	const ImU32 k0 = IM_COL32(8, 8, 16, 0), k1 = IM_COL32(8, 8, 16, 185);
	const float mid = p.y + s.y * 0.5f;
	dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + 20.f), ImVec2(q.x, mid), k0, k0, k1, k1);
	dl->AddRectFilledMultiColor(ImVec2(p.x, mid), ImVec2(q.x, q.y - 20.f), k1, k1, k0, k0);

	// diagonal shine streak (top-right)
	dl->AddRectFilledMultiColor(ImVec2(p.x + s.x * 0.55f, p.y), ImVec2(q.x, p.y + s.y * 0.45f),
		IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 10), IM_COL32(255, 255, 255, 0));

	dl->AddRectFilled(p, ImVec2(p.x + 200.f, q.y), IM_COL32(255, 255, 255, 12), 14.f, ImDrawFlags_RoundCornersLeft);
	dl->AddLine(ImVec2(p.x + 200.f, p.y + 1.f), ImVec2(p.x + 200.f, q.y - 1.f), IM_COL32(255, 255, 255, 30));
	dl->AddRect(p, q, IM_COL32(255, 255, 255, 45), 14.f);
}

// ============================================================================
// Pages
// ============================================================================
static const char* k_aim_modes[]    = { "Humanized", "Normal", "Blatant" };
static const char* k_mouse_modes[]  = { "Mouse", "Camera" };
static const char* k_target_parts[] = { "Head", "Torso", "HumanoidRootPart", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Closest Part", "Nearest" };
static const char* k_activate[]     = { "Hold", "Toggle", "Always" };
static const char* k_trig_methods[] = { "Normal", "Mesh" };
static const char* k_box_types[]    = { "2D", "Corner", "3D" };
static const char* k_name_modes[]   = { "Display Name", "Username", "Both" };
static const char* k_hp_styles[]    = { "Left", "Right", "Top", "Bottom" };
static const char* k_tracer_origins[] = { "Bottom", "Top", "Center", "Mouse" };
static const char* k_esp_teams[]    = { "Enemy", "Team", "Local" };
static const char* k_theme_names[]  = { "Midnight", "Dracula", "Nord", "Catppuccin Mocha", "Mac Light" };

static void aim_page()
{
	const bool blatant = (settings::aimbot::aim_mode == 2);
	const float colw = 306.f;
	ImGui::SetCursorPos(ImVec2(25.f, 87.f));

	// ---- aimbot
	ImGui::BeginChild("##col1", ImVec2(colw, -1.f), 0, ImGuiWindowFlags_NoBackground);
	{
		Card cd;
		if (CardBegin(cd, "AIMBOT"))
		{
			{ auto r = Row("Enabled", true);      Switch(r, &settings::aimbot::enabled); }
			{ auto r = Row("Aim mode");           ComboCtl(r, k_aim_modes, 3, &settings::aimbot::aim_mode); }
			{ auto r = Row("Aim at");             Segmented(r, k_mouse_modes, 2, &settings::aimbot::mode); }
			{ auto r = Row("Aim key");            KeyBindVK(r, &settings::aimbot::keybind); }
			{ auto r = Row("Activation");         Segmented(r, k_activate, 3, &settings::aimbot::activation_mode); }
			{ auto r = Row("FOV");                SliderCtl(r, &settings::aimbot::fov, 0.f, 600.f, "%.0f", true); }
			{ auto r = Row("Draw FOV circle");    Switch(r, &settings::aimbot::draw_fov); if (settings::aimbot::draw_fov) ColorDot(r, settings::aimbot::fov_circle_colour); }

			if (!blatant)
			{
				{ auto r = Row("Visible only");   Switch(r, &settings::aimbot::visible_check); }
				{ auto r = Row("Team check");     Switch(r, &settings::aimbot::teamcheck); }
				{ auto r = Row("Knock check");    Switch(r, &settings::aimbot::knock_check); }
				{ auto r = Row("Sticky aim");     Switch(r, &settings::aimbot::sticky_aim); }
				{ auto r = Row("Disable on kill"); Switch(r, &settings::aimbot::disable_on_kill); }
				{ auto r = Row("Target part");    ComboCtl(r, k_target_parts, 9, &settings::aimbot::target_parts); }
				{ auto r = Row("Air target part"); ComboCtl(r, k_target_parts, 9, &settings::aimbot::air_parts); }
				{ auto r = Row("Point amount");   Stepper(r, &settings::aimbot::point_amount, 1, 6); }

				{ auto r = Row("Aim speed");
					float master = settings::aimbot::aim_speed_x;
					if (SliderCtl(r, &master, 0.01f, 1.0f, "%.2f"))
					{
						settings::aimbot::aim_speed_x = master;
						settings::aimbot::aim_speed_y = master;
					} }
				{ auto r = Row("Aim speed X");    SliderCtl(r, &settings::aimbot::aim_speed_x, 0.01f, 1.0f, "%.2f"); }
				{ auto r = Row("Aim speed Y");    SliderCtl(r, &settings::aimbot::aim_speed_y, 0.01f, 1.0f, "%.2f"); }
			}
			CardEnd(cd, blatant ? "100% 1:1 lock - no smoothing." : "Aim speed and FOV apply while the aim key is held.");
		}
		if (!blatant && settings::aimbot::aim_mode == 0)
		{
			if (CardBegin(cd, "HUMANIZE"))
			{
				{ auto r = Row("Humanize", true);   Switch(r, &settings::aimbot::humanize); }
				if (settings::aimbot::humanize)
				{
					{ auto r = Row("Jitter");           SliderCtl(r, &settings::aimbot::human_jitter, 0.f, 10.f, "%.1f"); }
					{ auto r = Row("Overshoot");        SliderCtl(r, &settings::aimbot::overshoot, 0.f, 30.f, "%.1f"); }
					{ auto r = Row("Path wobble");      SliderCtl(r, &settings::aimbot::path_wobble, 0.f, 10.f, "%.1f"); }
					{ auto r = Row("Miss %");           SliderCtl(r, &settings::aimbot::miss_chance, 0.f, 50.f, "%.1f"); }
				}
				{ auto r = Row("Stick assist");     Switch(r, &settings::aimbot::stick_assist); }
				if (settings::aimbot::stick_assist)
				{
					{ auto r = Row("Stick strength");   SliderCtl(r, &settings::aimbot::stick_strength, 0.01f, 1.0f, "%.2f"); }
				}
				{ auto r = Row("Fatigue");          Switch(r, &settings::aimbot::fatigue); }
				if (settings::aimbot::fatigue)
				{
					{ auto r = Row("Fatigue rate");     SliderCtl(r, &settings::aimbot::fatigue_rate, 0.0f, 1.0f, "%.2f"); }
					{ auto r = Row("Fatigue decay");    SliderCtl(r, &settings::aimbot::fatigue_decay, 0.0f, 1.0f, "%.2f"); }
					{ auto r = Row("Fatigue move");     SliderCtl(r, &settings::aimbot::fatigue_move, 0.0f, 10.0f, "%.2f"); }
					{ auto r = Row("Fatigue offset");   SliderCtl(r, &settings::aimbot::fatigue_offset, 0.0f, 10.0f, "%.2f"); }
				}
				{ auto r = Row("Shake");            Switch(r, &settings::aimbot::shake); }
				if (settings::aimbot::shake)
				{
					{ auto r = Row("Shake amount");     SliderCtl(r, &settings::aimbot::shake_value, 0.1f, 20.0f, "%.1f"); }
				}
				{ auto r = Row("Offset");           Switch(r, &settings::aimbot::offset_enabled); }
				if (settings::aimbot::offset_enabled)
				{
					{ auto r = Row("Offset X");         SliderCtl(r, &settings::aimbot::offset_x, -200.0f, 200.0f, "%.1f"); }
					{ auto r = Row("Offset Y");         SliderCtl(r, &settings::aimbot::offset_y, -200.0f, 200.0f, "%.1f"); }
				}
				CardEnd(cd, "Humanize extras only apply in Humanized mode.");
			}
		}

		if (!blatant)
		{
			if (CardBegin(cd, "PREDICTION"))
			{
				{ auto r = Row("Prediction", true); Switch(r, &settings::aimbot::enable_prediction); }
				{ auto r = Row("Prediction X");     SliderCtl(r, &settings::aimbot::prediction_x, 0.0f, 100.0f, "%.1f"); }
				{ auto r = Row("Prediction Y");     SliderCtl(r, &settings::aimbot::prediction_y, 0.0f, 100.0f, "%.1f"); }
				{ auto r = Row("Velocity multiplier"); SliderCtl(r, &settings::aimbot::velocity_multiplier, 0.0f, 3.0f, "%.2f"); }
				{ auto r = Row("Range");            SliderCtl(r, &settings::aimbot::prediction_range, 0.0f, 1000.0f, "%.0f", true); }
				{ auto r = Row("Air prediction");   Switch(r, &settings::aimbot::air_prediction_enabled); }
				{ auto r = Row("Prediction dot");   Switch(r, &settings::aimbot::prediction_dot); if (settings::aimbot::prediction_dot) ColorDot(r, settings::aimbot::prediction_color); }
				CardEnd(cd);
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine(0.f, 17.f);

	// ---- triggerbot + mouse sync
	ImGui::BeginChild("##col2", ImVec2(colw, -1.f), 0, ImGuiWindowFlags_NoBackground);
	{
		Card cd;
		if (CardBegin(cd, "TRIGGERBOT"))
		{
			{ auto r = Row("Enabled", true);     Switch(r, &settings::triggerbot::enabled); }
			{ auto r = Row("Trigger key");       KeyBindVK(r, &settings::triggerbot::keybind); }
			{ auto r = Row("Activation");        Segmented(r, k_activate, 3, &settings::triggerbot::activation_mode); }
			{ auto r = Row("Method");            Segmented(r, k_trig_methods, 2, &settings::triggerbot::method); }
			{ auto r = Row("Threshold");         SliderCtl(r, &settings::triggerbot::threshold, 1.0f, 100.0f, "%.0f", true); }
			{ auto r = Row("Delay (ms)");        SliderCtl(r, &settings::triggerbot::delay_ms, 0.0f, 500.0f, "%.0f", true); }
			{ auto r = Row("Cooldown (ms)");     SliderCtl(r, &settings::triggerbot::cooldown_ms, 0.0f, 1000.0f, "%.0f", true); }
			{ auto r = Row("Team check");        Switch(r, &settings::triggerbot::teamcheck); }
			{ auto r = Row("Knock check");       Switch(r, &settings::triggerbot::knock_check); }
			{ auto r = Row("Gun check");         Switch(r, &settings::triggerbot::guncheck); }
			{ auto r = Row("Visible check");     Switch(r, &settings::triggerbot::visible_check); }
			{ auto r = Row("Draw FOV circle");   Switch(r, &settings::triggerbot::draw_fov); if (settings::triggerbot::draw_fov) ColorDot(r, settings::triggerbot::fov_circle_colour); }
			{ auto r = Row("Fill FOV");          Switch(r, &settings::triggerbot::fill_fov); if (settings::triggerbot::fill_fov) ColorDot(r, settings::triggerbot::fov_fill_colour); }
			CardEnd(cd, "The trigger FOV circle follows the cursor.");
		}

		if (!blatant)
		{
			SectionLabel("AIMBOT");
			if (settings::aimbot::mode == 0)
			{
				if (CardBegin(cd, "MOUSE SYNC"))
				{
					{ auto r = Row("Mouse sensitivity", true); SliderCtl(r, &settings::aimbot::mouse_sensitivity, 0.05f, 5.0f, "%.2f"); }
					{ auto r = Row("Match mouse DPI");  Switch(r, &settings::aimbot::match_dpi); }
					{ auto r = Row("Mouse DPI");        float f_dpi = (float)settings::aimbot::dpi; if (SliderCtl(r, &f_dpi, 100.f, 3200.f, "%.0f", true)) settings::aimbot::dpi = (int)f_dpi; }
					{ auto r = Row("Match pointer sensitivity"); Switch(r, &settings::aimbot::match_pointer_sens); }
					{ auto r = Row("Compensate mouse accel"); Switch(r, &settings::aimbot::compensate_accel); }
					{ auto r = Row("Accel factor");     SliderCtl(r, &settings::aimbot::accel_factor, 0.1f, 3.0f, "%.2f"); }
					CardEnd(cd, "Makes injected movement match your physical mouse.");
				}
			}

			if (CardBegin(cd, "RECOIL CONTROL"))
			{
				{ auto r = Row("Recoil control", true); Switch(r, &settings::aimbot::recoil_control); }
				{ auto r = Row("Recoil strength");  SliderCtl(r, &settings::aimbot::recoil_control_strength, 0.0f, 3.0f, "%.2f"); }
				CardEnd(cd);
			}
		}
	}
	ImGui::EndChild();
}

static void visuals_page()
{
	static int team_sel = 0;
	const float colw = 306.f;
	ImGui::SetCursorPos(ImVec2(25.f, 87.f));

	ImGui::BeginChild("##col1", ImVec2(colw, -1.f), 0, ImGuiWindowFlags_NoBackground);
	{
		Card cd;
		if (CardBegin(cd, "ESP"))
		{
			{ auto r = Row("Enabled", true);  Switch(r, &settings::visuals::enabled); }
			{ auto r = Row("ESP key");        KeyBindVK(r, &settings::visuals::keybind); }
			{ auto r = Row("Category");       ComboCtl(r, k_esp_teams, 3, &team_sel); }

			settings::visuals::priority_settings_t* cur =
				(team_sel == 0) ? &settings::visuals::hostile :
				(team_sel == 1) ? &settings::visuals::friendly : &settings::visuals::neutral;

			{ auto r = Row("Box");            Switch(r, &cur->box); if (cur->box) ColorDot(r, cur->colour); }
			{ auto r = Row("Box style");      ComboCtl(r, k_box_types, 3, &cur->box_type); }
			{ auto r = Row("Box fill");       Switch(r, &cur->box_fill); if (cur->box_fill) ColorDot(r, cur->box_fill_colour); }
			{ auto r = Row("Name");           Switch(r, &cur->username); if (cur->username) ColorDot(r, cur->username_colour); }
			{ auto r = Row("Name mode");      ComboCtl(r, k_name_modes, 3, &cur->username_type); }
			{ auto r = Row("Distance");       Switch(r, &cur->distance); if (cur->distance) ColorDot(r, cur->distance_colour); }
			{ auto r = Row("Held tool");      Switch(r, &cur->tool); if (cur->tool) ColorDot(r, cur->tool_colour); }
			CardEnd(cd, "Colors apply to every enabled overlay.");
		}
	}
	ImGui::EndChild();

	ImGui::SameLine(0.f, 17.f);

	ImGui::BeginChild("##col2", ImVec2(colw, -1.f), 0, ImGuiWindowFlags_NoBackground);
	{
		Card cd;
		if (CardBegin(cd, "OVERLAYS"))
		{
			settings::visuals::priority_settings_t* cur =
				(team_sel == 0) ? &settings::visuals::hostile :
				(team_sel == 1) ? &settings::visuals::friendly : &settings::visuals::neutral;

			{ auto r = Row("Health bar", true);  Switch(r, &cur->healthbar); }
			{ auto r = Row("Health text");       Switch(r, &cur->healthbar_text); if (cur->healthbar_text) ColorDot(r, cur->healthbar_text_colour); }
			{ auto r = Row("Bar position");      ComboCtl(r, k_hp_styles, 4, &cur->healthbar_style); }
			{ auto r = Row("Tracers");           Switch(r, &cur->tracers); if (cur->tracers) ColorDot(r, cur->tracers_colour); }
			{ auto r = Row("Tracer origin");     ComboCtl(r, k_tracer_origins, 4, &cur->tracers_origin); }
			{ auto r = Row("Skeleton");          Switch(r, &cur->skeleton); if (cur->skeleton) ColorDot(r, cur->skeleton_colour); }
			{ auto r = Row("Head dot");          Switch(r, &cur->head_dot); if (cur->head_dot) ColorDot(r, cur->head_dot_colour); }
			{ auto r = Row("Dot size");          SliderCtl(r, &cur->head_dot_size, 1.0f, 10.0f, "%.1f"); }
			{ auto r = Row("Offscreen arrows");  Switch(r, &cur->offscreen_arrows); }
			{ auto r = Row("Arrow radius");      SliderCtl(r, &cur->offscreen_arrows_radius, 50.0f, 400.0f, "%.0f", true); }
			{ auto r = Row("Arrow size");        SliderCtl(r, &cur->offscreen_arrows_size, 5.0f, 30.0f, "%.0f", true); }
			CardEnd(cd);
		}

		if (CardBegin(cd, "FILTERS"))
		{
			{ auto r = Row("Team check", true);    Switch(r, &settings::visuals::teamcheck); }
			{ auto r = Row("Visible check");       Switch(r, &settings::visuals::visible_check); }
			{ auto r = Row("Knock check");         Switch(r, &settings::visuals::knock_check); }
			{ auto r = Row("Alive check");         Switch(r, &settings::visuals::alive_check); }
			{ auto r = Row("Ragdoll check");       Switch(r, &settings::visuals::ragdoll_check); }
			{ auto r = Row("Distance limit");      Switch(r, &settings::visuals::distance_check); }
			{ auto r = Row("Max distance");        SliderCtl(r, &settings::visuals::max_distance, 50.0f, 5000.0f, "%.0f", true); }
			{ auto r = Row("Enemy highlight");     Switch(r, &settings::visuals::enemy_highlight); }
			{ auto r = Row("Friendly highlight");  Switch(r, &settings::visuals::friendly_highlight); }
			CardEnd(cd);
		}
	}
	ImGui::EndChild();
}

static void config_page()
{
	static std::vector<config::config_info_t> cfg_list = config::get_config_list();
	static int selected_cfg = -1;
	static char new_cfg_name[64] = "";

	const float colw = 306.f;
	ImGui::SetCursorPos(ImVec2(25.f, 87.f));

	ImGui::BeginChild("##col1", ImVec2(colw, -1.f), 0, ImGuiWindowFlags_NoBackground);
	{
		Card cd;
		if (CardBegin(cd, "CONFIGS"))
		{
			std::string current_autoload = config::get_autoload();

			// themed frame behind the listbox (replaces the stock frame)
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 lpos = ImGui::GetCursorScreenPos();
			const float lw = ImGui::GetContentRegionAvail().x;
			dl->AddRectFilled(lpos, lpos + ImVec2(lw, 176.0f), P().field, 9.0f);
			dl->AddRect(lpos, lpos + ImVec2(lw, 176.0f), P().card_brd, 9.0f);

			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(P().text));
			ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(P().accent_a));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(style::Lerp(P().accent_a, P().accent, 0.45f)));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(P().accent_a));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
			if (ImGui::BeginListBox("##configs_box", ImVec2(-FLT_MIN, 170.0f)))
			{
				for (int i = 0; i < (int)cfg_list.size(); i++)
				{
					const bool is_selected = (selected_cfg == i);
					const bool is_auto = (cfg_list[i].name == current_autoload);
					std::string display_name = cfg_list[i].name;
					if (is_auto) display_name += "  [Auto]";
					if (ImGui::Selectable(display_name.c_str(), is_selected))
						selected_cfg = i;
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndListBox();
			}
			ImGui::PopStyleColor(6);

			// config name input with a custom frame matching the menu controls
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
				const float fh = ImGui::GetFrameHeight(); // exact widget height
				const ImVec2 ipos = ImGui::GetCursorScreenPos();
				const float iw = ImGui::GetContentRegionAvail().x;
				// frame drawn at the exact widget size - fill behind, border on top
				dl->AddRectFilled(ipos, ipos + ImVec2(iw, fh), P().field, 8.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(P().text));
				ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(P().dim));
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
				ImGui::InputTextWithHint("##new_cfg", "Config name", new_cfg_name, sizeof(new_cfg_name));
				const bool input_focused = ImGui::IsItemFocused();
				ImGui::PopStyleColor(6);
				dl->AddRect(ipos, ipos + ImVec2(iw, fh), input_focused ? P().accent : P().card_brd, 8.0f);
				ImGui::PopStyleVar();
				ImGui::Dummy(ImVec2(1.f, 6.0f));
			}
		}
		CardEnd(cd);
	}
	ImGui::EndChild();

	ImGui::SameLine(0.f, 17.f);

	ImGui::BeginChild("##col2", ImVec2(colw, -1.f), 0, ImGuiWindowFlags_NoBackground);
	{
		Card cd;
		if (CardBegin(cd, "MANAGEMENT"))
		{
			const bool has_selection = (selected_cfg >= 0 && selected_cfg < (int)cfg_list.size());
			std::string sel_name = has_selection ? cfg_list[selected_cfg].name : "";

			// generic row-with-button helper
			auto action_row = [&](const char* row_label, const char* btn_label, bool danger, bool filled, ImGuiID seed) -> bool {
				RowCtx r2 = Row(row_label);
				const float bw = 90.0f;
				ImVec2 bpos(r2.x - bw, r2.cy - 14.0f);
				ImGui::SetCursorScreenPos(bpos);
				ImGui::PushID(seed);
				ImGui::InvisibleButton("##act", ImVec2(bw, 28.0f));
				const bool hit = ImGui::IsItemHovered() && ImGui::IsMouseReleased(0);
				const float hov = Anim(seed, ImGui::IsItemHovered(), 10.0f);
				ImGui::PopID();
				r2.dl->AddRectFilled(bpos, bpos + ImVec2(bw, 28.0f),
					filled ? LerpCol(P().accent, P().accent_hi, hov * 0.4f) : LerpCol(P().field, P().card_brd, hov * 0.6f), 8.0f);
				r2.dl->AddRect(bpos, bpos + ImVec2(bw, 28.0f), danger ? P().error : (filled ? P().accent : P().card_brd), 8.0f);
				ImVec2 ts = ImGui::CalcTextSize(btn_label);
				r2.dl->AddText(ImVec2(bpos.x + (bw - ts.x) * 0.5f, bpos.y + (28.0f - ts.y) * 0.5f),
					filled ? IM_COL32_WHITE : (danger ? P().error : P().text), btn_label);
				return hit;
			};

			{ auto r = Row("Create config", true); (void)r;
				if (action_row("New config name above", "Create", false, true, ImHashStr("cfg_create")))
				{
					if (strlen(new_cfg_name) > 0 && config::save_config(new_cfg_name))
					{
						cfg_list = config::get_config_list();
						ImNotify::Print(NotifyLevel::Success, "Config created!");
						new_cfg_name[0] = '\0';
					}
					else
						ImNotify::Print(NotifyLevel::Error, "Failed to create config!");
				}
			}

			if (!has_selection)
			{
				auto r = Row("Select a config first");
				(void)r;
			}
			else
			{
				if (action_row("Save selected", "Save", false, false, ImHashStr("cfg_save")))
				{
					if (config::save_config(sel_name)) ImNotify::Print(NotifyLevel::Success, "Config saved!");
					else ImNotify::Print(NotifyLevel::Error, "Failed to save config!");
				}
				if (action_row("Load selected", "Load", false, false, ImHashStr("cfg_load")))
				{
					if (config::load_config(sel_name)) ImNotify::Print(NotifyLevel::Success, "Config loaded!");
					else ImNotify::Print(NotifyLevel::Error, "Failed to load config!");
				}
				if (action_row("Set as autoload", "Set", false, false, ImHashStr("cfg_auto")))
				{
					config::set_autoload(sel_name.c_str());
					ImNotify::Print(NotifyLevel::Success, "Autoload set!");
				}
				if (action_row("Delete config", "Delete", true, false, ImHashStr("cfg_del")))
				{
					if (config::delete_config(sel_name))
					{
						cfg_list = config::get_config_list();
						selected_cfg = -1;
						ImNotify::Print(NotifyLevel::Success, "Config deleted!");
					}
					else
						ImNotify::Print(NotifyLevel::Error, "Failed to delete config!");
				}
			}
			CardEnd(cd);
		}

		if (CardBegin(cd, "THEME"))
		{
			{ auto r = Row("Theme", true); ComboCtl(r, k_theme_names, style::theme_count, &settings::misc::theme_index, 150.f); }
			if (settings::misc::theme_index != style::theme_index)
				Menu::ApplyTheme(settings::misc::theme_index);
			CardEnd(cd, "Theme changes apply instantly and save with configs.");
		}

		if (CardBegin(cd, "SYSTEM"))
		{
			{ auto r = Row("Keybind list", true);  Switch(r, &settings::misc::keybind_list); }
			{ auto r = Row("Show watermark");      Switch(r, &Menu::m_bShowWatermark); }
			{ auto r = Row("Streamproof");         Switch(r, &settings::streamproof); }
			{ auto r = Row("V-Sync");              Switch(r, &settings::vsync); }
			CardEnd(cd, "F2 loads the cheat - F3 ejects it.");
		}
	}
	ImGui::EndChild();
}

// ============================================================================
// Main menu
// ============================================================================
void Menu::DrawMenu()
{
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// plasma shader + darkening over the game while the menu is open
	{
		ImDrawList* bg = ImGui::GetBackgroundDrawList();
		bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(4, 4, 10, 95));
	}

	// search filter (lowercased)
	g_filter = g_search;
	std::transform(g_filter.begin(), g_filter.end(), g_filter.begin(), ::tolower);

	// input diagnostics
	{
		const ImGuiIO& m_io = ImGui::GetIO();
		static bool s_lmb_prev = false;
		if (m_io.MouseDown[0] && !s_lmb_prev)
			bootlog("menu: click at (%.0f, %.0f) - want_capture=%d tab=%d",
				m_io.MousePos.x, m_io.MousePos.y, (int)m_io.WantCaptureMouse, g_tab_index);
		s_lmb_prev = m_io.MouseDown[0];
	}

	// LazyUI visual assets: load on the first frame with a valid device
	if (!lazyui::loaded && render && render->detail && render->detail->device)
		lazyui::LoadAll(render->detail->device);

	const ImVec2 size(880.f, 560.f);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

	const ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
	ImGui::Begin("##menu", nullptr, wf);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 wp = ImGui::GetWindowPos();
	DrawBackground(dl, wp, size);

	// window drag (top strip)
	{
		static bool s_dragging = false;
		static ImVec2 s_drag_offset(0, 0);
		static ImVec2 s_pos = wp;
		const ImVec2 m(io.MousePos.x - wp.x, io.MousePos.y - wp.y);
		const bool in_strip = (m.y >= 0.0f && m.y <= 14.0f && m.x <= size.x - 60.0f);
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && in_strip)
		{
			s_dragging = true;
			s_drag_offset = ImVec2(io.MousePos.x - s_pos.x, io.MousePos.y - s_pos.y);
		}
		if (s_dragging)
		{
			if (ImGui::IsMouseDown(0))
			{
				s_pos = ImVec2(io.MousePos.x - s_drag_offset.x, io.MousePos.y - s_drag_offset.y);
				ImGui::SetNextWindowPos(s_pos, ImGuiCond_Always);
			}
			else
				s_dragging = false;
		}
	}

	// ---------------- sidebar
	ImGui::SetCursorPos(ImVec2(0, 0));
	ImGui::BeginChild("##sb", ImVec2(200.f, size.y), 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	{
		ImGui::SetCursorPos(ImVec2(11.f, 19.f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(P().text));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(P().dim));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(34.f, 6.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.f);
		ImGui::SetNextItemWidth(178.f);
		ImGui::InputTextWithHint("##search", "Search", g_search, sizeof g_search);
		const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		ImVec2 ic(a.x + 16.f, (a.y + b.y) * 0.5f - 1.f);
		ImDrawList* cdl = ImGui::GetWindowDrawList();
		cdl->AddRectFilled(a, b, P().field, 9.f);
		cdl->AddCircle(ic, 4.5f, P().dim, 0, 1.5f);
		cdl->AddLine(ImVec2(ic.x + 3.5f, ic.y + 3.5f), ImVec2(ic.x + 7.f, ic.y + 7.f), P().dim, 1.5f);

		struct T { const char* n; ImU32 c; };
		const T tabs[] = {
			{ "Aim",     IM_COL32(10, 132, 255, 255) },
			{ "Visuals", IM_COL32(50, 173, 230, 255) },
			{ "Config",  IM_COL32(142, 142, 147, 255) },
		};
		for (int i = 0; i < 3; i++)
		{
			ImGui::SetCursorPos(ImVec2(11.f, 61.f + i * 32.f));
			ImU32 col = (i == g_tab_index) ? IM_COL32(255, 255, 255, 40) : tabs[i].c;
			// accent glow behind the active tab
			if (g_tab_index == i && lazyui::tex_glow)
				cdl->AddImage((ImTextureID)lazyui::tex_glow,
					ImVec2(wp.x + 11.f - 26.f, wp.y + 61.f + i * 32.f - 24.f),
					ImVec2(wp.x + 11.f - 26.f + 178.f + 52.f, wp.y + 61.f + i * 32.f + 36.f + 48.f),
					ImVec2(0, 0), ImVec2(1, 1), style::Lerp(IM_COL32(0, 0, 0, 0), P().accent, 0.35f));
			if (NavTab(tabs[i].n, i, col, g_tab_index == i, 178.f))
			{
				g_tab_index = i;
				bootlog("nav: switched to '%s' (tab %d)", tabs[i].n, i);
			}
		}

		// avatar
		if (lazyui::tex_avatar)
			cdl->AddImage((ImTextureID)lazyui::tex_avatar,
				ImVec2(wp.x + 18.f, wp.y + size.y - 62.f), ImVec2(wp.x + 42.f, wp.y + size.y - 38.f),
				ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);

		cdl->AddCircleFilled(ImVec2(wp.x + 30.f, wp.y + size.y - 26.f), 4.5f, P().ok, 16);
		char fps[16];
		snprintf(fps, sizeof fps, "%.0f FPS", io.Framerate);
		cdl->AddText(ImVec2(wp.x + 44.f, wp.y + size.y - 34.f), P().dim, fps);
	}
	ImGui::EndChild();

	// ---------------- content
	ImGui::SetCursorPos(ImVec2(200.f, 0));
	ImGui::BeginChild("##content", ImVec2(680.f, size.y), 0, ImGuiWindowFlags_NoBackground);
	{
		ImDrawList* cdl = ImGui::GetWindowDrawList();
		const ImVec2 cp = ImGui::GetWindowPos();
		const char* titles[] = { "Aim", "Visuals", "Config" };
		const char* subs[] = { "Target selection and assist behavior", "Overlay and rendering options",
			"Save and load profiles" };

		// fake-bold title
		cdl->AddText(ImGui::GetFont(), 30.f, ImVec2(cp.x + 25.f, cp.y + 22.f), P().text, titles[g_tab_index]);
		cdl->AddText(ImGui::GetFont(), 30.f, ImVec2(cp.x + 25.7f, cp.y + 22.f), P().text, titles[g_tab_index]);
		cdl->AddText(ImGui::GetFont(), 16.f, ImVec2(cp.x + 25.f, cp.y + 56.f), P().dim, subs[g_tab_index]);

		// window buttons
		struct WB { float x; ImU32 col; bool close; } wbs[] = {
			{ 838.f - 200.f, IM_COL32(255, 189, 46, 255), false },
			{ 858.f - 200.f, IM_COL32(255, 69, 58, 255), true },
		};
		for (auto& b : wbs)
		{
			ImVec2 ctr(cp.x + b.x, cp.y + 21.f);
			ImGui::SetCursorScreenPos(ImVec2(ctr.x - 9.f, ctr.y - 9.f));
			ImGui::PushID(b.close);
			const bool hit = ImGui::InvisibleButton("wb", ImVec2(18.0f, 18.0f));
			ImGui::PopID();
			cdl->AddCircleFilled(ctr, 8.f, b.col, 20);
			if (b.close)
			{
				cdl->AddLine(ImVec2(ctr.x - 3.f, ctr.y - 3.f), ImVec2(ctr.x + 3.f, ctr.y + 3.f), IM_COL32_WHITE, 1.8f);
				cdl->AddLine(ImVec2(ctr.x + 3.f, ctr.y - 3.f), ImVec2(ctr.x - 3.f, ctr.y + 3.f), IM_COL32_WHITE, 1.8f);
				if (hit)
					var->gui.menu_opened = false;
			}
			else
			{
				cdl->AddLine(ImVec2(ctr.x - 3.5f, ctr.y), ImVec2(ctr.x + 3.5f, ctr.y), IM_COL32_WHITE, 1.8f);
				if (hit)
					var->gui.menu_opened = false;
			}
		}

		switch (g_tab_index)
		{
		case 0: aim_page(); break;
		case 1: visuals_page(); break;
		case 2: config_page(); break;
		}
	}
	ImGui::EndChild();

	ImGui::End();
	ImGui::PopStyleVar(4);
}

void Menu::Shutdown()
{
	if (!Menu::m_bInitialized) return;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Menu::InvalidateDeviceObjects()
{
	if (!Menu::m_bInitialized) return;

	ImGui_ImplDX11_InvalidateDeviceObjects();
}

void Menu::CreateDeviceObjects()
{
	if (!Menu::m_bInitialized) return;

	ImGui_ImplDX11_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool Menu::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (!Menu::m_bInitialized) return false;

	return ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
}
