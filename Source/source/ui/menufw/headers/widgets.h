#pragma once
#include "includes.h"
#include "../headers/config.h"

#define IMGUI_DEFINE_MATH_OPERATORS

class c_widgets
{
public:
    void checkbox(std::string_view name, bool* value);
    void color_picker(std::string_view name, float* color);
    void slider_int(std::string_view name, int* value, int min, int max);
    void slider_float(std::string_view name, float* value, float min, float max);
    void dropdown(std::string_view name, int* value, const char* items[], int items_count);
    bool button(std::string_view name);
    void draw_background_pattern(ImDrawList* draw_list, ImVec2 content_start, ImVec2 content_end);

    // Tooltip system
    void render_tooltip(std::string_view text, bool hovered);
};

inline std::unique_ptr<c_widgets> widgets = std::make_unique<c_widgets>();

enum notify_type
{
    success = 0,
    warning = 1,
    info = 2
};

struct notify_state
{
    int notify_id;
    std::string text;
    notify_type type{ success };

    ImVec2 window_size{ 0, 0 };
    float notify_alpha{ 0 };
    bool active_notify{ true };
    float notify_timer{ 0 };
    float notify_pos{ 0 };
};

class c_notify
{
public:
    void setup_notify();

    void add_notify(std::string_view text, notify_type type);

private:
    ImVec2 render_notify(int cur_notify_value, float notify_alpha, float notify_percentage, float notify_pos, std::string_view text, notify_type type);

    float notify_time{ 300.0f };  // 5 seconds at 60fps (300 frames)
    int notify_count{ 0 };

    float notify_spacing{ 12.0f };  // Space between notifications
    ImVec2 notify_padding{ 20.0f, 20.0f };  // Padding from screen edges

    std::vector<notify_state> notifications;

};

inline std::unique_ptr<c_notify> notify = std::make_unique<c_notify>();
