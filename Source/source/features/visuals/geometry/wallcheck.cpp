#include "wallcheck.h"

#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <core/system/eject.h>
#include <sdk/offsets/offsets.h>
#include <features/system/settings/settings.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/frame.h>

#include <queue>
#include <memory>
#include <cmath>

namespace wallcheck
{
    // parts are swapped as an immutable shared_ptr snapshot - readers grab the
    // pointer atomically under the mutex (cheap) and scan WITHOUT holding the
    // lock, instead of deep-copying up to 500 parts (2 strings each) per call
    inline std::shared_ptr<const std::vector<cached_part_t>> g_cached_parts;
    inline std::mutex g_mutex;
    inline std::atomic<bool> g_enabled{ false };

    std::shared_ptr<const std::vector<cached_part_t>> get_parts_snapshot()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_cached_parts;
    }

    static bool is_part_class(const std::string& cls)
    {
        return cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" ||
               cls == "WedgePart" || cls == "CornerWedgePart" || cls == "TrussPart";
    }

    static void compute_aabb(cached_part_t& part)
    {
        math::vector3 half = part.size * 0.5f;
        math::matrix3 rot = part.rotation;

        math::vector3 corners[8];
        static const math::vector3 local_corners[] = {
            {-1, -1, -1}, {-1, -1, 1}, {-1, 1, -1}, {-1, 1, 1},
            {1, -1, -1}, {1, -1, 1}, {1, 1, -1}, {1, 1, 1}
        };

        for (int i = 0; i < 8; ++i)
        {
            math::vector3 world;
            world.x = rot.m[0][0] * (half.x * local_corners[i].x) + rot.m[0][1] * (half.y * local_corners[i].y) + rot.m[0][2] * (half.z * local_corners[i].z);
            world.y = rot.m[1][0] * (half.x * local_corners[i].x) + rot.m[1][1] * (half.y * local_corners[i].y) + rot.m[1][2] * (half.z * local_corners[i].z);
            world.z = rot.m[2][0] * (half.x * local_corners[i].x) + rot.m[2][1] * (half.y * local_corners[i].y) + rot.m[2][2] * (half.z * local_corners[i].z);
            corners[i] = part.position + world;
        }

        part.min = corners[0];
        part.max = corners[0];
        for (int i = 1; i < 8; ++i)
        {
            part.min.x = (std::min)(part.min.x, corners[i].x);
            part.min.y = (std::min)(part.min.y, corners[i].y);
            part.min.z = (std::min)(part.min.z, corners[i].z);
            part.max.x = (std::max)(part.max.x, corners[i].x);
            part.max.y = (std::max)(part.max.y, corners[i].y);
            part.max.z = (std::max)(part.max.z, corners[i].z);
        }
    }

    static void scan_tree(rbx::c_instance root, std::vector<cached_part_t>& found)
    {
        std::queue<uint64_t> to_visit;
        to_visit.push(root.address);

        while (!to_visit.empty() && (int)found.size() < 2000)
        {
            uint64_t addr = to_visit.front();
            to_visit.pop();

            if (!addr) continue;

            rbx::c_instance inst(addr);
            std::vector<rbx::c_instance> children = inst.get_children<rbx::c_instance>();

            for (auto& child : children)
            {
                if (!child.address) continue;

                std::string cls = child.get_class_name();

                if (is_part_class(cls))
                {
                    rbx::c_part part(child.address);
                    rbx::c_primitive prim = part.get_primitive();
                    if (!prim.address) continue;

                    math::vector3 pos = prim.get_position();
                    if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) continue;

                    math::vector3 sz = prim.get_size();
                    if (sz.x < 0.05f || sz.y < 0.05f || sz.z < 0.05f) continue;
                    if (sz.x > 100.0f || sz.y > 100.0f || sz.z > 100.0f) continue;

                    cached_part_t obj;
                    obj.position = pos;
                    obj.size = sz;
                    obj.rotation = prim.get_cframe().rotation;
                    obj.name = child.get_name();
                    obj.class_name = cls;
                    obj.address = child.address;

                    compute_aabb(obj);
                    found.push_back(obj);
                }
                else if (cls == "Model" || cls == "Folder" || cls == "Workspace" || cls == "Configuration")
                {
                    to_visit.push(child.address);
                }
            }
        }
    }

    static void scan_thread()
    {
        while (true)
        {
            if (!g_enabled.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (!game::datamodel || game::datamodel->address == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::vector<cached_part_t> found;
            found.reserve(512);

            rbx::c_workspace ws = game::datamodel->get_workspace();
            if (ws.address != 0)
            {
                scan_tree(rbx::c_instance(ws.address), found);
            }

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_cached_parts = std::make_shared<const std::vector<cached_part_t>>(std::move(found));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
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

    bool is_visible(const math::vector3& target_pos, const math::vector3& cam_pos)
    {
        std::shared_ptr<const std::vector<cached_part_t>> parts;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            parts = g_cached_parts;
        }

        if (!parts || parts->empty()) return true;

        math::vector3 dir = target_pos - cam_pos;
        float dist = dir.length();
        if (dist < 0.001f) return true;
        dir = dir * (1.0f / dist);

        for (const auto& part : *parts)
        {
            if (part.address == 0) continue;

            float tmin = 0.0f;
            float tmax = dist;

            bool hit = true;
            for (int i = 0; i < 3; ++i)
            {
                float d = (i == 0) ? dir.x : (i == 1) ? dir.y : dir.z;
                float o = (i == 0) ? cam_pos.x : (i == 1) ? cam_pos.y : cam_pos.z;
                float mn = (i == 0) ? part.min.x : (i == 1) ? part.min.y : part.min.z;
                float mx = (i == 0) ? part.max.x : (i == 1) ? part.max.y : part.max.z;

                if (std::abs(d) < 0.0001f)
                {
                    if (o < mn || o > mx)
                    {
                        hit = false;
                        break;
                    }
                }
                else
                {
                    float inv = 1.0f / d;
                    float t1 = (mn - o) * inv;
                    float t2 = (mx - o) * inv;
                    if (t1 > t2) std::swap(t1, t2);
                    tmin = (std::max)(tmin, t1);
                    tmax = (std::min)(tmax, t2);
                    if (tmin > tmax)
                    {
                        hit = false;
                        break;
                    }
                }
            }

            if (hit && tmin < dist && tmin > 0.1f)
            {
                return false;
            }
        }

        return true;
    }

    void render()
    {
        if (!g_enabled.load(std::memory_order_acquire)) return;
        if (!game::visualengine) return;

        std::shared_ptr<const std::vector<cached_part_t>> parts;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            parts = g_cached_parts;
        }

        if (!parts || parts->empty()) return;

        auto fc = frame_cache::get_for_thread();
        const math::matrix4& view = fc.view;
        const math::vector2& dims = fc.dims;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        const ImU32 col = IM_COL32(255, 0, 255, 255);
        const ImU32 col_fill = IM_COL32(255, 0, 255, 20);

        for (const auto& part : *parts)
        {
            math::vector3 half = part.size * 0.5f;
            math::matrix3 rot = part.rotation;

            float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
            bool has_point = false;

            static const math::vector3 local_corners[] = {
                {-1, -1, -1}, {-1, -1, 1}, {-1, 1, -1}, {-1, 1, 1},
                {1, -1, -1}, {1, -1, 1}, {1, 1, -1}, {1, 1, 1}
            };

            for (const auto& lc : local_corners)
            {
                math::vector3 world;
                world.x = rot.m[0][0] * (half.x * lc.x) + rot.m[0][1] * (half.y * lc.y) + rot.m[0][2] * (half.z * lc.z);
                world.y = rot.m[1][0] * (half.x * lc.x) + rot.m[1][1] * (half.y * lc.y) + rot.m[1][2] * (half.z * lc.z);
                world.z = rot.m[2][0] * (half.x * lc.x) + rot.m[2][1] * (half.y * lc.y) + rot.m[2][2] * (half.z * lc.z);
                world = part.position + world;

                math::vector2 screen{};
                if (game::visualengine->world_to_screen(view, dims, world, screen))
                {
                    has_point = true;
                    min_x = (std::min)(min_x, screen.x);
                    min_y = (std::min)(min_y, screen.y);
                    max_x = (std::max)(max_x, screen.x);
                    max_y = (std::max)(max_y, screen.y);
                }
            }

            if (!has_point) continue;
            if (max_x - min_x < 2.0f || max_y - min_y < 2.0f) continue;

            draw->AddRectFilled(ImVec2(min_x, min_y), ImVec2(max_x, max_y), col_fill);
            draw->AddRect(ImVec2(min_x, min_y), ImVec2(max_x, max_y), col, 0.0f, 0, 1.0f);
        }
    }

    void draw_fov()
    {
    }
}
