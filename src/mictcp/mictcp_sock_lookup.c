#include "mictcp/mictcp_sock_lookup.h"
#include "mictcp/mictcp_config.h"
#include <stdio.h>

static int next_fd = 0;
socket_entry_t sockets[MAX_SOCKETS];

mic_tcp_sock *get_socket_by_fd(int fd) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].is_used && sockets[i].sock.fd == fd) {
            return &sockets[i].sock;
        }
    }
    return NULL;
}

mic_tcp_sock *get_socket_by_sys_fd(int sys_socket) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].is_used && sockets[i].sock.sys_socket == sys_socket) {
            return &sockets[i].sock;
        }
    }
    return NULL;
}

/**
 * @brief Initializes the socket array
 */
void init_socket_array(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        sockets[i].is_used = 0;
    }
    next_fd = 0;
    printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "Socket Array Initialized" ANSI_COLOR_RESET "\n");
}

/*
*   @brief Allocate a new mic-tcp socket
*/
int allocate_new_socket(int sys_socket) {

    if (next_fd == MAX_SOCKETS) {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "No available socket slots" ANSI_COLOR_RESET "\n");
        return -1;
    }
    int fd = next_fd++;
    sockets[fd].is_used = 1;
    sockets[fd].sock.fd = fd;
    sockets[fd].sock.sys_socket = sys_socket;
    sockets[fd].sock.state = CLOSED;
    sockets[fd].sock.current_seq_num = 0;
    sockets[fd].sock.received_packets = 0;
    pthread_mutex_init(&sockets[fd].sock.lock, NULL);
    pthread_cond_init(&sockets[fd].sock.cond, NULL);

    return fd;

}

