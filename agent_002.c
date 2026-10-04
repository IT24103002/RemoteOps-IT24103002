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
     5. EXEC with strict whitelist (DATE, UPTIME, DISKFREE, HOSTNAME, WHOAMI)
   ============================================================ */

#define _POSIX_C_SOURCE 200809L   /* for popen/pclose */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>       /* strcasecmp */
#include <stdarg.h>        /* va_list for log_message */
#include <unistd.h>
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
    if (n < 0 || n >= (int)sizeof(out)) return -1;

    ssize_t sent = 0;
    while (sent < n) {
        ssize_t s = send(fd, out + sent, n - sent, 0);
        if (s <= 0) return -1;
        sent += s;
    }
    return 0;
}

/* ============================================================
   SYSINFO: read CPU load, memory used, uptime from /proc.
   Response: OK SYSINFO <cpu_load> <mem_used_mb> <uptime_sec>
   ============================================================ */
static void handle_sysinfo(int fd) {
    double cpu_load    = 0.0;
    long   mem_used_mb = 0;
    double uptime_sec  = 0.0;

    /* 1-minute CPU load average from /proc/loadavg */
    FILE *fp = fopen("/proc/loadavg", "r");
    if (fp) {
        fscanf(fp, "%lf", &cpu_load);
        fclose(fp);
    }

    /* Memory used = (MemTotal - MemAvailable) / 1024 */
    long mem_total_kb = 0, mem_avail_kb = 0;
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char key[64];
        long value;
        char unit[16];
        while (fscanf(fp, "%63s %ld %15s", key, &value, unit) == 3) {
            if (strcmp(key, "MemTotal:") == 0)      mem_total_kb = value;
            if (strcmp(key, "MemAvailable:") == 0)  mem_avail_kb = value;
            if (mem_total_kb && mem_avail_kb) break;
        }
        fclose(fp);
    }
    if (mem_total_kb > mem_avail_kb)
        mem_used_mb = (mem_total_kb - mem_avail_kb) / 1024;

    /* Uptime in seconds from /proc/uptime */
    fp = fopen("/proc/uptime", "r");
    if (fp) {
        fscanf(fp, "%lf", &uptime_sec);
        fclose(fp);
    }

    char body[256];
    snprintf(body, sizeof(body),
             "OK SYSINFO %.2f %ld %.0f",
             cpu_load, mem_used_mb, uptime_sec);
    send_line(fd, body);
}

/* ============================================================
   LISTPROC: snapshot of running processes.
   Response: OK PROCS <comma-separated "pid-name" entries>
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

        /* Replace the space between PID and name with '-' */
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
   EXEC <name>: strictly-whitelisted remote commands.
   Whitelist (fixed by §2.3, MUST NOT be extended):
       DATE, UPTIME, DISKFREE, HOSTNAME, WHOAMI
   Anything else -> ERR 002 COMMAND_NOT_ALLOWED.
   ============================================================ */
static void handle_exec(int fd, const char *name) {
    if (!name || *name == '\0') {
        send_line(fd, "ERR 002 COMMAND_NOT_ALLOWED");
        return;
    }

    /* Map each allowed name -> the exact shell command to run */
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

    /* Run the command, capture stdout */
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
   Per-client state passed to each thread.
   ============================================================ */
typedef struct {
    int  client_fd;
    char client_ip[INET_ADDRSTRLEN];
    int  authed;                  /* 0 = not authenticated, 1 = authenticated */
} client_info_t;

/* ============================================================
   Thread function: handles one client for its lifetime.
   Reads bytes, splits into lines on '\n', dispatches commands.
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
                int  matched   = sscanf(line, "%63s %255s", cmd, arg1);

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
                        log_message("[%s] Rejected (not authenticated): %s",
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
                    else if (strcasecmp(cmd, "QUIT") == 0) {
                        send_line(fd, "OK BYE");
                        log_message("[%s] QUIT", ip);
                        goto cleanup;
                    }
                    else {
                        send_line(fd, "ERR 999 NOT_IMPLEMENTED");
                        log_message("[%s] Unimplemented command: %s",
                                    ip, cmd);
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

cleanup:
    log_message("Client disconnected: %s (fd=%d)", ip, fd);
    printf("Client disconnected: %s (fd=%d)\n", ip, fd);
    fflush(stdout);

    close(fd);
    free(info);
    return NULL;
}

/* ============================================================
   main(): create socket, bind, listen, accept loop.
   ============================================================ */
int main(void) {
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

    log_message("Agent started on port %d", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        client_info_t *info = malloc(sizeof(client_info_t));
        if (!info) {
            close(client_fd);
            continue;
        }

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
