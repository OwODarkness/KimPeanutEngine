#version 460 core
layout(location = 0) in vec2 inPos;

out vec2 uv;

void main()
{
    // Convert from NDC [-1, 1] to UV [0, 1]
    uv = inPos * 0.5 + 0.5;
    gl_Position = vec4(inPos.xy, 0.0, 1.0);
}