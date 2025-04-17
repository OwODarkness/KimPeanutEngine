#ifndef KPENGINE_RUNTIME_GL_ELEMENT_BUFFER_GUARD_H
#define KPENGINE_RUNTIME_GL_ELEMENT_BUFFER_GUARD_H

namespace kpengine{
    class GlElementBufferGuard{
    public:
        GlElementBufferGuard(unsigned int ebo);
        ~GlElementBufferGuard();
    };
}

#endif