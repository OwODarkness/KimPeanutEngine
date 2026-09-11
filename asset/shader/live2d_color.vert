#version 450

layout(binding = 0) uniform Live2DDrawData
{
    mat4 model_transform;
    vec4 multiply_color;
    vec4 screen_color;
    float opacity;
    vec3 padding;
} draw_data;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 fragUV;

void main()
{
    gl_Position = draw_data.model_transform * vec4(inPosition, 0.0, 1.0);
    fragUV = inUV;
}
