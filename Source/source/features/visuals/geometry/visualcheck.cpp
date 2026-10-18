#include "visualcheck.h"

#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <core/system/eject.h>
#include <sdk/offsets/offsets.h>
#include <features/system/settings/settings.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/frame.h>
#include <features/mesh/assetmesh.h>

#include <queue>
#include <memory>
#include <cmath>
#include <chrono>
#include <atomic>
#include <mutex>

namespace visualcheck
{
	struct geo_tri
	{
		math::vector3 a, b, c;
	};

	struct geo_part
	{
		std::uint64_t address = 0;
		math::vector3 min{}, max{};
		std::vector<geo_tri> tris;
	};

	// immutable snapshot swap - readers grab the pointer under the mutex
	// (cheap) and raycast WITHOUT holding the lock
	inline std::shared_ptr<const std::vector<geo_part>> g_geo;
	inline std::mutex g_mutex;
	inline std::atomic<bool> g_enabled{ false };
	inline std::atomic<bool> g_rescan{ false };
	inline stats_t g_stats;

	static bool is_part_class(const std::string& cls)
	{
		return cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" ||
		       cls == "WedgePart" || cls == "CornerWedgePart" || cls == "TrussPart";
	}

	// ------------------------------------------------------------------
	// analytic geometry builders ("perfect math" for primitive shapes)
	// ------------------------------------------------------------------

	static void push_quad(std::vector<geo_tri>& out, const math::vector3& a,
		const math::vector3& b, const math::vector3& c, const math::vector3& d)
	{
		out.push_back({ a, b, c });
		out.push_back({ a, c, d });
	}

	static void build_box_tris(const math::vector3& pos, const math::matrix3& rot,
		const math::vector3& size, std::vector<geo_tri>& out)
	{
		const math::vector3 h = size * 0.5f;
		math::vector3 bx(rot.m[0][0], rot.m[1][0], rot.m[2][0]);
		math::vector3 by(rot.m[0][1], rot.m[1][1], rot.m[2][1]);
		math::vector3 bz(rot.m[0][2], rot.m[1][2], rot.m[2][2]);
		const auto P = [&](float x, float y, float z) {
			return pos + bx * (h.x * x) + by * (h.y * y) + bz * (h.z * z);
		};
		// 8 corners
		const math::vector3 p000 = P(-1, -1, -1), p100 = P(1, -1, -1);
		const math::vector3 p010 = P(-1, 1, -1), p110 = P(1, 1, -1);
		const math::vector3 p001 = P(-1, -1, 1), p101 = P(1, -1, 1);
		const math::vector3 p011 = P(-1, 1, 1), p111 = P(1, 1, 1);
		// 6 faces x 2 tris
		push_quad(out, p000, p100, p110, p010);  // -z
		push_quad(out, p001, p101, p111, p011);  // +z
		push_quad(out, p000, p001, p011, p010);  // -x
		push_quad(out, p100, p101, p111, p110);  // +x
		push_quad(out, p000, p001, p101, p100);  // -y
		push_quad(out, p010, p011, p111, p110);  // +y
	}

	// exact wedge ramp: bottom quad, back quad, slope quad, two side tris
	static void build_wedge_tris(const math::vector3& pos, const math::matrix3& rot,
		const math::vector3& size, std::vector<geo_tri>& out)
	{
		const math::vector3 h = size * 0.5f;
		math::vector3 bx(rot.m[0][0], rot.m[1][0], rot.m[2][0]);
		math::vector3 by(rot.m[0][1], rot.m[1][1], rot.m[2][1]);
		math::vector3 bz(rot.m[0][2], rot.m[1][2], rot.m[2][2]);
		const auto P = [&](float x, float y, float z) {
			return pos + bx * (h.x * x) + by * (h.y * y) + bz * (h.z * z);
		};
		const math::vector3 A = P(-1, -1, 1), B = P(1, -1, 1);    // bottom front
		const math::vector3 C = P(1, -1, -1), D = P(-1, -1, -1);  // bottom back
		const math::vector3 E = P(1, 1, -1), F = P(-1, 1, -1);    // top back
		push_quad(out, A, B, C, D);            // bottom
		push_quad(out, D, C, E, F);            // back (vertical)
		push_quad(out, A, B, E, F);            // slope
		out.push_back({ A, D, F });            // left tri
		out.push_back({ B, C, E });            // right tri
	}

	// real MeshPart triangles: asset mesh scaled into part space, transformed
	// to world. same convention as the triggerbot mesh test (vertices are
	// part-local, scaled by part_size / bounds_size - no center offset).
	static void build_mesh_tris(const cache::entity_t* /*unused*/, rbx::c_part part,
		const math::vector3& pos, const math::matrix3& rot,
		const math::vector3& part_sz, std::vector<geo_tri>& out)
	{
		uint64_t asset_id = assetmesh::get_mesh_asset_id_from_part(part.address);
		if (asset_id == 0)
			return;
		assetmesh::request_mesh(asset_id);
		auto mesh = assetmesh::get_mesh(asset_id);
		if (!mesh || mesh->vertices.empty() || mesh->indices.empty())
			return;
		if (mesh->indices.size() / 3 > 16384)  // absurd triangle count - box fallback
			return;

		const auto& b = mesh->bounds;
		const float sx = b.size.x > 0.001f ? part_sz.x / b.size.x : 1.0f;
		const float sy = b.size.y > 0.001f ? part_sz.y / b.size.y : 1.0f;
		const float sz = b.size.z > 0.001f ? part_sz.z / b.size.z : 1.0f;

		const size_t n = mesh->indices.size();
		out.reserve(out.size() + n / 3);
		for (size_t ii = 0; ii + 2 < n; ii += 3)
		{
			const uint32_t idx[3] = { mesh->indices[ii], mesh->indices[ii + 1], mesh->indices[ii + 2] };
			bool ok = true;
			for (int k = 0; k < 3; ++k)
				if (idx[k] >= mesh->vertices.size()) { ok = false; break; }
			if (!ok) continue;

			geo_tri t{};
			math::vector3* vs[3] = { &t.a, &t.b, &t.c };
			for (int k = 0; k < 3; ++k)
			{
				const auto& v = mesh->vertices[idx[k]].position;
				const float lx = v.x * sx, ly = v.y * sy, lz = v.z * sz;
				*vs[k] = math::vector3{
					rot.m[0][0] * lx + rot.m[0][1] * ly + rot.m[0][2] * lz + pos.x,
					rot.m[1][0] * lx + rot.m[1][1] * ly + rot.m[1][2] * lz + pos.y,
					rot.m[2][0] * lx + rot.m[2][1] * ly + rot.m[2][2] * lz + pos.z
				};
			}
			out.push_back(t);
		}
	}

	static void compute_aabb(geo_part& part)
	{
		if (part.tris.empty()) return;
		math::vector3 mn = part.tris[0].a;
		math::vector3 mx = part.tris[0].a;
		for (const auto& t : part.tris)
		{
			const math::vector3* vs[3] = { &t.a, &t.b, &t.c };
			for (int k = 0; k < 3; ++k)
			{
				mn.x = (std::min)(mn.x, vs[k]->x);
				mn.y = (std::min)(mn.y, vs[k]->y);
				mn.z = (std::min)(mn.z, vs[k]->z);
				mx.x = (std::max)(mx.x, vs[k]->x);
				mx.y = (std::max)(mx.y, vs[k]->y);
				mx.z = (std::max)(mx.z, vs[k]->z);
			}
		}
		part.min = mn;
		part.max = mx;
	}

	// ------------------------------------------------------------------
	// scanner
	// ------------------------------------------------------------------

	static math::vector3 current_camera_pos()
	{
		if (game::camera == 0) return {};
		math::vector3 p = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
		if (!std::isfinite(p.x + p.y + p.z)) return {};
		return p;
	}

	static void scan_tree(rbx::c_instance root, const math::vector3& cam,
		float radius_sq, std::vector<geo_part>& found, int& tri_total)
	{
		std::queue<uint64_t> to_visit;
		to_visit.push(root.address);

		while (!to_visit.empty() && (int)found.size() < 4000 && tri_total < 400000)
		{
			const uint64_t addr = to_visit.front();
			to_visit.pop();
			if (!addr) continue;

			rbx::c_instance inst(addr);
			std::vector<rbx::c_instance> children = inst.get_children<rbx::c_instance>();

			for (auto& child : children)
			{
				if (!child.address) continue;
				const std::string cls = child.get_class_name();

				if (cls == "Model" || cls == "Folder" || cls == "Workspace" || cls == "Configuration")
				{
					to_visit.push(child.address);
					continue;
				}
				if (!is_part_class(cls))
					continue;

				rbx::c_part part(child.address);
				rbx::c_primitive prim = part.get_primitive();
				if (!prim.address) continue;

				const math::vector3 pos = prim.get_position();
				if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;
				const math::vector3 sz = prim.get_size();
				if (sz.x < 0.05f || sz.y < 0.05f || sz.z < 0.05f) continue;
				if (sz.x > 2048.f || sz.y > 2048.f || sz.z > 2048.f) continue;

				// radius gate - only parts near the camera enter the cache
				const float dx = pos.x - cam.x, dy = pos.y - cam.y, dz = pos.z - cam.z;
				if (dx * dx + dy * dy + dz * dz > radius_sq)
					continue;

				geo_part gp;
				gp.address = child.address;
				const math::matrix3 rot = prim.get_cframe().rotation;

				if (cls == "MeshPart")
				{
					build_mesh_tris(nullptr, part, pos, rot, sz, gp.tris);
					if (gp.tris.empty())
						build_box_tris(pos, rot, sz, gp.tris);  // mesh not ready yet
				}
				else if (cls == "WedgePart" || cls == "CornerWedgePart")
				{
					build_wedge_tris(pos, rot, sz, gp.tris);
				}
				else  // Part, TrussPart, UnionOperation (CSG unreadable)
				{
					build_box_tris(pos, rot, sz, gp.tris);
				}

				compute_aabb(gp);
				tri_total += (int)gp.tris.size();
				found.push_back(std::move(gp));
			}
		}
	}

	static void scan_once()
	{
		const auto t0 = std::chrono::steady_clock::now();

		const math::vector3 cam = current_camera_pos();
		if (cam.x == 0.f && cam.y == 0.f && cam.z == 0.f) return;
		if (!game::datamodel || game::datamodel->address == 0) return;

		std::vector<geo_part> found;
		found.reserve(1024);
		int tri_total = 0;

		rbx::c_workspace ws = game::datamodel->get_workspace();
		if (ws.address != 0)
			scan_tree(rbx::c_instance(ws.address), cam,
				settings::visuals::visual_radius * settings::visuals::visual_radius,
				found, tri_total);

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_geo = std::make_shared<const std::vector<geo_part>>(std::move(found));
		}

		const auto t1 = std::chrono::steady_clock::now();
		g_stats.last_scan_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
		g_stats.last_scan_age = 0.f;
	}

	static void scan_thread()
	{
		while (true)
		{
			if (!g_enabled.load(std::memory_order_acquire) ||
				!settings::visuals::visual_check)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			scan_once();
			g_rescan.store(false, std::memory_order_release);

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				if (g_geo)
				{
					int tris = 0, parts = 0;
					for (const auto& p : *g_geo) { parts++; tris += (int)p.tris.size(); }
					g_stats.parts = parts;
					g_stats.triangles = tris;
				}
			}

			g_stats.last_scan_age += settings::visuals::visual_refresh_ms / 1000.f;
			// recache cadence - fast so moving maps stay fresh
			std::this_thread::sleep_for(std::chrono::milliseconds(
				(int)(std::max)(50.f, settings::visuals::visual_refresh_ms)));
		}
	}

	void run()
	{
		static bool s_started = false;
		if (s_started) return;
		s_started = true;
		std::thread([]() {
			sys_eject::register_current_thread();
			scan_thread();
		}).detach();
	}

	void rescan()
	{
		g_rescan.store(true, std::memory_order_release);
	}

	stats_t stats()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_stats;
	}

	// ------------------------------------------------------------------
	// raycasting (Moller-Trumbore, exact)
	// ------------------------------------------------------------------

	static bool ray_tri(const math::vector3& o, const math::vector3& d,
		const geo_tri& t, float max_t, float& out_t)
	{
		const math::vector3 e1 = t.b - t.a;
		const math::vector3 e2 = t.c - t.a;
		const math::vector3 p = d.cross(e2);
		const float det = e1.dot(p);
		if (std::fabs(det) < 1e-8f) return false;   // parallel / degenerate
		const float inv = 1.0f / det;
		const math::vector3 tv = o - t.a;
		const float u = tv.dot(p) * inv;
		if (u < -1e-5f || u > 1.00001f) return false;
		const math::vector3 q = tv.cross(e1);
		const float v = d.dot(q) * inv;
		if (v < -1e-5f || u + v > 1.00001f) return false;
		const float tt = e2.dot(q) * inv;
		if (tt > 0.05f && tt < max_t)
		{
			out_t = tt;
			return true;
		}
		return false;
	}

	// ray vs part AABB (slab) - cheap broadphase before the triangle test
	static bool ray_aabb(const math::vector3& o, const math::vector3& inv_d,
		const geo_part& part, float max_t)
	{
		float tmin = 0.f, tmax = max_t;
		const math::vector3* mn = &part.min;
		const math::vector3* mx = &part.max;
		const float ox[3] = { o.x, o.y, o.z };
		const float dx[3] = { inv_d.x, inv_d.y, inv_d.z };
		for (int i = 0; i < 3; ++i)
		{
			const float lo = (i == 0) ? mn->x : (i == 1) ? mn->y : mn->z;
			const float hi = (i == 0) ? mx->x : (i == 1) ? mx->y : mx->z;
			float t1 = (lo - ox[i]) * dx[i];
			float t2 = (hi - ox[i]) * dx[i];
			if (t1 > t2) std::swap(t1, t2);
			tmin = (std::max)(tmin, t1);
			tmax = (std::min)(tmax, t2);
			if (tmin > tmax) return false;
		}
		return true;
	}

	bool segment_clear(const math::vector3& origin, const math::vector3& dir,
		float max_dist, float* out_hit)
	{
		std::shared_ptr<const std::vector<geo_part>> geo;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			geo = g_geo;
		}
		if (out_hit) *out_hit = FLT_MAX;
		if (!geo || geo->empty() || max_dist <= 0.f) return true;

		const math::vector3 d = dir.normalized();
		if (!std::isfinite(d.x + d.y + d.z)) return true;
		const math::vector3 inv_d{
			std::fabs(d.x) > 1e-8f ? 1.f / d.x : 1e8f,
			std::fabs(d.y) > 1e-8f ? 1.f / d.y : 1e8f,
			std::fabs(d.z) > 1e-8f ? 1.f / d.z : 1e8f
		};

		float nearest = max_dist;
		bool blocked = false;

		for (const auto& part : *geo)
		{
			if (!ray_aabb(origin, inv_d, part, nearest)) continue;
			for (const auto& t : part.tris)
			{
				float tt = 0.f;
				if (ray_tri(origin, d, t, nearest, tt))
				{
					nearest = tt;
					blocked = true;
				}
			}
		}

		if (blocked && out_hit) *out_hit = nearest;
		return !blocked;
	}

	bool is_visible(const math::vector3& target_pos, const math::vector3& cam_pos)
	{
		math::vector3 dir = target_pos - cam_pos;
		const float dist = dir.length();
		if (dist < 0.5f) return true;
		return segment_clear(cam_pos, dir * (1.f / dist), dist - 0.5f, nullptr);
	}
}
