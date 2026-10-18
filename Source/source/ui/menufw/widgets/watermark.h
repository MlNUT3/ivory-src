#pragma once
#include "../headers/includes.h"
#include <d3d11.h>
#include <chrono>




// Blur Shader HLSL Code
static const char* blur_shader_hlsl = R"(
cbuffer BlurBuffer : register(b0)
{
    float2 resolution;
    float blurAmount;
    float padding;
};

Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target
{
    // Gaussian Blur - 9 tap
    float2 texelSize = 1.0 / resolution * blurAmount;

    float4 color = float4(0.0, 0.0, 0.0, 0.0);

    // Gaussian kernel weights
    float weights[9] = {
        0.0625, 0.125, 0.0625,
        0.125,  0.25,  0.125,
        0.0625, 0.125, 0.0625
    };

    int index = 0;
    for(int x = -1; x <= 1; x++)
    {
        for(int y = -1; y <= 1; y++)
        {
            float2 offset = float2(float(x), float(y)) * texelSize;
            color += tex.Sample(samplerState, input.uv + offset) * weights[index++];
        }
    }

    return color;
}
)";

class c_watermark
{
public:
    void initialize(ID3D11Device* device);
    void render();
    void cleanup();

    // Public so gui.cpp can set it on first render
    void start_timer();

private:
    void create_blur_shader(ID3D11Device* device);
    void render_blurred_background(ImVec2 pos, ImVec2 size);

    // Shader resources
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11SamplerState* sampler_state = nullptr;
    ID3D11BlendState* blend_state = nullptr;

    // Timing
    std::chrono::steady_clock::time_point injection_time;
    bool timer_started = false;
    bool initialized = false;

    // Dragging
    ImVec2 watermark_pos = ImVec2(15.0f, 15.0f);
    bool is_dragging = false;
    ImVec2 drag_offset = ImVec2(0.0f, 0.0f);
    bool placed = false;
};

inline std::unique_ptr<c_watermark> watermark = std::make_unique<c_watermark>();


