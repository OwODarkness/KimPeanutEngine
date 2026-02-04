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
#include "graphics_context.h"

namespace kpengine::graphics
{

    struct ShaderSlot
    {
        std::unique_ptr<Shader> shader;
        std::string path;
    };

    template <GraphicsAPIType type>
    struct ShaderTypeTrait;

    template <>
    struct ShaderTypeTrait<GraphicsAPIType::GRAPHICS_API_OPENGL>
    {
        using ShaderClass = OpenglShader;
    };

    template <>
    struct ShaderTypeTrait<GraphicsAPIType::GRAPHICS_API_VULKAN>
    {
        using ShaderClass = VulkanShader;
    };

    class ShaderManager
    {
    public:
        template <GraphicsAPIType graphicstype>
        ShaderHandle CreateShader(ShaderType type, const std::string &path)
        {
            
            if (path_to_handle_.find(path) != path_to_handle_.end())
            {
                return path_to_handle_[path];
            }

            ShaderHandle handle = handle_system_.Create();

            if (handle.id == resources_.size())
            {
                resources_.emplace_back();
            }

            ShaderSlot &resource = resources_[handle.id];

            resource.path = path;
            using ShaderClass = ShaderTypeTrait<graphicstype>::ShaderClass;
            resource.shader = std::make_unique<ShaderClass>(type, path);

            path_to_handle_[path] = handle;
            return handle;
        }
        Shader *GetShader(ShaderHandle handle);
        bool DestroyShader(ShaderHandle handle);

    private:
        ShaderSlot *GetShaderResource(ShaderHandle handle);

    private:
        std::unordered_map<std::string, ShaderHandle> path_to_handle_;
        std::vector<ShaderSlot> resources_;
        HandleSystem<ShaderHandle> handle_system_;
    };
}

#endif