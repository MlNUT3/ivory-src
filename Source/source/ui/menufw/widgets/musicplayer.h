#pragma once

#include "../headers/includes.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <pplawait.h>
#include <d3d11.h>

#pragma comment(lib, "WindowsApp.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Forward declare
bool LoadTextureFromMemory(const unsigned char* image_data, int image_size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height);

// Ambient background renderer with shader
class c_ambient_background
{
public:
    void init(ID3D11Device* device);
    void render(ID3D11DeviceContext* context, ImVec2 pos, ImVec2 size, float dominant_colors[3][3]);
    void cleanup();

    ID3D11ShaderResourceView* shader_resource = nullptr;

    static constexpr UINT TEX_WIDTH = 512;
    static constexpr UINT TEX_HEIGHT = 512;

private:
    ID3D11Texture2D* render_texture = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11RasterizerState* rasterizer_state = nullptr;
    ID3D11BlendState* blend_state = nullptr;

    bool initialized = false;
};

class c_musicplayer
{
public:
    void setup();
    void update(); // Call this every frame
    void render();
    void shutdown();

    // Media controls
    void toggle_play_pause();
    void skip_next();
    void skip_previous();
    void set_position(float progress); // 0.0 to 1.0
    void toggle_shuffle(); // Toggle shuffle mode

private:
    // WinRT Media Session
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager session_manager{ nullptr };
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session{ nullptr };

    // Media info
    bool has_media = false;
    std::string title = "No Media Playing";
    std::string artist = "—";
    int playback_status = 0; // 0=closed, 4=playing, 5=paused
    bool shuffle_active = false; // Shuffle state

    uint64_t total_time = 0;
    uint64_t current_time = 0;

    // Lyrics data
    struct LyricLine {
        uint64_t time_ms;
        std::string text;
    };
    std::vector<LyricLine> synced_lyrics;
    std::vector<std::string> plain_lyrics;
    bool lyrics_available = false;
    bool lyrics_synced = false;
    bool lyrics_fetching = false;
    std::string last_lyrics_track;
    int current_lyric_line = -1;
    float lyrics_scroll_y = 0.0f;  // Smooth scroll position
    float lyrics_scroll_target = 0.0f;

    // Smooth progress interpolation
    uint64_t last_update_time = 0;
    uint64_t last_known_position = 0;
    float last_update_timestamp = 0.0f;

    // Thumbnail
    void* thumbnail_buffer = nullptr;
    uint32_t thumbnail_size = 0;
    bool thumbnail_updated = false;
    uint32_t last_thumb_size = 0;

    ID3D11ShaderResourceView* thumbnail_texture = nullptr;
    int thumbnail_width = 0;
    int thumbnail_height = 0;

    // Dominant colors from album cover (RGB 0-1)
    float dominant_colors[3][3] = {
        {0.561f, 0.784f, 1.0f},  // Baby blue default
        {0.15f, 0.40f, 0.85f},  // Blue default
        {0.80f, 0.20f, 0.45f}   // Pink default
    };

    // Ambient background renderer
    c_ambient_background ambient_bg;

    // UI state
    ImVec2 player_pos = ImVec2(100.0f, 500.0f);
    bool placed = false;
    bool is_dragging = false;
    ImVec2 drag_offset = ImVec2(0.0f, 0.0f);

    // Animation states
    float hover_alpha = 0.0f;
    float progress_hover = 0.0f;
    float shuffle_hover = 0.0f;
    float prev_hover = 0.0f;
    float play_hover = 0.0f;
    float next_hover = 0.0f;
    float lyrics_hover = 0.0f;
    float lyrics_expand_anim = 0.0f;

    // Lyrics state
    bool lyrics_expanded = false;
    float lyrics_height = 0.0f;

    bool initialized = false;
    std::string last_deezer_cover;

    // Helper
    std::string format_time(uint64_t time_100ns);
    float get_smooth_progress();
    void update_media_info();
    void load_deezer_cover();
    void update_thumbnail_texture();
    void extract_dominant_colors();
    void fetch_lyrics_async();
    void parse_lyrics_response(const std::string& json_response);
    void try_fetch_from_lrclib(const std::string& artist, const std::string& title, int duration_secs);
    void try_fetch_plain_lyrics(const std::string& artist, const std::string& title);
};

inline c_musicplayer* musicplayer = new c_musicplayer();
