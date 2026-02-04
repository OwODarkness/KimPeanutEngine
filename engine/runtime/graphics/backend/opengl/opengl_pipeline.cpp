#include "opengl_pipeline.h"
#include "log/logger.h"
#include "common/shader.h"
#include "opengl_enum.h"
namespace kpengine::graphics
{
    void OpenglPipeline::Initialize(const PipelineDesc &desc)
    {
        int bsucceed = 0;
        char log[512];

        // set shader program
        shader_program_ = glCreateProgram();

        GLuint gl_vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        if (desc.vert_shader)
        {
            const char *vertex_shader_code = reinterpret_cast<const char *>(desc.vert_shader->GetCode());
            glShaderSource(gl_vertex_shader, 1, &vertex_shader_code, nullptr);
            glCompileShader(gl_vertex_shader);
            glGetShaderiv(gl_vertex_shader, GL_COMPILE_STATUS, &bsucceed);
            if (!bsucceed)
            {
                glGetShaderInfoLog(gl_vertex_shader, 512, nullptr, log);
                KP_LOG("OpenglShaderCompilerLog", LOG_LEVEL_ERROR, log);
            }
            else
            {
                glAttachShader(shader_program_, gl_vertex_shader);
            }
        }
        GLuint gl_frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
        if (desc.frag_shader)
        {
            const char *frag_shader_code = reinterpret_cast<const char *>(desc.frag_shader->GetCode());
            glShaderSource(gl_frag_shader, 1, &frag_shader_code, nullptr);
            glCompileShader(gl_frag_shader);
            glGetShaderiv(gl_vertex_shader, GL_COMPILE_STATUS, &bsucceed);
            if (!bsucceed)
            {
                glGetShaderInfoLog(gl_frag_shader, 512, nullptr, log);
                KP_LOG("OpenglShaderCompilerLog", LOG_LEVEL_ERROR, log);
            }
            else
            {
                glAttachShader(shader_program_, gl_frag_shader);
            }
        }

        GLuint gl_geom_shader = glCreateShader(GL_GEOMETRY_SHADER);
        if (desc.geom_shader)
        {
            const char *geom_shader_code = reinterpret_cast<const char *>(desc.geom_shader->GetCode());
            glShaderSource(gl_geom_shader, 1, &geom_shader_code, nullptr);
            glCompileShader(gl_geom_shader);
            glGetShaderiv(gl_geom_shader, GL_COMPILE_STATUS, &bsucceed);
            if (!bsucceed)
            {
                glGetShaderInfoLog(gl_geom_shader, 512, nullptr, log);
                KP_LOG("OpenglShaderCompilerLog", LOG_LEVEL_ERROR, log);
            }
            else
            {
                glAttachShader(shader_program_, gl_geom_shader);
            }
        }

        glLinkProgram(shader_program_);
        glGetProgramiv(shader_program_, GL_LINK_STATUS, &bsucceed);
        if (!bsucceed)
        {
            glGetProgramInfoLog(shader_program_, 512, nullptr, log);
            KP_LOG("OpenglShaderLinkerLog", LOG_LEVEL_ERROR, log);
        }

        glDeleteShader(gl_vertex_shader);
        glDeleteShader(gl_frag_shader);
        glDeleteShader(gl_geom_shader);

        binding_descs_ = desc.binding_descs;
        attri_descs_ = desc.attri_descs;

        primitive_topology_type_ = ConvertToOpenglPrimitiveTopology(desc.primitive_topology_type);

        depth_clamp_enabled_ = desc.raster_state.depth_clamp_enabled;
        rasterizer_discard_enabled_ = desc.raster_state.rasterizer_discard_enabled;

        polygon_mode_ = ConvertToOpenglPolygonMode(desc.raster_state.polygon_mode);
        if (desc.raster_state.cull_mode == CullMode::CULL_MODE_NONE)
        {
            cull_face_enabled_ = false;
        }
        else
        {
            cull_face_enabled_ = true;
            cull_mode_ = ConvertTooPENGLCullMode(desc.raster_state.cull_mode);
        }
        front_face_mode_ = ConvertToOpenglFrontFaceMode(desc.raster_state.front_face);

        depth_bias_enabled_ = desc.raster_state.depth_bias_enabled;
        depth_bias_constant = desc.raster_state.depth_bias_constant;
        depth_bias_slope = desc.raster_state.depth_bias_slope;

        line_width_ = desc.raster_state.line_width;

        descriptor_binding_descs_ = desc.descriptor_binding_descs;
    }

    void OpenglPipeline::Bind() const
    {
        glEnable(GL_FRAMEBUFFER_SRGB);
        glUseProgram(shader_program_);

        if (depth_clamp_enabled_)
        {
            glEnable(GL_DEPTH_CLAMP);
        }
        else
        {
            glDisable(GL_DEPTH_CLAMP);
        }

        if (rasterizer_discard_enabled_)
        {
            glEnable(GL_RASTERIZER_DISCARD);
        }
        else
        {
            glDisable(GL_RASTERIZER_DISCARD);
        }

        glPolygonMode(GL_FRONT_AND_BACK, polygon_mode_);

        if (cull_face_enabled_)
        {
            glEnable(GL_CULL_FACE);
            glCullFace(cull_mode_);
        }
        else
        {
            glDisable(GL_CULL_FACE);
        }

        glFrontFace(front_face_mode_);

        if (depth_bias_enabled_)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(depth_bias_slope, depth_bias_constant);
        }
        else
        {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        glLineWidth(line_width_);
    }

    void OpenglPipeline::Destroy()
    {
        glDeleteProgram(shader_program_);
    }
}