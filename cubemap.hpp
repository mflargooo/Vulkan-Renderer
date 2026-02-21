#pragma once

#include "RTG.hpp"
#include "Helpers.hpp"
#include <vulkan/utility/vk_format_utils.h> // for byte counting

struct Cubemap {
    // upload vertical strip cubemap, extent is the size of a face
    static void transfer_vertical_strip_to_image(RTG &rtg, void const *data, size_t size, Helpers::AllocatedImage &image, VkExtent2D extent) {
        assert(extent.width == extent.height && "Width and height of cubemap face must be equal!");

        uint32_t data_size = (uint32_t)size / (extent.width * extent.height * 6);
        uint32_t block_size = extent.width * extent.height * data_size;
        
        for (uint32_t i = 0; i < 6; ++i) {
            rtg.helpers.transfer_to_image(
                ((char *)data) + block_size * i, 
                block_size, 
                image, 
                i
            );
        }
    };
};