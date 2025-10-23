#pragma once

#include "D3D11Renderer.h"
#include <iostream>
#include <unordered_map>

// Video Processor-based renderer
class D3D11VideoProcessorRenderer : public ID3D11RendererBase
{
private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTargetView;

    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoProcessor> videoProcessor;
    ComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnum;
    ComPtr<ID3D11VideoProcessorOutputView> outputView;

    // Input view cache for performance
    std::unordered_map<int, ComPtr<ID3D11VideoProcessorInputView>> inputViewCache;

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

        // Initialize video processor
        if (!InitializeVideoProcessor())
            return false;

        std::cout << "Initialized Hardware Video Processor for YUV to RGB conversion" << std::endl;
        return true;
    }

    void RenderFrame(ID3D11Texture2D *nv12Texture, int textureIndex) override
    {
        if (!nv12Texture)
            return;

        // Get or create cached input view
        auto it = inputViewCache.find(textureIndex);
        if (it == inputViewCache.end())
        {
            // Create new input view and cache it
            ComPtr<ID3D11VideoProcessorInputView> inputView;
            if (!CreateInputView(nv12Texture, textureIndex, inputView))
                return;
            inputViewCache[textureIndex] = inputView;
        }

        // Process to back buffer (don't present yet, ImGui will render on top)
        ProcessVideoFrame(inputViewCache[textureIndex].Get());
    }

    void Present() override
    {
        swapChain->Present(1, 0);
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

    bool InitializeVideoProcessor()
    {
        // Get video device and context
        HRESULT hr = device.As(&videoDevice);
        if (FAILED(hr))
        {
            std::cerr << "Failed to get video device" << std::endl;
            return false;
        }

        hr = context.As(&videoContext);
        if (FAILED(hr))
        {
            std::cerr << "Failed to get video context" << std::endl;
            return false;
        }

        // Create video processor enumerator
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
        contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDesc.InputWidth = width;
        contentDesc.InputHeight = height;
        contentDesc.OutputWidth = width;
        contentDesc.OutputHeight = height;
        contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        hr = videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &videoProcessorEnum);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create video processor enumerator" << std::endl;
            return false;
        }

        // Create video processor
        hr = videoDevice->CreateVideoProcessor(videoProcessorEnum.Get(), 0, &videoProcessor);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create video processor" << std::endl;
            return false;
        }

        // Create output view
        ComPtr<ID3D11Texture2D> backBuffer;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
        outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outputDesc.Texture2D.MipSlice = 0;

        hr = videoDevice->CreateVideoProcessorOutputView(
            backBuffer.Get(), videoProcessorEnum.Get(), &outputDesc, &outputView);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create video processor output view" << std::endl;
            return false;
        }

        return true;
    }

    bool CreateInputView(ID3D11Texture2D *nv12Texture, int textureIndex,
                         ComPtr<ID3D11VideoProcessorInputView> &inputView)
    {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc = {};
        inputDesc.FourCC = 0;
        inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputDesc.Texture2D.MipSlice = 0;
        inputDesc.Texture2D.ArraySlice = textureIndex;

        HRESULT hr = videoDevice->CreateVideoProcessorInputView(
            nv12Texture, videoProcessorEnum.Get(), &inputDesc, &inputView);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create video processor input view" << std::endl;
            return false;
        }

        return true;
    }

    void ProcessVideoFrame(ID3D11VideoProcessorInputView *inputView)
    {
        // Setup stream
        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.OutputIndex = 0;
        stream.InputFrameOrField = 0;
        stream.PastFrames = 0;
        stream.FutureFrames = 0;
        stream.pInputSurface = inputView;

        // Process: NV12 -> RGB (hardware accelerated)
        HRESULT hr = videoContext->VideoProcessorBlt(
            videoProcessor.Get(), outputView.Get(), 0, 1, &stream);

        if (FAILED(hr))
        {
            std::cerr << "VideoProcessorBlt failed: 0x" << std::hex << hr << std::endl;
            return;
        }

        // CRITICAL: Re-bind render target view for ImGui to draw on the same back buffer
        // VideoProcessorBlt doesn't set render targets, so we must do it manually
        context->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), nullptr);
    }
};
