#pragma once

#include <Windows.h>
#include <iostream>
#include <chrono>
#include <SDL3/SDL.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include "D3D11Renderer.h"

class FFmpegD3D11Decoder
{
private:
    AVFormatContext *formatCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    AVBufferRef *hwDeviceCtx = nullptr;
    int videoStreamIndex = -1;
    ID3D11RendererBase *renderer = nullptr;
    // Reusable decode objects and timing for SDL-driven loop
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    double frameDurationMs = 0.0;
    std::chrono::high_resolution_clock::time_point lastFrameTime;

public:
    bool Initialize(const char *filename, ID3D11RendererBase *render)
    {
        renderer = render;

        // Open input file
        if (avformat_open_input(&formatCtx, filename, nullptr, nullptr) < 0)
        {
            std::cerr << "Could not open input file: " << filename << std::endl;
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

        // Setup frame timing
        AVStream *videoStream = formatCtx->streams[videoStreamIndex];
        AVRational frameRate = videoStream->avg_frame_rate;
        if (frameRate.num > 0 && frameRate.den > 0)
            frameDurationMs = 1000.0 * frameRate.den / frameRate.num;
        else
            frameDurationMs = 1000.0 / 30.0;
        lastFrameTime = std::chrono::high_resolution_clock::now();

        // Allocate reusable packet/frame
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!packet || !frame)
        {
            std::cerr << "Failed to allocate packet/frame" << std::endl;
            return false;
        }

        std::cout << "Decoder initialized with D3D11VA hardware acceleration\n"
                  << "Frame duration: " << frameDurationMs << " ms/frame" << std::endl;
        return true;
    }

    // Decode and render at most one frame; return false on EOF/error
    bool DecodeOneFrame()
    {
        if (!formatCtx || !codecCtx || !packet || !frame)
            return false;

        int r = av_read_frame(formatCtx, packet);
        if (r < 0)
        {
            // EOF or error
            return false;
        }

        if (packet->stream_index == videoStreamIndex)
        {
            if (avcodec_send_packet(codecCtx, packet) == 0)
            {
                if (avcodec_receive_frame(codecCtx, frame) == 0)
                {
                    if (frame->format == AV_PIX_FMT_D3D11)
                    {
                        ID3D11Texture2D *texture = (ID3D11Texture2D *)frame->data[0];
                        int textureIndex = (int)(intptr_t)frame->data[1];
                        renderer->RenderFrame(texture, textureIndex);

                        // Pace to frame rate using SDL_Delay
                        auto now = std::chrono::high_resolution_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
                        double delay = frameDurationMs - elapsed;
                        if (delay > 0)
                            SDL_Delay((Uint32)delay);
                        lastFrameTime = std::chrono::high_resolution_clock::now();
                    }
                    av_frame_unref(frame);
                }
            }
        }

        av_packet_unref(packet);
        return true;
    }

    bool DecodeAndRender()
    {
        // Backward-compatible blocking loop without Win32 message pump
        while (DecodeOneFrame())
        {
            // Let SDL update internal state; event handling is done in main
            SDL_PumpEvents();
        }
        return true;
    }

    ~FFmpegD3D11Decoder()
    {
        if (frame)
            av_frame_free(&frame);
        if (packet)
            av_packet_free(&packet);
        if (codecCtx)
            avcodec_free_context(&codecCtx);
        if (formatCtx)
            avformat_close_input(&formatCtx);
        if (hwDeviceCtx)
            av_buffer_unref(&hwDeviceCtx);
    }
};
