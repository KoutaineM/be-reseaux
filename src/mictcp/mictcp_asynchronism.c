#include "mictcp/mictcp_pdu.h"
#include "mictcp/sliding_window.h"
#include "mictcp/mictcp.h"
#include "mictcp/mictcp_config.h"
#include "mictcp/mictcp_sock_lookup.h"
#include "api/mictcp_core.h"
#include <stdio.h>
#include <string.h>

void handle_awaiting_closing_state(mic_tcp_pdu* pdu, mic_tcp_sock* sock, int sys_socket, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr) {

    if (verify_pdu(pdu, 0, 1, 0, 0, 0)) {
        printf(LOG_PREFIX ANSI_COLOR_GREEN "Final ACK received, connection closed"
                ANSI_COLOR_RESET "\n");
        socket_set_state(sock, CLOSED);
        socket_cleanup(sock);
    } else if (verify_pdu(pdu, 0, 0, 1, 0, 0)) {
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received FIN again, initiating closure..." 
                ANSI_COLOR_RESET "\n");
        mic_tcp_pdu fin_ack = create_nopayload_pdu(0, 1, 1, 0, 0, pdu->header.dest_port, 
                                                    pdu->header.source_port);
        int result = IP_send(sys_socket, fin_ack, remote_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send FIN+ACK" ANSI_COLOR_RESET "\n");
        }
    }

}

/**
 * @brief Processes incoming PDUs based on socket state
 * @param sys_socket System socket descriptor
 * @param pdu Received PDU
 * @param local_addr Local address
 * @param remote_addr Remote address
 */
void process_server_PDU(int sys_socket, mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Processing server PDU..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_sys_fd(sys_socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "No socket found for system FD %d" ANSI_COLOR_RESET "\n", sys_socket);
        return;
    }

    if (sock->state != AWAITING_CLOSING && verify_pdu(&pdu, 0, 0, 1, 0, 0)) {
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received FIN, initiating closure..." 
                ANSI_COLOR_RESET "\n");
        socket_set_state(sock, AWAITING_CLOSING);
        
        mic_tcp_pdu fin_ack = create_nopayload_pdu(0, 1, 1, 0, 0,
                                                    pdu.header.dest_port,
                                                    pdu.header.source_port);
        int result = IP_send(sys_socket, fin_ack, remote_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send FIN+ACK" ANSI_COLOR_RESET "\n");
        }
        return;
    }
    
    switch (sock->state) {
        case ACCEPTING:
            if (verify_pdu(&pdu, 1, 0, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN received" ANSI_COLOR_RESET "\n");
                socket_set_state(sock, SYN_RECEIVED);
                sock->remote_addr.ip_addr = remote_addr;
                sock->remote_addr.port = pdu.header.source_port;
                pthread_cond_signal(&sock->cond);
            }
            break;
            
        case SYN_RECEIVED:
            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK received, connection established" 
                       ANSI_COLOR_RESET "\n");
                socket_set_state(sock, ESTABLISHED);
                sock->current_seq_num = 1;
                pthread_cond_signal(&sock->cond);
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
            }
            break;
            
        case AWAITING_CLOSING:
            handle_awaiting_closing_state(&pdu, sock, sys_socket, local_addr, remote_addr);
            break;
        
        case CLOSING:
            if (verify_pdu(&pdu, 0, 1, 1, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received FIN+ACK..." 
                       ANSI_COLOR_RESET "\n");
                pthread_cond_signal(&sock->cond);
                
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


void listening_client(int sys_socket) {
    printf(LOG_PREFIX ANSI_COLOR_CYAN "Client listening thread started for sys FD %d" ANSI_COLOR_RESET "\n", sys_socket);
    
    while (1) {
        mic_tcp_sock *sock = get_socket_by_sys_fd(sys_socket);
        if (!sock || sock->state == CLOSED) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "Socket closed or invalid, exiting listening thread" ANSI_COLOR_RESET "\n");
            break;
        }
        
        mic_tcp_pdu pdu;
        pdu.payload.size = 0;
        mic_tcp_ip_addr local_addr, remote_addr;
        
        int result = IP_recv(sys_socket, &pdu, &local_addr, &remote_addr, 0);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "No packet received, continuing..." ANSI_COLOR_RESET "\n");
            continue;
        }
        
        process_client_PDU(sys_socket, pdu, local_addr, remote_addr);
    }
}

void process_client_PDU(int sys_socket, mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr) {

    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Processing client PDU..." ANSI_COLOR_RESET "\n");
    
    mic_tcp_sock *sock = get_socket_by_sys_fd(sys_socket);
    if (!sock) {
        printf(LOG_PREFIX ANSI_COLOR_RED "No socket found for System FD %d" ANSI_COLOR_RESET "\n", sys_socket);
        return;
    }

    if (verify_pdu(&pdu, 1, 1, 0, 0, 0)) { // Connection SYN+ACK (means our ACK was not received)
        send_connection_acknowledgement(sock);
        return;
    }
    
    switch (sock->state) {
        case MEASURING_RELIABILITY:
            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) { // Measuring ACK
                pthread_mutex_lock(&sock->lock);
                sock->received_packets++;
                pthread_mutex_unlock(&sock->lock);
                pthread_cond_signal(&sock->cond); // Signal that an ACK was received
            }
            break;
        case ESTABLISHED:
            
            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) { // Data ACK

                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received Data ACK..." 
                       ANSI_COLOR_RESET "\n");
                sock->current_seq_num = pdu.header.ack_num;
                pthread_cond_signal(&sock->cond);

            } else if (verify_pdu(&pdu, 0, 0, 1, 0, 0)) {

                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received FIN, initiating closure..." 
                       ANSI_COLOR_RESET "\n");
                socket_set_state(sock, AWAITING_CLOSING);
                
                mic_tcp_pdu fin_ack = create_nopayload_pdu(0, 1, 1, 0, 0, pdu.header.dest_port, 
                                                          pdu.header.source_port);
                int result = IP_send(sys_socket, fin_ack, remote_addr);
                if (result == -1) {
                    printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send FIN+ACK" ANSI_COLOR_RESET "\n");
                }
            }
            break;
            
        case AWAITING_CLOSING:
            handle_awaiting_closing_state(&pdu, sock, sys_socket, local_addr, remote_addr);
            break;

        case CLOSING:
            if (verify_pdu(&pdu, 0, 1, 1, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received FIN+ACK..." 
                       ANSI_COLOR_RESET "\n");
                pthread_cond_signal(&sock->cond);
            }
            break;
            
        case IDLE:
        case CLOSED:
        case SYN_SENT:
        case ACCEPTING:
        case SYN_RECEIVED:
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "PDU ignored in state %d" ANSI_COLOR_RESET "\n", 
                   sock->state);
            break;
    }
    
    pthread_mutex_unlock(&sock->lock);
}