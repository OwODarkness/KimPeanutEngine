#version 450

layout(binding = 0) uniform Live2DMaskedData
{
    mat4 model_transform;
    vec4 multiply_color;
    vec4 screen_color;
    float opacity;
    vec3 padding;
    mat4 model_to_atlas_sample;
    uint channel;
    uint inverted;
    uvec2 mask_padding;
} draw_data;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D maskAtlas;
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec2 maskUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texture_color = texture(texSampler, fragUV);
    vec4 packed_mask = texture(maskAtlas, maskUV);
    float stored = draw_data.channel == 0u ? packed_mask.r :
                   (draw_data.channel == 1u ? packed_mask.g :
                    (draw_data.channel == 2u ? packed_mask.b : packed_mask.a));
    float coverage = draw_data.inverted != 0u ? stored : 1.0 - stored;
    vec3 transformed = texture_color.rgb * draw_data.multiply_color.rgb;
    transformed += draw_data.screen_color.rgb -
                   transformed * draw_data.screen_color.rgb;
    // The live2d sampler decodes sRGB textures to linear, and the render target
    // re-encodes on store; encoding here too would double-apply the transfer.
    float alpha = texture_color.a * clamp(draw_data.opacity, 0.0, 1.0) *
                  clamp(coverage, 0.0, 1.0);
    outColor = vec4(transformed * alpha, alpha);
}
