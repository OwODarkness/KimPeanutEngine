#version 450

layout(binding = 2) uniform sampler2D scene_hdr;
layout(binding = 3) uniform sampler2D selection_mask;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 hdr = max(texture(scene_hdr, frag_texcoord).rgb, vec3(0.0));
    // Global Reinhard maps linear HDR into display-linear [0, 1]. The sRGB
    // SceneColor attachment performs the final transfer-function encoding.
    vec3 mapped = hdr / (vec3(1.0) + hdr);
    vec2 mask_uv = frag_texcoord;
#if KP_GRAPHICS_API_VULKAN
    // SceneHdr has already passed through the Vulkan fullscreen orientation
    // conversion. The selection mask is sampled directly from the G-buffer,
    // so it needs the same source-space correction as CaptureView.
    mask_uv.y = 1.0 - mask_uv.y;
#endif
    float selected = texture(selection_mask, mask_uv).r;
    vec2 texel_size = 1.0 / vec2(textureSize(selection_mask, 0));
    float neighbor_selected = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            neighbor_selected = max(
                neighbor_selected,
                texture(selection_mask,
                mask_uv + vec2(float(x), float(y)) * texel_size).r);
        }
    }
    float outline = clamp(neighbor_selected - selected, 0.0, 1.0);
    const vec3 highlight_color = vec3(1.0, 0.72, 0.08);
    mapped = mix(mapped, highlight_color, outline * 0.9);
    out_color = vec4(mapped, 1.0);
}
