#ifndef MICTCP_PDU_H
#define MICTCP_PDU_H

#include "mictcp.h"

/**
 * @brief Creates a MIC-TCP PDU with no payload
 * @param syn SYN flag (1 for set, 0 for unset)
 * @param ack ACK flag (1 for set, 0 for unset)
 * @param fin FIN flag (1 for set, 0 for unset)
 * @param seq_num Sequence number
 * @param ack_num Acknowledgment number
 * @param source_port Source port number
 * @param remote_port Destination port number
 * @return Constructed MIC-TCP PDU
 */
mic_tcp_pdu create_nopayload_pdu(char syn, char ack, char fin, int seq_num, int ack_num, 
                                int source_port, int remote_port);

/**
 * @brief Validates a received PDU against expected header values
 * @param pdu Pointer to the PDU to verify
 * @param syn Expected SYN flag
 * @param ack Expected ACK flag
 * @param fin Expected FIN flag
 * @param seq_num Expected sequence number
 * @param ack_num Expected acknowledgment number
 * @return 1 if PDU is valid, 0 otherwise
 */
char verify_pdu(mic_tcp_pdu *pdu, char syn, char ack, char fin, int seq_num, int ack_num);

#endif