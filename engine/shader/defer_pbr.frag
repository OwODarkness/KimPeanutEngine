#version 450

#define IN_LOCATION(n) layout(location = n) in
#define OUT_LOCATION(n) layout(location = n) out

IN_LOCATION(0) vec2 in_uv;
OUT_LOCATION(0) vec4 out_color;

struct LightData{
    int type;
    int shadow_map_index;
    vec3 color;
    float intensity;
    vec3 position;
    vec3 direction;
    float padding;
    float radius;
    float inner_cutoff;
    float outer_cutoff;
};


struct Material{
    vec3 albedo;
    float roughness;
    float metallic;
    float ao;
};

layout(std140, binding=1) buffer LightBuffer{
    LightData lights[];
};

const float PI = 3.14159265;
const float constant = 1.0;    
const float linear = 0.09;     
const float quadratic = 0.032;
const float MAX_REFLECTION_LOD = 4.0;

uniform int light_num;

uniform vec3 view_position;
uniform mat4 directional_light_space_matrix;
uniform mat4 spot_light_space_matrix[4];

uniform float near_plane;
uniform float far_plane;
uniform float skylight_intensity = 1.0;
uniform sampler2D directional_shadow_map;
uniform samplerCube point_shadow_map[4];
uniform sampler2D spot_shadow_map[4];


uniform samplerCube irradiance_map;
uniform samplerCube prefilter_map;
uniform sampler2D brdf_map;
uniform sampler2D g_position;
uniform sampler2D g_normal;
uniform sampler2D g_albedo;
uniform sampler2D g_material;



float CalculateDirectionalShadowValue(vec4 light_space_pos, vec3 normal_vec, vec3 light_vec)
{
    vec3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords = proj_coords * 0.5 + 0.5;

        if(proj_coords.x < 0.0 || proj_coords.x > 1.0 || 
        proj_coords.y < 0.0 || proj_coords.y > 1.0 ||
        proj_coords.z > 1.0)
        {
            return 0.0; // Outside shadow map or too far → no shadow
        }

    float current_depth = proj_coords.z;
    float bias = max(0.5 * (1.0 - dot(normal_vec, light_vec)),  0.005);
    float shadow = 0.0;

    vec2 texture_size = 1.0 / textureSize(directional_shadow_map, 0);
    for(int x = -1;x<=1;x++)
    {
        for(int y = -1;y<=1;y++)
        {
            float pcf = texture(directional_shadow_map, proj_coords.xy + vec2(x, y) * texture_size).r;
            shadow += current_depth - bias > pcf ? 1.0: 0.0;
        }
    }
    shadow /= 9.0;
   return shadow;
}



float CalculatePointShadowValue(vec3 frag_position, vec3 light_pos, int index)
{
    vec3 frag_to_light = frag_position - light_pos;
    float current_depth = length(frag_to_light);
    float shadow = 0.0;
    float bias = 0.05; 
    float samples = 4.0;
    float offset = 0.1;

    for(float x = -offset ; x < offset ; x += offset / (samples * 0.5))
    {
        for(float y = -offset ; y < offset ; y += offset / (samples * 0.5))
        {
            for(float z = -offset; z < offset; z += offset / (samples * 0.5))
            {
                float close_depth = texture(point_shadow_map[index], frag_to_light + vec3(x, y, z)).r;
                close_depth *= far_plane;
                if(current_depth - bias > close_depth)
                {
                    shadow += 1.0;
                }
            }
        }
    }
    shadow /= (samples * samples * samples);
    return shadow;
}

float CalculateSpotShadowValue(vec4 light_space_pos, vec3 normal_vec, vec3 light_vec, int index)
{
    vec3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords = proj_coords * 0.5 + 0.5;

    if(proj_coords.x < 0.0 || proj_coords.x > 1.0 || 
    proj_coords.y < 0.0 || proj_coords.y > 1.0 ||
    proj_coords.z > 1.0)
    {
        return 0.0; 
    }

    float current_depth = proj_coords.z;
    float max_depth = texture(spot_shadow_map[index], proj_coords.xy).r;
    float ndotl = max(dot(normal_vec, light_vec), 0.0);

// Blend constant and slope bias
    float bias = max(0.0005, 0.005 * (1.0 - ndotl));
    bias = min(bias, 0.01);
    float shadow = 0.0;

    vec2 texture_size = 1.0 / textureSize(spot_shadow_map[index], 0);
    for(int x = -1;x<=1;x++)
    {
        for(int y = -1;y<=1;y++)
        {
            float pcf = texture(spot_shadow_map[index], proj_coords.xy + vec2(x, y) * texture_size).r;
            shadow += current_depth - bias > pcf ? 1.0: 0.0;
        }
    }
    shadow /= 9.0;
   return shadow;
}

vec3 CalculateHalfVector(vec3 a, vec3 b)
{
    vec3 tmp = a + b;
    return normalize(tmp);
}

float CalculateAttenuation(vec3 light_pos, vec3 frag_pos)
{
    float dist = length(light_pos - frag_pos);
    return  1.0 / (constant + linear * dist + quadratic * (dist * dist));
}

vec3 Lambert(vec3 albedo)
{
    return albedo / PI;
}

float DistributionGGX(vec3 normal_vec, vec3 half_vec, float roughness)
{
    float alpha = roughness * roughness;
    float alpha_square = alpha * alpha;
    float dot_nh =  max(dot(normal_vec, half_vec), 0.0);
    float denom = (dot_nh * dot_nh) * (alpha_square - 1.0) + 1.0;
    return alpha_square / (PI * denom * denom);
}

float GeometrySchlickGGX(vec3 normal_vec, vec3 vec, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    float dot_nv = max(dot(normal_vec, vec), 0.0);
    float nom   = dot_nv;
    float denom = dot_nv * (1.0 - k) + k;
    return nom / denom;
}
  
float GeometrySmith(vec3 normal_vec, vec3 view_vec, vec3 light_vec, float roughness)
{
    float ggx1 = GeometrySchlickGGX(normal_vec, view_vec, roughness);
    float ggx2 = GeometrySchlickGGX(normal_vec, light_vec, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 IndirectPBR(vec3 N, vec3 V, Material material)
{
    vec3 albedo = material.albedo;
    float roughness = material.roughness;
    float metallic = material.metallic;
    float ao = material.ao;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    

    // Specular Fresnel for IBL
    float NdotV = max(dot(normalize(N), normalize(V)), 0.0);
    vec3 ks = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kd = (1.0 - ks) * (1.0 - metallic);

    // Diffuse IBL
    vec3 irradiance = texture(irradiance_map, normalize(N)).rgb;
    vec3 diffuse = irradiance * albedo / PI;

    // Specular IBL
    vec3 R = reflect(-normalize(V), normalize(N));
    vec3 prefilteredColor = textureLod(prefilter_map, R, roughness * float(MAX_REFLECTION_LOD)).rgb;
    vec2 envBRDF = texture(brdf_map, vec2(NdotV, roughness)).rg;
    vec3 specular = prefilteredColor * (ks * envBRDF.x + envBRDF.y);
    return kd * diffuse +  specular;
}

// Direct PBR (per-light)
vec3 DirectPBR(vec3 N, vec3 V, vec3 L, vec3 radiance, Material material, float shadow)
{
    vec3 albedo = material.albedo;
    float roughness = material.roughness;
    float metallic = material.metallic;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 H = normalize(L + V);

    // Fresnel
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    // NDF and Geometry
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    // Cook-Torrance specular
    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = (NDF * G * F) / denom;

    // Diffuse
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = Lambert(albedo);

    // Final direct light
    vec3 Lo = (kd * diffuse + specular) * radiance * max(dot(N, L), 0.0);
    return Lo * (1.0 - shadow);
}
vec3 CalculateDirectionalLight(LightData light, vec3 frag_position, vec3 normal_vec, Material material)
{
    vec3 view_vec = normalize(view_position - frag_position);
    vec3 light_vec = normalize(-light.direction);
    vec4 light_space_pos = directional_light_space_matrix * vec4(frag_position, 1.0);
    float shadow = CalculateDirectionalShadowValue(light_space_pos, normal_vec, light_vec);
    vec3 radiance = light.intensity * light.color;
    vec3 color = DirectPBR( normal_vec, view_vec, light_vec, radiance, material, shadow);
    return color;
}
vec3 CalculatePointLight(LightData light, vec3 frag_position, vec3 normal_vec, Material material)
{
    vec3 view_vec = normalize(view_position - frag_position);
    vec3 light_vec = normalize(light.position - frag_position);

    float shadow = 0.0;
    if(light.shadow_map_index > 0)
    {
        int index = light.shadow_map_index - 1;
        shadow = CalculatePointShadowValue(frag_position, light.position, index);
    }
    //float attenuation = CalculateAttenuation(light.position, frag_position);
    float d = length(frag_position - light.position);
    float attenuation = clamp(1.0 - (d / light.radius)*(d / light.radius), 0.0, 1.0);
    vec3 radiance = light.intensity * attenuation * light.color;
    vec3 color = DirectPBR(normal_vec, view_vec, light_vec, radiance, material, shadow);
    return color;
}
vec3 CalculateSpotLight(LightData light, vec3 frag_position, vec3 normal_vec, Material material)
{
    vec3 view_vec = normalize(view_position - frag_position);
    vec3 light_vec = normalize(light.position - frag_position);
    float shadow = 0.0;
    if(light.shadow_map_index >0 )
    {
        int index = light.shadow_map_index  - 1;
        vec4 light_space_pos = spot_light_space_matrix[index] * vec4(frag_position, 1.0);
        shadow = CalculateSpotShadowValue(light_space_pos, normal_vec, light_vec, index);
    }
    
    float theta = dot(-light_vec, normalize(light.direction));
    float epsilon   = cos(light.inner_cutoff) - cos(light.outer_cutoff);
    float intensity = clamp((theta - cos(light.outer_cutoff)) / epsilon, 0.0, 1.0);  
    float attenuation = CalculateAttenuation(light.position, frag_position);
    vec3 radiance = light.intensity * intensity * light.color * attenuation;
    vec3 color = DirectPBR(normal_vec, view_vec, light_vec, radiance, material, shadow);
    return color;
}

vec3 CalculateLight(LightData light, vec3 frag_position, vec3 normal_vec, Material material)
{
    vec3 color;
    if(light.type == 1)
    {
        color = CalculateDirectionalLight(light, frag_position,normal_vec, material);
    }
    else if(light.type == 2)
    {
        color = CalculatePointLight(light, frag_position,  normal_vec, material);
    }
    else if(light.type == 3)
    {
        color = CalculateSpotLight(light, frag_position, normal_vec, material);
    }
    return color;
}

float linearize_depth(float z_ndc, float near, float far)
{
    return (near * far) / (far - z_ndc * (far - near));
}



void main()
{
    vec2 uv = in_uv;
    vec3 frag_position = texture(g_position, uv).rgb;
    vec3 normal_vec =normalize(texture(g_normal, uv).rgb * 2.0 - 1.0);
    vec3 view_vec = normalize(view_position - frag_position);

    Material material;
    vec3 material_rma = texture(g_material, uv).rgb;
    material.albedo = texture(g_albedo, uv).rgb;
    material.roughness = material_rma.r; 
    material.metallic = material_rma.g;  
    material.ao = material_rma.b;        

    vec3 indirect_color = IndirectPBR(normal_vec, view_vec, material);
    vec3 direct_color;
    for(int i = 0; i<light_num;i++)
    {
        direct_color += CalculateLight(lights[i], frag_position, normal_vec, material);
    }
    vec3 color = direct_color +  indirect_color ;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    out_color = vec4(color, 1.0);


}
