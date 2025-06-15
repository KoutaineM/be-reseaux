#include "mictcp/mictcp.h"
#include "mictcp/mictcp_pdu.h"
#include "mictcp/sliding_window.h"
#include "mictcp/mictcp_config.h"
#include "mictcp/mictcp_sock_lookup.h"
#include "api/mictcp_core.h"
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

/**
 * @brief Initializes a new MIC-TCP socket
 * @param sm Start mode (client or server)
 * @return Socket descriptor or -1 on error
 */
int mic_tcp_socket(start_mode sm) {

    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Initializing socket..." ANSI_COLOR_RESET "\n");
    
    static int initialized = 0;
    if (!initialized) {
        init_socket_array(); // Statically initialize internal socket array
        initialized = 1;
    }
    
    int sys_socket = initialize_components(sm); // Returns internal system socket
    if (sys_socket == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "System socket initialization failed" ANSI_COLOR_RESET "\n");
        return -1;
    }
    
    set_loss_rate(LOSS_RATE);
    
    int fd = allocate_new_socket(sys_socket);
    
    if (fd == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Socket initialization failed" ANSI_COLOR_RESET "\n");
        close(sys_socket);
        return -1;
    }
    
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Socket created successfully (FD: %d, Sys FD: %d)" 
           ANSI_COLOR_RESET "\n", fd, sys_socket);
    return fd;
}

/**
 * @brief Binds the socket to a local address
 * @param socket Socket descriptor
 * @param addr Address to bind
 * @return 0 on success, -1 on failure
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Binding socket..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_fd(socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Invalid socket FD %d" ANSI_COLOR_RESET "\n", socket);
        return -1;
    }
    
    sock->local_addr = addr;
    socket_set_state(sock, IDLE);
    
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Socket bound successfully to port %d" 
           ANSI_COLOR_RESET "\n", addr.port);
    return 0;
}

/**
 * @brief Accepts incoming connections using three-way handshake
 * @param socket Socket descriptor
 * @param addr Pointer to store remote address
 * @return 0 on success, -1 on failure
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr *addr) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Accepting connection..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_fd(socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Invalid socket FD %d" ANSI_COLOR_RESET "\n", socket);
        return -1;
    }
    
    socket_set_state(sock, ACCEPTING);
    
    pthread_mutex_lock(&sock->lock);
    if (pthread_cond_wait(&sock->cond, &sock->lock) != 0) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Error waiting for SYN" ANSI_COLOR_RESET "\n");
        pthread_mutex_unlock(&sock->lock);
        return -1;
    }
    
    while (sock->state == SYN_RECEIVED) {
        pthread_mutex_unlock(&sock->lock);
        
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending SYN+ACK..." ANSI_COLOR_RESET "\n");
        mic_tcp_pdu response = create_nopayload_pdu(1, 1, 0, 0, 0, 
                                                   sock->local_addr.port, 
                                                   sock->remote_addr.port);
        int result = IP_send(sock->sys_socket, response, sock->remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send SYN+ACK" ANSI_COLOR_RESET "\n");
            pthread_mutex_lock(&sock->lock);
            continue;
        }
        printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN+ACK sent successfully" ANSI_COLOR_RESET "\n");
        
        pthread_mutex_lock(&sock->lock);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += TIMEOUT / 1000;
        timeout.tv_nsec += (TIMEOUT % 1000) * 1e6;
        
        result = pthread_cond_timedwait(&sock->cond, &sock->lock, &timeout);
        if (result != 0) {
            if (result == ETIMEDOUT) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Timeout waiting for ACK, retrying SYN+ACK..." 
                       ANSI_COLOR_RESET "\n");
                continue;
            }
            printf(LOG_PREFIX ANSI_COLOR_RED "Error waiting for ACK: %d" ANSI_COLOR_RESET "\n", result);
            pthread_mutex_unlock(&sock->lock);
            return -1;
        }
    }
    
    pthread_mutex_unlock(&sock->lock);
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Connection accepted successfully" ANSI_COLOR_RESET "\n");
    return 0;
}

/**
 * @brief Sends connection acknowledgment
 * @param sock Socket pointer
 * @return 0 on success, -1 on failure
 */
int send_connection_acknowledgement(mic_tcp_sock *sock) {
    printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending ACK..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0, 
                                                   sock->local_addr.port, 
                                                   sock->remote_addr.port);
    int result = IP_send(sock->sys_socket, ack_response, sock->remote_addr.ip_addr);
    if (result == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send ACK" ANSI_COLOR_RESET "\n");
        return -1;
    }
    
    socket_set_state(sock, ESTABLISHED);
    sock->current_seq_num = 1;
    printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK sent successfully, connection established" 
           ANSI_COLOR_RESET "\n");
    
    return 0;
}

/**
 * @brief Establishes connection to remote address with reliability measurement
 * @param socket Socket descriptor
 * @param addr Remote address
 * @return 0 on success, -1 on failure
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Initiating connection..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_fd(socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Invalid socket FD %d" ANSI_COLOR_RESET "\n", socket);
        return -1;
    }
    
    int result;
    char syn_to_send = 1;
    char synack_received = 0;
    
    while (syn_to_send || !synack_received) {
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending SYN..." ANSI_COLOR_RESET "\n");
        mic_tcp_pdu connect_req = create_nopayload_pdu(1, 0, 0, 0, 0, 
                                                      sock->local_addr.port, addr.port);
        result = IP_send(sock->sys_socket, connect_req, addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send SYN" ANSI_COLOR_RESET "\n");
            continue;
        }
        printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN sent successfully" ANSI_COLOR_RESET "\n");
        
        syn_to_send = 0;
        socket_set_state(sock, SYN_SENT);
        
        while (!synack_received) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "Waiting for SYN+ACK..." ANSI_COLOR_RESET "\n");
            mic_tcp_pdu received_pdu;
            received_pdu.payload.size = 0;
            mic_tcp_ip_addr remote_addr;
            
            result = IP_recv(sock->sys_socket, &received_pdu, &sock->local_addr.ip_addr, &remote_addr, TIMEOUT);
            if (result == -1) {
                printf(LOG_PREFIX ANSI_COLOR_RED "Failed to receive SYN+ACK" ANSI_COLOR_RESET "\n");
                syn_to_send = 1;
                break;
            }
            
            if (!verify_pdu(&received_pdu, 1, 1, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Invalid SYN+ACK received, continuing..." 
                       ANSI_COLOR_RESET "\n");
                continue;
            }
            
            printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN+ACK received successfully" ANSI_COLOR_RESET "\n");
            synack_received = 1;
        }
    }
    
    sock->remote_addr = addr;
    if (send_connection_acknowledgement(sock) != 0) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Connection establishment failed" ANSI_COLOR_RESET "\n");
        return -1;
    }

    if (pthread_create(&sock->listen_thread, NULL, (void *(*)(void *))listening_client, (void *)(intptr_t)sock->sys_socket) != 0) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to create listening thread" ANSI_COLOR_RESET "\n");
        return -1;
    }
    
    socket_set_state(sock, MEASURING_RELIABILITY);
    
    for (int i = 0; i < MESURING_RELIABILITY_PACKET_NUMBER; i++) {
        mic_tcp_pdu packet = create_nopayload_pdu(0, 0, 0, 0, 0, 
                                                 sock->local_addr.port, 
                                                 sock->remote_addr.port);
        packet.payload.data = MESURING_PAYLOAD;
        packet.payload.size = strlen(packet.payload.data);
        
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending reliability Packet %d/%d..." 
               ANSI_COLOR_RESET "\n", i + 1, MESURING_RELIABILITY_PACKET_NUMBER);
        result = IP_send(sock->sys_socket, packet, sock->remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send reliability packet %d" 
                   ANSI_COLOR_RESET "\n", i + 1);
            continue;
        }

        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += TIMEOUT / 1000;

        pthread_mutex_lock(&sock->lock);
        pthread_cond_timedwait(&sock->cond, &sock->lock, &timeout); // Wait for the last ACKs to arrive
        pthread_mutex_unlock(&sock->lock);
    }
    
    pthread_mutex_lock(&sock->lock); // Retreive loss rate
    float success_rate = 100.0 * sock->received_packets / MESURING_RELIABILITY_PACKET_NUMBER;
    pthread_mutex_unlock(&sock->lock);
    float loss_rate = 100.0 - success_rate;
    printf(LOG_PREFIX ANSI_COLOR_CYAN "Channel reliability: %.1f%% (%d packets received out of %d)" 
           ANSI_COLOR_RESET "\n", success_rate, sock->received_packets, MESURING_RELIABILITY_PACKET_NUMBER);
    
    sock->sliding_window_size = 10;
    if (loss_rate < 2.0) {
        sock->sliding_window_consecutive_loss = 0;
    } else if (loss_rate >= 2.0 && loss_rate < 5.0) {
        sock->sliding_window_consecutive_loss = 1;
    } else if (loss_rate >= 5.0 && loss_rate < 12.0) {
        sock->sliding_window_consecutive_loss = 2;
    } else if (loss_rate >= 12.0 && loss_rate <= 20.0) {
        sock->sliding_window_consecutive_loss = 3;
    } else {
        printf(LOG_PREFIX ANSI_COLOR_RED "Channel too unreliable (%.1f%% loss), closing connection..." 
               ANSI_COLOR_RESET "\n", loss_rate);
        mic_tcp_close(socket);
        printf(LOG_PREFIX ANSI_COLOR_RED "Channel too unreliable (%.1f%% loss), connection closed." 
               ANSI_COLOR_RESET "\n", loss_rate);
        return -1;
    }
    
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Connection established with %d%% acceptable loss rate" 
           ANSI_COLOR_RESET "\n", 
           (sock->sliding_window_size - sock->sliding_window_consecutive_loss) * 100 / sock->sliding_window_size);
    socket_set_state(sock, ESTABLISHED);

    return 0;
}

void socket_set_state(mic_tcp_sock* socket, protocol_state state) {
    pthread_mutex_lock(&socket->lock);
    socket->state = state;
    pthread_mutex_unlock(&socket->lock);
}