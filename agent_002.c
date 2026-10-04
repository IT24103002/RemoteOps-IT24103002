/* ============================================================
   RemoteOps Agent - IT24103002
   File: agent_002.c
   Port: 9410 | SID: 2003 | Token: OPS-3002
   Session 1: TCP socket setup only (accept loop next session)
   ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>

#define PORT 9410
#define SID "2003"
# define LOG_FILE "remoteops_IT24103002.log"
#define MAX_CLIENTS 5

//Log Message
/* Mutex protects the log file from concurrent writes by multiple threads */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Thread-safe logger with timestamps */
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

/* Struct to pass per-client info to its thread */
typedef struct {
    int client_fd;
    char client_ip[INET_ADDRSTRLEN];
} client_info_t;

/* Runs in its own thread - handles one client for its lifetime */
static void *handle_client(void *arg) {
    client_info_t *info = (client_info_t *)arg;
    int client_fd = info->client_fd;

    log_message("Client connected from %s (fd=%d)",
                info->client_ip, client_fd);
    printf("Client connected: %s (fd=%d)\n",
           info->client_ip, client_fd);
    fflush(stdout);

    /* Placeholder - echo whatever the client sends back to them.
       Real commands will be added in the next session. */
    char buffer[1024];
    ssize_t n;
    while ((n = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';

        /* Print received data to terminal and log */
        printf("Received from %s: %s\n", info->client_ip, buffer);
        fflush(stdout);
        log_message("Received from %s: %s", info->client_ip, buffer);

        /* Echo back (temporary - real protocol responses later) */
        const char *echo = "OK ECHO SID:2003\n";
        send(client_fd, echo, strlen(echo), 0);
    }

    log_message("Client disconnected: %s (fd=%d)",
                info->client_ip, client_fd);
    printf("Client disconnected: %s (fd=%d)\n",
           info->client_ip, client_fd);
    fflush(stdout);

    close(client_fd);
    free(info);
    return NULL;
}

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

    /* 2. Allow immediate reuse of the port after restart */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 3. Bind to port 9410 on all interfaces */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 4. Listen for incoming connections (backlog = 5) */
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
   }
printf("RemoteOps Agent listening on port %d (SID:%s)\n", PORT, SID);
printf("Press Ctrl+C to stop.\n");
fflush(stdout);

log_message("Agent started on port %d", PORT);

/* 5. Accept loop - spawn a thread for each connecting client */
while (1) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &addr_len);
    if (client_fd < 0) {
        perror("accept");
        continue;   /* don't crash the server; try again */
    }

    /* Fill in the struct we'll hand to the thread */
    client_info_t *info = malloc(sizeof(client_info_t));
    if (!info) {
        close(client_fd);
        continue;
    }
    info->client_fd = client_fd;
    inet_ntop(AF_INET, &client_addr.sin_addr,
              info->client_ip, INET_ADDRSTRLEN);

    /* Create a detached thread so it cleans up automatically when done */
    pthread_t tid;
    if (pthread_create(&tid, NULL, handle_client, info) != 0) {
        perror("pthread_create");
        close(client_fd);
        free(info);
        continue;
    }
    pthread_detach(tid);   /* no need to pthread_join later */
}

close(server_fd);
return 0;

}
