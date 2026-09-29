#ifndef KPENGINE_RUNTIME_RENDER_SHADER_SIGNATURE_H
#define KPENGINE_RUNTIME_RENDER_SHADER_SIGNATURE_H

#include <cstdint>

#include "asset/shader.h"

namespace kpengine::render::detail
{
    inline void AddShaderSignature(uint64_t &signature, const data::ShaderData &shader)
    {
        const auto add = [&signature](uint64_t value) {
            signature ^= value;
            signature *= 1099511628211ull;
        };
        add(static_cast<uint32_t>(shader.stage));
        add(static_cast<uint32_t>(shader.api));
        add(shader.byte_code.size());
        for (const uint8_t byte : shader.byte_code)
        {
            add(byte);
        }
        add(shader.source.size());
        for (const unsigned char character : shader.source)
        {
            add(character);
        }
        for (const unsigned char character : shader.entry)
        {
            add(character);
        }
    }
}

#endif
