/* ============================================================
   RemoteOps Agent - IT24103002
   File: agent_002.c
   Port: 9410 | SID: 2003 | Token: OPS-3002
   Log:  remoteops_IT24103002.log
   Storage: ./agentfiles/IT24103002/
   Sessions implemented:
     1. TCP socket setup + bind + listen
     2. Accept loop with thread-per-client concurrency
     3. AUTH command + line-based protocol framing
     4. SYSINFO + LISTPROC handlers
     5. EXEC with strict whitelist
     6. PUT file upload with exact byte-count handling
     7. GET file download with exact byte-count handling
     8. UDP monitoring (MONITOR START/STOP)
     9. Robust disconnect handling + signal handling
   ============================================================ */

#define _POSIX_C_SOURCE 200809L   /* for popen/pclose, nanosleep */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>       /* strcasecmp */
#include <stdarg.h>        /* va_list for log_message */
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

/* ---------- Personalised constants ---------- */
#define PORT          9410
#define SID           "2003"
#define AUTH_TOKEN    "OPS-3002"
#define LOG_FILE      "remoteops_IT24103002.log"
#define MAX_CLIENTS   5
#define LINE_BUF_SIZE 4096
#define STORAGE_PATH  "./agentfiles/IT24103002/"
#define MAX_FILE_SIZE (10 * 1024 * 1024)   /* 10 MB cap */
#define MONITOR_INTERVAL_SEC 2

/* ============================================================
   Thread-safe logging
   ============================================================ */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void log_message(const char *format, ...) {
    pthread_mutex_lock(&log_mutex);

    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        time_t now = time(NULL);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(fp, "[%s] ", ts);

        va_list args;
        va_start(args, format);
        vfprintf(fp, format, args);
        va_end(args);

        fprintf(fp, "\n");
        fclose(fp);
    }

    pthread_mutex_unlock(&log_mutex);
}

/* ============================================================
   Send a line to the client, appending the personalised SID tag.
   Every TCP response ends with " SID:2003\n".
   ============================================================ */
static int send_line(int fd, const char *body) {
    char out[LINE_BUF_SIZE];
    int n = snprintf(out, sizeof(out), "%s SID:%s\n", body, SID);
    if (n < 0 || n >= (int)sizeof(out)) {
        log_message("send_line: snprintf overflow for fd=%d", fd);
        return -1;
    }

    ssize_t sent = 0;
    while (sent < n) {
        ssize_t s = send(fd, out + sent, n - sent, 0);
        if (s < 0) {
            log_message("send_line: send() failed fd=%d: %s",
                        fd, strerror(errno));
            return -1;
        }
        if (s == 0) return -1;
        sent += s;
    }
    return 0;
}

/* ============================================================
   Gather system stats - used by SYSINFO and the UDP monitor.
   ============================================================ */
static void read_sys_stats(double *cpu_load,
                           long *mem_used_mb,
                           double *uptime_sec) {
    *cpu_load    = 0.0;
    *mem_used_mb = 0;
    *uptime_sec  = 0.0;

    FILE *fp = fopen("/proc/loadavg", "r");
    if (fp) { fscanf(fp, "%lf", cpu_load); fclose(fp); }

    long mem_total_kb = 0, mem_avail_kb = 0;
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char key[64]; long value; char unit[16];
        while (fscanf(fp, "%63s %ld %15s", key, &value, unit) == 3) {
            if (strcmp(key, "MemTotal:") == 0)      mem_total_kb = value;
            if (strcmp(key, "MemAvailable:") == 0)  mem_avail_kb = value;
            if (mem_total_kb && mem_avail_kb) break;
        }
        fclose(fp);
    }
    if (mem_total_kb > mem_avail_kb)
        *mem_used_mb = (mem_total_kb - mem_avail_kb) / 1024;

    fp = fopen("/proc/uptime", "r");
    if (fp) { fscanf(fp, "%lf", uptime_sec); fclose(fp); }
}

/* ============================================================
   SYSINFO: CPU load, memory used, uptime from /proc.
   ============================================================ */
static void handle_sysinfo(int fd) {
    double cpu_load; long mem_used_mb; double uptime_sec;
    read_sys_stats(&cpu_load, &mem_used_mb, &uptime_sec);

    char body[256];
    snprintf(body, sizeof(body),
             "OK SYSINFO %.2f %ld %.0f",
             cpu_load, mem_used_mb, uptime_sec);
    send_line(fd, body);
}

/* ============================================================
   LISTPROC: process snapshot.
   ============================================================ */
static void handle_listproc(int fd) {
    FILE *fp = popen("ps -eo pid,comm --no-headers | head -20", "r");
    if (!fp) {
        send_line(fd, "ERR 003 INTERNAL_ERROR");
        return;
    }

    char procs[8192] = {0};
    char line[128];
    int  first = 1;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        for (char *q = p; *q; q++) {
            if (*q == ' ') { *q = '-'; break; }
        }

        if (!first) {
            strncat(procs, ",",
                    sizeof(procs) - strlen(procs) - 1);
        }
        strncat(procs, p,
                sizeof(procs) - strlen(procs) - 1);
        first = 0;
    }
    pclose(fp);

    char body[8400];
    snprintf(body, sizeof(body), "OK PROCS %s", procs);
    send_line(fd, body);
}

/* ============================================================
   EXEC <name>: strict whitelist of exactly five commands.
   ============================================================ */
static void handle_exec(int fd, const char *name) {
    if (!name || *name == '\0') {
        send_line(fd, "ERR 002 COMMAND_NOT_ALLOWED");
        return;
    }

    const char *shell_cmd = NULL;

    if      (strcasecmp(name, "DATE")     == 0) shell_cmd = "date";
    else if (strcasecmp(name, "UPTIME")   == 0) shell_cmd = "uptime";
    else if (strcasecmp(name, "DISKFREE") == 0) shell_cmd = "df -h";
    else if (strcasecmp(name, "HOSTNAME") == 0) shell_cmd = "hostname";
    else if (strcasecmp(name, "WHOAMI")   == 0) shell_cmd = "whoami";

    if (!shell_cmd) {
        send_line(fd, "ERR 002 COMMAND_NOT_ALLOWED");
        return;
    }

    FILE *fp = popen(shell_cmd, "r");
    if (!fp) {
        send_line(fd, "ERR 003 INTERNAL_ERROR");
        return;
    }

    char output[2048] = {0};
    char line[256];
    int  first = 1;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        if (!first) {
            strncat(output, " ",
                    sizeof(output) - strlen(output) - 1);
        }
        strncat(output, line,
                sizeof(output) - strlen(output) - 1);
        first = 0;
    }
    pclose(fp);

    char body[2200];
    snprintf(body, sizeof(body), "OK EXEC_RESULT %s", output);
    send_line(fd, body);
}

/* ============================================================
   PUT: receive <filesize> raw bytes and store under STORAGE_PATH.
   ============================================================ */
static void handle_put(int fd, const char *filename,
                       long filesize, const char *ip) {
    if (!filename || *filename == '\0' ||
        strchr(filename, '/') || strstr(filename, "..")) {
        send_line(fd, "ERR 006 INVALID_FILENAME");
        log_message("[%s] PUT rejected (bad filename): %s",
                    ip, filename ? filename : "(null)");
        return;
    }

    if (filesize <= 0 || filesize > MAX_FILE_SIZE) {
        send_line(fd, "ERR 004 FILE_TOO_LARGE");
        log_message("[%s] PUT rejected (size %ld)", ip, filesize);
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s%s", STORAGE_PATH, filename);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        send_line(fd, "ERR 003 INTERNAL_ERROR");
        log_message("[%s] PUT fopen failed: %s", ip, path);
        return;
    }

    char buf[4096];
    long received = 0;

    while (received < filesize) {
        long remaining = filesize - received;
        size_t want = (remaining < (long)sizeof(buf))
                      ? (size_t)remaining
                      : sizeof(buf);

        ssize_t r = recv(fd, buf, want, 0);
        if (r <= 0) {
            fclose(fp);
            remove(path);
            log_message("[%s] PUT failed: conn lost at %ld/%ld bytes",
                        ip, received, filesize);
            return;
        }
        if (fwrite(buf, 1, r, fp) != (size_t)r) {
            fclose(fp);
            remove(path);
            send_line(fd, "ERR 003 INTERNAL_ERROR");
            log_message("[%s] PUT fwrite failed", ip);
            return;
        }
        received += r;
    }

    fclose(fp);

    char body[512];
    snprintf(body, sizeof(body), "OK FILE_RECEIVED %s", filename);
    send_line(fd, body);
    log_message("[%s] PUT OK: %s (%ld bytes)", ip, filename, filesize);
}

/* ============================================================
   GET <filename>: send a file previously stored via PUT.
   ============================================================ */
static void handle_get(int fd, const char *filename, const char *ip) {
    if (!filename || *filename == '\0' ||
        strchr(filename, '/') || strstr(filename, "..")) {
        send_line(fd, "ERR 006 INVALID_FILENAME");
        log_message("[%s] GET rejected (bad filename): %s",
                    ip, filename ? filename : "(null)");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s%s", STORAGE_PATH, filename);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        send_line(fd, "ERR 005 FILE_NOT_FOUND");
        log_message("[%s] GET not found: %s", ip, filename);
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        send_line(fd, "ERR 003 INTERNAL_ERROR");
        return;
    }
    long filesize = ftell(fp);
    if (filesize < 0) {
        fclose(fp);
        send_line(fd, "ERR 003 INTERNAL_ERROR");
        return;
    }
    rewind(fp);

    char header[512];
    snprintf(header, sizeof(header),
             "OK FILE_SEND %s %ld", filename, filesize);
    if (send_line(fd, header) != 0) {
        fclose(fp);
        log_message("[%s] GET header send failed", ip);
        return;
    }

    char buf[4096];
    long sent_total = 0;
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        size_t offset = 0;
        while (offset < n) {
            ssize_t s = send(fd, buf + offset, n - offset, 0);
            if (s <= 0) {
                fclose(fp);
                log_message("[%s] GET aborted at %ld/%ld bytes",
                            ip, sent_total, filesize);
                return;
            }
            offset += s;
            sent_total += s;
        }
    }
    fclose(fp);

    log_message("[%s] GET OK: %s (%ld bytes)", ip, filename, filesize);
}

/* ============================================================
   Monitoring state (one per connected client).
   ============================================================ */
typedef struct {
    pthread_t thread;
    int       active;
    int       running;
    char      client_ip[INET_ADDRSTRLEN];
    int       udp_port;
} monitor_state_t;

/* ============================================================
   Monitoring thread: sends a SYSINFO-like UDP datagram every
   MONITOR_INTERVAL_SEC seconds until active=0.
   ============================================================ */
static void *monitor_thread(void *arg) {
    monitor_state_t *m = (monitor_state_t *)arg;

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        log_message("UDP socket() failed: %s", strerror(errno));
        return NULL;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons((uint16_t)m->udp_port);
    inet_pton(AF_INET, m->client_ip, &dest.sin_addr);

    log_message("UDP monitor START -> %s:%d (interval %ds)",
                m->client_ip, m->udp_port, MONITOR_INTERVAL_SEC);

    while (m->active) {
        double cpu_load; long mem_used_mb; double uptime_sec;
        read_sys_stats(&cpu_load, &mem_used_mb, &uptime_sec);

        char datagram[256];
        snprintf(datagram, sizeof(datagram),
                 "SYSINFO %.2f %ld %.0f SID:%s",
                 cpu_load, mem_used_mb, uptime_sec, SID);

        ssize_t s = sendto(udp_fd, datagram, strlen(datagram), 0,
                           (struct sockaddr *)&dest, sizeof(dest));
        if (s < 0) {
            log_message("UDP sendto failed: %s", strerror(errno));
        }

        int chunks = MONITOR_INTERVAL_SEC * 10;
        struct timespec ts = {0, 100 * 1000 * 1000};  /* 100 ms */
        for (int i = 0; i < chunks && m->active; i++) {
            nanosleep(&ts, NULL);
        }
    }

    close(udp_fd);
    log_message("UDP monitor STOP -> %s:%d", m->client_ip, m->udp_port);
    return NULL;
}

/* ============================================================
   Per-client state passed to each thread.
   ============================================================ */
typedef struct {
    int  client_fd;
    char client_ip[INET_ADDRSTRLEN];
    int  authed;
    monitor_state_t monitor;
} client_info_t;

/* ============================================================
   Monitoring helpers.
   ============================================================ */
static void monitor_start(client_info_t *info, int udp_port) {
    if (udp_port <= 0 || udp_port > 65535) {
        send_line(info->client_fd, "ERR 008 INVALID_PORT");
        log_message("[%s] MONITOR START rejected: bad port %d",
                    info->client_ip, udp_port);
        return;
    }

    if (info->monitor.running) {
        send_line(info->client_fd, "OK MONITOR_STARTED");
        return;
    }

    memset(&info->monitor, 0, sizeof(info->monitor));
    info->monitor.udp_port = udp_port;
    strncpy(info->monitor.client_ip, info->client_ip,
            INET_ADDRSTRLEN - 1);
    info->monitor.client_ip[INET_ADDRSTRLEN - 1] = '\0';
    info->monitor.active  = 1;
    info->monitor.running = 1;

    if (pthread_create(&info->monitor.thread, NULL,
                       monitor_thread, &info->monitor) != 0) {
        info->monitor.running = 0;
        info->monitor.active  = 0;
        send_line(info->client_fd, "ERR 003 INTERNAL_ERROR");
        log_message("[%s] MONITOR START pthread_create failed",
                    info->client_ip);
        return;
    }

    send_line(info->client_fd, "OK MONITOR_STARTED");
    log_message("[%s] MONITOR_STARTED (udp_port=%d)",
                info->client_ip, udp_port);
}

static void monitor_stop(client_info_t *info, int send_ok) {
    if (info->monitor.running) {
        info->monitor.active = 0;
        pthread_join(info->monitor.thread, NULL);
        info->monitor.running = 0;
        log_message("[%s] MONITOR_STOPPED", info->client_ip);
    }
    if (send_ok) {
        send_line(info->client_fd, "OK MONITOR_STOPPED");
    }
}

/* ============================================================
   Thread function: handles one client for its lifetime.
   ============================================================ */
static void *handle_client(void *arg) {
    client_info_t *info = (client_info_t *)arg;
    int  fd = info->client_fd;

    char ip[INET_ADDRSTRLEN];
    strncpy(ip, info->client_ip, INET_ADDRSTRLEN - 1);
    ip[INET_ADDRSTRLEN - 1] = '\0';

    log_message("Client connected from %s (fd=%d)", ip, fd);
    printf("Client connected: %s (fd=%d)\n", ip, fd);
    fflush(stdout);

    char line[LINE_BUF_SIZE];
    int  line_len = 0;

    char in[LINE_BUF_SIZE];
    ssize_t n;

    while ((n = recv(fd, in, sizeof(in), 0)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = in[i];

            if (c == '\n') {
                line[line_len] = '\0';
                if (line_len > 0 && line[line_len - 1] == '\r') {
                    line[--line_len] = '\0';
                }

                log_message("[%s] <- %s", ip, line);

                char cmd[64]   = {0};
                char arg1[256] = {0};
                char arg2[64]  = {0};
                int  matched   = sscanf(line,
                                        "%63s %255s %63s",
                                        cmd, arg1, arg2);

                if (matched >= 1) {
                    if (strcasecmp(cmd, "AUTH") == 0) {
                        if (matched >= 2 &&
                            strcmp(arg1, AUTH_TOKEN) == 0) {
                            info->authed = 1;
                            send_line(fd, "OK AUTHENTICATED");
                            log_message("[%s] AUTH OK", ip);
                        } else {
                            info->authed = 0;
                            send_line(fd, "ERR 001 AUTH_FAILED");
                            log_message("[%s] AUTH FAILED", ip);
                        }
                    }
                    else if (!info->authed) {
                        send_line(fd, "ERR 001 AUTH_REQUIRED");
                        log_message("[%s] Rejected (not authed): %s",
                                    ip, cmd);
                    }
                    else if (strcasecmp(cmd, "SYSINFO") == 0) {
                        handle_sysinfo(fd);
                        log_message("[%s] SYSINFO sent", ip);
                    }
                    else if (strcasecmp(cmd, "LISTPROC") == 0) {
                        handle_listproc(fd);
                        log_message("[%s] LISTPROC sent", ip);
                    }
                    else if (strcasecmp(cmd, "EXEC") == 0) {
                        handle_exec(fd, arg1);
                        log_message("[%s] EXEC %s", ip,
                                    arg1[0] ? arg1 : "(empty)");
                    }
                    else if (strcasecmp(cmd, "GET") == 0) {
                        if (matched < 2) {
                            send_line(fd, "ERR 006 INVALID_FILENAME");
                            log_message("[%s] GET missing filename", ip);
                        } else {
                            handle_get(fd, arg1, ip);
                        }
                    }
                    else if (strcasecmp(cmd, "PUT") == 0) {
                        if (matched < 3) {
                            send_line(fd, "ERR 006 INVALID_FILENAME");
                            log_message("[%s] PUT missing args", ip);
                        } else {
                            long filesize = atol(arg2);
                            handle_put(fd, arg1, filesize, ip);
                        }
                        line_len = 0;
                        i = n;
                        continue;
                    }
                    else if (strcasecmp(cmd, "MONITOR") == 0) {
                        if (strcasecmp(arg1, "START") == 0 &&
                            matched >= 3) {
                            int udp_port = atoi(arg2);
                            monitor_start(info, udp_port);
                        }
                        else if (strcasecmp(arg1, "STOP") == 0) {
                            monitor_stop(info, 1);
                        }
                        else {
                            send_line(fd, "ERR 999 NOT_IMPLEMENTED");
                            log_message("[%s] MONITOR invalid subcmd",
                                        ip);
                        }
                    }
                    else if (strcasecmp(cmd, "QUIT") == 0) {
                        monitor_stop(info, 0);
                        send_line(fd, "OK BYE");
                        log_message("[%s] QUIT", ip);
                        goto cleanup;
                    }
                    else {
                        send_line(fd, "ERR 999 NOT_IMPLEMENTED");
                        log_message("[%s] Unimplemented: %s", ip, cmd);
                    }
                }

                line_len = 0;
            }
            else {
                if (line_len < LINE_BUF_SIZE - 1) {
                    line[line_len++] = c;
                } else {
                    log_message("[%s] Line too long, dropping", ip);
                    line_len = 0;
                }
            }
        }
    }

    if (n < 0) {
        log_message("[%s] recv error: %s", ip, strerror(errno));
    } else if (n == 0) {
        log_message("[%s] Client closed connection cleanly", ip);
    }

cleanup:
    monitor_stop(info, 0);

    log_message("Client disconnected: %s (fd=%d)", ip, fd);
    printf("Client disconnected: %s (fd=%d)\n", ip, fd);
    fflush(stdout);

    close(fd);
    free(info);
    return NULL;
}

/* ============================================================
   Signal handling.
   ============================================================ */
static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_message("Agent shutting down (signal %d)", sig);
        printf("\nAgent shutting down.\n");
        fflush(stdout);
        _exit(EXIT_SUCCESS);
    }
}

static void install_signal_handlers(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
}

/* ============================================================
   main(): socket setup, bind, listen, accept loop.
   ============================================================ */
int main(void) {
    install_signal_handlers();

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Agent listening on port %d (SID:%s)\n", PORT, SID);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    log_message("============================================");
    log_message("RemoteOps Agent starting - IT24103002");
    log_message("Port: %d | SID: %s | Token: %s",
                PORT, SID, AUTH_TOKEN);
    log_message("Storage: %s | Max file: %d bytes",
                STORAGE_PATH, MAX_FILE_SIZE);
    log_message("============================================");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;   /* interrupted by signal */
            perror("accept");
            continue;
        }

        client_info_t *info = malloc(sizeof(client_info_t));
        if (!info) {
            close(client_fd);
            continue;
        }

        memset(info, 0, sizeof(*info));
        info->client_fd = client_fd;
        info->authed    = 0;

        if (!inet_ntop(AF_INET, &client_addr.sin_addr,
                       info->client_ip, INET_ADDRSTRLEN)) {
            strncpy(info->client_ip, "unknown",
                    INET_ADDRSTRLEN - 1);
            info->client_ip[INET_ADDRSTRLEN - 1] = '\0';
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(info);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
