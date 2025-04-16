#include "gl_vertex_array_guard.h"

#include <glad/glad.h>

namespace kpengine{
    GlVertexArrayGuard::GlVertexArrayGuard(unsigned int vao)
    {
        glBindVertexArray(vao);
    }
    GlVertexArrayGuard::~GlVertexArrayGuard()
    {
        glBindVertexArray(0);
    }
}