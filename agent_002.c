/* ============================================================
   RemoteOps Agent - IT24103002
   File: agent_002.c
   Port: 9410 | SID: 2003 | Token: OPS-3002
   Log:  remoteops_IT24103002.log
   Storage: ./agentfiles/IT24103002/
   Sessions implemented so far:
     1. TCP socket setup + bind + listen
     2. Accept loop with thread-per-client concurrency
     3. AUTH command + line-based protocol framing
   ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp */
#include <stdarg.h>       /* va_list for log_message */
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
   Per-client state passed to each thread.
   ============================================================ */
typedef struct {
    int  client_fd;
    char client_ip[INET_ADDRSTRLEN];
    int  authed;                  /* 0 = not authenticated, 1 = authenticated */
} client_info_t;

/* ============================================================
   Thread function: handles one client for its whole lifetime.
   Reads bytes, splits into lines on '\n', dispatches commands.
   ============================================================ */
static void *handle_client(void *arg) {
    client_info_t *info = (client_info_t *)arg;
    int  fd = info->client_fd;

    /* Local copy of the IP so we can free(info) at cleanup */
    char ip[INET_ADDRSTRLEN];
    strncpy(ip, info->client_ip, INET_ADDRSTRLEN - 1);
    ip[INET_ADDRSTRLEN - 1] = '\0';

    log_message("Client connected from %s (fd=%d)", ip, fd);
    printf("Client connected: %s (fd=%d)\n", ip, fd);
    fflush(stdout);

    /* Line accumulator: bytes from recv() are appended here until '\n' */
    char line[LINE_BUF_SIZE];
    int  line_len = 0;

    /* recv buffer: a single recv may return 0.5 lines, 1 line, or many */
    char in[LINE_BUF_SIZE];
    ssize_t n;

    while ((n = recv(fd, in, sizeof(in), 0)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = in[i];

            if (c == '\n') {
                /* We have a complete line in `line` */
                line[line_len] = '\0';

                /* Strip trailing \r if client sent CRLF */
                if (line_len > 0 && line[line_len - 1] == '\r') {
                    line[--line_len] = '\0';
                }

                log_message("[%s] <- %s", ip, line);

                /* ---- Parse: command word + first argument ---- */
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
                        /* Any non-AUTH command before AUTH is rejected */
                        send_line(fd, "ERR 001 AUTH_REQUIRED");
                        log_message("[%s] Rejected (not authenticated): %s",
                                    ip, cmd);
                    }
                    else if (strcasecmp(cmd, "QUIT") == 0) {
                        send_line(fd, "OK BYE");
                        log_message("[%s] QUIT", ip);
                        goto cleanup;
                    }
                    else {
                        /* Real handlers come in Session 4+ */
                        send_line(fd, "ERR 999 NOT_IMPLEMENTED");
                        log_message("[%s] Unimplemented command: %s",
                                    ip, cmd);
                    }
                }

                /* Reset the line buffer for the next line */
                line_len = 0;
            }
            else {
                /* Not a newline - append to accumulator */
                if (line_len < LINE_BUF_SIZE - 1) {
                    line[line_len++] = c;
                } else {
                    /* Line too long: drop and reset to prevent overflow */
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

    /* 1. Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* 2. Allow immediate port reuse after restart */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 3. Bind to port 9410 on all interfaces */
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 4. Listen (backlog 5) */
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Agent listening on port %d (SID:%s)\n", PORT, SID);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    log_message("Agent started on port %d", PORT);

    /* 5. Accept loop - one thread per client */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client_fd < 0) {
            perror("accept");
            continue;                    /* keep serving others */
        }

        /* Allocate a small struct to hand to the thread */
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

        pthread_detach(tid);   /* no join needed - thread cleans itself up */
    }

    close(server_fd);
    return 0;
}
