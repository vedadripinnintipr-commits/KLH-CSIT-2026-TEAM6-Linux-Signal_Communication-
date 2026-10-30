#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

static volatile sig_atomic_t running = 1;
static void stop_worker(int sig) { (void)sig; running = 0; }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: worker PIPE_FD\n"); return 1; }
    int fd = atoi(argv[1]);
    signal(SIGTERM, stop_worker);
    signal(SIGINT, stop_worker);
    printf("[Worker %d] Started and waiting for messages.\n", getpid());
    fflush(stdout);
    char buffer[512];
    while (running) {
        ssize_t n = read(fd, buffer, sizeof(buffer)-1);
        if (n > 0) {
            buffer[n] = '\0';
            printf("[Worker %d] Received: %s", getpid(), buffer);
            fflush(stdout);
        } else if (n == 0) break;
        else if (errno != EINTR) { perror("read"); break; }
    }
    close(fd);
    printf("[Worker %d] Exiting.\n", getpid());
    return 0;
}
