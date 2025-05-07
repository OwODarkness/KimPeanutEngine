#version 460 core


struct Material{
    int diffuse_count;
    int specular_count;
    float metallic;
    float roughness;
    float shininess;
    float ao;
    vec3 albedo;

    sampler2D diffuse_texture_0;
    sampler2D diffuse_texture_1;
    sampler2D diffuse_texture_2;

    sampler2D specular_texture_0;
    sampler2D specular_texture_1;
    sampler2D specular_texture_2;

    sampler2D normal_texture;

    sampler2D emission_texture;
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
vec3 Lambert()
{
    vec3 res_color = texture(material.diffuse_texture_0, texcoord).rgb;
    return res_color / PI;
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

vec3 FresnelSchlick(vec3 F0, vec3 half_vec, vec3 view_vec)
{
    return F0 + (vec3(1.0) - F0) * (1.0 - pow(max(dot(half_vec, view_vec), 0), 5));
}

vec3 BRDF(vec3 pos, vec2 dir_i, vec2 dir_o)
{
    //fr = k_d * f_lambert + ks * specular
    return vec3(0.);
}

void main()
{
    vec3 normal_vec = normalize(normal);
    vec3 view_vec = normalize(view_position - frag_position);
    vec3 L0 = vec3(0.);
        vec3 light_vec = normalize(point_light.position - frag_position);
        vec3 half_vec = CalculateHalfVector(light_vec, view_vec);

        float attenuation = CalculateAttenuation(point_light.position, frag_position);
        vec3 radiance = attenuation * point_light.color;

        vec3 F0 = vec3(0.04);
        F0 = mix(F0, material.albedo, material.metallic);
        vec3 F = FresnelSchlick(F0, half_vec, view_vec);
        float NDF = DistributionGGX(normal_vec, half_vec, material.roughness);
        float G = GeometrySmith(normal_vec, view_vec, light_vec, material.roughness);

        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(normal_vec, view_vec), 0.) * max(dot(normal_vec, light_vec), 0.) + 0.0001;
        vec3 specular = numerator / denom;

        vec3 ks = F;
        vec3 kd = vec3(1.0) - ks;
        kd *= (1.0 - material.metallic);

        L0 = (kd * material.albedo / PI + specular) * radiance * max(dot(normal_vec, light_vec), 0.0);

    vec3 ambient = vec3(0.03) * material.albedo * material.ao;
    vec3 color = ambient + L0;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    out_frag_color = vec4(color, 1.0);
}