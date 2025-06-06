#include <mictcp.h>
#include <api/mictcp_core.h>
#include <time.h> 
#include <errno.h>

#define MAX_ATTEMPTS 10
#define TIMEOUT 1000
#define LOSS_RATE 20
#define MAX_SOCKETS 20

mic_tcp_sock global_socket;
pthread_mutex_t connection_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t connection_cond = PTHREAD_COND_INITIALIZER;

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

/*
* Verifying the PDU and label number
*/
char verify_pdu(mic_tcp_pdu* pdu, char syn, char ack, char fin, int seq_num, int ack_num) {

    mic_tcp_header* header = &(pdu->header);
    if (header->syn != syn || (syn == 1 && header->seq_num != seq_num && seq_num != 0)) return 0;
    if (header->ack != ack || (ack == 1 && header->ack_num != ack_num && ack_num != 0)) return 0;
    if (header->fin != fin) return 0;
    return 1;

}


/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
   int result;
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   result = initialize_components(sm); /* Appel obligatoire */
   set_loss_rate(LOSS_RATE);

   if (result == -1) return -1;
   global_socket.fd = 1;

   printf("[MIC-TCP] Socket Created \n") ;
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
   global_socket.state = IDLE ;

   printf("[MIC-TCP] Socket Binded \n") ;

   return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    mic_tcp_pdu received_pdu;
    received_pdu.payload.size = 0;
    mic_tcp_ip_addr remote_ip_addr;

    int attempt_done = 0 ;
    int result ;
    char synack_to_send = 1;
    char ack_received = 0;

    pthread_mutex_lock(&connection_lock);

    global_socket.state = ACCEPTING;

    // Attente reception SYN
    if (pthread_cond_wait(&connection_cond, &connection_lock) != 0) {
        printf("[MIC-TCP] Error in SYN condition wait.");
        return -1;
    }

    while(global_socket.state == SYN_RECEIVED) {

        printf("[MIC-TCP] Envoi de SYN+ACK... \n");
        mic_tcp_pdu response = create_nopayload_pdu(1, 1, 0, 0, 0, global_socket.local_addr.port, global_socket.remote_addr.port);
        result = IP_send(response, global_socket.remote_addr.ip_addr);
        if (result == -1) continue;
        printf("[MIC-TCP] Envoi de SYN+ACK... OK\n");

        pthread_mutex_lock(&connection_lock);
        time_t CURRENT_TIME;                                                                     
        struct timespec timeout_timestamp;
        time(&CURRENT_TIME);
        timeout_timestamp.tv_sec = CURRENT_TIME + TIMEOUT/1000;
        timeout_timestamp.tv_nsec = 0;
        if (pthread_cond_timedwait(&connection_cond, &connection_lock, &timeout_timestamp) != 0) {
            if (errno == EAGAIN) {
                printf("[MIC-TCP] Timeout in ACK receiving, sending SYN+ACK again.");
                continue;
            } else {
                printf("[MIC-TCP] Error in SYN+ACK condition wait.");
                return -1;
            }
        }

    }

    return 0;

}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    int attempt_done = 0 ;
    int result ;
    char synack_received = 0 ;
    char syn_to_send = 1 ;

    while (syn_to_send || !synack_received) {
        
        printf("[MIC-TCP] Envoi du SYN...\n");
        mic_tcp_pdu connect_req = create_nopayload_pdu(1, 0, 0, 0, 0, global_socket.local_addr.port, addr.port);
        result = IP_send(connect_req, addr.ip_addr);
        if (result == -1) continue;
        printf("[MIC-TCP] Envoi du SYN... OK\n");

        syn_to_send = 0 ;
        synack_received = 0 ; 

        global_socket.state = SYN_SENT ;

        while (!synack_received) 
        {
            printf("[MIC-TCP] Reception du SYN+ACK...\n");

            mic_tcp_pdu received_pdu;
            received_pdu.payload.size = 0;
            mic_tcp_ip_addr remote_addr;
            result = IP_recv(&received_pdu, &global_socket.local_addr.ip_addr, &remote_addr, TIMEOUT);
            if (result == -1) {
                syn_to_send = 1 ;
                break;
            }
            if (!verify_pdu(&received_pdu, 1, 1, 0, 0, 0)) {
                continue;
            }

            printf("[MIC-TCP] Reception du SYN+ACK... OK\n");

            synack_received = 1 ;
        }
    }

    //Need to resend ACK if lost
    printf("[MIC-TCP] Envoi du ACK...\n");
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0, global_socket.local_addr.port, addr.port);
    result = IP_send(ack_response, addr.ip_addr);
    if (result == -1) return -1;
    printf("[MIC-TCP] Envoi du ACK... OK\n");

    global_socket.remote_addr = addr;
    global_socket.state = ESTABLISHED ;
    global_socket.current_seq_num = 1;

    return 0;
}

int has_to_redo_connect() {
    int result ;

    printf("[MIC-TCP] Envoi du ACK...\n");
    mic_tcp_pdu ack_response = create_nopayload_pdu(0, 1, 0, 0, 0, global_socket.local_addr.port, global_socket.remote_addr.port);
    result = IP_send(ack_response, global_socket.remote_addr.ip_addr);
    if (result == -1) return -1;
    printf("[MIC-TCP] Envoi du ACK... OK\n");

    global_socket.state = ESTABLISHED ;
    global_socket.current_seq_num = 1;

    return result ;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send(int mic_sock, char* msg, int msg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    if (global_socket.state != ESTABLISHED) return -1;

    mic_tcp_pdu received_pdu;
    received_pdu.payload.size = 0;
    mic_tcp_ip_addr remote_ip_addr;

    mic_tcp_pdu packet = create_nopayload_pdu(0, 0, 0, global_socket.current_seq_num, 0, global_socket.local_addr.port, global_socket.remote_addr.port);
    mic_tcp_payload payload;
    payload.data = msg;
    payload.size = msg_size;
    packet.payload = payload;

    int attempt_done = 0 ;
    int result ;
    char msg_to_send = 1 ;

    while (msg_to_send) {
        
        printf("[MIC-TCP] Sending packet with Sequence Number %d\n",global_socket.current_seq_num) ;
        result = IP_send(packet, global_socket.remote_addr.ip_addr);
        if (result == -1) return -1;

        printf("[MIC-TCP] Reception du ACK...\n");
        result = IP_recv(&received_pdu, &global_socket.local_addr.ip_addr, &remote_ip_addr, TIMEOUT);
        if (result == -1) continue;


        if (verify_pdu(&received_pdu,1 ,1, 0, 0, 0)) {
            printf("[MIC-TCP] Received SYN-ACK... of connexion ! Resend ACK \n") ;
            result = has_to_redo_connect() ;
            continue ;
        }
    
        else if (!verify_pdu(&received_pdu, 0, 1, 0, global_socket.current_seq_num+1, 0)) {
            printf("[MIC-TCP] Receiving ACK... OK [BAD ACK NUM %d != %d, UPDATING]\n", global_socket.current_seq_num+1, received_pdu.header.ack_num);
            global_socket.current_seq_num = received_pdu.header.ack_num;
            continue;
        } else {
            printf("[MIC-TCP] Receiving ACK... OK\n");
            global_socket.current_seq_num++;
            msg_to_send = 0 ;
        }

        break;
    }

    if (attempt_done >= MAX_ATTEMPTS) return -1;

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

    global_socket.state = CLOSING ;

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

    global_socket.state = CLOSED ;

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

    switch(global_socket.state) {

        case ACCEPTING:

            if (verify_pdu(&pdu, 1, 0, 0, 0, 0)) {
                printf("[MIC-TCP] Received SYN\n");
                global_socket.state = SYN_RECEIVED;
                global_socket.remote_addr.ip_addr = remote_addr;
                global_socket.remote_addr.port = pdu.header.source_port;
                pthread_cond_signal(&connection_cond);
            }

            break;

        case SYN_RECEIVED: 

            if (verify_pdu(&pdu, 0, 1, 0, 0, 0)) {
                printf("[MIC-TCP] Received ACK... Connection established\n");
                global_socket.state = ESTABLISHED;
                global_socket.current_seq_num = 1;
                pthread_cond_signal(&connection_cond);
            }

            break;

        case ESTABLISHED:

            if (verify_pdu(&pdu, 0, 0, 0, 0, 0)) { // DATA

                // If data PDU received is the one we're waiting for
                printf("[MIC-TCP] Reception DATA PDU : Expected Seq_num %d \n", global_socket.current_seq_num);
                if (verify_pdu(&pdu, 0, 0, 0, global_socket.current_seq_num, 0)) {
                    global_socket.current_seq_num++;
                    app_buffer_put(pdu.payload);

                    printf("[MIC-TCP] Reception DATA PDU OK : New Seq_num %d \n", global_socket.current_seq_num);

                }

                printf("[MIC-TCP] Envoi du ACK... Current Ack_num %d \n",global_socket.current_seq_num);
                mic_tcp_pdu acknowledgment = create_nopayload_pdu(0, 1, 0, 0, global_socket.current_seq_num, pdu.header.dest_port, pdu.header.source_port);
                int result = IP_send(acknowledgment, global_socket.remote_addr.ip_addr);
                if (result == -1) printf("[MIC-TCP] Could not send acknowledgement for packet %d\n", global_socket.current_seq_num);
                printf("[MIC-TCP] Envoi du ACK... OK\n");

            } else if (verify_pdu(&pdu, 0, 0, 1, 0, 0)) {

                global_socket.state = CLOSING;

                mic_tcp_pdu fin_ack = create_nopayload_pdu(0, 1, 1, 0, 0, pdu.header.dest_port, pdu.header.source_port);
                int result = IP_send(fin_ack, remote_addr);
                if (result == -1) return;

            }

            break;

        case CLOSING:

            if (!verify_pdu(&pdu, 0, 1, 0, 0, 0)) return;
            global_socket.state = CLOSED;

            break;

    }

}
