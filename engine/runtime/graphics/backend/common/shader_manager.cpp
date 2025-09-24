#include "shader_manager.h"
#include "log/logger.h"

namespace kpengine::graphics
{

    Shader* ShaderManager::GetShader(ShaderHandle handle)
    {
        ShaderSlot* resource = GetShaderResource(handle);
        if(resource)
        {
            return resource->shader.get();
        }
        return nullptr;
    }

    ShaderSlot* ShaderManager::GetShaderResource(ShaderHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);
        if(index >= resources_.size())
        {
            KP_LOG("OpenglShaderManagerLog", LOG_LEVEL_ERROR, "Faile to found shader, generation mismatch or shader is null");
            return nullptr;
        }
        return &resources_[index];
    }
    bool ShaderManager::DestroyShader(ShaderHandle handle)
    {
        ShaderSlot* slot = GetShaderResource(handle);
        if(!slot)
        {
            return false;
        }
        path_to_handle_.erase(slot->path);
        slot->shader.reset();
        slot->path.clear();
        return handle_system_.Destroy(handle);
   }

}