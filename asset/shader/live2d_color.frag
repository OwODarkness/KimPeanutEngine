#version 450

layout(binding = 0) uniform Live2DDrawData
{
    mat4 model_transform;
    vec4 multiply_color;
    vec4 screen_color;
    float opacity;
    vec3 padding;
} draw_data;

layout(binding = 1) uniform sampler2D texSampler;
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texture_color = texture(texSampler, fragUV);
    vec3 transformed = texture_color.rgb * draw_data.multiply_color.rgb;
    transformed += draw_data.screen_color.rgb -
                   transformed * draw_data.screen_color.rgb;
    // The live2d sampler decodes sRGB textures to linear, and the render target
    // re-encodes on store; encoding here too would double-apply the transfer.
    float alpha = texture_color.a * clamp(draw_data.opacity, 0.0, 1.0);
    outColor = vec4(transformed * alpha, alpha);
}
