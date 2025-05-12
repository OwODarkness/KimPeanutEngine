#version 460 core


struct Material{

    bool has_albedo_map;
    bool has_roughness_map;
    bool has_metallic_map;
    bool has_normal_map;
    bool has_ao_map;
    float metallic;
    float roughness;
    vec3 ao;
    vec3 albedo;

    sampler2D albedo_map;
    sampler2D roughness_map;
    sampler2D metallic_map;
    sampler2D normal_map;
    sampler2D ao_map;
};

struct PointLight{
    vec3 position;
    vec3 color;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

};

struct DirectionalLight{
    vec3 direction;
    vec3 color;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct SpotLight{
    vec3 position;
    vec3 direction;

    vec3 color;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float cutoff;
    float outer_cutoff;
};

layout(std140, binding=1) uniform Light{
    PointLight point_light;
    DirectionalLight directional_light;
    SpotLight spot_light;
};

uniform Material material;
uniform vec3 view_position;

in vec2 texcoord;
in vec3 frag_position;
in vec3 normal;

out vec4 out_frag_color;

float PI = 3.14159265;
float constant = 1.0;    
float linear = 0.09;     
float quadratic = 0.032;

vec3 CalculateHalfVector(vec3 a, vec3 b)
{
    vec3 tmp = a + b;
    return tmp / length(tmp);
}
float CalculateAttenuation(vec3 light_pos, vec3 frag_pos)
{
    float dist = length(light_pos - frag_pos);
    return  1.0 / (constant + linear * dist + 
                quadratic * (dist * dist));
}
vec3 Lambert(vec3 albedo)
{
    return albedo / PI;
}

float DistributionGGX(vec3 normal_vec, vec3 half_vec, float roughness)
{
    float alpha = roughness * roughness;
    float alpha_square = alpha * alpha;
    float dot_nh =  max(dot(normal_vec, half_vec), 0.) ;
    float denom = (dot_nh * dot_nh) * (alpha_square - 1.) + 1.;
    return alpha_square / (PI  * denom * denom);
}

float GeometrySchlickGGX(vec3 normal_vec, vec3 vec, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    float dot_nv = max(dot(normal_vec, vec), 0);
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

vec3 FresnelSchlick(vec3 f0, vec3 half_vec, vec3 view_vec)
{
    return f0 + (vec3(1.0) - f0) * pow(1.0 - max(dot(half_vec, view_vec), 0), 5);
}

vec3 BRDF(vec3 pos, vec2 dir_i, vec2 dir_o)
{
    //fr = k_d * f_lambert + ks * specular
    return vec3(0.);
}

void main()
{
    vec3 normal_vec = material.has_normal_map ? texture(material.normal_map, texcoord).rgb : normalize(normal);
    vec3 view_vec = normalize(view_position - frag_position);
    vec3 L0 = vec3(0.);
    vec3 light_vec = normalize(point_light.position - frag_position);
    vec3 half_vec = CalculateHalfVector(light_vec, view_vec);
    vec3 ao = material.has_ao_map ? texture(material.ao_map, texcoord).rgb : material.ao;

    float attenuation = CalculateAttenuation(point_light.position, frag_position);
    vec3 radiance = attenuation * point_light.color;

    vec3 albedo = material.has_albedo_map ? pow(texture(material.albedo_map, texcoord).rgb, vec3(2.2)) : material.albedo;
    
    float roughness = material.has_roughness_map ? texture(material.roughness_map, texcoord).r : material.roughness;
    float metallic = material.has_metallic_map ? texture(material.metallic_map, texcoord).r : material.metallic;

    vec3 f0 = vec3(0.04);
    f0 = mix(f0, albedo, metallic);
    vec3 F = FresnelSchlick(f0, half_vec, view_vec);
    float NDF = DistributionGGX(normal_vec, half_vec, roughness);
    float G = GeometrySmith(normal_vec, view_vec, light_vec, roughness);

    vec3 numerator = NDF * G * F;//DFG
    float denom = 4.0 * max(dot(normal_vec, view_vec), 0.) * max(dot(normal_vec, light_vec), 0.) + 0.0001;
    vec3 specular = numerator / denom;

    vec3 ks = F;
    vec3 kd = vec3(1.0) - ks;
    kd *= (1.0 - metallic);

    L0 = (kd * Lambert(albedo) + specular) * radiance * max(dot(normal_vec, light_vec), 0.0);

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + L0;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    out_frag_color = vec4(color, 1.0);
}