#version 450

layout(set=0, binding=0,std140) uniform World {
    vec3 SKY_DIRECTION;
    vec3 SKY_ENERGY; // energy supplied by sky to a surface patch with normal = SKY_DIRECTION
    vec3 SUN_DIRECTION;
    vec3 SUN_ENERGY; // energy supplied by sky to a surface patch with normal = SUN_DIRECTION
};

layout(set=2, binding=0, std140) uniform Camera {
    mat4 CLIP_FROM_WORLD;
};

layout(push_constant) uniform Material {
    uint type;
} material;

layout(set=3, binding=0) uniform sampler2D TEXTURE;
layout(set=4, binding=0) uniform samplerCube CUBEMAP;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 3) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

void main() {  
    vec3 albedo = texture(TEXTURE, texCoord).rgb;
    if (material.type == 0) { // LAMBERTIAN
        vec3 n = normalize(normal);

        // hemi sky + directional sun
        vec3 e = SKY_ENERGY * (0.5 * dot(n,SKY_DIRECTION) + 0.5)
            + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));

        outColor = vec4(albedo / 3.1415926 * e, 1.0);
    } else if (material.type == 1) { 
        mat4 WORLD_FROM_CLIP = inverse(CLIP_FROM_WORLD);
        vec4 eye = WORLD_FROM_CLIP * vec4(0.0, 0.0, 0.0, 1.0);
        eye /= eye.w;

        vec3 to_eye = normalize(eye.xyz - position);

        vec3 n = normalize(normal);
        vec3 r = reflect(-to_eye, n).rgb;
        vec3 env_lighting = texture(CUBEMAP, r).rgb;
        outColor = vec4(env_lighting, 1.0);
        
    } else { // material.type == 2 ENVIRONMENT
        vec3 n = normalize(normal);
        outColor = vec4(texture(CUBEMAP, n).rgb, 1.0);
    }
}