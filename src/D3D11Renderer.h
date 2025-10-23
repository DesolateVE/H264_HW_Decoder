#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Base renderer interface
class ID3D11RendererBase
{
public:
    virtual ~ID3D11RendererBase() = default;
    virtual bool Initialize(HWND hwnd, int videoWidth, int videoHeight) = 0;
    virtual void RenderFrame(ID3D11Texture2D *nv12Texture, int textureIndex) = 0;
    virtual void Present() = 0;  // Separated Present call for ImGui overlay
    virtual ID3D11Device *GetDevice() = 0;
    virtual ID3D11DeviceContext *GetContext() = 0;
};

// Factory for creating renderers
class D3D11RendererFactory
{
public:
    enum class Mode
    {
        Shader,
        VideoProcessor
    };

    static ID3D11RendererBase *Create(Mode mode);
};
