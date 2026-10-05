/* ============================================================
   RemoteOps Controller - IT24103002
   File: controller_002.c
   Connects to Agent on 127.0.0.1:9410
   Token: OPS-3002 | SID: 2003

   Interactive commands:
     sysinfo               - request system stats
     listproc              - request process list
     exec <NAME>           - run whitelisted command
     put <local> [remote]  - upload file to agent
     get <remote> [local]  - download file from agent
     monitor start <port>  - begin UDP monitoring on <port>
     monitor stop          - stop UDP monitoring
     help                  - show command list
     quit                  - disconnect and exit
   ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>   /* struct timeval */
#include <time.h>       /* nanosleep */

/* ---------- Personalised constants ---------- */
#define SERVER_IP     "127.0.0.1"
#define SERVER_PORT   9410
#define AUTH_TOKEN    "OPS-3002"
#define SID           "2003"
#define LINE_BUF_SIZE 4096

static int tcp_fd = -1;

/* Ctrl+C flag - set by signal handler, checked by cleanup */
static volatile int g_shutdown = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_shutdown = 1;
}

/* ============================================================
   Send a raw line (adds '\n'). No SID tag - client doesn't add it.
   ============================================================ */
static int tcp_send_line(const char *line) {
    char out[LINE_BUF_SIZE];
    int n = snprintf(out, sizeof(out), "%s\n", line);
    if (n < 0 || n >= (int)sizeof(out)) return -1;

    ssize_t sent = 0;
    while (sent < n) {
        ssize_t s = send(tcp_fd, out + sent, n - sent, 0);
        if (s <= 0) return -1;
        sent += s;
    }
    return 0;
}

/* ============================================================
   Read a single line from the socket (until '\n').
   ============================================================ */
static int tcp_recv_line(char *buf, int maxlen) {
    int len = 0;
    while (len < maxlen - 1) {
        char c;
        ssize_t r = recv(tcp_fd, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\n') { buf[len] = '\0'; return len; }
        if (c == '\r') continue;
        buf[len++] = c;
    }
    buf[len] = '\0';
    return len;
}

/* ============================================================
   Read exactly n bytes from the socket.
   ============================================================ */
static int tcp_recv_exact(char *buf, long n) {
    long got = 0;
    while (got < n) {
        ssize_t r = recv(tcp_fd, buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += r;
    }
    return 0;
}

/* ============================================================
   Simple request/response commands.
   ============================================================ */
static void send_simple(const char *cmd) {
    if (tcp_send_line(cmd) != 0) {
        printf("!! Failed to send %s\n", cmd);
        return;
    }
    char resp[LINE_BUF_SIZE];
    if (tcp_recv_line(resp, sizeof(resp)) < 0) {
        printf("!! No response (agent closed?)\n");
        return;
    }
    printf("%s\n", resp);
}

/* ============================================================
   PUT: upload a local file.
   ============================================================ */
static void do_put(const char *local_path, const char *remote_name) {
    FILE *fp = fopen(local_path, "rb");
    if (!fp) {
        printf("!! Cannot open local file: %s (%s)\n",
               local_path, strerror(errno));
        return;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    rewind(fp);

    if (filesize < 0) { fclose(fp); return; }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "PUT %s %ld", remote_name, filesize);

    if (tcp_send_line(cmd) != 0) {
        printf("!! Failed to send PUT header\n");
        fclose(fp);
        return;
    }

    /* Small delay so header and body arrive in separate TCP segments. */
    struct timespec ts = {0, 50 * 1000 * 1000};   /* 50 ms */
    nanosleep(&ts, NULL);

    /* Stream file bytes */
    char buf[4096];
    size_t n;
    long sent = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        size_t off = 0;
        while (off < n) {
            ssize_t s = send(tcp_fd, buf + off, n - off, 0);
            if (s <= 0) {
                printf("!! Send failed during upload\n");
                fclose(fp);
                return;
            }
            off += s;
            sent += s;
        }
    }
    fclose(fp);

    printf("Sent %ld bytes, waiting for ack...\n", sent);
    char resp[LINE_BUF_SIZE];
    if (tcp_recv_line(resp, sizeof(resp)) < 0) {
        printf("!! No ack from agent\n");
        return;
    }
    printf("%s\n", resp);
}

/* ============================================================
   GET: download a file.
   ============================================================ */
static void do_get(const char *remote_name, const char *local_path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "GET %s", remote_name);

    if (tcp_send_line(cmd) != 0) {
        printf("!! Failed to send GET\n");
        return;
    }

    char header[LINE_BUF_SIZE];
    if (tcp_recv_line(header, sizeof(header)) < 0) {
        printf("!! No response\n");
        return;
    }
    printf("%s\n", header);

    if (strncmp(header, "OK FILE_SEND", 12) != 0) {
        return;   /* error response - nothing more to read */
    }

    char name[256];
    long filesize = 0;
    if (sscanf(header, "OK FILE_SEND %255s %ld", name, &filesize) != 2) {
        printf("!! Could not parse header\n");
        return;
    }

    FILE *fp = fopen(local_path, "wb");
    if (!fp) {
        printf("!! Cannot create local file: %s (%s)\n",
               local_path, strerror(errno));
        return;
    }

    char buf[4096];
    long remaining = filesize;
    while (remaining > 0) {
        size_t want = (remaining < (long)sizeof(buf))
                      ? (size_t)remaining
                      : sizeof(buf);
        if (tcp_recv_exact(buf, want) != 0) {
            printf("!! Connection lost during download\n");
            fclose(fp);
            return;
        }
        fwrite(buf, 1, want, fp);
        remaining -= want;
    }
    fclose(fp);
    printf("Saved %ld bytes to %s\n", filesize, local_path);
}

/* ============================================================
   UDP monitoring receiver thread.
   ============================================================ */
static volatile int udp_active = 0;

static void *udp_receiver(void *arg) {
    int port = *(int *)arg;
    free(arg);

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        printf("!! UDP socket() failed: %s\n", strerror(errno));
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("!! UDP bind(%d) failed: %s\n", port, strerror(errno));
        close(udp_fd);
        return NULL;
    }

    printf("[UDP] Listening on port %d. Type 'monitor stop' to stop.\n",
           port);
    fflush(stdout);

    /* Timeout so we can notice udp_active = 0 */
    struct timeval tv = {0, 200000};   /* 200 ms */
    setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (udp_active) {
        char buf[1024];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        ssize_t r = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&from, &fromlen);
        if (r > 0) {
            buf[r] = '\0';
            printf("[UDP] %s\n", buf);
            fflush(stdout);
        }
    }

    close(udp_fd);
    printf("[UDP] Stopped.\n");
    return NULL;
}

static pthread_t udp_thread;
static int udp_thread_running = 0;

static void monitor_start(const char *port_str) {
    if (udp_thread_running) {
        printf("!! Monitoring already running. Use 'monitor stop' first.\n");
        return;
    }

    int port = atoi(port_str);
    if (port <= 0 || port > 65535) {
        printf("!! Invalid UDP port\n");
        return;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "MONITOR START %d", port);
    if (tcp_send_line(cmd) != 0) {
        printf("!! Failed to send MONITOR START\n");
        return;
    }

    char resp[LINE_BUF_SIZE];
    if (tcp_recv_line(resp, sizeof(resp)) < 0) {
        printf("!! No response\n");
        return;
    }
    printf("%s\n", resp);

    if (strncmp(resp, "OK MONITOR_STARTED", 18) != 0) {
        return;
    }

    udp_active = 1;
    int *port_ptr = malloc(sizeof(int));
    *port_ptr = port;
    if (pthread_create(&udp_thread, NULL, udp_receiver, port_ptr) != 0) {
        printf("!! pthread_create failed\n");
        free(port_ptr);
        udp_active = 0;
        return;
    }
    udp_thread_running = 1;
}

/* Stop UDP monitoring. If send_cmd = 1, sends MONITOR STOP over TCP. */
static void monitor_stop_internal(int send_cmd) {
    if (!udp_thread_running) return;

    if (send_cmd) {
        if (tcp_send_line("MONITOR STOP") != 0) {
            printf("!! Failed to send MONITOR STOP\n");
        } else {
            char resp[LINE_BUF_SIZE];
            if (tcp_recv_line(resp, sizeof(resp)) >= 0) {
                printf("%s\n", resp);
            }
        }
    }

    udp_active = 0;
    pthread_join(udp_thread, NULL);
    udp_thread_running = 0;
}

/* ============================================================
   Help text.
   ============================================================ */
static void print_help(void) {
    printf("Commands:\n");
    printf("  sysinfo                    - system stats\n");
    printf("  listproc                   - list processes\n");
    printf("  exec <NAME>                - run DATE|UPTIME|DISKFREE|HOSTNAME|WHOAMI\n");
    printf("  put <local> [remote]       - upload file (remote defaults to basename)\n");
    printf("  get <remote> [local]       - download file (local defaults to basename)\n");
    printf("  monitor start <udp_port>   - start UDP monitoring\n");
    printf("  monitor stop               - stop UDP monitoring\n");
    printf("  help                       - this message\n");
    printf("  quit                       - disconnect and exit\n");
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ============================================================
   main()
   ============================================================ */
int main(void) {
    /* Install Ctrl+C handler */
    signal(SIGINT, on_sigint);

    /* 1. Connect */
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    if (connect(tcp_fd, (struct sockaddr *)&server,
                sizeof(server)) < 0) {
        fprintf(stderr, "!! Cannot connect to agent at %s:%d - %s\n",
                SERVER_IP, SERVER_PORT, strerror(errno));
        fprintf(stderr, "   Is ./agent_002 running?\n");
        close(tcp_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to RemoteOps Agent at %s:%d\n",
           SERVER_IP, SERVER_PORT);

    /* 2. Authenticate */
    char auth_cmd[128];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s", AUTH_TOKEN);
    if (tcp_send_line(auth_cmd) != 0) {
        fprintf(stderr, "!! Failed to send AUTH\n");
        close(tcp_fd);
        exit(EXIT_FAILURE);
    }

    char resp[LINE_BUF_SIZE];
    if (tcp_recv_line(resp, sizeof(resp)) < 0) {
        fprintf(stderr, "!! No response to AUTH\n");
        close(tcp_fd);
        exit(EXIT_FAILURE);
    }
    printf("%s\n", resp);

    if (strncmp(resp, "OK AUTHENTICATED", 16) != 0) {
        fprintf(stderr, "!! Authentication failed (agent said: %s)\n",
                resp);
        fprintf(stderr, "   Expected token: %s\n", AUTH_TOKEN);
        close(tcp_fd);
        exit(EXIT_FAILURE);
    }

    printf("Type 'help' for commands.\n\n");

    /* 3. Interactive loop */
    char input[LINE_BUF_SIZE];
    while (!g_shutdown) {
        printf("RemoteOps> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            /* EOF (Ctrl+D) or Ctrl+C interrupting the read */
            printf("\n");
            break;
        }
        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0') continue;

        char cmd[64] = {0}, a1[256] = {0}, a2[256] = {0};
        int n = sscanf(input, "%63s %255s %255s", cmd, a1, a2);

        if (n < 1) continue;

        if (strcasecmp(cmd, "quit") == 0 ||
            strcasecmp(cmd, "exit") == 0) {
            break;
        }
        else if (strcasecmp(cmd, "help") == 0) {
            print_help();
        }
        else if (strcasecmp(cmd, "sysinfo") == 0) {
            send_simple("SYSINFO");
        }
        else if (strcasecmp(cmd, "listproc") == 0) {
            send_simple("LISTPROC");
        }
        else if (strcasecmp(cmd, "exec") == 0) {
            if (n < 2) { printf("Usage: exec <NAME>\n"); continue; }
            char c[512];
            snprintf(c, sizeof(c), "EXEC %s", a1);
            send_simple(c);
        }
        else if (strcasecmp(cmd, "put") == 0) {
            if (n < 2) { printf("Usage: put <local> [remote]\n"); continue; }
            const char *remote = (n >= 3) ? a2 : basename_of(a1);
            do_put(a1, remote);
        }
        else if (strcasecmp(cmd, "get") == 0) {
            if (n < 2) { printf("Usage: get <remote> [local]\n"); continue; }
            const char *local = (n >= 3) ? a2 : basename_of(a1);
            do_get(a1, local);
        }
        else if (strcasecmp(cmd, "monitor") == 0) {
            if (n < 2) {
                printf("Usage: monitor start <port> | monitor stop\n");
                continue;
            }
            if (strcasecmp(a1, "start") == 0 && n >= 3) {
                monitor_start(a2);
            }
            else if (strcasecmp(a1, "stop") == 0) {
                monitor_stop_internal(1);
            }
            else {
                printf("Usage: monitor start <port> | monitor stop\n");
            }
        }
        else {
            printf("Unknown command. Type 'help'.\n");
        }
    }

    /* 4. Clean shutdown */
    if (g_shutdown) {
        printf("\nCtrl+C received - cleaning up...\n");
    }

    if (udp_thread_running) {
        monitor_stop_internal(1);   /* send MONITOR STOP + join */
    }

    tcp_send_line("QUIT");
    char final_resp[LINE_BUF_SIZE];
    if (tcp_recv_line(final_resp, sizeof(final_resp)) >= 0) {
        printf("%s\n", final_resp);
    }

    close(tcp_fd);
    printf("Disconnected.\n");
    return 0;
}
