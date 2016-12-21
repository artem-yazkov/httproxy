#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

typedef enum cache_req_state {
    REQSTATE_ERROR,
    REQSTATE_INIT,
    REQSTATE_TO_SERVER,
    REQSTATE_FROM_SERVER,
    REQSTATE_FROM_SERVER_WAIT,
    REQSTATE_FROM_MEMORY,
    REQSTATE_FINISHED,
} cache_req_state_t;

typedef struct cache_req {
    cache_req_state_t state;
    char   *rdata;
    size_t  rsize;
    size_t  cursor; // TODO: temp
} cache_request_t;

void
cache_req_init(cache_request_t *reqt);

int
cache_req_write(cache_request_t *req, char *buffer, size_t size);

int
cache_req_process(cache_request_t *req);

int
cache_req_clean(cache_request_t *req);

int
cache_resp_read(cache_request_t *req, char *buffer, size_t size);

int
cache_resp_write(cache_request_t *req, char *buffer, size_t size);

#endif
