#pragma once
#include <string>
#include <vector>
#include <windows.h>

struct Program {
    const char* name;
    const char* description;
    const char* version;
    int                  status;
    const unsigned char* bin_data;
    size_t               bin_size;
    const char* source_dir;
};

struct TrackedProcess {
    HANDLE      hProcess;
    char        temp_exe[MAX_PATH];
};

bool launch_program_from_memory(const unsigned char* data, size_t size, const char* source_dir = "", DWORD* out_error = nullptr);
void cleanup_temp_files();

extern std::vector<Program>          g_programs;
extern std::vector<TrackedProcess>   g_tracked;
extern int                           g_launch_result;
extern bool                          g_launching;
extern DWORD                         g_last_error;

void cleanup_temp_files();
void programs_init();
bool nv_load_cheat(DWORD* out_error = nullptr);
bool nv_unload_cheat(DWORD* out_error = nullptr);
bool nv_cheat_loaded();