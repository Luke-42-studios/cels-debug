#ifndef CELS_DEBUG_JSON_PARSER_H
#define CELS_DEBUG_JSON_PARSER_H

#include "data_model.h"
#include <stddef.h>

// Parse /stats/world JSON response into a world_snapshot_t.
// Returns a newly allocated snapshot on success, NULL on parse failure.
// Caller owns the returned snapshot and must call world_snapshot_free().
//
// The /stats/world response has metrics like:
//   "entities.count": { "avg": [60 floats], "min": [...], "max": [...] }
// We extract the LAST element of "avg" (most recent measurement).
world_snapshot_t *json_parse_world_stats(const char *json, size_t len);

// Parse /query response into an entity_list_t with parent-child tree.
// The query uses table=true, values=false, entity_id=true.
// Returns a newly allocated list on success, NULL on parse failure.
// Caller owns the returned list and must call entity_list_free().
entity_list_t *json_parse_entity_list(const char *json, size_t len);

// Parse /entity/<path> response into an entity_detail_t.
// The entity_detail_t OWNS the yyjson_doc -- caller frees via entity_detail_free().
// Returns NULL on parse failure.
entity_detail_t *json_parse_entity_detail(const char *json, size_t len);

// Parse /components response into a component_registry_t.
// The response root is a JSON array (not object).
// Returns a newly allocated registry on success, NULL on parse failure.
// Caller owns the returned registry and must call component_registry_free().
component_registry_t *json_parse_component_registry(const char *json, size_t len);

// Parse /stats/pipeline JSON response into a system_registry_t.
// The response is a JSON array alternating system entries (have "name") and
// sync point entries (have "system_count"). Only system entries are parsed.
// Returns a newly allocated registry on success, NULL on parse failure.
// Caller owns the returned registry and must call system_registry_free().
system_registry_t *json_parse_pipeline_stats(const char *json, size_t len);

#endif // CELS_DEBUG_JSON_PARSER_H
