#version 450

layout(binding = 0) uniform Live2DMaskData
{
    mat4 model_to_mask;
    uint channel;
    float opacity;
    vec2 padding;
} mask_data;

layout(binding = 1) uniform sampler2D texSampler;
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    float value = 1.0 - texture(texSampler, fragUV).a *
                  clamp(mask_data.opacity, 0.0, 1.0);
    outColor = vec4(value, value, value, value);
}
