#pragma once

#include "../headers/includes.h"
#include <mutex>

enum e_provider
{
    prov_deezer = 0,
    prov_audius = 1,
    prov_soundcloud = 2
};

struct dz_track
{
    long long id = 0;
    std::string title;
    std::string artist;
    std::string album;
    std::string cover;
    std::string preview;
    int duration = 0;
    bool needs_resolve = false;

    int provider = 0;
    std::string src_id;
    std::string resolve_url;
    std::vector<std::string> mirrors;
    std::vector<std::string> cover_mirrors;
};

struct dz_playlist
{
    long long id = 0;
    std::string title;
    std::string cover;
    int track_count = 0;

    int provider = 0;
    std::string src_id;
};

struct dz_user_playlist
{
    std::string name;
    std::vector<dz_track> tracks;
};

class c_deezer
{
public:
    std::vector<dz_track> results;
    std::vector<dz_track> queue;
    std::vector<dz_playlist> playlists;

    int provider = prov_soundcloud;

    std::string query;
    std::string list_title;
    bool loading = false;
    bool has_results = false;
    bool has_playlists = false;
    int selected_playlist = -1;
    long long selected_id = -1;
    int queue_index = -1;

    std::vector<dz_user_playlist> user_playlists;
    int selected_user_playlist = -1;

    void ensure_user_playlists();
    int create_user_playlist(const std::string& name);
    void delete_user_playlist(int index);
    bool add_to_user_playlist(int index, const dz_track& t);
    void remove_from_user_playlist(int index, int track_index);
    void open_user_playlist(int index);
    void save_user_playlists();

    bool active = false;
    bool downloading = false;
    std::string status;

    std::string now_title;
    std::string now_artist;
    std::string now_album;

    void search(const std::string& q);
    void load_charts();
    void load_playlists();
    void load_playlist(const dz_playlist& p);

    void play(int index);
    void play_queue_at(int index);
    void play_track(const dz_track& t, const std::vector<dz_track>& source, int index);
    void toggle_play_pause();
    void next();
    void previous();
    void seek(float progress);
    void stop();
    void update();

    bool is_playing() const { return playing; }
    float progress() const;
    int position_ms() const { return pos_ms; }
    int length_ms() const { return len_ms; }

    const char* cover_file() const { return cover_path; }

private:
    void pump();
    void start_playback(std::vector<unsigned char> bytes);
    void advance();
    std::string user_pl_path();

    std::mutex mtx;
    std::vector<dz_track> pending_tracks;
    std::vector<dz_track> pending_queue;
    std::vector<dz_playlist> pending_playlists;
    std::vector<unsigned char> pending_audio;
    std::string pending_title;
    int pending_index = -1;
    bool tracks_pending = false;
    bool playlists_pending = false;
    bool title_pending = false;
    bool ready_to_play = false;

    bool playing = false;
    bool loaded = false;
    bool user_paused = false;
    bool ended = false;
    bool user_pl_loaded = false;
    int pos_ms = 0;
    int len_ms = 0;
    int fail_streak = 0;
    uint64_t last_poll = 0;

    char cover_path[520] = {};
};

inline c_deezer* deezer = new c_deezer();

void render_deezer_tab(float width, float height);
