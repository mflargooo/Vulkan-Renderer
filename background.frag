#version 450 // GLSL version 4.5

layout(location = 0) in vec2 position;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    float time;
};

void main() {
    float mask = step(.25, sin(position.x * 250.0 - time * 10.0)) - step(.25, sin(position.y * 250.0 - time * 10.0));
    
    // use position as hue
    // hsv to rgb from wikipedia: https://en.wikipedia.org/wiki/HSL_and_HSV#HSV_to_RGB
    float hue = mod((position.x + position.y) * 20.0 + time * 10.0, 6.0);
    float c = 1.0; // chroma = saturation * value; saturation and value as 1.0
    float x = c * (1.0 - abs(mod(hue, 2) - 1));

    vec3 rgb = vec3(0.0);
    if (hue < 1)
        rgb = vec3(c, x, 0.0);
    else if (hue < 2)
        rgb = vec3(x, c, 0.0);
    else if (hue < 3)
        rgb = vec3(0.0, c, x);
    else if (hue < 4)
        rgb = vec3(0.0, x, c);
    else if (hue < 5)
        rgb = vec3(x, 0.0, c);
    else
        rgb = vec3(c, 0.0, x);

    outColor = vec4(rgb * mask, 1.0);
}