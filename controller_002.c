/* ============================================================
   RemoteOps Controller - IT24103002
   File: controller_002.c
   Connects to Agent on port 9410
   Session 1: stub only (connection logic added in a later session)
   ============================================================ */

#include <stdio.h>
#include <stdlib.h>

#define SERVER_PORT 9410
#define SERVER_IP   "127.0.0.1"
#define AUTH_TOKEN  "OPS-3002"
#define SID         "2003"

int main(void) {
    printf("RemoteOps Controller (SID:%s)\n", SID);
    printf("Target: %s:%d\n", SERVER_IP, SERVER_PORT);
    printf("Token:  %s\n", AUTH_TOKEN);
    printf("\nConnection logic will be added in a later session.\n");
    return 0;
}
