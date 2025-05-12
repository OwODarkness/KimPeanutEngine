//Blinn-Phong fragment shader

#version 460 core

layout(location = 0) out vec4 out_frag_color;

struct Material{
    sampler2D diffuse_map;
    sampler2D specular_map;
    sampler2D normal_map;

    vec3 albedo;

    float shininess;
    bool has_diffuse_map;
    bool has_specular_map;
    bool has_normal_map;
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

in vec3 normal;
in vec2 texcoord;
in vec3 frag_position;
in vec4 frag_pos_light_space;
in mat3 TBN;


float constant = 1.0;    
float linear = 0.09;     
float quadratic = 0.032;

vec3 ambient = vec3(0.1);
uniform Material material;
uniform vec3 view_position;

//shadow map
uniform sampler2D shadow_map;
uniform samplerCube point_shadow_map;

uniform float far_plane;
uniform bool normal_texture_enabled;

float CalculateAttenuation(vec3 light_pos, vec3 frag_pos)
{
    float dist = length(light_pos - frag_pos);
    return  1.0 / (constant + linear * dist + 
                quadratic * (dist * dist));
}

float CalculateShadowValue(vec4 light_space, vec3 normal_vec)
{
    vec3 proj_coords = light_space.xyz / light_space.w;
    proj_coords = proj_coords * 0.5 + 0.5;
    if(proj_coords.z > 1.f)
    {
        return 0.f;
    }
    float current_depth = proj_coords.z;


    vec3 lightdir = normalize(directional_light.direction);

    float bias = max(0.5 * dot(normal_vec, lightdir),  0.005f);
    float shadow = 0.f;

    vec2 texture_size = 1.f / textureSize(shadow_map, 0);
    for(int x = -1;x<=1;x++)
    {
        for(int y = -1;y<=1;y++)
        {
            float pcf = texture(shadow_map, proj_coords.xy + vec2(x, y) * texture_size).r;
            shadow += current_depth - bias > pcf ? 1: 0;
        }
    }
    shadow/= 9.f;

    return shadow;
}

float CalculatePointShadowValue(vec3 frag_position)
{
    vec3 frag_to_light = frag_position - point_light.position;
    float current_depth = length(frag_to_light);
    float shadow = 0.f;
    float bias = 0.05; 
    float samples = 4.0f;
    float offset = 0.1f;

    for(float x = -offset ; x< offset ;x += offset / (samples * 0.5))
    {
        for(float y = -offset ; y<offset;y+=offset/(samples * 0.5))
        {
            for(float z = -offset;z<offset;z+= offset/(samples * 0.5))
            {
                float close_depth = texture(point_shadow_map, frag_to_light + vec3(x, y, z)).r;
                close_depth *= far_plane;
                if(current_depth - bias > close_depth)
                {
                    shadow += 1.f;
                }
            }
        }
    }
    shadow /= (samples * samples * samples);
    return shadow;
}


vec3 CalculatePointRender(PointLight light, vec3 normal_vec, vec3 diffuse, vec3 specular)
{
    vec3 light_direction = normalize(light.position - frag_position);
    vec3 light_color = light.color;


    vec3 ambient_render = light.ambient * material.albedo * diffuse * light_color;

    float diff = max(dot(normal_vec, light_direction), 0);
    vec3 diffuse_render = light.diffuse * material.albedo * diff  * diffuse * light_color;

    vec3 view_direction = normalize(view_position - frag_position);

    vec3 halfway_direction = normalize(view_direction + light_direction);

    float spec = pow(max(dot(normal_vec, halfway_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * specular * light_color;

    float attenuation = CalculateAttenuation(light.position, frag_position);
    return attenuation * vec3(ambient_render+ diffuse_render + specular_render);   
}

vec3 CalculateDirectionalLightRender(DirectionalLight light, vec3 normal_vec, vec3 diffuse, vec3 specular)
{
    vec3 light_direction = normalize(-light.direction);
    vec3 light_color = light.color;

    vec3 ambient_render = light.ambient * material.albedo * diffuse * light_color ;

    float diff = max(dot(normal_vec, light_direction), 0);
    vec3 diffuse_render = light.diffuse * material.albedo * diff * diffuse * light_color;

    vec3 view_direction = normalize(view_position - frag_position);
    vec3 halfway_direction = normalize(view_direction + light_direction);

    float spec = pow(max(dot(normal_vec, halfway_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * specular * light_color ;
    return vec3(diffuse_render + specular_render);  
}

vec3 CalculateSpotLightRender(SpotLight light, vec3 normal_vec, vec3 diffuse, vec3 specular)
{
    vec3 light_direction = normalize(light.position - frag_position);
    float theta = dot(light_direction, -light.direction);
    float epsilon   = light.cutoff - light.outer_cutoff;
    float intensity = clamp((theta - light.outer_cutoff) / epsilon, 0.0, 1.0);

    vec3 light_color = light.color;

    vec3 ambient_render = light.ambient * material.albedo * diffuse * light_color ;

    float diff = max(dot(normal_vec, light_direction), 0);
    vec3 diffuse_render = light.diffuse * material.albedo * diff * diffuse * light_color;

    vec3 view_direction = normalize(view_position - frag_position);
    vec3 halfway_direction = normalize(view_direction + light_direction);

    float spec = pow(max(dot(normal_vec, halfway_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * specular * light_color ;
    
    float attenuation = CalculateAttenuation(light.position, frag_position);
    return  vec3( intensity * attenuation *diffuse_render + intensity * attenuation *specular_render);  

}

void main()
{
    vec3 normal_vec = material.has_normal_map == true ? 
    normalize(TBN * (texture(material.normal_map, texcoord).rgb * 2.0 - 1.0)): normalize(normal);

    float shadow = CalculateShadowValue(frag_pos_light_space, normal_vec);
    float point_shadow = CalculatePointShadowValue(frag_position);
    vec3 diffuse = material.has_diffuse_map == true ? texture(material.diffuse_map, texcoord).rgb : vec3(0., 0., 0.);
    vec3 specular = material.has_specular_map == true ? texture(material.specular_map, texcoord).rgb : vec3(0., 0., 0.);

    vec3 point_light_res = (1.f - point_shadow) * CalculatePointRender(point_light, normal_vec, diffuse, specular);
    vec3 directional_light_res = (1.f - shadow) * CalculateDirectionalLightRender(directional_light, normal_vec, diffuse, specular);
    vec3 spot_light_res = CalculateSpotLightRender(spot_light, normal_vec, diffuse, specular);
    vec3 light_res =  point_light_res + directional_light_res + spot_light_res;
    vec3 ambient_res = ambient * diffuse;
    const float gamma = 2.2f;
    out_frag_color = vec4(light_res + ambient_res , 1 );


    
}