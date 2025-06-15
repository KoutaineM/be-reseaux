#include "mictcp/mictcp_pdu.h"
#include "mictcp/sliding_window.h"
#include "mictcp/mictcp.h"
#include "mictcp/mictcp_config.h"
#include "mictcp/mictcp_sock_lookup.h"
#include "api/mictcp_core.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

/**
 * @brief Sends application data with reliability checks
 * @param mic_sock Socket descriptor
 * @param msg Data to send
 * @param msg_size Size of data
 * @return Number of bytes sent, -1 on error
 */
int mic_tcp_send(int mic_sock, char *msg, int msg_size) {
    printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_MAGENTA "Sending data (Size: %d bytes)..." ANSI_COLOR_RESET "\n", msg_size);
    
    mic_tcp_sock *sock = get_socket_by_fd(mic_sock);
    if (!sock || sock->state != ESTABLISHED) {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Error: Invalid or non-established socket FD %d" ANSI_COLOR_RESET "\n", mic_sock);
        return -1;
    }
    
    int result;
    char msg_to_send = 1;
    
    while (msg_to_send) {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_YELLOW "Sending packet (Seq: %d)..." ANSI_COLOR_RESET "\n",
               sock->current_seq_num);
        mic_tcp_pdu packet = create_nopayload_pdu(0, 0, 0, sock->current_seq_num, 0,
                                             sock->local_addr.port,
                                             sock->remote_addr.port);
        mic_tcp_payload payload = { .data = msg, .size = msg_size };
        packet.payload = payload;
        unsigned int expected_ack_num = sock->current_seq_num + 1;

        result = IP_send(sock->sys_socket, packet, sock->remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Failed to send packet" ANSI_COLOR_RESET "\n");
            return -1;
        }
        
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_YELLOW "Waiting for ACK..." ANSI_COLOR_RESET "\n");
        pthread_mutex_lock(&sock->lock);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += TIMEOUT / 1000;
        timeout.tv_nsec += (TIMEOUT % 1000) * 1e6;
        if (timeout.tv_nsec > 1e9) {
            timeout.tv_nsec %= (long) 1e9;
        }
        
        result = pthread_cond_timedwait(&sock->cond, &sock->lock, &timeout);
        pthread_mutex_unlock(&sock->lock);
        if (result == ETIMEDOUT) {
            printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_YELLOW "Timeout waiting for ACK..." ANSI_COLOR_RESET "\n");
            if (verify_acceptable_loss(sock)) {
                printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "Loss acceptable, continuing..." ANSI_COLOR_RESET "\n");
                update_sliding_window(sock, 0);
                return 0;
            }
            continue;
        } else if (result != 0) {
            printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Error waiting for ACK: %d" ANSI_COLOR_RESET "\n", result);
            return -1;
        }
        
        // Check if a valid ACK was received (updated by process_client_PDU)
        if (sock->current_seq_num != expected_ack_num) {
            printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_YELLOW "Invalid ACK number (%d != %d), retrying..." 
                   ANSI_COLOR_RESET "\n", sock->current_seq_num, expected_ack_num);
            continue;
        }
        
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "ACK received successfully (Seq: %d)" 
               ANSI_COLOR_RESET "\n", sock->current_seq_num - 1);
        msg_to_send = 0;
        update_sliding_window(sock, 1);
    }
    
    return msg_size;
}

/**
 * @brief Receives data from the socket buffer
 * @param socket Socket descriptor
 * @param msg Buffer to store received data
 * @param max_msg_size Maximum size to receive
 * @return Number of bytes received, -1 on error
 */
int mic_tcp_recv(int socket, char *msg, int max_msg_size) {
    printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_MAGENTA "Receiving data..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_fd(socket);
    if (!sock) {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Invalid socket FD %d" ANSI_COLOR_RESET "\n", socket);
        return -1;
    }
    
    mic_tcp_payload payload_to_receive = { .data = msg, .size = max_msg_size };
    int result = app_buffer_get(payload_to_receive);
    
    printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "Received %d bytes from buffer" ANSI_COLOR_RESET "\n", 
           result);
    return result;
}

/**
 * @brief Closes the socket following MIC-TCP termination procedure
 * @param socket Socket descriptor
 * @return 0 on success, -1 on failure
 */
int mic_tcp_close(int socket) {
    printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_MAGENTA "Closing socket..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_fd(socket);
    if (!sock) {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Invalid socket FD %d" ANSI_COLOR_RESET "\n", socket);
        return -1;
    }
    
    socket_set_state(sock, CLOSING);
    
    mic_tcp_pdu close_req = create_nopayload_pdu(0, 0, 1, 0, 0,
                                                  sock->local_addr.port,
                                                  sock->remote_addr.port);
    
    int attempts = 0;
    int fin_ack_received = 0;
    
    while (!fin_ack_received && attempts < MAX_ATTEMPTS) {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_YELLOW "Sending FIN (Attempt %d)..." ANSI_COLOR_RESET "\n",
               attempts + 1);
        int result = IP_send(sock->sys_socket, close_req, sock->remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Failed to send FIN" ANSI_COLOR_RESET "\n");
            attempts++;
            continue;
        }
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "FIN sent successfully" ANSI_COLOR_RESET "\n");
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5 * TIMEOUT / 1000;
        timeout.tv_nsec += (TIMEOUT % 1000) * 1e6;
        if (timeout.tv_nsec > 1e9) {
            timeout.tv_nsec %= (long) 1e9;
        }

        pthread_mutex_lock(&sock->lock);
        result = pthread_cond_timedwait(&sock->cond, &sock->lock, &timeout);
        pthread_mutex_unlock(&sock->lock);
        if (result == ETIMEDOUT) {
            printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_YELLOW "Timeout waiting for FIN+ACK, retrying..." 
                   ANSI_COLOR_RESET "\n");
            attempts++;
        } else if (result != 0) {
            printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Error waiting for FIN+ACK: %d" ANSI_COLOR_RESET "\n", result);
            return -1;
        } else {
            fin_ack_received = 1;
        }
    }
    
    if (!fin_ack_received) {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_RED "Failed to receive FIN+ACK after %d attempts" 
               ANSI_COLOR_RESET "\n", MAX_ATTEMPTS);
    } else {
        printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "FIN+ACK received successfully" ANSI_COLOR_RESET "\n");
    }
    
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0,
                                                   sock->local_addr.port,
                                                   sock->remote_addr.port);
    IP_send(sock->sys_socket, ack_response, sock->remote_addr.ip_addr);
    
    socket_set_state(sock, CLOSED);
    
    printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "Socket closed successfully" ANSI_COLOR_RESET "\n");
    
    socket_cleanup(sock);
    
    return 0;
}

void socket_cleanup(mic_tcp_sock* sock) {
    pthread_join(sock->listen_thread, NULL);
    close(sock->sys_socket);
    pthread_cond_signal(&sock->cond);
    pthread_cond_destroy(&sock->cond);
    pthread_mutex_destroy(&sock->lock);
}