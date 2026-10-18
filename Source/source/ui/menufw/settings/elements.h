#pragma once
#include <string>
#include "imgui/imgui.h"

class c_elements
{
public:

    struct
    {
        c_text name{ "ivory" };
        c_vec2 size{ 1000, 540 };
        float rounding{ 10 };
    } window;
};

inline std::unique_ptr<c_elements> elements = std::make_unique<c_elements>();
