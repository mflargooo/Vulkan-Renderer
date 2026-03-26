#version 450

#include "tonemap.glsl"
#include "lights.glsl"

layout(set=0, binding=0, std140) uniform World {
    vec3 SKY_DIRECTION;
    vec3 SKY_ENERGY; // energy supplied by sky to a surface patch with normal = SKY_DIRECTION
    vec3 SUN_DIRECTION;
    vec3 SUN_ENERGY; // energy supplied by sky to a surface patch with normal = SUN_DIRECTION
};

layout(set=2, binding=0, std140) uniform Camera {
    mat4 CLIP_FROM_WORLD;
};

layout(push_constant) uniform Constants {
    uint type;
    float exposure;
    int tonemap;
    int has_normal_map;
    int has_roughness_map;
    int has_metalness_map;
    uint mip_levels;
} pc;

layout(set=3, binding=0) uniform sampler2D TEXTURE;

layout(set=4, binding=0) uniform samplerCube IRRADIANCE;
layout(set=4, binding=1) uniform samplerCube PREFILTER_ENV;
layout(set=4, binding=2) uniform sampler2D BRDF_LUT;

layout(set=5, binding=0) uniform sampler2D NORMAL;
layout(set=6, binding=0) uniform sampler2D ROUGHNESS;
layout(set=7, binding=0) uniform sampler2D METALNESS;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 texCoord;
layout(location = 5) in vec4 clip_pos;

layout(location = 0) out vec4 outColor;

void main() {  
    vec2 uv = clip_pos.xy / clip_pos.w * 0.5 + 0.5;
    outColor = vec4(vec3(textureProj(SHADOWS[1], vec4(uv, clip_pos.z / clip_pos.w, 1.0))), 1.0);
    return;

    vec3 n = normal;
    vec3 ns = normal;
    if (pc.has_normal_map != 0) {
        ns = normalize(2.0 * texture(NORMAL, texCoord).rgb - 1.0);
        mat3 xf = mat3(tangent, bitangent, normal);

        n = normalize(xf * ns);
    }

    vec3 direct_diffuse, direct_specular;

    vec3 base_color = vec3(0.0);
    if (pc.type == 0) { // LAMBERTIAN
        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        forward_pass(position, n, vec3(0.0), 0.0, vec3(0.0), direct_diffuse, direct_specular);

        vec3 env_lighting = texture(IRRADIANCE, n).rgb;
        base_color = albedo * (direct_diffuse + env_lighting);
        
    } else if (pc.type == 1) { // MIRROR
        mat4 WORLD_FROM_CLIP = inverse(CLIP_FROM_WORLD);
        vec4 eye = WORLD_FROM_CLIP * vec4(0.0, 0.0, 0.0, 1.0);

        vec3 to_eye = normalize(eye.xyz / eye.w - position);

        vec3 r = reflect(-to_eye, n);
        vec3 env_lighting = textureLod(PREFILTER_ENV, r, 0.0).rgb;
        base_color = env_lighting;
        
    } else if (pc.type == 2) { // ENVIRONMENT
        base_color = textureLod(PREFILTER_ENV, n, 0.0).rgb;
    } else { // (pc.type == 3) // PBR
        mat4 WORLD_FROM_CLIP = inverse(CLIP_FROM_WORLD);
        vec4 eye = WORLD_FROM_CLIP * vec4(0.0, 0.0, 0.0, 1.0);

        vec3 to_eye = normalize(eye.xyz / eye.w - position);

        vec3 r = reflect(-to_eye, n);
        float NoV = clamp(dot(to_eye, n), 0.0, 1.0);

        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        float roughness = .5f;
        if (pc.has_roughness_map != 0) roughness = texture(ROUGHNESS, texCoord).r;
        roughness = max(roughness, .04);

        float metalness = 0.f;
        if (pc.has_metalness_map != 0) metalness = texture(METALNESS, texCoord).r;

        vec3 prefilter_col = textureLod(PREFILTER_ENV, r, roughness * pc.mip_levels).rgb;
        vec2 brdf_val = texture(BRDF_LUT, vec2(roughness, NoV)).rg;

        // referenced from s72-viewer
        vec3 specular_tint = mix(vec3(.04), albedo, metalness);

        forward_pass(position, n, to_eye, roughness, specular_tint, direct_diffuse, direct_specular);

        vec3 specular_color = prefilter_col * (specular_tint * brdf_val.x + brdf_val.y);
        vec3 irradiance = texture(IRRADIANCE, n).rgb;

        float OneMinusNoV = 1.0 - NoV;
        float Fc = OneMinusNoV * OneMinusNoV * OneMinusNoV * OneMinusNoV * OneMinusNoV;
        vec3 F = specular_tint + (1.0 - specular_tint) * Fc;
        vec3 diffuse_factor = mix(albedo, vec3(0.0), metalness);
        base_color = 
            specular_color 
            + direct_specular 
            + (1.0 - F) * diffuse_factor * irradiance 
            + (1.0 - F) * diffuse_factor * direct_diffuse;
    }

    vec3 radiance = min(base_color * exp2(pc.exposure), vec3(1e4)); // avoiding nans and infs
    outColor = vec4(do_tonemap(radiance, pc.tonemap), 1.0);
}