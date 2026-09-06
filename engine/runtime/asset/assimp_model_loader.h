#ifndef KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_LOADER_H

#include <memory>
#include "assimp_model_decoder.h"
#include "model_loader.h"

namespace kpengine::asset
{
    class Assimp_ModelLoader : public IModelLoader
    {
    public:
        Assimp_ModelLoader();
        ~Assimp_ModelLoader() override;
        Assimp_ModelLoader(const Assimp_ModelLoader&) = delete;
        Assimp_ModelLoader& operator=(const Assimp_ModelLoader&) = delete;

        virtual bool Load(const std::string& path, ModelGeometryType type,  AssetRegisterInfo &info) override;
    private:
        AssetID LoadMesh(const std::string &path);
        AssimpModelDecoder decoder_;
    };
}

#endif
