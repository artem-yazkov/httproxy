#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>


#include "cache.h"

/* append binary data to string */
static int
__str_append(cache_str_t *str, char *data, size_t size)
{
    if (str->size < (str->len + size)) {
        str->size = str->len + size;
        str->data = realloc(str->data, str->size);
    }
    memcpy(&str->data[str->len], data, size);
    str->len += size;
    return size;
}

/* append formatted text data to string */
static int
__str_append_f(cache_str_t *str, char *format, ...)
{
    int     cout = 0;
    va_list args;

    if (!str->size) {
        str->size = 0xff;
        str->data = realloc(str->data, str->size);
    }
    while (1) {
        int fspace = (int)str->size - (int)str->len - 1;
        va_start(args, format);

        if ((fspace < 0) ||
            ( (cout = vsnprintf(&str->data[str->len], fspace, format, args)) >= fspace)) {
            str->size *= 2;
            str->data = realloc(str->data, str->size);
            va_end(args);
            continue;
        }
        va_end(args);
        break;
    }
    str->len += cout;

    return cout;
}

static char*
__parse_method(char *header, char **method, char **uri, char **proto)
{
    static cache_str_t line;
    line.len = 0;

    int ichar = 0;
    for (; (header[ichar] != '\0') && (header[ichar] != '\n'); ichar++);
    if (!ichar)
        return NULL;

    __str_append(&line, header, ichar);

    *method = strtok(line.data, " \t");
    *uri    = strtok(NULL,      " \t");
    *proto  = strtok(NULL,      " \t");

    if (header[ichar] == '\n')
        return header + ichar + 1;
    else
        return NULL;
}

static char*
__parse_field(char *header, char **name, char **value)
{
    static cache_str_t line;
    line.len = 0;

    int ichar = 0;
    for (; (header[ichar] != '\0') && (header[ichar] != '\n'); ichar++);
    if (!ichar)
        return NULL;

    __str_append(&line, header, ichar);
    line.data[ichar-1] = '\0';

    *name  = strtok(line.data, " :");
    *value = (*name) ? (line.data + strlen(*name)) : NULL;
    for (; *value && !isgraph(**value); (*value)++);

    if (!(*name) || !(*value))
        return NULL;

    if (header[ichar] == '\n')
        return header + ichar + 1;
    else
        return NULL;
}

static void
__get_response(char *host, cache_str_t *req, cache_str_t *resp)
{
    struct hostent *hp;
    struct sockaddr_in addr;
    int sock;

    if((hp = gethostbyname(host)) == NULL){
        herror("gethostbyname");
        exit(1);
    }
    bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
    addr.sin_port = htons(80);
    addr.sin_family = AF_INET;
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    //int on = 1;
    //setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(int));

    if(sock == -1){
        perror("setsockopt");
        exit(1);
    }
    if(connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) == -1){
        perror("connect");
        exit(1);
    }

    char buffer[1024];
    write(sock, "GET /\r\n", strlen("GET /\r\n")); // write(fd, char[]*, len);..
    bzero(buffer, sizeof(buffer));

    while(read(sock, buffer, sizeof(buffer) - 1) != 0){
        fprintf(stderr, "%s", buffer);
        bzero(buffer, sizeof(buffer));
    }

    shutdown(sock, SHUT_RDWR);
    close(sock);
}

void
cache_req_init(cache_request_t *req)
{
    memset(req, 0, sizeof(*req));
    req->state = REQSTATE_INIT;
}

int
cache_req_write(cache_request_t *req, char *buffer, size_t size)
{
    __str_append(&req->hdr_cli, buffer, size);
    return 0;
}

int
cache_req_process(cache_request_t *req)
{
    char eof = 0;
    __str_append(&req->hdr_cli, &eof, 1);

    static cache_str_t mhost;
    mhost.len = 0;
    char *mname = NULL, *muri = NULL, *mproto = NULL;
    char *data  = req->hdr_cli.data;

    data = __parse_method(data, &mname, &muri, &mproto);
    if (strcasecmp(mname, "GET") != 0) {
        /* unsupported method */
    }
    __str_append_f(&req->hdr_srv, "GET %s HTTP/1.0\n", muri);

    while (data) {
        char *fname = NULL, *fvalue = NULL;
        data = __parse_field(data, &fname, &fvalue);
        if (!data)
            break;

        if (strcasecmp(fname, "HOST") == 0) {
            __str_append_f(&mhost, "%s", fvalue);
            __str_append_f(&req->hdr_srv, "Host: %s\n", fvalue);
        } else if (strcasecmp(fname, "Connection") == 0) {
            __str_append_f(&req->hdr_srv, "Connection: close\n");
        } else {
            __str_append_f(&req->hdr_srv, "%s: %s\n", fname, fvalue);
        }
    }

    __get_response(mhost.data, &req->hdr_cli, &req->response);
    //fwrite(req->response.data, req->response.len, 1, stdout);

    req->state = REQSTATE_FROM_MEMORY;
    req->cursor = 0;

    return 0;
}

int
cache_req_clean(cache_request_t *req)
{
    if (req->hdr_cli.data)
        free(req->hdr_cli.data);
    if (req->hdr_srv.data)
        free(req->hdr_srv.data);
    memset(req, 0, sizeof(*req));
    req->state = REQSTATE_FINISHED;
    return 0;
}

int
cache_resp_read(cache_request_t *req, char *buffer, size_t size)
{
    if (req->cursor + size > req->hdr_srv.len) {
        size = req->hdr_srv.len - req->cursor;
    }
    memcpy(buffer, &req->hdr_srv.data[req->cursor], size);
    req->cursor += size;
    if (req->cursor == req->hdr_srv.len) {
        req->state = REQSTATE_FINISHED;
    }
    return (int)size;
}

int
cache_resp_write(cache_request_t *request, char *buffer, size_t size)
{
    return 0;
}
