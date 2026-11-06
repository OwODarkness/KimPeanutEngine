#version 450

layout(binding = 0) uniform sampler2D scene_hdr;
layout(binding = 1) uniform sampler2D gbuffer_depth;
layout(binding = 2, std140) uniform HeightFogData {
    mat4 inverse_view_projection;
    vec4 camera_and_orthographic;
    vec4 forward_and_affect_sky;
    vec4 density_height_falloff_distance;
    vec4 color;
} fog;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_color;

vec3 WorldAtDepth(vec2 ndc_xy, float depth)
{
    vec4 world = fog.inverse_view_projection * vec4(ndc_xy, depth * 2.0 - 1.0, 1.0);
    return world.xyz * sign(world.w) / max(abs(world.w), 1e-7);
}

float Transmittance(float start_y, float height_change, float distance)
{
    const vec4 p = fog.density_height_falloff_distance;
    if (distance <= 0.0 || p.x <= 0.0)
        return 1.0;
    // Anchor the integral at the denser end to avoid a positive exponential.
    float q = p.z * abs(height_change);
    float factor = q < 0.01 ? 1.0 - q * (0.5 - q / 6.0) : (1.0 - exp(-q)) / q;
    float log_depth = log(p.x) + log(distance) - p.z * (start_y - p.y + min(height_change, 0.0)) + log(factor);
    return exp(-exp(clamp(log_depth, -80.0, 4.3820266)));
}

void main()
{
    // The HDR input and output share framebuffer coordinates on both APIs.
    vec3 surface = texelFetch(scene_hdr, ivec2(gl_FragCoord.xy), 0).rgb;
    float depth = texture(gbuffer_depth, frag_texcoord).r;
    bool sky = depth >= 0.999999;
    if (sky && fog.forward_and_affect_sky.w < 0.5)
    {
        out_color = vec4(surface, 1.0);
        return;
    }
    vec2 ndc = frag_texcoord * 2.0 - 1.0;
#if KP_GRAPHICS_API_VULKAN
    ndc.y = -ndc.y;
#endif
    vec3 origin = fog.camera_and_orthographic.xyz;
    vec3 forward = fog.forward_and_affect_sky.xyz;
    if (fog.camera_and_orthographic.w > 0.5)
    {
        vec3 near_point = WorldAtDepth(ndc, 0.0);
        origin = near_point - forward * dot(near_point - origin, forward);
    }
    vec3 offset = WorldAtDepth(ndc, sky ? 0.5 : depth) - origin;
    float length_to_surface = length(offset);
    vec3 direction = length_to_surface > 1e-6 ? offset / length_to_surface : forward;
    float max_distance = fog.density_height_falloff_distance.w;
    float distance = sky ? max_distance : min(length_to_surface, max_distance);
    float t = Transmittance(origin.y, distance * direction.y, distance);
    out_color = vec4(mix(fog.color.rgb, surface, t), t);
}
