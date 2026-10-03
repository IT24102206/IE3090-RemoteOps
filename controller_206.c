/* RemoteOps Controller - IT24102206 (basic version) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 9410
#define BUF_SIZE 4096

static char rbuf[BUF_SIZE];
static int  rlen = 0;

static int send_all(int fd, const char *d, size_t n) {
    size_t s = 0;
    while (s < n) {
        ssize_t r = send(fd, d + s, n - s, MSG_NOSIGNAL);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        s += (size_t)r;
    }
    return 0;
}

/* Read one '\n'-terminated line from the Agent. 1 ok, 0 closed, -1 error */
static int read_line(int fd, char *out, size_t outsz) {
    for (;;) {
        char *nl = memchr(rbuf, '\n', (size_t)rlen);
        if (nl) {
            size_t ll = (size_t)(nl - rbuf);
            size_t cp = ll < outsz - 1 ? ll : outsz - 1;
            memcpy(out, rbuf, cp);
            out[cp] = '\0';
            int used = (int)ll + 1;
            memmove(rbuf, rbuf + used, (size_t)(rlen - used));
            rlen -= used;
            return 1;
        }
        if (rlen >= BUF_SIZE) return -1;
        ssize_t r = recv(fd, rbuf + rlen, (size_t)(BUF_SIZE - rlen), 0);
        if (r == 0) return 0;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        rlen += (int)r;
    }
}

int main(int argc, char *argv[]) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : DEFAULT_PORT;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
        fprintf(stderr, "Bad IP address: %s\n", host);
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { perror("connect"); return 1; }
    printf("Connected to %s:%d  (type commands, QUIT to exit)\n", host, port);

    char line[1024], resp[2048];
    for (;;) {
        printf("remoteops> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        char out[1100];
        int n = snprintf(out, sizeof out, "%s\n", line);
        if (send_all(fd, out, (size_t)n) < 0) { perror("send"); break; }

        int rc = read_line(fd, resp, sizeof resp);
        if (rc <= 0) { printf("Agent closed the connection.\n"); break; }
        printf("%s\n", resp);

        if (strcmp(line, "QUIT") == 0) break;
    }
    close(fd);
    return 0;
}
