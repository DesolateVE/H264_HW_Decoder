#include <Windows.h>
#include <iostream>
#include <string>
#include <SDL3/SDL.h>
#include "D3D11Renderer.h"
#include "FFmpegDecoder.h"

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

    // Initialize SDL3 (window + events)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    int windowWidth = 1280;
    int windowHeight = 720;
    SDL_Window *window = SDL_CreateWindow(
        "FFmpeg D3D11VA Zero-Copy H.264 Decoder",
        windowWidth, windowHeight,
        SDL_WINDOW_RESIZABLE);

    if (!window)
    {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // Get native HWND from SDL window
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd)
    {
        std::cerr << "Failed to get HWND from SDL window" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

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

    // SDL3 event loop + decode-per-frame
    bool running = true;
    bool paused = false;
    SDL_Event ev;
    while (running)
    {
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            else if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                if (ev.key.key == SDLK_ESCAPE)
                    running = false;
                else if (ev.key.key == SDLK_SPACE)
                    paused = !paused;
            }
        }

        if (!paused)
        {
            if (!decoder.DecodeOneFrame())
                running = false; // EOF or error
        }
        else
        {
            SDL_Delay(10); // avoid busy loop when paused
        }
    }

    // Cleanup
    delete renderer;
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
