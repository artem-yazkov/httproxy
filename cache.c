#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cache.h"

void
cache_req_init(cache_request_t *req)
{
    req->rdata = NULL;
    req->rsize = 0;
    req->state = REQSTATE_INIT;
}

int
cache_req_write(cache_request_t *req, char *buffer, size_t size)
{
fprintf(stdout, "cache_req_write: %lu bytes\n", size);
    req->rdata = realloc(req->rdata, req->rsize + size);
    memcpy(&req->rdata[req->rsize], buffer, size);
    req->rsize += size;
    return 0;
}

int
cache_req_process(cache_request_t *req)
{
fprintf(stdout, "cache_req_process\n");
    req->state = REQSTATE_FROM_MEMORY;
    req->cursor = 0;
    return 0;
}

int
cache_req_clean(cache_request_t *request)
{
    return 0;
}

int
cache_resp_read(cache_request_t *req, char *buffer, size_t size)
{
fprintf(stdout, "cache_resp_read\n");
    if (req->cursor + size > req->rsize) {
        size = req->rsize - req->cursor;
    }
    memcpy(buffer, &req->rdata[req->cursor], size);
    req->cursor += size;
    if (req->cursor == req->rsize) {
        req->state = REQSTATE_FINISHED;
    }
    return (int)size;
}

int
cache_resp_write(cache_request_t *request, char *buffer, size_t size)
{
    return 0;
}
