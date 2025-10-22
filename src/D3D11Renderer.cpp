#include "D3D11Renderer.h"
#include "D3D11ShaderRenderer.h"
#include "D3D11VideoProcessorRenderer.h"

ID3D11RendererBase *D3D11RendererFactory::Create(Mode mode)
{
    switch (mode)
    {
    case Mode::Shader:
        return new D3D11ShaderRenderer();
    case Mode::VideoProcessor:
        return new D3D11VideoProcessorRenderer();
    default:
        return nullptr;
    }
}
