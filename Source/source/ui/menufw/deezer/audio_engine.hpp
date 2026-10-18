#pragma once

#include <cstddef>
#include <vector>

namespace audio {

bool init();
void shutdown();

const char* play(const std::vector<unsigned char>& bytes);
void pause();
void resume();
void stop();

bool playing();
bool paused();
double position_sec();
double duration_sec();
void seek_sec(double sec);

void set_volume(float v);

} // namespace audio
