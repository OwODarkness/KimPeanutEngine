#version 460 core
layout (location = 0) in vec3 in_location;
layout (location = 1) in vec2 in_texcoord;

out vec2 texcoord;

void main()
{
    texcoord = in_texcoord;
    gl_Position = vec4(in_location, 1.0);
}