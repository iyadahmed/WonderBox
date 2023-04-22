#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "NanoGUI/nanogui.h"
#include "hash.h"

typedef struct {
    float x;
    float y;
    float z;
} vec3_t;

typedef struct {
    vec3_t origin;
    vec3_t direction;
} ray_t;

typedef struct {
    vec3_t center;
    float radius;
} sphere_t;

typedef struct {
    vec3_t color;
    vec3_t specular;
} lighting_t;

typedef struct {
    vec3_t position;
    vec3_t color;
    float power;
} point_light_t;

struct vec3_uint32_hash_table_node_t {
    vec3_t key;
    uint32_t value;
    struct vec3_uint32_hash_table_node_t *next;
};

static float vec3_length_squared(vec3_t v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

static float vec3_length(vec3_t v) {
    return sqrtf(vec3_length_squared(v));
}

static vec3_t vec3_normalized(vec3_t v) {
    float l = vec3_length(v);
    return (vec3_t) {v.x / l, v.y / l, v.z / l};
}

static vec3_t vec3_scale(vec3_t v, float scale) {
    return (vec3_t) {v.x * scale, v.y * scale, v.z * scale};
}

static vec3_t vec3_add(vec3_t a, vec3_t b) {
    return (vec3_t) {a.x + b.x, a.y + b.y, a.z + b.z};
}

static vec3_t vec3_sub(vec3_t a, vec3_t b) {
    return (vec3_t) {a.x - b.x, a.y - b.y, a.z - b.z};
}

static float vec3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float clampf(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static bool ray_sphere_intersection(ray_t r, sphere_t s, float *t) {
    vec3_t oc = {r.origin.x - s.center.x, r.origin.y - s.center.y, r.origin.z - s.center.z};
    float a = r.direction.x * r.direction.x + r.direction.y * r.direction.y + r.direction.z * r.direction.z;
    float b = 2.0f * (oc.x * r.direction.x + oc.y * r.direction.y + oc.z * r.direction.z);
    float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - s.radius * s.radius;
    float discriminant = b * b - 4 * a * c;
    if (discriminant < 0) {
        return false;
    } else {
        *t = (-b - sqrtf(discriminant)) / (2.0f * a);
        return true;
    }
}

static bool
ray_sphere_intersection_with_normal_and_position(ray_t r, sphere_t s, float *t, vec3_t *normal, vec3_t *position) {
    if (ray_sphere_intersection(r, s, t)) {
        *position = vec3_add(r.origin, vec3_scale(r.direction, *t));
        *normal = vec3_scale(vec3_sub(*position, s.center), 1.0f / s.radius);
        return true;
    } else {
        return false;
    }
}

static lighting_t
blinn_phong_shading(point_light_t pl, vec3_t surface_position, vec3_t surface_normal, vec3_t view_direction,
                    float specular_hardness) {
    if (pl.power < 0) {
        return (lighting_t) {{0, 0, 0},
                             {0, 0, 0}};
    }

    lighting_t out;

    vec3_t light_direction = vec3_sub(pl.position, surface_position);
    float distance_squared = vec3_length_squared(light_direction);
    float distance = sqrtf(distance_squared);
    light_direction = vec3_scale(light_direction, 1.0f / distance);

    // Diffuse
    float NdotL = vec3_dot(surface_normal, light_direction);
    float intensity = clampf(NdotL, 0.0f, 1.0f);
    out.color = vec3_scale(pl.color, intensity * pl.power / distance_squared);

    // Specular
    vec3_t half_vector = vec3_normalized(vec3_add(light_direction, view_direction));
    float NdotH = vec3_dot(surface_normal, half_vector);
    float specular_intensity = powf(clampf(NdotH, 0.0f, 1.0f), specular_hardness);
    out.specular = vec3_scale(pl.color, specular_intensity * pl.power / distance_squared);

    return out;
}

int main(int argc, char **argv) {

    if (argc != 2) {
        puts("Expected arguments: path/to/mesh.stl");
        return 0;
    }

    const char *stl_mesh_filepath = argv[1];
    FILE *stl_mesh_file = fopen(stl_mesh_filepath, "rb");
    if (stl_mesh_file == NULL) {
        puts("Failed to open file");
        return 1;
    }
    if (fseek(stl_mesh_file, 80, SEEK_CUR) != 0) {
        puts("Failed to seek");
        return 1;
    }
    uint32_t num_tris = 0;
    if (fread(&num_tris, sizeof(uint32_t), 1, stl_mesh_file) != 1) {
        puts("Failed to read number of triangles");
        return 1;
    }

    size_t num_max_unique_vertices = 3 * num_tris;
    vec3_t *unique_vertices = malloc(num_max_unique_vertices * sizeof(vec3_t));
    size_t num_unique_vertices = 0;

    struct vec3_uint32_hash_table_node_t *preallocated_nodes = malloc(
            num_max_unique_vertices * sizeof(struct vec3_uint32_hash_table_node_t));
    size_t new_node_index = 0;

    size_t num_buckets = 3 * num_tris;
    struct vec3_uint32_hash_table_node_t **buckets = malloc(
            num_buckets * sizeof(struct vec3_uint32_hash_table_node_t *));
    for (size_t i = 0; i < 3 * num_tris; i++) {
        buckets[i] = NULL;
    }

    for (size_t triangle_index = 0; triangle_index < num_tris; triangle_index++) {
        vec3_t triangle_normal, triangle_vertices[3];
        uint16_t attribute_byte_count;
        if (fread(&triangle_normal, sizeof(vec3_t), 1, stl_mesh_file) != 1) {
            puts("Failed to read normal");
            return 1;
        }
        if (fread(&triangle_vertices, sizeof(vec3_t), 3, stl_mesh_file) != 3) {
            puts("Failed to read vertices");
            return 1;
        }
        if (fread(&attribute_byte_count, sizeof(uint16_t), 1, stl_mesh_file) != 1) {
            puts("Failed to read attribute byte count");
            return 1;
        }
        for (int triangle_vertex_index = 0; triangle_vertex_index < 3; triangle_vertex_index++) {
            vec3_t vertex = triangle_vertices[triangle_vertex_index];
            uint32_t vertex_hash = hash(vertex.x, vertex.y, vertex.z);
            uint32_t bucket_index = vertex_hash % num_buckets;
            if (buckets[bucket_index] == NULL) {
                // Push new hash map node
                buckets[bucket_index] = preallocated_nodes + (new_node_index++);
                buckets[bucket_index]->next = NULL;
                buckets[bucket_index]->key = vertex;

                // Push new vertex to unique vertices array
                size_t new_unique_vertex_index = num_unique_vertices++;
                buckets[bucket_index]->value = new_unique_vertex_index;
                unique_vertices[new_unique_vertex_index] = vertex;
            }
        }
    }

// TODO: build BVH

    int width = 640, height = 480;
    nano_gui_create_fixed_size_window(width, height);

    point_light_t light1;
    light1.power = 1.0f;
    light1.color = (vec3_t) {1.0f, 1.0f, 1.0f};
    light1.position = (vec3_t) {-1.0f, -1.0f, 0.0f};

    // Main loop
    while (nano_gui_process_events()) {
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                float x = (float) i / (float) width;
                float y = (float) j / (float) height;
                // Transform to NDC and correct aspect ratio
                x = 2 * x - 1;
                y = 2 * y - 1;
                x *= (float) width / (float) height;
                ray_t ray;
                ray.direction = vec3_normalized((vec3_t) {x, y, -1.0f});
                ray.origin = (vec3_t) {0, 0, 0};
                sphere_t sphere = {{0, 0, -2}, 1.0f};

                float depth;
                vec3_t position, normal;
                if (ray_sphere_intersection_with_normal_and_position(ray, sphere, &depth, &normal, &position)) {
                    lighting_t lighting = blinn_phong_shading(light1, position, normal, ray.direction, 20.0f);
//                    uint8_t depth_clamped = (uint8_t) clampf(depth * 255, 0, 255);
                    uint8_t r = (uint8_t) clampf(lighting.color.x * 255, 0, 255);
                    uint8_t g = (uint8_t) clampf(lighting.color.y * 255, 0, 255);
                    uint8_t b = (uint8_t) clampf(lighting.color.z * 255, 0, 255);
                    nano_gui_draw_pixel(i, j, r, g, b);
                } else {
                    nano_gui_draw_pixel(i, j, 0, 0, 0);
                }
            }
        }
    }
    return 0;
}