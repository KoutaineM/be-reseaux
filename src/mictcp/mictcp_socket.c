#include "mictcp/mictcp.h"
#include "mictcp/mictcp_pdu.h"
#include "mictcp/sliding_window.h"
#include "mictcp/mictcp_config.h"
#include "api/mictcp_core.h"
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

mic_tcp_sock global_socket;
pthread_mutex_t connection_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t connection_cond = PTHREAD_COND_INITIALIZER;

/**
 * @brief Initializes a new MIC-TCP socket
 * @param sm Start mode (client or server)
 * @return Socket descriptor or -1 on error
 */
int mic_tcp_socket(start_mode sm) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Initializing socket..." ANSI_COLOR_RESET "\n");
    
    int result = initialize_components(sm);
    set_loss_rate(LOSS_RATE);
    
    if (result == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Socket initialization failed" ANSI_COLOR_RESET "\n");
        return -1;
    }
    
    global_socket.fd = 1;
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Socket created successfully (FD: %d)" 
           ANSI_COLOR_RESET "\n", global_socket.fd);
    return global_socket.fd;
}

/**
 * @brief Binds the socket to a local address
 * @param socket Socket descriptor
 * @param addr Address to bind
 * @return 0 on success, -1 on failure
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Binding socket..." ANSI_COLOR_RESET "\n");
    
    global_socket.local_addr = addr;
    global_socket.state = IDLE;
    
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
    
    pthread_mutex_lock(&connection_lock);
    global_socket.state = ACCEPTING;
    
    // Wait for SYN
    if (pthread_cond_wait(&connection_cond, &connection_lock) != 0) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Error waiting for SYN" ANSI_COLOR_RESET "\n");
        pthread_mutex_unlock(&connection_lock);
        return -1;
    }
    
    while (global_socket.state == SYN_RECEIVED) {
        pthread_mutex_unlock(&connection_lock);
        
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending SYN+ACK..." ANSI_COLOR_RESET "\n");
        mic_tcp_pdu response = create_nopayload_pdu(1, 1, 0, 0, 0, 
                                                  global_socket.local_addr.port, 
                                                  global_socket.remote_addr.port);
        int result = IP_send(response, global_socket.remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send SYN+ACK" ANSI_COLOR_RESET "\n");
            continue;
        }
        printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN+ACK sent successfully" ANSI_COLOR_RESET "\n");
        
        pthread_mutex_lock(&connection_lock);
        time_t current_time;
        struct timespec timeout_timestamp;
        time(&current_time);
        timeout_timestamp.tv_sec = current_time + TIMEOUT / 1000;
        timeout_timestamp.tv_nsec = 0;
        
        result = pthread_cond_timedwait(&connection_cond, &connection_lock, &timeout_timestamp);
        if (result != 0) {
            if (result == ETIMEDOUT) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Timeout waiting for ACK, retrying SYN+ACK..." 
                       ANSI_COLOR_RESET "\n");
                continue;
            }
            printf(LOG_PREFIX ANSI_COLOR_RED "Error waiting for ACK: %d" ANSI_COLOR_RESET "\n", result);
            return -1;
        }
    }
    
    pthread_mutex_unlock(&connection_lock);
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Connection accepted successfully" ANSI_COLOR_RESET "\n");
    return 0;
}

/**
 * @brief Sends connection acknowledgment
 * @return 0 on success, -1 on failure
 */
int send_connection_acknowledgement(void) {
    printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending ACK..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0, 
                                                  global_socket.local_addr.port, 
                                                  global_socket.remote_addr.port);
    int result = IP_send(ack_response, global_socket.remote_addr.ip_addr);
    if (result == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send ACK" ANSI_COLOR_RESET "\n");
        return -1;
    }
    
    global_socket.state = ESTABLISHED;
    global_socket.current_seq_num = 1;
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
    
    int result;
    char syn_to_send = 1;
    char synack_received = 0;
    
    while (syn_to_send || !synack_received) {
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending SYN..." ANSI_COLOR_RESET "\n");
        mic_tcp_pdu connect_req = create_nopayload_pdu(1, 0, 0, 0, 0, 
                                                     global_socket.local_addr.port, addr.port);
        result = IP_send(connect_req, addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send SYN" ANSI_COLOR_RESET "\n");
            continue;
        }
        printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN sent successfully" ANSI_COLOR_RESET "\n");
        
        syn_to_send = 0;
        global_socket.state = SYN_SENT;
        
        while (!synack_received) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "Waiting for SYN+ACK..." ANSI_COLOR_RESET "\n");
            mic_tcp_pdu received_pdu;
            received_pdu.payload.size = 0;
            mic_tcp_ip_addr remote_addr;
            
            result = IP_recv(&received_pdu, &global_socket.local_addr.ip_addr, &remote_addr, TIMEOUT);
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
    
    global_socket.remote_addr = addr;
    if (send_connection_acknowledgement() != 0) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Connection establishment failed" ANSI_COLOR_RESET "\n");
        return -1;
    }
    
    global_socket.state = MEASURING_RELIABILITY;
    int received_packets = 0;
    
    // Measure channel reliability
    for (int i = 0; i < MESURING_RELIABILITY_PACKET_NUMBER; i++) {
        mic_tcp_pdu packet = create_nopayload_pdu(0, 0, 0, 0, 0, 
                                                global_socket.local_addr.port, 
                                                global_socket.remote_addr.port);
        packet.payload.data = MESURING_PAYLOAD;
        packet.payload.size = strlen(packet.payload.data);
        
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending reliability Packet %d/%d..." 
               ANSI_COLOR_RESET "\n", i + 1, MESURING_RELIABILITY_PACKET_NUMBER);
        result = IP_send(packet, global_socket.remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send reliability packet %d" 
                   ANSI_COLOR_RESET "\n", i + 1);
            continue;
        }
        
        mic_tcp_pdu received_pdu;
        received_pdu.payload.size = 0;
        mic_tcp_ip_addr remote_addr;
        
        result = IP_recv(&received_pdu, &global_socket.local_addr.ip_addr, &remote_addr, TIMEOUT / 2);
        if (verify_pdu(&received_pdu, 0, 1, 0, 0, 0)) {
            received_packets++;
            printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK received for reliability packet %d" 
                   ANSI_COLOR_RESET "\n", i + 1);
        } else {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "No ACK for reliability packet %d" 
                   ANSI_COLOR_RESET "\n", i + 1);
        }
    }
    
    float success_rate = 100.0 * received_packets / MESURING_RELIABILITY_PACKET_NUMBER;
    float loss_rate = 100.0 - success_rate;
    printf(LOG_PREFIX ANSI_COLOR_CYAN "Channel reliability: %.1f%% (%d packets received out of %d)" 
           ANSI_COLOR_RESET "\n", success_rate, received_packets, MESURING_RELIABILITY_PACKET_NUMBER);
    
    // Set sliding window parameters based on loss rate
    SLIDING_WINDOW_SIZE = 10;
    if (loss_rate < 2.0) {
        SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS = 0;
    } else if (loss_rate >= 2.0 && loss_rate < 5.0) {
        SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS = 1;
    } else if (loss_rate >= 5.0 && loss_rate < 12.0) {
        SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS = 2;
    } else if (loss_rate >= 12.0 && loss_rate <= 20.0) {
        SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS = 3;
    } else {
        printf(LOG_PREFIX ANSI_COLOR_RED "Channel too unreliable (%.1f%%), closing connection..." 
               ANSI_COLOR_RESET "\n", loss_rate);
        return mic_tcp_close(socket);
    }
    
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Connection established with %d%% acceptable loss rate" 
           ANSI_COLOR_RESET "\n", 
           (SLIDING_WINDOW_SIZE - SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS) * 100 / SLIDING_WINDOW_SIZE);
    global_socket.state = ESTABLISHED;
    
    return 0;
}