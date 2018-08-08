#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
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
    struct conn_info   *prev;
} conn_info_t;

typedef struct conn_info_list {
    conn_info_t *first;
    conn_info_t *last;
    size_t       count;
} conn_info_list_t;

conn_info_list_t conn_list;

conn_info_t *
conn_info_add(void)
{
    static uint64_t client_id;
    conn_info_t *ci = calloc(1, sizeof(conn_info_t));
    ci->client_id = client_id++;

    if (!conn_list.first || !conn_list.last) {
        conn_list.first = conn_list.last = ci;
        conn_list.count = 1;
    } else {
        conn_list.last->next = ci;
        ci->prev = conn_list.last;
        conn_list.last = ci;
        conn_list.count++;
    }
    return ci;
}

void
conn_info_del(conn_info_t *ci)
{
    if (ci->prev) {
        ci->prev->next = ci->next;
    }
    if (ci->next) {
        ci->next->prev = ci->prev;
    }
    if (ci == conn_list.first) {
        conn_list.first = ci->next;
    }
    if (ci == conn_list.last) {
        conn_list.last = ci->prev;
    }
    free(ci);
    conn_list.count--;
}

void
say_hello(conn_info_t *ci)
{
    char wbuf[1024];
    ssize_t wcount = snprintf(wbuf, sizeof(wbuf) - 1,
                              "Hello, client %lu !\n"
                              "Welcome to HTTProxy prototype\n"
                              "type 'quit' for close session, and something else for echo\n\n",
                              ci->client_id);
    if (wcount > 0) {
        write(ci->fd, wbuf, wcount);
    }
}

void
say_echo(conn_info_t *ci, int *hardclose, int *softclose)
{
    char    rbuf[1024];
    char    wbuf[1024 + 128];
    ssize_t rcount, wcount;

    while (1) {
        rcount = read(ci->fd, rbuf, sizeof(rbuf) - 1);
        if (rcount == 0) {
            *hardclose = 1;
            break;
        } else if (rcount < 0) {
            break;
        }

        rbuf[rcount] = '\0';
        for (int itrunc = 1; (rcount - itrunc >= 0) && !isprint(rbuf[rcount - itrunc]); itrunc++) {
            rbuf[rcount - itrunc] = '\0';
        }
        wcount = snprintf(wbuf, sizeof(wbuf) - 1, "client %lu say: %s\n",
                          ci->client_id, rbuf);
        write(ci->fd, wbuf, wcount);

        if (strcmp(rbuf, "quit") == 0) {
            *softclose = 1;
            break;
        }
    }
}

int  sighandler_quit_flag;
void sighandler_quit(int signum)
{
    sighandler_quit_flag = 1;
}


int main(int argc, char *argv[])
{
    conn_info_t *listening = conn_info_add();

    listening->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listening->fd < 0) {
        fprintf(stderr, "Listen socket creation error: %s\n", strerror(errno));
        return -1;
    }
    int sockoption = 1;
    setsockopt(listening->fd, SOL_SOCKET, SO_REUSEADDR, &sockoption, sizeof(sockoption));

    listening->addr.sin_family = AF_INET;
    listening->addr.sin_port   = htons(LISTEN_PORT);
    if (inet_aton(LISTEN_ADDR, &listening->addr.sin_addr) == 0) {
        fprintf(stderr, "Bad listen addr: %s\n", LISTEN_ADDR);
        close(listening->fd);
        return -1;
    }
    if (bind(listening->fd, (struct sockaddr *)&listening->addr, sizeof(listening->addr)) < 0) {
        fprintf(stderr, "Listen socket binding error: %s\n", strerror(errno));
        close(listening->fd);
        return -1;
    }
    if (listen(listening->fd, INT32_MAX) < 0) {
        fprintf(stderr, "Listen socket listen error: %s\n", strerror(errno));
        close(listening->fd);
        return -1;
    }



    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        fprintf(stderr, "Epoll instance creation error: %s\n", strerror(errno));
        close(listening->fd);
        return -1;
    }
    struct epoll_event ev_push;
    ev_push.events   = EPOLLIN;
    ev_push.data.ptr = (void *)listening;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listening->fd, &ev_push) < 0) {
        fprintf(stderr, "Can't add listen socket to epoll: %s\n", strerror(errno));
        close(listening->fd);
        close(epoll_fd);
        return -1;
    }

    struct sigaction sigact;
    sigact.sa_handler = sighandler_quit;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    sigaction(SIGHUP, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);


    struct epoll_event ev_wait[EPOLL_EVENTS];
    while (1) {
        int ev_cnt = epoll_wait(epoll_fd, ev_wait, EPOLL_EVENTS, -1);

        if (sighandler_quit_flag) {
            fprintf(stderr, "HUP|INT|TERM signal was received; exit gracefully\n");
            break;
        }
        if (ev_cnt < 0) {
            fprintf(stderr, "Epoll wait error: %s\n", strerror(errno));
            break;
        }
        for (int iev = 0; iev < ev_cnt; iev++) {
            if (ev_wait[iev].data.ptr == (void *)listening) {
                conn_info_t *ci = conn_info_add();
                if (ci == NULL) {
                    fprintf(stderr, "Can't alloc new connection info; refuse client\n");
                    continue;
                }
                ci->fd = accept(listening->fd, (struct sockaddr *)&ci->addr, &ci->addr_len);
                if (ci->fd < 0) {
                    fprintf(stderr, "Can't accept new client\n");
                    conn_info_del(ci);
                    continue;
                }
                fcntl(ci->fd, F_SETFL, O_NONBLOCK);

                ev_push.events   = EPOLLIN;
                ev_push.data.ptr = (void *)ci;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ci->fd, &ev_push) < 0) {
                    fprintf(stderr, "Can't add client socket to epoll: %s\n", strerror(errno));
                    conn_info_del(ci);
                    continue;
                }
                say_hello(ci);
            } else {
                conn_info_t *ci = (conn_info_t *)ev_wait[iev].data.ptr;
                int hardclose = 0, softclose = 0;

                say_echo(ci, &hardclose, &softclose);

                if (softclose || hardclose) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ci->fd, NULL);
                    close(ci->fd);
                    conn_info_del(ci);
                }
            }
        }
    }

    for (conn_info_t *ci_prev = NULL, *ci = conn_list.first; ci != NULL; ci = ci->next) {
        if (ci_prev != NULL) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ci_prev->fd, NULL);
            close(ci_prev->fd);
            free(ci_prev);
        }
        ci_prev = ci;
        if (ci->next == NULL) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ci->fd, NULL);
            close(ci->fd);
            free(ci);
        }
    }
    close(epoll_fd);

    return 0;
}
