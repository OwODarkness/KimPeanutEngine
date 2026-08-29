#include "image_io/image_io.h"
#include "image_io/image_codec.h"

#include <limits>
#include <utility>

namespace kpengine::image_io
{
    namespace
    {
        constexpr size_t kRgba8PixelBytes = 4;
    }

    size_t ImageBuffer::ExpectedByteCount() const
    {
        if (width == 0 || height == 0 || format != ImagePixelFormat::Rgba8)
        {
            return 0;
        }
        if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height)
        {
            return 0;
        }
        const size_t pixel_count = static_cast<size_t>(width) * height;
        if (pixel_count > std::numeric_limits<size_t>::max() / kRgba8PixelBytes)
        {
            return 0;
        }
        return pixel_count * kRgba8PixelBytes;
    }

    bool ImageBuffer::IsValid() const
    {
        const size_t expected_byte_count = ExpectedByteCount();
        return expected_byte_count != 0 && pixels.size() == expected_byte_count;
    }

    ImageDecodeResult DecodeImageFile(const std::string &path)
    {
        return GetDefaultImageCodec().DecodeFile(path);
    }

    ImageIoResult WritePng(const ImageBuffer &image, const std::string &path)
    {
        if (path.empty())
        {
            return {false, "PNG output path is empty"};
        }
        if (!image.IsValid())
        {
            return {false, "PNG export requires a valid tightly packed RGBA8 image"};
        }
        return GetDefaultImageCodec().WritePngFile(image, path);
    }
}
