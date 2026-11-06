#ifndef KPENGINE_RUNTIME_RENDER_TEXTURE_FOOTPRINT_H
#define KPENGINE_RUNTIME_RENDER_TEXTURE_FOOTPRINT_H

#include <cstdint>
#include <optional>

namespace kpengine::render
{
    struct TextureFootprint
    {
        uint64_t payload_bytes = 0;
        std::optional<uint64_t> allocation_bytes{0};

        TextureFootprint &operator+=(const TextureFootprint &other) noexcept
        {
            payload_bytes += other.payload_bytes;
            allocation_bytes = allocation_bytes && other.allocation_bytes
                ? std::optional<uint64_t>{*allocation_bytes + *other.allocation_bytes}
                : std::nullopt;
            return *this;
        }
    };
}

#endif
