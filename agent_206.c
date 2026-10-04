/* RemoteOps Agent - IT24102206
 * Port = 7000 + 2410 = 9410 | SID = reverse(2206) = 6022 | Token = OPS-2206 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <ctype.h>

#define REG_NO       "IT24102206"
#define PORT         9410
#define SID          "6022"
#define AUTH_TOKEN   "OPS-2206"
#define LOG_FILE     "remoteops_IT24102206.log"
#define STORAGE_DIR  "./agentfiles/IT24102206"
#define BUF_SIZE     4096
#define LINE_MAX_LEN 1024

/* One of these per connected Controller */
typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    int  port;
    int  authed;          /* 0 until AUTH succeeds */
    char buf[BUF_SIZE];   /* bytes received but not yet consumed */
    int  len;
} conn_t;

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* Timestamped log to file + screen. Mutex stops threads mixing lines. */
static void log_msg(const char *fmt, ...) {
    char ts[32];
    time_t now = time(NULL);
    struct tm tmv;
    va_list ap;
    localtime_r(&now, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);

    pthread_mutex_lock(&log_lock);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "[%s] ", ts);
        va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fprintf(f, "\n");
        fclose(f);
    }
    printf("[%s] ", ts);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&log_lock);
}

/* send() may send fewer bytes than asked, so loop until all are sent */
static int send_all(int fd, const char *data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        sent += (size_t)r;
    }
    return 0;
}

/* Send one response line. ALWAYS appends " SID:6022\n" */
static int reply(conn_t *c, const char *fmt, ...) {
    char body[2048], line[2200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    int n = snprintf(line, sizeof line, "%s SID:%s\n", body, SID);
    return send_all(c->fd, line, (size_t)n);
}

/* Read ONE line (up to '\n') from the connection.
 * Returns 1 = got a line, 0 = client closed, -1 = error.
 * Leftover bytes stay in c->buf for the next call (handles partial
 * lines and several lines arriving in one recv). */
static int read_line(conn_t *c, char *out, size_t outsz) {
    for (;;) {
        char *nl = memchr(c->buf, '\n', (size_t)c->len);
        if (nl) {
            size_t linelen = (size_t)(nl - c->buf);
            size_t copy = linelen < outsz - 1 ? linelen : outsz - 1;
            memcpy(out, c->buf, copy);
            out[copy] = '\0';
            if (copy > 0 && out[copy - 1] == '\r') out[copy - 1] = '\0';
            int consumed = (int)linelen + 1;
            memmove(c->buf, c->buf + consumed, (size_t)(c->len - consumed));
            c->len -= consumed;
            return 1;
        }
        if (c->len >= BUF_SIZE) return -1;   /* line too long */
        ssize_t r = recv(c->fd, c->buf + c->len, (size_t)(BUF_SIZE - c->len), 0);
        if (r == 0) return 0;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        c->len += (int)r;
    }
}

/* ---------- SYSINFO: read real values from /proc ---------- */
static void sysinfo_string(char *out, size_t n) {
    double load = 0.0, up = 0.0;
    long total_kb = 0, avail_kb = 0, v;
    char line[256];

    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%lf", &load) != 1) load = 0.0; fclose(f); }

    f = fopen("/proc/meminfo", "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "MemTotal: %ld kB", &v) == 1) total_kb = v;
            else if (sscanf(line, "MemAvailable: %ld kB", &v) == 1) avail_kb = v;
        }
        fclose(f);
    }

    f = fopen("/proc/uptime", "r");
    if (f) { if (fscanf(f, "%lf", &up) != 1) up = 0.0; fclose(f); }

    /* cpu_load  mem_used_mb  uptime_sec */
    snprintf(out, n, "%.2f %ld %ld", load, (total_kb - avail_kb) / 1024, (long)up);
}

static int handle_sysinfo(conn_t *c) {
    char stats[128];
    sysinfo_string(stats, sizeof stats);
    return reply(c, "OK SYSINFO %s", stats);
}

/* ---------- LISTPROC: snapshot of /proc/<pid>/comm as pid/name,... ---------- */
static int handle_listproc(conn_t *c) {
    char out[1900];
    size_t used = 0;
    out[0] = '\0';

    DIR *d = opendir("/proc");
    if (!d) return reply(c, "ERR 007 PROC_UNAVAILABLE");

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;   /* only PID folders */
        char path[300], name[64] = "?", item[400];
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(name, sizeof name, f)) name[strcspn(name, "\n")] = '\0';
            fclose(f);
        }
        for (char *p = name; *p; p++) if (*p == ' ' || *p == ',') *p = '_';
        int n = snprintf(item, sizeof item, "%s%s/%s", used ? "," : "", e->d_name, name);
        if (used + (size_t)n >= sizeof out) break;             /* reply line is full */
        memcpy(out + used, item, (size_t)n + 1);
        used += (size_t)n;
    }
    closedir(d);
    return reply(c, "OK PROCS %s", out);
}

/* ---------- EXEC: fixed whitelist. User text NEVER reaches the shell ---------- */
static int handle_exec(conn_t *c, const char *name) {
    const char *shellcmd = NULL;
    if      (strcmp(name, "DATE")     == 0) shellcmd = "date";
    else if (strcmp(name, "UPTIME")   == 0) shellcmd = "uptime";
    else if (strcmp(name, "DISKFREE") == 0) shellcmd = "df -h /";
    else if (strcmp(name, "HOSTNAME") == 0) shellcmd = "hostname";
    else if (strcmp(name, "WHOAMI")   == 0) shellcmd = "whoami";

    if (!shellcmd) {
        log_msg("EXEC REJECTED '%s' from %s:%d", name, c->ip, c->port);
        return reply(c, "ERR 002 COMMAND_NOT_ALLOWED");
    }

    char out[1500], line[256];
    size_t used = 0;
    out[0] = '\0';
    FILE *p = popen(shellcmd, "r");
    if (!p) return reply(c, "ERR 008 EXEC_FAILED");
    while (fgets(line, sizeof line, p) && used < sizeof out - 300) {
        line[strcspn(line, "\n")] = '\0';
        used += (size_t)snprintf(out + used, sizeof out - used, "%s%s", used ? " " : "", line);
    }
    pclose(p);
    if (used == 0) return reply(c, "ERR 008 EXEC_FAILED");
    return reply(c, "OK EXEC_RESULT %s", out);
}

/* ---------- File transfer helpers ---------- */
#define MAX_FILE_SIZE (10LL * 1024 * 1024)      /* 10 MB limit for PUT */

/* Only plain names: letters, digits, . _ -  (blocks ../ path tricks) */
static int valid_filename(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > 100 || s[0] == '.') return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-') return 0;
    return 1;
}

/* Read exactly n bytes: first the leftovers already in c->buf, then recv().
 * Writes to 'out' (or throws the bytes away if out is NULL).
 * Returns 0 ok, -1 connection lost, -2 disk write error. */
static int recv_bytes(conn_t *c, FILE *out, long long n) {
    char tmp[BUF_SIZE];
    while (n > 0) {
        if (c->len > 0) {                         /* bytes that arrived with the command */
            int take = (n < c->len) ? (int)n : c->len;
            if (out && fwrite(c->buf, 1, (size_t)take, out) != (size_t)take) return -2;
            memmove(c->buf, c->buf + take, (size_t)(c->len - take));
            c->len -= take;
            n -= take;
            continue;
        }
        size_t want = (n < (long long)sizeof tmp) ? (size_t)n : sizeof tmp;
        ssize_t r = recv(c->fd, tmp, want, 0);
        if (r == 0) return -1;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (out && fwrite(tmp, 1, (size_t)r, out) != (size_t)r) return -2;
        n -= r;
    }
    return 0;
}

/* ---------- PUT <filename> <filesize> + raw bytes ---------- */
static int handle_put(conn_t *c, const char *args) {
    char fname[256];
    long long size = -1;
    if (sscanf(args, "%255s %lld", fname, &size) != 2 || size < 0)
        return reply(c, "ERR 009 BAD_ARGUMENTS");

    int too_big  = size > MAX_FILE_SIZE;
    int bad_name = !valid_filename(fname);
    if (too_big || bad_name) {
        log_msg("PUT REJECTED %s %lld from %s:%d (%s)", fname, size, c->ip, c->port,
                too_big ? "too large" : "bad filename");
        int rr;
        if (too_big) rr = reply(c, "ERR 004 FILE_TOO_LARGE");
        else         rr = reply(c, "ERR 010 BAD_FILENAME");
        if (rr < 0 || size > (1LL << 30)) return -1;
        return recv_bytes(c, NULL, size) == 0 ? 0 : -1;   /* discard the bytes that follow */
    }

    char path[512], tmp[512];
    snprintf(path, sizeof path, "%s/%s", STORAGE_DIR, fname);
    snprintf(tmp, sizeof tmp, "%s/.%s.%d.part", STORAGE_DIR, fname, c->fd);

    FILE *f = fopen(tmp, "wb");                  /* write to a temp name first */
    if (!f) {
        log_msg("PUT FAILED %s: cannot create file", fname);
        if (reply(c, "ERR 011 STORAGE_ERROR") < 0) return -1;
        return recv_bytes(c, NULL, size) == 0 ? 0 : -1;
    }

    int rc = recv_bytes(c, f, size);
    if (fclose(f) != 0 && rc == 0) rc = -2;
    if (rc != 0) {                               /* half-uploaded file is deleted */
        remove(tmp);
        log_msg("PUT FAILED %s from %s:%d (%s)", fname, c->ip, c->port,
                rc == -1 ? "connection lost" : "disk error");
        if (rc == -2) reply(c, "ERR 011 STORAGE_ERROR");
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return reply(c, "ERR 011 STORAGE_ERROR");
    }
    log_msg("PUT OK %s %lld bytes from %s:%d", fname, size, c->ip, c->port);
    return reply(c, "OK FILE_RECEIVED %s", fname);
}

/* ---------- GET <filename> -> OK FILE_SEND name size + raw bytes ---------- */
static int handle_get(conn_t *c, const char *args) {
    char fname[256], path[512];
    if (sscanf(args, "%255s", fname) != 1 || !valid_filename(fname))
        return reply(c, "ERR 005 FILE_NOT_FOUND");

    snprintf(path, sizeof path, "%s/%s", STORAGE_DIR, fname);
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_msg("GET FAILED %s from %s:%d (not found)", fname, c->ip, c->port);
        return reply(c, "ERR 005 FILE_NOT_FOUND");
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        fclose(f);
        return reply(c, "ERR 005 FILE_NOT_FOUND");
    }

    long long size = st.st_size, left = size;
    if (reply(c, "OK FILE_SEND %s %lld", fname, size) < 0) { fclose(f); return -1; }

    char tmp[BUF_SIZE];
    while (left > 0) {
        size_t want = (left < (long long)sizeof tmp) ? (size_t)left : sizeof tmp;
        size_t got = fread(tmp, 1, want, f);
        if (got == 0 || send_all(c->fd, tmp, got) < 0) {
            fclose(f);
            log_msg("GET FAILED %s to %s:%d (transfer interrupted)", fname, c->ip, c->port);
            return -1;
        }
        left -= (long long)got;
    }
    fclose(f);
    log_msg("GET OK %s %lld bytes to %s:%d", fname, size, c->ip, c->port);
    return 0;
}

/* Thread: serves one Controller from connect to disconnect */
static void *client_thread(void *arg) {
    conn_t *c = (conn_t *)arg;
    char line[LINE_MAX_LEN];
    int rc;

    log_msg("CONNECT %s:%d", c->ip, c->port);

    while ((rc = read_line(c, line, sizeof line)) == 1) {
        if (line[0] == '\0') continue;

        /* split "CMD args" at first space */
        char *cmd = line, *rest = "";
        char *sp = strchr(line, ' ');
        if (sp) { *sp = '\0'; rest = sp + 1; }

        /* never write the token into the log */
        if (strcmp(cmd, "AUTH") == 0)
            log_msg("CMD %s:%d AUTH <hidden>", c->ip, c->port);
        else
            log_msg("CMD %s:%d %s %s", c->ip, c->port, cmd, rest);

        /* Rule: nothing but AUTH is allowed before authentication */
        if (!c->authed && strcmp(cmd, "AUTH") != 0) {
            if (reply(c, "ERR 003 NOT_AUTHENTICATED") < 0) break;
            continue;
        }

        if (strcmp(cmd, "AUTH") == 0) {
            if (strcmp(rest, AUTH_TOKEN) == 0) {
                c->authed = 1;
                log_msg("AUTH OK %s:%d", c->ip, c->port);
                if (reply(c, "OK AUTHENTICATED") < 0) break;
            } else {
                log_msg("AUTH FAILED %s:%d", c->ip, c->port);
                if (reply(c, "ERR 001 AUTH_FAILED") < 0) break;
            }
        } else if (strcmp(cmd, "QUIT") == 0) {
            reply(c, "OK BYE");
            break;
        } else if (strcmp(cmd, "SYSINFO") == 0) {
            if (handle_sysinfo(c) < 0) break;
        } else if (strcmp(cmd, "LISTPROC") == 0) {
            if (handle_listproc(c) < 0) break;
        } else if (strcmp(cmd, "EXEC") == 0) {
            if (handle_exec(c, rest) < 0) break;
        } else if (strcmp(cmd, "PUT") == 0) {
            if (handle_put(c, rest) < 0) break;
        } else if (strcmp(cmd, "GET") == 0) {
            if (handle_get(c, rest) < 0) break;
        } else {
            /* SYSINFO, LISTPROC, EXEC, PUT, GET, MONITOR come in Part B */
            if (reply(c, "ERR 006 UNKNOWN_COMMAND") < 0) break;
        }
    }

    if (rc == 0) log_msg("DISCONNECT %s:%d (client closed)", c->ip, c->port);
    else         log_msg("DISCONNECT %s:%d (quit/error)", c->ip, c->port);

    close(c->fd);
    free(c);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);            /* writing to a dead client must not kill us */
    mkdir("./agentfiles", 0755);
    mkdir(STORAGE_DIR, 0755);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(lfd, 16) < 0) { perror("listen"); return 1; }

    log_msg("AGENT STARTED reg=%s port=%d sid=%s", REG_NO, PORT, SID);

    for (;;) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof caddr;
        int cfd = accept(lfd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        conn_t *c = calloc(1, sizeof *c);
        if (!c) { close(cfd); continue; }
        c->fd = cfd;
        inet_ntop(AF_INET, &caddr.sin_addr, c->ip, sizeof c->ip);
        c->port = ntohs(caddr.sin_port);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, c) != 0) {
            log_msg("ERROR pthread_create failed");
            close(cfd); free(c);
            continue;
        }
        pthread_detach(tid);             /* thread cleans itself up */
    }
}
