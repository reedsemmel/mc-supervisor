/* Copyright (c) 2021 Reed Semmel */
/* SPDX-License-Identifier: MIT */

/* mcsv.c: a supervisor for containerized Minecraft servers. */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOG_EMERG(fmt, ...) (void) fprintf(stderr, "[[MCSV]] EMERG: " \
fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) (void) fprintf(stderr, "[[MCSV]] WARN: " \
fmt "\n", ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) (void) fprintf(stderr, "[[MCSV]] INFO: " \
fmt "\n", ##__VA_ARGS__)

int
create_signalfd()
{
    int sigfd;
    sigset_t mask;
    (void) sigfillset(&mask);
    /* Do not block hardware exceptions (from signal(7)) */
    (void) sigdelset(&mask, SIGBUS);
    (void) sigdelset(&mask, SIGFPE);
    (void) sigdelset(&mask, SIGILL);
    (void) sigdelset(&mask, SIGSEGV);
    (void) sigdelset(&mask, SIGTRAP);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        LOG_EMERG("failed to set signal mask");
        return -1;
    }
    sigfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigfd == -1) {
        LOG_EMERG("failed to create signalfd: %s", strerror(errno));
    }
    return sigfd;
}

int
create_socket()
{
    int sockfd;
    struct sockaddr_un addr;

    addr.sun_family = AF_UNIX;
    (void) strcpy(addr.sun_path, "/run/mcsv.sock");

    /* First the actual socket syscall */
    sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd == -1) {
        LOG_EMERG("failed to create socket: %s", strerror(errno));
        return -1;
    }

    if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        LOG_EMERG("bind error: %s", strerror(errno));
        return -1;
    }

    if (listen(sockfd, 10) == -1) {
        LOG_EMERG("listen error: %s", strerror(errno));
        return -1;
    }
    return sockfd;
}

int
create_epoll_fd(int sigfd, int sockfd)
{
    int epfd;
    struct epoll_event event;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) {
        LOG_EMERG("failed to create epoll: %s", strerror(errno));
        return -1;
    }
    event.data.fd = sigfd;
    event.events = EPOLLIN;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sigfd, &event) == -1) {
        LOG_EMERG("failed to add signalfd to epoll: %s", strerror(errno));
        return -1;
    }
    event.data.fd = sockfd;
    event.events = EPOLLIN;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event) == -1) {
        LOG_EMERG("failed to add sockfd to epoll: %s", strerror(errno));
        return -1;
    }
    return epfd;
}

int
create_pipe(int pipefd[2])
{
    if (pipe2(pipefd, O_CLOEXEC) == -1) {
        LOG_EMERG("failed to create pipe: %s", strerror(errno));
        return -1;
    }
    return 0;
}

void
exec_child(char **argv, int pipe_stdin)
{
    /* First reset signal handlers. */
    sigset_t mask;
    (void) sigfillset(&mask);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
        LOG_WARN("failed to reset signal mask in child: %s", strerror(errno));
    }

    if (dup2(pipe_stdin, STDIN_FILENO) == -1) {
        LOG_EMERG("failed to set the child's stdin to the pipe");
    }

    if (setpgid(0, 0) == -1) {
        LOG_WARN("failed to put child in new process group: %s",
            strerror(errno));
    }

    (void) execvp(argv[0], argv);

    /* if exec failed, we just exit, and the event loop will catch it */
    LOG_EMERG("exec in child process failed: %s", strerror(errno));
    exit(EXIT_FAILURE);
}

int
handle_signal_event(struct epoll_event *event, int child_stdin, pid_t child)
{
    struct signalfd_siginfo buf;
    ssize_t nr_read;
    pid_t pid;

    nr_read = read(event->data.fd, &buf, sizeof(buf));
    if (nr_read != sizeof(buf)) {
        LOG_WARN("failed to get signal info, skipping: %s", strerror(errno));
        return -1;
    }
    switch (buf.ssi_signo) {
    /* Some common signals to gracefully stop the child */
    case SIGINT:
    case SIGTERM:
    case SIGHUP:
    case SIGQUIT:
        LOG_INFO("received a signal to stop server...");
        if (write(child_stdin, "stop\n", 5) == -1) {
            LOG_WARN("failed to send stop command to server: %s",
                strerror(errno));
        }
        return -1;
    case SIGCHLD:
        pid = waitpid(buf.ssi_pid, NULL, WNOHANG);
        if (pid == -1) {
            LOG_WARN("failed to waitpid on pid received by SIGCHLD");
            return -1;
        }
        if (pid == 0) {
            LOG_WARN("waitpid would have blocked");
            return -1;
        }
        /* zombie process */
        if (pid != child) {
            LOG_INFO("reaped zombie with pid %d", (int) pid);
            return -1;
        }
        /* child process */
        switch (buf.ssi_code) {
        case CLD_EXITED:
            LOG_INFO("child exited with code %d", (int) buf.ssi_status);
            return +buf.ssi_status;
        case CLD_KILLED:
        case CLD_DUMPED:
            LOG_INFO("child killed with signal %d", (int) buf.ssi_status);
            return 1;
        default:
            LOG_WARN("ignoring stops and continues on child");
            return -1;
        }
    default:
        LOG_WARN("ignoring received signal %d", (int)buf.ssi_signo);
        return -1;
    }
}

int
handle_new_connection(struct epoll_event *event, int epfd)
{
    struct epoll_event new_event;

    new_event.data.fd = accept(event->data.fd, NULL, NULL);
    if (new_event.data.fd == -1) {
        LOG_WARN("error accepting new connection: %s", strerror(errno));
        return -1;
    }
    /* this is all we want to do for now. Just add it to the epoll */
    new_event.events = EPOLLIN | EPOLLRDHUP;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, new_event.data.fd, &new_event) == -1) {
        LOG_WARN("failed to add new connection to epoll: %s", strerror(errno));
        (void) close(new_event.data.fd);
    }
    return -1;
}

int
handle_connection(struct epoll_event *event, int epfd, int child_stdin)
{
    ssize_t nr_read;
    static char buf[1024];

    if (event->events & EPOLLIN) {
        nr_read = recv(event->data.fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        if (nr_read == -1) {
            LOG_WARN("fail read from socket conn: %s", strerror(errno));
        } else {
            buf[nr_read] = '\n';
            nr_read = write(child_stdin, buf, nr_read + 1);
            if (nr_read == -1) {
                LOG_WARN("write error to child's stdin: %s", strerror(errno));
            }
        }
    }
    if (event->events & EPOLLRDHUP) {
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, event->data.fd, NULL) == -1) {
            LOG_WARN("error remove closing connection from epoll: %s",
                strerror(errno));
        }
        if (close(event->data.fd) == -1) {
            LOG_WARN("error closing connection: %s", strerror(errno));
        }
    }
    return -1;
}

int
event_loop(int epfd, int sigfd, int sockfd, int child_stdin, pid_t child)
{
    struct epoll_event events[16];
    int nr_ready, i, retval;

    for (;;) {
        nr_ready = epoll_wait(epfd, events, 16, -1);
        if (nr_ready == -1) {
            LOG_EMERG("epoll_wait failed: %s", strerror(errno));
            return EXIT_FAILURE;
        }
        for (i = 0; i < nr_ready; i++) {
            if (events[i].data.fd == sigfd) {
                retval = handle_signal_event(&events[i], child_stdin, child);
            } else
            if (events[i].data.fd == sockfd) {
                retval = handle_new_connection(&events[i], epfd);
            } else {
                retval = handle_connection(&events[i], epfd, child_stdin);
            }
            /* keep going if retval is negative. non-negative represents the
               exit code we want to exit with. */
            if (retval >= 0) {
                return retval;
            }
        }
    }
}

int
main(int argc, char **argv)
{
    int epfd, sigfd, sockfd, retval;
    int pipefd[2];
    pid_t child;

    /* Do not start unless we have arguments */
    if (argc < 2) {
        LOG_EMERG("must provide arguments to run the server");
        exit(EXIT_FAILURE);
    }

    /* Warn if we are not the init process or the root user */
    if (getpid() != 1) {
        LOG_WARN("not running as the init process");
    }
    if (geteuid() != 0) {
        LOG_WARN("not running as root");
    }

    if (create_pipe(pipefd) == -1) {
        return EXIT_FAILURE;
    }

    sigfd = create_signalfd();
    if (sigfd == -1) {
        retval = EXIT_FAILURE;
        goto close_pipe;
    }

    sockfd = create_socket();
    if (sockfd == -1) {
        retval = EXIT_FAILURE;
        goto close_sig;
    }

    epfd = create_epoll_fd(sigfd, sockfd);
    if (epfd == -1) {
        retval = EXIT_FAILURE;
        goto close_sock;
    }

    child = fork();
    if (child < 0) {
        LOG_EMERG("fork failed: %s", strerror(errno));
        retval = EXIT_FAILURE;
        goto close_ep;
    }
    if (child == 0) {
        exec_child(argv + 1, pipefd[0]);
    }

    retval = event_loop(epfd, sigfd, sockfd, pipefd[1], child);

close_ep:
    (void) close(epfd);

close_sock:
    (void) close(sockfd);

close_sig:
    (void) close(sigfd);

close_pipe:
    (void) close(pipefd[1]);
    (void) close(pipefd[0]);

    return retval;
}
