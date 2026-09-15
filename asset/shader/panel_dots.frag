#version 450

layout(std140, binding = 0) uniform PanelDrawData
{
    vec4 dot_color;
    vec4 background_color;
    vec4 accent_color;
    vec4 params; // x dot gap, y gradient amount, z gradient axis
    vec4 motion; // x seconds, y cycles per second
    vec4 ink_bounds; // min x, min y, max x, max y of the lit extent
} draw_data;

layout(binding = 1) uniform sampler2D dotMask;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float kTau = 6.28318530718;

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

    // The colour ramp, if one is asked for. A cosine over the axis rather than a
    // linear ramp, so it loops without a seam when it animates and so both ends
    // of the panel return to the first colour. Amount zero collapses this to
    // dot_color exactly, which is why an unset gradient changes nothing.
    float amount = clamp(draw_data.params.y, 0.0, 1.0);
    // Normalized within the lit extent, not the panel: a short line of text
    // occupies a fraction of a wide panel, so a ramp across the panel would move
    // the colour almost not at all over the text it is meant to colour. The
    // epsilon keeps a blank or single-dot panel from dividing by zero.
    vec2 lo = draw_data.ink_bounds.xy;
    vec2 span = max(draw_data.ink_bounds.zw - lo, vec2(1.0e-4));
    float t = 0.0;
    if (draw_data.params.z < 0.5)
    {
        t = (fragUV.x - lo.x) / span.x;
    }
    else if (draw_data.params.z < 1.5)
    {
        t = (fragUV.y - lo.y) / span.y;
    }
    else
    {
        // Outward from the middle of the lit extent, so both halves agree.
        t = abs(fragUV.x - (lo.x + span.x * 0.5)) / (span.x * 0.5);
    }
    float phase = clamp(t, 0.0, 1.0) - draw_data.motion.x * draw_data.motion.y;
    float ramp = (0.5 - 0.5 * cos(kTau * phase)) * amount;
    vec3 lit_color = mix(draw_data.dot_color.rgb, draw_data.accent_color.rgb, ramp);

    // Without the bezel, four lit dots in a row become one solid slab and the
    // panel stops looking like a matrix of elements.
    vec3 color = mix(draw_data.background_color.rgb, lit_color, lit * element);
    outColor = vec4(color, 1.0);
}
