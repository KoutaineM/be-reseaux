#include "mictcp/mictcp_pdu.h"
#include <stdio.h>
#include "mictcp/mictcp_config.h"

/**
 * @brief Constructs a MIC-TCP PDU with empty payload
 * @param syn SYN flag
 * @param ack ACK flag
 * @param fin FIN flag
 * @param seq_num Sequence number
 * @param ack_num Acknowledgment number
 * @param source_port Source port
 * @param remote_port Destination port
 * @return Constructed PDU
 */
mic_tcp_pdu create_nopayload_pdu(char syn, char ack, char fin, int seq_num, int ack_num,
                                int source_port, int remote_port) {
    mic_tcp_pdu pdu;
    mic_tcp_header pdu_header;
    mic_tcp_payload pdu_payload;
    
    // Initialize empty payload
    pdu_payload.data = NULL;
    pdu_payload.size = 0;
    
    // Set header fields
    pdu_header.source_port = source_port;
    pdu_header.dest_port = remote_port;
    pdu_header.syn = syn;
    pdu_header.ack = ack;
    pdu_header.fin = fin;
    pdu_header.seq_num = seq_num;
    pdu_header.ack_num = ack_num;
    
    // Combine header and payload
    pdu.header = pdu_header;
    pdu.payload = pdu_payload;
    
    printf(LOG_PREFIX_MAIN_THREAD ANSI_COLOR_GREEN "PDU created: SYN=%d ACK=%d FIN=%d SEQ=%d ACK_NUM=%d" 
           ANSI_COLOR_RESET "\n", syn, ack, fin, seq_num, ack_num);
    
    return pdu;
}

/**
 * @brief Verifies PDU header fields against expected values
 * @param pdu Pointer to PDU to verify
 * @param syn Expected SYN flag
 * @param ack Expected ACK flag
 * @param fin Expected FIN flag
 * @param seq_num Expected sequence number (if non-zero)
 * @param ack_num Expected acknowledgment number (if non-zero)
 * @return 1 if valid, 0 otherwise
 */
char verify_pdu(mic_tcp_pdu *pdu, char syn, char ack, char fin, int seq_num, int ack_num) {
    mic_tcp_header *header = &(pdu->header);
    
    // Check SYN and sequence number
    if (header->syn != syn) {
        return 0;
    }
    
    // Check ACK and acknowledgment number
    if (header->ack != ack || (ack == 1 && header->ack_num != ack_num && ack_num != 0)) {
        return 0;
    }
    
    // Check FIN flag
    if (header->fin != fin) {
        return 0;
    }

    // Check SEQ NUM
    if (seq_num != 0 && seq_num != pdu->header.seq_num) {
        return 0;
    }
    
    return 1;
}