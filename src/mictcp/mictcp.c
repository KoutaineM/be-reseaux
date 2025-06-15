#include "mictcp/mictcp_pdu.h"
#include "mictcp/sliding_window.h"
#include "mictcp/mictcp.h"
#include "mictcp/mictcp_config.h"
#include "mictcp/mictcp_sock_lookup.h"
#include "api/mictcp_core.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Sends application data with reliability checks
 * @param mic_sock Socket descriptor
 * @param msg Data to send
 * @param msg_size Size of data
 * @return Number of bytes sent, -1 on error
 */
int mic_tcp_send(int mic_sock, char *msg, int msg_size) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Sending data (Size: %d bytes)..." ANSI_COLOR_RESET "\n", msg_size);
    
    mic_tcp_sock *sock = get_socket_by_fd(mic_sock);
    if (!sock || sock->state != ESTABLISHED) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Error: Invalid or non-established socket FD %d" ANSI_COLOR_RESET "\n", mic_sock);
        return -1;
    }
    
    mic_tcp_pdu packet = create_nopayload_pdu(0, 0, 0, sock->current_seq_num, 0,
                                             sock->local_addr.port,
                                             sock->remote_addr.port);
    mic_tcp_payload payload = { .data = msg, .size = msg_size };
    packet.payload = payload;
    
    int result;
    char msg_to_send = 1;
    
    while (msg_to_send) {
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending packet (Seq: %d)..." ANSI_COLOR_RESET "\n",
               sock->current_seq_num);
        result = IP_send(sock->sys_socket, packet, sock->remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send packet" ANSI_COLOR_RESET "\n");
            return -1;
        }
        
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Waiting for ACK..." ANSI_COLOR_RESET "\n");
        mic_tcp_pdu received_pdu;
        received_pdu.payload.size = 0;
        mic_tcp_ip_addr remote_ip_addr;
        
        result = IP_recv(sock->sys_socket, &received_pdu, &sock->local_addr.ip_addr, &remote_ip_addr, TIMEOUT);
        if (result == -1) {
            if (sock->current_seq_num == 1) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Resending connection ACK..." ANSI_COLOR_RESET "\n");
                send_connection_acknowledgement(sock);
            }
            if (verify_acceptable_loss(sock)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "Loss acceptable, continuing..." ANSI_COLOR_RESET "\n");
                update_sliding_window(sock, 0);
                return 0;
            }
            continue;
        }
        
        if (verify_pdu(&received_pdu, 1, 1, 0, 0, 0)) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received SYN+ACK, resending connection ACK..." 
                   ANSI_COLOR_RESET "\n");
            send_connection_acknowledgement(sock);
            continue;
        }
        
        if (!verify_pdu(&received_pdu, 0, 1, 0, sock->current_seq_num + 1, 0)) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "Invalid ACK number (%d != %d), updating sequence..." 
                   ANSI_COLOR_RESET "\n", 
                   received_pdu.header.ack_num, sock->current_seq_num + 1);
            sock->current_seq_num = received_pdu.header.ack_num;
            continue;
        }
        
        printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK received successfully (Seq: %d)" 
               ANSI_COLOR_RESET "\n", sock->current_seq_num);
        sock->current_seq_num++;
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
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Receiving data..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_fd(socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Invalid socket FD %d" ANSI_COLOR_RESET "\n", socket);
        return -1;
    }
    
    mic_tcp_payload payload_to_receive = { .data = msg, .size = max_msg_size };
    int result = app_buffer_get(payload_to_receive);
    
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Received %d bytes from buffer" ANSI_COLOR_RESET "\n", 
           result);
    return result;
}

/**
 * @brief Closes the socket following MIC-TCP termination procedure
 * @param socket Socket descriptor
 * @return 0 on success, -1 on failure
 */
int mic_tcp_close(int socket) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Closing socket..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_fd(socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Invalid socket FD %d" ANSI_COLOR_RESET "\n", socket);
        return -1;
    }
    
    sock->state = CLOSING;
    
    mic_tcp_pdu connect_req = create_nopayload_pdu(0, 0, 1, 0, 0,
                                                  sock->local_addr.port,
                                                  sock->remote_addr.port);
    int result = IP_send(sock->sys_socket, connect_req, sock->remote_addr.ip_addr);
    if (result == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send FIN" ANSI_COLOR_RESET "\n");
        return -1;
    }
    printf(LOG_PREFIX ANSI_COLOR_GREEN "FIN sent successfully" ANSI_COLOR_RESET "\n");
    
    mic_tcp_pdu received_pdu;
    result = IP_recv(sock->sys_socket, &received_pdu, &sock->local_addr.ip_addr, NULL, TIMEOUT);
    if (result == -1 || !verify_pdu(&received_pdu, 0, 1, 1, 0, 0)) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to receive FIN+ACK" ANSI_COLOR_RESET "\n");
        return -1;
    }
    printf(LOG_PREFIX ANSI_COLOR_GREEN "FIN+ACK received successfully" ANSI_COLOR_RESET "\n");
    
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0,
                                                   sock->local_addr.port,
                                                   sock->remote_addr.port);
    result = IP_send(sock->sys_socket, ack_response, sock->remote_addr.ip_addr);
    if (result == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send final ACK" ANSI_COLOR_RESET "\n");
        return -1;
    }
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Final ACK sent successfully" ANSI_COLOR_RESET "\n");
    
    sock->state = CLOSED;
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Socket closed successfully" ANSI_COLOR_RESET "\n");
    return 0;
}

/**
 * @brief Processes incoming PDUs based on socket state
 * @param sys_socket System socket descriptor
 * @param pdu Received PDU
 * @param local_addr Local address
 * @param remote_addr Remote address
 */
void process_received_PDU(int sys_socket, mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Processing received PDU..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_sys_fd(sys_socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "No socket found for system FD %d" ANSI_COLOR_RESET "\n", sys_socket);
        return;
    }
    
    switch (sock->state) {
        case ACCEPTING:
            if (verify_pdu(&pdu, 1, 0, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN received" ANSI_COLOR_RESET "\n");
                sock->state = SYN_RECEIVED;
                sock->remote_addr.ip_addr = remote_addr;
                sock->remote_addr.port = pdu.header.source_port;
                pthread_cond_signal(&sock->connection_cond);
            }
            break;
            
        case SYN_RECEIVED:
            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK received, connection established" 
                       ANSI_COLOR_RESET "\n");
                sock->state = ESTABLISHED;
                sock->current_seq_num = 1;
                pthread_cond_signal(&sock->connection_cond);
            }
            break;
            
        case ESTABLISHED:
            if (verify_pdu(&pdu, 0, 0, 0, 0, 0)) {
                if (strcmp(pdu.payload.data, MESURING_PAYLOAD) == 0) {
                    printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received measurement packet, sending ACK..." 
                           ANSI_COLOR_RESET "\n");
                    mic_tcp_pdu acknowledgment = create_nopayload_pdu(0, 1, 0, 0, 0,
                                                                    pdu.header.dest_port,
                                                                    pdu.header.source_port);
                    IP_send(sys_socket, acknowledgment, sock->remote_addr.ip_addr);
                    break;
                }
                
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received data packet (Seq: %d, Expected: %d)" 
                       ANSI_COLOR_RESET "\n", pdu.header.seq_num, sock->current_seq_num);
                
                if (verify_pdu(&pdu, 0, 0, 0, sock->current_seq_num, 0)) {
                    sock->current_seq_num++;
                    printf(LOG_PREFIX ANSI_COLOR_GREEN "Data packet Accepted, using %d Bytes" 
                           ANSI_COLOR_RESET "\n", pdu.payload.size);
                    app_buffer_put(pdu.payload);
                }
                
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending ACK (Ack: %d)..." ANSI_COLOR_RESET "\n",
                       sock->current_seq_num);
                mic_tcp_pdu acknowledgment = create_nopayload_pdu(0, 1, 0, 0,
                                                                sock->current_seq_num,
                                                                pdu.header.dest_port,
                                                                pdu.header.source_port);
                int result = IP_send(sys_socket, acknowledgment, sock->remote_addr.ip_addr);
                if (result == -1) {
                    printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send ACK for Seq %d" 
                           ANSI_COLOR_RESET "\n", sock->current_seq_num);
                } else {
                    printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK sent successfully" ANSI_COLOR_RESET "\n");
                }
            } else if (verify_pdu(&pdu, 0, 0, 1, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received FIN, initiating closure..." 
                       ANSI_COLOR_RESET "\n");
                sock->state = CLOSING;
                
                mic_tcp_pdu fin_ack = create_nopayload_pdu(0, 1, 1, 0, 0,
                                                          pdu.header.dest_port,
                                                          pdu.header.source_port);
                int result = IP_send(sys_socket, fin_ack, remote_addr);
                if (result == -1) {
                    printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send FIN+ACK" ANSI_COLOR_RESET "\n");
                }
            }
            break;
            
        case CLOSING:
            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "Final ACK received, connection closed" 
                       ANSI_COLOR_RESET "\n");
                sock->state = CLOSED;
            }
            break;
            
        case IDLE:
        case CLOSED:
        case SYN_SENT:
        case MEASURING_RELIABILITY:
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "PDU ignored in state %d" ANSI_COLOR_RESET "\n",
                   sock->state);
            break;
    }
}