#version 450

// Debug composite of the G-buffer into linear SceneHdr. The output is four panels:
// albedo, world normal, and metallic/roughness/occlusion. This makes every
// D3 attachment inspectable before DeferredLightingPass replaces this producer.

layout(binding = 2) uniform sampler2D albedo_texture;
layout(binding = 3) uniform sampler2D normal_texture;
layout(binding = 4) uniform sampler2D material_texture;
layout(binding = 5) uniform sampler2D directional_shadow_depth;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 albedo = texture(albedo_texture, frag_texcoord).rgb;
    vec3 normal = texture(normal_texture, frag_texcoord).rgb * 0.5 + 0.5;
    vec3 material = texture(material_texture, frag_texcoord).rgb;
    float shadow_depth = texture(directional_shadow_depth, frag_texcoord).r;

    float panel = floor(clamp(frag_texcoord.x, 0.0, 0.999999) * 4.0);
    vec3 debug_color = panel < 1.0 ? albedo :
                       (panel < 2.0 ? normal :
                       (panel < 3.0 ? material : vec3(shadow_depth)));
    out_color = vec4(debug_color, 1.0);
}
