#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <sdk/math/math.h>

namespace cache { struct entity_t; }
namespace frame_cache { struct frame_t; }

namespace wallcheck
{
    struct cached_part_t
    {
        math::vector3 position{};
        math::vector3 size{};
        math::matrix3 rotation{};
        math::vector3 min{};
        math::vector3 max{};
        std::string name;
        std::string class_name;
        uint64_t address = 0;
    };

    void run();
    void render();
    bool is_visible(const math::vector3& target_pos, const math::vector3& cam_pos);
    void draw_fov();

    // immutable snapshot of the cached static geometry (panel minimap)
    std::shared_ptr<const std::vector<cached_part_t>> get_parts_snapshot();
}
