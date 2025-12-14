#version 450

layout (location = 0) in vec3 f_position;
layout (location = 1) in vec3 f_normal;
layout (location = 2) in vec2 f_uv;
layout (location = 3) in vec4 f_shadow_dir;
layout (location = 4) in vec4 f_shadow_spot;

layout (location = 0) out vec4 final_color;

struct DirectionalLight {
	vec4 direction;
	vec4 color;
};

struct PointLight {
	vec4 position;
	vec4 color;
};

struct Spotlight {
	vec4 position;
	vec4 direction;
	vec4 color;
	vec4 cone_angles;
};

struct LightingData {
	vec4 ambient_color;
	DirectionalLight directional_light;
};

layout (binding = 0, std140) uniform SceneUniforms {
	mat4 view_projection;   // 2.5 Матрица вида*проекции из CPU (transpose внутри движка)
	vec4 camera_position;   // 2.5 Позиция камеры в мировых координатах
	LightingData lighting;  // 2.5 Фоновый свет и направленный источник
	vec4 time;              // x = time
	mat4 shadow_view_projection_dir;
	mat4 shadow_view_projection_spot;
	vec4 shadow_meta;       // x: spot shadow enabled (0/1), y: shadowed spot index, z: point far, w: point near
} scene;

layout (binding = 1, std140) uniform ModelUniforms {
	mat4 model;          // 2.5 Матрица модели (мировое преобразование)
	vec4 albedo_color;   // 2.5 Диффузный/альбедо цвет
	vec4 specular_color; // 2.5 Цвет блика
	vec4 material;       // 2.5 x = shininess (экспонента блеска), <0 => эмиссия, y = warp, w = opacity
} model_data;

layout (binding = 2, std430) readonly buffer PointLightsBuffer {
	uint count;   // 2.6 Кол-во точечных источников
	uint _pad0;
	uint _pad1;
	uint _pad2;
	PointLight lights[]; // 2.6 Массив точечных источников (std430)
} point_lights;

layout (binding = 3, std430) readonly buffer SpotlightsBuffer {
	uint count;   // 2.7 Кол-во прожекторов
	uint _pad0;
	uint _pad1;
	uint _pad2;
	Spotlight lights[]; // 2.7 Массив прожекторов (std430)
} spotlights;

layout (set = 0, binding = 4) uniform sampler2DShadow shadow_texture_dir;
layout (set = 0, binding = 5) uniform sampler2DShadow shadow_texture_spot;
layout (set = 0, binding = 6) uniform samplerCubeShadow shadow_texture_point;

layout (set = 1, binding = 0) uniform sampler2D tex_albedo;
layout (set = 1, binding = 1) uniform sampler2D tex_specular;
layout (set = 1, binding = 2) uniform sampler2D tex_emissive;

float sampleShadow(sampler2DShadow shadow_tex, vec4 shadow_position) {
	vec3 coord = shadow_position.xyz / shadow_position.w;
	if (coord.z <= 0.0 || coord.z >= 1.0) {
		return 1.0;
	}
	coord.xy = coord.xy * 0.5 + 0.5;
	if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) {
		return 1.0;
	}
	coord.z -= 0.001;
	return texture(shadow_tex, coord);
}

float projectDepth(float z_eye, float near_plane, float far_plane) {
	float denom = far_plane - near_plane;
	if (denom <= 0.0) return 1.0;
	return (far_plane / denom) - (far_plane * near_plane) / (denom * z_eye);
}

float samplePointShadow(vec3 to_light, float near_plane, float far_plane) {
	float dist = length(to_light);
	if (far_plane <= near_plane || dist <= 0.0001) return 1.0;
	vec3 dir = normalize(to_light);

	// Pick the face the cube map will use and project along that forward axis
	vec3 face_forward;
	if (abs(dir.x) > abs(dir.y) && abs(dir.x) > abs(dir.z)) {
		face_forward = vec3(sign(dir.x), 0.0, 0.0);
	} else if (abs(dir.y) > abs(dir.z)) {
		face_forward = vec3(0.0, sign(dir.y), 0.0);
	} else {
		face_forward = vec3(0.0, 0.0, sign(dir.z));
	}

	float z_eye = dist * max(dot(dir, face_forward), 0.0);
	float compare = projectDepth(z_eye, near_plane, far_plane);
	// Small bias to reduce acne/flicker
	compare = clamp(compare - 0.001, 0.0, 1.0);
	return texture(shadow_texture_point, vec4(dir, compare));
}

void main() {
	vec3 N = normalize(f_normal);
	vec3 V = normalize(scene.camera_position.xyz - f_position);

	// 2.8 Свойства материала для Блинн-Фонга
	float warp = max(model_data.material.y, 0.0);
	vec2 uv = f_uv;
	if (warp > 0.0) {
		// 3.6 Нетривиальная анимация UV (дрейф+лёгкий вихрь) для выбранных материалов
		float t = scene.time.x;
		uv += vec2(t * 0.02, t * 0.01); // gentle drift
		vec2 centered = uv - vec2(0.5);
		float r = length(centered);
		float core = 1.0 - smoothstep(0.2, 0.9, r);
		// core swirl + shallow radial wave
		float twist = core * warp * 0.07 * sin(t * 1.4 + r * 12.0);
		float c = cos(twist);
		float s = sin(twist);
		centered = vec2(c * centered.x - s * centered.y, s * centered.x + c * centered.y);
		centered *= (1.0 + core * warp * 0.02 * sin(r * 15.0 - t * 1.5));
		uv = vec2(0.5) + centered;
	}
	vec4 albedo_sample = texture(tex_albedo, uv);
	vec4 spec_sample = texture(tex_specular, uv);
	vec4 emissive_sample = texture(tex_emissive, uv);

	float r_center = length(f_uv - vec2(0.5));
	float emissive_pulse = max(model_data.material.z, 0.0);
	float pulse = 1.0 + emissive_pulse * 0.15 * sin(scene.time.x * 2.0 - r_center * 15.0);
	albedo_sample.rgb *= pulse;
	emissive_sample.rgb *= pulse;

	vec3 k_a = model_data.albedo_color.xyz * albedo_sample.rgb;   // Фоновая/диффузная часть
	vec3 k_d = model_data.albedo_color.xyz * albedo_sample.rgb;   // Диффузная часть
	vec3 k_s = model_data.specular_color.xyz * spec_sample.rgb; // Бликовая часть
	float shininess = model_data.material.x;  // Степень блеска (экспонента)
	float alpha = clamp(model_data.material.w * albedo_sample.a, 0.0, 1.0);

	// 0.6 Эмиссия: при отрицательном shininess не считаем освещение, выводим базовый цвет (Солнце)
	if (shininess < 0.0) {
		final_color = vec4((k_d + emissive_sample.rgb), alpha);
		return;
	}
	
	// 2.8 Фоновая составляющая
	vec3 ambient = scene.lighting.ambient_color.xyz * k_a;
	vec3 result = ambient;
	float shadow_dir = sampleShadow(shadow_texture_dir, f_shadow_dir);
	float shadow_spot = sampleShadow(shadow_texture_spot, f_shadow_spot);
	int spot_shadow_enabled = int(scene.shadow_meta.x + 0.5);
	int shadowed_spot_index = int(round(scene.shadow_meta.y));
	uint point_light_count = point_lights.count;
	float shadow_point = 1.0;
	if (point_light_count > 0) {
		shadow_point = samplePointShadow(f_position - point_lights.lights[0].position.xyz, scene.shadow_meta.w, scene.shadow_meta.z);
	}
	
	// 2.8 Направленный источник (остался в API): стандартный Blinn-Phong
	// Направление уже указывает в сторону источника
	vec3 L_dir = normalize(scene.lighting.directional_light.direction.xyz);
	float NdotL_dir = max(dot(N, L_dir), 0.0);
	
	if (NdotL_dir > 0.0) {
		// Diffuse
		vec3 diffuse = k_d * scene.lighting.directional_light.color.xyz * NdotL_dir;
		
		// Blinn-Phong specular
		vec3 H_dir = normalize(L_dir + V);
		float NdotH_dir = max(dot(N, H_dir), 0.0);
		vec3 specular = k_s * scene.lighting.directional_light.color.xyz * pow(NdotH_dir, shininess);
		
		result += (diffuse + specular) * shadow_dir;
	}
	
	// 2.8 Точечные источники из SSBO: закон обратных квадратов + Blinn-Phong
	// Считаем затухание 1/dist^2 и добавляем диффуз/блик для каждого источника
	for (uint i = 0; i < point_light_count && i < 8; i++) {
		vec3 L_vec = point_lights.lights[i].position.xyz - f_position;
		float distance = length(L_vec);
		vec3 L = normalize(L_vec);
		
		// Inverse-square law attenuation: 1 / distance^2
		float attenuation = 1.0 / (distance * distance);
		
		float NdotL = max(dot(N, L), 0.0);
		
		if (NdotL > 0.0) {
			// Diffuse component
			vec3 diffuse = k_d * point_lights.lights[i].color.xyz * NdotL * attenuation;
			
			// Blinn-Phong specular component
			vec3 H = normalize(L + V);
			float NdotH = max(dot(N, H), 0.0);
			vec3 specular = k_s * point_lights.lights[i].color.xyz * pow(NdotH, shininess) * attenuation;
			
			float shadow_term = (i == 0) ? shadow_point : 1.0;
			result += (diffuse + specular) * shadow_term;
		}
	}
	
	// 2.9 Прожекторы из SSBO: гладкий край через smoothstep(inner/outer)
	// Учитываем угол между лучом и осью прожектора, делаем мягкий спад между inner/outer
	uint spotlight_count = spotlights.count;
	for (uint i = 0; i < spotlight_count && i < 8; i++) {
		vec3 L_vec = spotlights.lights[i].position.xyz - f_position;
		float distance = length(L_vec);
		vec3 L = normalize(L_vec);
		vec3 spot_dir = normalize(spotlights.lights[i].direction.xyz);
		
		// Calculate angle between light direction and spotlight direction
		float cos_angle = dot(-L, spot_dir);
		
		// Smooth falloff between inner and outer cone
		float inner_cone = spotlights.lights[i].cone_angles.x;
		float outer_cone = spotlights.lights[i].cone_angles.y;
		float spot_factor = smoothstep(outer_cone, inner_cone, cos_angle);
		
		// Inverse-square law attenuation
		float distance_attenuation = 1.0 / (distance * distance);
		float attenuation = distance_attenuation * spot_factor;
		
		float NdotL = max(dot(N, L), 0.0);
		
		if (NdotL > 0.0 && spot_factor > 0.0) {
			// Diffuse component
			vec3 diffuse = k_d * spotlights.lights[i].color.xyz * NdotL * attenuation;
			
			// Blinn-Phong specular component
			vec3 H = normalize(L + V);
			float NdotH = max(dot(N, H), 0.0);
			vec3 specular = k_s * spotlights.lights[i].color.xyz * pow(NdotH, shininess) * attenuation;
			
			float spot_shadow_term = 1.0;
			if ((spot_shadow_enabled != 0) && shadowed_spot_index == int(i)) {
				spot_shadow_term = shadow_spot;
			}

			result += (diffuse + specular) * spot_shadow_term;
		}
	}
	
	result += emissive_sample.rgb;
	final_color = vec4(result, alpha);
}
