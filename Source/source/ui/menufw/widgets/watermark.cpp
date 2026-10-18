#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "watermark.h"
#include <d3dcompiler.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "d3dcompiler.lib")

void c_watermark::initialize(ID3D11Device* device)
{
    if (initialized)
        return;

    try
    {
        create_blur_shader(device);
        initialized = true;
    }
    catch (...)
    {
        initialized = false;
    }
}

void c_watermark::start_timer()
{
    if (!timer_started)
    {
        injection_time = std::chrono::steady_clock::now();
        timer_started = true;
    }
}

void c_watermark::create_blur_shader(ID3D11Device* device)
{
    if (!device)
        return;

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* error_blob = nullptr;

    // Compile Vertex Shader
    HRESULT hr = D3DCompile(
        blur_shader_hlsl,
        strlen(blur_shader_hlsl),
        nullptr,
        nullptr,
        nullptr,
        "VS",
        "vs_5_0",
        0,
        0,
        &vs_blob,
        &error_blob
    );

    if (SUCCEEDED(hr))
    {
        device->CreateVertexShader(
            vs_blob->GetBufferPointer(),
            vs_blob->GetBufferSize(),
            nullptr,
            &vertex_shader
        );
    }

    if (error_blob) error_blob->Release();
    error_blob = nullptr;

    // Compile Pixel Shader
    hr = D3DCompile(
        blur_shader_hlsl,
        strlen(blur_shader_hlsl),
        nullptr,
        nullptr,
        nullptr,
        "PS",
        "ps_5_0",
        0,
        0,
        &ps_blob,
        &error_blob
    );

    if (SUCCEEDED(hr))
    {
        device->CreatePixelShader(
            ps_blob->GetBufferPointer(),
            ps_blob->GetBufferSize(),
            nullptr,
            &pixel_shader
        );
    }

    if (vs_blob) vs_blob->Release();
    if (ps_blob) ps_blob->Release();
    if (error_blob) error_blob->Release();

    // Create Constant Buffer
    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = sizeof(float) * 4; // resolution (2) + blurAmount (1) + padding (1)
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    device->CreateBuffer(&cb_desc, nullptr, &constant_buffer);

    // Create Sampler State
    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    device->CreateSamplerState(&sampler_desc, &sampler_state);

    // Create Blend State for transparency
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    device->CreateBlendState(&blend_desc, &blend_state);
}

void c_watermark::render()
{
    if (!gui || !draw || !font)
        return;

    float spawn_alpha = var->gui.watermark_spawn_alpha;
    if (spawn_alpha <= 0.01f)
        return;

    float spawn_scale = 0.85f + (spawn_alpha * 0.15f);

    if (!timer_started)
        start_timer();

    try
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        ImFont* text_font = font->get(main_font_data, 13.0f);
        ImFont* small_font = font->get(main_font_data, 12.0f);
        if (!text_font || !small_font)
            return;

        const float fs = 13.0f;

        char fps_num[16];
        sprintf_s(fps_num, "%d", (int)io.Framerate);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - injection_time);
        int hours = (int)(elapsed.count() / 3600);
        int minutes = (int)((elapsed.count() % 3600) / 60);
        int seconds = (int)(elapsed.count() % 60);

        char timer_text[16];
        sprintf_s(timer_text, "%02d:%02d:%02d", hours, minutes, seconds);

        static const char* logo_v    = "V";
        static const char* logo_rest = "ade";
        static const char* fps_label = " fps";
        static const char* build_txt = "b4080";
        static const char* ico_clock = "\xEF\x80\x97"; // U+F017
        static const char* ico_gear  = "\xEF\x80\x93"; // U+F013

        ImVec2 v_sz   = text_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, logo_v);
        ImVec2 ade_sz = text_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, logo_rest);
        ImVec2 fps_sz = text_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, fps_num);
        ImVec2 fpl_sz = text_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, fps_label);
        ImVec2 tm_sz  = text_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, timer_text);
        ImVec2 bld_sz = text_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, build_txt);
        ImVec2 clk_sz = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, ico_clock);
        ImVec2 gr_sz  = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, ico_gear);

        float cell_pad = SCALE(12.0f);
        float gap_ic   = SCALE(6.0f);
        float left_pad = SCALE(13.0f);

        float fps_w = SCALE(5.0f) + gap_ic + fps_sz.x + fpl_sz.x;
        float tm_w  = clk_sz.x + gap_ic + tm_sz.x;
        float bl_w  = gr_sz.x + gap_ic + bld_sz.x;

        float total_width =
            left_pad + v_sz.x + ade_sz.x + cell_pad +
            SCALE(1.0f) + cell_pad + fps_w + cell_pad +
            SCALE(1.0f) + cell_pad + tm_w + cell_pad +
            SCALE(1.0f) + cell_pad + bl_w + cell_pad;
        float box_height = SCALE(32.0f);

        if (!placed)
        {
            placed = true;
            watermark_pos = ImVec2(SCALE(16.0f), SCALE(16.0f));
        }

        ImVec2 pos = ImVec2(watermark_pos.x, watermark_pos.y);
        ImVec2 size = ImVec2(total_width, box_height);

        ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImVec2 scaled_pos = ImVec2(
            center.x + (pos.x - center.x) * spawn_scale,
            center.y + (pos.y - center.y) * spawn_scale
        );
        ImVec2 scaled_size = ImVec2(size.x * spawn_scale, size.y * spawn_scale);

        ImRect watermark_rect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y));

        bool menu_open = var->gui.menu_open;

        if (menu_open)
        {
            bool hovered = watermark_rect.Contains(ImGui::GetMousePos());

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                is_dragging = true;
                drag_offset = ImVec2(
                    ImGui::GetMousePos().x - pos.x,
                    ImGui::GetMousePos().y - pos.y
                );
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                is_dragging = false;

            if (is_dragging)
            {
                watermark_pos.x = (ImGui::GetMousePos().x - drag_offset.x);
                watermark_pos.y = (ImGui::GetMousePos().y - drag_offset.y);

                watermark_pos.x = ImMax(0.0f, ImMin(watermark_pos.x, io.DisplaySize.x - total_width));
                watermark_pos.y = ImMax(0.0f, ImMin(watermark_pos.y, io.DisplaySize.y - box_height));

                pos = ImVec2(watermark_pos.x, watermark_pos.y);

                center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
                scaled_pos = ImVec2(
                    center.x + (pos.x - center.x) * spawn_scale,
                    center.y + (pos.y - center.y) * spawn_scale
                );
                scaled_size = ImVec2(size.x * spawn_scale, size.y * spawn_scale);
                watermark_rect = ImRect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y));
            }
        }

        int a = (int)(spawn_alpha * 255.0f);

        // Soft drop shadow
        float shadow_blur = SCALE(10.0f);
        for (int s = 0; s < 4; s++)
        {
            float p = (float)s / 4.0f;
            float al = (1.0f - p) * 0.30f * spawn_alpha;
            float bl = shadow_blur * p;

            draw_list->AddRectFilled(
                ImVec2(scaled_pos.x - bl, scaled_pos.y - bl),
                ImVec2(scaled_pos.x + scaled_size.x + bl, scaled_pos.y + scaled_size.y + bl),
                IM_COL32(0, 0, 0, (int)(al * 255)),
                SCALE(9.0f) + bl
            );
        }

        // Glass plate
        draw_list->PushClipRect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y), true);
        draw_list->AddRectFilledMultiColor(
            scaled_pos,
            ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y),
            IM_COL32(38, 40, 45, (int)(0.78f * a)),
            IM_COL32(38, 40, 45, (int)(0.78f * a)),
            IM_COL32(24, 26, 30, (int)(0.80f * a)),
            IM_COL32(24, 26, 30, (int)(0.80f * a))
        );
        draw_list->PopClipRect();

        // Border + accent bar
        draw_list->AddRect(
            scaled_pos,
            ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y),
            IM_COL32(255, 255, 255, (int)(0.12f * a)),
            SCALE(9.0f), 0, SCALE(1.0f)
        );

        draw_list->AddRectFilled(
            ImVec2(scaled_pos.x + SCALE(5.0f), scaled_pos.y + SCALE(7.0f)),
            ImVec2(scaled_pos.x + SCALE(8.0f), scaled_pos.y + scaled_size.y - SCALE(7.0f)),
            IM_COL32(143, 200, 255, (int)(0.85f * a)),
            SCALE(1.5f)
        );

        // Layout cursor
        float text_y = scaled_pos.y + (scaled_size.y - v_sz.y * spawn_scale) * 0.5f;
        float x = scaled_pos.x + left_pad * spawn_scale;

        auto sep = [&]()
        {
            float sx = x;
            draw_list->AddLine(
                ImVec2(sx, scaled_pos.y + SCALE(8.0f)),
                ImVec2(sx, scaled_pos.y + scaled_size.y - SCALE(8.0f)),
                IM_COL32(255, 255, 255, (int)(0.13f * a)),
                SCALE(1.0f)
            );
            x += (SCALE(1.0f) + cell_pad) * spawn_scale;
        };

        // Brand
        draw_list->AddText(text_font, fs, ImVec2(x, text_y), IM_COL32(143, 200, 255, a), logo_v);
        x += v_sz.x * spawn_scale;
        draw_list->AddText(text_font, fs, ImVec2(x, text_y), IM_COL32(255, 255, 255, a), logo_rest);
        x += (ade_sz.x + cell_pad) * spawn_scale;

        sep();

        // FPS
        ImVec2 dot = ImVec2(x + SCALE(2.5f), scaled_pos.y + scaled_size.y * 0.5f);
        draw_list->AddCircleFilled(dot, SCALE(2.5f), IM_COL32(76, 242, 76, a));
        draw_list->AddCircleFilled(dot, SCALE(4.5f), IM_COL32(76, 242, 76, (int)(0.16f * a)));
        x += (SCALE(5.0f) + gap_ic) * spawn_scale;

        draw_list->AddText(text_font, fs, ImVec2(x, text_y), IM_COL32(76, 242, 76, a), fps_num);
        x += fps_sz.x * spawn_scale;
        draw_list->AddText(text_font, fs, ImVec2(x, text_y), IM_COL32(255, 255, 255, (int)(0.62f * a)), fps_label);
        x += (fpl_sz.x + cell_pad) * spawn_scale;

        sep();

        // Uptime
        draw_list->AddText(small_font, 12.0f, ImVec2(x, text_y + (v_sz.y - clk_sz.y) * 0.5f * spawn_scale),
            IM_COL32(255, 255, 255, (int)(0.38f * a)), ico_clock);
        x += (clk_sz.x + gap_ic) * spawn_scale;
        draw_list->AddText(text_font, fs, ImVec2(x, text_y), IM_COL32(255, 255, 255, a), timer_text);
        x += (tm_sz.x + cell_pad) * spawn_scale;

        sep();

        // Build tag
        draw_list->AddText(small_font, 12.0f, ImVec2(x, text_y + (v_sz.y - gr_sz.y) * 0.5f * spawn_scale),
            IM_COL32(255, 255, 255, (int)(0.38f * a)), ico_gear);
        x += (gr_sz.x + gap_ic) * spawn_scale;
        draw_list->AddText(text_font, fs, ImVec2(x, text_y),
            IM_COL32(255, 255, 255, (int)(0.62f * a)), build_txt);
    }
    catch (...)
    {
    }
}

void c_watermark::cleanup()
{
    if (vertex_shader) { vertex_shader->Release(); vertex_shader = nullptr; }
    if (pixel_shader) { pixel_shader->Release(); pixel_shader = nullptr; }
    if (constant_buffer) { constant_buffer->Release(); constant_buffer = nullptr; }
    if (sampler_state) { sampler_state->Release(); sampler_state = nullptr; }
    if (blend_state) { blend_state->Release(); blend_state = nullptr; }

    initialized = false;
}
