#version 460 core


struct Material{
    sampler2D diffuse_texture_0;
    sampler2D diffuse_texture_1;
    sampler2D diffuse_texture_2;

    sampler2D specular_texture_0;
    sampler2D specular_texture_1;
    sampler2D specular_texture_2;

    sampler2D normal_texture;

    sampler2D emission_texture;

    float shininess;
    vec3 diffuse_albedo;

    
    int diffuse_count;
    int specular_count;
};

uniform Material material;
uniform vec3 view_position;
uniform vec3 light_positions[4];
uniform vec3 light_colors[4];
uniform vec3 albedo;
uniform float metallic;
uniform float roughtness;
uniform float ao;

in vec2 texcoord;
in vec3 frag_position;
in vec3 normal;

out vec4 out_frag_color;

float PI = 3.14159265;

vec3 CalculateHalfVector(vec3 a, vec3 b)
{
    vec3 tmp = a + b;
    return tmp / length(tmp);
}

vec3 Lambert()
{
    vec3 res_color = texture(material.diffuse_texture_0, texcoord).rgb;
    return res_color / PI;
}

float DistributionGGX(vec3 normal_vec, vec3 half_vec, float roughtness)
{
    float alpha = roughtness * roughtness;
    float alpha_square = alpha * alpha;
    float dot_nh =  max(dot(normal_vec, half_vec), 0.) ;
    float denom = (dot_nh * dot_nh) * (alpha_square - 1.) + 1.;
    return alpha_square / (PI  * denom * denom);
}

float GeometrySchlickGGX(vec3 normal_vec, vec3 vec, float roughtness)
{
    float r = roughtness + 1.0;
    float k = r * r / 8.0;
    float dot_nv = max(dot(normal_vec, vec), 0);
    float nom   = dot_nv;
    float denom = dot_nv * (1.0 - k) + k;
    return nom / denom;
}
  
float GeometrySmith(vec3 normal_vec, vec3 view_vec, vec3 light_vec, float roughtness)
{

    float ggx1 = GeometrySchlickGGX(normal_vec, view_vec, roughtness);
    float ggx2 = GeometrySchlickGGX(normal_vec, light_vec, roughtness);
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
    for(int i = 0;i<4;i++)
    {
        vec3 light_vec = normalize(light_positions[i] - frag_position);
        vec3 half_vec = CalculateHalfVector(light_vec, view_vec);

        float dist = length(light_positions[i] - frag_position);
        float attenuation = 1.0 / (dist * dist);
        vec3 radiance = attenuation * light_colors[i];

        vec3 F0 = vec3(0.04);
        F0 = mix(F0, albedo, metallic);
        vec3 F = FresnelSchlick(F0, half_vec, view_vec);
        float NDF = DistributionGGX(normal_vec, half_vec, roughtness);
        float G = GeometrySmith(normal_vec, view_vec, light_vec, roughtness);

        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(normal_vec, view_vec), 0.) * max(dot(normal_vec, light_vec), 0.) + 0.0001;
        vec3 specular = numerator / denom;

        vec3 ks = F;
        vec3 kd = vec3(1.0) - ks;
        kd *= (1.0 - metallic);

        L0 += (kd * albedo / PI + specular) * radiance * max(dot(normal_vec, light_vec), 0.0);

    }

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + L0;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    out_frag_color = vec4(color, 1.0);
}