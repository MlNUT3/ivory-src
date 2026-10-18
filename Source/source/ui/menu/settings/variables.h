#pragma once
#include <string>
#include <vector>
#include "imgui.h"
#include "ui/menufw/headers/flags.h"

class c_variables
{
public:
	struct
	{
		ImGuiWindowFlags main_flags{ ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip };
		ImGuiWindowFlags flags{ ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground };
		ImVec2 padding{ 0, 0 };
		ImVec2 spacing{ 5, 6 };
		float shadow_size{ 30 };
		float shadow_alpha{ 0.3f };
		float border_size{ 0 };
		float rounding{ 4 };
		float width{ 0 };
		float titlebar{ 20 };
		float scrollbar_size{ 2 };
		bool hover_hightlight{ true };
		bool lerp_animations{ true };
		bool window_gradients{ true };
		bool window_glow{ true };
	} window;

	struct
	{
		bool current_section[10] = { true, false, false, false, false, false, false, false, false, false };
		const char* section_icons[IM_ARRAYSIZE(current_section)] = {"A", "B", "C", "D", "E", "F", "G", "{/}", "SVR", "NPC"};

		float menu_alpha{ 0 };
		bool menu_opened{ false };
		int menu_key{ 46 };
		bool custom_cursor{ true };

		// reference-menu gui state (menufw)
		float dpi = 1.f;
		int stored_dpi = 100;
		bool dpi_changed = true;
		bool menu_open = true;
		int active_tab = 0;
		float tab_transition_alpha = 1.0f;
		float section_transition_alpha = 1.0f;
		float menu_open_alpha = 1.0f;
		bool menu_was_closed = false;

		// UI element toggles
		bool show_playerlist = true;
		bool show_playerbar = true;
		bool show_spotify = true;
		bool show_active_hotkeys = true;
		bool show_watermark = true;

		bool playerlist_spawned = false;
		bool playerbar_spawned = false;
		bool spotify_spawned = false;
		bool active_hotkeys_spawned = false;
		bool watermark_spawned = false;

		float playerlist_spawn_alpha = 0.0f;
		float playerbar_spawn_alpha = 0.0f;
		float spotify_spawn_alpha = 0.0f;
		float active_hotkeys_spawn_alpha = 0.0f;
		float watermark_spawn_alpha = 0.0f;

		ImVec2 menu_rect_min = ImVec2(0.0f, 0.0f);
		ImVec2 menu_rect_size = ImVec2(0.0f, 0.0f);
		bool menu_rect_valid = false;
	} gui;

	struct
	{
		ImFont* icons[2];
		ImFont* tahoma;
	} font;

	gui_style style;
};

inline c_variables* var = new c_variables();