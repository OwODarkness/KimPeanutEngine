#include "gl_element_buffer_guard.h"

#include <glad/glad.h>

namespace kpengine{
    GlElementBufferGuard::GlElementBufferGuard(unsigned int ebo)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    }

    GlElementBufferGuard::~GlElementBufferGuard()
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}