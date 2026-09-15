#version 450

layout(std140, binding = 0) uniform PanelDrawData
{
    vec4 dot_color;
    vec4 background_color;
    vec4 params; // x: dot gap as a fraction of a dot
} draw_data;

layout(binding = 1) uniform sampler2D dotMask;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // One texel is one dot, and the quad is drawn larger than the mask, so this
    // lookup is the whole dot-to-pixel expansion: no per-dot geometry.
    float lit = texture(dotMask, fragUV).r;

    // The bezel. Position within the current dot, taken in dot space rather than
    // in pixels, so the gap is a constant fraction of a dot at any resolution --
    // the panel reads the same when it is 2 pixels per dot and when it is 16.
    ivec2 dot_count = textureSize(dotMask, 0);
    vec2 cell = fract(fragUV * vec2(dot_count));
    float gap = clamp(draw_data.params.x, 0.0, 0.45);
    vec2 inside = step(vec2(gap), cell) * step(cell, vec2(1.0 - gap));
    float element = inside.x * inside.y;

    // Without the bezel, four lit dots in a row become one solid slab and the
    // panel stops looking like a matrix of elements.
    vec3 color = mix(draw_data.background_color.rgb, draw_data.dot_color.rgb,
                     lit * element);
    outColor = vec4(color, 1.0);
}
