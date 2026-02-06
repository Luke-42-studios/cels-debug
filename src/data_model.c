#define _POSIX_C_SOURCE 199309L
#include "data_model.h"
#include <stdlib.h>
#include <time.h>

world_snapshot_t *world_snapshot_create(void) {
    world_snapshot_t *snap = calloc(1, sizeof(world_snapshot_t));
    if (snap) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        snap->timestamp_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    return snap;
}

void world_snapshot_free(world_snapshot_t *snap) {
    free(snap);
}

/* --- Entity node --- */

entity_node_t *entity_node_create(void) {
    entity_node_t *node = calloc(1, sizeof(entity_node_t));
    if (node) {
        node->expanded = true;
    }
    return node;
}

void entity_node_free(entity_node_t *node) {
    if (!node) return;
    free(node->name);
    free(node->full_path);
    for (int i = 0; i < node->component_count; i++) {
        free(node->component_names[i]);
    }
    free(node->component_names);
    for (int i = 0; i < node->tag_count; i++) {
        free(node->tags[i]);
    }
    free(node->tags);
    free(node->children);  /* NOT recursive -- entity_list_free handles nodes */
    free(node);
}

void entity_node_add_child(entity_node_t *parent, entity_node_t *child) {
    if (!parent || !child) return;

    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        entity_node_t **new_children =
            realloc(parent->children, (size_t)new_cap * sizeof(entity_node_t *));
        if (!new_children) return;
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    child->depth = parent->depth + 1;
}

/* --- Entity list --- */

entity_list_t *entity_list_create(void) {
    return calloc(1, sizeof(entity_list_t));
}

void entity_list_free(entity_list_t *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        entity_node_free(list->nodes[i]);
    }
    free(list->nodes);
    free(list->roots);
    free(list);
}

/* --- Entity detail --- */

entity_detail_t *entity_detail_create(void) {
    return calloc(1, sizeof(entity_detail_t));
}

void entity_detail_free(entity_detail_t *detail) {
    if (!detail) return;
    free(detail->path);
    if (detail->doc) {
        yyjson_doc_free(detail->doc);
    }
    free(detail);
}

/* --- Component registry --- */

component_registry_t *component_registry_create(void) {
    return calloc(1, sizeof(component_registry_t));
}

void component_registry_free(component_registry_t *reg) {
    if (!reg) return;
    for (int i = 0; i < reg->count; i++) {
        free(reg->components[i].name);
    }
    free(reg->components);
    free(reg);
}
