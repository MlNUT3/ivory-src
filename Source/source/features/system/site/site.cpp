// Control-panel client: applies this account's settings from the web panel to
// the live cheat and reports the player list back.
//
// The panel is the source of truth, with one exception: on connect the client
// first applies the stored config, then pushes its FULL current state back.
// Without that push the panel only ever shows the keys the user has touched in
// the browser - everything else displays schema defaults while the cheat keeps
// its locally loaded values, which reads as "the feature acts on when the
// panel says it's off".
//
// Transport (two threads, same shape as the loader): a receiver thread blocks
// in WinHttpWebSocketReceive (the receive timeout is NOT honored for WebSocket
// frames - verified against the live server) and handles config/commands; the
// site thread loops sending the player list every ~1.2s.
//
// Commands the *loader* owns (load/unload/reinject/selfdestruct) are ignored
// here - both processes share the account token so both receive them.

#include "site.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>

#include <core/utility/json/json.h>
#include <features/system/settings/settings.h>
#include <features/system/config/config.h>
#include <features/system/playertools/playerops.h>
#include <features/system/notifications/notifications.h>
#include <features/visuals/geometry/wallcheck.h>
#include <sdk/cache/core/cache.h>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace site
{
	namespace
	{
		const wchar_t* kKeyPath = L"Software\\RiotClientCrashHandler";
		const wchar_t* kEndpoint = L"cdn.edge-config.workers.dev";

		std::wstring reg_str(const wchar_t* name)
		{
			HKEY k;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, KEY_READ, &k) != ERROR_SUCCESS)
				return {};
			wchar_t buf[1024] = { 0 };
			DWORD sz = sizeof(buf) - sizeof(wchar_t), type = 0;
			const LSTATUS s = RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)buf, &sz);
			RegCloseKey(k);
			if (s != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
			return buf;
		}

		/* ------------------------------------------------------- table */

		struct ComboVal { const char* s; int v; };

		enum Kind : unsigned char { K_BOOL, K_INT, K_FLOAT, K_COLOR, K_COMBO, K_MS };

		struct Entry
		{
			const char* key;
			void* target;
			unsigned char kind;
			const ComboVal* combo;
			void (*custom)(const json::Value&);
		};

		const ComboVal C_ACT[]   = { { "toggle", 0 }, { "hold", 1 }, { "always", 2 }, { nullptr, 0 } };
		const ComboVal C_AIMAT[] = { { "mouse", 0 }, { "camera", 1 }, { nullptr, 0 } };
		const ComboVal C_AIMODE[]= { { "humanized", 0 }, { "normal", 1 }, { "blatant", 2 }, { nullptr, 0 } };
		const ComboVal C_TBMETH[]= { { "normal", 0 }, { "mesh", 1 }, { nullptr, 0 } };
		const ComboVal C_UTYPE[] = { { "display", 0 }, { "account", 1 }, { "tag", 2 }, { nullptr, 0 } };
		const ComboVal C_UPOS[]  = { { "top", 0 }, { "bottom", 1 }, { "above", 2 }, { nullptr, 0 } };
		const ComboVal C_TRANS[] = { { "none", 0 }, { "upper", 1 }, { "lower", 2 }, { nullptr, 0 } };
		const ComboVal C_HBSTY[] = { { "left", 0 }, { "right", 1 }, { "top", 2 }, { "bottom", 3 }, { nullptr, 0 } };
		const ComboVal C_BOXTY[] = { { "normal", 0 }, { "corners", 1 }, { "rounded", 2 }, { "filled", 0 }, { nullptr, 0 } };
		const ComboVal C_TRORIG[]= { { "bottom", 0 }, { "center", 1 }, { "top", 2 }, { "mouse", 3 }, { nullptr, 0 } };
		const ComboVal C_HTYPE[] = { { "dot", 0 }, { "circle", 1 }, { "cross", 2 }, { nullptr, 0 } };
		const ComboVal C_GRADT[] = { { "vertical", 0 }, { "horizontal", 1 }, { "diagonal", 2 }, { "rainbow", 1 }, { nullptr, 0 } };

		void ap_part(const json::Value& v)
		{
			const std::string s = v.as_str();
			int mask = 0x01;
			if (s == "head") mask = 0x01;
			else if (s == "neck") mask = 0x01;
			else if (s == "chest") mask = 0x02;
			else if (s == "stomach") mask = 0x02;
			else if (s == "nearest") mask = 0x01;
			else if (s == "random") mask = 0x7F;
			settings::aimbot::target_parts = mask;
		}

		void ap_air_part(const json::Value& v)
		{
			const std::string s = v.as_str();
			int mask = 0x01;
			if (s == "head") mask = 0x01;
			else if (s == "neck") mask = 0x01;
			else if (s == "chest") mask = 0x02;
			else if (s == "stomach") mask = 0x02;
			else if (s == "nearest") mask = 0x01;
			else if (s == "random") mask = 0x7F;
			settings::aimbot::air_parts = mask;
		}

		// gradient/fill "pulse" toggles: 0 = off, otherwise a sane speed
		void ap_box_pulse(const json::Value& v)
		{
			settings::visuals::hostile.box_pulse_speed = v.as_bool() ? 2.0f : 0.0f;
		}
		void ap_fill_pulse(const json::Value& v)
		{
			settings::visuals::hostile.box_fill_pulse_speed = v.as_bool() ? 2.0f : 0.0f;
		}
		void ap_hb_pulse(const json::Value& v)
		{
			settings::visuals::hostile.healthbar_pulse_speed = v.as_bool() ? 1.0f : 0.0f;
		}
		void ap_hb_grad(const json::Value& v)
		{
			settings::visuals::hostile.healthbar_gradient_type = v.as_bool() ? 1 : 0;
		}

#define B(k, t) { k, (void*)&(t), K_BOOL,  nullptr, nullptr }
#define I(k, t) { k, (void*)&(t), K_INT,   nullptr, nullptr }
#define F(k, t) { k, (void*)&(t), K_FLOAT, nullptr, nullptr }
#define MS(k, t){ k, (void*)&(t), K_MS,    nullptr, nullptr }
#define C(k, t) { k, (void*)(t),   K_COLOR, nullptr, nullptr }
#define S(k, t, m) { k, (void*)&(t), K_COMBO, m, nullptr }
#define U(k, fn) { k, nullptr, 0, nullptr, fn }

		using visuals_t = settings::visuals::priority_settings_t;

		const Entry kMap[] = {
			// ---- aimbot / aim ----
			B("aim.enabled",        settings::aimbot::enabled),
			S("aim.mode",           settings::aimbot::aim_mode, C_AIMODE),
			S("aim.at",             settings::aimbot::mode, C_AIMAT),
			I("aim.key",            settings::aimbot::keybind),
			S("aim.activation",     settings::aimbot::activation_mode, C_ACT),
			B("aim.use_fov",        settings::aimbot::use_fov),
			F("aim.fov",            settings::aimbot::fov),
			B("aim.dynamic_fov",    settings::aimbot::dynamic_fov),
			F("aim.deadzone",       settings::aimbot::aim_deadzone),
			B("aim.draw_fov",       settings::aimbot::draw_fov),
			C("aim.fov_color",      settings::aimbot::fov_circle_colour),
			F("aim.speed",          settings::aimbot::aim_speed),
			F("aim.speed_x",        settings::aimbot::aim_speed_x),
			F("aim.speed_y",        settings::aimbot::aim_speed_y),
			F("aim.mouse_sens",     settings::aimbot::mouse_sensitivity),
			F("aim.cam_smooth",     settings::aimbot::mouse_look),

			// ---- aimbot / targeting ----
			B("tgt.team",           settings::aimbot::teamcheck),
			B("tgt.knock",          settings::aimbot::knock_check),
			B("tgt.ping",           settings::aimbot::ping_interpolation),
			B("tgt.sticky",         settings::aimbot::sticky_aim),
			B("tgt.disable_kill",   settings::aimbot::disable_on_kill),
			B("tgt.health_check",   settings::aimbot::health_check_enabled),
			F("tgt.min_health",     settings::aimbot::min_health),
			I("tgt.point_count",    settings::aimbot::point_amount),
			U("tgt.part",           ap_part),
			U("tgt.air_part",       ap_air_part),

			// ---- triggerbot ----
			B("tb.enabled",         settings::triggerbot::enabled),
			I("tb.key",             settings::triggerbot::keybind),
			S("tb.activation",      settings::triggerbot::activation_mode, C_ACT),
			S("tb.method",          settings::triggerbot::method, C_TBMETH),
			F("tb.threshold",       settings::triggerbot::threshold),
			I("tb.delay",           settings::triggerbot::delay),
			F("tb.cooldown",        settings::triggerbot::cooldown_ms),
			B("tb.team",            settings::triggerbot::teamcheck),
			B("tb.knock",           settings::triggerbot::knock_check),
			B("tb.gun",             settings::triggerbot::guncheck),
			B("tb.draw_fov",        settings::triggerbot::draw_fov),
			C("tb.fov_color",       settings::triggerbot::fov_circle_colour),
			B("tb.fill",            settings::triggerbot::fill_fov),
			C("tb.fill_color",      settings::triggerbot::fov_fill_colour),

			// ---- recoil ----
			B("rec.enabled",        settings::aimbot::recoil_control),
			F("rec.strength",       settings::aimbot::recoil_control_strength),

			// ---- humanize ----
			B("hu.enabled",         settings::aimbot::humanize),
			F("hu.jitter",          settings::aimbot::human_jitter),
			I("hu.lock_delay",      settings::aimbot::lock_delay),
			F("hu.overshoot",       settings::aimbot::overshoot),
			F("hu.wobble",          settings::aimbot::path_wobble),
			F("hu.miss",            settings::aimbot::miss_chance),
			B("hu.motion",          settings::aimbot::aim_motion),
			F("hu.motion_speed",    settings::aimbot::aim_motion_speed),
			F("hu.motion_int",      settings::aimbot::aim_motion_intensity),
			B("hu.stick",           settings::aimbot::stick_assist),
			F("hu.stick_strength",  settings::aimbot::stick_strength),
			B("hu.fatigue",         settings::aimbot::fatigue),
			F("hu.fatigue_rate",    settings::aimbot::fatigue_rate),
			F("hu.fatigue_decay",   settings::aimbot::fatigue_decay),
			F("hu.fatigue_move",    settings::aimbot::fatigue_move),
			F("hu.fatigue_offset",  settings::aimbot::fatigue_offset),
			B("hu.slowdown",        settings::aimbot::assist_slowdown),
			F("hu.slowdown_speed",  settings::aimbot::assist_slowdown_speed),
			F("hu.slowdown_strength", settings::aimbot::assist_slowdown_strength),
			B("hu.shake",           settings::aimbot::shake),
			F("hu.shake_val",       settings::aimbot::shake_value),
			F("hu.shake_z",         settings::aimbot::shake_z),
			B("hu.offset",          settings::aimbot::offset_enabled),
			F("hu.offset_x",        settings::aimbot::offset_x),
			F("hu.offset_y",        settings::aimbot::offset_y),

			// ---- prediction ----
			B("pr.enabled",         settings::aimbot::enable_prediction),
			F("pr.x",               settings::aimbot::prediction_x),
			F("pr.y",               settings::aimbot::prediction_y),
			B("pr.jump",            settings::aimbot::jump_prediction),
			F("pr.jump_val",        settings::aimbot::jump_prediction_value),
			B("pr.fall",            settings::aimbot::fall_prediction),
			F("pr.fall_val",        settings::aimbot::fall_prediction_value),
			F("pr.vel_mult",        settings::aimbot::velocity_multiplier),
			B("pr.air",             settings::aimbot::air_prediction_enabled),
			F("pr.air_x",           settings::aimbot::air_prediction_x),
			F("pr.air_y",           settings::aimbot::air_prediction_y),
			F("pr.range",           settings::aimbot::prediction_range),
			B("pr.adaptive",        settings::aimbot::adaptive_prediction),
			F("pr.adapt_strength",  settings::aimbot::adapt_strength),
			F("pr.adapt_speed",     settings::aimbot::adapt_speed),
			B("pr.dot",             settings::aimbot::prediction_dot),
			C("pr.dot_color",       settings::aimbot::prediction_color),
			F("pr.dot_size",        settings::aimbot::prediction_size),

			// ---- esp (enemy = hostile priority) ----
			B("esp.box",            settings::visuals::hostile.box),
			S("esp.box_type",       settings::visuals::hostile.box_style, C_BOXTY),
			B("esp.box_fill",       settings::visuals::hostile.box_fill),
			B("esp.username",       settings::visuals::hostile.username),
			S("esp.username_type",  settings::visuals::hostile.username_type, C_UTYPE),
			S("esp.username_pos",   settings::visuals::hostile.username_position, C_UPOS),
			S("esp.name_transform", settings::visuals::hostile.name_transform, C_TRANS),
			B("esp.distance",       settings::visuals::hostile.distance),
			B("esp.tool",           settings::visuals::hostile.tool),
			B("esp.healthbar",      settings::visuals::hostile.healthbar),
			S("esp.healthbar_style",settings::visuals::hostile.healthbar_style, C_HBSTY),
			B("esp.healthbar_text", settings::visuals::hostile.healthbar_text),
			B("esp.tracers",        settings::visuals::hostile.tracers),
			S("esp.tracer_origin",  settings::visuals::hostile.tracers_origin, C_TRORIG),
			F("esp.tracer_thickness",settings::visuals::hostile.tracers_thickness),
			B("esp.skeleton",       settings::visuals::hostile.skeleton),
			B("esp.head_dot",       settings::visuals::hostile.head_dot),
			S("esp.head_type",      settings::visuals::hostile.head_type, C_HTYPE),
			F("esp.head_size",      settings::visuals::hostile.head_dot_size),
			B("esp.arrows",         settings::visuals::hostile.offscreen_arrows),
			F("esp.arrow_size",     settings::visuals::hostile.offscreen_arrows_size),
			F("esp.arrow_radius",   settings::visuals::hostile.offscreen_arrows_radius),
			B("esp.corpse",         settings::visuals::hostile.corpse),

			// ---- overlays ----
			B("ov.box_grad",        settings::visuals::hostile.box_gradient),
			S("ov.grad_type",       settings::visuals::hostile.box_gradient_type, C_GRADT),
			U("ov.grad_pulse",      ap_box_pulse),
			B("ov.fill_grad",       settings::visuals::hostile.box_fill_gradient),
			U("ov.fill_pulse",      ap_fill_pulse),
			C("ov.fill_color",      settings::visuals::hostile.box_fill_colour),
			C("ov.fill_mid",        settings::visuals::hostile.box_fill_colour_mid),
			C("ov.fill_low",        settings::visuals::hostile.box_fill_colour_low),
			B("ov.user_grad",       settings::visuals::hostile.username_gradient),
			S("ov.user_grad_type",  settings::visuals::hostile.username_gradient_type, C_GRADT),
			F("ov.user_grad_speed", settings::visuals::hostile.username_gradient_speed),
			C("ov.user_color2",     settings::visuals::hostile.username_colour_two),
			U("ov.hb_grad",         ap_hb_grad),
			U("ov.hb_pulse",        ap_hb_pulse),
			B("ov.chams",           settings::visuals::hostile.chams),
			B("ov.chams_fade",      settings::visuals::hostile.chams_fade),
			F("ov.fade_speed",      settings::visuals::hostile.chams_fade_speed),
			B("ov.chams_corpse",    settings::visuals::hostile.chams_corpse),
			B("ov.corpse_names",    settings::visuals::hostile.corpse_names),
			B("ov.trails",          settings::visuals::hostile.movement_trails),
			F("ov.trails_thickness",settings::visuals::hostile.movement_trails_thickness),
			F("ov.trails_length",   settings::visuals::hostile.movement_trails_length),
			B("ov.va_lines",        settings::visuals::hostile.view_angle_lines),
			F("ov.lines_thickness", settings::visuals::hostile.view_angle_lines_thickness),
			F("ov.lines_length",    settings::visuals::hostile.view_angle_lines_length),

			// ---- filters (global visuals) ----
			B("fl.team",            settings::visuals::teamcheck),
			B("fl.knock",           settings::visuals::knock_check),
			B("fl.alive",           settings::visuals::alive_check),
			B("fl.ragdoll",         settings::visuals::ragdoll_check),
			B("fl.dist_limit",      settings::visuals::distance_check),
			F("fl.max_dist",        settings::visuals::max_distance),
			F("fl.font_size",       settings::visuals::esp_font_size),

			// ---- highlights ----
			B("hl.enemy",           settings::visuals::enemy_highlight),
			C("hl.enemy_color",     settings::visuals::enemy_highlight_colour),
			B("hl.friendly",        settings::visuals::friendly_highlight),
			C("hl.friendly_color",  settings::visuals::friendly_highlight_colour),

			// ---- exploits (only the ones that back a real setting) ----
			B("ex.typing",          settings::typingcheck::enabled),
			B("ex.no_recoil",       settings::aimbot::recoil_control),
			B("ex.fps_unlock",      settings::misc::unlock_fps),

			// ---- colours ----
			C("col.enemy_box",      settings::visuals::hostile.colour),
			C("col.enemy_fill",     settings::visuals::hostile.box_fill_colour),
			C("col.friendly_box",   settings::visuals::friendly.colour),
			C("col.friendly_fill",  settings::visuals::friendly.box_fill_colour),
			C("col.skeleton",       settings::visuals::hostile.skeleton_colour),
			C("col.tracer",         settings::visuals::hostile.tracers_colour),
			C("col.username",       settings::visuals::hostile.username_colour),
			C("col.distance",       settings::visuals::hostile.distance_colour),
			C("col.health_high",    settings::visuals::hostile.healthbar_colour),
			C("col.health_mid",     settings::visuals::hostile.healthbar_colour_mid),
			C("col.health_low",     settings::visuals::hostile.healthbar_colour_low),
			C("col.chams",          settings::visuals::hostile.chams_colour),
			C("col.trail",          settings::visuals::hostile.movement_trails_colour),
			C("col.arrow",          settings::visuals::hostile.offscreen_arrows_colour),
			C("col.corpse",         settings::visuals::hostile.corpse_colour),

			// ---- settings / interface ----
			B("ui.vsync",           settings::vsync),
			B("ui.streamproof",     settings::streamproof),
			B("ui.cursor",          settings::stealth::cursor_in_capture),
			I("ui.panic_key",       settings::stealth::panic_key),
		};

#undef B
#undef I
#undef F
#undef MS
#undef C
#undef S
#undef U

		/* ---------------------------------------------------- apply */

		void hex_to_rgba(const std::string& s, float* out)
		{
			auto nib = [](char c) -> int
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			out[0] = out[1] = out[2] = 1.f;
			out[3] = 1.f;
			if (s.size() < 7 || s[0] != '#') return;
			const int r = nib(s[1]), r2 = nib(s[2]), g = nib(s[3]), g2 = nib(s[4]), b = nib(s[5]), b2 = nib(s[6]);
			if (r < 0 || r2 < 0 || g < 0 || g2 < 0 || b < 0 || b2 < 0) return;
			out[0] = (r * 16 + r2) / 255.f;
			out[1] = (g * 16 + g2) / 255.f;
			out[2] = (b * 16 + b2) / 255.f;
			if (s.size() >= 9)
			{
				const int a1 = nib(s[7]), a2 = nib(s[8]);
				if (a1 >= 0 && a2 >= 0) out[3] = (a1 * 16 + a2) / 255.f;
			}
		}

		std::string rgba_to_hex(const float* c)
		{
			auto byte = [](float v) -> int
			{
				int i = (int)(v * 255.f + 0.5f);
				if (i < 0) i = 0;
				if (i > 255) i = 255;
				return i;
			};
			char out[11];
			if (c[3] < 0.999f)
				std::snprintf(out, sizeof(out), "#%02x%02x%02x%02x", byte(c[0]), byte(c[1]), byte(c[2]), byte(c[3]));
			else
				std::snprintf(out, sizeof(out), "#%02x%02x%02x", byte(c[0]), byte(c[1]), byte(c[2]));
			return out;
		}

		void apply_entry(const Entry& e, const json::Value& v)
		{
			if (e.custom) { e.custom(v); return; }
			switch (e.kind)
			{
			case K_BOOL:  *(bool*)e.target = v.as_bool(); break;
			case K_INT:
			case K_MS:    *(int*)e.target = (int)v.as_num(); break;
			case K_FLOAT: *(float*)e.target = (float)v.as_num(); break;
			case K_COLOR: hex_to_rgba(v.as_str(), (float*)e.target); break;
			case K_COMBO:
			{
				int out = 0;
				if (e.combo)
				{
					const std::string s = v.as_str();
					for (const ComboVal* c = e.combo; c->s; ++c)
						if (s == c->s) { out = c->v; break; }
				}
				*(int*)e.target = out;
				break;
			}
			}
		}

		// the two flat maps the panel edits, keyed by the site's own names
		std::unordered_map<std::string, std::string> g_params_raw; // for cf.name etc.

		void apply_config(const json::Value& msg)
		{
			if (const json::Value* feats = msg.find("features"))
			{
				if (feats->is_obj())
				{
					for (size_t i = 0; i < feats->items.size(); ++i)
					{
						for (const Entry& e : kMap)
							if (feats->keys[i] == e.key) { apply_entry(e, feats->items[i]); break; }
					}
				}
			}
			if (const json::Value* params = msg.find("params"))
			{
				if (params->is_obj())
				{
					for (size_t i = 0; i < params->items.size(); ++i)
					{
						for (const Entry& e : kMap)
							if (params->keys[i] == e.key) { apply_entry(e, params->items[i]); break; }
						if (params->items[i].is_str()) g_params_raw[params->keys[i]] = params->items[i].as_str();
					}
				}
			}
		}

		/* full current state, same wire shape the browser pushes with set.
		 * Sent once after the first config apply so the panel shows the real
		 * values instead of defaults for keys the user never touched. */
		std::string state_json()
		{
			json::Value features = json::Value::mkobj();
			json::Value params = json::Value::mkobj();

			for (const Entry& e : kMap)
			{
				if (e.custom)
				{
					const std::string key = e.key;
					if (key == "tgt.part")
					{
						const int m = settings::aimbot::target_parts;
						params.put(key, json::Value::mk(std::string(m == 0x7F ? "random" : (m & 0x02 ? "chest" : "head"))));
					}
					else if (key == "tgt.air_part")
					{
						const int m = settings::aimbot::air_parts;
						params.put(key, json::Value::mk(std::string(m == 0x7F ? "random" : (m & 0x02 ? "chest" : "head"))));
					}
					else if (key == "ov.grad_pulse")
						features.put(key, json::Value::mk(settings::visuals::hostile.box_pulse_speed > 0.f));
					else if (key == "ov.fill_pulse")
						features.put(key, json::Value::mk(settings::visuals::hostile.box_fill_pulse_speed > 0.f));
					else if (key == "ov.hb_pulse")
						features.put(key, json::Value::mk(settings::visuals::hostile.healthbar_pulse_speed > 0.f));
					else if (key == "ov.hb_grad")
						features.put(key, json::Value::mk(settings::visuals::hostile.healthbar_gradient_type != 0));
					continue;
				}
				switch (e.kind)
				{
				case K_BOOL:   features.put(e.key, json::Value::mk(*(bool*)e.target)); break;
				case K_INT:
				case K_MS:     params.put(e.key, json::Value::mk((double)*(int*)e.target)); break;
				case K_FLOAT:  params.put(e.key, json::Value::mk((double)*(float*)e.target)); break;
				case K_COLOR:  params.put(e.key, json::Value::mk(rgba_to_hex((const float*)e.target))); break;
				case K_COMBO:
				{
					std::string s;
					if (e.combo)
					{
						const int cur = *(int*)e.target;
						for (const ComboVal* c = e.combo; c->s; ++c)
							if (c->v == cur) { s = c->s; break; }
					}
					params.put(e.key, json::Value::mk(s));
					break;
				}
				}
			}

			json::Value msg = json::Value::mkobj();
			msg.put("type", json::Value::mk(std::string("set")));
			msg.put("features", std::move(features));
			msg.put("params", std::move(params));
			return json::dump(msg);
		}

		/* ----------------------------------------------- ws transport */

		struct Site
		{
			HINTERNET session = nullptr, conn = nullptr, ws = nullptr;
			std::mutex sendMtx;

			bool open(const std::wstring& token)
			{
				session = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
				if (!session) return false;
				conn = WinHttpConnect(session, kEndpoint, INTERNET_DEFAULT_HTTPS_PORT, 0);
				if (!conn) return false;

				const std::wstring path = L"/api/ws?token=" + token;
				HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
					WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
				if (!req) return false;
				int timeout = 10000; // TLS+upgrade can take a moment; retries handle the rest
				WinHttpSetOption(req, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

				bool ok = WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
					WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
					WinHttpReceiveResponse(req, nullptr);
				if (ok) ws = WinHttpWebSocketCompleteUpgrade(req, 0);
				WinHttpCloseHandle(req);
				return ws != nullptr;
			}

			bool send(const std::string& m)
			{
				if (!ws) return false;
				std::lock_guard<std::mutex> lock(sendMtx);
				return WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
					(PVOID)m.data(), (DWORD)m.size()) == ERROR_SUCCESS;
			}

			// 1 = message, 0 = closed, -1 = error. Blocks until data arrives -
			// must run on its own thread (see the file header).
			int recv(std::string& out)
			{
				out.clear();
				if (!ws) return -1;
				BYTE buf[8192];
				for (;;)
				{
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

			void close()
			{
				if (ws) { WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0); WinHttpCloseHandle(ws); ws = nullptr; }
				if (conn) { WinHttpCloseHandle(conn); conn = nullptr; }
				if (session) { WinHttpCloseHandle(session); session = nullptr; }
			}
		};

		/* ---------------------------------------------- players report */

		std::string players_json()
		{
			json::Value list = json::Value::mkarr();

			const auto snap = cache::get_players_snap();
			const auto local = cache::get_local_snap();
			const float lx = local ? local->position.x : 0.f;
			const float ly = local ? local->position.y : 0.f;
			const float lz = local ? local->position.z : 0.f;
			const std::uint64_t lteam = local ? local->team : 0;

			if (snap)
			{
				for (const auto& p : *snap)
				{
					const std::uint64_t addr = p.instance.address;
					if (!addr) continue;
					json::Value row = json::Value::mkobj();
					char uid[32];
					std::snprintf(uid, sizeof(uid), "%llu", (unsigned long long)addr);
					row.put("uid", json::Value::mk(std::string(uid)));
					row.put("name", json::Value::mk(p.display_name.empty() ? p.name : p.display_name));
					row.put("dn", json::Value::mk(p.display_name));
					row.put("acc", json::Value::mk(p.name));
					row.put("health", json::Value::mk((double)p.health));
					row.put("mh", json::Value::mk((double)p.max_health));
					row.put("team", json::Value::mk(lteam != 0 && p.team == lteam));
					row.put("x", json::Value::mk((double)p.position.x));
					row.put("y", json::Value::mk((double)p.position.y));
					row.put("z", json::Value::mk((double)p.position.z));
					const float dx = p.position.x - lx, dy = p.position.y - ly, dz = p.position.z - lz;
					row.put("dist", json::Value::mk((double)std::sqrt(dx * dx + dy * dy + dz * dz)));
					list.items.push_back(std::move(row));
				}
			}

			json::Value loc = json::Value::mkobj();
			loc.put("x", json::Value::mk((double)lx));
			loc.put("y", json::Value::mk((double)ly));
			loc.put("z", json::Value::mk((double)lz));

			// static geometry intersecting the player's plane, for the minimap
			json::Value walls = json::Value::mkarr();
			if (const auto parts = wallcheck::get_parts_snapshot())
			{
				struct WallRect { float d2, x, z, w, d; };
				std::vector<WallRect> near_walls;
				near_walls.reserve(parts->size());
				const float range = 400.f;
				for (const auto& p : *parts)
				{
					// geometry crossing the player's height band (floors at
					// other levels and ceilings would just cover the map)
					if (p.max.y < ly - 4.f || p.min.y > ly + 4.f) continue;
					const float dx = p.position.x - lx;
					const float dz = p.position.z - lz;
					const float d2 = dx * dx + dz * dz;
					if (d2 > range * range) continue;
					near_walls.push_back({ d2, p.min.x, p.min.z, p.max.x - p.min.x, p.max.z - p.min.z });
				}
				std::sort(near_walls.begin(), near_walls.end(),
					[](const WallRect& a, const WallRect& b) { return a.d2 < b.d2; });
				const size_t n = near_walls.size() < 90 ? near_walls.size() : 90;
				for (size_t i = 0; i < n; ++i)
				{
					const auto& w = near_walls[i];
					json::Value row = json::Value::mkobj();
					row.put("x", json::Value::mk((double)w.x));
					row.put("z", json::Value::mk((double)w.z));
					row.put("w", json::Value::mk((double)w.w));
					row.put("d", json::Value::mk((double)w.d));
					walls.items.push_back(std::move(row));
				}
			}

			json::Value msg = json::Value::mkobj();
			msg.put("type", json::Value::mk(std::string("players")));
			msg.put("local", std::move(loc));
			msg.put("walls", std::move(walls));
			msg.put("list", std::move(list));
			return json::dump(msg);
		}

		/* ----------------------------------------------- config list */

		std::string cfgs_json()
		{
			json::Value list = json::Value::mkarr();
			for (const auto& n : config::get_config_list_mem())
				list.items.push_back(json::Value::mk(n));
			json::Value msg = json::Value::mkobj();
			msg.put("type", json::Value::mk(std::string("cfgs")));
			msg.put("auto", json::Value::mk(config::get_autoload_mem()));
			msg.put("list", std::move(list));
			return json::dump(msg);
		}

		/* ------------------------------------------------ commands */

		void run_command(const std::string& cmd, Site& s)
		{
			// loader-owned verbs - it shares the token and handles them
			if (cmd == "load" || cmd == "unload" || cmd == "reinject" || cmd == "selfdestruct" || cmd == "close")
				return;

			if (cmd == "reset")
			{
				config::reset_to_defaults();
				notifications::add("settings reset");
				return;
			}

			if (cmd == "stopspectate")
			{
				playertools::ops::stop_spectate();
				return;
			}

			// "cfg_save:<name>" - the argument rides in the command itself so
			// panel buttons can't race the cf.name param push
			for (const auto& cv : { std::string("cfg_save:"), std::string("cfg_load:"), std::string("cfg_delete:") })
			{
				if (cmd.rfind(cv, 0) == 0)
				{
					const std::string name = cmd.substr(cv.size());
					if (name.empty()) return;
					bool ok = true;
					if (cv == "cfg_save:")  ok = config::save_config_mem(name);
					if (cv == "cfg_load:")  ok = config::load_config_mem(name);
					if (cv == "cfg_delete:") ok = config::delete_config_mem(name);
					notifications::add(ok ? "config " + cv.substr(4, cv.size() - 5) + " ok" : "config op failed");
					s.send(cfgs_json());   // refresh the panel list either way
					return;
				}
			}

			if (cmd == "cfg_list") { s.send(cfgs_json()); return; }

			// autoload: the config that loads on every run (registry-backed)
			if (cmd.rfind("cfg_autoload:", 0) == 0)
			{
				const std::string name = cmd.substr(12);
				if (!name.empty() && config::config_exists_mem(name))
				{
					config::set_autoload_mem(name);
					notifications::add("autoload set: " + name);
				}
				s.send(cfgs_json());
				return;
			}
			if (cmd == "cfg_autoload_clear")
			{
				config::clear_autoload_mem();
				notifications::add("autoload cleared");
				s.send(cfgs_json());
				return;
			}

			auto name = [](const char* k) -> std::string
			{
				const auto it = g_params_raw.find(k);
				return it == g_params_raw.end() ? std::string() : it->second;
			};

			if (cmd == "cfg_save") { config::save_config_mem(name("cf.name")); notifications::add("config saved"); s.send(cfgs_json()); return; }
			if (cmd == "cfg_load") { config::load_config_mem(name("cf.name")); notifications::add("config loaded"); return; }
			if (cmd == "cfg_delete") { config::delete_config_mem(name("cf.name")); notifications::add("config deleted"); s.send(cfgs_json()); return; }
			if (cmd == "cfg_export") { config::export_config_to_clipboard(); notifications::add("config exported"); return; }
			if (cmd == "cfg_insert") { config::import_config_from_clipboard(); notifications::add("config imported"); return; }
			if (cmd == "cfg_share")
			{
				std::string url;
				if (config::share_config_cloud(url)) notifications::add("config shared");
				else notifications::add("share failed");
				return;
			}
			if (cmd == "cfg_fetch")
			{
				const std::string id = name("cf.paste_id");
				if (id.empty()) { notifications::add("no share id"); return; }
				std::string body;
				if (config::fetch_config_cloud_body(id, body) && config::apply_fetched_config(body))
					notifications::add("config fetched");
				else
					notifications::add("fetch failed");
				return;
			}

			// "<action>:<instance address>" from the world page
			const size_t colon = cmd.find(':');
			if (colon != std::string::npos)
			{
				const std::string action = cmd.substr(0, colon);
				const std::uint64_t addr = std::strtoull(cmd.c_str() + colon + 1, nullptr, 10);
				if (!addr) return;
				if (action == "teleport")
				{
					if (!playertools::ops::teleport_to(addr)) notifications::add("teleport failed");
				}
				else if (action == "spectate")
				{
					if (playertools::ops::is_spectating(addr)) playertools::ops::stop_spectate();
					else playertools::ops::spectate(addr);
				}
			}
		}

		/* --------------------------------------------------- threads */

		Site* g_site = nullptr;
		std::atomic<bool> g_up{ false };

		void rx_loop(Site* s)
		{
			std::string msg;
			bool state_pushed = false;
			for (;;)
			{
				const int r = s->recv(msg);
				if (r != 1) break;

				json::Value m;
				if (!json::parse(msg, m) || !m.is_obj()) continue;
				const json::Value* t = m.find("type");
				if (!t) continue;
				const std::string type = t->as_str();

				if (type == "config")
				{
					apply_config(m);
					if (!state_pushed)
					{
						state_pushed = true;
						// apply the account's autoload config once per run (it
						// wins over the stored live state), then mirror the
						// result so the panel shows the loaded setup
						static bool s_autoload_done = false;
						if (!s_autoload_done)
						{
							s_autoload_done = true;
							const std::string al = config::get_autoload_mem();
							if (!al.empty() && config::load_autoload_mem())
								notifications::add("autoloaded: " + al);
						}
						s->send(state_json());
					}
				}
				else if (type == "command")
				{
					const json::Value* c = m.find("cmd");
					if (c) run_command(c->as_str(), *s);
				}
				// "commands" (history) is deliberately ignored: replayed
				// old commands must not re-fire on reconnect.
			}
			g_up = false;
		}
	}

	void run()
	{
		const std::wstring token = reg_str(L"Token");
		if (token.empty()) return;

		std::string last_players;

		for (;;)
		{
			Site* s = new Site();
			if (!s->open(token))
			{
				delete s;
				std::this_thread::sleep_for(std::chrono::seconds(3));
				continue;
			}
			g_site = s;
			g_up = true;

			std::thread rx(rx_loop, s);
			while (g_up)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1200));
				std::string pj = players_json();
				if (pj != last_players) { s->send(pj); last_players = std::move(pj); }
			}
			rx.join();

			g_site = nullptr;
			s->close();
			delete s;
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}
	}
}
