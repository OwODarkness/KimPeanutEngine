#version 330 core
layout (location = 0) in vec3 in_location;

out vec3 texcoord;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    texcoord = in_location;
    vec4 pos = projection * view * vec4(in_location, 1.f);
    gl_Position = pos.xyww;
}