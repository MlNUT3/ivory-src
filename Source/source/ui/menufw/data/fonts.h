#pragma once
#include <vector>
#include "font_data.h"
#include "IconsFontAwesome6_Bytes.h"
#include "IconsFontAwesome6Brands_Bytes.h"

// Font data vectors
inline std::vector<unsigned char> main_font_data(rawData, rawData + sizeof(rawData));
inline std::vector<unsigned char> icon_font_data((unsigned char*)fa6_solid_compressed_data, (unsigned char*)fa6_solid_compressed_data + fa6_solid_compressed_size);
inline std::vector<unsigned char> icon_brands_font_data((unsigned char*)fa_brands_400_compressed_data, (unsigned char*)fa_brands_400_compressed_data + fa_brands_400_compressed_size);
