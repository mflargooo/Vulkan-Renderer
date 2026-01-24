#version 450 // GLSL version 4.5

layout(location = 0) in vec2 position;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    float time;
};

// https://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float STAR_DENSITY = .0001;
    float r = rand(position);
    float thresh = step(r, STAR_DENSITY);
    float shift = rand(2.0 * position);

    if (shift < .5) {
        outColor = vec4(mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0), 2.0 * shift), 1.0) * thresh * sin (time + shift * 6.28);
    } else {
        outColor = vec4(mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 1.0, 1.0), 2.0 * (shift - .5)), 1.0) * thresh * sin(time + shift * 6.28);
    }
}