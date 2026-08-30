#version 450

// Deferred G-buffer fragment stage. Outputs a 3-color MRT + depth:
//   loc0 albedo   RGBA8_UNORM linear   base_color.rgb * sRGB-sampled albedo
//   loc1 normal   RGBA16F  raw world-space [-1,1] (no *2-1 encode needed)
//   loc2 material RGBA8_UNORM linear   metallic R / roughness G / occlusion B
// The constants block is the StandardPbr ABI shared with
// material_asset_resolver.cpp: base_color@0, metallic@16, roughness@20,
// occlusion@24, emissive@32. Textures wins over scalars in the resolver, so
// here scalars are always multiplied (identity default when no texture).

layout(binding = 2) uniform sampler2D base_color_texture;
layout(binding = 3) uniform KpMaterialData
{
    vec4 base_color;
    float metallic;
    float roughness;
    float occlusion;
    vec4 emissive;
} material_data;
layout(binding = 5) uniform sampler2D normal_texture;
layout(binding = 6) uniform sampler2D metallic_texture;
layout(binding = 7) uniform sampler2D roughness_texture;
layout(binding = 8) uniform sampler2D occlusion_texture;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 1) in vec3 frag_T;
layout(location = 2) in vec3 frag_B;
layout(location = 3) in vec3 frag_N;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;

void main()
{
    // Hardware sRGB decode linearizes the base-color fetch.
    vec3 albedo = material_data.base_color.rgb * texture(base_color_texture, frag_texcoord).rgb;

    // Tangent-degenerate guard: data::Vertex zero-fills tangents for meshes
    // without UVs, so a zero-length T/B falls back to the geometric normal.
    vec3 tangent_normal = texture(normal_texture, frag_texcoord).rgb * 2.0 - 1.0;
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

    float metallic = material_data.metallic * texture(metallic_texture, frag_texcoord).r;
    float roughness = material_data.roughness * texture(roughness_texture, frag_texcoord).r;
    float occlusion = material_data.occlusion * texture(occlusion_texture, frag_texcoord).r;

    out_albedo = vec4(albedo, 1.0);
    out_normal = vec4(normal, 1.0);
    out_material = vec4(metallic, roughness, occlusion, 1.0);
}
