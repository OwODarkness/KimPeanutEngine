#include "gl_vertex_buffer_guard.h"

#include <glad/glad.h>

namespace kpengine{
    GlVertexBufferGuard::GlVertexBufferGuard(unsigned int vbo)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
    }

    GlVertexBufferGuard::~GlVertexBufferGuard()
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}