#include "../headers/includes.h"
#include "imgui/imgui_freetype.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include "../data/IconsFontAwesome6.h"
#include "../data/IconsFontAwesome6Brands.h"
#include "../data/IconsV2.h"
#include "../data/IconsV2_Defines.h"
#include "../data/fonts.h"

void c_font::update()
{
    // register every size the menu uses up front so the atlas builds once
    static bool primed = false;
    if (!primed)
    {
        primed = true;

        static const float k_sizes[] = { 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 18.0f };
        for (float s : k_sizes)
            add(main_font_data, s);

        var->gui.dpi_changed = true;
    }

    if (var->gui.dpi_changed)
    {
        var->gui.dpi = var->gui.stored_dpi / 100.f;

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        // glyph ranges are identical for every size - build them once and keep
        // them alive for as long as the atlas points at them
        if (glyph_ranges.empty())
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
            builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
            builder.AddRanges(io.Fonts->GetGlyphRangesGreek());
            builder.AddRanges(io.Fonts->GetGlyphRangesThai());
            builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());

            // CJK full range - keeps non-latin track titles rendering
            builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
            builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
            builder.AddRanges(io.Fonts->GetGlyphRangesKorean());

            static const ImWchar extra_ranges[] = {
                0x2000, 0x206F, // General Punctuation
                0x2190, 0x21FF, // Arrows
                0x2600, 0x26FF, // Miscellaneous Symbols
                0x2700, 0x27BF, // Dingbats
                0x3000, 0x303F, // CJK Symbols and Punctuation
                0
            };
            builder.AddRanges(extra_ranges);

            builder.BuildRanges(&glyph_ranges);
        }

        // Load each text font with merged icons
        for (auto& font_t : data)
        {
            if (!font_t.data.empty())
            {
                // Load text font from rawData (NOT compressed)
                ImFontConfig font_config;
                font_config.FontDataOwnedByAtlas = false;
                font_config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_Bitmap;

                font_t.font = io.Fonts->AddFontFromMemoryTTF(
                    (void*)font_t.data.data(),
                    font_t.data.size(),
                    SCALE(font_t.size),
                    &font_config,
                    glyph_ranges.Data
                );

                // Merge IconsV2 FIRST (has priority over FontAwesome for overlapping codes)
                static const ImWchar icons_v2_ranges[] = { ICON_MIN_V2, ICON_MAX_V2, 0 };

                ImFontConfig v2_config;
                v2_config.MergeMode = true;
                v2_config.PixelSnapH = true;
                v2_config.FontDataOwnedByAtlas = false;
                v2_config.GlyphOffset = ImVec2(0, 1);

                io.Fonts->AddFontFromMemoryTTF(
                    (void*)IconsV2,
                    sizeof(IconsV2),
                    SCALE(font_t.size + 2),
                    &v2_config,
                    icons_v2_ranges
                );

                // NOW merge Font Awesome icons (for other icons like CODE)
                static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };

                ImFontConfig fa_config;
                fa_config.MergeMode = true;
                fa_config.PixelSnapH = true;
                fa_config.FontDataOwnedByAtlas = false;
                fa_config.GlyphOffset = ImVec2(-1, 1);

                // Merge Font Awesome Solid (compressed)
                io.Fonts->AddFontFromMemoryCompressedTTF(
                    fa6_solid_compressed_data,
                    fa6_solid_compressed_size,
                    SCALE(font_t.size + 2),
                    &fa_config,
                    icons_ranges
                );
            }
        }

        io.Fonts->Build();
        ImGui_ImplDX11_CreateDeviceObjects();

        var->gui.dpi_changed = false;
    }
}

ImFont* c_font::get(const std::vector<unsigned char>& font_data, float size)
{
    if (font_data.empty())
    {
        return ImGui::GetDefaultFont();
    }

    const void* src = font_data.data();

    for (auto& f : data)
    {
        if (f.src == src && f.size == size)
        {
            return f.font ? f.font : ImGui::GetDefaultFont();
        }
    }

    add(font_data, size);

    var->gui.dpi_changed = true;

    return ImGui::GetDefaultFont();
}

void c_font::add(const std::vector<unsigned char>& font_data, float size)
{
    data.push_back({ font_data, size, nullptr, font_data.data() });
}
