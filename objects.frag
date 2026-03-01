#version 450

#include "tonemap.glsl"

layout(set=0, binding=0,std140) uniform World {
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
} pc;

layout(set=3, binding=0) uniform sampler2D TEXTURE;
layout(set=4, binding=0) uniform samplerCube CUBEMAP;
layout(set=5, binding=0) uniform sampler2D NORMAL;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

void main() {  
    vec3 n = normal;
    vec3 ns = normal;
    if (pc.has_normal_map != 0) {
        ns = normalize(2.0 * texture(NORMAL, texCoord).rgb - 1.0);
        mat3 xf = mat3(tangent, bitangent, normal);

        n = normalize(xf * ns);
    }

    vec3 base_color = vec3(0.0);
    if (pc.type == 0) { // LAMBERTIAN
        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        // hemi sky + directional sun
        vec3 e = SKY_ENERGY * (0.5 * dot(n,SKY_DIRECTION) + 0.5)
            + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));

        vec3 env_lighting = texture(CUBEMAP, n).rgb;
        base_color = albedo * (e + env_lighting) / 3.1415926;
    } else if (pc.type == 1) { 
        mat4 WORLD_FROM_CLIP = inverse(CLIP_FROM_WORLD);
        vec4 eye = WORLD_FROM_CLIP * vec4(0.0, 0.0, 0.0, 1.0);

        vec3 to_eye = normalize(eye.xyz / eye.w - position);

        vec3 r = reflect(-to_eye, n);
        vec3 env_lighting = texture(CUBEMAP, r).rgb;
        base_color = env_lighting;
        
    } else { // pc.type == 2 // ENVIRONMENT
        base_color = texture(CUBEMAP, n).rgb;
    }

    vec3 radiance = min(base_color * exp2(pc.exposure), vec3(1e4)); // avoiding nans and infs
    outColor = vec4(do_tonemap(radiance, pc.tonemap), 1.0);
}