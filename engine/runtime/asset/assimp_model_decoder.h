#ifndef KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_DECODER_H
#define KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_DECODER_H

#include <filesystem>
#include <memory>

#include "imported_model.h"

namespace kpengine::asset
{
    // Pure foreign-format decoding. This type owns no AssetManager state and
    // returns only value-owned CPU data for the later import stages.
    class AssimpModelDecoder final
    {
    public:
        AssimpModelDecoder();
        ~AssimpModelDecoder() noexcept;

        AssimpModelDecoder(const AssimpModelDecoder &) = delete;
        AssimpModelDecoder &operator=(const AssimpModelDecoder &) = delete;
        AssimpModelDecoder(AssimpModelDecoder &&other) noexcept;
        AssimpModelDecoder &operator=(AssimpModelDecoder &&other) noexcept;

        ImportedModelDocument Decode(const std::filesystem::path &source_path);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif
