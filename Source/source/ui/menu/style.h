#pragma once
// ============================================================================
// Shared UI style: palette + 5 named themes used by the menu, loader and
// watermark. Palettes use well-known color combinations:
//   Midnight         - deep charcoal/navy + Apple blue  (default, macOS dark)
//   Dracula          - #282A36 bg, #BD93F9 purple, #50FA7B green
//   Nord             - #2E3440 polar night, #88C0D0 frost, #A3BE8C aurora
//   Catppuccin Mocha - #1E1E2E base, #CBA6F7 mauve, #A6E3A1 green
//   Mac Light        - #F5F5F7, Apple blue #007AFF (macOS light mode)
// ============================================================================

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <ui/menu/settings/colors.h>

namespace style
{
	struct Palette
	{
		ImU32 win_bg;      // window background
		ImU32 win_tint;    // subtle tint (right side of the window gradient)
		ImU32 grad_a;      // background shader gradient start (vivid)
		ImU32 grad_b;      // background shader gradient end
		ImU32 sidebar_bg;  // sidebar panel
		ImU32 card_bg;     // card background
		ImU32 card_brd;    // card border
		ImU32 accent;      // primary accent (toggles active, sliders, values)
		ImU32 accent_hi;   // accent hover
		ImU32 accent_dn;   // accent pressed / muted
		ImU32 accent_a;    // accent wash (pill highlight, ~12% alpha)
		ImU32 toggle_on;   // green toggle
		ImU32 text;        // primary text
		ImU32 dim;         // secondary text
		ImU32 field;       // input/track background
		ImU32 knob;        // toggle knob
		ImU32 error;       // error text
		ImU32 ok;          // status dot (green)
	};

	// --- Midnight (default; matches the reference screenshots) ---
	inline Palette midnight = {
		IM_COL32(15, 17, 22, 255),     // win_bg    #0F1116
		IM_COL32(24, 20, 40, 255),     // win_tint  subtle purple
		IM_COL32(60, 52, 140, 255),    // grad_a    vivid blue-purple
		IM_COL32(18, 66, 130, 255),    // grad_b    deep blue
		IM_COL32(20, 23, 30, 255),     // sidebar   #14171E
		IM_COL32(25, 29, 38, 255),     // card      #191D26
		IM_COL32(38, 44, 56, 255),     // card_brd  #262C38
		IM_COL32(10, 132, 255, 255),   // accent    #0A84FF
		IM_COL32(64, 156, 255, 255),   // accent_hi
		IM_COL32(8, 96, 190, 255),     // accent_dn
		IM_COL32(10, 132, 255, 30),    // accent_a
		IM_COL32(48, 209, 88, 255),    // toggle_on #30D158
		IM_COL32(232, 234, 238, 255),  // text
		IM_COL32(122, 127, 138, 255),  // dim
		IM_COL32(35, 40, 51, 255),     // field     #232833
		IM_COL32(245, 246, 248, 255),  // knob
		IM_COL32(255, 105, 120, 255),  // error
		IM_COL32(48, 209, 88, 255),    // ok
	};

	// --- Dracula ---
	inline Palette dracula = {
		IM_COL32(33, 34, 44, 255),     // win_bg    #21222C
		IM_COL32(44, 40, 66, 255),     // win_tint
		IM_COL32(98, 63, 152, 255),    // grad_a
		IM_COL32(52, 48, 90, 255),     // grad_b
		IM_COL32(40, 42, 54, 255),     // sidebar   #282A36
		IM_COL32(43, 45, 61, 255),     // card      #2B2D3D
		IM_COL32(68, 71, 90, 255),     // card_brd  #44475A
		IM_COL32(189, 147, 249, 255),  // accent    #BD93F9
		IM_COL32(255, 121, 198, 255),  // accent_hi #FF79C6
		IM_COL32(139, 103, 187, 255),  // accent_dn
		IM_COL32(189, 147, 249, 30),   // accent_a
		IM_COL32(80, 250, 123, 255),   // toggle_on #50FA7B
		IM_COL32(248, 248, 242, 255),  // text      #F8F8F2
		IM_COL32(98, 114, 164, 255),   // dim       #6272A4
		IM_COL32(59, 61, 79, 255),     // field     #3B3D4F
		IM_COL32(248, 248, 242, 255),  // knob
		IM_COL32(255, 85, 85, 255),    // error     #FF5555
		IM_COL32(80, 250, 123, 255),   // ok
	};

	// --- Nord ---
	inline Palette nord = {
		IM_COL32(36, 41, 51, 255),     // win_bg    #242933
		IM_COL32(46, 52, 64, 255),     // win_tint  #2E3440
		IM_COL32(76, 88, 110, 255),    // grad_a
		IM_COL32(59, 66, 82, 255),     // grad_b
		IM_COL32(41, 47, 58, 255),     // sidebar   #292F3A
		IM_COL32(46, 52, 64, 255),     // card      #2E3440
		IM_COL32(59, 66, 82, 255),     // card_brd  #3B4252
		IM_COL32(136, 192, 208, 255),  // accent    #88C0D0
		IM_COL32(163, 190, 232, 255),  // accent_hi #8FBCBB-ish
		IM_COL32(94, 129, 172, 255),   // accent_dn #5E81AC
		IM_COL32(136, 192, 208, 30),   // accent_a
		IM_COL32(163, 190, 140, 255),  // toggle_on #A3BE8C
		IM_COL32(236, 239, 244, 255),  // text      #ECEFF4
		IM_COL32(123, 136, 161, 255),  // dim       #7B88A1
		IM_COL32(59, 66, 82, 255),     // field     #3B4252
		IM_COL32(236, 239, 244, 255),  // knob
		IM_COL32(191, 97, 106, 255),   // error     #BF616A
		IM_COL32(163, 190, 140, 255),  // ok
	};

	// --- Catppuccin Mocha ---
	inline Palette catppuccin = {
		IM_COL32(24, 24, 37, 255),     // win_bg    #181825 (mantle)
		IM_COL32(30, 30, 46, 255),     // win_tint  #1E1E2E (base)
		IM_COL32(88, 62, 140, 255),    // grad_a
		IM_COL32(48, 45, 90, 255),     // grad_b
		IM_COL32(24, 24, 37, 255),     // sidebar
		IM_COL32(30, 30, 46, 255),     // card      #1E1E2E
		IM_COL32(49, 50, 68, 255),     // card_brd  #313244
		IM_COL32(203, 166, 247, 255),  // accent    #CBA6F7 (mauve)
		IM_COL32(245, 194, 231, 255),  // accent_hi #F5C2E7 (pink)
		IM_COL32(137, 120, 224, 255),  // accent_dn #89B4FA-ish lavender
		IM_COL32(203, 166, 247, 30),   // accent_a
		IM_COL32(166, 227, 161, 255),  // toggle_on #A6E3A1
		IM_COL32(205, 214, 244, 255),  // text      #CDD6F4
		IM_COL32(108, 112, 134, 255),  // dim       #6C7086
		IM_COL32(49, 50, 68, 255),     // field     #313244
		IM_COL32(205, 214, 244, 255),  // knob
		IM_COL32(243, 139, 168, 255),  // error     #F38BA8
		IM_COL32(166, 227, 161, 255),  // ok
	};

	// --- Mac Light ---
	inline Palette mac_light = {
		IM_COL32(245, 245, 247, 255),  // win_bg    #F5F5F7
		IM_COL32(233, 234, 240, 255),  // win_tint
		IM_COL32(226, 226, 235, 255),  // grad_a
		IM_COL32(208, 222, 245, 255),  // grad_b
		IM_COL32(236, 236, 241, 255),  // sidebar   #ECECF1
		IM_COL32(255, 255, 255, 255),  // card      #FFFFFF
		IM_COL32(216, 216, 222, 255),  // card_brd  #D8D8DE
		IM_COL32(0, 122, 255, 255),    // accent    #007AFF
		IM_COL32(64, 156, 255, 255),   // accent_hi
		IM_COL32(0, 98, 190, 255),     // accent_dn
		IM_COL32(0, 122, 255, 26),     // accent_a
		IM_COL32(52, 199, 89, 255),    // toggle_on #34C759
		IM_COL32(29, 29, 31, 255),     // text      #1D1D1F
		IM_COL32(134, 134, 139, 255),  // dim       #86868B
		IM_COL32(233, 233, 238, 255),  // field     #E9E9EE
		IM_COL32(255, 255, 255, 255),  // knob
		IM_COL32(255, 59, 48, 255),    // error     #FF3B30
		IM_COL32(52, 199, 89, 255),    // ok
	};

	inline const char* theme_names[] = { "Midnight", "Dracula", "Nord", "Catppuccin Mocha", "Mac Light" };
	inline const int   theme_count = 5;

	inline Palette P = midnight;
	inline int   theme_index = 0;

	// color lerp helper (for animations)
	inline ImU32 Lerp(ImU32 a, ImU32 b, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const int ar = (a >> 0) & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
		const int br = (b >> 0) & 0xFF, bg_ = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ba = (b >> 24) & 0xFF;
		return IM_COL32(
			(int)(ar + (br - ar) * t),
			(int)(ag + (bg_ - ag) * t),
			(int)(ab + (bb - ab) * t),
			(int)(aa + (ba - aa) * t));
	}

	inline float GetAlpha() { return ((P.text >> 24) & 0xFF) > 0 ? 1.0f : 1.0f; } // reserved

	// apply palette to the ImGui style + the legacy widget layer
	inline void ApplyImGui(const Palette& p)
	{
		ImGuiStyle& st = ImGui::GetStyle();
		ImVec4* colors = st.Colors;

		auto toV4 = [](ImU32 c) {
			return ImVec4(((c >> 0) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f,
				((c >> 16) & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f);
		};

		colors[ImGuiCol_WindowBg]          = toV4(p.win_bg);
		colors[ImGuiCol_ChildBg]           = ImVec4(0, 0, 0, 0);
		colors[ImGuiCol_PopupBg]           = toV4(p.card_bg);
		colors[ImGuiCol_Text]              = toV4(p.text);
		colors[ImGuiCol_TextDisabled]      = toV4(p.dim);
		colors[ImGuiCol_Border]            = toV4(p.card_brd);
		colors[ImGuiCol_BorderShadow]      = ImVec4(0, 0, 0, 0);
		colors[ImGuiCol_FrameBg]           = toV4(p.field);
		colors[ImGuiCol_FrameBgHovered]    = toV4(style::Lerp(p.field, p.card_brd, 0.5f));
		colors[ImGuiCol_FrameBgActive]     = toV4(p.card_brd);
		colors[ImGuiCol_FrameBgShadow]     = ImVec4(0, 0, 0, 0);
		colors[ImGuiCol_TitleBg]           = toV4(p.sidebar_bg);
		colors[ImGuiCol_TitleBgActive]     = toV4(p.sidebar_bg);
		colors[ImGuiCol_TitleBgCollapsed]  = toV4(p.sidebar_bg);
		colors[ImGuiCol_Header]            = toV4(p.accent_dn);
		colors[ImGuiCol_HeaderHovered]     = toV4(p.accent);
		colors[ImGuiCol_HeaderActive]      = toV4(p.accent_dn);
		colors[ImGuiCol_ScrollbarBg]       = ImVec4(0, 0, 0, 0);
		colors[ImGuiCol_ScrollbarGrab]     = toV4(p.field);
		colors[ImGuiCol_ScrollbarGrabHovered] = toV4(p.card_brd);
		colors[ImGuiCol_ScrollbarGrabActive]  = toV4(p.accent);
		colors[ImGuiCol_SliderGrab]        = toV4(p.accent);
		colors[ImGuiCol_SliderGrabActive]  = toV4(p.accent_hi);
		colors[ImGuiCol_Button]            = toV4(p.field);
		colors[ImGuiCol_ButtonHovered]     = toV4(p.card_brd);
		colors[ImGuiCol_ButtonActive]      = toV4(p.card_brd);
		colors[ImGuiCol_Separator]         = toV4(p.card_brd);
		colors[ImGuiCol_CheckMark]         = toV4(p.accent);
		colors[ImGuiCol_ModalWindowDimBg]  = ImVec4(0, 0, 0, 0.55f);

		// legacy widget layer
		clr->accent = ImColor(p.accent);
		clr->window.background_one = ImColor(p.win_bg);
		clr->window.background_two = ImColor(p.win_bg);
		clr->window.stroke = ImColor(p.card_brd);
		clr->widgets.stroke_two = ImColor(p.card_brd);
		clr->widgets.text = ImColor(p.text);
		clr->widgets.text_inactive = ImColor(p.dim);
	}

	inline void SetTheme(int index)
	{
		theme_index = std::clamp(index, 0, theme_count - 1);
		switch (theme_index)
		{
		case 1:  P = dracula;    break;
		case 2:  P = nord;       break;
		case 3:  P = catppuccin; break;
		case 4:  P = mac_light;  break;
		default: P = midnight;   break;
		}
		ApplyImGui(P);
	}
}
