#version 450

layout(std140, binding = 0) uniform PanelDrawData
{
    vec4 dot_color;
    vec4 background_color;
} draw_data;

layout(binding = 1) uniform sampler2D dotMask;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // One texel is one dot. The quad is drawn larger than the mask and the
    // sampler is NEAREST, so this lookup is the whole dot-to-pixel expansion:
    // no per-dot arithmetic, no geometry per dot.
    float lit = texture(dotMask, fragUV).r;
    outColor = mix(draw_data.background_color, draw_data.dot_color, lit);
}
