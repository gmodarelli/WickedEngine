#pragma once

struct Wicked_Camera
{
	float position[3];
	float orientation[4];
	float fov_vertical;
	float near_plane;
	float far_plane;
};

struct Movable_Entity
{
	float orientation[4];
	float position[3];
	float scale[3];
	float _padding[2];
	uint64_t game_entity;
};

void wicked_set_shader_path(const char* path);
void wicked_set_shader_source_path(const char* path);
void wicked_set_window(void* handle);
void wicked_run(Wicked_Camera camera, Movable_Entity* movable_entities, uint32_t movable_entity_count);
void wicked_shutdown();
void wicked_load_prefab(const char* prefab_path);
