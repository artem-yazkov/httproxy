#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "cache.h"

#define LISTEN_ADDR   INADDR_ANY
#define LISTEN_PORT   8080

typedef enum fd_type {
    FD_TYPE_LISTEN,
    FD_TYPE_CLIENT,
    FD_TYPE_SERVER
} fd_type_t;

typedef struct fd_info {
    fd_type_t       type;
    cache_request_t request;
} fd_info_t;

typedef struct fd_pool {
    struct pollfd *fds;
    int            fds_cnt;
    int            fds_sz;
    fd_info_t     *infos;
} fd_pool_t;

int main(void)
{
    static fd_pool_t pool;

    pool.fds_sz = sysconf(_SC_OPEN_MAX);
    pool.fds    = malloc(pool.fds_sz * sizeof(pool.fds[0]));
    pool.infos  = malloc(pool.fds_sz * sizeof(pool.infos[0]));

    pool.infos[0].type = FD_TYPE_LISTEN;
    pool.fds[0].fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!pool.fds[0].fd) {
        perror("can't create listen socket");
        return -1;
    }

    int sockopt_reuseaddr = 1;
    int rcode = setsockopt(pool.fds[0].fd,
                           SOL_SOCKET, SO_REUSEADDR,
                           &sockopt_reuseaddr, sizeof(sockopt_reuseaddr));
    if (rcode < 0) {
      perror("can't set listen socket to be reuseable");
      close(pool.fds[0].fd);
      return -1;
    }

    int ioctl_fionbio = 1;
    rcode = ioctl(pool.fds[0].fd, FIONBIO, &ioctl_fionbio);
    if (rcode < 0) {
       perror("can't set listen socket to be nonblocking");
       close(pool.fds[0].fd);
       return -1;
    }

    static struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(LISTEN_ADDR);
    addr.sin_port        = htons(LISTEN_PORT);
    rcode = bind(pool.fds[0].fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rcode < 0) {
        perror("can't bind listen socket");
        close(pool.fds[0].fd);
        return -1;
    }

    rcode = listen(pool.fds[0].fd, SOMAXCONN);
    if (rcode < 0) {
        perror("can't listen socket");
        close(pool.fds[0].fd);
        return -1;
    }

    pool.fds[0].events = POLLIN;
    pool.fds_cnt = 1;

    for (;;) {
        rcode = poll(pool.fds, pool.fds_cnt, -1);
        if (rcode < 0) {
            perror("poll() failed");
            return -1;
        }

        //for (int ifd = pool.fds_cnt - 1; ifd >= 0; ifd--) {
        for (int ifd = 0; ifd < pool.fds_cnt; ifd++) {

            if (pool.fds[ifd].revents == 0) {
                /* no events for this fd; skip */
                continue;
            }
            if (pool.fds[ifd].revents != POLLIN) {
                fprintf(stderr, "unexpected revents: %d\n", pool.fds[ifd].revents);
                return -1;
            }

            /* */
            if (pool.infos[ifd].type == FD_TYPE_LISTEN) {
                /* accept all incoming connections until EWOULDBLOCK errno */

                int fd_cli = 0;
                while (fd_cli >= 0) {
                    fd_cli = accept(pool.fds[ifd].fd, NULL, NULL);

                    if ((fd_cli < 0) && (errno == EWOULDBLOCK)) {
                        /* no more incom */
                        break;
                    } else if (fd_cli < 0) {
                        /* can't accept */
                        perror("can't accept client connection");
                        break;
                    } else {
                        /* new connection was accepted */
                        int ioctl_fionbio = 1;
                        rcode = ioctl(fd_cli, FIONBIO, &ioctl_fionbio);

                        pool.fds[pool.fds_cnt].fd = fd_cli;
                        pool.fds[pool.fds_cnt].events = POLLIN;
                        pool.infos[pool.fds_cnt].type = FD_TYPE_CLIENT;
                        fprintf(stdout, "new connection was accepted: %d\n", pool.fds_cnt);

                        cache_req_init(&pool.infos[pool.fds_cnt].request);
                        pool.fds_cnt++;
                    }
                }

            } else if (pool.infos[ifd].type == FD_TYPE_CLIENT) {

                int rcode = 1;
                while (rcode > 0) {
                    char buffer[80];
                    /* receive all incoming data until EWOULDBLOCK errno */
                    rcode = recv(pool.fds[ifd].fd, buffer, sizeof(buffer), 0);

                    if ((rcode < 0) && (errno == EWOULDBLOCK)) {
                        /* no more data */
                        cache_req_process(&pool.infos[ifd].request);
                        while (pool.infos[ifd].request.state == REQSTATE_FROM_MEMORY) {
                            int rcache = cache_resp_read(&pool.infos[ifd].request, buffer, sizeof(buffer));
                            int rsend = send(pool.fds[ifd].fd, buffer, rcache, 0);
                        }

                        close(pool.fds[ifd].fd);
                        memcpy(&pool.fds[ifd], &pool.fds[ifd+1], (pool.fds_cnt - ifd) * sizeof(pool.fds[0]));
                        memcpy(&pool.infos[ifd], &pool.infos[ifd+1], (pool.fds_cnt - ifd) * sizeof(pool.infos[0]));
                        pool.fds_cnt--; // TODO: fix this

                        break;

                    } else if (rcode < 0) {
                        /* can't accept data block */

                    } else if (rcode == 0) {
                        /* connection was closed */
                        fprintf(stdout, "%d connection was closed\n", ifd);
                        cache_req_clean(&pool.infos[ifd].request);
                        memcpy(&pool.fds[ifd], &pool.fds[ifd+1], (pool.fds_cnt - ifd) * sizeof(pool.fds[0]));
                        memcpy(&pool.infos[ifd], &pool.infos[ifd+1], (pool.fds_cnt - ifd) * sizeof(pool.infos[0]));
                        pool.fds_cnt--; // TODO: fix this

                    } else {
                        /* data block was accepted */
                        cache_req_write(&pool.infos[ifd].request, buffer, rcode);
                    }
                }
            }
        }
    }

    return 0;
}
