#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>

#include "tcp_server.h"

void sig_handler(int sig_no) {
    if (sig_no == SIGINT) {
        fprintf(stderr, "SIGNAL: Received SIGINT\n");
        http_abort_signal = 1;
    } else {
        // another signal (SIGUSR1)
        // for test purposes
    }
}

void set_signal_handler() {
    struct sigaction sa;

    memset(&sa, 0 , sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    for (int i = 1; i < MAX_SIGNAL_NUM + 1; ++i) {
        if (i != SIGINT && i != SIGUSR1 && i != SIGKILL && i != SIGSTOP && i != 32 && i != 33) {
            if (sigaction(i, &sa, NULL) == -1) {
                fprintf(stderr, "ERROR: Cannot assign signal(%d) handler\n", i);
                perror("sigaction.");
            }
        }
    }
    sa.sa_handler = &sig_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        fprintf(stderr, "ERROR: Cannot assign SIGINT handler\n");
        perror("sigaction.");
    }
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        fprintf(stderr, "ERROR: Cannot assign SIGUSR1 handler\n");
        perror("sigaction.");
    }
}

int init_listen_server(char *address, uint16_t port) {
    int s;
    struct sockaddr_in socket_address;
    int true_value = 1;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        fprintf(stderr,"ERROR: socket()\n");
        return 0;
    }

    if (setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&true_value,sizeof(true_value)) == -1)
    {
        fprintf(stderr,"ERROR: setsockopt()\n");
        return 0;
    }

    // set as nonblocking
    if (ioctl(s, FIONBIO, (char *)&true_value) == -1) {
        fprintf(stderr,"ERROR: ioctl()\n");
        return 0;
    }

    memset((void *)&socket_address, 0, sizeof(socket_address));
    socket_address.sin_addr.s_addr = inet_addr(address);

    if (!inet_aton(address, &socket_address.sin_addr)) {
        fprintf(stderr,"ERROR: inet_addr()\n");
        errno = EFAULT;
        return 0;
    }

    socket_address.sin_port = htons(port);
    socket_address.sin_family = AF_INET;

    if (bind(s, (struct sockaddr *)&socket_address, sizeof(socket_address)) == -1) {
        fprintf(stderr,"ERROR: bind()\n");
        return 0;
    }

    if (listen(s,SOMAXCONN) == -1) {
        fprintf(stderr,"ERROR: listen()\n");
        return 0;
    }

    // return server socket
    return s;
}

void start_server_loop(int server_socket, connection_handler_p handler) {
    int epoll_fd;
    struct epoll_event einf;
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int nfds;
    int client_socket;

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        fprintf(stderr,"ERROR: epoll_create1()\n");
        perror("epoll_create1()");
        close(server_socket);
        return;
    }

    einf.events = EPOLLIN;
    einf.data.fd = server_socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &einf) == -1) {
        fprintf(stderr,"ERROR: epoll_ctl()\n");
        perror("epoll_ctl()");
        close(server_socket);
        return;
    }

    while(!http_abort_signal) {
        nfds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if (nfds == -1) {
            if (errno != EINTR) {
                fprintf(stderr, "ERROR: epoll_wait()\n");
                perror("epoll_wait()");
                break;
            }
            continue;
        }
        for(int eidx = 0; eidx < nfds; ++eidx) {
            if (events[eidx].data.fd == server_socket) {
                // accept
                client_socket = accept(server_socket, NULL, NULL);
                if (client_socket == -1) {
                    fprintf(stderr,"ERROR: accept()\n");
                    perror("accept()");
                    http_abort_signal = 1;
                    break;
                }
                einf.events = EPOLLIN | EPOLLET;
                einf.data.fd = client_socket;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &einf) == -1) {
                    fprintf(stderr,"ERROR: epoll_ctl(EPOLL_CTL_ADD)\n");
                    perror("epoll_ctl()");
                    close(client_socket);
                }
            } else {
                handler(&events[eidx].data.fd);
            }
        }


    }
    close(server_socket);
}
