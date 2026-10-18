#pragma once
#include "includes.h"

class c_font
{
public:
    void update();

    ImFont* get(const std::vector<unsigned char>& font_data, float size);

private:
    struct font_data
    {
        std::vector<unsigned char> data;
        float size;
        ImFont* font;
        const void* src;

        bool operator==(const font_data& other) const
        {
            return src == other.src && size == other.size;
        }
    };

    void add(const std::vector<unsigned char>& font_data, float size);

    std::vector<font_data> data;
    ImVector<ImWchar> glyph_ranges;
};

inline std::unique_ptr<c_font> font = std::make_unique<c_font>();
