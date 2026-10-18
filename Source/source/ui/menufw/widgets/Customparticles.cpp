#include "../headers/functions.h"
#include "../headers/widgets.h"

void c_widgets::draw_background_pattern(ImDrawList* draw_list, ImVec2 content_start, ImVec2 content_end)
{
    static float pattern_time = 0.0f;
    pattern_time += ImGui::GetIO().DeltaTime * 0.08f;

    // Clip to content area
    draw_list->PushClipRect(content_start, content_end, true);

    // Enable anti-aliasing for smooth lines
    ImDrawListFlags backup_flags = draw_list->Flags;
    draw_list->Flags |= ImDrawListFlags_AntiAliasedLines;

    ImVec2 content_size = ImVec2(content_end.x - content_start.x, content_end.y - content_start.y);

    // PERFORMANCE: Reduced peak count
    struct HeightPeak {
        ImVec2 position;
        float strength;
        float radius;
    };

    // 6 peaks instead of 8 - less computation
    HeightPeak peaks[] = {
        { ImVec2(content_start.x + content_size.x * 0.25f, content_start.y + content_size.y * 0.25f), 0.8f, SCALE(120.0f) },
        { ImVec2(content_start.x + content_size.x * 0.65f, content_start.y + content_size.y * 0.35f), -0.6f, SCALE(100.0f) },
        { ImVec2(content_start.x + content_size.x * 0.45f, content_start.y + content_size.y * 0.65f), 0.9f, SCALE(130.0f) },
        { ImVec2(content_start.x + content_size.x * 0.15f, content_start.y + content_size.y * 0.55f), -0.5f, SCALE(90.0f) },
        { ImVec2(content_start.x + content_size.x * 0.75f, content_start.y + content_size.y * 0.75f), 0.7f, SCALE(110.0f) },
        { ImVec2(content_start.x + content_size.x * 0.85f, content_start.y + content_size.y * 0.5f), 0.8f, SCALE(105.0f) }
    };

    // Helper function - optimized
    auto getHeight = [&](float x, float y) -> float {
        float height = 0.0f;

        // PERFORMANCE: Loop unrolling would help but keeping it simple
        for (int i = 0; i < 6; i++)
        {
            float dx = x - peaks[i].position.x;
            float dy = y - peaks[i].position.y;

            // Directional stretching
            float angle = atan2f(dy, dx);
            float dir_factor = sinf(angle * (2.0f + i * 0.5f) + i * 0.8f) * 0.4f + 1.0f;
            dx *= dir_factor;

            float perp_factor = cosf(angle * (3.0f - i * 0.3f) + i * 1.2f) * 0.3f + 1.0f;
            dy *= perp_factor;

            float dist_sq = dx * dx + dy * dy; // Avoid sqrt when possible
            float radius_sq = peaks[i].radius * peaks[i].radius;

            // Smooth falloff
            height += peaks[i].strength * expf(-dist_sq / radius_sq);
        }

        // PERFORMANCE: Reduced to 2 noise layers instead of 4
        height += sinf(x * 0.012f + y * 0.008f + pattern_time * 0.4f) * 0.09f;
        height += cosf(x * 0.018f - y * 0.015f - pattern_time * 0.3f) * 0.06f;

        return height;
    };

    // PERFORMANCE: Balanced contour count for good detail
    int contour_count = 20; // More lines for denser pattern
    float height_step = 0.12f; // Smaller steps = more lines
    const int grid_size = 30;
    const int stride = grid_size + 1;
    float cell_width = content_size.x / grid_size;
    float cell_height = content_size.y / grid_size;

    // the height field does not depend on the contour level - sample it once
    static std::vector<float> field;
    field.resize(stride * stride);
    for (int gy = 0; gy <= grid_size; gy++)
    {
        const float py = content_start.y + gy * cell_height;
        for (int gx = 0; gx <= grid_size; gx++)
            field[gy * stride + gx] = getHeight(content_start.x + gx * cell_width, py);
    }

    std::vector<ImVec2> edge_points;
    edge_points.reserve(4);

    for (int level = 0; level < contour_count; level++)
    {
        float target_height = -1.2f + level * height_step;
        float alpha = 0.115f - (level % 4) * 0.012f;

        // PERFORMANCE: Much coarser grid - 30x30 instead of 50x50
        // March through grid
        for (int gy = 0; gy < grid_size; gy++)
        {
            for (int gx = 0; gx < grid_size; gx++)
            {
                float x1 = content_start.x + gx * cell_width;
                float y1 = content_start.y + gy * cell_height;
                float x2 = x1 + cell_width;
                float y2 = y1 + cell_height;

                // corner heights come from the cached field
                const float h1 = field[gy * stride + gx];
                const float h2 = field[gy * stride + gx + 1];
                const float h3 = field[(gy + 1) * stride + gx + 1];
                const float h4 = field[(gy + 1) * stride + gx];

                // Check if contour passes through
                bool above1 = h1 > target_height;
                bool above2 = h2 > target_height;
                bool above3 = h3 > target_height;
                bool above4 = h4 > target_height;

                // Early exit if all same
                if ((above1 == above2 && above2 == above3 && above3 == above4))
                    continue;

                edge_points.clear();

                // Top edge

                if (above1 != above2) {
                    float t = (target_height - h1) / (h2 - h1);
                    edge_points.push_back(ImVec2(x1 + t * cell_width, y1));
                }
                // Right edge
                if (above2 != above3) {
                    float t = (target_height - h2) / (h3 - h2);
                    edge_points.push_back(ImVec2(x2, y1 + t * cell_height));
                }
                // Bottom edge
                if (above3 != above4) {
                    float t = (target_height - h4) / (h3 - h4);
                    edge_points.push_back(ImVec2(x1 + t * cell_width, y2));
                }
                // Left edge
                if (above4 != above1) {
                    float t = (target_height - h1) / (h4 - h1);
                    edge_points.push_back(ImVec2(x1, y1 + t * cell_height));
                }

                // Draw line segments
                if (edge_points.size() >= 2)
                {
                    // Baby blue accent color
                    draw_list->AddLine(
                        edge_points[0],
                        edge_points[1],
                        IM_COL32(143, 200, 255, (int)(alpha * 255)), // RGB(143, 200, 255) = 0.65, 0.40, 0.95
                        SCALE(1.4f)
                    );
                }
            }
        }
    }

    // Restore flags
    draw_list->Flags = backup_flags;

    draw_list->PopClipRect();
}
