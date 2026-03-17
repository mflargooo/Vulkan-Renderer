struct Light {
    mat4 WORLD_FROM_LIGHT;
    vec3 radiance;
    float attenuation;
    float radius;
    float blend;
    float fov;
    uint type;
};

layout(set=8, binding=0, std430) buffer Lights {
    float num_lights, _padding0, _padding1, _padding2;
    Light LIGHTS[];
};