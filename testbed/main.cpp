#include <cstdint>
#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>

#include <veekay/veekay.hpp>

#include <vulkan/vulkan_core.h>
#include <imgui.h>
#include <lodepng.h>

namespace {

constexpr uint32_t max_models = 1024;

struct Vertex {
	veekay::vec3 position;
	veekay::vec3 normal;
	veekay::vec2 uv;
	// NOTE: You can add more attributes
};

struct DirectionalLight {
	veekay::vec4 direction;
	veekay::vec4 color;
};

struct PointLight {
	veekay::vec4 position;
	veekay::vec4 color;
};

struct Spotlight {
	veekay::vec4 position;
	veekay::vec4 direction;
	veekay::vec4 color;
	veekay::vec4 cone_angles;     //inner, outer
};

struct LightingData {
	veekay::vec4 ambient_color;
	DirectionalLight directional_light;
};

struct SceneUniforms {
	veekay::mat4 view_projection;
	veekay::vec4 camera_position; 
	LightingData lighting;
};

struct ModelUniforms {
	veekay::mat4 model;
	veekay::vec4 albedo_color;
	veekay::vec4 specular_color;
	veekay::vec4 material;
};

struct Mesh{
	veekay::graphics::Buffer* vertex_buffer;
	veekay::graphics::Buffer* index_buffer;
	uint32_t indices;
};

struct Transform {
	veekay::vec3 position = {};
	veekay::vec3 scale = {1.0f, 1.0f, 1.0f};
	veekay::vec3 rotation = {};

	// NOTE: Model matrix (translation, rotation and scaling)
	veekay::mat4 matrix() const;
};

struct GameObject {
	Transform transform;

	void animate_rotate(veekay::vec3 direction, double time)
	{
		transform.rotation.x += direction.x * sin(time);
		transform.rotation.y += direction.y *sin(time);
		transform.rotation.z += direction.z *sin(time);
	}
};

struct Material
{
	veekay::vec3 albedo_color;
	veekay::vec3 specular_color = {0.5f, 0.5f, 0.5f};  // Specular color
	float shininess = 32.0f;  // Specular shininess exponent
};

Material Standart = {
	.albedo_color = veekay::vec3{0.6f, 0.6f, 0.6f},
	.specular_color = {0.5f, 0.5f, 0.5f}, // Specular color
	.shininess = 50.0f  // Specular shininess exponent
};

Material MettalicBlue = {
	.albedo_color = veekay::vec3{0.2f, 0.2f, 0.8f},
	.specular_color = veekay::vec3{0.2f, 0.2f, 0.8f},  // High specular for metallic
	.shininess = 150.0f
};

Material MateGreen = {
	.albedo_color = veekay::vec3{0.2f, 0.8f, 0.2f},
	.specular_color = veekay::vec3{0.1f, 0.1f, 0.1f},  // Low specular for matte
	.shininess = 1.0f     // Lower shininess for matte
};

Material MediumRed = {
	.albedo_color = veekay::vec3{0.8f, 0.2f, 0.2f},
	.specular_color = veekay::vec3{0.3f, 0.3f, 0.3f},  // Medium specular
	.shininess = 32.0f
};

Material YellowSun = {
	.albedo_color = veekay::vec3{1.0f, 0.9f, 0.4f},
	.specular_color = veekay::vec3{1.0f, 0.9f, 0.6f},
	.shininess = 128.0f
};

Material MercuryMat = {
	.albedo_color = veekay::vec3{0.7f, 0.7f, 0.7f},
	.specular_color = veekay::vec3{0.4f, 0.4f, 0.4f},
	.shininess = 64.0f
};

Material VenusMat = {
	.albedo_color = veekay::vec3{0.9f, 0.7f, 0.4f},
	.specular_color = veekay::vec3{0.4f, 0.3f, 0.2f},
	.shininess = 64.0f
};

Material EarthMat = {
	.albedo_color = veekay::vec3{0.2f, 0.4f, 0.9f},
	.specular_color = veekay::vec3{0.3f, 0.3f, 0.3f},
	.shininess = 96.0f
};

Material MarsMat = {
	.albedo_color = veekay::vec3{0.8f, 0.4f, 0.2f},
	.specular_color = veekay::vec3{0.3f, 0.2f, 0.1f},
	.shininess = 64.0f
};

Material JupiterMat = {
	.albedo_color = veekay::vec3{0.9f, 0.7f, 0.5f},
	.specular_color = veekay::vec3{0.5f, 0.4f, 0.3f},
	.shininess = 96.0f
};

Material SaturnMat = {
	.albedo_color = veekay::vec3{0.9f, 0.8f, 0.6f},
	.specular_color = veekay::vec3{0.5f, 0.4f, 0.2f},
	.shininess = 96.0f
};

Material UranusMat = {
	.albedo_color = veekay::vec3{0.6f, 0.8f, 0.9f},
	.specular_color = veekay::vec3{0.3f, 0.4f, 0.4f},
	.shininess = 96.0f
};

Material NeptuneMat = {
	.albedo_color = veekay::vec3{0.3f, 0.4f, 0.9f},
	.specular_color = veekay::vec3{0.2f, 0.3f, 0.6f},
	.shininess = 96.0f
};

Material MoonMat = {
	.albedo_color = veekay::vec3{0.8f, 0.8f, 0.8f},
	.specular_color = veekay::vec3{0.3f, 0.3f, 0.3f},
	.shininess = 32.0f
};

Material RingMat = {
	.albedo_color = veekay::vec3{0.8f, 0.7f, 0.6f},
	.specular_color = veekay::vec3{0.4f, 0.3f, 0.2f},
	.shininess = 32.0f
};

struct Model : GameObject {
	Mesh mesh;
	std::string title;
	Material material;
};

struct Camera {
	constexpr static float default_fov = 60.0f;
	constexpr static float default_near_plane = 0.01f;
	constexpr static float default_far_plane = 100.0f;

	veekay::vec3 position = {};
	veekay::vec3 rotation = {};

	float fov = default_fov;
	float near_plane = default_near_plane;
	float far_plane = default_far_plane;

	// NOTE: View matrix of camera (inverse of a transform)
	veekay::mat4 view() const;

	bool LookAt = true;
	veekay::vec3 target_position = {0, 0, 0};
	veekay::mat4 lookAt_view() const;

	// NOTE: View and projection composition
	veekay::mat4 view_projection(float aspect_ratio) const;
};

struct CameraState {
	veekay::vec3 position{};
	veekay::vec3 rotation{};
	veekay::vec3 target{};
};

struct Orbit {
	size_t model_index;
	size_t parent_index; // SIZE_MAX = no parent
	float a;
	float b;
	float rotation_rad;
	float omega;
	float phase;
};

// NOTE: Scene objects
inline namespace {
	Camera camera{
		.position = {0.0f, -0.5f, -5.0f},
        .rotation = {0.0f, 0.0f, 0.0f}  // x pitch = 0°, y  yaw = 0°, roll = 0° 
	};

	CameraState lookat_state{
		.position = camera.position,
		.rotation = camera.rotation,
		.target = camera.target_position,
	};

	CameraState transform_state{
		.position = camera.position,
		.rotation = camera.rotation,
		.target = camera.target_position,
	};

	bool lookat_state_valid = true;
	bool transform_state_valid = true;

	std::vector<Model> models;
	std::vector<Orbit> orbits;
}

struct Plane : Model {
	Plane(veekay::vec3 position, Material material = Standart , std::string title = "None")
	{
		std::vector<Vertex> vertices = {
			{{-5.0f, 0.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
			{{5.0f, 0.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
			{{5.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
			{{-5.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
		};

		std::vector<uint32_t> indices = {
			0, 1, 2, 2, 3, 0
		};

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.mesh = mesh;
		m.material = material,
		m.title = title;

		models.push_back(m);
	}
};

struct Cube : Model {
	Cube(veekay::vec3 position, Material material = Standart, std::string title = "None", veekay::vec3 scale = {1.f,1.f,1.f})
	{
		std::vector<Vertex> vertices = {
			{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
			{{+0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
			{{+0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
			{{-0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},

			{{+0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
			{{+0.5f, -0.5f, +0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
			{{+0.5f, +0.5f, +0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
			{{+0.5f, +0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

			{{+0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
			{{-0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
			{{-0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
			{{+0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},

			{{-0.5f, -0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
			{{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
			{{-0.5f, +0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
			{{-0.5f, +0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

			{{-0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
			{{+0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
			{{+0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
			{{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},

			{{-0.5f, +0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
			{{+0.5f, +0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
			{{+0.5f, +0.5f, +0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
			{{-0.5f, +0.5f, +0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
		};

		std::vector<uint32_t> indices = {
			0, 1, 2, 2, 3, 0,
			4, 5, 6, 6, 7, 4,
			8, 9, 10, 10, 11, 8,
			12, 13, 14, 14, 15, 12,
			16, 17, 18, 18, 19, 16,
			20, 21, 22, 22, 23, 20,
		};

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.transform.scale = scale;
		m.mesh = mesh;
		m.material = material,
		m.title = title;

		models.push_back(m);
	}
};

struct Sphere : Model {
	Sphere(veekay::vec3 position, float radius, uint32_t stacks, uint32_t slices, Material material, std::string title = "Sphere", veekay::vec3 scale = {1.f,1.f,1.f})
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t i = 0; i <= stacks; ++i) {
			float v = float(i) / float(stacks);
			float phi = v * float(M_PI);

			for (uint32_t j = 0; j <= slices; ++j) {
				float u = float(j) / float(slices);
				float theta = u * float(M_PI) * 2.0f;

				float x = std::sin(phi) * std::cos(theta);
				float y = std::cos(phi);
				float z = std::sin(phi) * std::sin(theta);

				veekay::vec3 normal = {x, y, z};
				veekay::vec3 pos = {x * radius, y * radius, z * radius};
				vertices.push_back({pos, normal, {u, v}});
			}
		}

		for (uint32_t i = 0; i < stacks; ++i) {
			for (uint32_t j = 0; j < slices; ++j) {
				uint32_t first = i * (slices + 1) + j;
				uint32_t second = first + slices + 1;

				indices.push_back(first);
				indices.push_back(second);
				indices.push_back(first + 1);

				indices.push_back(second);
				indices.push_back(second + 1);
				indices.push_back(first + 1);
			}
		}

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.transform.scale = scale;
		m.mesh = mesh;
		m.material = material,
		m.title = title;

		models.push_back(m);
	}
};

struct Ring : Model {
	Ring(veekay::vec3 position, float inner_radius, float outer_radius, uint32_t segments, Material material, std::string title = "Ring")
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t i = 0; i <= segments; ++i) {
			float u = float(i) / float(segments);
			float angle = u * float(M_PI) * 2.0f;
			float c = std::cos(angle);
			float s = std::sin(angle);

			veekay::vec3 normal = {0.0f, 1.0f, 0.0f};

			vertices.push_back({{inner_radius * c, 0.0f, inner_radius * s}, normal, {u, 0.0f}});
			vertices.push_back({{outer_radius * c, 0.0f, outer_radius * s}, normal, {u, 1.0f}});
		}

		for (uint32_t i = 0; i < segments; ++i) {
			uint32_t start = i * 2;
			indices.push_back(start);
			indices.push_back(start + 1);
			indices.push_back(start + 2);

			indices.push_back(start + 1);
			indices.push_back(start + 3);
			indices.push_back(start + 2);
		}

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.mesh = mesh;
		m.material = material,
		m.title = title;

		models.push_back(m);
	}
};

// NOTE: Vulkan objects
inline namespace {
	VkShaderModule vertex_shader_module;
	VkShaderModule fragment_shader_module;

	VkDescriptorPool descriptor_pool;
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_set;

	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;

	veekay::graphics::Buffer* scene_uniforms_buffer;
	veekay::graphics::Buffer* model_uniforms_buffer;
	veekay::graphics::Buffer* point_lights_buffer;
	veekay::graphics::Buffer* spotlights_buffer;

	veekay::graphics::Texture* missing_texture;
	VkSampler missing_texture_sampler;

	veekay::graphics::Texture* texture;
	VkSampler texture_sampler;
}

float toRadians(float degrees) {
	return degrees * float(M_PI) / 180.0f;
}

veekay::mat4 Transform::matrix() const {
    auto scale_mat = veekay::mat4::scaling(scale);
    
    auto rot_x = veekay::mat4::rotation(veekay::vec3{1.0f, 0.0f, 0.0f}, toRadians(rotation.x));
    auto rot_y = veekay::mat4::rotation(veekay::vec3{0.0f, 1.0f, 0.0f}, toRadians(rotation.y));
    auto rot_z = veekay::mat4::rotation(veekay::vec3{0.0f, 0.0f, 1.0f}, toRadians(rotation.z));
    
    auto rotation_mat = rot_z * rot_y * rot_x;
    
    auto translation_mat = veekay::mat4::translation(position);
    
	return scale_mat * rotation_mat * translation_mat;
}

veekay::mat4 Camera::view() const {
    veekay::mat4 rot_x = veekay::mat4::rotation(veekay::vec3{1.0f, 0.0f, 0.0f}, -toRadians(rotation.x));
    veekay::mat4 rot_y = veekay::mat4::rotation(veekay::vec3{0.0f, 1.0f, 0.0f}, -toRadians(rotation.y));
    veekay::mat4 rot_z = veekay::mat4::rotation(veekay::vec3{0.0f, 0.0f, 1.0f}, -toRadians(rotation.z));

    veekay::mat4 rotation_matrix = rot_y * rot_x * rot_z;
    veekay::mat4 translation_matrix = veekay::mat4::translation(-position);

    // Match existing row-major style: T^-1 then R^-1
    return translation_matrix * rotation_matrix;
}

veekay::mat4 Camera::lookAt_view() const{
    veekay::vec3 forward = target_position - position;

	float forward_len_sq = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
	if (forward_len_sq < 1e-6f) {
		forward = {0.0f, 0.0f, 1.0f};
	}
    forward = veekay::vec3::normalized(forward);

    veekay::vec3 up = {0.0f, 1.0f, 0.0f};
    veekay::vec3 right = veekay::vec3::cross(up, forward);
	float right_len_sq = right.x * right.x + right.y * right.y + right.z * right.z;
	if (right_len_sq < 1e-6f) {
		right = {1.0f, 0.0f, 0.0f};
	}
    right = veekay::vec3::normalized(right);
    up = veekay::vec3::normalized(veekay::vec3::cross(forward, right));

	veekay::mat4 r_matrix = veekay::mat4::identity();
    r_matrix.elements[0][0] = right.x;
    r_matrix.elements[1][0] = right.y;
    r_matrix.elements[2][0] = right.z;
    
    r_matrix.elements[0][1] = up.x;
    r_matrix.elements[1][1] = up.y;
    r_matrix.elements[2][1] = up.z;
    
    r_matrix.elements[0][2] = forward.x;
    r_matrix.elements[1][2] = forward.y;
    r_matrix.elements[2][2] = forward.z;

    veekay::mat4 translation_matrix = veekay::mat4::translation(-position);

	return translation_matrix * r_matrix;
}

veekay::mat4 Camera::view_projection(float aspect_ratio) const {
	auto projection = veekay::mat4::projection(fov, aspect_ratio, near_plane, far_plane);

	if(LookAt == true)
		return lookAt_view() * projection;
	else
		return view() * projection;
}

// NOTE: Loads shader byte code from file
// NOTE: Your shaders are compiled via CMake with this code too, look it up
VkShaderModule loadShaderModule(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	size_t size = file.tellg();
	std::vector<uint32_t> buffer(size / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), size);
	file.close();

	VkShaderModuleCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = buffer.data(),
	};

	VkShaderModule result;
	if (vkCreateShaderModule(veekay::app.vk_device, &
	                         info, nullptr, &result) != VK_SUCCESS) {
		return nullptr;
	}

	return result;
}

void initialize(VkCommandBuffer cmd) {
	VkDevice& device = veekay::app.vk_device;
	VkPhysicalDevice& physical_device = veekay::app.vk_physical_device;

	{ // NOTE: Build graphics pipeline
		vertex_shader_module = loadShaderModule("D:/Denis/Documents/Lab2/Lab2/shaders/shader.vert.spv");
		if (!vertex_shader_module) {
			std::cerr << "Failed to load Vulkan vertex shader from file\n";
			veekay::app.running = false;
			return;
		}

		fragment_shader_module = loadShaderModule("D:/Denis/Documents/Lab2/Lab2/shaders/shader.frag.spv");
		if (!fragment_shader_module) {
			std::cerr << "Failed to load Vulkan fragment shader from file\n";
			veekay::app.running = false;
			return;
		}

		VkPipelineShaderStageCreateInfo stage_infos[2];

		// NOTE: Vertex shader stage
		stage_infos[0] = VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader_module,
			.pName = "main",
		};

		// NOTE: Fragment shader stage
		stage_infos[1] = VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader_module,
			.pName = "main",
		};

		// NOTE: How many bytes does a vertex take?
		VkVertexInputBindingDescription buffer_binding{
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		// NOTE: Declare vertex attributes
		VkVertexInputAttributeDescription attributes[] = {
			{
				.location = 0, // NOTE: First attribute
				.binding = 0, // NOTE: First vertex buffer
				.format = VK_FORMAT_R32G32B32_SFLOAT, // NOTE: 3-component vector of floats
				.offset = offsetof(Vertex, position), // NOTE: Offset of "position" field in a Vertex struct
			},
			{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, normal),
			},
			{
				.location = 2,
				.binding = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(Vertex, uv),
			},
		};

		// NOTE: Describe inputs
		VkPipelineVertexInputStateCreateInfo input_state_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &buffer_binding,
			.vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
			.pVertexAttributeDescriptions = attributes,
		};

		// NOTE: Every three vertices make up a triangle,
		//       so our vertex buffer contains a "list of triangles"
		VkPipelineInputAssemblyStateCreateInfo assembly_state_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		};

		// NOTE: Declare clockwise triangle order as front-facing
		//       Discard triangles that are facing away
		//       Fill triangles, don't draw lines instaed
		VkPipelineRasterizationStateCreateInfo raster_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
			.lineWidth = 1.0f,
		};

		// NOTE: Use 1 sample per pixel
		VkPipelineMultisampleStateCreateInfo sample_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = false,
			.minSampleShading = 1.0f,
		};

		VkViewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(veekay::app.window_width),
			.height = static_cast<float>(veekay::app.window_height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};

		VkRect2D scissor{
			.offset = {0, 0},
			.extent = {veekay::app.window_width, veekay::app.window_height},
		};

		// NOTE: Let rasterizer draw on the entire window
		VkPipelineViewportStateCreateInfo viewport_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

			.viewportCount = 1,
			.pViewports = &viewport,

			.scissorCount = 1,
			.pScissors = &scissor,
		};

		// NOTE: Let rasterizer perform depth-testing and overwrite depth values on condition pass
		VkPipelineDepthStencilStateCreateInfo depth_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = true,
			.depthWriteEnable = true,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		};

		// NOTE: Let fragment shader write all the color channels
		VkPipelineColorBlendAttachmentState attachment_info{
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
			                  VK_COLOR_COMPONENT_G_BIT |
			                  VK_COLOR_COMPONENT_B_BIT |
			                  VK_COLOR_COMPONENT_A_BIT,
		};

		// NOTE: Let rasterizer just copy resulting pixels onto a buffer, don't blend
		VkPipelineColorBlendStateCreateInfo blend_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

			.logicOpEnable = false,
			.logicOp = VK_LOGIC_OP_COPY,

			.attachmentCount = 1,
			.pAttachments = &attachment_info
		};

		{
			VkDescriptorPoolSize pools[] = {
				{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 8,
				},
				{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.descriptorCount = 8,
				},
				{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 8,
				},
				{
					.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 8,
				}
			};
			
			VkDescriptorPoolCreateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = 1,
				.poolSizeCount = sizeof(pools) / sizeof(pools[0]),
				.pPoolSizes = pools,
			};

			if (vkCreateDescriptorPool(device, &info, nullptr,
			                           &descriptor_pool) != VK_SUCCESS) {
				std::cerr << "Failed to create Vulkan descriptor pool\n";
				veekay::app.running = false;
				return;
			}
		}

		// NOTE: Descriptor set layout specification
		{
			VkDescriptorSetLayoutBinding bindings[] = {
				{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 2,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 3,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
			};

			VkDescriptorSetLayoutCreateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = sizeof(bindings) / sizeof(bindings[0]),
				.pBindings = bindings,
			};

			if (vkCreateDescriptorSetLayout(device, &info, nullptr,
			                                &descriptor_set_layout) != VK_SUCCESS) {
				std::cerr << "Failed to create Vulkan descriptor set layout\n";
				veekay::app.running = false;
				return;
			}
		}

		{
			VkDescriptorSetAllocateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &descriptor_set_layout,
			};

			if (vkAllocateDescriptorSets(device, &info, &descriptor_set) != VK_SUCCESS) {
				std::cerr << "Failed to create Vulkan descriptor set\n";
				veekay::app.running = false;
				return;
			}
		}

		// NOTE: Declare external data sources, only push constants this time
		VkPipelineLayoutCreateInfo layout_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &descriptor_set_layout,
		};

		// NOTE: Create pipeline layout
		if (vkCreatePipelineLayout(device, &layout_info,
		                           nullptr, &pipeline_layout) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan pipeline layout\n";
			veekay::app.running = false;
			return;
		}
		
		VkGraphicsPipelineCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = 2,
			.pStages = stage_infos,
			.pVertexInputState = &input_state_info,
			.pInputAssemblyState = &assembly_state_info,
			.pViewportState = &viewport_info,
			.pRasterizationState = &raster_info,
			.pMultisampleState = &sample_info,
			.pDepthStencilState = &depth_info,
			.pColorBlendState = &blend_info,
			.layout = pipeline_layout,
			.renderPass = veekay::app.vk_render_pass,
		};

		// NOTE: Create graphics pipeline
		if (vkCreateGraphicsPipelines(device, nullptr,
		                              1, &info, nullptr, &pipeline) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan pipeline\n";
			veekay::app.running = false;
			return;
		}
	}

	scene_uniforms_buffer = new veekay::graphics::Buffer(
		sizeof(SceneUniforms),
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

	model_uniforms_buffer = new veekay::graphics::Buffer(
		max_models * veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms)),
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	
	constexpr size_t ssbo_padding = 16 - sizeof(uint32_t);

	point_lights_buffer = new veekay::graphics::Buffer(
		16 + sizeof(PointLight) * 8,  // padded count + lights
		nullptr,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	spotlights_buffer = new veekay::graphics::Buffer(
		16 + sizeof(Spotlight) * 4,  // padded count + lights
		nullptr,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	// NOTE: This texture and sampler is used when texture could not be loaded
	{
		VkSamplerCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		};

		if (vkCreateSampler(device, &info, nullptr, &missing_texture_sampler) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan texture sampler\n";
			veekay::app.running = false;
			return;
		}

		uint32_t pixels[] = {
			0xff000000, 0xffff00ff,
			0xffff00ff, 0xff000000,
		};

		missing_texture = new veekay::graphics::Texture(cmd, 2, 2,
		                                                VK_FORMAT_B8G8R8A8_UNORM,
		                                                pixels);
	}

	{
		VkDescriptorBufferInfo buffer_infos[] = {
			{
				.buffer = scene_uniforms_buffer->buffer,
				.offset = 0,
				.range = sizeof(SceneUniforms),
			},
			{
				.buffer = model_uniforms_buffer->buffer,
				.offset = 0,
				.range = sizeof(ModelUniforms),
			},
			{
				.buffer = point_lights_buffer->buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
			},
			{
				.buffer = spotlights_buffer->buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
			},
		};

		VkWriteDescriptorSet write_infos[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &buffer_infos[0],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &buffer_infos[1],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = &buffer_infos[2],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = &buffer_infos[3],
			},
		};

		vkUpdateDescriptorSets(device, sizeof(write_infos) / sizeof(write_infos[0]),
		                       write_infos, 0, nullptr);
	}

	// Scene setup: solar system
	Plane({0.0f, 0.0f, 0.0f}, Standart, "Ecliptic");

	const auto add_orbit = [&](size_t model_index, size_t parent_index, float a, float e, float rotation_deg, float period_years, float phase = 0.0f) {
		float b = a * std::sqrt(1.0f - e * e);
		float omega = float(2.0 * M_PI / period_years);
		orbits.push_back(Orbit{
			.model_index = model_index,
			.parent_index = parent_index,
			.a = a,
			.b = b,
			.rotation_rad = toRadians(rotation_deg),
			.omega = omega,
			.phase = phase
		});
	};

	auto add_planet = [&](const veekay::vec3& pos, float radius, Material mat, const std::string& name) -> size_t {
		size_t before = models.size();
		Sphere(pos, radius, 24, 36, mat, name);
		return models.size() - 1;
	};

	// Scale distances to fit the scene
	const float base_distance = 1.5f;

	// Sun
	size_t sun_idx = add_planet({0.0f, 0.0f, 0.0f}, 1.5f, YellowSun, "Sun");

	// Planets
	size_t mercury_idx = add_planet({base_distance * 2.0f, 0.0f, 0.0f}, 0.25f, MercuryMat, "Mercury");
	add_orbit(mercury_idx, SIZE_MAX, base_distance * 2.0f, 0.206f, 29.0f, 0.24f);

	size_t venus_idx = add_planet({base_distance * 3.0f, 0.0f, 0.0f}, 0.35f, VenusMat, "Venus");
	add_orbit(venus_idx, SIZE_MAX, base_distance * 3.0f, 0.007f, 77.0f, 0.62f);

	size_t earth_idx = add_planet({base_distance * 4.0f, 0.0f, 0.0f}, 0.4f, EarthMat, "Earth");
	add_orbit(earth_idx, SIZE_MAX, base_distance * 4.0f, 0.017f, 0.0f, 1.0f);

	size_t moon_idx = add_planet({base_distance * 4.6f, 0.0f, 0.0f}, 0.12f, MoonMat, "Moon");
	add_orbit(moon_idx, earth_idx, 0.7f, 0.055f, 0.0f, 0.075f);

	size_t mars_idx = add_planet({base_distance * 5.0f, 0.0f, 0.0f}, 0.3f, MarsMat, "Mars");
	add_orbit(mars_idx, SIZE_MAX, base_distance * 5.0f, 0.093f, 49.0f, 1.88f);

	size_t jupiter_idx = add_planet({base_distance * 7.0f, 0.0f, 0.0f}, 0.9f, JupiterMat, "Jupiter");
	add_orbit(jupiter_idx, SIZE_MAX, base_distance * 7.0f, 0.048f, 100.0f, 11.86f);

	size_t saturn_idx = add_planet({base_distance * 9.0f, 0.0f, 0.0f}, 0.8f, SaturnMat, "Saturn");
	add_orbit(saturn_idx, SIZE_MAX, base_distance * 9.0f, 0.054f, 113.0f, 29.46f);

	// Saturn ring
	Ring({0.0f, 0.0f, 0.0f}, 1.0f, 1.6f, 64, RingMat, "Saturn Ring");
	size_t ring_idx = models.size() - 1;
	add_orbit(ring_idx, saturn_idx, 0.0f, 0.0f, 0.0f, 1.0f); // stick to Saturn

	size_t uranus_idx = add_planet({base_distance * 11.0f, 0.0f, 0.0f}, 0.7f, UranusMat, "Uranus");
	add_orbit(uranus_idx, SIZE_MAX, base_distance * 11.0f, 0.047f, 74.0f, 84.01f);

	size_t neptune_idx = add_planet({base_distance * 13.0f, 0.0f, 0.0f}, 0.7f, NeptuneMat, "Neptune");
	add_orbit(neptune_idx, SIZE_MAX, base_distance * 13.0f, 0.009f, 131.0f, 164.79f);
}

// NOTE: Destroy resources here, do not cause leaks in your program!
void shutdown() {
	VkDevice& device = veekay::app.vk_device;

	vkDestroySampler(device, missing_texture_sampler, nullptr);
	delete missing_texture;

	for(auto model : models)
	{
		delete model.mesh.index_buffer;
		delete model.mesh.vertex_buffer;
	}

	delete spotlights_buffer;
	delete point_lights_buffer;
	delete model_uniforms_buffer;
	delete scene_uniforms_buffer;

	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
	vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyShaderModule(device, fragment_shader_module, nullptr);
	vkDestroyShaderModule(device, vertex_shader_module, nullptr);
}
namespace {
	LightingData lighting_params{};
	uint32_t point_light_count = 1;
	PointLight point_lights[8];
	uint32_t spotlight_count = 1;
	Spotlight spotlights[4];
	float orbit_speedup = 2.0f;
	veekay::vec4 sun_base_color = {2.5f, 2.3f, 1.6f, 0.0f};
	float sun_intensity = 1.0f;
}
void update(double time) {
    static int selectedModelIndex = 0;
	static bool first = false;

	ImGui::Begin("Controls:");
	ImGui::DragFloat3("Camera pos", reinterpret_cast<float *>(&camera.position));
	ImGui::DragFloat3("Camera rot", reinterpret_cast<float *>(&camera.rotation));
	ImGui::InputFloat3("Look-at target", reinterpret_cast<float *>(&camera.target_position));

	bool previousLookAt = camera.LookAt;
	ImGui::Checkbox("Look-at mode", &camera.LookAt);
	if (previousLookAt != camera.LookAt) {
		if (previousLookAt) {
			lookat_state = {
				.position = camera.position,
				.rotation = camera.rotation,
				.target = camera.target_position,
			};
			lookat_state_valid = true;

			if (transform_state_valid) {
				camera.position = transform_state.position;
				camera.rotation = transform_state.rotation;
				camera.target_position = transform_state.target;
			}
		} else {
			transform_state = {
				.position = camera.position,
				.rotation = camera.rotation,
				.target = camera.target_position,
			};
			transform_state_valid = true;

			if (lookat_state_valid) {
				camera.position = lookat_state.position;
				camera.rotation = lookat_state.rotation;
				camera.target_position = lookat_state.target;
			} else if (!models.empty()) {
				size_t clamped_index = std::min<size_t>(selectedModelIndex, models.size() - 1);
				camera.target_position = models[clamped_index].transform.position;
			}
		}
	}

	if (camera.LookAt && !models.empty()) {
        ImGui::Text("Target:");
        if (ImGui::BeginCombo("##CameraTarget", ("Model " + models[selectedModelIndex].title).c_str())) {
            for (size_t i = 0; i < models.size(); ++i) {
                bool isSelected = (selectedModelIndex == (int)i);
                if (ImGui::Selectable(("Model " + models[i].title).c_str(), isSelected)) {
                    selectedModelIndex = (int)i;
					camera.target_position = models[selectedModelIndex].transform.position;
                }
                if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
            }
            ImGui::EndCombo();
        }
	}

	ImGui::SliderFloat("Orbit speedup", &orbit_speedup, 0.1f, 50.0f, "%.1f");
	ImGui::SliderFloat("Sun intensity", &sun_intensity, 0.1f, 5.0f, "%.1f");

	ImGui::Separator();
	
	// Ambient light
	ImGui::Text("Ambient Light");
	ImGui::ColorEdit3("Ambient Color", &lighting_params.ambient_color.x);
	
	ImGui::Separator();
	
	// Point lights
	ImGui::Text("Point Lights");
	int point_count_int = int(point_light_count);
	if (ImGui::DragInt("Point Light Count", &point_count_int, 1.0f, 0, 8)) {
		point_light_count = uint32_t(std::max(0, std::min(8, point_count_int)));
	}
	for (uint32_t i = 0; i < point_light_count && i < 8; i++) {
		ImGui::PushID(int(i));
		ImGui::Text("Point Light %u", i);
		ImGui::DragFloat3("Position", &point_lights[i].position.x, 0.1f);
		ImGui::ColorEdit3("Color", &point_lights[i].color.x);
		ImGui::PopID();
	}
	
	ImGui::Separator();
	
	// Spotlights
	ImGui::Text("Spotlights");
	int spot_count_int = int(spotlight_count);
	if (ImGui::DragInt("Spotlight Count", &spot_count_int, 1.0f, 0, 4)) {
		uint32_t old_count = spotlight_count;
		spotlight_count = uint32_t(std::max(0, std::min(4, spot_count_int)));

		// Инициализируем новые источники, если количество увеличилось
		for (uint32_t i = old_count; i < spotlight_count; ++i) {
			if (i >= 4) break;

			// Устанавливаем стандартные значения
			spotlights[i].position = veekay::vec4{0.0f, 0.0f, 0.0f, 0.0f};
			spotlights[i].direction = veekay::vec4{0.0f, 0.0f, -1.0f, 0.0f}; // важное: не нулевой вектор
			spotlights[i].color = veekay::vec4{1.0f, 1.0f, 1.0f, 0.0f};
			spotlights[i].cone_angles = veekay::vec4{
				std::cos(toRadians(20.0f)),  // inner
				std::cos(toRadians(25.0f)),  // outer
				0.0f, 0.0f
			};
		}
	}
	for (uint32_t i = 0; i < spotlight_count && i < 4; i++) {
		ImGui::PushID(int(i + 8));
		ImGui::Text("Spotlight %u", i);
		ImGui::DragFloat3("Position", &spotlights[i].position.x, 0.1f);
		ImGui::DragFloat3("Direction", &spotlights[i].direction.x, 0.01f, -1.0f, 1.0f);
		ImGui::ColorEdit3("Color", &spotlights[i].color.x);
		float inner_deg = std::acos(spotlights[i].cone_angles.x) * 180.0f / float(M_PI);
		float outer_deg = std::acos(spotlights[i].cone_angles.y) * 180.0f / float(M_PI);
		if (ImGui::DragFloat("Inner Angle (deg)", &inner_deg, 1.0f, 0.0f, 90.0f)) {
			spotlights[i].cone_angles.x = std::cos(inner_deg * float(M_PI) / 180.0f);
		}
		if (ImGui::DragFloat("Outer Angle (deg)", &outer_deg, 1.0f, 0.0f, 90.0f)) {
			spotlights[i].cone_angles.y = std::cos(outer_deg * float(M_PI) / 180.0f);
		}
		ImGui::PopID();
	}
	ImGui::End();

	if (!ImGui::IsWindowHovered()) {
		using namespace veekay::input;

		if (mouse::isButtonDown(mouse::Button::left)) {
			auto view = (camera.LookAt) ? camera.lookAt_view() : camera.view();
			veekay::vec3 right = {view[0][0], view[1][0], view[2][0]};
			veekay::vec3 up    = {view[0][1], view[1][1], view[2][1]};
			veekay::vec3 front = {-view[0][2], -view[1][2], -view[2][2]};

			if (!camera.LookAt) {
				auto delta = mouse::cursorDelta();
				camera.rotation.y += delta.x * 0.2f;
    			camera.rotation.x -= delta.y * 0.2f;
			}

			if (keyboard::isKeyDown(keyboard::Key::w))
				camera.position -= front * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::s))
				camera.position += front * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::d))
				camera.position += right * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::a))
				camera.position -= right * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::q))
				camera.position -= up * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::z))
				camera.position += up * 0.1f;
		}
	}

	float aspect_ratio = float(veekay::app.window_width) / float(veekay::app.window_height);

		// создаем источники первый раз
		if (!first) {
			lighting_params.ambient_color = veekay::vec4{0.1f, 0.1f, 0.1f, 0.0f};
			
			// Point light
			point_light_count = 1;
			point_lights[0].position = veekay::vec4{0.0f, 0.0f, 0.0f, 0.0f};
			point_lights[0].color = veekay::vec4{
				sun_base_color.x * sun_intensity,
				sun_base_color.y * sun_intensity,
				sun_base_color.z * sun_intensity,
				0.0f}; // bright sun
			
			// Spotlights from above for fill
			spotlight_count = 2;
			float inner_angle_rad = toRadians(25.0f);
			float outer_angle_rad = toRadians(40.0f);

			spotlights[0].position = veekay::vec4{0.0f, 8.0f, 0.0f, 0.0f};
			veekay::vec3 spot_dir0 = veekay::vec3::normalized(veekay::vec3{0.0f, -1.0f, 0.0f});
			spotlights[0].direction = veekay::vec4{spot_dir0.x, spot_dir0.y, spot_dir0.z, 0.0f};
			spotlights[0].color = veekay::vec4{0.8f, 0.8f, 0.9f, 0.0f};
			spotlights[0].cone_angles = veekay::vec4{
				std::cos(inner_angle_rad),
				std::cos(outer_angle_rad),
				0.0f,
				0.0f
			};

			spotlights[1].position = veekay::vec4{0.0f, 8.0f, 8.0f, 0.0f};
			veekay::vec3 spot_dir1 = veekay::vec3::normalized(veekay::vec3{0.0f, -1.0f, -1.0f});
			spotlights[1].direction = veekay::vec4{spot_dir1.x, spot_dir1.y, spot_dir1.z, 0.0f};
			spotlights[1].color = veekay::vec4{0.7f, 0.7f, 0.8f, 0.0f};
			spotlights[1].cone_angles = veekay::vec4{
				std::cos(inner_angle_rad),
				std::cos(outer_angle_rad),
				0.0f,
				0.0f
			};
			
			first = true;
		}
	veekay::vec3 dir = veekay::vec3::normalized(veekay::vec3{
		lighting_params.directional_light.direction.x,
		lighting_params.directional_light.direction.y,
		lighting_params.directional_light.direction.z
	});
	lighting_params.directional_light.direction = veekay::vec4{dir.x, dir.y, dir.z, 0.0f};
	
	// Normalize spotlight directions
	for (uint32_t i = 0; i < spotlight_count && i < 4; i++) {
		veekay::vec3 spot_dir = veekay::vec3::normalized(veekay::vec3{
			spotlights[i].direction.x,
			spotlights[i].direction.y,
			spotlights[i].direction.z
		});
		spotlights[i].direction = veekay::vec4{spot_dir.x, spot_dir.y, spot_dir.z, 0.0f};
	}

	if (point_light_count > 0) {
		point_lights[0].color = veekay::vec4{
			sun_base_color.x * sun_intensity,
			sun_base_color.y * sun_intensity,
			sun_base_color.z * sun_intensity,
			0.0f};
	}

	// Update orbital positions
	double sim_years = time * (orbit_speedup * 0.1);
	for (const auto& orbit : orbits) {
		if (orbit.model_index >= models.size()) continue;

		veekay::vec3 center = {0.0f, 0.0f, 0.0f};
		if (orbit.parent_index != SIZE_MAX && orbit.parent_index < models.size()) {
			center = models[orbit.parent_index].transform.position;
		}

		auto angle = float(sim_years * orbit.omega + orbit.phase);

		float x_temp = orbit.a * std::cos(angle);
		float z_temp = orbit.b * std::sin(angle);

		float cos_r = std::cos(orbit.rotation_rad);
		float sin_r = std::sin(orbit.rotation_rad);

		float x = x_temp * cos_r - z_temp * sin_r;
		float z = x_temp * sin_r + z_temp * cos_r;

		models[orbit.model_index].transform.position = center + veekay::vec3{x, 0.0f, z};
	}

	SceneUniforms scene_uniforms{
		.view_projection = camera.view_projection(aspect_ratio),
		.camera_position = veekay::vec4{camera.position.x, camera.position.y, camera.position.z, 0.0f},
		.lighting = lighting_params,
	};

	std::vector<ModelUniforms> model_uniforms(models.size());
	for (size_t i = 0, n = models.size(); i < n; ++i) {
		const Model& model = models[i];
		ModelUniforms& uniforms = model_uniforms[i];

		uniforms.model = model.transform.matrix();
		uniforms.albedo_color = veekay::vec4{model.material.albedo_color.x, model.material.albedo_color.y, model.material.albedo_color.z, 0.0f};
		uniforms.specular_color = veekay::vec4{model.material.specular_color.x, model.material.specular_color.y, model.material.specular_color.z, 0.0f};
		uniforms.material = veekay::vec4{model.material.shininess, 0.0f, 0.0f, 0.0f};
	}

	*(SceneUniforms*)scene_uniforms_buffer->mapped_region = scene_uniforms;

	const size_t alignment =
		veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms));

	for (size_t i = 0, n = model_uniforms.size(); i < n; ++i) {
		const ModelUniforms& uniforms = model_uniforms[i];

		char* const pointer = static_cast<char*>(model_uniforms_buffer->mapped_region) + i * alignment;
		*reinterpret_cast<ModelUniforms*>(pointer) = uniforms;
	}

	// Write point lights to SSBO
	{
		// Write count at offset 0, then pad to 16-byte boundary
		uint32_t* count_ptr = static_cast<uint32_t*>(point_lights_buffer->mapped_region);
		*count_ptr = point_light_count;
		
		// Array starts at 16-byte boundary (std430 alignment)
		PointLight* lights_ptr = reinterpret_cast<PointLight*>(
			static_cast<char*>(point_lights_buffer->mapped_region) + 16);
		for (uint32_t i = 0; i < point_light_count && i < 8; i++) {
			lights_ptr[i] = point_lights[i];
		}
	}
	
	// Write spotlights to SSBO
	{
		// Write count at offset 0, then pad to 16-byte boundary
		uint32_t* count_ptr = static_cast<uint32_t*>(spotlights_buffer->mapped_region);
		*count_ptr = spotlight_count;
		
		// Array starts at 16-byte boundary (std430 alignment)
		Spotlight* lights_ptr = reinterpret_cast<Spotlight*>(
			static_cast<char*>(spotlights_buffer->mapped_region) + 16);
		for (uint32_t i = 0; i < spotlight_count && i < 4; i++) {
			lights_ptr[i] = spotlights[i];
		}
	}

}

void render(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
	vkResetCommandBuffer(cmd, 0);

	{ // NOTE: Start recording rendering commands
		VkCommandBufferBeginInfo info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		vkBeginCommandBuffer(cmd, &info);
	}

	{ // NOTE: Use current swapchain framebuffer and clear it
		VkClearValue clear_color{.color = {{0.1f, 0.1f, 0.1f, 1.0f}}};
		VkClearValue clear_depth{.depthStencil = {1.0f, 0}};

		VkClearValue clear_values[] = {clear_color, clear_depth};

		VkRenderPassBeginInfo info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = veekay::app.vk_render_pass,
			.framebuffer = framebuffer,
			.renderArea = {
				.extent = {
					veekay::app.window_width,
					veekay::app.window_height
				},
			},
			.clearValueCount = 2,
			.pClearValues = clear_values,
		};

		vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	VkDeviceSize zero_offset = 0;

	VkBuffer current_vertex_buffer = VK_NULL_HANDLE;
	VkBuffer current_index_buffer = VK_NULL_HANDLE;

	const size_t model_uniorms_alignment =
		veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms));

	for (size_t i = 0, n = models.size(); i < n; ++i) {
		const Model& model = models[i];
		const Mesh& mesh = model.mesh;

		if (current_vertex_buffer != mesh.vertex_buffer->buffer) {
			current_vertex_buffer = mesh.vertex_buffer->buffer;
			vkCmdBindVertexBuffers(cmd, 0, 1, &current_vertex_buffer, &zero_offset);
		}

		if (current_index_buffer != mesh.index_buffer->buffer) {
			current_index_buffer = mesh.index_buffer->buffer;
			vkCmdBindIndexBuffer(cmd, current_index_buffer, zero_offset, VK_INDEX_TYPE_UINT32);
		}

		uint32_t offset = i * model_uniorms_alignment;
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                    0, 1, &descriptor_set, 1, &offset);

		vkCmdDrawIndexed(cmd, mesh.indices, 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

} // namespace

int main() {
	return veekay::run({
		.init = initialize,
		.shutdown = shutdown,
		.update = update,
		.render = render,
	});
}
