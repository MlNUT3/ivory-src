#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "musicplayer.h"
#include "../deezer/deezer.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <thread>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <winhttp.h>




#pragma warning(disable: 4996)  // Disable sscanf warning
#pragma comment(lib, "winhttp.lib")

// STB Image (implementation already in images.cpp)
#include "stb/stb_image.h"

// Forward declare from images.cpp
extern bool LoadTextureFromMemory(const unsigned char* image_data, int image_size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height);

// Get D3D11 device from ImGui backend
struct ImGui_ImplDX11_Data
{
    ID3D11Device*            pd3dDevice;
    ID3D11DeviceContext*     pd3dDeviceContext;
    // ... other fields
};

static ImGui_ImplDX11_Data* GetDX11Data()
{
    return (ImGui_ImplDX11_Data*)ImGui::GetIO().BackendRendererUserData;
}

// Shader code for ambient background
static const char* ambient_vertex_shader = R"(
struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSOut main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(uv.x, 1.0 - uv.y);
    return o;
}
)";

static const char* ambient_pixel_shader = R"(
cbuffer Constants : register(b0) {
    float4 params[8];
    // params[0].xy = card_size (pixels)
    // params[1].xy = center1, params[1].z = radius1
    // params[2] = color1 (rgba, a=strength)
    // params[3].xy = center2, params[3].z = radius2
    // params[4] = color2
    // params[5].xy = center3, params[5].z = radius3
    // params[6] = color3
};

struct PSIn {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

float blob(float2 px, float2 center, float radius, float strength) {
    float d = length(px - center) / max(radius, 1.0);
    return saturate(1.0 - d * d) * strength;
}

float4 main(PSIn input) : SV_Target {
    float2 card_size = params[0].xy;
    float2 px = input.uv * card_size;

    // Base dark color
    float3 col = float3(12.0/255.0, 12.0/255.0, 16.0/255.0);

    // Add color blobs
    col += params[2].rgb * blob(px, params[1].xy, params[1].z, params[2].a);
    col += params[4].rgb * blob(px, params[3].xy, params[3].z, params[4].a);
    col += params[6].rgb * blob(px, params[5].xy, params[5].z, params[6].a);

    return float4(saturate(col), 1.0);
}
)";

// Ambient background implementation
void c_ambient_background::init(ID3D11Device* device)
{
    if (initialized || !device)
        return;

    // Create render texture
    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = TEX_WIDTH;
    tex_desc.Height = TEX_HEIGHT;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&tex_desc, nullptr, &render_texture)))
        return;

    device->CreateRenderTargetView(render_texture, nullptr, &render_target);
    device->CreateShaderResourceView(render_texture, nullptr, &shader_resource);

    // Compile shaders
    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* error = nullptr;

    if (SUCCEEDED(D3DCompile(ambient_vertex_shader, strlen(ambient_vertex_shader), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vs_blob, &error)))
    {
        device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vertex_shader);
        vs_blob->Release();
    }
    if (error) { error->Release(); error = nullptr; }

    if (SUCCEEDED(D3DCompile(ambient_pixel_shader, strlen(ambient_pixel_shader), nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &ps_blob, &error)))
    {
        device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &pixel_shader);
        ps_blob->Release();
    }
    if (error) error->Release();

    // Create constant buffer
    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = 128;
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cb_desc, nullptr, &constant_buffer);

    // Create rasterizer state
    D3D11_RASTERIZER_DESC rast_desc = {};
    rast_desc.FillMode = D3D11_FILL_SOLID;
    rast_desc.CullMode = D3D11_CULL_NONE;
    device->CreateRasterizerState(&rast_desc, &rasterizer_state);

    // Create blend state
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blend_desc, &blend_state);

    initialized = (vertex_shader && pixel_shader && constant_buffer && render_target && shader_resource);
}

void c_ambient_background::render(ID3D11DeviceContext* context, ImVec2 pos, ImVec2 size, float dominant_colors[3][3])
{
    if (!initialized || !context)
        return;

    // Save previous state
    ID3D11RenderTargetView* prev_rtv = nullptr;
    ID3D11DepthStencilView* prev_dsv = nullptr;
    context->OMGetRenderTargets(1, &prev_rtv, &prev_dsv);

    D3D11_VIEWPORT prev_vp = {};
    UINT num_vp = 1;
    context->RSGetViewports(&num_vp, &prev_vp);

    ID3D11BlendState* prev_bs = nullptr;
    float prev_bf[4] = {};
    UINT prev_sm = 0;
    context->OMGetBlendState(&prev_bs, prev_bf, &prev_sm);

    // Set render target
    context->OMSetRenderTargets(1, &render_target, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)TEX_WIDTH;
    vp.Height = (float)TEX_HEIGHT;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
    context->RSSetState(rasterizer_state);
    context->OMSetBlendState(blend_state, nullptr, 0xffffffff);

    // Update constant buffer with animated blob positions
    float time = (float)ImGui::GetTime();
    float cw = size.x;
    float ch = size.y;

    // Animate blob positions
    float cx1 = cw * 0.30f + std::sin(time * 0.25f) * cw * 0.15f;
    float cy1 = ch * 0.20f + std::cos(time * 0.35f) * ch * 0.10f;

    float cx2 = cw * 0.70f + std::cos(time * 0.20f) * cw * 0.15f;
    float cy2 = ch * 0.50f + std::sin(time * 0.30f) * ch * 0.10f;

    float cx3 = cw * 0.50f + std::cos(time * 0.35f) * cw * 0.10f;
    float cy3 = ch * 0.80f + std::sin(time * 0.18f) * ch * 0.10f;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        float* data = (float*)mapped.pData;

        // params[0]: card size
        data[0] = cw; data[1] = ch; data[2] = 0; data[3] = 0;

        // params[1]: blob1 center + radius
        data[4] = cx1; data[5] = cy1; data[6] = 200.0f; data[7] = 0;

        // params[2]: blob1 color + strength
        data[8] = dominant_colors[0][0];
        data[9] = dominant_colors[0][1];
        data[10] = dominant_colors[0][2];
        data[11] = 0.30f; // strength

        // params[3]: blob2 center + radius
        data[12] = cx2; data[13] = cy2; data[14] = 220.0f; data[15] = 0;

        // params[4]: blob2 color + strength
        data[16] = dominant_colors[1][0];
        data[17] = dominant_colors[1][1];
        data[18] = dominant_colors[1][2];
        data[19] = 0.25f;

        // params[5]: blob3 center + radius
        data[20] = cx3; data[21] = cy3; data[22] = 180.0f; data[23] = 0;

        // params[6]: blob3 color + strength
        data[24] = dominant_colors[2][0];
        data[25] = dominant_colors[2][1];
        data[26] = dominant_colors[2][2];
        data[27] = 0.22f;

        context->Unmap(constant_buffer, 0);
    }

    // Draw fullscreen triangle
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(vertex_shader, nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constant_buffer);
    context->Draw(3, 0);

    // Restore previous state
    context->OMSetRenderTargets(1, &prev_rtv, prev_dsv);
    context->RSSetViewports(1, &prev_vp);
    context->OMSetBlendState(prev_bs, prev_bf, prev_sm);

    if (prev_rtv) prev_rtv->Release();
    if (prev_dsv) prev_dsv->Release();
    if (prev_bs) prev_bs->Release();
}

void c_ambient_background::cleanup()
{
    if (blend_state) { blend_state->Release(); blend_state = nullptr; }
    if (rasterizer_state) { rasterizer_state->Release(); rasterizer_state = nullptr; }
    if (constant_buffer) { constant_buffer->Release(); constant_buffer = nullptr; }
    if (pixel_shader) { pixel_shader->Release(); pixel_shader = nullptr; }
    if (vertex_shader) { vertex_shader->Release(); vertex_shader = nullptr; }
    if (shader_resource) { shader_resource->Release(); shader_resource = nullptr; }
    if (render_target) { render_target->Release(); render_target = nullptr; }
    if (render_texture) { render_texture->Release(); render_texture = nullptr; }
    initialized = false;
}

void c_musicplayer::setup()
{
    if (initialized)
        return;

    // Initialize lyrics variables
    lyrics_available = false;
    lyrics_synced = false;
    lyrics_fetching = false;
    current_lyric_line = -1;
    last_lyrics_track = "";
    lyrics_scroll_y = 0.0f;
    lyrics_scroll_target = 0.0f;
    synced_lyrics.clear();
    plain_lyrics.clear();
    lyrics_height = SCALE(110.0f);
    lyrics_expanded = false;

    try
    {
        winrt::init_apartment();
        session_manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        initialized = true;
    }
    catch (...)
    {
        initialized = false;
    }
}

void c_musicplayer::shutdown()
{
    if (thumbnail_buffer)
    {
        free(thumbnail_buffer);
        thumbnail_buffer = nullptr;
    }

    if (thumbnail_texture)
    {
        thumbnail_texture->Release();
        thumbnail_texture = nullptr;
    }

    // Cleanup ambient background
    ambient_bg.cleanup();

    session = nullptr;
    session_manager = nullptr;

    if (initialized)
    {
        winrt::uninit_apartment();
        initialized = false;
    }
}

std::string c_musicplayer::format_time(uint64_t time_100ns)
{
    if (time_100ns == 0)
        return "0:00";

    std::chrono::system_clock::duration duration(time_100ns);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration).count();

    if (minutes >= 60)
    {
        const auto hours = std::chrono::duration_cast<std::chrono::hours>(duration).count();
        char buf[32];
        sprintf_s(buf, "%02lld:%02lld:%02lld", hours, minutes - (60 * hours), seconds - (60 * minutes));
        return std::string(buf);
    }

    char buf[32];
    sprintf_s(buf, "%lld:%02lld", minutes, seconds - (60 * minutes));
    return std::string(buf);
}

float c_musicplayer::get_smooth_progress()
{
    if (total_time == 0)
        return 0.0f;

    if (current_time != last_update_time)
    {
        last_update_time    = current_time;
        last_known_position = current_time;
        last_update_timestamp = (float)ImGui::GetTime();
    }

    float current_display_time = 0.0f;

    if (playback_status == 4) // Playing
    {
        float time_since_update = (float)ImGui::GetTime() - last_update_timestamp;
        uint64_t interpolated = last_known_position + (uint64_t)(time_since_update * 10000000.0);
        if (interpolated > total_time) interpolated = total_time;
        current_display_time = (float)interpolated / 10000000.0f;
    }
    else
    {
        current_display_time = (float)current_time / 10000000.0f;
    }

    float total_secs = (float)total_time / 10000000.0f;
    if (total_secs <= 0.0f) return 0.0f;
    return ImClamp(current_display_time / total_secs, 0.0f, 1.0f);
}

void c_musicplayer::load_deezer_cover()
{
    if (!deezer || !deezer->active)
        return;

    const std::string path = deezer->cover_file();
    if (path.empty() || path == last_deezer_cover)
        return;

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    const DWORD size = GetFileSize(file, nullptr);
    if (size == 0 || size > 8u * 1024u * 1024u)
    {
        CloseHandle(file);
        return;
    }

    std::vector<char> bytes(size);
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes.data(), size, &read, nullptr);
    CloseHandle(file);

    if (!ok || read != size)
        return;

    void* fresh = malloc(size);
    if (!fresh)
        return;

    if (thumbnail_buffer)
        free(thumbnail_buffer);

    memcpy(fresh, bytes.data(), size);
    thumbnail_buffer = fresh;
    thumbnail_size = size;
    last_thumb_size = size;
    thumbnail_updated = true;
    last_deezer_cover = path;
}

void c_musicplayer::update_thumbnail_texture()
{
    if (!thumbnail_updated || !thumbnail_buffer || thumbnail_size == 0)
        return;

    ID3D11ShaderResourceView* srv = nullptr;
    if (LoadTextureFromMemory((const unsigned char*)thumbnail_buffer, thumbnail_size, &srv, &thumbnail_width, &thumbnail_height))
    {
        if (thumbnail_texture)
        {
            thumbnail_texture->Release();
        }
        thumbnail_texture = srv;

        // Extract dominant colors from the new thumbnail
        extract_dominant_colors();
    }

    thumbnail_updated = false;
}

void c_musicplayer::extract_dominant_colors()
{
    if (!thumbnail_buffer || thumbnail_size == 0)
        return;

    try
    {
        // Load image data
        int width, height, channels;
        unsigned char* pixels = stbi_load_from_memory(
            (const unsigned char*)thumbnail_buffer,
            thumbnail_size,
            &width,
            &height,
            &channels,
            3  // Force RGB
        );

        if (!pixels)
            return;

        // Sample colors from different regions
        struct ColorSample { int r, g, b, count; };
        ColorSample samples[9] = {}; // 3x3 grid

        int step_x = width / 3;
        int step_y = height / 3;

        for (int grid_y = 0; grid_y < 3; grid_y++)
        {
            for (int grid_x = 0; grid_x < 3; grid_x++)
            {
                int idx = grid_y * 3 + grid_x;

                // Sample from center of each grid cell
                int start_x = grid_x * step_x;
                int start_y = grid_y * step_y;
                int end_x = start_x + step_x;
                int end_y = start_y + step_y;

                // Sample every 4th pixel to speed up
                for (int y = start_y; y < end_y; y += 4)
                {
                    for (int x = start_x; x < end_x; x += 4)
                    {
                        if (x >= width || y >= height)
                            continue;

                        int pixel_idx = (y * width + x) * 3;
                        samples[idx].r += pixels[pixel_idx + 0];
                        samples[idx].g += pixels[pixel_idx + 1];
                        samples[idx].b += pixels[pixel_idx + 2];
                        samples[idx].count++;
                    }
                }

                // Average
                if (samples[idx].count > 0)
                {
                    samples[idx].r /= samples[idx].count;
                    samples[idx].g /= samples[idx].count;
                    samples[idx].b /= samples[idx].count;
                }
            }
        }

        // Find 3 regions with highest saturation
        int best_indices[3] = {0, 1, 2};
        float best_sats[3] = {0, 0, 0};

        for (int i = 0; i < 9; i++)
        {
            // Calculate saturation
            int max_c = (std::max)(samples[i].r, (std::max)(samples[i].g, samples[i].b));
            int min_c = (std::min)(samples[i].r, (std::min)(samples[i].g, samples[i].b));
            float sat = 0.0f;
            if (max_c > 0)
                sat = (float)(max_c - min_c) / (float)max_c;

            for (int j = 0; j < 3; j++)
            {
                if (sat > best_sats[j])
                {
                    // Shift down
                    for (int k = 2; k > j; k--)
                    {
                        best_sats[k] = best_sats[k-1];
                        best_indices[k] = best_indices[k-1];
                    }
                    best_sats[j] = sat;
                    best_indices[j] = i;
                    break;
                }
            }
        }

        // Set dominant colors (boost saturation and darken)
        for (int i = 0; i < 3; i++)
        {
            int idx = best_indices[i];
            float r = samples[idx].r / 255.0f;
            float g = samples[idx].g / 255.0f;
            float b = samples[idx].b / 255.0f;

            // Boost saturation
            float max_c = (std::max)(r, (std::max)(g, b));
            float min_c = (std::min)(r, (std::min)(g, b));
            float l = (max_c + min_c) * 0.5f;

            if (max_c > min_c)
            {
                float s = (l > 0.5f) ? (max_c - min_c) / (2.0f - max_c - min_c) : (max_c - min_c) / (max_c + min_c);
                s = (std::min)(s * 1.3f, 1.0f); // Boost saturation by 30%

                float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
                float x = c * (1.0f - std::abs(std::fmod(0.0f, 2.0f) - 1.0f));
                float m = l - c * 0.5f;

                // Reconstruct RGB with boosted saturation
                if (r >= g && r >= b)
                {
                    r = (r - min_c) / (max_c - min_c) * c + m;
                    g = (g - min_c) / (max_c - min_c) * c + m;
                    b = (b - min_c) / (max_c - min_c) * c + m;
                }
            }

            // Darken a bit for better contrast
            r *= 0.85f;
            g *= 0.85f;
            b *= 0.85f;

            dominant_colors[i][0] = r;
            dominant_colors[i][1] = g;
            dominant_colors[i][2] = b;
        }

        stbi_image_free(pixels);
    }
    catch (...)
    {
        // Keep default colors on error
    }
}

void c_musicplayer::update_media_info()
{
    if (deezer && deezer->active)
    {
        const bool changed = (title != deezer->now_title || artist != deezer->now_artist);

        has_media = true;
        title = deezer->now_title.empty() ? "No Media Playing" : deezer->now_title;
        artist = deezer->now_artist.empty() ? "â€”" : deezer->now_artist;
        playback_status = deezer->is_playing() ? 4 : 5;
        shuffle_active = false;

        total_time = (uint64_t)deezer->length_ms() * 10000ULL;
        current_time = (uint64_t)deezer->position_ms() * 10000ULL;

        if (changed && !deezer->now_title.empty())
            fetch_lyrics_async();

        load_deezer_cover();
        return;
    }

    if (!initialized || !session_manager)
        return;

    try
    {
        session = session_manager.GetCurrentSession();

        if (session)
        {
            has_media = true;

            auto info = session.TryGetMediaPropertiesAsync().get();
            std::string new_title = winrt::to_string(info.Title());
            std::string new_artist = winrt::to_string(info.Artist());

            // Check if track changed - fetch lyrics
            if (new_title != title || new_artist != artist)
            {
                title = new_title;
                artist = new_artist;

                if (title.empty())
                    title = "No Media Playing";
                if (artist.empty())
                    artist = "â€”";

                // Fetch lyrics when track changes
                if (!title.empty() && title != "No Media Playing" && !artist.empty() && artist != "â€”")
                {
                    fetch_lyrics_async();
                }
                else
                {
                    lyrics_available = false;
                    synced_lyrics.clear();
                    plain_lyrics.clear();
                }
            }

            // Thumbnail
            if (info.Thumbnail())
            {
                auto thumbnailStream = info.Thumbnail().OpenReadAsync().get();
                winrt::Windows::Storage::Streams::Buffer buffer = winrt::Windows::Storage::Streams::Buffer(thumbnailStream.Size());
                thumbnailStream.ReadAsync(buffer, buffer.Capacity(), winrt::Windows::Storage::Streams::InputStreamOptions::ReadAhead).get();

                thumbnail_size = buffer.Length();

                if (thumbnail_size != last_thumb_size)
                {
                    if (thumbnail_buffer)
                    {
                        free(thumbnail_buffer);
                    }

                    thumbnail_buffer = malloc(buffer.Length());
                    memcpy(thumbnail_buffer, buffer.data(), buffer.Length());
                    thumbnail_updated = true;
                    last_thumb_size = thumbnail_size;
                }
            }
            else
            {
                thumbnail_size = 0;
            }

            // Playback info
            const auto playbackInfo = session.GetPlaybackInfo();
            playback_status = static_cast<int>(playbackInfo.PlaybackStatus());

            // Shuffle state
            if (playbackInfo.IsShuffleActive())
            {
                shuffle_active = playbackInfo.IsShuffleActive().Value();
            }

            // Timeline
            const auto timelineProperties = session.GetTimelineProperties();
            total_time = timelineProperties.EndTime().count();
            current_time = timelineProperties.Position().count();
        }
        else
        {
            has_media = false;
            title = "No Media Playing";
            artist = "â€”";
        }
    }
    catch (...)
    {
        has_media = false;
    }
}

void c_musicplayer::update()
{
    update_media_info();
    update_thumbnail_texture();
}

void c_musicplayer::toggle_play_pause()
{
    if (deezer && deezer->active)
    {
        deezer->toggle_play_pause();
        return;
    }

    if (!has_media || !session)
        return;

    try
    {
        session.TryTogglePlayPauseAsync();
    }
    catch (...) {}
}

void c_musicplayer::skip_next()
{
    if (deezer && deezer->active)
    {
        deezer->next();
        return;
    }

    if (!has_media || !session)
        return;

    try
    {
        session.TrySkipNextAsync();
    }
    catch (...) {}
}

void c_musicplayer::skip_previous()
{
    if (deezer && deezer->active)
    {
        deezer->previous();
        return;
    }

    if (!has_media || !session)
        return;

    try
    {
        session.TrySkipPreviousAsync();
    }
    catch (...) {}
}

void c_musicplayer::set_position(float progress)
{
    if (deezer && deezer->active)
    {
        deezer->seek(progress);
        return;
    }

    if (!has_media || !session || total_time == 0)
        return;

    try
    {
        session.TryChangePlaybackPositionAsync(static_cast<int64_t>(total_time * progress));
    }
    catch (...) {}
}

void c_musicplayer::toggle_shuffle()
{
    if (deezer && deezer->active)
        return;

    if (!has_media || !session)
        return;

    try
    {
        // Get current shuffle state from playback info
        const auto playbackInfo = session.GetPlaybackInfo();
        bool currentShuffleState = false;

        if (playbackInfo.IsShuffleActive())
        {
            currentShuffleState = playbackInfo.IsShuffleActive().Value();
        }

        // Toggle shuffle state
        session.TryChangeShuffleActiveAsync(!currentShuffleState);
    }
    catch (...) {}
}

void c_musicplayer::render()
{
    if (!gui || !draw || !font)
        return;

    float anim_alpha = var->gui.spotify_spawn_alpha;
    if (anim_alpha <= 0.01f)
        return;

    float scale = 0.85f + (anim_alpha * 0.15f);

    if (!initialized)
        setup();

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    try
    {
        ImGuiIO& io = ImGui::GetIO();

        // Get D3D11 device for ambient background
        auto* backend_data = GetDX11Data();
        if (backend_data && backend_data->pd3dDevice)
        {
            ambient_bg.init(backend_data->pd3dDevice);
        }

        // Fonts
        ImFont* text_font = font->get(main_font_data, 13.0f);
        ImFont* title_font = font->get(main_font_data, 15.0f);
        if (!text_font || !title_font)
            return;

        float base_height = SCALE(110.0f);
        float lyrics_expanded_height = SCALE(350.0f);
        float target_height = lyrics_expanded ? lyrics_expanded_height : base_height;
        gui->easing(lyrics_height, target_height, 12.0f, dynamic_easing);

        float width = SCALE(380.0f);
        float height = lyrics_height;
        float padding = SCALE(12.0f);

        if (!placed && var->gui.menu_rect_valid)
        {
            placed = true;
            player_pos = ImVec2(
                var->gui.menu_rect_min.x + var->gui.menu_rect_size.x * 0.5f - width * 0.5f,
                var->gui.menu_rect_min.y + var->gui.menu_rect_size.y + SCALE(16.0f));
        }

        ImVec2 pos  = player_pos;
        ImVec2 size = ImVec2(width, height);

        ImVec2 center     = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImVec2 scaled_pos = ImVec2(
            center.x + (pos.x - center.x) * scale,
            center.y + (pos.y - center.y) * scale);
        ImVec2 scaled_size = ImVec2(size.x * scale, size.y * scale);

        ImRect player_rect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y));

        bool hovered   = player_rect.Contains(ImGui::GetMousePos());
        bool menu_open = var->gui.menu_open;

        if (menu_open && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            is_dragging = true;
            drag_offset = ImVec2(ImGui::GetMousePos().x - pos.x, ImGui::GetMousePos().y - pos.y);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            is_dragging = false;

        if (is_dragging)
        {
            player_pos.x = ImClamp(ImGui::GetMousePos().x - drag_offset.x, 0.0f, io.DisplaySize.x - width);
            player_pos.y = ImClamp(ImGui::GetMousePos().y - drag_offset.y, 0.0f, io.DisplaySize.y - height);
            pos = player_pos;

            // Scaled values aktualisieren
            center     = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
            scaled_pos = ImVec2(center.x + (pos.x - center.x) * scale,
                                center.y + (pos.y - center.y) * scale);
            scaled_size = ImVec2(size.x * scale, size.y * scale);
            player_rect = ImRect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y));
        }

        gui->easing(hover_alpha, hovered ? 1.0f : 0.0f, 10.0f, dynamic_easing);

        float shadow_blur = SCALE(12.0f);
        for (int s = 0; s < 6; s++)
        {
            float progress   = (float)s / 6.0f;
            float alpha      = (1.0f - progress) * 0.25f * anim_alpha;
            float blur_amount = shadow_blur * progress;

            draw_list->AddRectFilled(
                ImVec2(scaled_pos.x - blur_amount, scaled_pos.y - blur_amount),
                ImVec2(scaled_pos.x + scaled_size.x + blur_amount, scaled_pos.y + scaled_size.y + blur_amount),
                IM_COL32(0, 0, 0, (int)(alpha * 255)),
                SCALE(12.0f) + blur_amount
            );
        }

        draw_list->PushClipRect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y), true);

        // Redefine pos/size to scaled for all inner draws:
        float dx = scaled_pos.x - pos.x;
        float dy = scaled_pos.y - pos.y;
        pos  = scaled_pos;
        size = scaled_size;

        // Render ambient background with shader
        if (backend_data && backend_data->pd3dDeviceContext)
        {
            ambient_bg.render(backend_data->pd3dDeviceContext, pos, size, dominant_colors);

            // Draw the ambient texture as background
            if (ambient_bg.shader_resource)
            {
                float u_max = size.x / c_ambient_background::TEX_WIDTH;
                float v_max = size.y / c_ambient_background::TEX_HEIGHT;

                draw_list->AddImageRounded(
                    (ImTextureID)ambient_bg.shader_resource,
                    pos,
                    ImVec2(pos.x + size.x, pos.y + size.y),
                    ImVec2(0, 0),
                    ImVec2(u_max, v_max),
                    IM_COL32(255, 255, 255, 255),
                    SCALE(12.0f)
                );
            }
        }

        ImVec4 overlay = ImVec4(66.0f / 255.0f, 68.0f / 255.0f, 72.0f / 255.0f, 0.55f * anim_alpha);
        draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), draw->get_clr(overlay), SCALE(12.0f));

        // Border glow
        ImVec4 color1 = ImVec4(dominant_colors[0][0], dominant_colors[0][1], dominant_colors[0][2], 1.0f);
        ImVec4 glow_color = ImVec4(color1.x, color1.y, color1.z, 0.25f * anim_alpha);
        draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), draw->get_clr(glow_color), SCALE(12.0f), 0, SCALE(1.5f));

        // ========== Album cover ==========
        const float cover_size = SCALE(86.0f);
        const ImVec2 cover_pos = ImVec2(pos.x + padding, pos.y + padding);

        if (thumbnail_texture && thumbnail_size > 0)
        {
            // Draw actual album cover
            draw_list->AddImageRounded(
                (ImTextureID)thumbnail_texture,
                cover_pos,
                ImVec2(cover_pos.x + cover_size, cover_pos.y + cover_size),
                ImVec2(0, 0),
                ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255),
                SCALE(6.0f)
            );
        }
        else
        {
            // Placeholder
            draw_list->AddRectFilled(cover_pos, ImVec2(cover_pos.x + cover_size, cover_pos.y + cover_size), IM_COL32(54, 56, 60, 255), SCALE(6.0f));

            const char* placeholder = "?";
            ImVec2 placeholder_size = title_font->CalcTextSizeA(36.0f, FLT_MAX, 0.0f, placeholder);
            ImVec2 placeholder_pos = ImVec2(
                cover_pos.x + (cover_size - placeholder_size.x) * 0.5f,
                cover_pos.y + (cover_size - placeholder_size.y) * 0.5f
            );
            draw_list->AddText(title_font, 36.0f, placeholder_pos, IM_COL32(100, 100, 110, 255), placeholder);
        }

        // ========== Info section ==========
        float info_x = cover_pos.x + cover_size + padding;
        float info_width = width - cover_size - padding * 3;

        // Title
        const char* title_text = title.c_str();
        ImVec2 title_pos = ImVec2(info_x, pos.y + padding);
        ImVec4 text_white = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        draw_list->AddText(text_font, 13.0f, title_pos, draw->get_clr(text_white), title_text);

        // Artist
        const char* artist_text = artist.c_str();
        ImVec2 artist_pos = ImVec2(info_x, title_pos.y + SCALE(16.0f));
        ImVec4 text_gray = ImVec4(0.7f, 0.7f, 0.75f, 1.0f);
        draw_list->AddText(text_font, 11.0f, artist_pos, draw->get_clr(text_gray), artist_text);

        // Progress bar
        float progress_y = artist_pos.y + SCALE(22.0f);
        float progress_width = info_width;
        float progress_height = SCALE(3.0f);

        ImRect progress_rect(ImVec2(info_x, progress_y), ImVec2(info_x + progress_width, progress_y + progress_height));
        bool progress_hovered = progress_rect.Contains(ImGui::GetMousePos());
        gui->easing(progress_hover, progress_hovered ? 1.0f : 0.0f, 15.0f, dynamic_easing);

        // Progress click
        if (progress_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && total_time > 0)
        {
            float click_x = ImGui::GetMousePos().x - progress_rect.Min.x;
            float progress = ImClamp(click_x / progress_width, 0.0f, 1.0f);
            set_position(progress);
        }

        // Progress background
        draw_list->AddRectFilled(progress_rect.Min, progress_rect.Max, IM_COL32(255, 255, 255, 30), SCALE(2.0f));

        // Progress fill (smooth interpolated)
        float fill_progress = get_smooth_progress();
        float fill_width = progress_width * fill_progress;
        draw_list->AddRectFilled(
            progress_rect.Min,
            ImVec2(progress_rect.Min.x + fill_width, progress_rect.Max.y),
            IM_COL32(255, 255, 255, 200),
            SCALE(2.0f)
        );

        // Time labels
        float time_y = progress_y + SCALE(6.0f);

        // Current time (smooth)
        float current_display_seconds = (total_time > 0) ? ((total_time / 10000000.0f) * fill_progress) : 0.0f;
        int current_minutes = (int)(current_display_seconds / 60.0f);
        int current_seconds = (int)(current_display_seconds) % 60;
        char current_time_str[16];
        snprintf(current_time_str, sizeof(current_time_str), "%d:%02d", current_minutes, current_seconds);

        std::string total_time_str = format_time(total_time);

        draw_list->AddText(text_font, 10.0f, ImVec2(info_x, time_y), draw->get_clr(text_gray), current_time_str);

        ImVec2 total_size = text_font->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, total_time_str.c_str());
        draw_list->AddText(text_font, 10.0f, ImVec2(info_x + progress_width - total_size.x, time_y), draw->get_clr(text_gray), total_time_str.c_str());

        // Control buttons - smaller and better centered
        float button_y = time_y + SCALE(13.0f);
        float button_size = SCALE(24.0f);
        float button_spacing = SCALE(10.0f);

        // Center buttons horizontally in the info area (4 buttons now: shuffle + prev + play + next)
        float total_buttons_width = button_size * 4 + button_spacing * 3;
        float buttons_start_x = info_x + (progress_width - total_buttons_width) * 0.5f;

        {
            ImVec2 btn_center = ImVec2(buttons_start_x + button_size * 0.5f, button_y + button_size * 0.5f);
            ImRect btn_rect(ImVec2(btn_center.x - button_size * 0.5f, btn_center.y - button_size * 0.5f),
                           ImVec2(btn_center.x + button_size * 0.5f, btn_center.y + button_size * 0.5f));
            bool btn_hovered = btn_rect.Contains(ImGui::GetMousePos());
            gui->easing(shuffle_hover, btn_hovered ? 1.0f : 0.0f, 15.0f, dynamic_easing);

            if (btn_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                toggle_shuffle();
            }

            // Icon U+F09E (Shuffle) - perfectly centered
            const char* icon = "\xEF\x82\x9E";
            float icon_size = 12.0f;
            ImVec2 ic_text_size = text_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon);
            ImVec2 ic_pos = ImVec2(
                btn_center.x - ic_text_size.x * 0.5f,
                btn_center.y - ic_text_size.y * 0.5f
            );

            ImU32 icon_color;
            if (shuffle_active)
            {
                int base_alpha = 220;
                int hover_alpha = (int)(shuffle_hover * 35);
                icon_color = IM_COL32(50, 255, 100, base_alpha + hover_alpha);
            }
            else
            {
                int alpha = (int)(180 + (shuffle_hover * 75));
                icon_color = IM_COL32(255, 255, 255, alpha);
            }

            draw_list->AddText(text_font, icon_size, ic_pos, icon_color, icon);
        }

        // Previous button (Skip Left) - U+F09B
        {
            ImVec2 btn_center = ImVec2(buttons_start_x + button_size + button_spacing + button_size * 0.5f, button_y + button_size * 0.5f);
            ImRect btn_rect(ImVec2(btn_center.x - button_size * 0.5f, btn_center.y - button_size * 0.5f),
                           ImVec2(btn_center.x + button_size * 0.5f, btn_center.y + button_size * 0.5f));
            bool btn_hovered = btn_rect.Contains(ImGui::GetMousePos());
            gui->easing(prev_hover, btn_hovered ? 1.0f : 0.0f, 15.0f, dynamic_easing);

            if (btn_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                skip_previous();
            }

            // Icon U+F09B (Skip Left) - perfectly centered
            const char* icon = "\xEF\x82\x9B";
            float icon_size = 12.0f;
            ImVec2 ic_text_size = text_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon);
            ImVec2 ic_pos = ImVec2(
                btn_center.x - ic_text_size.x * 0.5f,
                btn_center.y - ic_text_size.y * 0.5f
            );
            int alpha = (int)(180 + (prev_hover * 75));
            draw_list->AddText(text_font, icon_size, ic_pos, IM_COL32(255, 255, 255, alpha), icon);
        }

        // Play/Pause button - slightly larger and centered
        {
            float play_size = SCALE(28.0f);
            ImVec2 btn_center = ImVec2(buttons_start_x + (button_size + button_spacing) * 2 + button_size * 0.5f, button_y + button_size * 0.5f);
            ImRect btn_rect(ImVec2(btn_center.x - play_size * 0.5f, btn_center.y - play_size * 0.5f),
                           ImVec2(btn_center.x + play_size * 0.5f, btn_center.y + play_size * 0.5f));
            bool btn_hovered = btn_rect.Contains(ImGui::GetMousePos());
            gui->easing(play_hover, btn_hovered ? 1.0f : 0.0f, 15.0f, dynamic_easing);

            if (btn_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                toggle_play_pause();
            }

            // Play or pause icon based on state - perfectly centered
            bool is_playing = (playback_status == 4);
            const char* icon = is_playing ? "\xEF\x82\x9D" : "\xEF\x82\x9C"; // U+F09D (Pause) or U+F09C (Play)

            float icon_size = 14.0f;
            ImVec2 ic_text_size = text_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon);
            ImVec2 ic_pos = ImVec2(
                btn_center.x - ic_text_size.x * 0.5f,
                btn_center.y - ic_text_size.y * 0.5f
            );
            int alpha = (int)(200 + (play_hover * 55));
            draw_list->AddText(text_font, icon_size, ic_pos, IM_COL32(255, 255, 255, alpha), icon);
        }

        // Next button (Skip Right) - U+F09A
        {
            ImVec2 btn_center = ImVec2(buttons_start_x + (button_size + button_spacing) * 3 + button_size * 0.5f, button_y + button_size * 0.5f);
            ImRect btn_rect(ImVec2(btn_center.x - button_size * 0.5f, btn_center.y - button_size * 0.5f),
                           ImVec2(btn_center.x + button_size * 0.5f, btn_center.y + button_size * 0.5f));
            bool btn_hovered = btn_rect.Contains(ImGui::GetMousePos());
            gui->easing(next_hover, btn_hovered ? 1.0f : 0.0f, 15.0f, dynamic_easing);

            if (btn_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                skip_next();
            }

            // Icon U+F09A (Skip Right) - perfectly centered
            const char* icon = "\xEF\x82\x9A";
            float icon_size = 12.0f;
            ImVec2 ic_text_size = text_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon);
            ImVec2 ic_pos = ImVec2(
                btn_center.x - ic_text_size.x * 0.5f,
                btn_center.y - ic_text_size.y * 0.5f
            );
            int alpha = (int)(180 + (next_hover * 75));
            draw_list->AddText(text_font, icon_size, ic_pos, IM_COL32(255, 255, 255, alpha), icon);
        }

        float lyrics_btn_size = SCALE(24.0f);
        float lyrics_btn_y = pos.y + base_height - SCALE(30.0f);
        ImVec2 lyrics_btn_pos = ImVec2(pos.x + width - SCALE(34.0f), lyrics_btn_y);
        ImRect lyrics_btn_rect(lyrics_btn_pos, ImVec2(lyrics_btn_pos.x + lyrics_btn_size, lyrics_btn_pos.y + lyrics_btn_size));
        bool lyrics_btn_hovered = lyrics_btn_rect.Contains(ImGui::GetMousePos());

        // Hover animation
        gui->easing(lyrics_hover, lyrics_btn_hovered ? 1.0f : 0.0f, 15.0f, dynamic_easing);

        // Button click
        if (lyrics_btn_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            lyrics_expanded = !lyrics_expanded;
        }

        const char* icon_text = "aA";
        float icon_size = 11.0f;
        ImVec2 icon_text_size = text_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon_text);
        ImVec2 icon_pos = ImVec2(
            lyrics_btn_pos.x + (lyrics_btn_size - icon_text_size.x) * 0.5f,
            lyrics_btn_pos.y + (lyrics_btn_size - icon_text_size.y) * 0.5f - SCALE(2.0f)
        );

        int base_alpha = lyrics_expanded ? 255 : 150;
        int dot_alpha = (int)(base_alpha + lyrics_hover * (255 - base_alpha));
        draw_list->AddText(text_font, icon_size, icon_pos, IM_COL32(255, 255, 255, dot_alpha), icon_text);

        if (lyrics_expanded)
        {
            float dot_radius = SCALE(1.5f);
            ImVec2 dot_pos = ImVec2(
                lyrics_btn_pos.x + lyrics_btn_size * 0.5f,
                lyrics_btn_pos.y + lyrics_btn_size * 0.5f + SCALE(7.0f)
            );
            draw_list->AddCircleFilled(dot_pos, dot_radius, IM_COL32(255, 255, 255, 255));
        }

        if (lyrics_expanded && lyrics_height > base_height + SCALE(20.0f))
        {
            float lyrics_content_y = pos.y + base_height + SCALE(10.0f);
            float lyrics_content_height = lyrics_height - base_height - SCALE(20.0f);
            ImVec2 lyrics_area_pos = ImVec2(pos.x + SCALE(20.0f), lyrics_content_y);
            ImVec2 lyrics_area_size = ImVec2(width - SCALE(40.0f), lyrics_content_height);

            draw_list->PushClipRect(lyrics_area_pos, ImVec2(lyrics_area_pos.x + lyrics_area_size.x, lyrics_area_pos.y + lyrics_area_size.y), true);

            if (!lyrics_available)
            {
                if (lyrics_fetching)
                {
                    float time = (float)ImGui::GetTime();
                    float dot_radius = SCALE(4.0f);
                    float spacing = SCALE(14.0f);
                    ImVec2 center = ImVec2(
                        lyrics_area_pos.x + lyrics_area_size.x * 0.5f,
                        lyrics_area_pos.y + lyrics_area_size.y * 0.5f
                    );

                    for (int d = 0; d < 3; d++)
                    {
                        float phase = time * 3.0f - d * 0.5f;
                        float scale = 0.6f + 0.4f * (sinf(phase) * 0.5f + 0.5f);
                        float alpha = 0.4f + 0.6f * (sinf(phase) * 0.5f + 0.5f);
                        ImVec2 dot_pos = ImVec2(center.x + (d - 1) * spacing, center.y);
                        draw_list->AddCircleFilled(dot_pos, dot_radius * scale, IM_COL32(255, 255, 255, (int)(alpha * 255)));
                    }
                }
                else
                {
                    const char* msg = "No lyrics available";
                    ImVec2 msg_size = text_font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, msg);
                    ImVec2 msg_pos = ImVec2(
                        lyrics_area_pos.x + (lyrics_area_size.x - msg_size.x) * 0.5f,
                        lyrics_area_pos.y + (lyrics_area_size.y - msg_size.y) * 0.5f
                    );
                    draw_list->AddText(text_font, 13.0f, msg_pos, IM_COL32(180, 180, 180, 120), msg);
                }
            }
            else if (lyrics_synced && !synced_lyrics.empty())
            {
                // currentDisplayTime in Sekunden (float) - smooth interpoliert
                float currentDisplayTime = 0.0f;

                if (total_time > 0)
                {
                    if (current_time != last_update_time)
                    {
                        last_update_time      = current_time;
                        last_known_position   = current_time;
                        last_update_timestamp = (float)ImGui::GetTime();
                    }

                    if (playback_status == 4) // Playing
                    {
                        float timeSinceUpdate = (float)ImGui::GetTime() - last_update_timestamp;
                        uint64_t interpolated = last_known_position + (uint64_t)(timeSinceUpdate * 10000000.0);
                        if (interpolated > total_time) interpolated = total_time;
                        currentDisplayTime = (float)interpolated / 10000000.0f;
                    }
                    else
                    {
                        currentDisplayTime = (float)current_time / 10000000.0f;
                    }
                }

                int currentLineIndex = -1;
                for (int i = 0; i < (int)synced_lyrics.size(); i++)
                {
                    // synced_lyrics[i].time_ms ist in Millisekunden
                    float line_time_sec = (float)synced_lyrics[i].time_ms / 1000.0f;
                    if (currentDisplayTime >= line_time_sec)
                        currentLineIndex = i;
                    else
                        break;
                }
                current_lyric_line = currentLineIndex;

                float line_height = SCALE(34.0f);
                float center_y = lyrics_area_pos.y + lyrics_area_size.y * 0.5f;

                // Scroll target
                if (current_lyric_line >= 0)
                    lyrics_scroll_target = (float)current_lyric_line * line_height;
                else
                    lyrics_scroll_target = 0.0f;

                static int s_last_lyric_line = -1;
                if (current_lyric_line != s_last_lyric_line)
                {
                    lyrics_scroll_y   = lyrics_scroll_target;
                    s_last_lyric_line = current_lyric_line;
                }

                float dt = ImGui::GetIO().DeltaTime;
                if (dt > 0.0f && dt < 0.1f)
                    lyrics_scroll_y += (lyrics_scroll_target - lyrics_scroll_y) * dt * 20.0f;

                for (int i = 0; i < (int)synced_lyrics.size(); i++)
                {
                    if (synced_lyrics[i].text.empty()) continue;

                    float line_y = center_y + ((float)i * line_height) - lyrics_scroll_y - line_height * 0.5f;

                    if (line_y + line_height < lyrics_area_pos.y || line_y > lyrics_area_pos.y + lyrics_area_size.y)
                        continue;

                    float alpha;
                    float font_size;
                    int dist = (current_lyric_line >= 0) ? abs(i - current_lyric_line) : 999;

                    if (current_lyric_line < 0)
                    {
                        alpha = 0.2f; font_size = SCALE(13.0f);
                    }
                    else if (i == current_lyric_line)
                    {
                        alpha = 1.0f; font_size = SCALE(15.0f);
                    }
                    else if (i < current_lyric_line)
                    {
                        if (dist == 1)      { alpha = 0.45f; font_size = SCALE(13.5f); }
                        else if (dist == 2) { alpha = 0.3f;  font_size = SCALE(13.0f); }
                        else                { alpha = 0.15f; font_size = SCALE(13.0f); }
                    }
                    else
                    {
                        if (dist == 1)      { alpha = 0.5f;  font_size = SCALE(13.5f); }
                        else if (dist == 2) { alpha = 0.35f; font_size = SCALE(13.0f); }
                        else                { alpha = 0.2f;  font_size = SCALE(13.0f); }
                    }

                    const char* txt = synced_lyrics[i].text.c_str();
                    ImVec2 txt_size = text_font->CalcTextSizeA(font_size, FLT_MAX, lyrics_area_size.x, txt);
                    ImVec2 txt_pos  = ImVec2(
                        lyrics_area_pos.x + (lyrics_area_size.x - txt_size.x) * 0.5f,
                        line_y
                    );

                    if (i == current_lyric_line)
                    {
                        draw_list->AddText(text_font, font_size, ImVec2(txt_pos.x - 1, txt_pos.y),     IM_COL32(255,255,255,35), txt, nullptr, lyrics_area_size.x);
                        draw_list->AddText(text_font, font_size, ImVec2(txt_pos.x + 1, txt_pos.y),     IM_COL32(255,255,255,35), txt, nullptr, lyrics_area_size.x);
                        draw_list->AddText(text_font, font_size, ImVec2(txt_pos.x,     txt_pos.y - 1), IM_COL32(255,255,255,35), txt, nullptr, lyrics_area_size.x);
                        draw_list->AddText(text_font, font_size, ImVec2(txt_pos.x,     txt_pos.y + 1), IM_COL32(255,255,255,35), txt, nullptr, lyrics_area_size.x);
                    }

                    draw_list->AddText(text_font, font_size, txt_pos, IM_COL32(255,255,255,(int)(alpha*255)), txt, nullptr, lyrics_area_size.x);
                }
            }
            else if (!plain_lyrics.empty())
            {
                // Plain lyrics (non-synced) - scroll based on song progress
                float line_height = SCALE(22.0f);
                float total_height = (float)plain_lyrics.size() * line_height;
                float progress = (total_time > 0) ? ((float)current_time / (float)total_time) : 0.0f;
                float scroll_offset = progress * (total_height - lyrics_area_size.y);
                if (scroll_offset < 0.0f) scroll_offset = 0.0f;

                float current_y = lyrics_area_pos.y - scroll_offset;

                for (size_t i = 0; i < plain_lyrics.size(); i++)
                {
                    if (current_y + line_height > lyrics_area_pos.y && current_y < lyrics_area_pos.y + lyrics_area_size.y)
                    {
                        if (!plain_lyrics[i].empty())
                        {
                            const char* line_text = plain_lyrics[i].c_str();
                            ImVec2 text_size = text_font->CalcTextSizeA(13.0f, FLT_MAX, lyrics_area_size.x, line_text);
                            ImVec2 text_pos = ImVec2(
                                lyrics_area_pos.x + (lyrics_area_size.x - text_size.x) * 0.5f,
                                current_y
                            );
                            draw_list->AddText(text_font, 13.0f, text_pos, IM_COL32(220, 220, 220, 200), line_text, nullptr, lyrics_area_size.x);
                        }
                    }
                    current_y += line_height;
                }
            }

            draw_list->PopClipRect();
        }
    }
    catch (...)
    {
        draw_list->PopClipRect(); // outer scale clip
        // Catch errors
    }
}


// ========== Lyrics Fetching ==========

static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }
    }

    return escaped.str();
}

static std::string clean_song_title(const std::string& title) {
    std::string clean = title;
    std::string lower = clean;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Remove features
    const std::vector<std::string> markers = {"(feat", "(ft", "[feat", "[ft", " feat.", " ft."};
    for (const auto& marker : markers) {
        size_t pos = lower.find(marker);
        if (pos != std::string::npos) {
            clean.erase(pos);
            break;
        }
    }

    // Trim
    clean.erase(clean.begin(), std::find_if(clean.begin(), clean.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    clean.erase(std::find_if(clean.rbegin(), clean.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), clean.end());

    return clean;
}

static std::string clean_artist_name(const std::string& artist) {
    std::string clean = artist;
    std::string lower = clean;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Take only first artist
    const std::vector<std::string> delimiters = {" &", ",", " and", " x", " feat"};
    size_t earliest_pos = std::string::npos;
    for (const auto& delim : delimiters) {
        size_t pos = lower.find(delim);
        if (pos != std::string::npos && (earliest_pos == std::string::npos || pos < earliest_pos)) {
            earliest_pos = pos;
        }
    }
    if (earliest_pos != std::string::npos) {
        clean.erase(earliest_pos);
    }

    // Trim
    clean.erase(clean.begin(), std::find_if(clean.begin(), clean.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    clean.erase(std::find_if(clean.rbegin(), clean.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), clean.end());

    return clean;
}

static std::string http_get(const std::wstring& server, const std::wstring& path) {
    std::string result;

    HINTERNET hSession = WinHttpOpen(L"MusicPlayer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, server.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);

        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

            if (hRequest) {
                if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hRequest, NULL)) {

                    DWORD dwSize = 0;
                    do {
                        dwSize = 0;
                        if (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
                            std::vector<char> buffer(dwSize + 1);
                            DWORD dwDownloaded = 0;
                            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                                buffer[dwDownloaded] = 0;
                                result.append(buffer.data(), dwDownloaded);
                            }
                        }
                    } while (dwSize > 0);
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }

    return result;
}

void c_musicplayer::fetch_lyrics_async() {
    std::string track_id = artist + " - " + title;

    if (track_id == last_lyrics_track || lyrics_fetching || artist.empty() || title.empty())
        return;
    if (artist == "â€”" || title == "No Media Playing")
        return;

    last_lyrics_track = track_id;
    lyrics_fetching = true;
    lyrics_available = false;
    synced_lyrics.clear();
    plain_lyrics.clear();

    std::string artist_copy = artist;
    std::string title_copy  = title;
    uint64_t    dur_copy    = total_time;

    std::thread([this, artist_copy, title_copy, dur_copy]() {
        try {
            std::string ca = clean_artist_name(artist_copy);
            std::string ct = clean_song_title(title_copy);
            int dur = (int)(dur_copy / 10000000);

            // --- 1. LRCLIB /api/get (exact, fast) ---
            {
                std::string ua = url_encode(ca), ut = url_encode(ct);
                std::wstring path = L"/api/get?artist_name="
                    + std::wstring(ua.begin(), ua.end())
                    + L"&track_name="
                    + std::wstring(ut.begin(), ut.end())
                    + L"&duration=" + std::to_wstring(dur);

                std::string resp = http_get(L"lrclib.net", path);
                if (!resp.empty() && resp.find("syncedLyrics") != std::string::npos) {
                    parse_lyrics_response(resp);
                    if (lyrics_available && lyrics_synced) { lyrics_fetching = false; return; }
                }
            }

            // --- 2. LRCLIB /api/search ---
            {
                std::string q  = url_encode(ca + " " + ct);
                std::wstring path = L"/api/search?q=" + std::wstring(q.begin(), q.end());
                std::string resp = http_get(L"lrclib.net", path);

                if (!resp.empty() && resp.length() > 10) {
                    size_t pos = 0;
                    while (pos < resp.length()) {
                        size_t s = resp.find("{\"id\"", pos);
                        if (s == std::string::npos) s = resp.find("{\"trackName\"", pos);
                        if (s == std::string::npos) break;

                        size_t e = resp.find("},{\"", s);
                        if (e == std::string::npos) e = resp.find("}]", s);
                        if (e == std::string::npos) break;

                        std::string entry = resp.substr(s, e - s + 1);

                        // Bevorzuge synced lyrics
                        if (entry.find("\"syncedLyrics\":\"[") != std::string::npos ||
                            (entry.find("\"syncedLyrics\":\"") != std::string::npos &&
                             entry.find("\"syncedLyrics\":\"\"") == std::string::npos &&
                             entry.find("\"syncedLyrics\":null") == std::string::npos))
                        {
                            parse_lyrics_response(entry);
                            if (lyrics_available && lyrics_synced) { lyrics_fetching = false; return; }
                        }
                        pos = e + 1;
                    }

                    pos = 0;
                    while (pos < resp.length()) {
                        size_t s = resp.find("{\"id\"", pos);
                        if (s == std::string::npos) s = resp.find("{\"trackName\"", pos);
                        if (s == std::string::npos) break;
                        size_t e = resp.find("},{\"", s);
                        if (e == std::string::npos) e = resp.find("}]", s);
                        if (e == std::string::npos) break;

                        std::string entry = resp.substr(s, e - s + 1);
                        if (entry.find("\"plainLyrics\":\"") != std::string::npos &&
                            entry.find("\"plainLyrics\":\"\"") == std::string::npos &&
                            entry.find("\"plainLyrics\":null") == std::string::npos)
                        {
                            parse_lyrics_response(entry);
                            if (lyrics_available) { lyrics_fetching = false; return; }
                        }
                        pos = e + 1;
                    }
                }
            }

            // --- 3. lyrics.ovh fallback ---
            {
                std::string ua = url_encode(ca), ut = url_encode(ct);
                std::wstring path = L"/v1/"
                    + std::wstring(ua.begin(), ua.end())
                    + L"/"
                    + std::wstring(ut.begin(), ut.end());

                std::string resp = http_get(L"api.lyrics.ovh", path);
                if (!resp.empty() && resp.find("\"lyrics\"") != std::string::npos) {
                    size_t lp = resp.find("\"lyrics\":");
                    if (lp != std::string::npos) {
                        size_t sq = resp.find("\"", lp + 9);
                        if (sq != std::string::npos) {
                            size_t eq = resp.rfind("\"");
                            if (eq > sq) {
                                std::string txt = resp.substr(sq + 1, eq - sq - 1);
                                size_t p = 0;
                                while ((p = txt.find("\\n", p)) != std::string::npos) { txt.replace(p, 2, "\n"); p++; }

                                std::stringstream ss(txt);
                                std::string line;
                                plain_lyrics.clear();
                                while (std::getline(ss, line)) {
                                    line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char c){ return !std::isspace(c); }));
                                    line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), line.end());
                                    if (!line.empty()) plain_lyrics.push_back(line);
                                }
                                if (!plain_lyrics.empty()) { lyrics_available = true; lyrics_synced = false; }
                            }
                        }
                    }
                }
            }

        } catch (...) {}

        lyrics_fetching = false;
    }).detach();
}

void c_musicplayer::try_fetch_from_lrclib(const std::string&, const std::string&, int) {}
void c_musicplayer::try_fetch_plain_lyrics(const std::string&, const std::string&) {}

void c_musicplayer::parse_lyrics_response(const std::string& json_response) {
    try {
        synced_lyrics.clear();
        plain_lyrics.clear();
        lyrics_available = false;
        lyrics_synced = false;

        if (json_response.empty() || json_response == "null") {
            return;
        }

        // Simple JSON parsing for syncedLyrics
        size_t synced_pos = json_response.find("\"syncedLyrics\":");
        if (synced_pos != std::string::npos) {
            size_t start_quote = json_response.find("\"", synced_pos + 15);
            if (start_quote != std::string::npos && start_quote + 1 < json_response.length()) {
                size_t end_quote = json_response.find("\"", start_quote + 1);
                while (end_quote != std::string::npos && end_quote > 0 && json_response[end_quote - 1] == '\\') {
                    end_quote = json_response.find("\"", end_quote + 1);
                }

                if (end_quote != std::string::npos && end_quote > start_quote + 1) {
                    std::string synced_text = json_response.substr(start_quote + 1, end_quote - start_quote - 1);

                    // Replace \n with actual newlines
                    size_t pos = 0;
                    while ((pos = synced_text.find("\\n", pos)) != std::string::npos) {
                        synced_text.replace(pos, 2, "\n");
                        pos += 1;
                    }

                    std::stringstream ss(synced_text);
                    std::string line;
                    while (std::getline(ss, line)) {
                        if (line.empty() || line[0] != '[') continue;

                        size_t close_bracket = line.find(']');
                        if (close_bracket == std::string::npos || close_bracket == 0) continue;

                        std::string time_str = line.substr(1, close_bracket - 1);
                        int min = 0;
                        float sec = 0.0f;
                        if (sscanf(time_str.c_str(), "%d:%f", &min, &sec) == 2) {
                            uint64_t time_ms = (uint64_t)((min * 60.0f + sec) * 1000.0f);

                            if (close_bracket + 1 < line.length()) {
                                std::string lyric_text = line.substr(close_bracket + 1);

                                while (lyric_text.find('[') != std::string::npos) {
                                    size_t next_bracket = lyric_text.find('[');
                                    lyric_text = lyric_text.substr(0, next_bracket);
                                }

                                // Trim
                                lyric_text.erase(lyric_text.begin(), std::find_if(lyric_text.begin(), lyric_text.end(), [](unsigned char ch) {
                                    return !std::isspace(ch);
                                }));
                                lyric_text.erase(std::find_if(lyric_text.rbegin(), lyric_text.rend(), [](unsigned char ch) {
                                    return !std::isspace(ch);
                                }).base(), lyric_text.end());

                                if (!lyric_text.empty()) {
                                    synced_lyrics.push_back({time_ms, lyric_text});
                                }
                            }
                        }
                    }

                    if (!synced_lyrics.empty()) {
                        std::sort(synced_lyrics.begin(), synced_lyrics.end(), [](const LyricLine& a, const LyricLine& b) {
                            return a.time_ms < b.time_ms;
                        });

                        lyrics_available = true;
                        lyrics_synced = true;

                        char debug_msg[256];
                        sprintf_s(debug_msg, "Parsed %d synced lyrics lines\n", (int)synced_lyrics.size());
                        OutputDebugStringA(debug_msg);
                        return;
                    }
                }
            }
        }

        // Fallback to plainLyrics
        size_t plain_pos = json_response.find("\"plainLyrics\":");
        if (plain_pos != std::string::npos) {
            size_t start_quote = json_response.find("\"", plain_pos + 14);
            if (start_quote != std::string::npos && start_quote + 1 < json_response.length()) {
                size_t end_quote = json_response.find("\"", start_quote + 1);
                while (end_quote != std::string::npos && end_quote > 0 && json_response[end_quote - 1] == '\\') {
                    end_quote = json_response.find("\"", end_quote + 1);
                }

                if (end_quote != std::string::npos && end_quote > start_quote + 1) {
                    std::string plain_text = json_response.substr(start_quote + 1, end_quote - start_quote - 1);

                    // Replace \n with actual newlines
                    size_t pos = 0;
                    while ((pos = plain_text.find("\\n", pos)) != std::string::npos) {
                        plain_text.replace(pos, 2, "\n");
                        pos += 1;
                    }

                    std::stringstream ss(plain_text);
                    std::string line;
                    while (std::getline(ss, line)) {
                        // Trim
                        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
                            return !std::isspace(ch);
                        }));
                        line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char ch) {
                            return !std::isspace(ch);
                        }).base(), line.end());

                        if (!line.empty()) {
                            plain_lyrics.push_back(line);
                        }
                    }

                    if (!plain_lyrics.empty()) {
                        lyrics_available = true;
                        lyrics_synced = false;
                    }
                }
            }
        }
    } catch (...) {
        lyrics_available = false;
        OutputDebugStringA("ERROR: Exception in parse_lyrics_response\n");
    }
}


