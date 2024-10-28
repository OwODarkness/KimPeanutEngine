#version 330 core

in vec3 out_normal;
in vec3 out_texcoord;

out vec4 out_frag_color;
void main()
{
    out_frag_color = vec4(1.0f, 0.f, 0.f, 1.f);
}