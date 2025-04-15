#version 460 core

struct Material{
    sampler2D diffuse_texture_0;
    sampler2D diffuse_texture_1;

    sampler2D specular_texture_0;
    sampler2D specular_texture_1;

    sampler2D emission_texture;

    sampler2D normal_texture;
    float shininess;
    vec3 diffuse_albedo;
};

struct PointLight{
    vec3 position;
    vec3 color;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    float constant;
    float linear;
    float quadratic;
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

    float constant;
    float linear;
    float quadratic;

    float cutoff;
    float outer_cutoff;
};

layout(std140, binding=1) uniform Light{
    PointLight point_light;
    DirectionalLight directional_light;
    SpotLight spot_light;
};

in vec3 out_normal;
in vec2 out_texcoord;
in vec3 frag_position;
in vec4 frag_pos_light_space;
in mat3 out_TBN;
out vec4 out_frag_color;

// uniform PointLight point_light;
// uniform DirectionalLight directional_light;
// uniform SpotLight spot_light;
vec3 ambient = vec3(0.1);
uniform Material material;
uniform vec3 view_position;

//shadow map
uniform sampler2D shadow_map;
uniform samplerCube point_shadow_map;

uniform float far_plane;
uniform bool normal_texture_enabled;

float CalculateShadowValue(vec4 light_space)
{
    vec3 proj_coords = light_space.xyz / light_space.w;
    proj_coords = proj_coords * 0.5 + 0.5;
    if(proj_coords.z > 1.f)
    {
        return 0.f;
    }
    float current_depth = proj_coords.z;

    vec3 normal = out_normal;
    if(normal_texture_enabled)
    {
        normal = texture(material.normal_texture, out_texcoord).rgb;
        normal = normalize(normal * 2.f - 1.f);
    }
    else
    {
        normal = normalize(out_normal);
    }
    vec3 lightdir = normalize(directional_light.direction);

    float bias = max(0.5 * dot(normal, lightdir),  0.005f);
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


vec3 CalculatePointRender(PointLight light)
{
    vec3 light_direction = normalize(light.position - frag_position);
    vec3 light_color = light.color;

    vec3 normal = out_normal;
    if(normal_texture_enabled)
    {
        normal = texture(material.normal_texture, out_texcoord).rgb;
        normal = normalize(normal * 2.f - 1.f);
        normal = normalize(out_TBN * normal);
    }
    else
    {
        normal = normalize(out_normal);
    }

    vec3 ambient_render = light.ambient * material.diffuse_albedo * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    float diff = max(dot(normal, light_direction), 0);
    vec3 diffuse_render = light.diffuse * material.diffuse_albedo * diff  * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    vec3 view_direction = normalize(view_position - frag_position);

    vec3 halfway_direction = normalize(view_direction + light_direction);

    float spec = pow(max(dot(normal, halfway_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * vec3(texture(material.specular_texture_0, out_texcoord)) * light_color ;

    float distance    = length(light.position - frag_position);
    float attenuation = 1.0 / (light.constant + light.linear * distance + 
                light.quadratic * (distance * distance));
    return attenuation * vec3(ambient_render+ diffuse_render + specular_render);   
}

vec3 CalculateDirectionalLightRender(DirectionalLight light)
{
    vec3 light_direction = normalize(-light.direction);
    vec3 light_color = light.color;
    vec3 normal = out_normal;
    if(normal_texture_enabled)
    {
        normal = texture(material.normal_texture, out_texcoord).rgb;
        normal = normalize(normal * 2.f - 1.f);
        normal = normalize(out_TBN * normal);
    }
    else
    {
        normal = normalize(out_normal);
    }

    vec3 ambient_render = light.ambient * material.diffuse_albedo * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color ;

    float diff = max(dot(normal, light_direction), 0);
    vec3 diffuse_render = light.diffuse * material.diffuse_albedo * diff * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    vec3 view_direction = normalize(view_position - frag_position);
    vec3 halfway_direction = normalize(view_direction + light_direction);

    float spec = pow(max(dot(normal, halfway_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * vec3(texture(material.specular_texture_0, out_texcoord)) * light_color ;
    return vec3(diffuse_render + specular_render);  
}

vec3 CalculateSpotLightRender(SpotLight light)
{
    vec3 light_direction = normalize(light.position - frag_position);
    float theta = dot(light_direction, -light.direction);
    float epsilon   = light.cutoff - light.outer_cutoff;
    float intensity = clamp((theta - light.outer_cutoff) / epsilon, 0.0, 1.0);

    vec3 light_color = light.color;
    vec3 normal = normal_texture_enabled ? texture(material.normal_texture, out_texcoord).rgb : normalize(out_normal);

    vec3 ambient_render = light.ambient * material.diffuse_albedo * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color ;

    float diff = max(dot(normal, light_direction), 0);
    vec3 diffuse_render = light.diffuse * material.diffuse_albedo * diff * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    vec3 view_direction = normalize(view_position - frag_position);
    vec3 halfway_direction = normalize(view_direction + light_direction);

    float spec = pow(max(dot(normal, halfway_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * vec3(texture(material.specular_texture_0, out_texcoord)) * light_color ;
    
    float distance = length(light.position - frag_position);
    float attenuation = 1.0 / (light.constant + light.linear * distance + 
                light.quadratic * (distance * distance));
        
    return  vec3( intensity * attenuation *diffuse_render + intensity * attenuation *specular_render);  

}

void main()
{
    float shadow = CalculateShadowValue(frag_pos_light_space);
    float point_shadow = CalculatePointShadowValue(frag_position);

    vec3 point_light_res = (1.f - point_shadow) * CalculatePointRender(point_light);
    vec3 directional_light_res = (1.f - shadow) * CalculateDirectionalLightRender(directional_light);
    vec3 spot_light_res = CalculateSpotLightRender(spot_light);
    vec3 light_res =  point_light_res + directional_light_res + spot_light_res;
    vec3 ambient_res = ambient * vec3(texture(material.diffuse_texture_0, out_texcoord));
    const float gamma = 2.2f;
    out_frag_color = vec4(light_res + ambient_res , 1 );
    
    // if(normal_texture_enabled)
    // {
    //     vec3 normal = texture(material.normal_texture, out_texcoord).rgb;
    //     normal = normalize(normal * 2.f - 1.f);
    //     normal = normalize(out_TBN * normal);
    //     out_frag_color = vec4(normal, 1.f);
    // }

    // if(gl_FragCoord.x < 600)
    //     out_frag_color = vec4(ambient_res + diffuse_res + specular_res, 1 );
    // else
    // {
    //     //float dist = distance(frag_position, view_position);
    //     //vec3 deep_color = vec3( dist /5.f );
    //     //out_frag_color = vec4(deep_color, 1);
    //     out_frag_color = vec4(normal * 0.5 + 0.5, 1);

    // }
    
}