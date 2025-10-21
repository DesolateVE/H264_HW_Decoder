#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/imgutils.h>
}

using Microsoft::WRL::ComPtr;

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

class D3D11Renderer
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
    bool Initialize(HWND hwnd, int videoWidth, int videoHeight)
    {
        width = videoWidth;
        height = videoHeight;

        // Create D3D11 Device and Context
        D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL featureLevel;
        UINT createFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            &context);

        if (FAILED(hr))
        {
            std::cerr << "Failed to create D3D11 device" << std::endl;
            return false;
        }

        // Create Swap Chain
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

        hr = dxgiFactory->CreateSwapChainForHwnd(
            device.Get(),
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain);

        if (FAILED(hr))
        {
            std::cerr << "Failed to create swap chain" << std::endl;
            return false;
        }

        // Create Render Target View
        ComPtr<ID3D11Texture2D> backBuffer;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView);

        // Compile and create shaders
        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

        hr = D3DCompile(vertexShaderSrc, strlen(vertexShaderSrc), nullptr, nullptr, nullptr,
                        "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                std::cerr << "VS Error: " << (char *)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }

        hr = D3DCompile(pixelShaderSrc, strlen(pixelShaderSrc), nullptr, nullptr, nullptr,
                        "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                std::cerr << "PS Error: " << (char *)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }

        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);

        // Create Input Layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(),
                                  vsBlob->GetBufferSize(), &inputLayout);

        // Create Vertex Buffer (full screen quad)
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

        // Create Sampler State
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

    void RenderFrame(ID3D11Texture2D *nv12Texture, int textureIndex = 0)
    {
        if (!nv12Texture)
            return;

        // Get source texture description
        D3D11_TEXTURE2D_DESC srcDesc;
        nv12Texture->GetDesc(&srcDesc);

        // Create or reuse a staging texture with shader resource binding
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
                return;
            }
        }

        // Copy specific array slice from decoder output to shader-resource-capable texture
        UINT srcSubresource = D3D11CalcSubresource(0, textureIndex, srcDesc.MipLevels);
        UINT dstSubresource = D3D11CalcSubresource(0, 0, 1);
        context->CopySubresourceRegion(
            stagingTexture.Get(), dstSubresource, 0, 0, 0,
            nv12Texture, srcSubresource, nullptr);

        // Create Shader Resource Views for Y and UV planes
        ComPtr<ID3D11ShaderResourceView> srvY, srvUV;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        HRESULT hr = device->CreateShaderResourceView(stagingTexture.Get(), &srvDesc, &srvY);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create Y SRV" << std::endl;
            return;
        }

        srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        hr = device->CreateShaderResourceView(stagingTexture.Get(), &srvDesc, &srvUV);
        if (FAILED(hr))
        {
            std::cerr << "Failed to create UV SRV" << std::endl;
            return;
        }

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

        // Set shaders and resources
        context->VSSetShader(vertexShader.Get(), nullptr, 0);
        context->PSSetShader(pixelShader.Get(), nullptr, 0);
        context->IASetInputLayout(inputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);

        ID3D11ShaderResourceView *srvs[] = {srvY.Get(), srvUV.Get()};
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

    ID3D11Device *GetDevice() { return device.Get(); }
    ID3D11DeviceContext *GetContext() { return context.Get(); }
};

class FFmpegD3D11Decoder
{
private:
    AVFormatContext *formatCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    AVBufferRef *hwDeviceCtx = nullptr;
    int videoStreamIndex = -1;
    D3D11Renderer *renderer = nullptr;

public:
    bool Initialize(const char *filename, D3D11Renderer *render)
    {
        renderer = render;

        // Open input file
        if (avformat_open_input(&formatCtx, filename, nullptr, nullptr) < 0)
        {
            std::cerr << "Could not open input file" << std::endl;
            return false;
        }

        if (avformat_find_stream_info(formatCtx, nullptr) < 0)
        {
            std::cerr << "Could not find stream info" << std::endl;
            return false;
        }

        // Find video stream
        for (unsigned i = 0; i < formatCtx->nb_streams; i++)
        {
            if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                videoStreamIndex = i;
                break;
            }
        }

        if (videoStreamIndex == -1)
        {
            std::cerr << "Could not find video stream" << std::endl;
            return false;
        }

        // Find decoder
        const AVCodec *codec = avcodec_find_decoder(formatCtx->streams[videoStreamIndex]->codecpar->codec_id);
        if (!codec)
        {
            std::cerr << "Codec not found" << std::endl;
            return false;
        }

        codecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codecCtx, formatCtx->streams[videoStreamIndex]->codecpar);

        // Create D3D11VA hardware device context
        AVBufferRef *deviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        AVHWDeviceContext *deviceCtx = (AVHWDeviceContext *)deviceRef->data;
        AVD3D11VADeviceContext *d3d11DeviceCtx = (AVD3D11VADeviceContext *)deviceCtx->hwctx;

        // Use the same D3D11 device as renderer for zero-copy
        d3d11DeviceCtx->device = renderer->GetDevice();
        d3d11DeviceCtx->device->AddRef();
        d3d11DeviceCtx->device_context = renderer->GetContext();
        d3d11DeviceCtx->device_context->AddRef();

        if (av_hwdevice_ctx_init(deviceRef) < 0)
        {
            std::cerr << "Failed to create D3D11VA device" << std::endl;
            av_buffer_unref(&deviceRef);
            return false;
        }

        hwDeviceCtx = deviceRef;
        codecCtx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

        // Open codec
        if (avcodec_open2(codecCtx, codec, nullptr) < 0)
        {
            std::cerr << "Could not open codec" << std::endl;
            return false;
        }

        std::cout << "Decoder initialized successfully with D3D11VA hardware acceleration" << std::endl;
        return true;
    }

    bool DecodeAndRender()
    {
        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        bool ret = true;

        while (av_read_frame(formatCtx, packet) >= 0)
        {
            if (packet->stream_index == videoStreamIndex)
            {
                if (avcodec_send_packet(codecCtx, packet) == 0)
                {
                    while (avcodec_receive_frame(codecCtx, frame) == 0)
                    {
                        // Frame is in GPU memory (D3D11 texture)
                        if (frame->format == AV_PIX_FMT_D3D11)
                        {
                            // Extract D3D11 texture directly - zero copy!
                            ID3D11Texture2D *texture = (ID3D11Texture2D *)frame->data[0];
                            int textureIndex = (int)(intptr_t)frame->data[1];

                            // Render directly from GPU texture with array index
                            renderer->RenderFrame(texture, textureIndex);
                        }
                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(packet);

            // Process Windows messages
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    ret = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (!ret)
                break;
        }

        av_frame_free(&frame);
        av_packet_free(&packet);
        return ret;
    }

    ~FFmpegD3D11Decoder()
    {
        if (codecCtx)
        {
            avcodec_free_context(&codecCtx);
        }
        if (formatCtx)
        {
            avformat_close_input(&formatCtx);
        }
        if (hwDeviceCtx)
        {
            av_buffer_unref(&hwDeviceCtx);
        }
    }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Parse command line for video file
    std::string videoFile = "test.h264"; // Default file
    if (strlen(lpCmdLine) > 0)
    {
        videoFile = lpCmdLine;
    }

    // Register window class
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "D3D11VideoPlayer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExA(&wc);

    // Create window
    int windowWidth = 1280;
    int windowHeight = 720;
    HWND hwnd = CreateWindowExA(
        0,
        "D3D11VideoPlayer",
        "FFmpeg D3D11VA Zero-Copy H.264 Decoder",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);

    // Initialize renderer
    D3D11Renderer renderer;
    if (!renderer.Initialize(hwnd, windowWidth, windowHeight))
    {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return -1;
    }

    // Initialize decoder
    FFmpegD3D11Decoder decoder;
    if (!decoder.Initialize(videoFile.c_str(), &renderer))
    {
        std::cerr << "Failed to initialize decoder" << std::endl;
        return -1;
    }

    std::cout << "Starting zero-copy hardware decoding playback..." << std::endl;
    std::cout << "Press ESC to exit" << std::endl;

    // Decode and render
    decoder.DecodeAndRender();

    return 0;
}
