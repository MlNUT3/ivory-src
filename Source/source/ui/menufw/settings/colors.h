#pragma once
#include "../headers/includes.h"
#include "../headers/flags.h"
#include <memory>

class c_colors
{
public:

};

inline std::unique_ptr<c_colors> clr = std::make_unique<c_colors>();
