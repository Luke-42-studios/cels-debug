#ifndef CELS_DEBUG_HTTP_CLIENT_H
#define CELS_DEBUG_HTTP_CLIENT_H

#include <stddef.h>
#include <curl/curl.h>

// Connection state machine
typedef enum {
    CONN_DISCONNECTED = 0,
    CONN_CONNECTED,
    CONN_RECONNECTING
} connection_state_t;

// HTTP response buffer
typedef struct {
    char *data;
    size_t size;
} http_buffer_t;

// HTTP response (caller must call http_response_free)
typedef struct {
    int status;          // HTTP status code, or -1 on network error
    http_buffer_t body;
} http_response_t;

// Initialize libcurl (call once at startup). Returns NULL on failure.
CURL *http_client_init(void);

// Perform HTTP GET. Timeout is 200ms (localhost only).
// Caller must call http_response_free() on the result.
http_response_t http_get(CURL *curl, const char *url);

// Free response body memory.
void http_response_free(http_response_t *resp);

// Cleanup libcurl (call once at shutdown).
void http_client_fini(CURL *curl);

// Update connection state based on HTTP result.
// Call after each http_get to transition the state machine:
//   status 200 -> CONNECTED
//   status != 200 && was CONNECTED/RECONNECTING -> RECONNECTING (silent retry)
//   status != 200 && never connected -> DISCONNECTED
connection_state_t connection_state_update(connection_state_t current, int http_status);

#endif // CELS_DEBUG_HTTP_CLIENT_H
