#version 450

layout(binding = 0) uniform Live2DMaskData
{
    mat4 model_to_mask;
    uint channel;
    float opacity;
    vec2 padding;
} mask_data;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 fragUV;

void main()
{
    gl_Position = mask_data.model_to_mask * vec4(inPosition, 0.0, 1.0);
    fragUV = inUV;
}
