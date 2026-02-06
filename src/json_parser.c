#define _POSIX_C_SOURCE 200809L
#include "json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* --- World stats parser (existing) --- */

// Extract the latest value from a gauge metric.
// Flecs stats are 60-element circular buffers; last element is most recent.
static double extract_latest_gauge(yyjson_val *root, const char *field) {
    yyjson_val *metric = yyjson_obj_get(root, field);
    if (!metric) return 0.0;

    yyjson_val *avg = yyjson_obj_get(metric, "avg");
    if (!avg || !yyjson_is_arr(avg)) return 0.0;

    size_t count = yyjson_arr_size(avg);
    if (count == 0) return 0.0;

    yyjson_val *last = yyjson_arr_get(avg, count - 1);
    if (!last) return 0.0;

    // yyjson_get_num handles both integer and real JSON number types
    // (flecs may serialize 42 or 42.0 depending on the metric)
    if (yyjson_is_num(last)) {
        return yyjson_get_num(last);
    }
    return 0.0;
}

world_snapshot_t *json_parse_world_stats(const char *json, size_t len) {
    if (!json || len == 0) return NULL;

    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return NULL;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return NULL;
    }

    world_snapshot_t *snap = world_snapshot_create();
    if (!snap) {
        yyjson_doc_free(doc);
        return NULL;
    }

    snap->entity_count  = extract_latest_gauge(root, "entities.count");
    snap->fps           = extract_latest_gauge(root, "performance.fps");
    snap->system_count  = extract_latest_gauge(root, "queries.system_count");

    // frame_time is in seconds from flecs; convert to milliseconds
    double frame_time_s = extract_latest_gauge(root, "performance.frame_time");
    snap->frame_time_ms = frame_time_s * 1000.0;

    yyjson_doc_free(doc);
    return snap;
}

/* --- Entity list parser --- */

// Convert a dot-separated flecs path to slash-separated REST path.
// Caller must free the returned string.
static char *dots_to_slashes(const char *path) {
    if (!path) return NULL;
    size_t len = strlen(path);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    for (size_t i = 0; i < len; i++) {
        result[i] = (path[i] == '.') ? '/' : path[i];
    }
    result[len] = '\0';
    return result;
}

// Build full_path from parent (dot-separated) and name.
// Returns a slash-separated path. Caller must free.
static char *build_full_path(const char *parent_dot, const char *name) {
    if (!name || name[0] == '\0') return NULL;

    if (!parent_dot || parent_dot[0] == '\0') {
        return strdup(name);
    }

    // Convert parent dots to slashes, then append /name
    char *parent_slash = dots_to_slashes(parent_dot);
    if (!parent_slash) return NULL;

    size_t plen = strlen(parent_slash);
    size_t nlen = strlen(name);
    char *path = malloc(plen + 1 + nlen + 1);
    if (!path) {
        free(parent_slash);
        return NULL;
    }
    memcpy(path, parent_slash, plen);
    path[plen] = '/';
    memcpy(path + plen + 1, name, nlen);
    path[plen + 1 + nlen] = '\0';

    free(parent_slash);
    return path;
}

entity_list_t *json_parse_entity_list(const char *json, size_t len) {
    if (!json || len == 0) return NULL;

    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return NULL;

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *results = yyjson_obj_get(root, "results");
    if (!results || !yyjson_is_arr(results)) {
        yyjson_doc_free(doc);
        return NULL;
    }

    size_t result_count = yyjson_arr_size(results);
    if (result_count == 0) {
        yyjson_doc_free(doc);
        // Return empty list, not NULL (valid but empty world)
        entity_list_t *list = entity_list_create();
        return list;
    }

    // Allocate flat node array
    entity_node_t **nodes = calloc(result_count, sizeof(entity_node_t *));
    if (!nodes) {
        yyjson_doc_free(doc);
        return NULL;
    }

    // First pass: create nodes, extract fields
    size_t idx, max;
    yyjson_val *entity;
    int node_count = 0;

    yyjson_arr_foreach(results, idx, max, entity) {
        yyjson_val *name_val   = yyjson_obj_get(entity, "name");
        yyjson_val *parent_val = yyjson_obj_get(entity, "parent");
        yyjson_val *id_val     = yyjson_obj_get(entity, "id");

        entity_node_t *node = entity_node_create();
        if (!node) continue;

        // Name
        if (name_val && yyjson_is_str(name_val)) {
            const char *name_str = yyjson_get_str(name_val);
            if (name_str && name_str[0] != '\0') {
                node->name = strdup(name_str);
            }
        }

        // ID
        if (id_val && yyjson_is_num(id_val)) {
            node->id = (uint64_t)yyjson_get_uint(id_val);
        }

        // Anonymous detection
        node->is_anonymous = (node->name == NULL);

        // Build full_path (slash-separated)
        const char *parent_str = NULL;
        if (parent_val && yyjson_is_str(parent_val)) {
            parent_str = yyjson_get_str(parent_val);
        }

        if (node->is_anonymous) {
            // Anonymous: full_path is the string representation of ID
            char id_buf[32];
            snprintf(id_buf, sizeof(id_buf), "%llu",
                     (unsigned long long)node->id);
            node->full_path = strdup(id_buf);
        } else {
            node->full_path = build_full_path(parent_str, node->name);
            if (!node->full_path) {
                // Fallback: just the name
                node->full_path = strdup(node->name);
            }
        }

        // Component names (from "components" object keys)
        yyjson_val *comps = yyjson_obj_get(entity, "components");
        if (comps && yyjson_is_obj(comps)) {
            int comp_count = (int)yyjson_obj_size(comps);
            if (comp_count > 0) {
                node->component_names = calloc((size_t)comp_count, sizeof(char *));
                if (node->component_names) {
                    node->component_count = comp_count;
                    size_t ci, cmax;
                    yyjson_val *ckey, *cval;
                    int c = 0;
                    yyjson_obj_foreach(comps, ci, cmax, ckey, cval) {
                        node->component_names[c++] =
                            strdup(yyjson_get_str(ckey));
                    }
                }
            }
        }

        // Tags (from "tags" array)
        yyjson_val *tags = yyjson_obj_get(entity, "tags");
        if (tags && yyjson_is_arr(tags)) {
            int tag_count = (int)yyjson_arr_size(tags);
            if (tag_count > 0) {
                node->tags = calloc((size_t)tag_count, sizeof(char *));
                if (node->tags) {
                    node->tag_count = tag_count;
                    size_t ti, tmax;
                    yyjson_val *tag;
                    int t = 0;
                    yyjson_arr_foreach(tags, ti, tmax, tag) {
                        if (yyjson_is_str(tag)) {
                            node->tags[t++] = strdup(yyjson_get_str(tag));
                        }
                    }
                    node->tag_count = t;  // actual count (may differ if non-string tags)
                }
            }
        }

        nodes[node_count++] = node;
    }

    // Second pass: build parent-child tree.
    // For each node that has a parent, look up the parent's full_path in nodes[].
    // O(n^2) linear scan -- acceptable for <5K entities.
    for (int i = 0; i < node_count; i++) {
        entity_node_t *node = nodes[i];
        if (!node->full_path) continue;

        // Find the last '/' in full_path to get parent's path
        const char *last_slash = strrchr(node->full_path, '/');
        if (!last_slash) continue;  // root node, no parent

        size_t parent_len = (size_t)(last_slash - node->full_path);
        if (parent_len == 0) continue;

        // Search for a node with matching full_path
        for (int j = 0; j < node_count; j++) {
            if (i == j) continue;
            entity_node_t *candidate = nodes[j];
            if (!candidate->full_path) continue;
            if (strlen(candidate->full_path) == parent_len &&
                strncmp(candidate->full_path, node->full_path, parent_len) == 0) {
                entity_node_add_child(candidate, node);
                break;
            }
        }
    }

    // Collect roots (nodes with no parent)
    int root_cap = 16;
    entity_node_t **roots = calloc((size_t)root_cap, sizeof(entity_node_t *));
    int root_count = 0;
    for (int i = 0; i < node_count; i++) {
        if (nodes[i]->parent == NULL) {
            if (root_count >= root_cap) {
                root_cap *= 2;
                roots = realloc(roots, (size_t)root_cap * sizeof(entity_node_t *));
            }
            roots[root_count++] = nodes[i];
        }
    }

    // Build result
    entity_list_t *list = entity_list_create();
    if (!list) {
        for (int i = 0; i < node_count; i++) {
            entity_node_free(nodes[i]);
        }
        free(nodes);
        free(roots);
        yyjson_doc_free(doc);
        return NULL;
    }

    list->nodes = nodes;
    list->count = node_count;
    list->roots = roots;
    list->root_count = root_count;

    yyjson_doc_free(doc);  // Safe: all strings were strdup'd
    return list;
}

/* --- Entity detail parser --- */

entity_detail_t *json_parse_entity_detail(const char *json, size_t len) {
    if (!json || len == 0) return NULL;

    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return NULL;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return NULL;
    }

    entity_detail_t *detail = entity_detail_create();
    if (!detail) {
        yyjson_doc_free(doc);
        return NULL;
    }

    // DO NOT free doc -- entity_detail_t owns it
    detail->doc = doc;

    // Extract id
    yyjson_val *id_val = yyjson_obj_get(root, "id");
    if (id_val && yyjson_is_num(id_val)) {
        detail->id = (uint64_t)yyjson_get_uint(id_val);
    }

    // Build path from parent + name (dot-to-slash conversion)
    yyjson_val *parent_val = yyjson_obj_get(root, "parent");
    yyjson_val *name_val   = yyjson_obj_get(root, "name");
    const char *parent_str = NULL;
    const char *name_str   = NULL;
    if (parent_val && yyjson_is_str(parent_val)) {
        parent_str = yyjson_get_str(parent_val);
    }
    if (name_val && yyjson_is_str(name_val)) {
        name_str = yyjson_get_str(name_val);
    }
    if (name_str) {
        detail->path = build_full_path(parent_str, name_str);
    }

    // Store pointers into doc (valid while doc lives)
    detail->components = yyjson_obj_get(root, "components");
    detail->tags       = yyjson_obj_get(root, "tags");
    detail->pairs      = yyjson_obj_get(root, "pairs");

    return detail;
}

/* --- Component registry parser --- */

component_registry_t *json_parse_component_registry(const char *json, size_t len) {
    if (!json || len == 0) return NULL;

    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return NULL;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        return NULL;
    }

    size_t count = yyjson_arr_size(root);
    component_registry_t *reg = component_registry_create();
    if (!reg) {
        yyjson_doc_free(doc);
        return NULL;
    }

    if (count == 0) {
        yyjson_doc_free(doc);
        return reg;
    }

    reg->components = calloc(count, sizeof(component_info_t));
    if (!reg->components) {
        yyjson_doc_free(doc);
        free(reg);
        return NULL;
    }

    size_t idx, max;
    yyjson_val *comp;
    int ci = 0;

    yyjson_arr_foreach(root, idx, max, comp) {
        yyjson_val *name_val = yyjson_obj_get(comp, "name");
        yyjson_val *ec_val   = yyjson_obj_get(comp, "entity_count");
        yyjson_val *type_val = yyjson_obj_get(comp, "type");

        if (name_val && yyjson_is_str(name_val)) {
            reg->components[ci].name = strdup(yyjson_get_str(name_val));
        }
        if (ec_val && yyjson_is_num(ec_val)) {
            reg->components[ci].entity_count = (int)yyjson_get_int(ec_val);
        }

        if (type_val && yyjson_is_obj(type_val)) {
            reg->components[ci].has_type_info = true;
            yyjson_val *sz = yyjson_obj_get(type_val, "size");
            if (sz && yyjson_is_num(sz)) {
                reg->components[ci].size = (int)yyjson_get_int(sz);
            }
        }

        ci++;
    }

    reg->count = ci;
    yyjson_doc_free(doc);  // Safe: all strings were strdup'd
    return reg;
}
