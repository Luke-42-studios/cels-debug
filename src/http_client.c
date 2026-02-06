#include "http_client.h"
#include <stdlib.h>
#include <string.h>

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    http_buffer_t *buf = (http_buffer_t *)userdata;

    char *new_data = realloc(buf->data, buf->size + total + 1);
    if (!new_data) return 0;

    buf->data = new_data;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';

    return total;
}

CURL *http_client_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    // Short timeouts -- localhost only, sub-ms round trip expected
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 200L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 200L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    // Prevent libcurl from installing its own signal handlers
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    return curl;
}

http_response_t http_get(CURL *curl, const char *url) {
    http_response_t resp = {0};
    http_buffer_t buf = {NULL, 0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        resp.status = (int)http_code;
        resp.body = buf;
    } else {
        resp.status = -1;
        free(buf.data);
    }

    return resp;
}

void http_response_free(http_response_t *resp) {
    free(resp->body.data);
    resp->body.data = NULL;
    resp->body.size = 0;
}

void http_client_fini(CURL *curl) {
    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();
}

connection_state_t connection_state_update(connection_state_t current, int http_status) {
    if (http_status == 200) {
        return CONN_CONNECTED;
    }
    /* Once we've ever connected, always show Reconnecting on failure */
    if (current == CONN_CONNECTED || current == CONN_RECONNECTING) {
        return CONN_RECONNECTING;
    }
    /* Never connected yet -- stay disconnected */
    return CONN_DISCONNECTED;
}
