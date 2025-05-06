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

in vec2 texcoord;
in vec3 frag_position;

float PI = 3.14159265;

vec3 Lambert()
{
    vec3 res_color = texture(material.diffuse_texture_0, texcoord).rgb;
    return res_color / PI;
}

float DistributionGGX(vec3 normal_vec, vec3 half_vec, float alpha)
{
    float alpha_square = alpha * alpha;
    float dot_nh =  max(dot(normal_vec, half_vec), 0.) ;
    float denom = (dot_nh * dot_nh) * (alpha_square - 1.) + 1.;
    return alpha_square / (PI  * denom * denom);
}

vec3 BRDF(vec3 pos, vec2 dir_i, vec2 dir_o)
{
    //fr = k_d * f_lambert + 
    return vec3(0.);
}

void main()
{

}