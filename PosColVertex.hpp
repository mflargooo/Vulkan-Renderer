#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <glm/glm.hpp>

struct PosColVertex {
    glm::vec3 Position;
    glm::u8vec4 Color;

    // a pipeline vertex input state that works with a buffer holding a PosColorVertex[] array
    static const VkPipelineVertexInputStateCreateInfo array_input_state;
};
static_assert(sizeof(PosColVertex) == 3 * 4 + 4 * 1, "PosColVertex is packed.");