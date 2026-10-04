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

#define PORT 9410
#define SID "2003"

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

    printf("RemoteOps Agent listening on port %d (SID:%s)\n",
           PORT, SID);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    /* 5. Placeholder - accept loop added next session */
    while (1) {
        pause();
    }

    close(server_fd);
    return 0;
}
