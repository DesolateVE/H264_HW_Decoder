# FFmpeg D3D11VA 零拷贝 H.264 硬件解码器

基于 FFmpeg + D3D11VA 的 H.264 硬件解码器,实现 GPU 零拷贝显示。

## 特性

- ✅ **硬件加速解码**: 使用 D3D11VA 进行 GPU 解码
- ✅ **零拷贝架构**: 数据始终在 GPU 显存,不经过 CPU
- ✅ **双渲染模式**: Shader 转换 / Video Processor 硬件加速
- ✅ **帧率同步**: 自动同步视频帧率播放

## 编译

```bash
# 配置
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 编译 Debug 版本
cmake --build build --config Debug

# 编译 Release 版本
cmake --build build --config Release
```

## 使用

### Shader 转换模式 (默认)
```bash
.\build\bin\Debug\H264_HW_Decoder.exe video.mp4
```

### Video Processor 模式 (推荐,性能更好)
```bash
.\build\bin\Debug\H264_HW_Decoder.exe video.mp4 --vp
```

### 控制
- `ESC` 键退出

## 项目结构

```
src/
├── main_refactored.cpp              # 主程序入口
├── D3D11Renderer.h/.cpp             # 渲染器接口和工厂
├── D3D11ShaderRenderer.h            # Shader 转换渲染器
├── D3D11VideoProcessorRenderer.h   # Video Processor 渲染器
└── FFmpegDecoder.h                  # FFmpeg 解码器封装
```

## 渲染模式对比

| 模式 | 转换方式 | GPU 单元 | 性能 | 推荐场景 |
|------|---------|---------|------|---------|
| **Shader** | 自定义 HLSL Shader | 通用计算 | 好 | 学习、调试 |
| **Video Processor** | 硬件 VP API | 专用硬件 | 最优 | 生产环境 |

## 技术细节

### 零拷贝流程
```
视频文件 → FFmpeg解码(GPU) → D3D11纹理(GPU显存) 
  → YUV转RGB(GPU) → 交换链显示
```

### 关键技术
- **D3D11VA**: DirectX Video Acceleration 硬件解码
- **NV12 格式**: 解码器输出的 YUV 格式
- **Shader/VP 转换**: GPU 上进行 YUV → RGB 颜色空间转换
- **GPU 内拷贝**: `CopySubresourceRegion` 在显存内部传输

## 依赖

- FFmpeg (通过 vcpkg)
- DirectX 11
- Windows 10/11
- 支持 D3D11VA 的 GPU

## 许可

MIT License
