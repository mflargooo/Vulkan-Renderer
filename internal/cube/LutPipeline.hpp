// from the provided sample code

#pragma once

#include "../../RTG.hpp"
#include <glm/glm.hpp>

struct LutPipeline {
    VkDescriptorSetLayout set0_lut = VK_NULL_HANDLE;

    // no push consts

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline handle = VK_NULL_HANDLE;

    void create(RTG &);
    void destroy(RTG &);
};