#include "Viewer.hpp"

#include "VK.hpp"
#include "mat4.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <filesystem>

// for building the scene
#include <stack>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <s72/DataStream.hpp>
#include <variant>
#include <limits>
#include <algorithm>

// for environment
#include "rgbe.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include "external/stb/stb_image.h"

Viewer::Viewer(RTG &rtg_) : rtg(rtg_) {
	// select depth format
	// 	(at least one of these two must be supported according to the spec; but neither are required)
	depth_format = rtg.helpers.find_image_format(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32 },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	{ // create render pass
		// attachments
		std::array< VkAttachmentDescription, 2 > attachments{
			VkAttachmentDescription{ // 0 - color
				.format = rtg.surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = rtg.present_layout,
			},
			VkAttachmentDescription{ // 1 - depth
				.format = depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
		};

		// subpass
		VkAttachmentReference color_attachment_ref{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference depth_attachment_ref{
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_ref,
			.pDepthStencilAttachment = &depth_attachment_ref,
		};

		// dependencies
		// this refers to image load actions for attachments
		std::array< VkSubpassDependency, 2 > dependencies {
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			},
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			}
		};

		VkRenderPassCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = uint32_t(dependencies.size()),
			.pDependencies = dependencies.data(),
		};

		VK( vkCreateRenderPass(rtg.device, &create_info, nullptr, &render_pass) );
	}
	// create command pool

	{
		VkCommandPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value(),
		};
		VK( vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool) );
	}

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{ // create descriptor pool
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); // for easier-to-read counting

		std::array< VkDescriptorPoolSize, 2 > pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace, // one descriptor per set, two set per workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1 * per_workspace, // same comment
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, // omission of CREATE_FREE_DESCRIPTOR_SET_BIT, prevent individual descriptors allocated from this pool
			.maxSets = 3 * per_workspace, // three sets per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK( vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool) );
	}

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {

		{ // allocate cmd buf
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK( vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.command_buffer) );
		}

		workspace.Camera_src = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // GPU to copy from this mem
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible mem coherent (no special sync needed)
			Helpers::Mapped
		);
		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // to use as uniform buf, GPU to copy into here
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // GPU-local mem
			Helpers::Unmapped // no pointer to this mem 
		);

		{ // allocate descriptor set for Camera descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera,
			};

			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors) );
		}

		workspace.World_src = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);
		workspace.World = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		{ // allocate descriptor set for World descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set0_World,
			};

			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors) );
		}

		{ // allocate descriptor set for Transforms descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set1_Transforms,
			};

			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors) );
			// will fill in this descriptor set in render when buffers are [re-]allocated
		}

		{ // point descriptor to camera buffer
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			VkDescriptorBufferInfo World_info{
				.buffer = workspace.World.handle,
				.offset = 0,
				.range = workspace.World.size,
			};

			std::array< VkWriteDescriptorSet, 2 > writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &World_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device, // device
				uint32_t(writes.size()), // descriptorWriteCount
				writes.data(), // pDescriptorWrites
				0, // descriptorCopyCount
				nullptr // pDescriptorCopies
			);
		}
	}

	{ // static sun and sky
		world.SKY_DIRECTION.x = 0.0f;
		world.SKY_DIRECTION.y = 0.0f;
		world.SKY_DIRECTION.z = 1.0f;

		world.SKY_ENERGY.r = 0.1f;
		world.SKY_ENERGY.g = 0.1f;
		world.SKY_ENERGY.b = 0.2f;

		world.SUN_DIRECTION.x = 6.0f / 23.0f;
		world.SUN_DIRECTION.y = 13.0f / 23.0f;
		world.SUN_DIRECTION.z = 18.0f / 23.0f;

		world.SUN_ENERGY.r = 1.0f;
		world.SUN_ENERGY.g = 1.0f;
		world.SUN_ENERGY.b = 0.9f;
	}

	{ // make some textures
		{ // tex 0 is dark / light grey checker with red origin - debug texture
			uint32_t size = 128;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				float fy = (y + .5f) / float(size);
				for (uint32_t x = 0; x < size; ++x) {
					float fx = (x + .5f) / float(size);
					// highlight origin
					if (fx < .05f && fy < .05f) data.emplace_back(0xff0000ff);
					else if ((fx < .5f) == (fy < .5f)) data.emplace_back(0xff444444);
					else data.emplace_back(0xffbbbbbb);
				}
			}
			assert(data.size() == size * size);

			// make place for tex to live on GPU
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size, .height = size }, // size of img
				VK_FORMAT_R8G8B8A8_UNORM, // how to interpret image data
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
				Helpers::Unmapped
			));

			// transfer data
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}

		{ // tex 1 will be LUT texture at known location
			std::string lut_path = "bin/.lut"; // sibling of viewer.exe
			uint32_t size = 512;
			std::vector< char > data;
			DataStream::load(lut_path, data);
			assert(data.size() == size * size * sizeof(uint32_t));
			// make a place for the tex to live on GPU
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size, .height = size }, // size of img
				VK_FORMAT_R16G16_SFLOAT, // how to interpret image data
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
				Helpers::Unmapped
			));
			rtg.helpers.transfer_to_image(data.data(), data.size(), textures.back());
		}
	}
	
	if (rtg.configuration.scene.compare("")) { // Load scene
		load_scene(rtg.configuration.scene);
	}

	{ // make image views for the textures
		texture_views.reserve(textures.size());
		for (Helpers::AllocatedImage const &image : textures) {
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

			VkImageView image_view = VK_NULL_HANDLE;
			VK( vkCreateImageView(rtg.device, &create_info, nullptr, &image_view) );

			texture_views.emplace_back(image_view);
		}
		assert(texture_views.size() == textures.size());
	}

	{ // make a sampler for the textures
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.f,
			.anisotropyEnable  = VK_FALSE,
			.maxAnisotropy = 0.f,
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.minLod = 0.f,
			.maxLod = 0.f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK( vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler) );
	}

	{ // create the texture descriptor pool
		uint32_t per_texture = uint32_t(textures.size()); // for easier to read counting

		std::array< VkDescriptorPoolSize, 1 > pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 * 1 * per_texture, // one desc per set, on set per tex
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, // because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individuals descriptors allocated from this pool
			.maxSets = 1 * per_texture, // one set per texture
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK( vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool) );
	}

	{ // allocate and write the texture descriptor sets
		// allocate the descriptors (using same alloc_info)
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = texture_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &objects_pipeline.set3_TEXTURE,
		};
		texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);
		for (VkDescriptorSet &descriptor_set : texture_descriptors) {
			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set) );
		}

		// write descriptor for textures
		std::vector< VkDescriptorImageInfo > infos(textures.size());
		std::vector< VkWriteDescriptorSet > writes(textures.size());

		for (Helpers::AllocatedImage const &image : textures) {
			size_t i = &image - &textures[0];

			infos[i] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[i],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			writes[i] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = texture_descriptors[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i],
			};
		}

		vkUpdateDescriptorSets( rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr );
	}

	// cubemap initialization
	// env_maps vector is ordered such that the ith entry is the ith mip
	// the lambertian prefiltered.

	bool default_env_map = env_maps.empty();
	if (env_maps.empty()) { // create dummy data
		for (uint32_t i = 0; i < 2; ++i) {
			env_maps.emplace_back(rtg.helpers.create_cubemap(
				VkExtent2D{ .width = 1, .height = 1 },
				VK_FORMAT_R16G16B16A16_SFLOAT, // change after converting data to appropriate format
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));

			uint64_t pixels[6] = { 0, 0, 0, 0, 0, 0 };
			rtg.helpers.transfer_to_cubemap(&pixels, sizeof(uint64_t) * 6, env_maps.back());
		}
	}
	
	// make cubemap views 0 - lambertian, -1 - pbr
	assert(env_maps.size() == 2); // lambertian and pbr
	{ // make image views for the env_maps
		env_map_views.reserve(3);
		// lambertian

		struct EnvMapView {
			VkImageViewType type;
			Helpers::AllocatedImage *image = nullptr;
			uint32_t level_count = 1;
		} view_infos[3] = { 
			{ .type = VK_IMAGE_VIEW_TYPE_CUBE, .image = &env_maps.back(), .level_count = 1 }, // lambertian
			{ .type = VK_IMAGE_VIEW_TYPE_CUBE, .image = &env_maps[0], .level_count = default_env_map ? 1 : mip_levels }, // pbr
			{ .type = VK_IMAGE_VIEW_TYPE_2D, .image = &textures[1], .level_count = 1 }, // lut is at index 1 of textures
		};

		for (EnvMapView &info : view_infos) {
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = info.image->handle,
				.viewType = info.type,
				.format = info.image->format,
				// .components sets swizzling and is fine when zero inited
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = info.level_count,
					.baseArrayLayer = 0,
					.layerCount = info.type == VK_IMAGE_VIEW_TYPE_CUBE ? 6U : 1U,
				},
			};

			VkImageView image_view = VK_NULL_HANDLE;
			VK( vkCreateImageView(rtg.device, &create_info, nullptr, &image_view) );

			env_map_views.emplace_back(image_view);
		}
	}

	for (uint32_t level_count : std::array< uint32_t, 3 >{ 0, default_env_map ? 0 : mip_levels, 0 }) { // make a sampler for the env_maps
		env_map_samplers.emplace_back(VK_NULL_HANDLE);
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.mipLodBias = 0.f,
			.anisotropyEnable  = VK_FALSE,
			.maxAnisotropy = 0.f,
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.minLod = 0.f,
			.maxLod = float(level_count),
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK( vkCreateSampler(rtg.device, &create_info, nullptr, &env_map_samplers.back()) );
	}

	{ // create the cubemap descriptor pool
		uint32_t per_cubemap_view = uint32_t(env_map_views.size()); // for easier to read counting

		std::array< VkDescriptorPoolSize, 1 > pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 * 1 * per_cubemap_view, // one desc per set, on set per tex
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, // because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individuals descriptors allocated from this pool
			.maxSets = 1 * per_cubemap_view, // one set per texture
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK( vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &env_map_descriptor_pool) );
	}

	{ // allocate and write the cubemap descriptor sets
		// allocate the descriptors (using same alloc_info)
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = env_map_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &objects_pipeline.set4_ENV_MAP,
		};
		env_map_descriptor = VK_NULL_HANDLE;
		VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &env_map_descriptor) );

		// write descriptor for env_maps
		std::vector< VkDescriptorImageInfo > infos(env_map_views.size());
		std::vector< VkWriteDescriptorSet > writes(env_map_views.size());

		for (auto const &cubemap_view : env_map_views) {
			size_t i = &cubemap_view - &env_map_views[0];

			infos[i] = VkDescriptorImageInfo{
				.sampler = env_map_samplers[i],
				.imageView = cubemap_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			writes[i] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = env_map_descriptor,
				.dstBinding = (uint32_t)i,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i],
			};
		}

		vkUpdateDescriptorSets( rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr );
	}
}

Viewer::~Viewer() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Viewer::~Viewer [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (env_map_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, env_map_descriptor_pool, nullptr);
		env_map_descriptor_pool = nullptr;
	}

	if (!env_map_samplers.empty()) {
		for (uint32_t i = 0; i < env_map_samplers.size(); ++i) {
			vkDestroySampler(rtg.device, env_map_samplers[i], nullptr);
			env_map_samplers[i] = VK_NULL_HANDLE;
		}
	}

	for (VkImageView &view : env_map_views) {
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}
	env_map_views.clear();

	for (auto &env_map : env_maps) {
		rtg.helpers.destroy_cubemap(std::move(env_map));
	}
	env_maps.clear();

	if (texture_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, texture_descriptor_pool, nullptr);
		texture_descriptor_pool = nullptr;
		
		// this also frees descriptor sets allocated from pool
		texture_descriptors.clear();
	}

	if (texture_sampler) {
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
	}

	for (VkImageView &view : texture_views) {
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}
	texture_views.clear();

	for (auto &texture : textures) {
		rtg.helpers.destroy_image(std::move(texture));
	}
	textures.clear();

	for (auto &[_, object_vertices] : scene_vertices) {
		rtg.helpers.destroy_buffer(std::move(object_vertices));
	}

	scene_vertices.clear();

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		if(workspace.command_buffer != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
			workspace.command_buffer = VK_NULL_HANDLE;
		}

		if (workspace.lines_vertices_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if (workspace.lines_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}

		if (workspace.Camera_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}
		if (workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
		// Camera_descriptors freed when pool is destroyed

		if (workspace.World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}
		if (workspace.World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}
		// World_descriptors freed when pool is destroyed

		if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}
		if (workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
		}
		// Transforms_descriptors freed when pool is destroyed.
	}
	workspaces.clear();

	if (descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr; 
		// also frees descriptor sets allocated from pool
	}

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);

	// destroy command pool
	if (command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
	}

	if (render_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(rtg.device, render_pass, nullptr);
		render_pass = VK_NULL_HANDLE;
	}
}

void Viewer::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	// clean up exist framebufs (and depth img)
	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}
	
	// allocate depth image for framebufs to share
	swapchain_depth_image = rtg.helpers.create_image(
		swapchain.extent,
		depth_format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);

	{ // create an image view of the depth img 
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		};

		VK( vkCreateImageView(rtg.device, &create_info, nullptr, &swapchain_depth_image_view) );
	}
	
	// create framebufs pointing to each swapchain img view and the shared depth view
	swapchain_framebuffers.assign(swapchain.image_views.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < swapchain.image_views.size(); ++i) {
		std::array< VkImageView, 2 > attachments{
			swapchain.image_views[i],
			swapchain_depth_image_view,
		};
		VkFramebufferCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.layers = 1,
		};

		VK( vkCreateFramebuffer(rtg.device, &create_info, nullptr, &swapchain_framebuffers[i]) );
	}
}

void Viewer::destroy_framebuffers() {
	for (VkFramebuffer &framebuffer : swapchain_framebuffers) {
		assert(framebuffer != VK_NULL_HANDLE);
		vkDestroyFramebuffer(rtg.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}
	swapchain_framebuffers.clear();

	assert(swapchain_depth_image_view != VK_NULL_HANDLE);
	vkDestroyImageView(rtg.device, swapchain_depth_image_view, nullptr);
	swapchain_depth_image_view = VK_NULL_HANDLE;

	rtg.helpers.destroy_image(std::move(swapchain_depth_image));
}

int Viewer::load_scene(const std::string &filename) {

	// try-catch from print-scene.cpp
	try {
		s72 = std::move(S72::load(filename));
	} catch (std::exception &e) {
		std::cerr << "Failed to load s72-format scene from '" << filename << "':\n" << e.what() << std::endl;
		return -1;
	}
	std::cerr << "Loaded scene with filename \'" << filename << "\'" << std::endl;

	{ // create buffers for each data files
		for (auto &[src, df] : s72.data_files) {
			if (scene_vertices.find(df.path) != scene_vertices.end()) { // i don't expect this to ever call
				std::cerr << "Skipping duplicate src file \'" << src << "\'." << std::endl;
				continue;
			}

			std::vector< char > vertices; 
			DataStream::load(df.path, vertices); // vertex data

			// allocate new buffer
			Helpers::AllocatedBuffer buffer;

			// upload vertex bytes
			size_t bytes = vertices.size();
		
			buffer = rtg.helpers.create_buffer(
				bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			rtg.helpers.transfer_to_buffer(vertices.data(), bytes, buffer);
			
			// track the buffer
			scene_vertices.insert({df.path, std::move(buffer)});
		}
	}

	{ // create AABBs
		for (auto &[_, mesh] : s72.meshes) {
			auto const &path = mesh.attributes.at("POSITION").src.path;
			auto const &stride = mesh.attributes.at("POSITION").stride;
			auto const &offset = mesh.attributes.at("POSITION").offset;
			std::vector< char > data;
			char *vertices = DataStream::load(path, data).data(); // vertex data
			auto &buffer = scene_vertices[path];
			AABB aabb = AABB{
				.min = glm::vec3(FLT_MAX),
				.max = glm::vec3(-FLT_MAX),
			};
			for (size_t i = 1; i < mesh.count; ++i) { // remember to revert this -- I think is data layout is issue for infinite edge, but for now, skip that vertex position
				glm::vec3 pos = glm::vec3{
					*(float *)&vertices[i * stride + offset],
					*(float *)&vertices[i * stride + offset + 4],
					*(float *)&vertices[i * stride + offset + 8],
				};
				aabb.min = glm::min(aabb.min, pos);
				aabb.max = glm::max(aabb.max, pos);
			}

			for (glm::length_t i = 0; i < 3; ++i) {
				if (std::abs(aabb.max[i] - aabb.min[i]) < .00001f) {
					aabb.min[i] -= .000005f;
					aabb.max[i] += .000005f;
				}
			}

			// for now, index by buffer, later by mesh
			aabbs.insert({ &buffer, std::move(aabb) });
		}
	}

	{ // load textures
		for (auto &[key, texture] : s72.textures) {
			if (rtg.configuration.debug) std::cerr << "[TEXTURE] " << texture.src << std::endl;
			int x = 0, y = 0, c = 0;
			stbi_set_flip_vertically_on_load(true);
			// stbi loads with 8 bit channels, hence static 32 bits chosen
			uint32_t *tex = (uint32_t *)stbi_load(texture.path.data(), &x, &y, &c, 4);
			uint32_t width = (uint32_t)x, height = (uint32_t)y;

			if (texture.type == S72::Texture::Type::flat) {
				VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
				if (texture.format == S72::Texture::Format::linear) {
					format = VK_FORMAT_R8G8B8A8_UNORM;
				}

				// track index of texture
				tex_ids.insert({ texture.path, (int32_t)textures.size() });
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{ .width = width, .height = height },
					format,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					Helpers::Unmapped
				));
				
				rtg.helpers.transfer_to_image(tex, sizeof(uint32_t) * x * y, textures.back());

			} else if (texture.type == S72::Texture::Type::cube) {
				height = width; // assume always vertical strip, for now

				{
					uint32_t expected_mip_levels = (uint32_t)std::floor(std::log2f((float)width) - std::log2f(32.f)) + 1;
					// material-dependent env maps
					std::vector< std::string > mip_paths;
					{ // mips paths
						for (uint32_t m = 1; m < expected_mip_levels; ++m) {
							std::string path = texture.path.substr(0, texture.path.size() - 3) + std::to_string(m) + ".png";
							if (std::filesystem::exists(path)) mip_paths.emplace_back(path);
						}
					}
					mip_levels = std::max((uint32_t)mip_paths.size(), 1U);
					// track index of texture
					cubemap_ids.insert({ texture.path, (uint32_t)env_maps.size() });
					env_maps.emplace_back(rtg.helpers.create_cubemap(
						VkExtent2D{ .width = width, .height = height },
						VK_FORMAT_R16G16B16A16_SFLOAT, // change after converting data to appropriate format
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						Helpers::Unmapped,
						mip_levels
					));

					uint32_t m = 0;
					std::vector< uint64_t > pixels;
					do {
						if (m > 0) {
							tex = (uint32_t *)stbi_load(mip_paths[m-1].data(), &x, &y, &c, 4);
							width = (uint32_t)x, height = (uint32_t)y;
							height = width; // assume always vertical strip, for now
						}

						pixels.clear();
						pixels.reserve(x * y);

						for (int i = 0; i < x * y; ++i) {
							// switch from cubemap render -> vulkan convention
							// rotate cubemap face conventions to upright
							size_t f = 5 - i / (width * height); // current face
							size_t j = i % (width * height); // jth pixel of face
						
							size_t col = j % width; 
							size_t row = j / width;
							size_t k = (height - 1 - row) * width + col;

							uint32_t pixel = tex[f * height * width + k];

							glm::vec3 rgb = linear_from_rgbe(glm::u8vec4(
								pixel        & 0xff,
								(pixel >>  8) & 0xff,
								(pixel >> 16) & 0xff,
								(pixel >> 24) & 0xff
							));

							pixels.emplace_back(pack_r16g16b16a16_from_linear(rgb));
						}
						assert(pixels.size() * sizeof(pixels[0]) == sizeof(uint64_t) * x * y);
						rtg.helpers.transfer_to_cubemap(pixels.data(), sizeof(uint64_t) * x * y, env_maps.back(), m);
						stbi_image_free(tex);
						m += 1;
					} while(m < mip_levels);
				}

				{
					// load lambertian
					for (std::string suffix : { "lambertian.png" }){
						std::string path = texture.path.substr(0, texture.path.size() - 3) + suffix;
						if (!std::filesystem::exists(path)) continue;

						tex = (uint32_t *)stbi_load(path.data(), &x, &y, &c, 4);
						width = (uint32_t)x, height = (uint32_t)y;
						height = width; // assume always vertical strip, for now
						
						// track index of texture
						cubemap_ids.insert({ path, (uint32_t)env_maps.size() });
						env_maps.emplace_back(rtg.helpers.create_cubemap(
							VkExtent2D{ .width = width, .height = height },
							VK_FORMAT_R16G16B16A16_SFLOAT, // change after converting data to appropriate format
							VK_IMAGE_TILING_OPTIMAL,
							VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							Helpers::Unmapped
						));

						std::vector< uint64_t > pixels;
						pixels.reserve(x * y);
						for (int i = 0; i < x * y; ++i) {
							// switch from cubemap render -> vulkan convention
							// rotate cubemap face conventions to upright
							size_t f = 5 - i / (width * height); // current face
							size_t j = i % (width * height); // jth pixel of face
							
						size_t col = j % width; 
						size_t row = j / width;	
						size_t k = (height - 1 - row) * width + col;

						uint32_t pixel = tex[f * height * width + k];

							glm::vec3 rgb = linear_from_rgbe(glm::u8vec4(
								pixel        & 0xff,
								(pixel >>  8) & 0xff,
								(pixel >> 16) & 0xff,
								(pixel >> 24) & 0xff
							));

							pixels.emplace_back(pack_r16g16b16a16_from_linear(rgb));
						}
						assert(pixels.size() * sizeof(pixels[0]) == sizeof(uint64_t) * x * y);
						rtg.helpers.transfer_to_cubemap(pixels.data(), sizeof(uint64_t) * x * y, env_maps.back());
						stbi_image_free(tex);
					}
				}
			}
		}
	}

	std::cout << "Loaded " << textures.size() << " textures and " << env_maps.size() << " env_maps!" << std::endl;

	{ // load materials
		for (auto &[key, material] : s72.materials) {
			// if (rtg.configuration.debug) std::cerr << "[MATERIAL] " << material.name << std::endl;
			
			int32_t albedo_id = -1;
			int32_t normal_id = material.normal_map ? (int32_t)tex_ids[material.normal_map->path] : -1;
			int32_t displacement_id = material.displacement_map ? (int32_t)tex_ids[material.displacement_map->path] : -1;
			int32_t roughness_id = -1;
			int32_t metalness_id = -1;

			if (auto p_lamb = std::get_if<S72::Material::Lambertian>(&material.brdf)) {
				S72::Material::Lambertian lamb = *p_lamb;
				// https://stackoverflow.com/questions/63482070/how-can-i-code-something-like-a-switch-for-stdvariant
				if (auto p_albedo = std::get_if<glm::vec3>(&lamb.albedo)) {
					glm::u8vec4 albedo = glm::u8vec4(255.f * glm::clamp(glm::vec4(*p_albedo, 1.f), 0.f, 1.f));
					// simple monocolor texture
					uint32_t size = 32;
					std::vector< glm::u8vec4 > data(size * size, albedo);
					assert(data.size() == size * size);

					albedo_id = (int)textures.size();
					// make place for tex to live on GPU
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = size, .height = size }, // size of img
						VK_FORMAT_R8G8B8A8_UNORM, // how to interpret image data
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
						Helpers::Unmapped
					));

					// transfer data
					rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
				} else if (auto p_texture = std::get_if<S72::Texture *>(&lamb.albedo)) {
					albedo_id = (int32_t)tex_ids[(*p_texture)->path];
				}

				materials.insert({ material.name, Material{ .type = 0, .albedo_id = albedo_id, .normal_id = normal_id, .displacement_id = displacement_id, .roughness_id = -1, .metalness_id = -1 } });
			} else if (/*auto p_mirror = */std::get_if<S72::Material::Mirror>(&material.brdf)) {
				materials.insert({ material.name, Material{ .type = 1, .normal_id = normal_id, .displacement_id = displacement_id, .roughness_id = -1, .metalness_id = -1 }});
			} else if (/*auto p_env = */std::get_if<S72::Material::Environment>(&material.brdf)) {
				materials.insert({ material.name, Material{ .type = 2, .normal_id = normal_id, .displacement_id = displacement_id, .roughness_id = -1, .metalness_id = -1 }});
			} else if (auto p_pbr = std::get_if<S72::Material::PBR>(&material.brdf)) {
				// https://stackoverflow.com/questions/63482070/how-can-i-code-something-like-a-switch-for-stdvariant
				if (auto p_albedo = std::get_if<glm::vec3>(&p_pbr->albedo)) {
					glm::u8vec4 albedo = glm::u8vec4(255.f * glm::clamp(glm::vec4(*p_albedo, 1.f), 0.f, 1.f));
					// simple monocolor texture
					uint32_t size = 32;
					std::vector< glm::u8vec4 > data(size * size, albedo);
					assert(data.size() == size * size);

					albedo_id = (int)textures.size();
					// make place for tex to live on GPU
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = size, .height = size }, // size of img
						VK_FORMAT_R8G8B8A8_UNORM, // how to interpret image data
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
						Helpers::Unmapped
					));

					// transfer data
					rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
				} else if (auto p_texture = std::get_if<S72::Texture *>(&p_pbr->albedo)) {
					albedo_id = (int32_t)tex_ids[(*p_texture)->path];
				}

				if (auto p_roughness = std::get_if< float >(&p_pbr->roughness)) {
					// simple monocolor texture
					uint32_t size = 32;
					std::vector< float > data(size * size, *p_roughness);
					assert(data.size() == size * size);

					roughness_id = (int)textures.size();
					// make place for tex to live on GPU
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = size, .height = size }, // size of img
						VK_FORMAT_R32_SFLOAT, // how to interpret image data
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
						Helpers::Unmapped
					));

					// transfer data
					rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
				} else if (auto p_roughness_map = std::get_if<S72::Texture *>(&p_pbr->roughness)) {
					roughness_id = (int32_t)tex_ids[(*p_roughness_map)->path];
				}

				if (auto p_metalness = std::get_if< float >(&p_pbr->metalness)) {
					// simple monocolor texture
					uint32_t size = 32;
					std::vector< float > data(size * size, *p_metalness);
					assert(data.size() == size * size);

					metalness_id = (int)textures.size();
					// make place for tex to live on GPU
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = size, .height = size }, // size of img
						VK_FORMAT_R32_SFLOAT, // how to interpret image data
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
						Helpers::Unmapped
					));

					// transfer data
					rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
				} else if (auto p_metalness_map = std::get_if<S72::Texture *>(&p_pbr->metalness)) {
					metalness_id = (int32_t)tex_ids[(*p_metalness_map)->path];
				}
				
				materials.insert({ material.name, Material{ .type = 3, .albedo_id = albedo_id, .normal_id = normal_id, .displacement_id = displacement_id, .roughness_id = roughness_id, .metalness_id = metalness_id }});
			}
		}
	}

	parse_scene_graph();

	if (rtg.configuration.camera.compare("")) { // select correct scene camera, if exists
		auto it = scene_cameras.find(rtg.configuration.camera);
		if (it == scene_cameras.end()) {
			std::cerr << "Scene camera " << rtg.configuration.camera << " does not exist! Exiting";
			exit(1);
		}

	selected_scene_camera = it;
	} else if (scene_cameras.size() > 0) {
		selected_scene_camera = scene_cameras.begin();
	} else {
		selected_scene_camera = scene_cameras.end();
	}

	for (auto &driver : s72.drivers) {
		for (auto d_time : driver.times) {
			max_animation_time = std::max(d_time, max_animation_time);
		}
	}

	return 0;
};

void Viewer::parse_scene_graph() { // populate all object instances
	object_instances.clear();

	struct Element {
		S72::Node *node;
		glm::mat4 xf;
	};
	std::stack< Element > frontier;

	// build the frontier from scene roots
	for (auto root : s72.scene.roots) {
		frontier.push({ .node = root, .xf = glm::mat4(1.f) });
	}
	
	// dfs through the DAG to populate instances
	while (!frontier.empty()) {
		Element elem = std::move(frontier.top());
		frontier.pop();

		S72::Node *node = elem.node;
		// transform parent space to local
		glm::mat4 xf = elem.xf *
			glm::translate(glm::mat4(1.0f), node->translation) *
			glm::mat4_cast(glm::quat(node->rotation.z, node->rotation.w, node->rotation.x, node->rotation.y)) *
			glm::scale(glm::mat4(1.0f), node->scale);

		// push children to frontier with this xf as their parent space
		for (auto child : node->children) {
			frontier.push({ .node = child, .xf = xf });
		}

		// handle the node appropriately
		if (node->mesh) {
			// if (rtg.configuration.debug) std::cerr << "[MESH] " << node->mesh->name << " located at " << glm::to_string(xf) << std::endl; 
			
			Material material;
			if (node->mesh->material) {
				material = materials[node->mesh->material->name];
			}

			// create object instance
			object_instances.emplace_back(ObjectInstance{
				.vertices{
					.first = 0,
					.count = node->mesh->count
				},
				.buffer = &scene_vertices.at(node->mesh->attributes.at("POSITION").src.path),
				.transform{
					/* .CLIP_FROM_LOCAL = glm::mat4(1.f), -- filled by update */
					.WORLD_FROM_LOCAL = xf,
					.WORLD_FROM_LOCAL_NORMAL = glm::transpose(glm::inverse(glm::mat3(xf))),
				},
				.material = material,
			});
		} else if (node->camera) {
			// if (rtg.configuration.debug) std::cerr << "[CAMERA] " << node->camera->name << " located at " << glm::to_string(xf) << std::endl; 
			if (auto p_proj = std::get_if<S72::Camera::Perspective>(&node->camera->projection)) {
				scene_cameras[node->camera->name] = {
					.transform = {
						.WORLD_FROM_LOCAL = xf,
					},
					.aspect = p_proj->aspect,
					.vfov = p_proj->vfov,
					.near = p_proj->near,
					.far = p_proj->far,
				};
			}
		} else if (node->light) {
			if (auto p_sun = std::get_if< S72::Light::Sun >(&node->light->source)) {
				glm::vec3 direction = glm::normalize(glm::vec3(xf * glm::vec4(0.f, 0.f, 1.f, 0.f)));
				
				if (node->light->name == "Sun") {
					world.SUN_DIRECTION = { .x = direction.x, .y = direction.y, .z = direction.z };
					world.SUN_ENERGY = { .r = node->light->tint.x * p_sun->strength, .g = node->light->tint.y * p_sun->strength, .b = node->light->tint.z * p_sun->strength };
				} else if (node->light->name == "Sky") {
					world.SKY_DIRECTION = { .x = direction.x, .y = direction.y, .z = direction.z };
					world.SKY_ENERGY = { .r = node->light->tint.x * p_sun->strength, .g = node->light->tint.y * p_sun->strength, .b = node->light->tint.z * p_sun->strength };
				}
			}
		}
	}
};

void Viewer::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	//get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];

	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	// reset the command buffer (clear old commands)
	VK ( vkResetCommandBuffer(workspace.command_buffer, 0) );
	{ // record
		VkCommandBufferBeginInfo begin_info {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		VK( vkBeginCommandBuffer(workspace.command_buffer, &begin_info) );
	}

	if (!lines_vertices.empty()) { // upload lines vertices
		// [re-]allocate line bufs in needed
		size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
		if (workspace.lines_vertices_src.handle == VK_NULL_HANDLE || workspace.lines_vertices_src.size < needed_bytes) {
			// round to next mult of 4k to avoid realloc continuously if vert count grows slowly
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if (workspace.lines_vertices_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
			}
			if (workspace.lines_vertices.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
			}

			workspace.lines_vertices_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // going to have GPU copy from this mem
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible mem, coherent (no special sync needed)
				Helpers::Mapped // get a pointer to the mem
			);
			workspace.lines_vertices = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as vertex buf, also going to have GPU into this mem
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // GPU-local mem
				Helpers::Unmapped // don't get a pointer to the mem
			);

			std::cout << "Re-allocated lines buffers to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.lines_vertices_src.size == workspace.lines_vertices.size);
		assert(workspace.lines_vertices_src.size >= needed_bytes);
		
		{
			// host-side copy into lines_vertices_src
			assert(workspace.lines_vertices_src.allocation.mapped);
			std::memcpy(workspace.lines_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);
			
			// device-side copy from lines_vertices_src -> lines_vertices
			VkBufferCopy copy_region{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = needed_bytes,
			};
			vkCmdCopyBuffer(workspace.command_buffer, workspace.lines_vertices_src.handle, workspace.lines_vertices.handle, 1, &copy_region);
		}
	}

	{ // upload cam info
		LinesPipeline::Camera camera{
			.CLIP_FROM_WORLD = CLIP_FROM_WORLD,
		};
		assert(workspace.Camera_src.size == sizeof(camera));

		// host-side copy into Camera_src
		memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));
		
		// add device-side copy from Camera_src -> Camera
		assert(workspace.Camera_src.size == workspace.Camera.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Camera_src.size,
		};

		vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &copy_region);
	}

	{ // upload world info
		assert(workspace.World_src.size == sizeof(world));

		// host-side copy into World_src
		memcpy(workspace.World_src.allocation.data(), &world, sizeof(world));

		// add device-side copy form World_src -> World
		assert(workspace.World_src.size == workspace.World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.World_src.size,
		};
		
		vkCmdCopyBuffer(workspace.command_buffer, workspace.World_src.handle, workspace.World.handle, 1, &copy_region);
	}

	if (!object_instances.empty()) {
		// [re-]allocate line bufs in needed
		size_t needed_bytes = object_instances.size() * sizeof(ObjectsPipeline::Transform);
		if (workspace.Transforms_src.handle == VK_NULL_HANDLE || workspace.Transforms_src.size < needed_bytes) {
			// round to next mult of 4k to avoid realloc continuously if vert count grows slowly
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if (workspace.Transforms_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
			}
			if (workspace.Transforms.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
			}

			workspace.Transforms_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // going to have GPU copy from this mem
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible mem, coherent (no special sync needed)
				Helpers::Mapped // get a pointer to the mem
			);
			workspace.Transforms = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as vertex buf, also going to have GPU into this mem
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // GPU-local mem
				Helpers::Unmapped // don't get a pointer to the mem
			);

			// update descriptor set
			VkDescriptorBufferInfo Transforms_info{
				.buffer = workspace.Transforms.handle,
				.offset = 0,
				.range = workspace.Transforms.size,
			};

			std::array< VkWriteDescriptorSet, 1 > writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Transforms_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Transforms_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()), writes.data(), // descriptorWrites count, data
				0, nullptr // descriptorCopies count, data
			);

			std::cout << "Re-allocated lines buffers to " << new_bytes << " bytes." << std::endl;
		}
		
		assert(workspace.Transforms_src.size == workspace.Transforms.size);
		assert(workspace.Transforms_src.size >= needed_bytes);
		
		{ // copy transforms into Transforms_src
			assert(workspace.Transforms_src.allocation.mapped);
			ObjectsPipeline::Transform *out = reinterpret_cast< ObjectsPipeline::Transform* >(workspace.Transforms_src.allocation.data()); // strict aliasing violation, but doesnt matter

			for (ObjectInstance const &inst : object_instances) {
				*out = inst.transform;
				++out;
			}
		}

		// device-side copy from lines_vertices_src -> lines_vertices
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Transforms_src.handle, workspace.Transforms.handle, 1, &copy_region);
	}

	{ // mem barrier to make sure copies complete before rendering happens
		VkMemoryBarrier memory_barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};

		vkCmdPipelineBarrier( workspace.command_buffer, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, // srcStageMask
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // dstStageMask
			0, // dependencyFlags
			1, &memory_barrier, // memoryBarriers (count, data)
			0, nullptr, // bufferMemoryBarriers (count, data)
			0, nullptr // imageMemoryBarriers (count, data)
		);
	}

	{ // render pass
		std::array< VkClearValue, 2 > clear_values {
			VkClearValue{ .color{ .float32{0.f, 0.f, 0.f, 1.f} } },
			VkClearValue{ .depthStencil{ .depth = 1.f, .stencil = 0 } },
		};

		VkRenderPassBeginInfo begin_info {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass,
			.framebuffer = framebuffer,
			.renderArea {
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			},
			.clearValueCount = uint32_t(clear_values.size()),
			.pClearValues = clear_values.data(),
		};

		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	
		float swap_aspect = (float)(rtg.swapchain_extent.width) / (float)(rtg.swapchain_extent.height);
		float camera_aspect = 1.77778f;
		float width = (float)(rtg.swapchain_extent.width), height = (float)(rtg.swapchain_extent.height), x = 0, y = 0;
		if (swap_aspect > camera_aspect) { // set scissor rect
			width = rtg.swapchain_extent.width * (camera_aspect / swap_aspect);
			height = float(rtg.swapchain_extent.height);
			x = ((float)rtg.swapchain_extent.width - width) * .5f;
			y = 0.f;
		} else {
			width = float(rtg.swapchain_extent.width);
			height = rtg.swapchain_extent.height * (swap_aspect / camera_aspect);
			x = 0.f;
		 	y = ((float)rtg.swapchain_extent.height - height) * .5f;
		}

		{
			VkRect2D scissor {
				.offset = {.x = (int)x, .y = (int)y},
				.extent = { (uint32_t)width, (uint32_t)height },
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}		

		{ // configure viewport transform
			VkViewport viewport {
				.x = x,
				.y = y,
				.width = width,
				.height = height,
				.minDepth = 0.f,
				.maxDepth = 1.f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
		}

		{ // draw w background pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
			
			{ // push time
				BackgroundPipeline::Push push {
					.time = float(time)
				};
				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}
			
			vkCmdDraw(workspace.command_buffer, 3, 1, 0 ,0);
		}

		if (camera_mode == CameraMode::Debug) { // draw w lines pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);
			{ // use lines_vertices (offset 0) as vert buffer binding 0
				std::array< VkBuffer, 1 > vertex_buffers{ workspace.lines_vertices.handle };
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind Camera descriptor set
				std::array< VkDescriptorSet, 1 > descriptor_sets{
					workspace.Camera_descriptors, // 0: Camera
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					lines_pipeline.layout, // pipeline layout
					0, // first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor sets count, ptr
					0, nullptr // dynamic sets count, ptr
				);
			}

			// draw lines vertices
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		for (auto &[object_vertices, instances] : drawables) { // draw w objects pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);

			{ // use object_vertices (offset 0) as vert buf binding 0
				std::array< VkBuffer, 1 > vertex_buffers{ object_vertices->handle };
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind World, Transforms, Camera descriptor sets
				std::array< VkDescriptorSet, 3 > descriptor_sets {
					workspace.World_descriptors, // 0: World
					workspace.Transforms_descriptors, // 1: Transforms
					workspace.Camera_descriptors, // 2: Camera
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // cmd buf
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					objects_pipeline.layout, // pipeline layout
					0, // first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);
			}

			// cam descriptor set still bound but unused

			// draw all instances
			for (auto &inst : instances) {
				uint32_t index = (uint32_t)(inst - &object_instances[0]);
				
				// bind texture descriptor set
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // cmd buf
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					objects_pipeline.layout, // pipeline layout
					3, // third set
					1, &texture_descriptors[inst->material.albedo_id >= 0 ? inst->material.albedo_id : 0], // descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);

				// bind envemap descriptor set
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // cmd buf
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					objects_pipeline.layout, // pipeline layout
					4, // fourth set
					1, &env_map_descriptor, // descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);

				// bind normal_map descriptor set
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // cmd buf
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					objects_pipeline.layout, // pipeline layout
					5, // fifth set
					1, &texture_descriptors[inst->material.normal_id >= 0 ? inst->material.normal_id : 0], // descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);

				// bind roughness_map descriptor set
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // cmd buf
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					objects_pipeline.layout, // pipeline layout
					6, // sixth set
					1, &texture_descriptors[inst->material.roughness_id>= int32_t(0) ? inst->material.roughness_id: int32_t(0)], // descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);

				// bind metalness_map descriptor set
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // cmd buf
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					objects_pipeline.layout, // pipeline layout
					7, // seventh set
					1, &texture_descriptors[inst->material.metalness_id >= int32_t(0) ? inst->material.metalness_id: int32_t(0)], // descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);

				ObjectsPipeline::Constants constants = {
					.material_type = inst->material.type,
					.exposure = rtg.configuration.exposure,
					.tonemap = rtg.configuration.tonemap,
					.has_normal_map = inst->material.normal_id >= int32_t(0) ? int32_t(1) : int32_t(0),
					.has_roughness_map = inst->material.roughness_id >= int32_t(0) ? int32_t(1) : int32_t(0),
					.has_metalness_map = inst->material.metalness_id >= int32_t(0) ? int32_t(1) : int32_t(0),
					.mip_levels = mip_levels,
				};

				// push constants type
				vkCmdPushConstants(workspace.command_buffer, objects_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectsPipeline::Constants), &constants);

				vkCmdDraw(workspace.command_buffer, inst->vertices.count, 1, inst->vertices.first, index);
			}
		}

		vkCmdEndRenderPass(workspace.command_buffer);
	}

	//end rec
	VK ( vkEndCommandBuffer(workspace.command_buffer) );

	{ //submit `workspace.command buffer` for the GPU to run:
		std::array< VkSemaphore, 1 > wait_semaphores{
			render_params.image_available
		};
		std::array< VkPipelineStageFlags, 1 > wait_stages{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

		std::array< VkSemaphore, 1 > signal_semaphores{
			render_params.image_done
		};
		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = uint32_t(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &workspace.command_buffer,
			.signalSemaphoreCount = uint32_t(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data(),
		};

		VK( vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, render_params.workspace_available) );
	}
}


bool Viewer::cull_frustrum(glm::vec3 frustrum[8], glm::vec3 bbox[8]) {
	glm::vec3 frustrum_normals[5] = {
		glm::normalize(glm::cross(frustrum[0] - frustrum[1], frustrum[2] - frustrum[1])), // frustrum forward
		glm::normalize(glm::cross(frustrum[0] - frustrum[1], frustrum[4] - frustrum[1])), // frustrum bottom
		glm::normalize(glm::cross(frustrum[0] - frustrum[2], frustrum[4] - frustrum[2])), // frustrum left
		glm::normalize(glm::cross(frustrum[3] - frustrum[1], frustrum[6] - frustrum[1])), // frustrum right
		glm::normalize(glm::cross(frustrum[3] - frustrum[2], frustrum[6] - frustrum[2])), // frustrum top
	};

	// first check with frustrum plane normals
	for (size_t i = 0; i < 5; ++i) {
		glm::vec3 const &f = frustrum_normals[i];
		float min_f = FLT_MAX, max_f = -FLT_MAX;
		float min_b = FLT_MAX, max_b = -FLT_MAX;

		for (size_t k = 0; k < 8; ++k) {
			glm::vec3 const &v = frustrum[k];
			float d = glm::dot(f, v);
			min_f = std::min(min_f, d);
			max_f = std::max(max_f, d);
		}

		for (size_t k = 0; k < 8; ++k) {
			glm::vec3 const &v = bbox[k];
			float d = glm::dot(f, v);
			min_b = std::min(min_b, d);
			max_b = std::max(max_b, d);
		}

		if (max_f < min_b || max_b < min_f) return true;
	}

	// then with box normals == box edges
	glm::vec3 bbox_edges[3] = {
		glm::normalize(bbox[1] - bbox[0]), // bbox x normal
		glm::normalize(bbox[3] - bbox[0]), // bbox y normal
		glm::normalize(bbox[4] - bbox[0]), // bbox z normal
	};

	for (size_t i = 0; i < 3; ++i) {
		auto const &b = bbox_edges[i];
		float min_f = FLT_MAX, max_f = -FLT_MAX;
		float min_b = FLT_MAX, max_b = -FLT_MAX;

		for (size_t k = 0; k < 8; ++k) {
			glm::vec3 const &v = frustrum[k];
			float d = glm::dot(b, v);
			min_f = std::min(min_f, d);
			max_f = std::max(max_f, d);
		}

		for (size_t k = 0; k < 8; ++k) {
			glm::vec3 const &v = bbox[k];
			float d = glm::dot(b, v);
			min_b = std::min(min_b, d);
			max_b = std::max(max_b, d);
		}

		if (max_f < min_b || max_b < min_f) return true;
	}

	// then cross of frustrum edges and bbox edges
	glm::vec3 frustrum_edges[6] = {
		glm::normalize(frustrum[0] - frustrum[1]),
		glm::normalize(frustrum[2] - frustrum[1]),
		glm::normalize(frustrum[4] - frustrum[0]),
		glm::normalize(frustrum[5] - frustrum[1]),
		glm::normalize(frustrum[6] - frustrum[2]),
		glm::normalize(frustrum[7] - frustrum[3]),
	};

	for (size_t i = 0; i < 6; ++i) {
		auto const &f = frustrum_edges[i];
		for (size_t j = 0; j < 3; ++j) {
			auto const &b = bbox_edges[j];
			glm::vec3 c = glm::cross(f, b);
			glm::vec3 n = glm::length(c) < .0001f ? f : glm::normalize(c);
			float min_f = FLT_MAX, max_f = -FLT_MAX;
			float min_b = FLT_MAX, max_b = -FLT_MAX;

			for (size_t k = 0; k < 8; ++k) {
				glm::vec3 const &v = frustrum[k];
				float d = glm::dot(n, v);
				min_f = std::min(min_f, d);
				max_f = std::max(max_f, d);
			}

			for (size_t k = 0; k < 8; ++k) {
				glm::vec3 const &v = bbox[k];
				float d = glm::dot(n, v);
				min_b = std::min(min_b, d);
				max_b = std::max(max_b, d);
			}

			if (max_f < min_b || max_b < min_f) return true;
		}
	}

	return false;
};

void Viewer::update(float dt) {
	time = std::fmod(time + dt, 60.f);

	if (!pause_animation) {
		animation_time = std::clamp(animation_time + (float)animation_step * dt, 0.f, max_animation_time);
		
		for (size_t j = 0; j < s72.drivers.size(); ++j) {
			size_t i = animation_step == 1 ? j : s72.drivers.size() - 1 - j;
			auto &driver = s72.drivers[i];

			// https://cplusplus.com/reference/algorithm/upper_bound/
			auto it = std::upper_bound(driver.times.begin(), driver.times.end(), animation_time);
			int next_pos = std::min((int)std::distance(driver.times.begin(), it), (int)driver.times.size() - 1);
			int pos = std::max(next_pos - 1, 0);

			float t_a = driver.times[pos];
			float t_b = driver.times[next_pos];
			float t = pos == next_pos ? 0.f : std::clamp((animation_time - t_a) / (t_b - t_a), 0.f, 1.f);

			auto interp = [interp_mode=driver.interpolation](const glm::vec4 &a, const glm::vec4 &b, const float &t) -> glm::vec4 {
				if (interp_mode == S72::Driver::Interpolation::LINEAR) {
					return glm::mix(a, b, t);
				} else if (interp_mode == S72::Driver::Interpolation::SLERP) {
					glm::quat d = glm::slerp(glm::normalize(glm::quat(a.x, a.y, a.z, a.w)), glm::normalize(glm::quat(b.x, b.y, b.z, b.w)), t);
					return glm::vec4(d.w, d.x, d.y, d.z);
				} else if (interp_mode == S72::Driver::Interpolation::STEP) {
					return a;
				}
				return {};
			};

			if (driver.channel == S72::Driver::Channel::translation){
				glm::vec4 a = glm::vec4(driver.values[3*pos], driver.values[3*pos+1], driver.values[3*pos+2], 0);
				glm::vec4 b = glm::vec4(driver.values[3*next_pos], driver.values[3*next_pos+1], driver.values[3*next_pos+2], 0);
				driver.node.translation = interp(a, b, t);
			} else if (driver.channel == S72::Driver::Channel::rotation) {
				glm::vec4 a = glm::vec4(driver.values[4*pos], driver.values[4*pos+1], driver.values[4*pos+2], driver.values[4*pos+3]);
				glm::vec4 b = glm::vec4(driver.values[4*next_pos], driver.values[4*next_pos+1], driver.values[4*next_pos+2], driver.values[4*next_pos+3]);
				glm::vec4 r = interp(a, b, t);
				driver.node.rotation = glm::quat(r.x, r.y, r.z, r.w);
			} else if (driver.channel == S72::Driver::Channel::scale) {
				glm::vec4 a = glm::vec4(driver.values[3*pos], driver.values[3*pos+1], driver.values[3*pos+2], 0);
				glm::vec4 b = glm::vec4(driver.values[3*next_pos], driver.values[3*next_pos+1], driver.values[3*next_pos+2], 0);
				driver.node.scale = interp(a, b, t);
			}			
		}
	}

	{ // iterate through scene graph
		parse_scene_graph();
	}

	if (camera_mode == CameraMode::Scene) {
		 //camera rotating around the origin:
		auto scene_camera = selected_scene_camera->second;
		CLIP_FROM_WORLD = perspective(
			scene_camera.vfov, //vfov
			scene_camera.aspect, // rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
			scene_camera.near, //near
			scene_camera.far //far
		) * glm::inverse(scene_camera.transform.WORLD_FROM_LOCAL);
	} else if (camera_mode == CameraMode::Free || camera_mode == CameraMode::Debug) {
		OrbitCamera &user_camera = camera_mode == CameraMode::Free ? free_camera : debug_camera;
		CLIP_FROM_WORLD = perspective(
			user_camera.fov,
			1.7778f, // aspect
			user_camera.near,
			user_camera.far
		) * orbit(
			user_camera.target_x, user_camera.target_y, user_camera.target_z,
			user_camera.azimuth, user_camera.elevation, user_camera.radius
		);
	} else {
		assert(0 && "only three camera modes");
	}

	if (camera_mode == CameraMode::Debug) {
		if (last_mode == CameraMode::Free) {
			OrbitCamera &user_camera = free_camera;
			debug_CLIP_FROM_WORLD = perspective(
				user_camera.fov,
				1.7778f, // aspect
				user_camera.near,
				user_camera.far
			) * orbit(
				user_camera.target_x, user_camera.target_y, user_camera.target_z,
				user_camera.azimuth, user_camera.elevation, user_camera.radius
			);
		} else if (last_mode == CameraMode::Scene) {
			auto scene_camera = selected_scene_camera->second;
			debug_CLIP_FROM_WORLD = perspective(
				scene_camera.vfov, //vfov
				scene_camera.aspect, // rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
				scene_camera.near, //near
				scene_camera.far //far
			) * glm::inverse(scene_camera.transform.WORLD_FROM_LOCAL);
		}
	}
	
	{ // compile objects to draw -- objects to be pruned here

		// clear drawables
		lines_vertices.clear(); 
		drawables.clear();

		glm::mat4 WORLD_FROM_CLIP = (camera_mode == CameraMode::Debug) ? 
			glm::inverse(debug_CLIP_FROM_WORLD) : glm::inverse(CLIP_FROM_WORLD);
		
		for (auto &inst : object_instances) {
			inst.transform.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * inst.transform.WORLD_FROM_LOCAL;

			glm::mat4 WORLD_FROM_LOCAL = inst.transform.WORLD_FROM_LOCAL;
			Viewer::AABB aabb = aabbs[inst.buffer];
			glm::vec3 bbox[8] = {
				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.min.x, aabb.min.y, aabb.min.z, 1.f }),
				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.max.x, aabb.min.y, aabb.min.z, 1.f }),
				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.max.x, aabb.max.y, aabb.min.z, 1.f }),
				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.min.x, aabb.max.y, aabb.min.z, 1.f }),

				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.min.x, aabb.min.y, aabb.max.z, 1.f }),
				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.max.x, aabb.min.y, aabb.max.z, 1.f }),
				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.max.x, aabb.max.y, aabb.max.z, 1.f }),
				glm::vec3(WORLD_FROM_LOCAL * glm::vec4{ aabb.min.x, aabb.max.y, aabb.max.z, 1.f }),
			};

			// frustrum in clip space transformed to world
			glm::vec4 frustrum4[8] = {
				WORLD_FROM_CLIP * glm::vec4{ -1.f, -1.f, 0.f,  1.f }, // near bl
				WORLD_FROM_CLIP * glm::vec4{  1.f, -1.f, 0.f,  1.f }, // near br
				WORLD_FROM_CLIP * glm::vec4{  1.f,  1.f, 0.f,  1.f }, // near tl
				WORLD_FROM_CLIP * glm::vec4{ -1.f,  1.f, 0.f,  1.f }, // near tr

				WORLD_FROM_CLIP * glm::vec4{ -1.f, -1.f,  1.f,  1.f }, // far bl
				WORLD_FROM_CLIP * glm::vec4{  1.f, -1.f,  1.f,  1.f }, // far br
				WORLD_FROM_CLIP * glm::vec4{  1.f,  1.f,  1.f,  1.f }, // far tl
				WORLD_FROM_CLIP * glm::vec4{ -1.f,  1.f,  1.f,  1.f }, // far tr
			};

			glm::vec3 frustrum[8];
			for (size_t i = 0; i < 8; ++i) {
				frustrum[i] = glm::vec3(frustrum4[i]) / frustrum4[i].w;
			}

			bool cull = false;
			if (rtg.configuration.culling_mode == "frustrum") {
				cull = cull_frustrum(frustrum, bbox);
			}

			if (rtg.configuration.debug && camera_mode == CameraMode::Debug) {
				glm::u8vec4 bbox_color = glm::u8vec4(0, 255, 0, 255);
				if (cull) {
					// switch to red color if culled
					bbox_color = glm::u8vec4(255, 0, 0, 255);
				}

				{ // draw bbox for debug
					// draw bottom face
					for (size_t i = 0; i < 4; ++i) {
						lines_vertices.emplace_back(PosColVertex{
							.Position = bbox[i],
							.Color = bbox_color
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position = bbox[(i+1) % 4],
							.Color = bbox_color
						});
					}

					// draw top face
					for (size_t i = 0; i < 4; ++i) {
						lines_vertices.emplace_back(PosColVertex{
							.Position = bbox[4 + i],
							.Color = bbox_color
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position = bbox[4 + (i+1) % 4],
							.Color = bbox_color
						});
					}

					// connect faces
					for (size_t i = 0; i < 4; ++i) {
						lines_vertices.emplace_back(PosColVertex{
							.Position = bbox[i],
							.Color = bbox_color
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position = bbox[i+4],
							.Color = bbox_color
						});
					}
				}
			}

			if (!cull) drawables[inst.buffer].emplace_back(&inst);
		}
	}

	if (camera_mode == CameraMode::Debug) {

		{ // draw frustrum
			glm::mat4 debug_WORLD_FROM_CLIP = glm::inverse(debug_CLIP_FROM_WORLD);
			// frustrum in clip space transformed to world
			glm::vec4 frustrum4[8] = {
				debug_WORLD_FROM_CLIP * glm::vec4{ -1.f, -1.f, 0.f,  1.f }, // near bl
				debug_WORLD_FROM_CLIP * glm::vec4{  1.f, -1.f, 0.f,  1.f }, // near br
				debug_WORLD_FROM_CLIP * glm::vec4{  1.f,  1.f, 0.f,  1.f }, // near tl
				debug_WORLD_FROM_CLIP * glm::vec4{ -1.f,  1.f, 0.f,  1.f }, // near tr

				debug_WORLD_FROM_CLIP * glm::vec4{ -1.f, -1.f,  1.f,  1.f }, // far bl
				debug_WORLD_FROM_CLIP * glm::vec4{  1.f, -1.f,  1.f,  1.f }, // far br
				debug_WORLD_FROM_CLIP * glm::vec4{  1.f,  1.f,  1.f,  1.f }, // far tl
				debug_WORLD_FROM_CLIP * glm::vec4{ -1.f,  1.f,  1.f,  1.f }, // far tr
			};

			glm::vec3 frustrum[8];
			for (size_t i = 0; i < 8; ++i) {
				frustrum[i] = glm::vec3(frustrum4[i]) / frustrum4[i].w;
			}

			glm::u8vec4 frustrum_color = glm::u8vec4(255, 255, 0, 255);

			// draw near plane
			for (size_t i = 0; i < 4; ++i) {
				lines_vertices.emplace_back(PosColVertex{
					.Position = frustrum[i],
					.Color = frustrum_color
				});
				lines_vertices.emplace_back(PosColVertex{
					.Position = frustrum[(i+1) % 4],
					.Color = frustrum_color
				});
			}

			// draw far plane
			for (size_t i = 0; i < 4; ++i) {
				lines_vertices.emplace_back(PosColVertex{
					.Position = frustrum[4 + i],
					.Color = frustrum_color
				});
				lines_vertices.emplace_back(PosColVertex{
					.Position = frustrum[4 + (i+1) % 4],
					.Color = frustrum_color
				});
			}

			// connect planes
			for (size_t i = 0; i < 4; ++i) {
				lines_vertices.emplace_back(PosColVertex{
					.Position = frustrum[i],
					.Color = frustrum_color
				});
				lines_vertices.emplace_back(PosColVertex{
					.Position = frustrum[i+4],
					.Color = frustrum_color
				});
			}
		}
	}
}


void Viewer::on_input(InputEvent const &evt) {
	// if current actions it get input prio
	if (action) {
		action(evt);
		return;
	}

	// general ctrls
	if (evt.type == InputEvent::KeyDown) {
		if (evt.key.key == GLFW_KEY_TAB && camera_mode != CameraMode::Debug && scene_cameras.size() > 0) {
			// switch cam modes
			int dir = (evt.key.mods & GLFW_MOD_SHIFT) ? -1 : 1;
			camera_mode = CameraMode((int(camera_mode) + dir + 2) % 2);
			return;
		} else if (evt.key.key == GLFW_KEY_F3) {
			// switch to debug, and save previous or switch back
			if (camera_mode == CameraMode::Debug) {
				camera_mode = last_mode;
				last_mode = CameraMode::Debug;
			} else {
				last_mode = camera_mode;
				camera_mode = CameraMode::Debug;
			}
		} else if (evt.key.key == GLFW_KEY_SPACE) {
			pause_animation = !pause_animation;
		} else if (!pause_animation && evt.key.key == GLFW_KEY_LEFT_ALT) {
			animation_step = -animation_step;
			pause_animation = false;
		}
	}

	// free cam ctrls
	if (camera_mode == CameraMode::Free || camera_mode == CameraMode::Debug) {
		if (evt.type == InputEvent::MouseWheel) {
			OrbitCamera &user_camera = camera_mode == CameraMode::Free ? free_camera : debug_camera;
			// change dist by 10% every scroll click
			user_camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);

			// make sure cam isnt too close or too far from target
			user_camera.radius = std::max(user_camera.radius, .5f * user_camera.near);
			user_camera.radius = std::min(user_camera.radius, 2.f * user_camera.far);
			return;
		}

		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
			// start panning
			OrbitCamera &user_camera = camera_mode == CameraMode::Free ? free_camera : debug_camera;
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = user_camera;

			action = [this,init_x,init_y,init_camera,&user_camera](InputEvent const &evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel
					action = nullptr;
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					float height = 2.f * std::tan(user_camera.fov * .5f) * user_camera.radius;

					// motion at target
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height;
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height * height; // negated b/c glfw y-down coords

					// compute cam transform to extract right and up
					glm::mat4 camera_from_world = orbit(
						init_camera.target_x, init_camera.target_y, init_camera.target_z,
						init_camera.azimuth, init_camera.elevation, init_camera.radius
					);

					// move desired distance
					user_camera.target_x = init_camera.target_x - dx * camera_from_world[0][0] - dy * camera_from_world[0][1];
					user_camera.target_y = init_camera.target_y - dx * camera_from_world[1][0] - dy * camera_from_world[1][1];
					user_camera.target_z = init_camera.target_z - dx * camera_from_world[2][0] - dy * camera_from_world[2][1];
					return;
				}
			};
			return;
		}

		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
			// start tumble
			OrbitCamera &user_camera = camera_mode == CameraMode::Free ? free_camera : debug_camera;

			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = user_camera;

			action = [this,init_x,init_y,init_camera,&user_camera](InputEvent const &evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel upon button lifted
					action = nullptr;
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height;
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height; // b/c glfw uses y-down coords

					// rotate cam based on motion
					float speed = float(M_PI); // how much rotation happens at one full window height
					float flip_x = (std::abs(init_camera.elevation) > .5f * float(M_PI ? -1.f : 1.f));
					user_camera.azimuth = init_camera.azimuth - dx * speed * flip_x;
					user_camera.elevation = init_camera.elevation - dy * speed;

					// reduce azimuth and elevation to [-pi, pi] range
					const float twopi = 2.f * float(M_PI);
					user_camera.azimuth -= std::round(user_camera.azimuth / twopi) * twopi;
					user_camera.elevation -= std::round(user_camera.elevation / twopi) * twopi;
					return;
				}
			};
			return;
		}
	} else if (camera_mode == CameraMode::Scene) {
		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (selected_scene_camera == scene_cameras.begin()) {
				selected_scene_camera = scene_cameras.end();
			}
			--selected_scene_camera;
		} else 	if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_RIGHT) {
			selected_scene_camera++;
			if (selected_scene_camera == scene_cameras.end()) {
				selected_scene_camera = scene_cameras.begin();
			}
		}
	}
}
