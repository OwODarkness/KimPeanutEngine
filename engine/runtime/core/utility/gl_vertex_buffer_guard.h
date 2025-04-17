#ifndef KPENGINE_RUNTIME_GL_VERTEX_BUFFER_GUARD_H
#define KPENGINE_RUNTIME_GL_VERTEX_BUFFER_GUARD_H

namespace kpengine{
class GlVertexBufferGuard{
public:
    GlVertexBufferGuard(unsigned int vbo);
    ~GlVertexBufferGuard();
};
}
#endif
