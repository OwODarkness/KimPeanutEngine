#ifndef KPENGINE_RUNTIME_GRAPHICS_SHADER_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_SHADER_MANAGER_H

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include "opengl/opengl_shader.h"
#include "vulkan/vulkan_shader.h"
#include "base/base.h"
#include "api.h"

namespace kpengine::graphics
{

    struct ShaderSlot
    {
        std::unique_ptr<Shader> shader;
        uint32_t generation = 0;
    };


    template<GraphicsAPIType type>
    struct ShaderTypeTrait;
    
    template<>
    struct ShaderTypeTrait<GraphicsAPIType::GRAPHICS_API_OPENGL>
    {
        using ShaderClass = OpenglShader;
    };

    template<>
    struct ShaderTypeTrait<GraphicsAPIType::GRAPHICS_API_VULKAN>
    {
        using ShaderClass = VulkanShader;
    };

    class ShaderManager
    {
    public:
        template<GraphicsAPIType graphics_type>
        ShaderHandle LoadShader(const ShaderType type, const std::string &path)
        {
            if(path_to_handle_.contains(path))
            {
                return path_to_handle_[path];
            }

            using ShaderClass = typename ShaderTypeTrait<graphics_type>::ShaderClass;

            std::unique_ptr<ShaderClass> shader = std::make_unique<ShaderClass>(type, path);
            uint32_t id = UINT32_MAX;

            if (!free_slots_.empty())
            {
                id = free_slots_.back();
                free_slots_.pop_back();
                shaders_[id].shader = std::move(shader);
            }
            else
            {
                id = static_cast<uint32_t>(shaders_.size());
                shaders_.push_back({std::move(shader), 0});
            }
            ShaderHandle handle = {id, shaders_[id].generation};
            path_to_handle_[path] = handle;
            return handle;
        }
        Shader *GetShader(ShaderHandle handle);
        bool DestroyShader(ShaderHandle handle);

    private:
        std::unordered_map<std::string, ShaderHandle> path_to_handle_;
        std::vector<ShaderSlot> shaders_;
        std::vector<uint32_t> free_slots_;
    };
}

#endif