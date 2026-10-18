#include "aimbot.h"
#include <features/system/playerlist/playerlist.h>
#include <features/combat/rivals/rivals.h>
#include <features/visuals/geometry/visualcheck.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "winmm.lib")
#include <sdk/game/game.h>
#include <features/system/settings/settings.h>
#include <sdk/math/math.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <features/system/keybind/keybind.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <sdk/cache/core/frame.h>
#include <features/visuals/geometry/wallcheck.h>
#include <ui/menu/settings/functions.h>
#include <features/system/notifications/notifications.h>


static float get_world_distance_to_entity(const cache::entity_t& entity, const math::vector3& cam_pos);

static math::vector3 get_velocity(const cache::entity_t& entity)
{
	if (entity.velocity.x != 0.0f || entity.velocity.y != 0.0f || entity.velocity.z != 0.0f)
	{
		return entity.velocity;
	}

	if (entity.hrp_prim_addr >= 0x10000 && entity.hrp_prim_addr < 0x7FFFFFFFFFFFull)
	{
		return memory->read<math::vector3>(entity.hrp_prim_addr + Offsets::Primitive::AssemblyLinearVelocity);
	}

	if (entity.humanoid_root_part.address != 0)
	{
		rbx::c_primitive prim = entity.humanoid_root_part.get_primitive();
		if (prim.address != 0)
		{
			return memory->read<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity);
		}
	}

	return entity.velocity;
}

static float get_workspace_gravity()
{
	try
	{
		if (!game::datamodel || game::datamodel->address == 0)
			return 196.2f;

		uintptr_t workspace = memory->read<uintptr_t>(game::datamodel->address + Offsets::DataModel::Workspace);
		if (workspace == 0)
			return 196.2f;

		uintptr_t container = memory->read<uintptr_t>(workspace + Offsets::Workspace::World);
		if (container == 0)
			return 196.2f;

		float gravity = memory->read<float>(container + Offsets::World::Gravity);
		if (gravity <= 0.0f)
			return 196.2f;

		return gravity;
	}
	catch (...)
	{
		return 196.2f;
	}
}

// Slow-changing shared reads, cached per tick-chain. Gravity/camera-FOV move
// slowly; the camera position changes per frame - without these caches every
// BONE of every ENTITY re-read them (N players x M bones RPMs per tick).
static float get_fov_scale();

static float cached_gravity()
{
	static thread_local float s_gravity = 196.2f;
	static thread_local std::chrono::steady_clock::time_point s_at{};
	const auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration<float>(now - s_at).count() > 2.0f)
	{
		s_gravity = get_workspace_gravity();
		s_at = now;
	}
	return s_gravity;
}

static float cached_fov_scale()
{
	static thread_local float s_scale = 1.0f;
	static thread_local std::chrono::steady_clock::time_point s_at{};
	const auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration<float>(now - s_at).count() > 0.1f)
	{
		s_scale = get_fov_scale();
		s_at = now;
	}
	return s_scale;
}

static math::vector3 cached_camera_position()
{
	static thread_local math::vector3 s_pos{};
	static thread_local std::chrono::steady_clock::time_point s_at{};
	const auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration<float>(now - s_at).count() > 0.008f && game::camera >= 0x10000)
	{
		math::vector3 p = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
		if (std::isfinite(p.x + p.y + p.z))
			s_pos = p;
		s_at = now;
	}
	return s_pos;
}

static math::vector3 smooth_target_velocity(const cache::entity_t& entity)
{
	struct vel_state {
		math::vector3 vel{};
		math::vector3 last_pos{};
		bool init = false;
		std::chrono::steady_clock::time_point last_time{};
	};
	static thread_local std::unordered_map<std::uint64_t, vel_state> s_vel;

	auto now = std::chrono::steady_clock::now();
	// prune entries that have not been seen for 3s instead of wiping the
	// whole table - a wholesale clear on the 256-entry cap also wiped the
	// velocity state of live targets (briefly degraded prediction)
	if (s_vel.size() > 256)
	{
		for (auto it = s_vel.begin(); it != s_vel.end();)
		{
			if (now - it->second.last_time > std::chrono::seconds(3))
				it = s_vel.erase(it);
			else
				++it;
		}
		if (s_vel.size() > 256)
			s_vel.clear();
	}
	vel_state& st = s_vel[entity.instance.address];

	math::vector3 raw = get_velocity(entity);
	if (!std::isfinite(raw.x + raw.y + raw.z))
	{
		raw = st.init ? st.vel : math::vector3{};
	}

	if (!st.init)
	{
		st.vel = raw;
		st.last_pos = entity.position;
		st.last_time = now;
		st.init = true;
		return raw;
	}

	float dt = std::chrono::duration<float>(now - st.last_time).count();
	st.last_time = now;

	if (dt > 0.001f && dt < 0.5f)
	{
		math::vector3 fd_vel = (entity.position - st.last_pos) / dt;
		if (std::isfinite(fd_vel.x + fd_vel.y + fd_vel.z))
		{
			float blend = std::clamp(dt * 12.0f, 0.0f, 1.0f);
			st.vel = math::vector3::lerp(raw, fd_vel, blend);
		}
	}

	st.last_pos = entity.position;
	return st.vel;
}

static math::vector3 apply_prediction(const cache::entity_t& entity, const math::vector3& position)
{
	math::vector3 velocity = smooth_target_velocity(entity);
	velocity = velocity * settings::aimbot::velocity_multiplier;

	// Range: prediction only applies within this distance of the camera
	if (settings::aimbot::prediction_range > 0.f && game::camera >= 0x10000)
	{
		const math::vector3 cam_pos = cached_camera_position();
		if ((position - cam_pos).length() > settings::aimbot::prediction_range)
			return position;
	}

	const bool in_air = std::abs(velocity.y) > 1.5f;
	// higher slider value = LESS prediction, lower value = MORE prediction
	float prediction_factor_x = (100.f - settings::aimbot::prediction_x) * 0.01f;
	float prediction_factor_y = (100.f - settings::aimbot::prediction_y) * 0.01f;

	// per-axis air prediction overrides while jumping/falling
	if (settings::aimbot::air_prediction_enabled && in_air)
	{
		prediction_factor_x = (100.f - settings::aimbot::air_prediction_x) * 0.01f;
		prediction_factor_y = (100.f - settings::aimbot::air_prediction_y) * 0.01f;
	}

	math::vector3 predicted_offset;
	predicted_offset.x = velocity.x * prediction_factor_x;
	predicted_offset.y = velocity.y * prediction_factor_y;
	predicted_offset.z = velocity.z * prediction_factor_x;

	if (velocity.y > 1.0f && settings::aimbot::jump_prediction)
	{
		float t = settings::aimbot::jump_prediction_value * 0.01f;
		float gravity = cached_gravity();
		// keep the per-axis y factor coherent - the jump arc replaces the raw
		// velocity term but still respects the y prediction strength
		predicted_offset.y = ((velocity.y * t) - (0.5f * gravity * t * t)) * prediction_factor_y;
	}
	else if (velocity.y < -1.0f && settings::aimbot::fall_prediction)
	{
		float t = settings::aimbot::fall_prediction_value * 0.01f;
		float gravity = cached_gravity();
		predicted_offset.y = ((velocity.y * t) - (0.5f * gravity * t * t)) * prediction_factor_y;
	}

	// Ping interpolation (toggle): lead the target by one-way latency so the
	// aim meets them where they WILL be when the input lands. No server-side
	// ping source exists externally - a conservative regional estimate is
	// used; wire a real ping offset here when one is dumped.
	if (settings::aimbot::ping_interpolation)
	{
		constexpr float k_assumed_ping_ms = 60.f;
		const float lead = k_assumed_ping_ms / 2000.f; // one-way, seconds
		predicted_offset.x += velocity.x * lead;
		predicted_offset.y += velocity.y * lead;
		predicted_offset.z += velocity.z * lead;
	}

	// Adaptive: scales the correction by how fast the target moves,
	// converging toward the adapted strength at the adapt speed rate.
	if (settings::aimbot::adaptive_prediction && settings::aimbot::adapt_strength > 0.f)
	{
		static thread_local float s_adapt = 1.0f;
		static thread_local std::chrono::steady_clock::time_point s_adapt_at = std::chrono::steady_clock::now();
		auto adapt_now = std::chrono::steady_clock::now();
		float adt = std::chrono::duration<float>(adapt_now - s_adapt_at).count();
		s_adapt_at = adapt_now;

		const float speed_norm = std::clamp(velocity.length() / 50.0f, 0.0f, 1.0f);
		const float target_adapt = 1.0f + (settings::aimbot::adapt_strength - 1.0f) * speed_norm;
		const float k = std::clamp(1.0f - std::exp(-(settings::aimbot::adapt_speed / 20.0f) * (std::max)(adt, 0.001f)), 0.0f, 1.0f);
		s_adapt += (target_adapt - s_adapt) * k;

		predicted_offset = predicted_offset * s_adapt;
	}

	return position + predicted_offset;
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

static float get_distance_from_center(const math::vector2& point, const math::vector2& position)
{
	float dx{ position.x - point.x };
	float dy{ position.y - point.y };
	return std::sqrt(dx * dx + dy * dy);
}

static std::vector<std::string> collect_aim_bones(int mask, const cache::entity_t& entity)
{
	std::vector<std::string> out;
	if (mask == 0) mask = 0x01;
	if (mask & 0x01) out.push_back("Head");
	if (mask & 0x02)
	{
		auto v = bodyparts::get_part_names(entity, "Torso");
		out.insert(out.end(), v.begin(), v.end());
	}
	if (mask & 0x04) out.push_back("HumanoidRootPart");
	if (mask & 0x08)
	{
		auto v = bodyparts::get_part_names(entity, "LeftArm");
		out.insert(out.end(), v.begin(), v.end());
	}
	if (mask & 0x10)
	{
		auto v = bodyparts::get_part_names(entity, "RightArm");
		out.insert(out.end(), v.begin(), v.end());
	}
	if (mask & 0x20)
	{
		auto v = bodyparts::get_part_names(entity, "LeftLeg");
		out.insert(out.end(), v.begin(), v.end());
	}
	if (mask & 0x40)
	{
		auto v = bodyparts::get_part_names(entity, "RightLeg");
		out.insert(out.end(), v.begin(), v.end());
	}
	if (out.empty()) out.push_back("Head");
	return out;
}

static bool get_target_point_for_entity
(
	const cache::entity_t& entity,
	const math::matrix4& view,
	const math::vector2& dims,
	float fov,
	float& out_distance,
	math::vector2& out_screen,
	int parts_mask_override = -1,
	bool check_fov = true,
	math::vector3* out_world = nullptr
)
{
	math::vector2 cursor_pos = dims * 0.5f;
	POINT cursor{};
	HWND roblox_hwnd = game::get_roblox_window();
	if (GetCursorPos(&cursor) && roblox_hwnd)
	{
		ScreenToClient(roblox_hwnd, &cursor);
		if (cursor.x >= 0 && cursor.y >= 0 && cursor.x <= dims.x && cursor.y <= dims.y)
		{
			cursor_pos = math::vector2{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };
		}
	}

	struct aim_hit_t { float dist; math::vector2 screen; math::vector3 world; };
	std::vector<aim_hit_t> hits;

	float best_distance = FLT_MAX;
	bool found = false;

	auto try_part = [&](const char* name)
		{
			math::vector3 pos_3d{};
			if (!bodyparts::get_part_position(entity, name, pos_3d))
			{
				return;
			}

			if (settings::aimbot::enable_prediction)
			{
				pos_3d = apply_prediction(entity, pos_3d);
			}

			math::vector2 pos_2d{};

			if (!game::visualengine->world_to_screen(view, dims, pos_3d, pos_2d))
			{
				return;
			}

			const float distance = get_distance_from_center(cursor_pos, pos_2d);

			// use_fov: the FOV limit is optional - when off, any on-screen
			// bone is a candidate regardless of cursor distance
			float current_fov = fov;
			if (settings::aimbot::dynamic_fov)
			{
				current_fov *= cached_fov_scale();
			}

			if (check_fov && settings::aimbot::use_fov && distance > current_fov)
			{
				return;
			}

		if (distance < best_distance)
		{
			best_distance = distance;
		}
		hits.push_back({ distance, pos_2d, pos_3d });
		found = true;
	};

	int aim_mask = parts_mask_override != -1 ? parts_mask_override : settings::aimbot::target_parts;
	for (const std::string& bone : collect_aim_bones(aim_mask, entity))
		try_part(bone.c_str());

	if (!found)
	{
		try_part("Head");
		if (!found)
			try_part("HumanoidRootPart");
	}

	if (!found)
	{
		return false;
	}

	// Point Amount: average the N closest bone points (1 = closest only)
	const int amount = std::clamp(settings::aimbot::point_amount, 1, (int)hits.size());
	if (amount <= 1 || hits.size() == 1)
	{
		out_screen = hits[0].screen;
		if (out_world) *out_world = hits[0].world;
	}
	else
	{
		std::sort(hits.begin(), hits.end(), [](const aim_hit_t& a, const aim_hit_t& b) { return a.dist < b.dist; });
		float sx = 0.f, sy = 0.f, wx = 0.f, wy = 0.f, wz = 0.f;
		for (int i = 0; i < amount; i++)
		{
			sx += hits[i].screen.x;
			sy += hits[i].screen.y;
			wx += hits[i].world.x;
			wy += hits[i].world.y;
			wz += hits[i].world.z;
		}
		out_screen = math::vector2{ sx / amount, sy / amount };
		if (out_world) *out_world = math::vector3{ wx / amount, wy / amount, wz / amount };
	}

	out_distance = best_distance;
	return true;
}

static void mouse_aim(const math::vector2& target, const math::vector2& dims)
{
	math::vector2 center = dims * 0.5f;
	math::vector2 screen_pos = center;

	HWND roblox_hwnd = game::get_roblox_window();
	POINT cursor{};
	bool cursor_valid = false;
	if (GetCursorPos(&cursor) && roblox_hwnd)
	{
		ScreenToClient(roblox_hwnd, &cursor);
		if (cursor.x >= 0 && cursor.y >= 0 && cursor.x <= dims.x && cursor.y <= dims.y)
		{
			screen_pos = math::vector2{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };
			cursor_valid = true;
		}
	}

	// Adaptive pacing: measure how fast the user physically moves the mouse
	// (OS cursor motion minus our own injected counts - both pass through the
	// same Windows transform) and bias the aim speed slightly toward their
	// natural pace. Normalizes the feel across mice without asking for DPI.
	// Subtle by design: max ~±12%.
	static thread_local float s_last_cur_x = 0.f, s_last_cur_y = 0.f;
	static thread_local bool s_cur_valid = false;
	static thread_local int s_last_inj_x = 0, s_last_inj_y = 0;
	static thread_local float s_motion_scale = 1.0f;
	static thread_local float s_avg_speed = 1600.f;
	static thread_local auto s_meas_at = std::chrono::steady_clock::now();
	{
		auto meas_now = std::chrono::steady_clock::now();
		float mdt = std::clamp(std::chrono::duration<float>(meas_now - s_meas_at).count(), 0.001f, 0.1f);
		s_meas_at = meas_now;

		if (cursor_valid && s_cur_valid)
		{
			const float pdx = screen_pos.x - s_last_cur_x - (float)s_last_inj_x;
			const float pdy = screen_pos.y - s_last_cur_y - (float)s_last_inj_y;
			const float inst_speed = std::sqrt(pdx * pdx + pdy * pdy) / mdt;

			// only learn while the user is actually moving the mouse
			if (inst_speed > 60.f)
				s_avg_speed += (inst_speed - s_avg_speed) * std::clamp(mdt * 1.5f, 0.0f, 0.3f);

			// ~1600 px/s cursor pace is the neutral point: slow hands get a
			// slightly softer aim, fast hands a slightly firmer one
			const float target = std::clamp(0.88f + (s_avg_speed / 1600.0f) * 0.12f, 0.88f, 1.12f);
			s_motion_scale += (target - s_motion_scale) * std::clamp(mdt * 2.0f, 0.0f, 1.0f);
		}
		s_last_cur_x = screen_pos.x;
		s_last_cur_y = screen_pos.y;
		s_cur_valid = cursor_valid;
		s_last_inj_x = 0;  // this tick has not injected yet - set after SendInput
		s_last_inj_y = 0;
	}

	float delta_x = target.x - screen_pos.x;
	float delta_y = target.y - screen_pos.y;

	// Offset: static screen offset on the aim point (humanized toggle)
	if (settings::aimbot::offset_enabled)
	{
		delta_x += settings::aimbot::offset_x;
		delta_y += settings::aimbot::offset_y;
	}

	const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
	// deadzone: within this range the aim settles instead of micro-adjusting
	// Deadzone: the area around the crosshair where the lock does nothing
	if (distance < (std::max)(settings::aimbot::aim_deadzone, 0.5f))
	{
		return;
	}

	static thread_local auto s_last_aim = std::chrono::steady_clock::now();
	auto aim_now = std::chrono::steady_clock::now();
	float aim_dt = std::clamp(std::chrono::duration<float>(aim_now - s_last_aim).count(), 0.001f, 0.1f);
	s_last_aim = aim_now;

	// ------------------------------------------------------------------------
	// Fully linear aim: the cursor always travels the straight line to the
	// target. Per-axis "aim speed" (0.01..1) drives every non-blatant mode -
	// each is the fraction of the remaining straight-line distance covered
	// per tick on that axis, scaled by 0.5 for extra smoothness. Blatant is
	// a hard 1:1 lock with no smoothing at all.
	// ------------------------------------------------------------------------
	float fraction_x, fraction_y;
	if (settings::aimbot::aim_mode == 2)
	{
		fraction_x = 1.0f;
		fraction_y = 1.0f;
	}
	else
	{
		fraction_x = std::clamp(settings::aimbot::aim_speed_x * 0.5f, 0.01f, 1.0f);
		fraction_y = std::clamp(settings::aimbot::aim_speed_y * 0.5f, 0.01f, 1.0f);
	}

		const float dt_frac = std::clamp(aim_dt / 0.008f, 0.1f, 8.0f);
		if (fraction_x < 1.0f)
			fraction_x = 1.0f - std::pow(1.0f - fraction_x, dt_frac);
		if (fraction_y < 1.0f)
			fraction_y = 1.0f - std::pow(1.0f - fraction_y, dt_frac);

		// Final-approach ease-out: in the last few pixels the per-tick step
		// tapers smoothly, so the aim settles onto the target instead of
		// micro-oscillating around the lock point.
		if (distance < 10.0f)
		{
			const float taper = std::clamp(distance / 10.0f, 0.15f, 1.0f);
			fraction_x *= taper;
			fraction_y *= taper;
		}

	// Humanize extras - humanized mode only, every piece behind its own toggle
	if (settings::aimbot::aim_mode == 0 && settings::aimbot::humanize)
	{
		// Stick assist: magnetism, not a lock. The pull ramps up as the
		// crosshair approaches the target and scales with strength - it
		// never snaps to a full 1:1 lock on its own; aim speed still
		// governs the base tracking at every distance.
		if (settings::aimbot::stick_assist && settings::aimbot::stick_strength > 0.01f)
		{
			const float pull_radius = settings::aimbot::stick_strength * 250.0f;
			if (distance < pull_radius)
			{
				const float nearness = 1.0f - (distance / pull_radius);   // 0 at the edge, 1 dead on
				const float pull = nearness * nearness * std::clamp(settings::aimbot::stick_strength, 0.0f, 1.0f);
				fraction_x = (std::min)(1.0f, fraction_x + (1.0f - fraction_x) * pull);
				fraction_y = (std::min)(1.0f, fraction_y + (1.0f - fraction_y) * pull);
			}
		}

		// miss chance
		{
			static thread_local std::random_device mrd; static thread_local std::mt19937 mgen(mrd());
			std::uniform_real_distribution<float> mdis(0.f, 100.f);
			if (mdis(mgen) < settings::aimbot::miss_chance) return;
		}
		// path wobble + jitter + overshoot (all optional)
		static thread_local float wobble_phase = 0.f;
		wobble_phase += 0.35f;
		{
			static thread_local std::random_device jrd; static thread_local std::mt19937 jgen(jrd());
			float j = settings::aimbot::human_jitter;
			std::normal_distribution<float> jdis(0.f, j * 0.5f);
			delta_x += jdis(jgen); delta_y += jdis(jgen);
		}
		delta_x += sinf(wobble_phase * 1.1f) * settings::aimbot::path_wobble * 0.5f;
		delta_y += cosf(wobble_phase * 0.9f) * settings::aimbot::path_wobble * 0.5f;
		float dist01 = std::clamp(distance / 300.f, 0.f, 1.f);
		delta_x += settings::aimbot::overshoot * dist01 * 0.4f;
		delta_y -= settings::aimbot::overshoot * dist01 * 0.2f;
	}

	// Fatigue (humanized toggle): aim precision degrades the longer you
	// continuously aim; releasing the key (gap > 200ms) decays it back down.
	if (settings::aimbot::aim_mode == 0 && settings::aimbot::fatigue)
	{
		static thread_local float s_fatigue = 0.f;
		static thread_local auto s_fatigue_at = std::chrono::steady_clock::now() - std::chrono::seconds(10);
		{
			auto f_now = std::chrono::steady_clock::now();
			float f_dt = std::chrono::duration<float>(f_now - s_fatigue_at).count();
			s_fatigue_at = f_now;
			if (f_dt > 0.2f)
				s_fatigue = (std::max)(0.f, s_fatigue - settings::aimbot::fatigue_decay * f_dt * 2.f);
			else
				s_fatigue = (std::min)(1.f, s_fatigue + settings::aimbot::fatigue_rate * aim_dt);
		}
		if (s_fatigue > 0.001f)
		{
			static thread_local float s_fatigue_phase = 0.f;
			s_fatigue_phase += 0.25f;
			delta_x += std::sinf(s_fatigue_phase * 1.7f) * settings::aimbot::fatigue_move * s_fatigue * 0.4f;
			delta_y += std::cosf(s_fatigue_phase * 1.3f) * settings::aimbot::fatigue_move * s_fatigue * 0.3f;
			delta_x += settings::aimbot::fatigue_offset * s_fatigue;
		}
	}

	// Shake (humanized toggle)
	if (settings::aimbot::aim_mode == 0 && settings::aimbot::shake)
	{
		float range = settings::aimbot::shake_value;
		float range_y = settings::aimbot::shake_z > 0.01f ? settings::aimbot::shake_z : range;
		static thread_local std::random_device rd;
		static thread_local std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dis(-range, range);
		std::uniform_real_distribution<float> dis_y(-range_y, range_y);
		delta_x += dis(gen);
		delta_y += dis_y(gen);
	}

	// Aim motion (humanized toggle): circular sway - like shake but a smooth
	// rotating ellipse, far less robotic than random jitter
	if (settings::aimbot::aim_mode == 0 && settings::aimbot::aim_motion)
	{
		static thread_local float s_motion_phase = 0.f;
		s_motion_phase += aim_dt * (std::max)(settings::aimbot::aim_motion_speed, 0.05f) * 6.2831853f;
		const float r = settings::aimbot::aim_motion_intensity;
		delta_x += std::cos(s_motion_phase) * r;
		delta_y += std::sin(s_motion_phase * 2.0f) * r * 0.6f;  // elliptical lissajous
	}

	// ------------------------------------------------------------------------
	// Mouse sync (fully automatic - nothing to configure):
	//   - Windows pointer-speed slider is read and honored (1:1 at 10/20),
	//     so injected counts behave like your physical mouse whatever your
	//     sensitivity setting is
	//   - Windows pointer acceleration ("Enhance pointer precision") is
	//     detected and compensated so fast flicks don't overshoot
	//   - The DPI slider below only SLIGHTLY biases the counts (15% of the
	//     raw dpi/800 ratio) - it refines the feel, it never rescales the aim
	// ------------------------------------------------------------------------
	static thread_local float s_sens_scale = 1.0f;
	static thread_local auto s_sens_at = std::chrono::steady_clock::now() - std::chrono::seconds(10);
	{
		auto now = std::chrono::steady_clock::now();
		if (now - s_sens_at >= std::chrono::milliseconds(250))
		{
			s_sens_at = now;
			float s = 1.0f;
			float fov_scale = cached_fov_scale();
			if (fov_scale > 0.01f && fov_scale < 100.0f)
				s = 1.0f / fov_scale;
			s_sens_scale = s;
		}
	}
	static thread_local float s_win_scale = 1.0f;
	static thread_local auto s_win_at = std::chrono::steady_clock::now() - std::chrono::seconds(5);
	{
		auto now = std::chrono::steady_clock::now();
		if (now - s_win_at >= std::chrono::seconds(5))
		{
			s_win_at = now;
			// SPI_GETMOUSESPEED returns the raw 1..20 slider; a speed of
			// 10 is 1:1, but the curve is non-linear (20 is 3.5x, not 2x),
			// so map it through the real table instead of speed/10.
			static constexpr int   k_speeds[11] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20 };
			static constexpr float k_mults[11] = { 0.03125f, 0.0625f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f };
			float s = 1.0f;
			int speed = 10;
			if (SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &speed, 0) && speed >= 1 && speed <= 20)
			{
				int best = 5;
				int best_delta = 0x7FFFFFFF;
				for (int i = 0; i < 11; ++i)
				{
					const int d = k_speeds[i] - speed;
					const int ad = d < 0 ? -d : d;
					if (ad < best_delta) { best_delta = ad; best = i; }
				}
				s = k_mults[best];
			}
			s_win_scale = s;
		}
	}
	static thread_local float s_accel_scale = 1.0f;
	static thread_local auto s_accel_at = std::chrono::steady_clock::now() - std::chrono::seconds(5);
	{
		auto now = std::chrono::steady_clock::now();
		if (now - s_accel_at >= std::chrono::seconds(5))
		{
			s_accel_at = now;
			// Windows: Enhance pointer precision - SPI_GETMOUSE returns
			// { threshold1, threshold2, acceleration }. A non-zero
			// acceleration level means the pointer ballistics amplify fast
			// movements; divide the step by an estimate of the applied gain
			// so fast flicks don't overshoot.
			float s = 1.0f;
			INT mouse_params[3] = { 0, 0, 0 };
			if (SystemParametersInfoA(SPI_GETMOUSE, 0, &mouse_params, 0))
			{
				if (mouse_params[2] > 0)
					s /= 1.5f * (float)mouse_params[2];
			}
			s_accel_scale = std::clamp(s, 0.2f, 1.0f);
		}
	}

	// raw remaining distance before the sensitivity chain scales it - the
	// anti-overshoot clamp below needs the true distance to the target
	const float rem_x = delta_x;
	const float rem_y = delta_y;

	delta_x *= settings::aimbot::mouse_sensitivity * s_sens_scale * s_win_scale * s_accel_scale * s_motion_scale;
	delta_y *= settings::aimbot::mouse_sensitivity * s_sens_scale * s_win_scale * s_accel_scale * s_motion_scale;

	// Straight-line step: a constant fraction of the remaining distance,
	// applied per axis so the path to the target stays linear.
	float step_x = delta_x * fraction_x;
	float step_y = delta_y * fraction_y;

	// Speed cap (humanized toggle): clamp the per-tick step to [min, max].
	// The min floor only applies well outside the landing zone so it never
	// fights the deadzone or the humanize noise (a low floor inside that band
	// turned into a forced-step oscillation - the "hard stick + shaking")
	if (settings::aimbot::aim_mode == 0 && settings::aimbot::speed_cap)
	{
		const float cap_max = (std::max)(settings::aimbot::max_speed_px, settings::aimbot::min_speed_px);
		const float cap_min = (std::min)(settings::aimbot::max_speed_px, settings::aimbot::min_speed_px);
		const float mag = std::sqrt(step_x * step_x + step_y * step_y);
		if (mag > 1e-4f)
		{
			float capped = mag;
			if (mag > cap_max)
				capped = cap_max;
			else if (mag < cap_min && distance > (std::max)(8.0f, cap_min * 3.0f))
				capped = cap_min;
			const float s = capped / mag;
			step_x *= s;
			step_y *= s;
		}
	}

	// Aim-assist slowdown (humanized): inside the radius, the tracking speed
	// is pulled toward the configured cap - the controller-assist "catch".
	// strength blends the natural step toward the capped step; at full grab
	// the aim tracks at exactly `assist_slowdown_speed` px/s. Because the cap
	// scales the whole step vector, the humanize noise near the target is
	// clamped to sub-pixel and dropped by rounding - the aim RESTS on the
	// target instead of shaking.
	if (settings::aimbot::aim_mode == 0 && settings::aimbot::assist_slowdown && settings::aimbot::assist_slowdown_strength > 0.01f)
	{
		const float radius = 60.0f;
		const float t = std::clamp(distance / radius, 0.0f, 1.0f);
		const float grab = (1.0f - t) * std::clamp(settings::aimbot::assist_slowdown_strength, 0.0f, 1.0f);
		if (grab > 0.001f)
		{
			const float mag = std::sqrt(step_x * step_x + step_y * step_y);
			if (mag > 1e-4f)
			{
				const float capped = (std::max)(settings::aimbot::assist_slowdown_speed, 10.0f) * aim_dt;
				const float target_mag = mag * (1.0f - grab) + (std::min)(mag, capped) * grab;
				const float s = target_mag / mag;
				step_x *= s;
				step_y *= s;
			}
		}
	}

	// Anti-shake clamp (all modes): with a high aim speed or sensitivity the
	// scaled per-tick step could move PAST the target, and the next tick
	// would correct back - that ping-pong is the "shaking" around the lock.
	// Never cross the remaining distance on either axis; at worst the aim
	// lands exactly on the target and rests.
	{
		const auto no_cross = [](float step, float rem) -> float
		{
			if ((step > 0.f && rem > 0.f) || (step < 0.f && rem < 0.f))
			{
				if (fabsf(step) > fabsf(rem)) return rem;
			}
			return step;
		};
		step_x = no_cross(step_x, rem_x);
		step_y = no_cross(step_y, rem_y);
	}

	// No subpixel carryover: fractional steps are rounded per tick and
	// dropped, so near the target the aim settles instead of micro-adjusting.
	// Clamp before lround - a pathological sensitivity chain could otherwise
	// overflow int and lround on an out-of-range float is UB.
	const int move_x = static_cast<int>(std::lround(std::clamp(step_x, -30000.0f, 30000.0f)));
	const int move_y = static_cast<int>(std::lround(std::clamp(step_y, -30000.0f, 30000.0f)));

	if (move_x != 0 || move_y != 0)
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dx = move_x;
		input.mi.dy = move_y;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		SendInput(1, &input, sizeof(INPUT));
		// remembered for next tick's physical-motion measurement (the injected
		// counts move the OS cursor too and must not count as user motion)
		s_last_inj_x = move_x;
		s_last_inj_y = move_y;
	}
}

static void select_target_point()
{
	if (aimbot::player.instance.address == 0)
	{
		aimbot::clear_aim_point();
		return;
	}

	const auto fc   = frame_cache::get_for_thread();
	const math::matrix4& view = fc.view;
	const math::vector2& dims = fc.dims;

	math::vector3 cam_pos{};
	if (game::camera != 0)
	{
		cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
	}
	float world_distance = get_world_distance_to_entity(aimbot::player, cam_pos);
	if (world_distance == FLT_MAX)
	{
		aimbot::clear_aim_point();
		return;
	}

	math::vector3 velocity = get_velocity(aimbot::player);
	bool is_in_air = std::abs(velocity.y) > 1.5f;
	int parts_mask = (is_in_air && settings::aimbot::air_parts != 0) ? settings::aimbot::air_parts : settings::aimbot::target_parts;

	float distance{};
	math::vector2 target_screen{};
	math::vector3 target_world{};
	if (get_target_point_for_entity(aimbot::player, view, dims, settings::aimbot::fov, distance, target_screen, parts_mask, !settings::aimbot::sticky_aim, &target_world))
	{
		aimbot::set_aim_point(target_world);
	}
	else
	{
		aimbot::clear_aim_point();
	}
}

static void fast_mouse_step()
{
	math::vector3 aim3d{};
	if (!aimbot::get_aim_point(aim3d))
		return;

	const auto fc   = frame_cache::get_for_thread();
	const math::matrix4& view = fc.view;
	const math::vector2& dims = fc.dims;

	math::vector2 target_screen{};
	if (!game::visualengine->world_to_screen(view, dims, aim3d, target_screen))
		return;

	mouse_aim(target_screen, dims);
}

static math::matrix3 look_at(const math::vector3& from, const math::vector3& to)
{
	math::vector3 forward = (to - from);
	float length = forward.length();
	if (length < 0.0001f)
	{
		return math::matrix3::identity();
	}
	forward = forward * (1.0f / length);

	math::vector3 world_up(0.0f, 1.0f, 0.0f);
	math::vector3 right = world_up.cross(forward);
	length = right.length();
	if (length < 0.0001f)
	{
		world_up = math::vector3(0.0f, 0.0f, 1.0f);
		right = world_up.cross(forward);
		length = right.length();
		if (length < 0.0001f)
		{
			return math::matrix3::identity();
		}
	}
	right = right * (1.0f / length);

	math::vector3 up = forward.cross(right);

	math::matrix3 result{};
	result.m[0][0] = -right.x;
	result.m[0][1] = up.x;
	result.m[0][2] = -forward.x;
	result.m[1][0] = -right.y;
	result.m[1][1] = up.y;
	result.m[1][2] = -forward.y;
	result.m[2][0] = -right.z;
	result.m[2][1] = up.z;
	result.m[2][2] = -forward.z;

	return result;
}

static math::matrix3 lerp_rotation(const math::matrix3& from, const math::matrix3& to, float t)
{
	math::vector3 from_forward = from.forward();
	math::vector3 to_forward = to.forward();
	math::vector3 from_right = from.right();
	math::vector3 to_right = to.right();
	math::vector3 from_up = from.up();
	math::vector3 to_up = to.up();
	math::vector3 lerped_forward = (from_forward + (to_forward - from_forward) * t).normalized();
	math::vector3 lerped_right = (from_right + (to_right - from_right) * t).normalized();
	math::vector3 lerped_up = lerped_forward.cross(lerped_right).normalized();
	lerped_right = lerped_up.cross(lerped_forward).normalized();
	math::matrix3 result{};
	result.m[0][0] = -lerped_right.x;
	result.m[0][1] = lerped_up.x;
	result.m[0][2] = -lerped_forward.x;
	result.m[1][0] = -lerped_right.y;
	result.m[1][1] = lerped_up.y;
	result.m[1][2] = -lerped_forward.y;
	result.m[2][0] = -lerped_right.z;
	result.m[2][1] = lerped_up.z;
	result.m[2][2] = -lerped_forward.z;
	return result;
}

static void camera_aimbot()
{
	if (aimbot::player.instance.address == 0)
	{
		return;
	}

	if (game::camera == 0)
	{
		return;
	}

	math::vector3 target_pos;
	bool found_part = false;

	math::vector3 velocity = get_velocity(aimbot::player);
	bool is_in_air = std::abs(velocity.y) > 1.5f;
	int parts_mask = (is_in_air && settings::aimbot::air_parts != 0) ? settings::aimbot::air_parts : settings::aimbot::target_parts;

	const auto cam_fc = frame_cache::get_for_thread();
	math::vector2 screen_center = cam_fc.dims * 0.5f;
	struct cam_hit_t { float dist; math::vector3 pos; };
	std::vector<cam_hit_t> bone_hits;
	for (const std::string& bone : collect_aim_bones(parts_mask, aimbot::player))
	{
		math::vector3 p{};
		if (!bodyparts::get_part_position(aimbot::player, bone.c_str(), p))
			continue;
		if (settings::aimbot::enable_prediction)
			p = apply_prediction(aimbot::player, p);
		math::vector2 scr{};
		if (!game::visualengine->world_to_screen(cam_fc.view, cam_fc.dims, p, scr))
			continue;
		bone_hits.push_back({ scr.distance(screen_center), p });
	}
	if (!bone_hits.empty())
	{
		// Point Amount: average the N closest bone points (1 = closest only)
		const int amount = std::clamp(settings::aimbot::point_amount, 1, (int)bone_hits.size());
		if (amount <= 1 || bone_hits.size() == 1)
		{
			target_pos = bone_hits[0].pos;
		}
		else
		{
			std::sort(bone_hits.begin(), bone_hits.end(), [](const cam_hit_t& a, const cam_hit_t& b) { return a.dist < b.dist; });
			math::vector3 acc{};
			for (int i = 0; i < amount; i++)
				acc = acc + bone_hits[i].pos;
			target_pos = acc * (1.0f / amount);
		}
		found_part = true;
	}
	if (!found_part)
		found_part = bodyparts::get_part_position(aimbot::player, "Head", target_pos);

	if (!found_part)
	{
		return;
	}

	math::vector3 camera_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
	math::vector3 direction = target_pos - camera_pos;
	float distance = direction.length();
	if (distance < 0.1f)
	{
		return;
	}

	math::matrix3 target_rot = look_at(camera_pos, target_pos);
	math::matrix3 current_rot = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);

	// Shake (humanized): jitter the aim point in camera space so the shake
	// actually reaches the rotation write - the old code only perturbed the
	// angle metric, which the camera never followed
	if (settings::aimbot::aim_mode == 0 && settings::aimbot::shake)
	{
		const float range = settings::aimbot::shake_value;
		const float range_y = settings::aimbot::shake_z > 0.01f ? settings::aimbot::shake_z : range;
		static thread_local std::mt19937 gen(std::random_device{}());
		std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
		const float px_per_rad = ((cam_fc.dims.y > 0.0f) ? cam_fc.dims.y : 1080.0f) / 1.0472f;
		const float wx = (dis(gen) * range) / px_per_rad * distance;
		const float wy = (dis(gen) * range_y) / px_per_rad * distance;
		target_pos = target_pos + target_rot.right() * wx + target_rot.up() * wy;
		target_rot = look_at(camera_pos, target_pos);
	}

	math::vector3 current_forward = current_rot.forward();
	math::vector3 target_forward = target_rot.forward();

	float current_yaw = std::atan2(-current_forward.x, -current_forward.z);
	float current_pitch = std::asin(current_forward.y);

	float target_yaw = std::atan2(-target_forward.x, -target_forward.z);
	float target_pitch = std::asin(target_forward.y);

	float yaw_diff = target_yaw - current_yaw;
	if (yaw_diff > 3.14159f)
	{
		yaw_diff -= 6.28318f;
	}
	else if (yaw_diff < -3.14159f)
	{
		yaw_diff += 6.28318f;
	}

	float pitch_diff = target_pitch - current_pitch;

	float total_angle_diff = std::sqrtf(yaw_diff * yaw_diff + pitch_diff * pitch_diff);

	uint64_t pf_cam_part_primitive = 0;
	if (game::camera != 0)
	{
		// the camera's "Part" child rarely changes - cache the walk instead of
		// re-traversing the child tree 250x per second
		static thread_local uint64_t s_cached_camera = 0;
		static thread_local uint64_t s_cached_prim = 0;
		if (s_cached_camera != game::camera)
		{
			s_cached_camera = game::camera;
			s_cached_prim = 0;
			rbx::c_instance camera_inst(game::camera);
			uint64_t cam_part_addr = camera_inst.find_first_child("Part");
			if (cam_part_addr != 0)
			{
				rbx::c_part cam_part(cam_part_addr);
				rbx::c_primitive prim = cam_part.get_primitive();
				if (prim.address != 0)
				{
					s_cached_prim = prim.address;
				}
			}
		}
		pf_cam_part_primitive = s_cached_prim;
	}

	if (settings::aimbot::aim_mode == 2) {
		// Blatant: 1:1 lock - write the target rotation directly
		memory->write<math::matrix3>(game::camera + Offsets::Camera::Rotation, target_rot);
		if (pf_cam_part_primitive != 0)
		{
			memory->write<math::matrix3>(pf_cam_part_primitive + Offsets::Primitive::Rotation, target_rot);
		}
	} else {
		static thread_local auto s_last_cam = std::chrono::steady_clock::now();
		auto cam_now = std::chrono::steady_clock::now();
		float cam_dt = std::clamp(std::chrono::duration<float>(cam_now - s_last_cam).count(), 0.001f, 0.1f);
		s_last_cam = cam_now;

		// Mouse Look: scales the camera-follow speed (camera mode)
		const float ml = std::clamp(settings::aimbot::mouse_look, 0.05f, 2.0f);

		// Unified aim speed (same slider as mouse mode): a constant fraction
		// of the remaining angle per tick, scaled by Mouse Look.
		float fraction = std::clamp(settings::aimbot::aim_speed_x * ml, 0.01f, 1.0f);

		// frame-rate independent normalization
		const float cam_dt_frac = std::clamp(cam_dt / 0.008f, 0.1f, 8.0f);
		if (fraction < 1.0f)
			fraction = 1.0f - std::pow(1.0f - fraction, cam_dt_frac);

		// Stick assist (humanized): magnetism, not a lock - the same pull
		// curve as the mouse path, in angle space
		if (settings::aimbot::aim_mode == 0 && settings::aimbot::stick_assist && settings::aimbot::stick_strength > 0.01f)
		{
			const float px_per_rad = (cam_fc.dims.x > 0.0f ? cam_fc.dims.x : 1920.0f) / 1.0472f;
			const float angle_dist = total_angle_diff * px_per_rad;
			const float pull_radius = 250.0f;
			if (angle_dist < pull_radius)
			{
				const float nearness = 1.0f - (angle_dist / pull_radius);
				const float pull = nearness * nearness * std::clamp(settings::aimbot::stick_strength, 0.0f, 1.0f);
				fraction = (std::min)(1.0f, fraction + (1.0f - fraction) * pull);
			}
		}

		// Speed cap (humanized toggle, angle space): clamp the per-tick rotation step.
		if (settings::aimbot::aim_mode == 0 && settings::aimbot::speed_cap)
		{
			const float max_rad = settings::aimbot::max_speed_px * 0.002f;
			const float min_rad = settings::aimbot::min_speed_px * 0.002f;
			const float step_angle = total_angle_diff * fraction;
			if (step_angle > max_rad)
			{
				fraction = (std::min)(1.0f, fraction * (max_rad / step_angle));
			}
			else if (step_angle < min_rad && total_angle_diff > 0.002f)
			{
				fraction = (std::min)(1.0f, fraction * (min_rad / step_angle));
			}
		}

		math::matrix3 smoothed_rotation = lerp_rotation(current_rot, target_rot, std::clamp(fraction, 0.001f, 1.0f));
		memory->write<math::matrix3>(game::camera + Offsets::Camera::Rotation, smoothed_rotation);
		if (pf_cam_part_primitive != 0)
		{
			memory->write<math::matrix3>(pf_cam_part_primitive + Offsets::Primitive::Rotation, smoothed_rotation);
		}
	}
}

static float get_world_distance_to_entity(const cache::entity_t& entity, const math::vector3& cam_pos)
{
	if (cam_pos.x != 0.f || cam_pos.y != 0.f || cam_pos.z != 0.f)
	{
		return cam_pos.distance(entity.position);
	}
	return FLT_MAX;
}

static cache::entity_t get_closest_player(const math::matrix4& view, const math::vector2& dims)
{
	cache::entity_t best_player{};
	float closest = FLT_MAX;
	cache::entity_t best_hostile_player{};
	float closest_hostile = FLT_MAX;

	bool ignore_friendly = (settings::aimbot::priorities & (1 << 0)) != 0;
	bool prioritise_hostile = (settings::aimbot::priorities & (1 << 1)) != 0;

	math::vector3 cam_pos{};
	if (game::camera != 0)
	{
		cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
	}

	auto entities_snap = cache::get_players_snap();
	auto local_snap    = cache::get_local_snap();
	if (!entities_snap) return cache::entity_t{};
	const std::vector<cache::entity_t>& entities_snapshot = *entities_snap;
	static cache::entity_t s_empty_local{};
	const cache::entity_t& local_entity = local_snap ? *local_snap : s_empty_local;

	const std::uint64_t local_addr = local_entity.instance.address;
	const std::uint64_t local_team = local_entity.team;
	const bool use_teamcheck = settings::teamcheck || settings::aimbot::teamcheck;
	// Rivals-first: detected Rivals places use ONLY the TeammateLabel check -
	// the generic Team fields are FFA-style noise there and never run
	const bool rivals_tc = use_teamcheck && rivals::is_rivals_place();

	for (const cache::entity_t& entity : entities_snapshot)
	{
		if (!entity.instance.address)
		{
			continue;
		}

		if (entity.instance.address == local_addr)
		{
			continue;
		}

		// Rivals-first: the TeammateLabel check is the ONLY decider there
		if (rivals_tc && rivals::is_teammate(entity))
		{
			continue;
		}

if (use_teamcheck && !rivals_tc && local_team != 0 && entity.team == local_team)
	{
		continue;
	}

		if (use_teamcheck && !rivals_tc && local_entity.team_color != 0 && entity.team_color == local_entity.team_color)
		{
			continue;
		}

	if (settings::aimbot::knock_check && entity.knocked)
	{
		continue;
	}


	if (settings::aimbot::health_check_enabled && entity.health <= 0.0f)
	{
		continue;
	}

	if (settings::aimbot::visible_check)
	{
		math::vector3 cam_pos{};
		if (game::camera != 0)
			cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
		if (cam_pos.x != 0.f || cam_pos.y != 0.f || cam_pos.z != 0.f)
		{
			// visualcheck (exact triangle raycast) takes precedence when enabled
			bool vis;
			if (settings::visuals::visual_check)
				vis = visualcheck::is_visible(entity.position, cam_pos);
			else
				vis = wallcheck::is_visible(entity.position, cam_pos);
			if (!vis)
				continue;
		}
	}

		cache::player_priority priority = playerlist::get_priority(entity.name);
		if (ignore_friendly && priority == cache::player_priority::friendly)
		{
			continue;
		}

		float world_distance = get_world_distance_to_entity(entity, cam_pos);
		if (world_distance == FLT_MAX)
		{
			continue;
		}

		float distance{};
		math::vector2 target_screen{};
		if (!get_target_point_for_entity(entity, view, dims, settings::aimbot::fov, distance, target_screen))
		{
			continue;
		}

		if (prioritise_hostile)
		{
			if (priority == cache::player_priority::hostile)
			{
				if (distance < closest_hostile)
				{
					closest_hostile = distance;
					best_hostile_player = entity;
				}
			}
			else
			{
				if (distance < closest)
				{
					closest = distance;
					best_player = entity;
				}
			}
		}
		else
		{
			if (distance < closest)
			{
				closest = distance;
				best_player = entity;
			}
		}
	}

	// Target hysteresis: prefer the current target if it's still in FOV
	// to prevent flickering between players every frame
	if (aimbot::last_target.instance.address != 0 &&
		best_player.instance.address != aimbot::last_target.instance.address &&
		!(prioritise_hostile && best_hostile_player.instance.address))
	{
		auto snap = cache::get_players_snap();
		if (snap)
		{
			for (const auto& entity : *snap)
			{
				if (entity.instance.address != aimbot::last_target.instance.address)
					continue;
				float last_distance{};
				math::vector2 last_screen{};
				if (get_target_point_for_entity(entity, view, dims, settings::aimbot::fov, last_distance, last_screen))
				{
					// keep the last target unless the new one is significantly closer (30%)
					if (best_player.instance.address == 0 || last_distance < closest * 1.3f)
					{
						best_player = entity;
					}
				}
				break;
			}
		}
	}

	if (prioritise_hostile && best_hostile_player.instance.address)
	{
		return best_hostile_player;
	}

	return best_player;
}

static bool is_target_valid(const cache::entity_t& entity)
{
	if (entity.instance.address == 0)
	{
		return false;
	}

	cache::entity_t local = cache::get_local_player();
	if (entity.instance.address == local.instance.address)
	{
		return false;
	}

	bool use_teamcheck = settings::teamcheck || settings::aimbot::teamcheck;
	// Rivals-first: detected Rivals places use ONLY the TeammateLabel check
	if (use_teamcheck && rivals::is_rivals_place())
	{
		if (rivals::is_teammate(entity))
		{
			return false;
		}
	}
	else
	{
		if (use_teamcheck && local.team != 0 && entity.team == local.team)
		{
			return false;
		}

		if (use_teamcheck && local.team_color != 0 && entity.team_color == local.team_color)
		{
			return false;
		}
	}

	if (settings::aimbot::knock_check && entity.knocked)
	{
		return false;
	}


	// min health gate: filter targets below the configured health threshold
	// (0 keeps the old behavior of only filtering fully dead players)
	if (settings::aimbot::health_check_enabled && entity.health < (std::max)(settings::aimbot::min_health, 0.0f))
	{
		return false;
	}

	// sticky lock must respect the visibility check too - without this a
	// target walking behind a wall stayed locked indefinitely
	if (settings::aimbot::visible_check)
	{
		bool target_visible = true;
		try
		{
			// visualcheck (exact triangle raycast) takes precedence when enabled
			if (settings::visuals::visual_check)
				target_visible = visualcheck::is_visible(entity.position, cached_camera_position());
			else
				target_visible = wallcheck::is_visible(entity.position, cached_camera_position());
		}
		catch (...)
		{
			target_visible = true;
		}
		if (!target_visible)
		{
			return false;
		}
	}

	bool ignore_friendly = (settings::aimbot::priorities & (1 << 0)) != 0;
	if (ignore_friendly)
	{
		cache::player_priority priority = playerlist::get_priority(entity.name);
		if (priority == cache::player_priority::friendly)
		{
			return false;
		}
	}

	return true;
}

// ============================================================================
// Silent Aim (PF style) - Camera Part rotation method
// Writes a look-at rotation matrix to Camera's child "Part" primitive.
// PF reads bullet direction from this Part, so it redirects shots without
// visibly moving the camera. The m[1][1]=0.01f trick prevents visual snap.
// Self-gates: games without a Camera "Part" child simply do nothing.
// ============================================================================
// Recoil control: while the fire button is held, watch the camera's pitch
// (forward vector Y) and push the mouse back down by the kick delta so the
// muzzle climb is cancelled. Passive - runs whenever the aimbot is enabled,
// independent of the aim key.
static void apply_recoil_control()
{
	if (settings::aimbot::aim_mode == 2 || !settings::aimbot::recoil_control || game::camera < 0x10000)
		return;

	math::matrix3 rot = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);
	float pitch = std::asinf(std::clamp(rot.m[1][2], -1.0f, 1.0f));

	static thread_local float s_prev_pitch = 0.0f;
	static thread_local bool s_init = false;
	if (!s_init) { s_prev_pitch = pitch; s_init = true; }

	const float delta = pitch - s_prev_pitch;
	s_prev_pitch = pitch;

	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
		return;
	if (std::abs(delta) < 1e-6f)
		return;

	// A large per-tick pitch change is the user's own flick, not recoil kick -
	// compensating it would fight the player's aim. Recoil kick is a small
	// constant climb per tick, so only compensate small deltas.
	if (std::abs(delta) > 0.02f)
		return;

	const auto fc = frame_cache::get_for_thread();
	const float px_per_rad = ((fc.dims.y > 0.0f) ? fc.dims.y : 1080.0f) / 1.0472f;

	// recoil_control_smooth low-passes the compensation (1 = instant, higher
	// = softer push-back) instead of mirroring every single tick
	static thread_local float s_smooth_delta = 0.0f;
	const float smooth = (std::max)(settings::aimbot::recoil_control_smooth, 0.1f);
	const float k = std::clamp(1.0f / smooth, 0.05f, 1.0f);
	s_smooth_delta += (delta - s_smooth_delta) * k;

	const int dy = (int)std::lround(std::clamp(s_smooth_delta * px_per_rad * settings::aimbot::recoil_control_strength, -30000.0f, 30000.0f));

	if (dy != 0)
	{
		INPUT in{};
		in.type = INPUT_MOUSE;
		in.mi.dx = 0;
		in.mi.dy = dy;
		in.mi.dwFlags = MOUSEEVENTF_MOVE;
		SendInput(1, &in, sizeof(in));
	}
}

void aimbot::run()
{
	using namespace std::chrono_literals;

	keybind::keybind_t aimbot_kb{};

	auto last_select = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
	auto last_cam_write = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
	auto next_tick = std::chrono::steady_clock::now();

	timeBeginPeriod(1);

	for (;;)
	{
		if (!settings::aimbot::enabled)
		{
			aimbot::clear_sticky_target();
			aimbot::clear_player();
			aimbot::clear_aim_point();
			aimbot::last_target = cache::entity_t{};
			Sleep(50);
			continue;
		}

		// passive recoil control - works while firing, independent of the aim key
		apply_recoil_control();

		aimbot_kb.key = settings::aimbot::keybind;
		aimbot_kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::activation_mode);

		if (g_menu.IsMenuOpened() || !keybind::is_active(aimbot_kb))
		{
			aimbot::clear_sticky_target();
			aimbot::clear_player();
			aimbot::clear_aim_point();
			aimbot::last_target = cache::entity_t{};
			Sleep(50);
			continue;
		}

		auto now = std::chrono::steady_clock::now();

		// Slow path (~125 Hz): target selection + predicted 3D aim point.
		if (now - last_select >= std::chrono::milliseconds(8))
		{
			last_select = now;

			const auto fc   = frame_cache::get_for_thread();
			const math::matrix4& view = fc.view;
			const math::vector2& dims = fc.dims;

			cache::entity_t player{};

			if (settings::aimbot::sticky_aim && aimbot::sticky_target.instance.address != 0)
			{
				cache::entity_t refreshed_target{};
				bool target_found = false;
				auto snap = cache::get_players_snap();
				if (snap)
				{
					for (const auto& entity : *snap)
					{
						if (entity.instance.address == aimbot::sticky_target.instance.address)
						{
							refreshed_target = entity;
							target_found = true;
							break;
						}
					}
				}

				// Aim-assist style lock: the current target is retained until it
				// dies/goes invalid, or a challenger is CLEARLY closer to the
				// crosshair (hysteresis - prevents the lock flickering between
				// two equidistant targets the way plain closest-pick does)
				cache::entity_t closest = get_closest_player(view, dims);

				float sticky_dist = FLT_MAX, challenger_dist = FLT_MAX;
				math::vector2 tmp_scr{};
				math::vector3 tmp_world{};
				if (target_found)
					get_target_point_for_entity(refreshed_target, view, dims, 0.f, sticky_dist, tmp_scr, -1, false, &tmp_world);
				if (closest.instance.address != 0)
					get_target_point_for_entity(closest, view, dims, 0.f, challenger_dist, tmp_scr, -1, false, &tmp_world);

				const bool challenger_takes_over = closest.instance.address != 0 &&
					closest.instance.address != aimbot::sticky_target.instance.address &&
					challenger_dist < sticky_dist * 0.65f;

				if (target_found && is_target_valid(refreshed_target) && !challenger_takes_over)
				{
					player = refreshed_target;
					aimbot::set_sticky_target(refreshed_target);
				}
				else
				{
					aimbot::clear_sticky_target();
					player = closest;
					if (settings::aimbot::sticky_aim && player.instance.address != 0)
					{
						aimbot::set_sticky_target(player);
					}
				}
			}
			else
			{
				player = get_closest_player(view, dims);
				if (settings::aimbot::sticky_aim && player.instance.address != 0)
				{
					aimbot::set_sticky_target(player);
				}
			}

			if (player.instance.address == 0)
			{
				aimbot::clear_player();
				aimbot::clear_aim_point();
			}
			else if (!is_target_valid(player))
			{
				// the lock only drops when the USER'S own checks say so -
				// is_target_valid respects knock_check / health_check_enabled /
				// teamcheck / visibility. With health & knock checks disabled a
				// dead player stays locked instead of silently dropping.
				if (settings::aimbot::disable_on_kill && player.health <= 0.0f)
				{
					settings::aimbot::enabled = false;
					notifications::add("aimbot disabled - target eliminated");
				}
				aimbot::clear_sticky_target();
				aimbot::clear_player();
				aimbot::clear_aim_point();
			}
			else
			{
				aimbot::set_player(player);
				aimbot::last_target = player;
				select_target_point();
			}
		}

		// Lock delay (humanized): true reaction gate - when a new target
		// locks, aim holds completely still for the delay, then corrects at
		// full speed. Scaled 1-50 in the menu (x10 = ms). Minimum ~= every tick.
		static thread_local auto s_lock_start = std::chrono::steady_clock::now();
		static thread_local std::uint64_t s_lock_addr = 0;
		{
			const int units = (std::max)(1, (std::min)(settings::aimbot::lock_delay, 50));
			const std::uint64_t cur = aimbot::get_player_address();
			auto tick_now = std::chrono::steady_clock::now();
			if (cur == 0)
			{
				// no target: re-arm so the next lock waits the full delay
				s_lock_addr = 0;
			}
			else
			{
				if (cur != s_lock_addr)
				{
					s_lock_addr = cur;
					s_lock_start = tick_now;
				}
				if (std::chrono::duration<float, std::milli>(tick_now - s_lock_start).count() < (float)(units * 10))
				{
					next_tick += std::chrono::microseconds(1000);
					if (next_tick > tick_now)
						std::this_thread::sleep_for(next_tick - tick_now);
					continue;
				}
			}
		}

		// Fast path: mouse moves at ~1 kHz off the cached 3D point with a
		// fresh projection each tick (no RPM in the hot loop); camera writes
		// stay at ~250 Hz to avoid hammering memory writes.
		if (settings::aimbot::mode == 0)
		{
			fast_mouse_step();
		}
		else if (settings::aimbot::mode == 1)
		{
			if (now - last_cam_write >= std::chrono::milliseconds(4))
			{
				last_cam_write = now;
				camera_aimbot();
			}
		}

		// Deadline-paced 1 kHz: sleep the remainder so the core idles,
		// steady_clock keeps the average exact with zero long-term drift.
		next_tick += std::chrono::microseconds(1000);
		auto tick_now = std::chrono::steady_clock::now();
		if (tick_now - next_tick > std::chrono::milliseconds(2))
		{
			next_tick = tick_now; // resync after hitches, never burst-catch-up
		}
		else if (next_tick > tick_now)
		{
			std::this_thread::sleep_for(next_tick - tick_now);
			while (std::chrono::steady_clock::now() < next_tick) {}
		}
	}
}
