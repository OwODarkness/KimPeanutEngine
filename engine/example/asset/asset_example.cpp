#include "asset_example.h"
#include <iostream>
#include <string>
#include "asset/asset_manager.h"
#include "config/path.h"

namespace kpengine::example{
    void TextureLoad()
    {   
        using namespace asset;
        auto &asset_mananger = AssetManager::GetInstance();

        std::string path = GetTextureDirectory() + "default.png";
        
        AssetID id = asset_mananger.LoadSync(path);


        //asset_mananger.LoadSync()
    }
}