#version 330 core

layout (triangles) in;
layout(triangle_strip, max_vertices = 18) out;

uniform mat4 shadow_matrices[6];

out vec4 frag_position;

void main()
{
    for(int face = 0;face < 6;face++)
    {
        gl_Layer = face;
        for(int ver_index = 0;ver_index <3 ;ver_index++)
        {
            frag_position = gl_in[ver_index].gl_Position;
            gl_Position = shadow_matrices[face] * frag_position;
            EmitVertex();
        }
        EndPrimitive();
    }
}