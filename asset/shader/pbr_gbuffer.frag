#version 450

// Deferred G-buffer fragment stage. Outputs a 5-color MRT + depth:
//   loc0 albedo   RGBA8_UNORM linear   base_color.rgb * sRGB-sampled albedo
//   loc1 normal   RGBA16F  raw world-space [-1,1] (no *2-1 encode needed)
//   loc2 material RGBA8_UNORM linear   metallic R / roughness G / occlusion B
//   loc3 selection R8_UNORM            selected object mask
//   loc4 motion   RGBA16F               previous-minus-current UV, view depth, validity
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

layout(binding = 0) uniform PerPassData
{
    mat4 view;
    mat4 proj;
    mat4 previous_view;
    mat4 previous_proj;
    vec4 temporal_params;
    vec4 history_params;
} pass_data;
layout(binding = 1) uniform PerObjectData
{
    mat4 model;
    mat4 previous_submitted_model;
    vec4 temporal_state;
} object_data;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 1) in vec3 frag_T;
layout(location = 2) in vec3 frag_B;
layout(location = 3) in vec3 frag_N;
layout(location = 4) in vec3 frag_local_position;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;
layout(location = 3) out float out_selection;
layout(location = 4) out vec4 out_motion;

vec2 ClipToTopLeftUv(vec4 clip_position)
{
    return vec2(clip_position.x / clip_position.w * 0.5 + 0.5,
                0.5 - clip_position.y / clip_position.w * 0.5);
}

bool IsInsideClip(vec4 clip_position)
{
    if (clip_position.w <= 1e-6 || any(isnan(clip_position)) || any(isinf(clip_position)))
        return false;
#if KP_GRAPHICS_API_VULKAN
    return clip_position.z >= 0.0 && clip_position.z <= clip_position.w;
#else
    return clip_position.z >= -clip_position.w && clip_position.z <= clip_position.w;
#endif
}

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
    // Normal products use BC5_UNORM when the compressed profile is selected.
    // BC5 stores tangent-space X/Y in RG; reconstruct the positive-Z
    // hemisphere instead of reading the format's unused B channel. This is
    // also valid for the portable RGBA8 normal product and keeps both paths
    // on the same tangent-normal convention.
    vec2 tangent_xy = texture(normal_texture, frag_texcoord).rg * 2.0 - 1.0;
    tangent_xy *= material_data.normal_scale;
    vec3 tangent_normal = vec3(
        tangent_xy,
        sqrt(max(0.0, 1.0 - dot(tangent_xy, tangent_xy))));
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

    vec4 local_position = vec4(frag_local_position, 1.0);
    vec4 current_world = object_data.model * local_position;
    vec4 current_clip = pass_data.proj * pass_data.view * current_world;
    vec4 previous_world = object_data.previous_submitted_model * local_position;
    vec4 previous_clip = pass_data.previous_proj * pass_data.previous_view * previous_world;
#if KP_GRAPHICS_API_VULKAN
    current_clip.z = 0.5 * (current_clip.z + current_clip.w);
    previous_clip.z = 0.5 * (previous_clip.z + previous_clip.w);
#endif
    vec2 current_uv = ClipToTopLeftUv(current_clip) - pass_data.temporal_params.zw;
    vec2 previous_uv = ClipToTopLeftUv(previous_clip) - pass_data.history_params.yz;
    bool valid = pass_data.history_params.x > 0.5 &&
                 object_data.temporal_state.x > 0.5 &&
                 IsInsideClip(current_clip) && IsInsideClip(previous_clip) &&
                 all(greaterThanEqual(current_uv, vec2(0.0))) &&
                 all(lessThanEqual(current_uv, vec2(1.0))) &&
                 all(greaterThanEqual(previous_uv, vec2(0.0))) &&
                 all(lessThanEqual(previous_uv, vec2(1.0)));
    float view_depth = max(0.0, -(pass_data.view * current_world).z);
    out_motion = vec4(valid ? previous_uv - current_uv : vec2(0.0),
                      view_depth, valid ? 1.0 : 0.0);
}
