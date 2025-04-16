#ifndef KPENGINE_RUNTIME_GL_VERTEX_ARRAY_GUARD_H
#define KPENGINE_RUNTIME_GL_VERTEX_ARRAY_GUARD_H

namespace kpengine{
    class GlVertexArrayGuard{
    public:
        GlVertexArrayGuard(unsigned int vao);
        ~GlVertexArrayGuard();
    };
}

#endif