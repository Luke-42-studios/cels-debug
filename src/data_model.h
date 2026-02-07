#ifndef CELS_DEBUG_DATA_MODEL_H
#define CELS_DEBUG_DATA_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <yyjson.h>

// Snapshot of /stats/world response
// Each poll produces a new snapshot; previous is freed atomically
typedef struct world_snapshot {
    double entity_count;
    double fps;
    double frame_time_ms;   // performance.frame_time converted to ms
    double system_count;
    int64_t timestamp_ms;   // when this snapshot was taken
} world_snapshot_t;

world_snapshot_t *world_snapshot_create(void);
void world_snapshot_free(world_snapshot_t *snap);

// Entity classification -- sections spell CELS + Systems + Components
typedef enum {
    ENTITY_CLASS_COMPOSITION,  // C: scene structure (AppUI, MainMenu, Button trees)
    ENTITY_CLASS_ENTITY,       // E: entities showing module/system relationships
    ENTITY_CLASS_LIFECYCLE,    // L: lifecycle controllers (MainMenuLC, CELS_LifecycleSystem)
    ENTITY_CLASS_STATE,        // S: state entities (names ending with "State")
    ENTITY_CLASS_SYSTEM,       // Systems: pipeline systems (TUI_InputSystem, etc.)
    ENTITY_CLASS_COMPONENT,    // Components: type definitions (Text, ClickArea, etc.)
    ENTITY_CLASS_COUNT
} entity_class_t;

// Entity tree node (from /query response)
// Each node represents one entity with its parent-child relationships.
// Children array uses dynamic capacity (doubled on grow).
typedef struct entity_node {
    char *name;             // entity leaf name, NULL for anonymous
    char *full_path;        // slash-separated for REST URL (e.g., "Sun/Earth")
    uint64_t id;            // numeric entity ID

    char **component_names; // from lightweight list poll
    int component_count;

    char **tags;
    int tag_count;

    struct entity_node *parent;       // tree link
    struct entity_node **children;
    int child_count;
    int child_capacity;

    bool expanded;          // UI collapse state (default true for root nodes)
    bool is_anonymous;      // no name, only numeric ID
    int depth;              // nesting level for indentation

    entity_class_t entity_class;  // section classification
    char *class_detail;           // display label: "OnLoad", "Observer", etc.

    int system_match_count;   // match count from pipeline stats, 0 if not a system
    bool disabled;            // system disabled from pipeline stats
} entity_node_t;

// Flat ownership of all entity nodes from one poll cycle
typedef struct entity_list {
    entity_node_t **nodes;  // flat ownership array of all nodes
    int count;

    entity_node_t **roots;  // top-level nodes (pointers into nodes[])
    int root_count;
} entity_list_t;

// Selected entity component data (from /entity/<path> response)
// Owns the yyjson_doc -- all yyjson_val* pointers are valid while doc lives.
typedef struct entity_detail {
    char *path;             // entity path for REST lookup
    uint64_t id;

    yyjson_doc *doc;        // parsed JSON, owns all values
    yyjson_val *components; // pointer into doc: "components" object
    yyjson_val *tags;       // pointer into doc: "tags" array
    yyjson_val *pairs;      // pointer into doc: "pairs" object

    char *doc_brief;        // flecs doc brief text (strdup'd, may be NULL)
} entity_detail_t;

// Single component type info (from /components response)
typedef struct component_info {
    char *name;
    int entity_count;
    int size;               // type size in bytes, 0 if no type_info
    bool has_type_info;
} component_info_t;

// Component registry (all component types from one poll)
typedef struct component_registry {
    component_info_t *components;
    int count;
} component_registry_t;

// Single system info (parsed from /stats/pipeline + entity tags)
typedef struct system_info {
    char *name;              // leaf name (e.g., "MovementSystem")
    char *full_path;         // dot-separated path from pipeline stats
    char *phase;             // phase name (e.g., "OnUpdate") -- filled by tab_ecs classify
    bool disabled;           // from pipeline stats
    int matched_entity_count; // latest gauge value
    int matched_table_count;  // latest gauge value
    double time_spent_ms;    // latest gauge value, converted to ms
} system_info_t;

// All systems from one /stats/pipeline poll
typedef struct system_registry {
    system_info_t *systems;
    int count;
} system_registry_t;

// Entity node lifecycle
entity_node_t *entity_node_create(void);
void entity_node_free(entity_node_t *node);
void entity_node_add_child(entity_node_t *parent, entity_node_t *child);

// Entity list lifecycle
entity_list_t *entity_list_create(void);
void entity_list_free(entity_list_t *list);

// Entity detail lifecycle
entity_detail_t *entity_detail_create(void);
void entity_detail_free(entity_detail_t *detail);

// Component registry lifecycle
component_registry_t *component_registry_create(void);
void component_registry_free(component_registry_t *reg);

// System registry lifecycle
system_registry_t *system_registry_create(void);
void system_registry_free(system_registry_t *reg);

#endif // CELS_DEBUG_DATA_MODEL_H
