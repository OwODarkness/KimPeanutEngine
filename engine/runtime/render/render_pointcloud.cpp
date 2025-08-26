#include "render_pointcloud.h"

#include <glad/glad.h>
#include "model_loader.h"
#include "render_pointcloud_resource.h"
#include "runtime/core/utility/utility.h"

namespace kpengine{
    RenderPointCloud::RenderPointCloud():
    pointcloud_resource_(nullptr),name_("")
    {

    }

    RenderPointCloud::RenderPointCloud(const std::string& relative_path):
    pointcloud_resource_(std::make_unique<RenderPointCloudResource>()), name_(relative_path)
    {

    }

    void RenderPointCloud::Initialize()
    {
        //TODO: load pointcloud
        ModelLoader::Load(name_, *pointcloud_resource_);
        
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        
        GlVertexArrayGuard vao_guard(vao_);

        GlVertexBufferGuard vbo_guard(vbo_);
        glBufferData(GL_ARRAY_BUFFER, pointcloud_resource_->vertex_buffer_.size(), pointcloud_resource_->vertex_buffer_.data(), GL_STATIC_DRAW );

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vector3f), (void*)0);
    }

    RenderPointCloud::~RenderPointCloud()
    {
        glDeleteBuffers(1, &vbo_);
    }
}