#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <glm/gtc/packing.hpp>

// from provided 15466 sample
inline glm::vec3 linear_from_rgbe(glm::u8vec4 color) {
    if (color == glm::u8vec4(0)) return glm::vec3(0.f);

    int e = color.a - 128;
    return glm::vec3{
        std::ldexpf(((float)color.r + 0.5f) / 256.f, e),
        std::ldexpf(((float)color.g + 0.5f) / 256.f, e),
        std::ldexpf(((float)color.b + 0.5f) / 256.f, e),
    };
};

inline uint64_t pack_r16g16b16a16_from_linear(glm::vec3 color) {
    return (uint64_t)glm::packHalf4x16(glm::vec4(color.r, color.g, color.b, 1.0f));
}