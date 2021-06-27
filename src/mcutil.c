/* Copyright (c) 2021 Reed Semmel */
/* SPDX-License-Identifier: MIT */

/* mcutil.c: a helper binary for interacting with the supervisor. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void
print_usage(void)
{
    printf("Usage: mcutil [\"oneshot command\"]\n");
}

int
main(int argc, char **argv)
{
    int sockfd;
    ssize_t len;
    struct sockaddr_un addr;
    static char buf[1024];

    if (argc > 2) {
        print_usage();
        printf("make sure to quote your multi-word command\n");
    }

    /* connect to the socket */
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/run/mcsv.sock");
    bind(sockfd, (struct sockaddr *) &addr, sizeof(addr));

    if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        printf("failed to connect to the socket.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        len = strlen(argv[1]);
        argv[1][len] = '\n';
        write(sockfd, argv[1], len + 1);
        close(sockfd);
        return EXIT_SUCCESS;
    }

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        len = strlen(buf);
        if (len == 0) {
            continue;
        }
        buf[len] = '\n';
        write(sockfd, buf, len + 1);
    }
    return EXIT_SUCCESS;
}
