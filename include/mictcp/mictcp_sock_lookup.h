#ifndef MICTCP_SOCK_LOOKUP_H
#define MICTCP_SOCK_LOOKUP_H

#include "mictcp.h"

// Socket storage structure
typedef struct {
    mic_tcp_sock sock;    // MIC-TCP socket
    int is_used;          // 1 if slot is occupied, 0 if free
} socket_entry_t;

/**
 * @brief Looks up a socket by file descriptor
 * @param fd File descriptor
 * @return Pointer to socket or NULL if not found
 */
mic_tcp_sock *get_socket_by_fd(int fd);

/**
 * @brief Looks up a socket by system socket descriptor
 * @param sys_socket System socket descriptor
 * @return Pointer to socket or NULL if not found
 */
mic_tcp_sock *get_socket_by_sys_fd(int sys_socket);

int allocate_new_socket(int sys_socket);
void init_socket_array(void);

#endif