/* RemoteOps Controller - IT24102206
 * Extra commands: PUT <localfile> | GET <name> | MONITOR START [udp_port] | MONITOR STOP
 * Any other line is sent to the Agent as it is. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT     9410
#define DEFAULT_UDP_PORT 9500
#define BUF_SIZE         4096

static char rbuf[BUF_SIZE];          /* bytes received from Agent, not yet used */
static int  rlen = 0;

static int udp_fd = -1;              /* UDP monitoring socket */
static pthread_t udp_tid;
static volatile int udp_running = 0;

static int send_all(int fd, const char *d, size_t n) {
    size_t s = 0;
    while (s < n) {
        ssize_t r = send(fd, d + s, n - s, MSG_NOSIGNAL);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        s += (size_t)r;
    }
    return 0;
}

/* Read one '\n'-terminated line. 1 ok, 0 closed, -1 error */
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

/* Read exactly n raw bytes (leftovers in rbuf first). out==NULL discards. */
static int recv_exact(int fd, FILE *out, long long n) {
    char tmp[BUF_SIZE];
    while (n > 0) {
        if (rlen > 0) {
            int take = (n < rlen) ? (int)n : rlen;
            if (out && fwrite(rbuf, 1, (size_t)take, out) != (size_t)take) return -1;
            memmove(rbuf, rbuf + take, (size_t)(rlen - take));
            rlen -= take;
            n -= take;
            continue;
        }
        size_t want = (n < (long long)sizeof tmp) ? (size_t)n : sizeof tmp;
        ssize_t r = recv(fd, tmp, want, 0);
        if (r == 0) return -1;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (out && fwrite(tmp, 1, (size_t)r, out) != (size_t)r) return -1;
        n -= r;
    }
    return 0;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void print_rate(const char *what, long long bytes, double secs) {
    if (secs < 1e-6) secs = 1e-6;
    printf("[%s] %lld bytes in %.3f s = %.0f bytes/sec\n", what, bytes, secs, (double)bytes / secs);
}

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* PUT <localfile>: sends "PUT name size\n" then exactly size raw bytes */
static int do_put(int fd, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        printf("Local file not found: %s\n", path);
        return 0;
    }
    const char *name = base_name(path);
    if (strchr(name, ' ')) { printf("File names with spaces are not supported.\n"); return 0; }
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 0; }

    long long size = st.st_size, left = size;
    char head[512];
    int n = snprintf(head, sizeof head, "PUT %s %lld\n", name, size);
    double t0 = now_sec();
    if (send_all(fd, head, (size_t)n) < 0) { fclose(f); return -1; }

    char tmp[BUF_SIZE];
    while (left > 0) {
        size_t want = (left < (long long)sizeof tmp) ? (size_t)left : sizeof tmp;
        size_t got = fread(tmp, 1, want, f);
        if (got == 0 || send_all(fd, tmp, got) < 0) { fclose(f); return -1; }
        left -= (long long)got;
    }
    fclose(f);

    char resp[2048];
    if (read_line(fd, resp, sizeof resp) <= 0) return -1;
    double t1 = now_sec();
    printf("%s\n", resp);
    if (strncmp(resp, "OK", 2) == 0) print_rate("PUT", size, t1 - t0);
    return 0;
}

/* GET <name>: reads "OK FILE_SEND name size", then exactly size bytes -> downloads/<name> */
static int do_get(int fd, const char *path) {
    const char *name = base_name(path);
    char cmd[512];
    int n = snprintf(cmd, sizeof cmd, "GET %s\n", name);
    double t0 = now_sec();
    if (send_all(fd, cmd, (size_t)n) < 0) return -1;

    char resp[2048];
    if (read_line(fd, resp, sizeof resp) <= 0) return -1;
    printf("%s\n", resp);
    if (strncmp(resp, "OK FILE_SEND", 12) != 0) return 0;      /* error line, nothing follows */

    char fname[256];
    long long size = 0;
    if (sscanf(resp, "OK FILE_SEND %255s %lld", fname, &size) != 2 || size < 0) return -1;

    mkdir("downloads", 0755);
    char outp[512];
    snprintf(outp, sizeof outp, "downloads/%s", name);
    FILE *f = fopen(outp, "wb");
    if (!f) {
        perror("fopen");
        return recv_exact(fd, NULL, size) == 0 ? 0 : -1;      /* must still read the bytes */
    }
    int rc = recv_exact(fd, f, size);
    fclose(f);
    if (rc != 0) { printf("Download failed.\n"); return -1; }
    double t1 = now_sec();
    printf("Saved to %s\n", outp);
    print_rate("GET", size, t1 - t0);
    return 0;
}

/* ---------- UDP monitoring listener ---------- */
static void *udp_listener(void *arg) {
    (void)arg;
    char dg[512];
    while (udp_running) {
        ssize_t r = recv(udp_fd, dg, sizeof dg - 1, 0);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;  /* timeout */
            break;
        }
        dg[r] = '\0';
        printf("\n[UDP] %s\n", dg);
        fflush(stdout);
    }
    return NULL;
}

static int start_udp(int port) {
    if (udp_running) { printf("Monitoring is already running here.\n"); return -1; }
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("udp socket"); return -1; }
    int yes = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct timeval tv = { 1, 0 };                  /* wake up every second to check the flag */
    setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(udp_fd, (struct sockaddr *)&a, sizeof a) < 0) {
        perror("udp bind"); close(udp_fd); udp_fd = -1; return -1;
    }
    udp_running = 1;
    if (pthread_create(&udp_tid, NULL, udp_listener, NULL) != 0) {
        udp_running = 0; close(udp_fd); udp_fd = -1; return -1;
    }
    printf("Listening for UDP monitoring datagrams on port %d\n", port);
    return 0;
}

static void stop_udp(void) {
    if (!udp_running) return;
    udp_running = 0;
    pthread_join(udp_tid, NULL);
    close(udp_fd);
    udp_fd = -1;
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
    printf("Connected to %s:%d  (QUIT to exit)\n", host, port);

    char line[1024], resp[2048], out[1100];
    for (;;) {
        printf("remoteops> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        if (strncmp(line, "PUT ", 4) == 0) {
            if (do_put(fd, line + 4) < 0) { printf("Connection problem.\n"); break; }
            continue;
        }
        if (strncmp(line, "GET ", 4) == 0) {
            if (do_get(fd, line + 4) < 0) { printf("Connection problem.\n"); break; }
            continue;
        }
        if (strncmp(line, "MONITOR START", 13) == 0) {
            int p = atoi(line + 13);
            if (p <= 0) p = DEFAULT_UDP_PORT;
            if (start_udp(p) < 0) continue;          /* open our UDP port BEFORE asking the Agent */
            int n = snprintf(out, sizeof out, "MONITOR START %d\n", p);
            if (send_all(fd, out, (size_t)n) < 0) { perror("send"); break; }
            if (read_line(fd, resp, sizeof resp) <= 0) { printf("Agent closed the connection.\n"); break; }
            printf("%s\n", resp);
            if (strncmp(resp, "OK", 2) != 0) stop_udp();
            continue;
        }

        int n = snprintf(out, sizeof out, "%s\n", line);
        if (send_all(fd, out, (size_t)n) < 0) { perror("send"); break; }
        if (read_line(fd, resp, sizeof resp) <= 0) { printf("Agent closed the connection.\n"); break; }
        printf("%s\n", resp);

        if (strcmp(line, "MONITOR STOP") == 0) stop_udp();
        if (strcmp(line, "QUIT") == 0) break;
    }
    stop_udp();
    close(fd);
    return 0;
}
