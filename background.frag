#version 450 // GLSL version 4.5

layout(location = 0) in vec2 position;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    float time;
};

void main() {
    vec2 pos = position - .5;
    float C = 3.0;
    float cosT = cos(C * time);
    float sinT = sin(C * time);

    if (length(pos) < .25) {
        outColor = vec4(cosT * pos.x + sinT * pos.y, cosT * pos.y - sinT * pos.x, (sinT + cosT) * .5, 1.0);
    } else {
        outColor = vec4(0.f);
    }
}