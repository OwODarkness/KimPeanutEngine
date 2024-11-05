#version 330 core

in vec3 out_normal;
in vec2 out_texcoord;

out vec4 out_frag_color;

uniform sampler2D texture1;

void main()
{
    out_frag_color = texture(texture1, out_texcoord);
    //vec4(1.0f, 0.f, 0.f, 1.f);
}