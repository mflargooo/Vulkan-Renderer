#include "Tutorial.hpp"

#include "VK.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb/stb_image.h"

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
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
				.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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

	{ // create obj vertices
		std::vector< PosNorTexVertex > vertices;

		{
			plane_vertices.first = uint32_t(vertices.size());
			// [-1,1]x[-1,1]x{0} quad
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.f, .y = -1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 0.f, .t = 0.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.f, .y = -1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 1.f, .t = 0.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.f, .y = 1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 0.f, .t = 1.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.f, .y = 1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 1.f, .t = 1.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.f, .y = 1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 0.f, .t = 1.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.f, .y = -1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 1.f, .t = 0.f },
			});

			plane_vertices.count = uint32_t(vertices.size() - plane_vertices.first);
		}

		constexpr float Scale = 10.f;
		{
			torus_vertices.first = uint32_t(vertices.size());

			// (u, v) parameterization with
			// - u is angle around main axis (+z)
			// - v is angle around tube

			constexpr float R1 = .075f * Scale; // main radius
			constexpr float R2 = .015f * Scale; // tube radius

			constexpr uint32_t U_STEPS = 20;
			constexpr uint32_t V_STEPS = 16;

			// texture repeats around torus
			constexpr float V_REPEATS = 1.f;
			constexpr float U_REPEATS = int(V_REPEATS / R2 * R1 + .999f); // approx sqr rounded up

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				// convert steps to angles
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.f * float (M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.f * float (M_PI);

				vertices.emplace_back( PosNorTexVertex{
					.Position {
						.x = (R1 + R2 * std::cos(va)) * std::cos(ua),
						.y = (R1 + R2 * std::cos(va)) * std::sin(ua),
						.z = R2 * std::sin(va),
					},
					.Normal {
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord {
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui, vi+1);

					emplace_vertex(ui, vi+1);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui+1, vi+1);

				}
			}

			torus_vertices.count = uint32_t(vertices.size() - torus_vertices.first);
		}

		{
			sphere_vertices.first = uint32_t(vertices.size());

			constexpr float A = .025f * Scale;
			constexpr float B = .025f * Scale;
			constexpr float C = .025f * Scale;
			constexpr float C_x = 0.f;
			constexpr float C_y = 0.f;
			constexpr float C_z = 0.f;

			constexpr uint32_t V_STEPS = 40; // phi
			constexpr uint32_t U_STEPS = 2 * V_STEPS; // theta

			constexpr float V_REPEATS = 1.f;
			constexpr float U_REPEATS = 1.f * V_REPEATS;

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * float(M_PI) * 2.f;

				vertices.emplace_back( PosNorTexVertex{
					.Position {
						.x = A * std::sin(va) * std::cos(ua) + C_x,
						.y = B * std::sin(va) * std::sin(ua) + C_y,
						.z = C * std::cos(va) + C_z,
					},
					.Normal {
						.x = std::sin(va) * std::cos(ua),
						.y = std::sin(va) * std::sin(ua),
						.z = std::cos(va),
					},
					.TexCoord {
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui, vi+1);

					emplace_vertex(ui, vi+1);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui+1, vi+1);
				}
			}

			sphere_vertices.count = uint32_t(vertices.size() - sphere_vertices.first);
		}

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		// copy data to buf
		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	{ // make some textures
		{ // tex 0 is dark / light grey checker with red origin
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

		{ // tex 1 will be classic 'xor' tex
			uint32_t size = 256;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					uint8_t r = uint8_t(x) ^ uint8_t(y);
					uint8_t g = uint8_t(x + 128) ^ uint8_t(y);
					uint8_t b = uint8_t(x) ^ uint8_t(y + 27);
					uint8_t a = 0xff;
					data.emplace_back( uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24) );
				}
			}
			assert(data.size() == size * size);

			// make a place for the tex to live on GPU
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size, .height = size }, // size of img
				VK_FORMAT_R8G8B8A8_SRGB, // how to interpret image data
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
				Helpers::Unmapped
			));

			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}
	}

	{ // load earth textures from image
		int x = 0, y = 0, c = 0;
		stbi_set_flip_vertically_on_load(true);
		uint32_t *tex = (uint32_t *)stbi_load("textures/earth.jpg", &x, &y, &c, 4);

		if (tex) {
			std::cout << "Image loaded!" << std::endl;
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = (uint32_t)x, .height = (uint32_t)y },
				VK_FORMAT_R8G8B8A8_SRGB,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));

			rtg.helpers.transfer_to_image(tex, sizeof(tex[0]) * x * y, textures.back());

			stbi_image_free(tex);
		} else {
			std::cout << "Failed to load texture: " << stbi_failure_reason() << std::endl; 
		}
	}

	{ // load glitter textures from image
		int x = 0, y = 0, c = 0;
		stbi_set_flip_vertically_on_load(false);
		uint32_t *tex = (uint32_t *)stbi_load("textures/glitter.jpg", &x, &y, &c, 4);

		if (tex) {
			std::cout << "Image loaded!" << std::endl;
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = (uint32_t)x, .height = (uint32_t)y },
				VK_FORMAT_R8G8B8A8_SRGB,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));

			rtg.helpers.transfer_to_image(tex, sizeof(tex[0]) * x * y, textures.back());

			stbi_image_free(tex);
		} else {
			std::cout << "Failed to load texture: " << stbi_failure_reason() << std::endl; 
		}
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
			.pSetLayouts = &objects_pipeline.set2_TEXTURE,
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
}

Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

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

	rtg.helpers.destroy_buffer(std::move(object_vertices));

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

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
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

void Tutorial::destroy_framebuffers() {
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


void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params) {
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
			.CLIP_FROM_WORLD = CLIP_FROM_WORLD
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
			VkClearValue{ .color{ .float32{.5f, .25f, 1.f, 1.f} } },
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
	
		{ // set scissor rect
			VkRect2D scissor {
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}		

		{ // configure viewport transform
			VkViewport viewport {
				.x = 0.f,
				.y = 0.f,
				.width = float(rtg.swapchain_extent.width),
				.height = float(rtg.swapchain_extent.height),
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

		if (false) { // draw w lines pipeline
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

		if (!object_instances.empty()) { // draw w objects pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);

			{ // use object_vertices (offset 0) as vert buf binding 0
				std::array< VkBuffer, 1 > vertex_buffers{ object_vertices.handle };
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind World and Transforms descriptor sets
				std::array< VkDescriptorSet, 2 > descriptor_sets {
					workspace.World_descriptors, // 0: World
					workspace.Transforms_descriptors, // 1: Transforms
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
			for (ObjectInstance const &inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);

				// bind texture descriptor set
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // cmd buf
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					objects_pipeline.layout, // pipeline layout
					2, // second set
					1, &texture_descriptors[inst.texture], // descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);

				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
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


void Tutorial::update(float dt) {
	time = std::fmod(time + dt, 60.f);

	if (camera_mode == CameraMode::Scene) {
		 //camera rotating around the origin:
		float ang = float(M_PI) * 2.0f * 10.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
			60.0f * float(M_PI) / 180.0f, //vfov
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
			0.1f, //near
			1000.0f //far
		) * look_at(
			10.0f * std::cos(ang), 10.0f * std::sin(ang), 1.0f, //eye
			0.0f, 0.0f, 0.5f, //target
			0.0f, 0.0f, 1.0f //up
		);
	} else if (camera_mode == CameraMode::Free) {
		CLIP_FROM_WORLD = perspective(
			free_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), // aspect
			free_camera.near,
			free_camera.far
		) * orbit(
			free_camera.target_x, free_camera.target_y, free_camera.target_z,
			free_camera.azimuth, free_camera.elevation, free_camera.radius
		);
	} else {
		assert(0 && "only two camera modes");
	}

	{ // static sun and sky
		world.SKY_DIRECTION.x = 0.f;
		world.SKY_DIRECTION.y = 0.f;
		world.SKY_DIRECTION.z = 1.f;

		world.SKY_ENERGY.r = .1f;
		world.SKY_ENERGY.g = .1f;
		world.SKY_ENERGY.b = .2f;

		constexpr float Tilt = 23.5f * float(M_PI) / 180.f;
		world.SUN_DIRECTION.x = std::sin(time) * std::cos(Tilt);
		world.SUN_DIRECTION.y = std::cos(time);
		world.SUN_DIRECTION.z = std::sin(time) * std::sin(Tilt);

		world.SUN_ENERGY.r = 1.f;
		world.SUN_ENERGY.g = 1.f;
		world.SUN_ENERGY.b = .9f;
	}

	{ // make cylinder
		lines_vertices.clear();
		static size_t max_count = 0;
		constexpr int lines = 0;

		int side_lines = lines;
		if (side_lines < 0) side_lines = 0;

		int face_lines = 2 * lines;
		if (face_lines < 0) face_lines = 0;

		size_t count = size_t(2 * side_lines + 2 * face_lines * 2);

		if (count > max_count) max_count = count;
		lines_vertices.reserve(max_count);

		constexpr float Line_len = 3.f;
		// cylinder side_lines
		for (int i = 0; i < side_lines; ++i) {
			float angle = 2.f * (float)M_PI / (float)side_lines * i; 
			float x = std::cos(angle);
			float y = std::sin(angle);
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = Line_len *.5f},
				.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff },
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = -Line_len * .5f},
				.Color{ .r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff },
			});
		}

		for (int i = 0; i < face_lines; ++i) {
			float angle = 2.f * (float)M_PI / (float)side_lines * i; 
			float x = std::cos(angle);
			float y = std::sin(angle);

			// circle face 1
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = -Line_len * .5f},
				.Color{ .r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = -x, .y = y, .z = -Line_len * .5f},
				.Color{ .r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});

			// circle face 2
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = Line_len * .5f},
				.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = -x, .y = y, .z = Line_len * .5f},
				.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});
		}

		assert(lines_vertices.size() == count);
	}

	{ // make some objects
		object_instances.clear();

		if (false) { // plane translated +x by one unit
			mat4 WORLD_FROM_LOCAL{
				1.f, 0.f, 0.f, 0.f,
				0.f, 1.f, 0.f, 0.f,
				0.f, 0.f, 1.f, 0.f,
				0.f, 0.f, 0.f, 1.f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = plane_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
			});
		}

		constexpr size_t Width = 1;
		constexpr size_t Height = 1;
		constexpr size_t Depth = 1;
		constexpr float Spacing = 2.f;

		auto emplace_rotating_torus = [&](uint32_t w, uint32_t h, uint32_t d) {
			float x = (float(w) - float(Width) / 2.f + .5f) * Spacing;
			float y = (float(h) - float(Height) / 2.f + .5f) * Spacing;
			float z = (float(d) - float(Depth) / 2.f + .5f) * Spacing;
			float ang = time / 60.f * 2.f * float(M_PI) * 10.f;
			float ca = std::cos(ang);
			float sa = std::sin(ang);
			mat4 WORLD_FROM_LOCAL{
				ca, 0.f, -sa, 0.f,
				0.f, 1.f, 0.f, 0.f,
				sa, 0.f, ca, 0.f,
				x, y, z, 1.f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 3,
			});
		};

		auto emplace_sphere = [&](uint32_t w, uint32_t h, uint32_t d) {
			float x = (float(w) - float(Width) / 2.f + .5f) * Spacing;
			float y = (float(h) - float(Height) / 2.f + .5f) * Spacing;
			float z = (float(d) - float(Depth) / 2.f + .5f) * Spacing;
			mat4 WORLD_FROM_LOCAL{
				1.f, 0.f, 0.f, 0.f,
				0.f, 1.f, 0.f, 0.f,
				0.f, 0.f, 1.f, 0.f,
				x, y, z, 1.f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = sphere_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 2,
			});
		};

		for (uint32_t w = 0; w < Width; ++w) {
			for (uint32_t h = 0; h < Height; ++h) {
				for (uint32_t d = 0; d < Depth; ++d) {
					// emplace_rotating_torus(w, h, d);
					emplace_sphere(w, h, d);
				}
			}
		} 
	}
}


void Tutorial::on_input(InputEvent const &evt) {
	// if current actions it get input prio
	if (action) {
		action(evt);
		return;
	}

	// general ctrls
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_TAB) {
		// switch cam modes
		camera_mode = CameraMode((int(camera_mode) + 1) % 2);
		return;
	}

	// free cam ctrls
	if (camera_mode == CameraMode::Free) {
		if (evt.type == InputEvent::MouseWheel) {
			// change dist by 10% every scroll click
			free_camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);

			// make sure cam isnt too close or too far from target
			free_camera.radius = std::max(free_camera.radius, .5f * free_camera.near);
			free_camera.radius = std::min(free_camera.radius, 2.f * free_camera.far);
			return;
		}

		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
			// start panning
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = free_camera;

			action = [this,init_x,init_y,init_camera](InputEvent const &evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel
					action = nullptr;
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					float height = 2.f * std::tan(free_camera.fov * .5f) * free_camera.radius;

					// motion at target
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height;
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height * height; // negated b/c glfw y-down coords

					// compute cam transform to extract right and up
					mat4 camera_from_world = orbit(
						init_camera.target_x, init_camera.target_y, init_camera.target_z,
						init_camera.azimuth, init_camera.elevation, init_camera.radius
					);

					// move desired distance
					free_camera.target_x = init_camera.target_x - dx * camera_from_world[0] - dy * camera_from_world[1];
					free_camera.target_y = init_camera.target_y - dx * camera_from_world[4] - dy * camera_from_world[5];
					free_camera.target_z = init_camera.target_z - dx * camera_from_world[8] - dy * camera_from_world[9];
					return;
				}
			};
			return;
		}

		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
			// start tumble

			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = free_camera;

			action = [this,init_x,init_y,init_camera](InputEvent const &evt) {
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
					free_camera.azimuth = init_camera.azimuth - dx * speed * flip_x;
					free_camera.elevation = init_camera.elevation - dy * speed;

					// reduce azimuth and elevation to [-pi, pi] range
					const float twopi = 2.f * float(M_PI);
					free_camera.azimuth -= std::round(free_camera.azimuth / twopi) * twopi;
					free_camera.elevation -= std::round(free_camera.elevation / twopi) * twopi;
					return;
				}
			};
			return;
		}
	}
}
