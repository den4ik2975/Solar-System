#version 450

layout (location = 0) in vec3 v_position;
layout (location = 1) in vec3 v_normal;
layout (location = 2) in vec2 v_uv;

layout (location = 0) out vec3 f_position;
layout (location = 1) out vec3 f_normal;
layout (location = 2) out vec2 f_uv;
layout (location = 3) out vec4 f_shadow_dir;
layout (location = 4) out vec4 f_shadow_spot;

struct DirectionalLight {
	vec4 direction;
	vec4 color;
};

struct LightingData {
	vec4 ambient_color;
	DirectionalLight directional_light;
};

layout (binding = 0, std140) uniform SceneUniforms {
	mat4 view_projection;
	vec4 camera_position;
	LightingData lighting;
	vec4 time;
	mat4 shadow_view_projection_dir;
	mat4 shadow_view_projection_spot;
	vec4 shadow_meta;
} scene;

layout (binding = 1, std140) uniform ModelUniforms {
	mat4 model;
	vec4 albedo_color;
	vec4 specular_color;
	vec4 material;
} model_data;

void main() {
	vec4 position = model_data.model * vec4(v_position, 1.0f);

	// 2.7 Пересчёт нормали в мировое пространство (transpose(inverse(model))) для корректного освещения
	mat3 normal_matrix = mat3(transpose(inverse(model_data.model)));
	vec3 normal = normal_matrix * v_normal;

	gl_Position = scene.view_projection * position;

	f_position = position.xyz;
	f_normal = normalize(normal);
	f_uv = v_uv;
	f_shadow_dir = scene.shadow_view_projection_dir * position;
	f_shadow_spot = scene.shadow_view_projection_spot * position;
}
