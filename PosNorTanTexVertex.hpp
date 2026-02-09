#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

#include <glm/glm.hpp>

struct PosNorTanTexVertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec4 Tangent;
    glm::vec2 TexCoord;

    // a pipeline vertex input state that works with a buffer holding a PosNorTexVertex[] array
    static const VkPipelineVertexInputStateCreateInfo array_input_state;
};
static_assert(sizeof(PosNorTanTexVertex) == 3 * 4 + 3 * 4 + 4 * 4 + 2 * 4, "PosNorTanTexVertex is packed.");