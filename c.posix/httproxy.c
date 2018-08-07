#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#define LISTEN_ADDR  "0.0.0.0"
#define LISTEN_PORT  1234
#define EPOLL_EVENTS 16


typedef struct conn_info {
    int                 fd;
    socklen_t           addr_len;
    struct sockaddr_in  addr;

    uint64_t            client_id;
    struct conn_info   *next;
} conn_info_t;

conn_info_t conn_listen;
conn_info_t *conn_first = &conn_listen, *conn_last = &conn_listen;

conn_info_t *
conn_info_alloc(void)
{
    static uint64_t client_id;

    conn_info_t *ci = calloc(1, sizeof(conn_info_t));
    ci->client_id = ++client_id;

    conn_last->next = ci;
    conn_last       = ci;

    return ci;
}

void
conn_info_free(conn_info_t *conn_info)
{

}

int main(int argc, char *argv[])
{
    conn_listen.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_listen.fd < 0) {
        fprintf(stderr, "Listen socket creation error: %s\n", strerror(errno));
        return -1;
    }

    conn_listen.addr.sin_family = AF_INET;
    conn_listen.addr.sin_port   = htons(LISTEN_PORT);
    if (inet_aton(LISTEN_ADDR, &conn_listen.addr.sin_addr) == 0) {
        fprintf(stderr, "Bad listen addr: %s\n", LISTEN_ADDR);
        return -1;
    }
    if (bind(conn_listen.fd, (struct sockaddr *)&conn_listen.addr, sizeof(conn_listen.addr)) < 0) {
        fprintf(stderr, "Listen socket binding error: %s\n", strerror(errno));
        return -1;
    }
    if (listen(conn_listen.fd, INT32_MAX) < 0) {
        fprintf(stderr, "Listen socket listen error: %s\n", strerror(errno));
        return -1;
    }



    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        fprintf(stderr, "Epoll instance creation error: %s\n", strerror(errno));
        return -1;
    }
    struct epoll_event ev_push;
    ev_push.events   = EPOLLIN;
    ev_push.data.ptr = (void *)&conn_listen;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_listen.fd, &ev_push) < 0) {
        fprintf(stderr, "Can't add listen socket to epoll: %s\n", strerror(errno));
        return -1;
    }

    struct epoll_event ev_wait[EPOLL_EVENTS];
    while (1) {
        int ev_cnt = epoll_wait(epoll_fd, ev_wait, EPOLL_EVENTS, -1);
        if (ev_cnt < 0) {
            fprintf(stderr, "Epoll wait error: %s\n", strerror(errno));
            return -1;
        }
        for (int iev = 0; iev < ev_cnt; iev++) {
            if (ev_wait[iev].data.ptr == (void *)&conn_listen) {
                conn_info_t *ci = conn_info_alloc();
                if (ci == NULL) {
                    fprintf(stderr, "Can't alloc new connection info; refuse client\n");
                    continue;
                }
                ci->fd = accept(conn_listen.fd, (struct sockaddr *)&ci->addr, &ci->addr_len);
                if (ci->fd < 0) {
                    fprintf(stderr, "Can't accept new client\n");
                    conn_info_free(ci);
                    continue;
                }
                fcntl(ci->fd, F_SETFL, O_NONBLOCK);

                ev_push.events   = EPOLLIN | EPOLLOUT;
                ev_push.data.ptr = (void *)ci;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ci->fd, &ev_push) < 0) {
                    fprintf(stderr, "Can't add client socket to epoll: %s\n", strerror(errno));
                    conn_info_free(ci);
                    continue;
                }
            } else {
                conn_info_t *ci = (conn_info_t *)ev_wait[iev].data.ptr;
                char    rbuf[1024];
                char    wbuf[1024 + 128];
                ssize_t rcount, wcount;

                while (1) {
                    rcount = read(ci->fd, rbuf, sizeof(rbuf) - 1);
                    if (rcount <= 0) {
                        break;
                    }
                    rbuf[rcount] = '\0';
                    wcount = snprintf(wbuf, sizeof(wbuf) - 1, "client %lu say: %s\n",
                                      ci->client_id, rbuf);
                    write(ci->fd, wbuf, wcount);
                }
            }
        }
    }

    return 0;
}
