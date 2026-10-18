#include "../headers/includes.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include <d3d11.h>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// Dummy data for missing textures
static const unsigned char Logo[] = { 0 };
static const unsigned char pattern[] = { 0 };

// Global texture storage
static ID3D11ShaderResourceView* g_LogoTexture = nullptr;
static int g_LogoWidth = 0;
static int g_LogoHeight = 0;

static ID3D11ShaderResourceView* g_PatternTexture = nullptr;
static int g_PatternWidth = 0;
static int g_PatternHeight = 0;

// Get device from ImGui backend
struct ImGui_ImplDX11_Data
{
    ID3D11Device*            pd3dDevice;
    ID3D11DeviceContext*     pd3dDeviceContext;
    IDXGIFactory*            pFactory;
    ID3D11Buffer*            pVB;
    ID3D11Buffer*            pIB;
    ID3D11VertexShader*      pVertexShader;
    ID3D11InputLayout*       pInputLayout;
    ID3D11Buffer*            pVertexConstantBuffer;
    ID3D11PixelShader*       pPixelShader;
    ID3D11SamplerState*      pFontSampler;
    ID3D11ShaderResourceView* pFontTextureView;
    ID3D11RasterizerState*   pRasterizerState;
    ID3D11BlendState*        pBlendState;
    ID3D11DepthStencilState* pDepthStencilState;
    int                      VertexBufferSize;
    int                      IndexBufferSize;
};

static ImGui_ImplDX11_Data* GetDX11Data()
{
    return (ImGui_ImplDX11_Data*)ImGui::GetIO().BackendRendererUserData;
}

bool LoadTextureFromMemory(const unsigned char* image_data, int image_size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
    // Get D3D11 device from ImGui backend
    ImGui_ImplDX11_Data* bd = GetDX11Data();
    if (!bd || !bd->pd3dDevice)
        return false;

    // Load image using stb_image
    int width, height, channels;
    unsigned char* image_pixels = stbi_load_from_memory(image_data, image_size, &width, &height, &channels, 4);
    if (image_pixels == nullptr)
        return false;

    // Create texture
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = nullptr;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_pixels;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;

    HRESULT hr = bd->pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

    if (FAILED(hr) || !pTexture)
    {
        stbi_image_free(image_pixels);
        return false;
    }

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = bd->pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();

    *out_width = width;
    *out_height = height;
    stbi_image_free(image_pixels);

    return SUCCEEDED(hr) && (*out_srv != nullptr);
}

void LoadLogoTexture()
{
    if (g_LogoTexture == nullptr)
    {
        LoadTextureFromMemory(Logo, sizeof(Logo), &g_LogoTexture, &g_LogoWidth, &g_LogoHeight);
    }
}

ID3D11ShaderResourceView* GetLogoTexture(int* out_width, int* out_height)
{
    if (g_LogoTexture == nullptr)
    {
        try {
            LoadLogoTexture();
        }
        catch (...) {
            // Failed to load, return nullptr
            if (out_width) *out_width = 0;
            if (out_height) *out_height = 0;
            return nullptr;
        }
    }

    if (out_width) *out_width = g_LogoWidth;
    if (out_height) *out_height = g_LogoHeight;

    return g_LogoTexture;
}

void CleanupLogoTexture()
{
    if (g_LogoTexture)
    {
        g_LogoTexture->Release();
        g_LogoTexture = nullptr;
    }
}

void LoadPatternTexture()
{
    if (g_PatternTexture == nullptr)
    {
        LoadTextureFromMemory(pattern, sizeof(pattern), &g_PatternTexture, &g_PatternWidth, &g_PatternHeight);
    }
}

ID3D11ShaderResourceView* GetPatternTexture(int* out_width, int* out_height)
{
    if (g_PatternTexture == nullptr)
    {
        try {
            LoadPatternTexture();
        }
        catch (...) {
            // Failed to load, return nullptr
            if (out_width) *out_width = 0;
            if (out_height) *out_height = 0;
            return nullptr;
        }
    }

    if (out_width) *out_width = g_PatternWidth;
    if (out_height) *out_height = g_PatternHeight;

    return g_PatternTexture;
}

void CleanupPatternTexture()
{
    if (g_PatternTexture)
    {
        g_PatternTexture->Release();
        g_PatternTexture = nullptr;
    }
}

static ID3D11ShaderResourceView* g_GrainTexture = nullptr;
static int g_GrainWidth = 0;
static int g_GrainHeight = 0;

ID3D11ShaderResourceView* GetGrainTexture(int* out_width, int* out_height)
{
    if (g_GrainTexture == nullptr)
    {
        ImGui_ImplDX11_Data* bd = GetDX11Data();
        if (!bd || !bd->pd3dDevice)
        {
            if (out_width) *out_width = 0;
            if (out_height) *out_height = 0;
            return nullptr;
        }

        const int N = 512;
        std::vector<unsigned char> px((size_t)N * N * 4);
        unsigned int s = 0x9e3779b9u;
        for (int i = 0; i < N * N; i++)
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            px[i * 4 + 0] = 255;
            px[i * 4 + 1] = 255;
            px[i * 4 + 2] = 255;
            px[i * 4 + 3] = (unsigned char)((s >> 16) & 15);
        }

        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = N;
        desc.Height = N;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub;
        sub.pSysMem = px.data();
        sub.SysMemPitch = N * 4;
        sub.SysMemSlicePitch = 0;

        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(bd->pd3dDevice->CreateTexture2D(&desc, &sub, &tex)) && tex)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv;
            ZeroMemory(&srv, sizeof(srv));
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
            srv.Texture2D.MostDetailedMip = 0;
            bd->pd3dDevice->CreateShaderResourceView(tex, &srv, &g_GrainTexture);
            tex->Release();
        }

        g_GrainWidth = N;
        g_GrainHeight = N;
    }

    if (out_width) *out_width = g_GrainTexture ? g_GrainWidth : 0;
    if (out_height) *out_height = g_GrainTexture ? g_GrainHeight : 0;
    return g_GrainTexture;
}
