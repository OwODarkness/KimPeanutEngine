#version 460 core
layout (location = 0) in vec3 in_location;

uniform mat4 projection;
uniform mat4 view;

out vec3 world_pos;


void main()
{
    world_pos = in_location;
    gl_Position = projection * view * vec4(world_pos, 1.0);
}
