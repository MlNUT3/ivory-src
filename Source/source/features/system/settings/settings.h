#pragma once
#include <imgui/imgui.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <atomic>

namespace settings
{
	inline bool streamproof = false;
	inline bool vsync = true;
	inline bool performance_mode = false;
	inline bool teamcheck = false;

	// Scan / manual-check resistance (FiveM-bypass style: leave no readable traces)
	namespace stealth
	{
		inline int module_mode = 1;      // 0 off, 1 rename module, 2 unlink module
		inline bool hide_console = true; // no console window for a checker to read
		inline bool name_threads = true; // spoof our thread names
		inline int panic_key = 0;        // VK code, 0 = disabled
		inline bool panic_active = false;
		// keeps the mouse cursor visible in recordings while streamproof is on
		// by rendering it through the software composition path (a 2px mouse
		// trail - WDA must stay on, it IS the streamproof mechanism)
		inline bool cursor_in_capture = true;
	}

	namespace general
	{
		inline bool confirm_unload = true;
		// irl mode: no visuals at all (no esp, no fov, no menu, no watermark) -
		// aimbot/triggerbot keep running. set from the loader at boot, live
		// toggle lives in the menu (system -> interface).
		inline bool irl_mode = false;
	}

	namespace config
	{
		inline bool enabled = true;
		inline int keybind = 0;
		inline int activation_mode = 1;
	}

	namespace aimbot
	{
		inline bool enabled = false;
		inline int keybind = 0;
		inline int activation_mode = 1;

		inline int mode = 0;            // 0 = Mouse, 1 = Camera
		inline int aim_mode = 1;        // 0 = Humanized, 1 = Normal, 2 = Blatant (1:1 lock)
		inline float mouse_sensitivity = 1.0f;
		inline bool visible_check = false;

		// Mouse sync: makes injected movement match your physical setup
		// no manual DPI input: Windows pointer-speed + acceleration are matched
		// automatically, and aim pacing adapts to the user's measured mouse
		// speed (subtle, see aimbot mouse sync)

		// Humanized (aim-assist) tuning
	inline float aim_speed = 0.65f; // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x) // legacy master (camera path now uses aim_speed_x)         // master aim speed - moving it sets both axes
		inline float aim_speed_x = 0.65f;       // per-axis aim speed (1.0 = 1:1 lock)
		inline float aim_speed_y = 0.65f;       // per-axis aim speed (1.0 = 1:1 lock)
		inline bool stick_assist = true;        // pull-in assist toward the target
		inline float stick_strength = 0.4f;
		// aim motion: circular sway (like shake but rotating)
		inline bool aim_motion = false;
		inline float aim_motion_speed = 2.0f;
		inline float aim_motion_intensity = 3.0f;     // how hard the assist pulls (0..1)
		// aim-assist feel: aim slows down while the cursor is over/near the
		// target (the signature controller-assist behavior). speed = the
		// tracking cap in px/s inside the radius at full grab; strength =
		// how hard the cap grabs (also swallows humanize noise near the
		// target, so the aim rests instead of shaking)
		inline bool assist_slowdown = true;
		inline float assist_slowdown_speed = 250.0f;  // px/s
		inline float assist_slowdown_strength = 0.5f; // 0 = off .. 1 = strong

		// Bitmask of selected hitparts: bit0 Head, bit1 Torso (+Upper/Lower),
		// bit2 HumanoidRootPart, bit3 Left Arm, bit4 Right Arm, bit5 Left Leg, bit6 Right Leg.
		inline int target_parts = 0x01;
		inline int air_parts = 0x01;

		inline float fov = 100.f;
		inline bool use_fov = true;
		inline bool dynamic_fov = false;
		inline bool ring_fov_enabled = false;
		inline float ring_fov_inner = 20.f;
		inline float ring_fov_outer = 100.f;


		inline bool shake = false;
		inline float shake_value = 5.0f;

		inline bool humanize = false;
	inline int lock_delay = 1;   // 1-50 scale (x10 = ms) - true reaction gate: aim holds still this long on a new lock, then corrects at full speed
		inline float human_jitter = 1.5f;
		inline float overshoot = 6.f;
		inline float path_wobble = 2.f;
		inline float miss_chance = 0.f;

		inline bool enable_prediction = false;
		inline float prediction_x = 10.f;
		inline float prediction_y = 10.f;
		inline bool jump_prediction = false;
		inline float jump_prediction_value = 10.f;
		inline bool fall_prediction = false;
		inline float fall_prediction_value = 10.f;

		// velocity customization
		inline float velocity_multiplier = 1.0f;
		inline bool air_prediction_enabled = false;
		inline float air_prediction_x = 10.f;
		inline float air_prediction_y = 10.f;

		// prediction extras
		inline float prediction_range = 250.f;   // prediction only applies within this distance
		inline bool adaptive_prediction = false; // adapt strength by target movement
		inline bool ping_interpolation = false;  // lead the target by latency in prediction

		// targeting extras
		inline float aim_deadzone = 0.f;           // px radius around the crosshair where the lock is off
		inline float adapt_strength = 1.0f;
		inline float adapt_speed = 20.f;
		inline int point_amount = 1;             // average of the N closest aim bones
		inline bool fatigue = false;             // aim degrades the longer you aim
		inline float fatigue_rate = 0.2f;
		inline float fatigue_decay = 0.75f;
		inline float fatigue_move = 3.0f;
		inline float fatigue_offset = 1.2f;
		inline bool prediction_dot = false;      // draw a dot at the predicted aim point
		inline float prediction_color[4] = { 1.f, 0.9f, 0.2f, 1.f };
		inline float prediction_size = 6.f;
		inline bool prediction_outline = false;

		// smoothing extras
		inline float mouse_look = 1.0f;          // camera-follow smoothing scale (camera mode)          // curved aim path instead of straight               // 0 Right, 1 Left, 2 Random
		inline bool offset_enabled = false;      // static screen offset on the aim point
		inline float offset_x = 0.f;
		inline float offset_y = 0.f;
		inline float shake_z = 0.f;              // vertical shake scale

		// advanced speed customization - clamps the per-tick aim step
		inline bool speed_cap = false;
		inline float max_speed_px = 120.f;
		inline float min_speed_px = 1.5f;

		// recoil control - counter the weapon kick while firing
		inline bool recoil_control = false;
		inline float recoil_control_strength = 1.0f;
		inline float recoil_control_smooth = 1.0f;
 // Linear


		inline bool teamcheck = false;
		inline bool knock_check = false;
		inline bool sticky_aim = false;
		inline bool disable_on_kill = false;

		inline int priorities = 0;
		inline bool health_check_enabled = false;
		inline float min_health = 0.f;          // filter targets below this health

		inline bool draw_fov = false;
		inline bool fill_fov = false;
		inline float fov_circle_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
		inline float fov_fill_colour[4] = { 1.f, 1.f, 1.f, 0.15f };
	}

	namespace triggerbot
	{
		inline bool enabled = false;
		inline int method = 0;
		inline int keybind = 0;
		inline int activation_mode = 1;

		inline bool visible_check = false;
		inline float threshold = 10.0f;
		inline int delay = 1;   // 1-50 scale (x10 = ms) - reaction wait before firing, re-verified on fresh data
		inline float cooldown_ms = 35.0f;

		inline bool teamcheck = false;
		inline bool knock_check = false;
		inline bool guncheck = false;

		inline bool draw_fov = false;
		inline bool fill_fov = false;
		inline float fov_circle_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
		inline float fov_fill_colour[4] = { 1.f, 1.f, 1.f, 0.15f };
	}

namespace misc
	{
		inline bool auto_rescan = true;
		inline bool custom_cursor = true;
		inline bool unlock_fps = false;
		inline int fps_cap = 180;
		inline bool keybind_list = false;
		inline bool keybind_indicator = false;
		inline float keybind_indicator_feature_colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		inline float keybind_indicator_mode_colour[4] = { 0.65f, 0.65f, 0.65f, 1.0f };
		inline bool watermark = true;
		inline bool target_hud = false;
		inline bool topbar = true;
		inline uint32_t topbar_active_tabs = 0x01;
		inline float watermark_pos_x = -1.0f;
		inline float watermark_pos_y = -1.0f;
		inline float target_hud_pos_x = -1.0f;
		inline float target_hud_pos_y = -1.0f;
		inline float keybind_list_pos_x = -1.0f;
		inline float keybind_list_pos_y = -1.0f;
		inline int menu_background_style = 0;
		inline int watermark_position = 2;
		inline bool explorer_window = false;
		inline bool explorer_pinned = false;
		inline bool spotify_window = false;
		inline bool spotify_pinned = false;
		inline bool npc_system_window = false;
		inline bool npc_system_pinned = false;
		inline int npc_system_keybind = 0;
		inline int npc_system_keybind_mode = 0;
		inline int preview_model = 2; // 0 = Arsenal, 1 = Client (Avatar), 2 = Tung Tung Tung Sahur, 3 = Custom (Resources)
		inline char custom_model_path[260] = "";
		inline bool sticky_preview = false;
		inline bool hide_console = false;
		inline bool bot_support = false;
		inline bool only_workspace_bots = false;
		inline bool disable_scrollbars = false;
		inline bool disable_confirmations = false;
		inline int theme_index = 0;
		inline bool custom_colors_enabled = false;
		inline bool performance_window = false;
		inline bool model_viewer_window = false;
		inline bool filter_hidden_characters = true;
		inline float custom_colors[14][4] = {
			{ 44 / 255.f, 48 / 255.f, 55 / 255.f, 1.0f },    // WindowBg
			{ 31 / 255.f, 33 / 255.f, 37 / 255.f, 1.0f },    // ChildBg
			{ 31 / 255.f, 33 / 255.f, 37 / 255.f, 1.0f },    // PopupBg
			{ 214 / 255.f, 217 / 255.f, 224 / 255.f, 1.0f }, // Text
			{ 118 / 255.f, 119 / 255.f, 123 / 255.f, 1.0f }, // TextDisabled
			{ 78 / 255.f, 81 / 255.f, 88 / 255.f, 1.0f },    // Border
			{ 0.00f, 0.00f, 0.00f, 1.0f },                   // BorderShadow
			{ 44 / 255.f, 48 / 255.f, 55 / 255.f, 1.0f },    // FrameBg
			{ 52 / 255.f, 56 / 255.f, 63 / 255.f, 1.0f },    // FrameBgHovered
			{ 39 / 255.f, 43 / 255.f, 50 / 255.f, 1.0f },    // FrameBgActive
			{ 0.00f, 0.00f, 0.00f, 0.30f },                  // FrameBgShadow
			{ 221 / 255.f, 168 / 255.f, 93 / 255.f, 0.22f }, // Header
			{ 221 / 255.f, 168 / 255.f, 93 / 255.f, 0.35f }, // HeaderHovered
			{ 221 / 255.f, 168 / 255.f, 93 / 255.f, 0.48f }  // HeaderActive
		};
	}

	namespace performance
	{
		inline int target_thread_sleep = 8;
		inline int aim_thread_sleep = 8;
		inline int cache_refresh_delay = 15;
		inline int main_loop_delay = 0;
		inline bool thread_throttling = true;

		inline float fps = 0.0f;
		inline float frametime = 0.0f;
		inline float target_thread_fps = 0.0f;
		inline float aim_thread_fps = 0.0f;
		inline float cache_thread_fps = 0.0f;
		inline float target_work_ms = 0.0f;
		inline float aim_work_ms = 0.0f;
		inline float cache_work_ms = 0.0f;

		inline std::atomic<uint64_t> target_loop_count = 0;
		inline std::atomic<uint64_t> aim_loop_count = 0;
		inline std::atomic<uint64_t> cache_loop_count = 0;
	}

	namespace visuals
	{
		struct priority_settings_t
		{
			bool box = false;
			int box_type = 1;
			int box_style = 0; // 0 = Regular, 1 = Corner, 2 = 3D
			float corner_box_length = 0.3f;
			bool box_inline = false;
			bool box_gradient = false;
			int box_gradient_type = 0;
			float box_pulse_speed = 2.0f;
			float colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float box_colour_mid[4] = { 1.f, 1.f, 1.f, 1.f };
			float box_colour_low[4] = { 1.f, 1.f, 1.f, 1.f };

			bool box_fill = false;
			int box_fill_style = 0;
			bool box_fill_gradient = false;
			bool box_fill_gradient_spin = false;
			float box_fill_gradient_speed = 2.0f;
			int box_fill_gradient_type = 0;
			int box_fill_gradient_mask = 1;
			float box_fill_pulse_speed = 2.0f;
			float box_fill_colour[4] = { 1.f, 1.f, 1.f, 0.2f };
			float box_fill_colour_mid[4] = { 1.f, 1.f, 1.f, 0.2f };
			float box_fill_colour_low[4] = { 1.f, 1.f, 1.f, 0.2f };

			bool username = false;
			int username_type = 0; // 0 = Display Name, 1 = Username, 2 = Both
			int username_position = 0; // 0 = Top, 1 = Bottom, 2 = Left, 3 = Right
			int name_transform = 0; // 0 = None, 1 = Uppercase, 2 = Lowercase
			float username_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			bool username_gradient = false;
			int username_gradient_type = 0; // 0 = Wave, 1 = Rainbow, 2 = Left to Right
			float username_gradient_speed = 2.5f;
			float username_colour_two[4] = { 1.f, 1.f, 1.f, 1.f };

			bool distance = false;
			float distance_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool tool = false;
			float tool_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool healthbar = false;
			int healthbar_style = 0;
			float healthbar_padding = 4.f;
			float healthbar_offset_x = 0.f;
			float healthbar_offset_y = 0.f;
			int healthbar_gradient_type = 0;
			float healthbar_pulse_speed = 1.f;
			int name_bracket_style = 0;
			bool healthbar_text = false;
			float healthbar_text_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			bool healthbar_lerp = true;
			bool value_follow = false;
			bool ignore_full = false;
			bool bar_fold = false;
			float bar_size = 1.f;
			float healthbar_colour[4] = { 0.f, 1.f, 0.f, 1.f };
			float healthbar_colour_mid[4] = { 1.f, 1.f, 0.f, 1.f };
			float healthbar_colour_low[4] = { 1.f, 0.f, 0.f, 1.f };

			bool tracers = false;
			int tracers_origin = 0;
			float tracers_thickness = 1.0f;
			float tracers_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool chams = false;
			int chams_type = 0;
			int chams_texture = 0;
			int mesh_shader = 0;
			bool mesh_outline = false;
			int mesh_outline_mode = 0; 
			int mesh_outline_style = 0;
			float mesh_outline_thickness = 2.5f;
			bool mesh_outline_only = false;
			bool mesh_accessories = false;
			float mesh_depth_bias = 0.0f;
			bool chams_corpse = false;
			bool chams_health_based = false;
			bool chams_flash = false;
			float chams_flash_speed = 1.0f;
			float chams_flash_colour[4] = { 1.f, 0.f, 0.f, 1.f };
			bool chams_fade = false;
			float chams_fade_speed = 1.0f;
			float chams_colour[4] = { 1.f, 1.f, 1.f, 0.25f };
			float chams_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
			bool chams_outline_glow = false;
			float chams_glow_colour[4] = { 1.f, 1.f, 1.f, 0.6f };
			float chams_glow_radius = 4.0f;
			float chams_glow_alpha = 0.5f;
			float chams_glow_intensity = 100.0f;

			bool skeleton = false;
			float skeleton_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float skeleton_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };

			bool head_dot = false;
			int head_type = 0; // 0 = Dot, 1 = Hexagon
			int head_mode = 0; // 0 = Static, 1 = Bounding
			bool circular_head_dot = false;
			float head_dot_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float head_dot_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
			float head_dot_size = 4.f;

			bool corpse = false;
			float corpse_colour[4] = { 1.f, 1.f, 1.f, 0.25f };
			float corpse_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
			bool corpse_names = false;
			float corpse_names_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool offscreen_arrows = false;
			int arrow_type = 0;
			int arrow_mode = 0;
			std::vector<int> arrows_info = { 1, 1 };
			float offscreen_arrows_size = 12.f;
			float offscreen_arrows_radius = 150.f;
			float offscreen_arrows_position = 40.f;
			float arrows_max_dist = 5000.f;
			bool arrows_glow = false;
			float offscreen_arrows_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float arrows_glow_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool movement_trails = false;
			float movement_trails_thickness = 1.0f;
			float movement_trails_length = 1.0f;
			float movement_trails_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool view_angle_lines = false;
			float view_angle_lines_thickness = 1.0f;
			float view_angle_lines_length = 10.0f;
			float view_angle_lines_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool sound_esp = false;
			float sound_esp_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float sound_esp_radius = 5.f;
			float sound_esp_speed = 1.0f;

			bool footprints = false;
			bool footprints_glow = false;
			float footprints_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float footprints_radius = 1.f;
			float footprints_speed = 1.0f;

			bool state_flags = false;
			bool rig_flags = false;
		};

		inline priority_settings_t neutral;
		inline priority_settings_t friendly;
		inline priority_settings_t hostile;

		inline bool enabled = true;
		inline int keybind = 0;
		inline int activation_mode = 1;
 // 0 = Glow (10), 1 = Invisible (14), 2 = Always On Top (13)

		inline bool teamcheck = false;
		inline int esp_font_index = 0;
		inline float esp_font_size = 12.f;
		inline int bounding_type = 0;
		inline std::vector<int> render_outlines = { 1, 1, 1, 1, 1, 1, 1, 1, 1 }; // Skeleton, Box, Name, Health Bar, Health Text, Held Item, Tracers, Head Dot, Distance
		inline float skeleton_thickness = 1.5f;
		inline float box_thickness = 1.0f;
		inline float healthbar_thickness = 2.0f;
		inline int distance_unit = 0; // 0 = Studs, 1 = Meters
		inline bool distance_check = false;
		inline float max_distance = 500.0f;
		inline bool sort_by_status = false;
		inline bool knock_check = false;
		inline bool local_player = false;
		inline bool tool_check = false;
		inline bool godded_check = false;
		inline bool forcefield_check = false;
		inline bool alive_check = false;
		inline bool ragdoll_check = false;
		inline bool visible_check = false; // 0 = None, 1 = Outline, 2 = Shadow
		// visualcheck: exact triangle-raycast occlusion engine (replaces the
		// aabb wallcheck when on) - gates esp, aimbot targeting and the
		// triggerbot fire decision through one toggle
		inline bool visual_check = false;
		inline float visual_radius = 500.f;      // geometry cache radius (studs)
		inline float visual_refresh_ms = 300.f;  // recache cadence
		inline bool ignore_full_health = false;
		inline float corpse_shift = 0.0f;
		inline bool enemy_highlight = false;
		inline float enemy_highlight_colour[4] = { 1.f, 0.2f, 0.2f, 0.6f };
		inline bool friendly_highlight = false;
		inline float friendly_highlight_colour[4] = { 0.2f, 1.f, 0.2f, 0.6f };
		inline bool bar_fill = false;
		inline float bar_fill_colour[4] = { 0.1f, 0.1f, 0.1f, 0.5f };
		inline bool health_check_enabled = false;

		inline bool flags = false;
		inline int flags_mask = 0;
		inline float flags_state_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float flags_colour[4] = { 1.f, 1.f, 1.f, 1.f };


		inline bool target_recolor = false;
		inline float target_recolor_colour[4] = { 1.f, 0.f, 0.f, 1.f };


	}

	namespace playerlist
	{
		inline char npc_path_input[256] = "Ugc.Workspace.Bots";
		inline std::unordered_set<std::string> active_npc_paths;
	}

	namespace lighting
	{
	}

	namespace typingcheck
	{
		inline bool enabled = true;
	}

	namespace mm2
	{
		inline bool role_esp = false;
		inline bool coin_esp = false;
	}

}

