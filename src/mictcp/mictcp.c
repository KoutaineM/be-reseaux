#include "mictcp/mictcp_pdu.h"
#include "mictcp/sliding_window.h"
#include "mictcp/mictcp.h"
#include "mictcp/mictcp_config.h"
#include "api/mictcp_core.h"
#include <stdio.h>
#include <string.h>

extern mic_tcp_sock global_socket;
extern pthread_mutex_t connection_lock;
extern pthread_cond_t connection_cond;

/**
 * @brief Sends application data with reliability checks
 * @param mic_sock Socket descriptor
 * @param msg Data to send
 * @param msg_size Size of data
 * @return Number of bytes sent, -1 on error
 */
int mic_tcp_send(int mic_sock, char *msg, int msg_size) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Sending data (Size: %d bytes)..." ANSI_COLOR_RESET "\n", msg_size);
    
    if (global_socket.state != ESTABLISHED) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Error: socket not in ESTABLISHED State" ANSI_COLOR_RESET "\n");
        return -1;
    }
    
    mic_tcp_pdu packet = create_nopayload_pdu(0, 0, 0, global_socket.current_seq_num, 0,
                                            global_socket.local_addr.port,
                                            global_socket.remote_addr.port);
    mic_tcp_payload payload = { .data = msg, .size = msg_size };
    packet.payload = payload;
    
    int result;
    char msg_to_send = 1;
    
    while (msg_to_send) {
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending packet (Seq: %d)..." ANSI_COLOR_RESET "\n",
               global_socket.current_seq_num);
        result = IP_send(packet, global_socket.remote_addr.ip_addr);
        if (result == -1) {
            printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send packet" ANSI_COLOR_RESET "\n");
            return -1;
        }
        
        printf(LOG_PREFIX ANSI_COLOR_YELLOW "Waiting for ACK..." ANSI_COLOR_RESET "\n");
        mic_tcp_pdu received_pdu;
        received_pdu.payload.size = 0;
        mic_tcp_ip_addr remote_ip_addr;
        
        result = IP_recv(&received_pdu, &global_socket.local_addr.ip_addr, &remote_ip_addr, TIMEOUT);
        if (result == -1) {
            if (global_socket.current_seq_num == 1) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Resending connection ACK..." ANSI_COLOR_RESET "\n");
                send_connection_acknowledgement();
            }
            if (verify_acceptable_loss()) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "Loss acceptable, continuing..." ANSI_COLOR_RESET "\n");
                update_sliding_window(0);
                return 0;
            }
            continue;
        }
        
        if (verify_pdu(&received_pdu, 1, 1, 0, 0, 0)) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received SYN+ACK, resending connection ACK..." 
                   ANSI_COLOR_RESET "\n");
            send_connection_acknowledgement();
            continue;
        }
        
        if (!verify_pdu(&received_pdu, 0, 1, 0, global_socket.current_seq_num + 1, 0)) {
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "Invalid ACK number (%d != %d), updating sequence..." 
                   ANSI_COLOR_RESET "\n", 
                   received_pdu.header.ack_num, global_socket.current_seq_num + 1);
            global_socket.current_seq_num = received_pdu.header.ack_num;
            continue;
        }
        
        printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK received successfully (Seq: %d)" 
               ANSI_COLOR_RESET "\n", global_socket.current_seq_num);
        global_socket.current_seq_num++;
        msg_to_send = 0;
        update_sliding_window(1);
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
    
    global_socket.state = CLOSING;
    
    mic_tcp_pdu connect_req = create_nopayload_pdu(0, 0, 1, 0, 0,
                                                 global_socket.local_addr.port,
                                                 global_socket.remote_addr.port);
    int result = IP_send(connect_req, global_socket.remote_addr.ip_addr);
    if (result == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send FIN" ANSI_COLOR_RESET "\n");
        return -1;
    }
    printf(LOG_PREFIX ANSI_COLOR_GREEN "FIN sent successfully" ANSI_COLOR_RESET "\n");
    
    mic_tcp_pdu received_pdu;
    result = IP_recv(&received_pdu, &global_socket.local_addr.ip_addr, NULL, TIMEOUT);
    if (result == -1 || !verify_pdu(&received_pdu, 0, 1, 1, 0, 0)) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to receive FIN+ACK" ANSI_COLOR_RESET "\n");
        return -1;
    }
    printf(LOG_PREFIX ANSI_COLOR_GREEN "FIN+ACK received successfully" ANSI_COLOR_RESET "\n");
    
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0,
                                                  global_socket.local_addr.port,
                                                  global_socket.remote_addr.port);
    result = IP_send(ack_response, global_socket.remote_addr.ip_addr);
    if (result == -1) {
        printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send final ACK" ANSI_COLOR_RESET "\n");
        return -1;
    }
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Final ACK sent successfully" ANSI_COLOR_RESET "\n");
    
    global_socket.state = CLOSED;
    printf(LOG_PREFIX ANSI_COLOR_GREEN "Socket closed successfully" ANSI_COLOR_RESET "\n");
    return 0;
}

/**
 * @brief Processes incoming PDUs based on socket state
 * @param pdu Received PDU
 * @param local_addr Local address
 * @param remote_addr Remote address
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr) {
    printf(LOG_PREFIX ANSI_COLOR_MAGENTA "Processing received PDU..." ANSI_COLOR_RESET "\n");
    
    switch (global_socket.state) {
        case ACCEPTING:
            if (verify_pdu(&pdu, 1, 0, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "SYN received" ANSI_COLOR_RESET "\n");
                global_socket.state = SYN_RECEIVED;
                global_socket.remote_addr.ip_addr = remote_addr;
                global_socket.remote_addr.port = pdu.header.source_port;
                pthread_cond_signal(&connection_cond);
            }
            break;
            
        case SYN_RECEIVED:
            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK received, connection established" 
                       ANSI_COLOR_RESET "\n");
                global_socket.state = ESTABLISHED;
                global_socket.current_seq_num = 1;
                pthread_cond_signal(&connection_cond);
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
                    IP_send(acknowledgment, global_socket.remote_addr.ip_addr);
                    break;
                }
                
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received data packet (Seq: %d, Expected: %d)" 
                       ANSI_COLOR_RESET "\n", pdu.header.seq_num, global_socket.current_seq_num);
                
                if (verify_pdu(&pdu, 0, 0, 0, global_socket.current_seq_num, 0)) {
                    global_socket.current_seq_num++;
                    printf(LOG_PREFIX ANSI_COLOR_GREEN "Data packet Accepted, using %d Bytes" 
                           ANSI_COLOR_RESET "\n", pdu.payload.size);
                    app_buffer_put(pdu.payload);
                }
                
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Sending ACK (Ack: %d)..." ANSI_COLOR_RESET "\n",
                       global_socket.current_seq_num);
                mic_tcp_pdu acknowledgment = create_nopayload_pdu(0, 1, 0, 0,
                                                               global_socket.current_seq_num,
                                                               pdu.header.dest_port,
                                                               pdu.header.source_port);
                int result = IP_send(acknowledgment, global_socket.remote_addr.ip_addr);
                if (result == -1) {
                    printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send ACK for Seq %d" 
                           ANSI_COLOR_RESET "\n", global_socket.current_seq_num);
                } else {
                    printf(LOG_PREFIX ANSI_COLOR_GREEN "ACK sent successfully" ANSI_COLOR_RESET "\n");
                }
            } else if (verify_pdu(&pdu, 0, 0, 1, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_YELLOW "Received FIN, initiating closure..." 
                       ANSI_COLOR_RESET "\n");
                global_socket.state = CLOSING;
                
                mic_tcp_pdu fin_ack = create_nopayload_pdu(0, 1, 1, 0, 0,
                                                         pdu.header.dest_port,
                                                         pdu.header.source_port);
                int result = IP_send(fin_ack, remote_addr);
                if (result == -1) {
                    printf(LOG_PREFIX ANSI_COLOR_RED "Failed to send FIN+ACK" ANSI_COLOR_RESET "\n");
                }
            }
            break;
            
        case CLOSING:
            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) {
                printf(LOG_PREFIX ANSI_COLOR_GREEN "Final ACK received, connection closed" 
                       ANSI_COLOR_RESET "\n");
                global_socket.state = CLOSED;
            }
            break;
            
        case IDLE:
        case CLOSED:
        case SYN_SENT:
        case MEASURING_RELIABILITY:
            printf(LOG_PREFIX ANSI_COLOR_YELLOW "PDU ignored in state %d" ANSI_COLOR_RESET "\n",
                   global_socket.state);
            break;
    }
}