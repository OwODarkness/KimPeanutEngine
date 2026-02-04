#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_PIPELINE_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_PIPELINE_H

#include <glad/glad.h>
#include "common/pipeline_types.h"

namespace kpengine::graphics
{
    class OpenglPipeline
    {
    public:
        void Initialize(const PipelineDesc &desc);
        void Bind() const;
        void Destroy();

    private:
        GLuint shader_program_;
        bool depth_clamp_enabled_;
        bool rasterizer_discard_enabled_;

        bool depth_test_enabled_;
        GLenum polygon_mode_;
        bool cull_face_enabled_;
        GLenum cull_mode_;
        GLenum front_face_mode_;

        bool depth_bias_enabled_;
        float depth_bias_constant = 0.f;
        float depth_bias_slope = 0.f;

        float line_width_;

    public:
        std::vector<VertexBindingDesc> binding_descs_;
        std::vector<VertexAttributionDesc> attri_descs_;
        std::vector<std::vector<DescriptorBindingDesc>> descriptor_binding_descs_;
        GLenum primitive_topology_type_;
    };
}

#endif