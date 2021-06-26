/* A helper program for communiating with the socket */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void
print_usage(void)
{
    printf("Usage: mcutil <console|oneshot> [\"oneshot command\"]\n");
}

int
main(int argc, char **argv)
{
    int sockfd, len;
    struct sockaddr_un addr;
    static char buf[1024];
    
    enum {
        CONSOLE,
        ONESHOT,
    } mode;
    
    if (argc != 2 && argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "oneshot") == 0) {
        mode = ONESHOT;
    } else
    if (strcmp(argv[1], "console") == 0) {
        mode = CONSOLE;
    } else {
        print_usage();
        return EXIT_FAILURE;
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
    if (mode == ONESHOT) {
        if (argc != 3) {
            printf("no argument provided (did not you quote your multi-word command?)\n");
            close(sockfd);
            return EXIT_FAILURE;
        }
        len = strlen(argv[2]);
        argv[2][len] = '\n';
        write(sockfd, argv[2], len);
        close(sockfd);
        return EXIT_SUCCESS;
    }

    for (;;) {
        while (fgets(buf, sizeof(buf) - 1, stdin)) {
            if (buf[0] < 0x20) break;
            len = strlen(buf);
            buf[len] = '\n';
            write(sockfd, buf, len);
            if (feof(stdin)) break;

        }
    }
    return EXIT_SUCCESS;
}