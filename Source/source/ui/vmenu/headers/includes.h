#pragma once

// Define this to use local ImGui math operators
#define IMGUI_DEFINE_MATH_OPERATORS

// Include local ImGui FIRST - direct path from framework/headers to thirdparty
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

// Now include other headers
#include <vector>
#include <unordered_map>
#include <sstream>
#include <string>
#include <algorithm>
#include "flags.h"
#include "../settings/colors.h"
#include "../settings/felements.h"
#include "../settings/variables.h"
#include "fonts.h"
#include "draw.h"
#include "functions.h"
#include "../data/fonts.h"
#include "../data/images.h"

// helpers that exist in the framework's own imgui fork but not in stock 1.92
static inline float ImLength(const ImVec2& lhs, float fail_value = 0.0f)
{
	float d = (lhs.x * lhs.x) + (lhs.y * lhs.y);
	if (d > 0.0f) return ImSqrt(d);
	return fail_value;
}