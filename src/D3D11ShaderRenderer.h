#pragma once

#include "D3D11Renderer.h"
#include <d3dcompiler.h>
#include <iostream>

// Vertex Shader (NV12 to RGB conversion)
const char *vertexShaderSrc = R"(
struct VS_INPUT {
    float2 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.tex = input.tex;
    return output;
}
)";

// Pixel Shader (NV12 to RGB conversion)
const char *pixelShaderSrc = R"(
Texture2D<float> texY : register(t0);
Texture2D<float2> texUV : register(t1);
SamplerState samplerState : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target {
    float y = texY.Sample(samplerState, input.tex);
    float2 uv = texUV.Sample(samplerState, input.tex);
    
    float u = uv.x - 0.5f;
    float v = uv.y - 0.5f;
    y = 1.164f * (y - 0.0625f);
    
    float r = y + 1.596f * v;
    float g = y - 0.391f * u - 0.813f * v;
    float b = y + 2.018f * u;
    
    return float4(r, g, b, 1.0f);
}
)";

struct Vertex
{
    float pos[2];
    float tex[2];
};

// Shader-based renderer
class D3D11ShaderRenderer : public ID3D11RendererBase
{
private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTargetView;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11SamplerState> samplerState;
    ComPtr<ID3D11Texture2D> stagingTexture;

    int width = 0;
    int height = 0;

public:
    bool Initialize(HWND hwnd, int videoWidth, int videoHeight) override
    {
        width = videoWidth;
        height = videoHeight;

        // Create D3D11 Device
        if (!CreateDevice())
            return false;

        // Create Swap Chain
        if (!CreateSwapChain(hwnd))
            return false;

        // Initialize shader pipeline
        if (!InitializeShaderPipeline())
            return false;

        std::cout << "Initialized Shader-based YUV to RGB converter" << std::endl;
        return true;
    }

    void RenderFrame(ID3D11Texture2D *nv12Texture, int textureIndex) override
    {
        if (!nv12Texture)
            return;

        // Copy to staging texture
        if (!PrepareTexture(nv12Texture, textureIndex))
            return;

        // Create shader resource views
        ComPtr<ID3D11ShaderResourceView> srvY, srvUV;
        if (!CreateShaderResourceViews(srvY, srvUV))
            return;

        // Render
        RenderToScreen(srvY.Get(), srvUV.Get());
    }

    ID3D11Device *GetDevice() override { return device.Get(); }
    ID3D11DeviceContext *GetContext() override { return context.Get(); }

private:
    bool CreateDevice()
    {
        D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL featureLevel;
        UINT createFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            createFlags, featureLevels, ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION, &device, &featureLevel, &context);

        if (FAILED(hr))
        {
            std::cerr << "Failed to create D3D11 device" << std::endl;
            return false;
        }
        return true;
    }

    bool CreateSwapChain(HWND hwnd)
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        device.As(&dxgiDevice);
        ComPtr<IDXGIAdapter> dxgiAdapter;
        dxgiDevice->GetAdapter(&dxgiAdapter);
        ComPtr<IDXGIFactory2> dxgiFactory;
        dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), &dxgiFactory);

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = width;
        swapChainDesc.Height = height;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
            device.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain);

        if (FAILED(hr))
        {
            std::cerr << "Failed to create swap chain" << std::endl;
            return false;
        }

        // Create render target view
        ComPtr<ID3D11Texture2D> backBuffer;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView);

        return true;
    }

    bool InitializeShaderPipeline()
    {
        // Compile shaders
        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

        HRESULT hr = D3DCompile(vertexShaderSrc, strlen(vertexShaderSrc), nullptr,
                                nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
                std::cerr << "VS Error: " << (char *)errorBlob->GetBufferPointer() << std::endl;
            return false;
        }

        hr = D3DCompile(pixelShaderSrc, strlen(pixelShaderSrc), nullptr,
                        nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
                std::cerr << "PS Error: " << (char *)errorBlob->GetBufferPointer() << std::endl;
            return false;
        }

        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);

        // Create input layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(),
                                  vsBlob->GetBufferSize(), &inputLayout);

        // Create vertex buffer
        Vertex vertices[] = {
            {{-1.0f, 1.0f}, {0.0f, 0.0f}},
            {{1.0f, 1.0f}, {1.0f, 0.0f}},
            {{-1.0f, -1.0f}, {0.0f, 1.0f}},
            {{1.0f, -1.0f}, {1.0f, 1.0f}},
        };

        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(vertices);
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initData = {vertices};
        device->CreateBuffer(&bufferDesc, &initData, &vertexBuffer);

        // Create sampler state
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&samplerDesc, &samplerState);

        return true;
    }

    bool PrepareTexture(ID3D11Texture2D *nv12Texture, int textureIndex)
    {
        D3D11_TEXTURE2D_DESC srcDesc;
        nv12Texture->GetDesc(&srcDesc);

        // Create staging texture if needed
        if (!stagingTexture)
        {
            D3D11_TEXTURE2D_DESC texDesc = {};
            texDesc.Width = srcDesc.Width;
            texDesc.Height = srcDesc.Height;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format = srcDesc.Format;
            texDesc.SampleDesc.Count = 1;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            texDesc.CPUAccessFlags = 0;

            HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &stagingTexture);
            if (FAILED(hr))
            {
                std::cerr << "Failed to create staging texture" << std::endl;
                return false;
            }
        }

        // Copy from decoder output
        UINT srcSubresource = D3D11CalcSubresource(0, textureIndex, srcDesc.MipLevels);
        UINT dstSubresource = D3D11CalcSubresource(0, 0, 1);
        context->CopySubresourceRegion(stagingTexture.Get(), dstSubresource, 0, 0, 0,
                                       nv12Texture, srcSubresource, nullptr);
        return true;
    }

    bool CreateShaderResourceViews(ComPtr<ID3D11ShaderResourceView> &srvY,
                                    ComPtr<ID3D11ShaderResourceView> &srvUV)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        HRESULT hr = device->CreateShaderResourceView(stagingTexture.Get(), &srvDesc, &srvY);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create Y SRV" << std::endl;
            return false;
        }

        srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        hr = device->CreateShaderResourceView(stagingTexture.Get(), &srvDesc, &srvUV);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create UV SRV" << std::endl;
            return false;
        }

        return true;
    }

    void RenderToScreen(ID3D11ShaderResourceView *srvY, ID3D11ShaderResourceView *srvUV)
    {
        // Set render target
        context->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), nullptr);

        // Clear
        float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
        context->ClearRenderTargetView(renderTargetView.Get(), clearColor);

        // Set viewport
        D3D11_VIEWPORT viewport = {};
        viewport.Width = (float)width;
        viewport.Height = (float)height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        // Set pipeline state
        context->VSSetShader(vertexShader.Get(), nullptr, 0);
        context->PSSetShader(pixelShader.Get(), nullptr, 0);
        context->IASetInputLayout(inputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);

        ID3D11ShaderResourceView *srvs[] = {srvY, srvUV};
        context->PSSetShaderResources(0, 2, srvs);
        context->PSSetSamplers(0, 1, samplerState.GetAddressOf());

        // Draw
        context->Draw(4, 0);

        // Present
        swapChain->Present(1, 0);

        // Cleanup
        ID3D11ShaderResourceView *nullSRVs[] = {nullptr, nullptr};
        context->PSSetShaderResources(0, 2, nullSRVs);
    }
};
