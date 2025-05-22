#include <mictcp.h>
#include <api/mictcp_core.h>

mic_tcp_sock global_socket;

mic_tcp_pdu create_nopayload_pdu(char syn, char ack, char fin, int seq_num, int ack_num, int source_port, int remote_port) {

    mic_tcp_pdu pdu;
    mic_tcp_header pdu_header;
    mic_tcp_payload pdu_payload;
    pdu_payload.data = NULL;
    pdu_payload.size = 0;
    pdu_header.source_port = source_port;
    pdu_header.dest_port = remote_port;
    pdu_header.syn = syn;
    pdu_header.ack = ack;
    pdu_header.fin = fin;
    pdu_header.seq_num = seq_num;
    pdu_header.ack_num = ack_num;
    pdu.header = pdu_header;
    pdu.payload = pdu_payload;

    return pdu;

}

char verify_pdu(mic_tcp_pdu* pdu, char syn, char ack, char fin, int seq_num, int ack_num) {

    mic_tcp_header* header = &(pdu->header);
    if (header->syn != syn || (syn != 0 && header->seq_num != seq_num)) return 0;
    if (header->ack != ack || (ack != 0 && header->ack_num != ack_num)) return 0;
    if (header->fin != fin) return 0;
    return 1;

}


/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
   int result = -1;
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   result = initialize_components(sm); /* Appel obligatoire */
   set_loss_rate(0);

   if (result == -1) return -1;
   global_socket.fd = 1;

   return global_socket.fd;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   global_socket.local_addr = addr;
   return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    mic_tcp_pdu* received_pdu = NULL;
    mic_tcp_ip_addr* remote_ip_addr = NULL;

    printf("[MIC-TCP] Reception du SYN...\n");
    int result = IP_recv(received_pdu, &global_socket.local_addr.ip_addr, remote_ip_addr, 100000);
    if (result == -1) return -1;
    if (!verify_pdu(received_pdu, 1, 0, 0, 0, 0)) return -1;
    int remote_port = received_pdu->header.source_port;
    printf("[MIC-TCP] Reception du SYN... OK\n");

    printf("[MIC-TCP] Envoi de SYN+ACK... \n");
    mic_tcp_pdu response = create_nopayload_pdu(1, 1, 0, 0, 0, global_socket.local_addr.port, remote_port);
    result = IP_send(response, *remote_ip_addr);
    if (result == -1) return -1;
    printf("[MIC-TCP] Envoi de SYN+ACK... OK\n");

    printf("[MIC-TCP] Reception du ACK...\n");
    result = IP_recv(received_pdu, &global_socket.local_addr.ip_addr, remote_ip_addr, 1000);
    if (result == -1) return -1;
    if (!verify_pdu(received_pdu, 0, 1, 0, 0, 0)) return -1;
    printf("[MIC-TCP] Reception du ACK... OK\n");

    global_socket.remote_addr.ip_addr = *remote_ip_addr;
    global_socket.remote_addr.port = remote_port;
    addr = &global_socket.remote_addr;

    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    printf("[MIC-TCP] Envoi du SYN...\n");
    mic_tcp_pdu connect_req = create_nopayload_pdu(1, 0, 0, 0, 0, global_socket.local_addr.port, addr.port);
    int result = IP_send(connect_req, addr.ip_addr);
    if (result == -1) return -1;
    printf("[MIC-TCP] Envoi du SYN... OK\n");

    printf("[MIC-TCP] Reception du SYN+ACK...\n");
    mic_tcp_pdu* received_pdu = NULL;
    result = IP_recv(received_pdu, &global_socket.local_addr.ip_addr, NULL, 1000);
    if (result == -1) return -1;
    if (!verify_pdu(received_pdu, 1, 1, 0, 0, 0)) return -1;
    printf("[MIC-TCP] Reception du SYN+ACK... OK\n");

    printf("[MIC-TCP] Envoi du ACK...\n");
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0, global_socket.local_addr.port, addr.port);
    result = IP_send(ack_response, addr.ip_addr);
    if (result == -1) return -1;
    printf("[MIC-TCP] Envoi du ACK... OK\n");

    global_socket.remote_addr = addr;

    return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send(int mic_sock, char* msg, int msg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    mic_tcp_pdu packet = create_nopayload_pdu(0, 0, 0, 0, 0, global_socket.local_addr.port, global_socket.remote_addr.port);
    mic_tcp_payload payload;
    payload.data = msg;
    payload.size = msg_size;
    packet.payload = payload;
    int result = IP_send(packet, global_socket.remote_addr.ip_addr);
    if (result == -1) return -1;

    return msg_size;
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* msg, int max_msg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    // voir pour cas d'erreur

    mic_tcp_payload payload_to_receive;
    payload_to_receive.data = msg;
    payload_to_receive.size = max_msg_size;
    return app_buffer_get(payload_to_receive);

}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");

    mic_tcp_pdu connect_req = create_nopayload_pdu(0, 0, 1, 0, 0, global_socket.local_addr.port, global_socket.remote_addr.port);
    int result = IP_send(connect_req, global_socket.remote_addr.ip_addr);
    if (result == -1) return -1;

    mic_tcp_pdu* received_pdu = NULL;
    result = IP_recv(received_pdu, &global_socket.local_addr.ip_addr, NULL, 1000);
    if (result == -1) return -1;
    if (!verify_pdu(received_pdu, 0, 1, 1, 0, 0)) return -1;

    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0, global_socket.local_addr.port, global_socket.remote_addr.port);
    result = IP_send(ack_response, global_socket.remote_addr.ip_addr);
    if (result == -1) return -1;

    return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    if (verify_pdu(&pdu, 0, 0, 0, 0, 0)) {
        app_buffer_put(pdu.payload);
    }

    if (verify_pdu(&pdu, 0, 0, 1, 0, 0)) {
        mic_tcp_pdu fin_ack = create_nopayload_pdu(0, 1, 1, 0, 0, pdu.header.source_port, pdu.header.dest_port);
        int result = IP_send(fin_ack, remote_addr);
        if (result == -1) return;

        mic_tcp_pdu* received_pdu = NULL;
        result = IP_recv(received_pdu, &local_addr, NULL, 1000);
        if (result == -1) return;
        if (!verify_pdu(received_pdu, 0, 1, 0, 0, 0)) return;

        // connection ended
        // detruire socket

    }

}
