#version 450

struct Transform {
    mat4 CLIP_FROM_LOCAL;
    mat4 WORLD_FROM_LOCAL;
    mat4 WORLD_FROM_LOCAL_NORMAL;
};

layout(set=0, binding=0, std140) readonly buffer Transforms {
    Transform TRANSFROMS[];
};

layout(push_constant) uniform Constants {
    mat4 LIGHT_CLIP_FROM_WORLD;
};

layout(location = 0) in vec3 Position;

void main() {
    gl_Position = LIGHT_CLIP_FROM_WORLD * TRANSFROMS[gl_InstanceIndex].WORLD_FROM_LOCAL * vec4(Position, 1.0);
}