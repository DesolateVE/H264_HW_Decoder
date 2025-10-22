#include <Windows.h>
#include <iostream>
#include <string>
#include "D3D11Renderer.h"
#include "FFmpegDecoder.h"

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int main(int argc, char* argv[])
{
    // Parse command line
    std::string videoFile = "test.h264";
    D3D11RendererFactory::Mode renderMode = D3D11RendererFactory::Mode::Shader;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--vp" || arg == "-vp")
        {
            renderMode = D3D11RendererFactory::Mode::VideoProcessor;
        }
        else if (arg[0] != '-')
        {
            videoFile = arg;
        }
    }

    // Create window
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "D3D11VideoPlayer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExA(&wc);

    int windowWidth = 1280;
    int windowHeight = 720;
    HWND hwnd = CreateWindowExA(
        0, "D3D11VideoPlayer",
        "FFmpeg D3D11VA Zero-Copy H.264 Decoder",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }

    ShowWindow(hwnd, SW_SHOW);

    // Create renderer
    ID3D11RendererBase *renderer = D3D11RendererFactory::Create(renderMode);
    if (!renderer || !renderer->Initialize(hwnd, windowWidth, windowHeight))
    {
        std::cerr << "Failed to initialize renderer" << std::endl;
        delete renderer;
        return -1;
    }

    // Create decoder
    FFmpegD3D11Decoder decoder;
    if (!decoder.Initialize(videoFile.c_str(), renderer))
    {
        std::cerr << "Failed to initialize decoder" << std::endl;
        delete renderer;
        return -1;
    }

    // Print usage
    std::cout << "\n=== FFmpeg D3D11VA Zero-Copy Decoder ===" << std::endl;
    std::cout << "Usage: H264_HW_Decoder.exe [video_file] [--vp]" << std::endl;
    std::cout << "  --vp: Use Video Processor (hardware YUV->RGB)" << std::endl;
    std::cout << "  default: Use Shader conversion" << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  ESC: Exit" << std::endl;
    std::cout << "\nPlaying: " << videoFile << std::endl;
    std::cout << "========================================\n"
              << std::endl;

    // Decode and render
    decoder.DecodeAndRender();

    // Cleanup
    delete renderer;

    return 0;
}
