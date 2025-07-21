#ifndef KPENGINE_RUNTIME_GEOMETRY_BUFFER_H
#define KPENGINE_RUNTIME_GEOMETRY_BUFFER_H

namespace kpengine{
struct GeometryBuffer{
    GeometryBuffer(): vao(0), vbo(0), ebo(0){}
    GeometryBuffer(unsigned int in_vao, unsigned int in_vbo, unsigned int in_ebo):vao(in_vao), vbo(in_vbo), ebo(in_ebo){}
    unsigned int vao;
    unsigned int vbo;
    unsigned int ebo;
};
}

#endif