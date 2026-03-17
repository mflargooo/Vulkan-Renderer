#pragma once

#include "LutPipeline.hpp"
#include "../../RTG.hpp"
#include "../../VK.hpp"

#include <glm/glm.hpp>

#include "../../external/stb/stb_image_write.h"

#include <iostream>
#include <fstream>

void lut(const char *outfile) {
    try {
        RTG::Configuration configuration;
        
        configuration.application_info = VkApplicationInfo{
            .pApplicationName = "Cube Utility",
            .applicationVersion = VK_MAKE_VERSION(0,0,0),
            .pEngineName = "Unknown",
            .engineVersion = VK_MAKE_VERSION(0,0,0),
            .apiVersion = VK_API_VERSION_1_3
        };

        /* bool print_usage = false;

        try {
            configuration.parse(argc, argv);
        } catch (std::runtime_error &e) {
            std::cerr << "Failed to parse arguments:\n" << e.what() << std::endl;
            print_usage = true;
        }

        if (print_usage) {
            std::cerr << "Usage:" << std::endl;
            RTG::Configuration::usage( [](const char *arg, const char *desc){
                std::cerr << "    " << arg << "\n        " << desc << std::endl;
            });
            return 1;
        } */

        configuration.headless = true;

        RTG rtg(configuration);

        LutPipeline pipeline;
        pipeline.create(rtg);


        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        { // create dp
            std::array< VkDescriptorPoolSize, 1 > pool_sizes{
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .descriptorCount = 1, // one for results
                },
            };

            VkDescriptorPoolCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = 0,
                .maxSets = 1,
                .poolSizeCount = (uint32_t)(pool_sizes.size()),
                .pPoolSizes = pool_sizes.data(),
            };

            VK( vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool) );
        }

        VkCommandPool command_pool = VK_NULL_HANDLE;
        { // create cmd pool
            VkCommandPoolCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = rtg.graphics_queue_family.value(),
            };
            VK( vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool) );
        }

        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        { // alloc cmd buffer from cmd pool
            VkCommandBufferAllocateInfo alloc_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            VK( vkAllocateCommandBuffers(rtg.device, &alloc_info, &command_buffer) );
        }

        uint32_t lut_size = 512;
        Helpers::AllocatedImage image = rtg.helpers.create_image(
            VkExtent2D{ .width = lut_size, .height = lut_size },
            VK_FORMAT_R16G16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            Helpers::Unmapped
        );
        Helpers::AllocatedBuffer results = rtg.helpers.create_buffer(
            lut_size * lut_size * sizeof( uint32_t ),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            Helpers::Mapped
        );

        std::vector< uint32_t > data(lut_size * lut_size);
        rtg.helpers.transfer_to_image(data.data(), sizeof(data[0])*lut_size*lut_size, image, (VkImageLayout)VK_IMAGE_LAYOUT_GENERAL);
            
        VkDescriptorSet descriptors;
        Helpers::AllocatedBuffer buffer;
        VkImageView view;

        VkImageViewCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .flags = 0,
            .image = image.handle,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image.format,
            // .components sets swizzling and is fine when zero inited
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VK( vkCreateImageView(rtg.device, &create_info, nullptr, &view) );

        VkDescriptorSetAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &pipeline.set0_lut
        };

        VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptors) );

        VkDescriptorImageInfo image_info{
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        VkWriteDescriptorSet write_info{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptors,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &image_info,
        };

        vkUpdateDescriptorSets(rtg.device, 1, &write_info, 0, nullptr);

        VK( vkResetCommandBuffer(command_buffer, 0) );

        { // begin rec
            VkCommandBufferBeginInfo begin_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            VK( vkBeginCommandBuffer(command_buffer, &begin_info) );
        }

        // use cube pipeline
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);

        {
            VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL, // for GPU -> CPU
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image.handle,
                .subresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, // dependencyFlags
                0, nullptr, // mem barrier count, ptr
                0, nullptr, // buf mem barrier count, ptr
                1, &barrier // img mem barrier count, ptr
            );
        }

        { // bind descriptor sets
            std::array< VkDescriptorSet, 1 > descriptor_sets{
                descriptors,
            };
            vkCmdBindDescriptorSets(
                command_buffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline.layout,
                0,
                (uint32_t)(descriptor_sets.size()), descriptor_sets.data(),
                0, nullptr
            );
        }

        // run
        vkCmdDispatchBase(command_buffer, 0, 0, 1, (uint32_t)lut_size/8, (uint32_t)lut_size/8, 1);

        { // pull results
            VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // for GPU -> CPU
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image.handle,
                .subresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, // dependencyFlags
                0, nullptr, // mem barrier count, ptr
                0, nullptr, // buf mem barrier count, ptr
                1, &barrier // img mem barrier count, ptr
            );
        } 

        {
            VkBufferImageCopy region{
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .imageOffset{ .x = 0, .y = 0, .z = 0 }, // TODO: compute in blocks
                .imageExtent{
                    .width = image.extent.width,
                    .height = image.extent.height,
                    .depth = 1,
                },
            };

            vkCmdCopyImageToBuffer(
                command_buffer,
                image.handle,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                results.handle,
                1,
                &region
            );
        }

        {
            VkBufferMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                .buffer = results.handle,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };

            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                0, 
                0, nullptr, 
                1, &barrier, 
                0, nullptr
            );
        }

        // finish rec
        VK( vkEndCommandBuffer(command_buffer) );

        { // submit cmd buf
            VkSubmitInfo submit_info{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &command_buffer,
            };

            VK( vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, nullptr) );
        }

        VK( vkDeviceWaitIdle(rtg.device) );

        std::ofstream file(outfile, std::ios::binary);
        if (!file) {
            std::cerr << "Unable to save to " << outfile << std::endl;
            exit(1);
        }
        file.write((const char *)results.allocation.mapped, lut_size * lut_size * sizeof(uint32_t));


        rtg.helpers.destroy_buffer(std::move(results));
        rtg.helpers.destroy_image(std::move(image));

        vkDestroyImageView(rtg.device, view, nullptr);
        view = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;

        vkDestroyCommandPool(rtg.device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;

        pipeline.destroy(rtg);

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        exit(1);
    }
}