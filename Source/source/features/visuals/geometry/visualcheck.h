#pragma once
// visualcheck - exact geometry occlusion engine.
//
// a background thread scans the workspace and builds a world-space triangle
// cache of every part within `settings::visuals::visual_radius` studs of the
// camera (boxes/wedges get exact analytic geometry, MeshParts get their real
// asset mesh triangles). recaches on a fast cadence so moving maps stay fresh.
//
// queries are true raycasts: broadphase ray-vs-AABB per part, then
// Moller-Trumbore ray-vs-triangle, tracking the nearest hit. the cursor query
// casts a tiny 5-ray fan through the actual cursor pixel (the "very small
// fov") and reports the nearest geometry depth - so a triggerbot can compare
// the enemy mesh depth against the wall depth and never fire through geometry.
#include <string>

namespace math { struct vector2; struct vector3; }
namespace frame_cache { struct frame_t; }

namespace visualcheck
{
	struct stats_t
	{
		int parts = 0;
		int triangles = 0;
		float last_scan_ms = 0.f;
		float last_scan_age = 0.f;
	};

	void run();
	void rescan();
	stats_t stats();

	// exact occlusion along a segment: true when NOTHING blocks between
	// origin and origin + dir * max_dist. out_hit receives the nearest
	// geometry distance when blocked.
	bool segment_clear(const math::vector3& origin, const math::vector3& dir,
		float max_dist, float* out_hit = nullptr);

	// point visibility from the camera (ESP / aimbot targeting / triggerbot)
	bool is_visible(const math::vector3& target_pos, const math::vector3& cam_pos);
}
