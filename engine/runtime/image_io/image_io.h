#ifndef KPENGINE_RUNTIME_IMAGE_IO_H
#define KPENGINE_RUNTIME_IMAGE_IO_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kpengine::image_io
{
    // The first codec contract is deliberately small. Callers that need a
    // different CPU representation must convert explicitly rather than making
    // image-file formats leak into Asset, Render, or Graphics.
    enum class ImagePixelFormat : uint8_t
    {
        Rgba8,
    };

    struct ImageBuffer
    {
        uint32_t width = 0;
        uint32_t height = 0;
        ImagePixelFormat format = ImagePixelFormat::Rgba8;
        std::vector<uint8_t> pixels;

        size_t ExpectedByteCount() const;
        bool IsValid() const;
    };

    struct ImageIoResult
    {
        bool success = false;
        std::string diagnostic;
    };

    struct ImageDecodeResult
    {
        ImageIoResult result;
        ImageBuffer image;
    };

    // Decodes a supported source image into tightly packed RGBA8 pixels in the
    // engine's bottom-origin texture convention. The caller owns file
    // identity, caching, and any GPU/texture format policy.
    ImageDecodeResult DecodeImageFile(const std::string &path);

    // Writes a tightly packed RGBA8 image as a lossless PNG. Path selection and
    // directory creation remain caller policy.
    ImageIoResult WritePng(const ImageBuffer &image, const std::string &path);
}

#endif
