#version 450

// Deferred G-buffer fragment stage. Outputs a 4-color MRT + depth:
//   loc0 albedo   RGBA8_UNORM linear   base_color.rgb * sRGB-sampled albedo
//   loc1 normal   RGBA16F  raw world-space [-1,1] (no *2-1 encode needed)
//   loc2 material RGBA8_UNORM linear   metallic R / roughness G / occlusion B
//   loc3 selection R8_UNORM            selected object mask
// The constants block is the StandardPbr ABI shared with
// material_asset_resolver.cpp: base_color@0, metallic@16, roughness@20,
// occlusion@24, emissive@32, texture_channels@48, normal_scale@64,
// alpha_cutoff@68. Textures wins over scalars in the resolver, so
// here scalars are always multiplied (identity default when no texture).

layout(binding = 2) uniform sampler2D base_color_texture;
layout(binding = 3) uniform KpMaterialData
{
    vec4 base_color;
    float metallic;
    float roughness;
    float occlusion;
    vec4 emissive;
    vec4 texture_channels;
    float normal_scale;
    float alpha_cutoff;
} material_data;
layout(binding = 5) uniform sampler2D normal_texture;
layout(binding = 6) uniform sampler2D metallic_texture;
layout(binding = 7) uniform sampler2D roughness_texture;
layout(binding = 8) uniform sampler2D occlusion_texture;
layout(binding = 9) uniform SelectionData
{
    vec4 selected;
} selection_data;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 1) in vec3 frag_T;
layout(location = 2) in vec3 frag_B;
layout(location = 3) in vec3 frag_N;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;
layout(location = 3) out float out_selection;

void main()
{
    // Hardware sRGB decode linearizes the base-color fetch.
    vec4 base_sample = texture(base_color_texture, frag_texcoord);
    if (material_data.alpha_cutoff > 0.0 && base_sample.a < material_data.alpha_cutoff)
    {
        discard;
    }
    vec3 albedo = material_data.base_color.rgb * base_sample.rgb;

    // Tangent-degenerate guard: data::Vertex zero-fills tangents for meshes
    // without UVs, so a zero-length T/B falls back to the geometric normal.
    vec3 tangent_normal = texture(normal_texture, frag_texcoord).rgb * 2.0 - 1.0;
    tangent_normal.xy *= material_data.normal_scale;
    tangent_normal = normalize(tangent_normal);
    vec3 normal = frag_N;
    if (dot(normal, normal) > 1e-8)
    {
        normal = normalize(normal);
    }
    else
    {
        normal = vec3(0.0, 0.0, 1.0);
    }
    if (dot(frag_T, frag_T) > 1e-8 && dot(frag_B, frag_B) > 1e-8)
    {
        vec3 tangent = frag_T - normal * dot(normal, frag_T);
        if (dot(tangent, tangent) > 1e-8)
        {
            tangent = normalize(tangent);
            vec3 bitangent = normalize(cross(normal, tangent));
            if (dot(bitangent, frag_B) < 0.0)
            {
                bitangent = -bitangent;
            }
            normal = normalize(mat3(tangent, bitangent, normal) * tangent_normal);
        }
    }

    vec4 metallic_sample = texture(metallic_texture, frag_texcoord);
    vec4 roughness_sample = texture(roughness_texture, frag_texcoord);
    vec4 occlusion_sample = texture(occlusion_texture, frag_texcoord);
    float metallic = material_data.metallic *
                     (material_data.texture_channels.x < 0.5 ? metallic_sample.r :
                      material_data.texture_channels.x < 1.5 ? metallic_sample.g :
                      material_data.texture_channels.x < 2.5 ? metallic_sample.b : metallic_sample.a);
    float roughness = material_data.roughness *
                      (material_data.texture_channels.y < 0.5 ? roughness_sample.r :
                       material_data.texture_channels.y < 1.5 ? roughness_sample.g :
                       material_data.texture_channels.y < 2.5 ? roughness_sample.b : roughness_sample.a);
    float occlusion = material_data.occlusion *
                      (material_data.texture_channels.z < 0.5 ? occlusion_sample.r :
                       material_data.texture_channels.z < 1.5 ? occlusion_sample.g :
                       material_data.texture_channels.z < 2.5 ? occlusion_sample.b : occlusion_sample.a);

    out_albedo = vec4(albedo, base_sample.a * material_data.base_color.a);
    out_normal = vec4(normal, 1.0);
    out_material = vec4(metallic, roughness, occlusion, 1.0);
    out_selection = selection_data.selected.x;
}
