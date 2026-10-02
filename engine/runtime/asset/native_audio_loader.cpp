#include "native_audio_loader.h"

#include <magic_enum/magic_enum.hpp>

#include "audio.h"
#include "native_audio.h"
#include "utility.h"

namespace kpengine::asset
{
    bool NativeAudioLoader::Load(const std::string &path, AssetRegisterInfo &info) const
    {
        auto product = std::make_shared<NativeAudioFileProduct>(ReadNativeAudioFile(path));
        auto resource = std::make_shared<AudioResource>();
        resource->native_product = std::move(product);
        info.resource = std::move(resource);
        info.type = AssetType::KPAT_Audio;
        info.path = path;
        info.name = std::string(magic_enum::enum_name(info.type)) + ExtractNameFromPath(path);
        return true;
    }
}
