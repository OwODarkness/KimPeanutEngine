#include "shader_manager.h"
#include "log/logger.h"

namespace kpengine::graphics
{


    Shader* ShaderManager::GetShader(ShaderHandle handle)
    {
        if (handle.id >= shaders_.size())
        {
            KP_LOG("OpenglShaderManagerLog", LOG_LEVEL_ERROR, "Faile to found shader, out of range");
            return nullptr;
        }

        ShaderSlot &slot = shaders_[handle.id];

        if(slot.generation != handle.generation || !slot.shader)
        {
            KP_LOG("OpenglShaderManagerLog", LOG_LEVEL_ERROR, "Faile to found shader, generation mismatch or shader is null");
            return nullptr;
        }
        return slot.shader.get();
    }
    bool ShaderManager::DestroyShader(ShaderHandle handle)
    {
        if (handle.id >= shaders_.size())
        {
            KP_LOG("OpenglShaderManagerLog", LOG_LEVEL_ERROR, "Faile to destroy shader, out of range");
            return false;
        }

        ShaderSlot &slot = shaders_[handle.id];
        path_to_handle_.erase(slot.shader->GetPath());
        slot.shader.reset();
        slot.generation++;
        return true;
    }

}