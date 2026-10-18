#include "trigger.h"
#include <features/combat/rivals/rivals.h>
#include <features/visuals/geometry/visualcheck.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/frame.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <sdk/game/game.h>
#include <sdk/math/math.h>
#include <sdk/offsets/offsets.h>
#include <features/mesh/assetmesh.h>
#include <features/visuals/geometry/wallcheck.h>
#include <core/memory/memory.h>
#include <features/system/settings/settings.h>
#include <features/system/keybind/keybind.h>
#include <features/system/playerlist/playerlist.h>
#include <windows.h>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace triggerbot
{
	static bool point_in_triangle(const math::vector2& p, const math::vector2& a, const math::vector2& b, const math::vector2& c)
	{
		// Reject degenerate triangles (zero area) — they cause false positives
		const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
		if (fabsf(area) < 0.01f) return false;

		float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
		float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
		float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
		bool neg = (d1 < -0.001f) || (d2 < -0.001f) || (d3 < -0.001f);
		bool pos = (d1 > 0.001f) || (d2 > 0.001f) || (d3 > 0.001f);
		return !(neg && pos);
	}

	static float point_segment_dist(const math::vector2& p, const math::vector2& a, const math::vector2& b)
	{
		float dx = b.x - a.x, dy = b.y - a.y;
		float len_sq = dx * dx + dy * dy;
		if (len_sq < 1e-8f) return p.distance(a);
		float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len_sq, 0.0f, 1.0f);
		float px = a.x + t * dx - p.x, py = a.y + t * dy - p.y;
		return std::sqrt(px * px + py * py);
	}

	// Mesh-perfect hit test: projects a body part's real mesh triangles to
	// screen and checks whether the crosshair lands inside any of them.
	// Mesh vertices are in part-local space (centered at origin), so the
	// transform is: world = part_pos + rot * (vertex * scale).
	// No bounds-center subtraction — that was the accuracy bug.
	static bool crosshair_on_part_mesh(
		const cache::entity_t& entity,
		const std::string& part_name,
		rbx::c_part part_inst,
		const math::matrix4& view,
		const math::vector2& dims,
		const math::vector3& cam_pos,
		const math::vector2& crosshair,
		float pad_px,
		float& out_depth)
	{
		rbx::c_primitive prim = part_inst.get_primitive();
		if (!prim.address) return false;

		math::cframe cf = prim.get_cframe();
		math::vector3 pos = cf.position;
		if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) return false;
		math::matrix3 rot = cf.rotation;
		math::vector3 part_sz = prim.get_size();
		if (part_sz.x <= 0.05f || part_sz.y <= 0.05f || part_sz.z <= 0.05f) return false;

		// camera forward for behind-camera rejection (phantom-hit fix)
		math::matrix3 cam_rot = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);
		const math::vector3 cam_fwd = cam_rot.forward();

		bool is_r15 = (entity.parts.count("UpperTorso") > 0 && entity.parts.count("LowerTorso") > 0);

		uint64_t asset_id = assetmesh::get_mesh_asset_id_from_part(part_inst.address);
		if (asset_id == 0 && entity.user_id != 0)
		{
			assetmesh::request_avatar_assets(static_cast<int64_t>(entity.user_id));
			asset_id = assetmesh::get_mesh_asset_id_for_part(static_cast<int64_t>(entity.user_id), part_name.c_str(), is_r15);
		}
		if (asset_id == 0)
			asset_id = assetmesh::get_default_asset_for_part(part_name.c_str(), is_r15);
		if (asset_id != 0)
			assetmesh::request_mesh(asset_id);

		auto mesh = assetmesh::get_mesh_for_part(part_name.c_str(), is_r15, asset_id);
		if (!mesh || mesh->vertices.empty() || !mesh->indices.empty() == false)
			return false;

		const auto& b = mesh->bounds;
		float sx = b.size.x > 0.001f ? part_sz.x / b.size.x : 1.0f;
		float sy = b.size.y > 0.001f ? part_sz.y / b.size.y : 1.0f;
		float sz_scale = b.size.z > 0.001f ? part_sz.z / b.size.z : 1.0f;

		bool hit = false;
		float best_depth = FLT_MAX;

		const size_t num_indices = mesh->indices.size();
		for (size_t ii = 0; ii + 2 < num_indices; ii += 3)
		{
			uint32_t i0 = mesh->indices[ii];
			uint32_t i1 = mesh->indices[ii + 1];
			uint32_t i2 = mesh->indices[ii + 2];
			if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size())
				continue;

			math::vector2 scr[3];
			math::vector3 world[3];
			bool ok = true;
			const uint32_t idx[3] = { i0, i1, i2 };
			for (int k = 0; k < 3; ++k)
			{
				const auto& v = mesh->vertices[idx[k]].position;
				// Part-local space: no bounds-center subtraction
				float lx = v.x * sx;
				float ly = v.y * sy;
				float lz = v.z * sz_scale;
				world[k] = math::vector3{
					rot.m[0][0] * lx + rot.m[0][1] * ly + rot.m[0][2] * lz + pos.x,
					rot.m[1][0] * lx + rot.m[1][1] * ly + rot.m[1][2] * lz + pos.y,
					rot.m[2][0] * lx + rot.m[2][1] * ly + rot.m[2][2] * lz + pos.z
				};
				if (!game::visualengine->world_to_screen(view, dims, world[k], scr[k]))
				{
					ok = false;
					break;
				}
			}
			if (!ok) continue;

			bool inside = point_in_triangle(crosshair, scr[0], scr[1], scr[2]);
			if (!inside && pad_px > 0.01f)
			{
				float d = (std::min)({ point_segment_dist(crosshair, scr[0], scr[1]),
					point_segment_dist(crosshair, scr[1], scr[2]),
					point_segment_dist(crosshair, scr[2], scr[0]) });
				inside = (d <= pad_px);
			}
			if (!inside) continue;

			math::vector3 centroid = (world[0] + world[1] + world[2]) * (1.0f / 3.0f);
			// reject triangles behind the camera — world_to_screen can still
			// return a valid point for them, causing phantom hits
			if (cam_fwd.dot(centroid - cam_pos) <= 0.5f) continue;

			float depth = centroid.distance(cam_pos);
			if (depth < best_depth)
			{
				best_depth = depth;
				hit = true;
			}
		}

		if (hit)
		{
			out_depth = best_depth;
			return true;
		}
		return false;
	}

	static bool is_gun_equipped(const cache::entity_t& local_player)
	{
		if (local_player.instance.address == 0)
			return false;

		std::uint64_t character_addr = local_player.instance.address;
		rbx::c_instance character(character_addr);

		for (const auto& child : character.get_children<rbx::c_instance>())
		{
			if (!child.address) continue;
			std::string cls = child.get_class_name();
			if (cls == "Tool")
			{
				return true;
			}
		}

		if (game::camera != 0)
		{
			rbx::c_instance cam(game::camera);
			for (const auto& c : cam.get_children<rbx::c_instance>())
			{
				if (!c.address) continue;
				std::string cls = c.get_class_name();
				if (cls == "Model" || cls == "Tool")
				{
					std::string nm = c.get_name();
					std::string lower_nm = nm;
					std::transform(lower_nm.begin(), lower_nm.end(), lower_nm.begin(), ::tolower);
					if (lower_nm.find("viewmodel") != std::string::npos ||
						lower_nm.find("gun") != std::string::npos ||
						lower_nm.find("weapon") != std::string::npos ||
						lower_nm.find("arms") != std::string::npos)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	void run()
	{
		while (true)
		{
			if (!settings::triggerbot::enabled)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}

			keybind::keybind_t kb{};
			kb.key = settings::triggerbot::keybind;
			kb.mode = static_cast<keybind::activation_mode>(settings::triggerbot::activation_mode);

			// unique bind id - a null id shared one toggle state across every
			// unbound-id consumer on the same key
			if (!keybind::is_key_active(settings::triggerbot::keybind, settings::triggerbot::activation_mode, "triggerbot"))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				continue;
			}

			HWND roblox_window = game::get_roblox_window();
			HWND fg = GetForegroundWindow();
			if (roblox_window && fg != roblox_window)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			if (!game::visualengine || game::visualengine->address == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			// snapshot the pointer - the loop dereferences it dozens of times
			// per iteration and the game can close mid-tick
			rbx::c_visualengine* ve = game::visualengine.get();
			if (!ve || ve->address == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			const auto fc   = frame_cache::get_for_thread();
			const math::matrix4& view = fc.view;
			const math::vector2& dims = fc.dims;
			if (dims.x < 100.0f || dims.y < 100.0f)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			math::vector2 crosshair{ dims.x * 0.5f, dims.y * 0.5f };
			POINT pt{};
			if (roblox_window && GetCursorPos(&pt))
			{
				ScreenToClient(roblox_window, &pt);
				if (pt.x >= 0 && pt.x <= (int)dims.x && pt.y >= 0 && pt.y <= (int)dims.y)
				{
					crosshair = { (float)pt.x, (float)pt.y };
				}
			}

			cache::entity_t local_player{};
			{
				std::lock_guard<std::mutex> lock(cache::local_player_mtx);
				local_player = cache::local_player;
			}

			if (settings::triggerbot::guncheck && !is_gun_equipped(local_player))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			// cheap shared_ptr snapshot instead of a full deep copy of the
			// player vector under cache::mtx at 1000 Hz
		auto entities_snap = cache::get_players_snap();
		if (!entities_snap)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			continue;
		}

		// refreshed before every fire decision so delayed shots never use
		// stale entity positions or a stale crosshair
		auto refresh_frame = [&]() -> bool {
			POINT pt{};
			if (roblox_window && GetCursorPos(&pt))
			{
				ScreenToClient(roblox_window, &pt);
				if (pt.x >= 0 && pt.x <= (int)dims.x && pt.y >= 0 && pt.y <= (int)dims.y)
					crosshair = { (float)pt.x, (float)pt.y };
			}
			entities_snap = cache::get_players_snap();
			return (bool)entities_snap;
		};

			// Rivals-first gate, evaluated once per tick (1 RPM, not per entity)
		const bool rivals_tc = settings::triggerbot::teamcheck && rivals::is_rivals_place();

		// detection as a lambda so it can be re-run after the fire delay -
			// the old code slept delay_ms and then clicked wherever the
			// crosshair had moved to since the check
		auto detect_hit = [&](const auto& ents) -> bool {
			// Always mesh method. Screen-space triangle test on the real
			// avatar mesh for every body part — no bounding-rect fallback.
			math::vector3 cam_pos{};
			if (game::camera != 0)
			{
				cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
			}

			if (cam_pos.x != 0.f || cam_pos.y != 0.f || cam_pos.z != 0.f)
			{
				// hitbox = real avatar mesh + small pad; the pad is scaled
				// down slightly so shots land closer to true center-mass
				float pad_px = (std::max)(0.0f, settings::triggerbot::threshold * 0.75f);
				// prefilter scales with screen size so non-fullscreen windows
				// don't let every entity through (was a fixed 260px)
				float prefilter_px = pad_px + dims.x * 0.35f;
				float prefilter_sq = prefilter_px * prefilter_px;
				float closest_tri_depth = 1000.0f;
				bool hit_enemy = false;

				for (const auto& entity : ents)
				{
					if (entity.instance.address == 0 || entity.instance.address == local_player.instance.address)
						continue;
					if (entity.health <= 0.0f)
						continue;
if (settings::triggerbot::teamcheck && !rivals_tc && local_player.team != 0 && entity.team == local_player.team)
						continue;
					if (settings::triggerbot::teamcheck && !rivals_tc && local_player.team_color != 0 && entity.team_color == local_player.team_color)
						continue;
					// Rivals-first: detected Rivals places use ONLY the TeammateLabel
					// check - the generic Team fields are FFA-style noise there
					if (settings::triggerbot::teamcheck && rivals_tc && rivals::is_teammate(entity))
						continue;
					if (settings::triggerbot::knock_check && entity.knocked)
						continue;
					if (playerlist::get_priority(entity.name) == cache::player_priority::friendly)
						continue;
					// visualcheck: exact triangle raycast from the camera to the
					// entity - geometry between camera and target blocks the shot
					if (settings::visuals::visual_check &&
						!visualcheck::is_visible(entity.position, cam_pos))
						continue;

					// Coarse prefilter: skip entities nowhere near the crosshair.
					{
						math::vector3 anchor = entity.position;
						if (entity.part_positions.head.y != 0.0f)
							anchor = entity.part_positions.head;
						math::vector2 anchor_scr{};
						if (!ve->world_to_screen(view, dims, anchor, anchor_scr))
							continue;
						float adx = anchor_scr.x - crosshair.x;
						float ady = anchor_scr.y - crosshair.y;
						if ((adx * adx + ady * ady) > prefilter_sq)
							continue;
					}

					static const std::vector<std::string> part_names_to_check = {
						"Head",
						"Torso", "UpperTorso", "LowerTorso",
						"HumanoidRootPart",
						"LeftArm", "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
						"RightArm", "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
						"LeftLeg", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
						"RightLeg", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg"
					};

					for (const auto& pname : part_names_to_check)
					{
						rbx::c_part part_inst(0);
						auto it = entity.parts.find(pname);
						if (it == entity.parts.end())
						{
							if (pname == "Head" && entity.head_part.address)
								part_inst = entity.head_part;
							else
								continue;
						}
						else
						{
							part_inst = it->second;
						}
						if (!part_inst.address) continue;

						float part_depth = FLT_MAX;
						if (crosshair_on_part_mesh(entity, pname, part_inst, view, dims, cam_pos, crosshair, pad_px, part_depth))
						{
							if (part_depth < closest_tri_depth)
							{
								closest_tri_depth = part_depth;
								hit_enemy = true;
							}
						}
					}
				}

				if (hit_enemy)
				{
					return true;
				}
			}
			return false;
		};

		bool should_shoot = detect_hit(*entities_snap);
		{
			// 1-50 scale (x10 = ms) reaction wait. minimum fires instantly;
			// anything above re-verifies on FRESH data so the delayed shot
			// never fires on stale positions - script-style double-check
			const int units = (std::max)(1, (std::min)(settings::triggerbot::delay, 50));
			if (should_shoot && units > 1)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(units * 10));
				// re-verify after the delay - the crosshair may have moved off
				// the target or the key been released; never fire blind
				should_shoot = keybind::is_key_active(settings::triggerbot::keybind, settings::triggerbot::activation_mode, "triggerbot")
					&& refresh_frame() && detect_hit(*entities_snap);
			}
		}

			if (should_shoot)
			{
				INPUT inputs[2] = {};
				inputs[0].type = INPUT_MOUSE;
				inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
				inputs[1].type = INPUT_MOUSE;
				inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
				SendInput(2, inputs, sizeof(INPUT));

				int cd = (std::max)(5, (int)settings::triggerbot::cooldown_ms);
				std::this_thread::sleep_for(std::chrono::milliseconds(cd));
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}
}
