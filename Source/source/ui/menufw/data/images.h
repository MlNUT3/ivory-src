#pragma once
#include <d3d11.h>

// Image loading functions
bool LoadTextureFromMemory(const unsigned char* image_data, int image_size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height);
void LoadLogoTexture();
ID3D11ShaderResourceView* GetLogoTexture(int* out_width, int* out_height);
void CleanupLogoTexture();

void LoadPatternTexture();
ID3D11ShaderResourceView* GetPatternTexture(int* out_width, int* out_height);
void CleanupPatternTexture();

ID3D11ShaderResourceView* GetGrainTexture(int* out_width, int* out_height);
