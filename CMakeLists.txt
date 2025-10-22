cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
project(H264_HW_Decoder)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 抑制 vcpkg FFmpeg 包装器的开发警告
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 查找FFmpeg包
find_package(FFMPEG REQUIRED)

# 源文件 (重构版本)
set(SOURCES_REFACTORED
    src/main_refactored.cpp
    src/D3D11Renderer.cpp
)

# 头文件 (重构版本)
set(HEADERS_REFACTORED
    src/D3D11Renderer.h
    src/D3D11ShaderRenderer.h
    src/D3D11VideoProcessorRenderer.h
    src/FFmpegDecoder.h
)

# 创建可执行文件 (控制台程序) - 使用重构版本
add_executable(${PROJECT_NAME} ${SOURCES_REFACTORED} ${HEADERS_REFACTORED})

# 定义Unicode宏
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    ${FFMPEG_LIBRARIES}
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)