#version 450

layout(binding = 2) uniform sampler2D scene_hdr;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 hdr = max(texture(scene_hdr, frag_texcoord).rgb, vec3(0.0));
    // Global Reinhard maps linear HDR into display-linear [0, 1]. The sRGB
    // SceneColor attachment performs the final transfer-function encoding.
    vec3 mapped = hdr / (vec3(1.0) + hdr);
    out_color = vec4(mapped, 1.0);
}
