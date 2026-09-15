#version 450

layout(std140, binding = 0) uniform BubbleDrawData
{
    mat4 placement;
    vec4 quad_size;  // width, height, aspect
    vec4 body;       // min x, min y, max x, max y in bubble units
    vec4 text;       // min x, min y, max x, max y in bubble units
    vec4 shape;      // corner radius, outline width, tail capsule count
    vec4 fill_color;
    vec4 outline_color;
    vec4 dot_color;
    vec4 tail[8];    // x, y, radius
    vec4 ink_uv;     // where the text is inside the mask
    vec4 grid;       // cell width, cell height, bezel fraction
    vec4 bezel_color;
} draw_data;

layout(binding = 1) uniform sampler2D dotMask;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const int kMaxTailCapsules = 8;

// The exact distance to a rounded rectangle: negative inside, positive outside,
// and in the same units as the point it is given.
float RoundedBox(vec2 point, vec2 center, vec2 half_size, float radius)
{
    vec2 offset = abs(point - center) - half_size + radius;
    return min(max(offset.x, offset.y), 0.0) + length(max(offset, vec2(0.0))) - radius;
}

void main()
{
    // Bubble units, where one unit is the text's height. Distances are evaluated
    // here rather than in the quad's normalized space, so a radius stays a circle
    // instead of being stretched into an ellipse by the quad's aspect.
    vec2 point = fragUV * draw_data.quad_size.xy;

    vec2 body_center = (draw_data.body.xy + draw_data.body.zw) * 0.5;
    vec2 body_half = (draw_data.body.zw - draw_data.body.xy) * 0.5;
    float distance_to_edge =
        RoundedBox(point, body_center, body_half, draw_data.shape.x);

    // The tail is unioned with the body by taking the smaller distance, which is
    // what makes the two read as one drawn shape rather than as a shape with a
    // shape stuck to it. Its first circle sits on the body's edge, so there is no
    // seam to hide.
    int capsule_count = int(draw_data.shape.z + 0.5);
    for (int index = 0; index < kMaxTailCapsules; ++index)
    {
        if (index >= capsule_count)
        {
            break;
        }
        vec4 capsule = draw_data.tail[index];
        distance_to_edge =
            min(distance_to_edge, length(point - capsule.xy) - capsule.z);
    }

    // Anti-aliased from the distance's own screen-space derivative, so the edge
    // stays about one pixel wide however large the bubble is drawn.
    float aa = max(fwidth(distance_to_edge), 1.0e-5);
    float coverage = 1.0 - smoothstep(-aa, aa, distance_to_edge);
    float interior = 1.0 - smoothstep(-aa, aa, distance_to_edge + draw_data.shape.y);
    float outline = coverage - interior;

    // The text is one bit per dot, sampled inside the region the layout reserved
    // for it. Outside that region there is no text, which is what stops a
    // stretched lookup from smearing the mask across the whole bubble.
    float ink = 0.0;
    if (all(greaterThanEqual(point, draw_data.text.xy)) &&
        all(lessThanEqual(point, draw_data.text.zw)))
    {
        vec2 text_uv = (point - draw_data.text.xy) /
                       (draw_data.text.zw - draw_data.text.xy);
        // The mask holds a whole panel's worth of dots with the text somewhere in
        // it, so the text is read from the part it occupies. Sampling the mask's
        // full extent here would squeeze the text into a corner of the bubble.
        vec2 mask_uv = mix(draw_data.ink_uv.xy, draw_data.ink_uv.zw, text_uv);
        ink = texture(dotMask, mask_uv).r;
    }

    // Every dot is a cell, lit or not. The grid is measured from the text
    // region's origin, so the text lands inside cells rather than across them,
    // and it extends outward so the padding around the text is cells too. Without
    // this the paper is a flat field: the text reads as ink on a page rather than
    // as a matrix of elements, and no boundary is visible anywhere.
    vec2 cell = fract((point - draw_data.text.xy) /
                      max(draw_data.grid.xy, vec2(1.0e-5)));
    float bezel_width = clamp(draw_data.grid.z, 0.0, 0.45);
    vec2 in_cell =
        step(vec2(bezel_width), cell) * step(cell, vec2(1.0 - bezel_width));
    float bezel = in_cell.x * in_cell.y;

    // Between cells the bezel colour shows; inside a cell, the paper or the ink.
    vec3 unlit = mix(draw_data.bezel_color.rgb, draw_data.fill_color.rgb, bezel);
    vec3 lit_dot = mix(draw_data.bezel_color.rgb, draw_data.dot_color.rgb, bezel);
    vec3 inside_color = mix(unlit, lit_dot, ink);

    // The outline band is drawn over the grid rather than under it, because a
    // bubble's edge is a drawn line and not a row of cells.
    vec3 color = mix(inside_color, draw_data.outline_color.rgb, outline);

    outColor = vec4(color, coverage);
}
