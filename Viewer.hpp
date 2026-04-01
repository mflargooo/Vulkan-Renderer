#pragma once

#include "PosColVertex.hpp"
#include "PosNorTexVertex.hpp"
#include "PosNorTanTexVertex.hpp"

#include "RTG.hpp"

#include <s72/S72.hpp>

struct Viewer : RTG::Application {

	Viewer(RTG &);
	Viewer(Viewer const &) = delete; //you shouldn't be copying this object
	~Viewer();

	//kept for use in destructor:
	RTG &rtg;

	//--------------------------------------------------------------------
	//Resources that last the lifetime of the application:

	//chosen format for depth buffer:
	VkFormat depth_format{};
	//Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;
	VkRenderPass shadow_render_pass = VK_NULL_HANDLE;

	//Pipelines:
	struct BackgroundPipeline {
		// no descriptor set layouts

		struct Push {
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// no vertex bindings

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} background_pipeline;

	struct LinesPipeline {
		// descriptor set layouts
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		// types for descriptors
		struct Camera {
			glm::mat4 CLIP_FROM_WORLD;
		};
		static_assert(sizeof(Camera) == 16*4, "camera buffer structure is packed.");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} lines_pipeline;

	struct ObjectsPipeline {
		// descriptor set layouts
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_Camera = VK_NULL_HANDLE;
		VkDescriptorSetLayout set3_TEXTURE = VK_NULL_HANDLE;
		VkDescriptorSetLayout set4_ENV_MAP = VK_NULL_HANDLE;
		VkDescriptorSetLayout set5_NORMAL_MAP = VK_NULL_HANDLE;
		VkDescriptorSetLayout set6_ROUGHNESS_MAP = VK_NULL_HANDLE;
		VkDescriptorSetLayout set7_METALNESS_MAP = VK_NULL_HANDLE;
		VkDescriptorSetLayout set8_Lights = VK_NULL_HANDLE;
		VkDescriptorSetLayout set9_Shadows = VK_NULL_HANDLE;

		// types for descriptors
		struct World {
			struct { float x, y, z, padding_; } SKY_DIRECTION;
			struct { float r, g, b, padding_; } SKY_ENERGY;
			struct { float x, y, z, padding_; } SUN_DIRECTION;
			struct { float r, g, b, padding_; } SUN_ENERGY;
		};
		static_assert(sizeof(World) == 4*4 + 4*4 + 4*4 + 4*4, "World is the expected size.");
		
		struct Transform {
			glm::mat4 CLIP_FROM_LOCAL;
			glm::mat4 WORLD_FROM_LOCAL;
			glm::mat4 WORLD_FROM_LOCAL_NORMAL;
		};
		static_assert(sizeof(Transform) == 16*4 + 16*4 + 16*4, "Transform is the expected size.");

		struct Light { // light structure for all light types
			glm::mat4 CLIP_FROM_WORLD;
			glm::mat4 WORLD_FROM_LIGHT;
			glm::vec3 energy;
			float limit_inv;
			float radius;
			float cos_outer;
			float cos_inner;
			uint32_t flags; // flags & 0b11 yield the type of light, flags & b100 == 1 iff the light has a shadow map

			static void pack_light(Light &light, S72::Light &s72_light);
		};
		static_assert(sizeof(Light) == (4*4*4)*2+4*3+4+4+4+4+4, "Viewer::Light is packed");

		struct Constants {
			uint32_t material_type;
			float exposure;
			int32_t tonemap;
			int32_t has_normal_map;
			int32_t has_roughness_map;
			int32_t has_metalness_map;
			uint32_t mip_levels;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosNorTanTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} objects_pipeline;

	struct TerrainPipeline {
		VkDescriptorSetLayout set0_Terrain = VK_NULL_HANDLE;

		struct Constants {
			glm::uvec3 chunk_center;
			uint32_t chunk_id;
		};
		static_assert(sizeof(Constants) == 4+4*3, "TerrainPipeline::Constants is packed");

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosNorTanTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &);
		void destroy(RTG &);
	} terrain_pipeline;

	struct ShadowPipeline {
		VkDescriptorSetLayout set0_Transforms = VK_NULL_HANDLE;

		struct Constants {
			glm::mat4 LIGHT_CLIP_FROM_WORLD;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;
		
		using Vertex = PosNorTanTexVertex;
		
		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} shadow_pipeline;

	//pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	//workspaces hold per-render resources:
	struct Workspace {
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; //from the command pool above; reset at the start of every render.

		// location for lines data (streamed to GPU per frame)
		Helpers::AllocatedBuffer lines_vertices_src; // host coherent; mapped
		Helpers::AllocatedBuffer lines_vertices; // device-local

		// location for LinesPipeline::Camera data (streamed to GPU per frame)
		Helpers::AllocatedBuffer Camera_src; // host coherent; mapped
		Helpers::AllocatedBuffer Camera; // device-local
		VkDescriptorSet Camera_descriptors; // references Camera
		
		// location for ObjectsPipeline::World data (streamed to GPU per frame)
		Helpers::AllocatedBuffer World_src; // host coherent; mapped
		Helpers::AllocatedBuffer World; // device-local
		VkDescriptorSet World_descriptors; // references World

		// location for ObjectsPipeline::Transforms data (streamed to GPU per frame)
		Helpers::AllocatedBuffer Transforms_src; // host coherent; mapped
		Helpers::AllocatedBuffer Transforms; // device-local
		VkDescriptorSet Transforms_descriptors; // references Transforms

		// location for ObjectPipeline::Lights data (streamed to GPU per frame)
		Helpers::AllocatedBuffer Lights_src;
		Helpers::AllocatedBuffer Lights;
		VkDescriptorSet Lights_descriptors;
	};
	std::vector< Workspace > workspaces; 

	//-------------------------------------------------------------------
	//static scene resources:

	// Helpers::AllocatedBuffer object_vertices;

	struct ObjectVertices {
		uint32_t first = 0;
		uint32_t count = 0;
	};
	ObjectVertices plane_vertices;
	ObjectVertices torus_vertices;
	ObjectVertices sphere_vertices;

	std::vector< Helpers::AllocatedImage > textures;
	std::vector< VkImageView > texture_views;
	VkSampler texture_sampler = VK_NULL_HANDLE;
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
	std::vector< VkDescriptorSet > texture_descriptors; // allocated from texture_descriptor_pool

	uint32_t mip_levels = 0; // leverages at most 1 cubemap exists
	std::vector< Helpers::AllocatedImage > env_maps;
	std::vector< VkImageView > env_map_views;
	std::vector< VkSampler > env_map_samplers;
	VkDescriptorPool env_map_descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSet env_map_descriptor = VK_NULL_HANDLE; // allocated from cubemap_descriptor_pool

	// normal maps are stored in textures map

	//--------------------------------------------------------------------
	//Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector< VkFramebuffer > swapchain_framebuffers;
	//used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	//Resources that change when time passes or the user interacts:

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;

	// modal action, intercepts inputs
	std::function< void(InputEvent const &) > action;
	
	float time = 0.f;

	// selecting between cams
	enum class CameraMode {
		Scene = 0,
		Free = 1,
		Debug = 2,
	} last_mode = CameraMode::Debug, camera_mode = CameraMode(rtg.configuration.camera.compare("") == 0);

	struct OrbitCamera { // used when camera_mode == CameraMode::Free
		float target_x = 0.f, target_y = 0.f, target_z = 0.f; // cam looking + orbiting
		float radius = 2.f; // dist from cam to target
		float azimuth = -float(M_PI) / 2.0f; // ccw angle around z between x and cam dir (rads)
		float elevation = 0.f; // angle from xy plane to cam dir (rads)

		float fov = 60.f / 180.f * float(M_PI); // vfov (rad)
		float near = .1f; // near clip
		float far = 1000.f; // far clip
	} free_camera, debug_camera;

	struct Camera {
		ObjectsPipeline::Transform transform;
		float aspect;
		float vfov;
		float near;
		float far;
	};
	std::map< std::string, Camera > scene_cameras;
	std::map< std::string, Camera >::iterator selected_scene_camera;
	
	glm::mat4 debug_CLIP_FROM_WORLD;
	glm::mat4 CLIP_FROM_WORLD;
	float aspect_ratio = 1.7778f;

	std::vector< LinesPipeline::Vertex > lines_vertices;

	ObjectsPipeline::World world;

	S72 s72;

	struct AABB {
		glm::vec3 min;
		glm::vec3 max;
	};
	std::unordered_map< Helpers::AllocatedBuffer *, AABB > aabbs;

	struct Material {
		uint32_t type = 0;
		int32_t albedo_id = -1; // may not exists
		int32_t normal_id = -1; // may not exist
		int32_t displacement_id = -1; // may not exist
		int32_t roughness_id = -1; // may not exist
		int32_t metalness_id = -1; // may not exist
	};
	std::unordered_map< std::string, Material > materials;

	struct ObjectInstance {
		ObjectVertices vertices;
		Helpers::AllocatedBuffer *buffer = nullptr;
		ObjectsPipeline::Transform transform;
		Material material;
	};
	std::vector< ObjectInstance > object_instances;
	std::vector< ObjectInstance > chunk_instances;

	std::unordered_map< std::string, ObjectsPipeline::Light > lights;
	std::vector< ObjectsPipeline::Light > light_instances;
	std::vector< Helpers::AllocatedImage > shadow_map_instances;
	std::vector< VkImageView > shadow_map_views;
	std::vector< VkFramebuffer > shadow_map_fbs;
	VkSampler shadow_map_sampler  = VK_NULL_HANDLE;
	VkDescriptorPool shadow_map_descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSet shadow_map_descriptor = VK_NULL_HANDLE;

	// DataFile path -> buffer holding data obtained from DataFile path
	std::unordered_map< std::string, Helpers::AllocatedBuffer > scene_vertices;
	
	std::unordered_map < Helpers::AllocatedBuffer *, std::vector < ObjectInstance * > > drawables;

	// texture path -> *_view id
	std::unordered_map< std::string, uint32_t > tex_ids;
	std::unordered_map< std::string, uint32_t > cubemap_ids;

	struct Terrain {
		glm::uvec3 grid_dims = glm::uvec3(0); // grid of chunks
		glm::uvec3 chunk_dims = glm::uvec3(0);

		// Terrain vertices, 16 per voxel
		struct Chunk {
			ObjectsPipeline::Transform transform; // transforms chunk-local to work space
			Helpers::AllocatedBuffer vertices; // local vertex positions

			// Holds VkDrawIndirectCommand info per voxel for indirect draw: https://vkguide.dev/docs/gpudriven/draw_indirect/
			Helpers::AllocatedBuffer vertices_per_voxel; 

			VkDescriptorSet descriptor = VK_NULL_HANDLE;
		};
		std::vector< Chunk > chunks;

		using Vertex = PosNorTanTexVertex;

		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		Helpers::AllocatedBuffer Transforms_src;
		Helpers::AllocatedBuffer Transforms;
		VkDescriptorSet Transforms_descriptors = VK_NULL_HANDLE;
	} terrain;

	int load_scene(const std::string &file);
	void parse_scene_graph();

	bool cull_frustrum(glm::vec3 frustrum[8], glm::vec3 bbox[8]);

	float animation_time = 0.f;
	float max_animation_time = 0.f;
	int animation_step = 1;
	bool pause_animation = false;

	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
