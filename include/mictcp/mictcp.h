#ifndef MICTCP_H
#define MICTCP_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>

/*
 * Etats du protocole (les noms des états sont donnés à titre indicatif
 * et peuvent être modifiés)
 */
typedef enum protocol_state
{
    IDLE,
    CLOSED,
    SYN_SENT,
    MEASURING_RELIABILITY,
    ACCEPTING,
    SYN_RECEIVED,
    ESTABLISHED,
    CLOSING
} protocol_state;

/*
 * Mode de démarrage du protocole
 * NB : nécessaire à l’usage de la fonction initialize_components()
 */
typedef enum start_mode
{
    CLIENT,
    SERVER
} start_mode;

/*
 * Structure d’une adresse IP
 */
typedef struct mic_tcp_ip_addr
{
    char *addr;
    int addr_size;
} mic_tcp_ip_addr;

/*
 * Structure d’une adresse de socket
 */
typedef struct mic_tcp_sock_addr
{
    mic_tcp_ip_addr ip_addr;
    unsigned short port;
} mic_tcp_sock_addr;

/*
 * Structure d'un socket
 */
typedef struct mic_tcp_sock
{
    int fd;         /* descripteur du socket */
    int sys_socket; /* descripteur interne du socket */

    protocol_state state;          /* état du protocole */
    mic_tcp_sock_addr local_addr;  /* adresse locale du socket */
    mic_tcp_sock_addr remote_addr; /* adresse distante du socket */
    unsigned int current_seq_num;  /* PSE/PSA numéro de séquence */

    // Connection asynchronisme
    pthread_mutex_t connection_lock;
    pthread_cond_t connection_cond;

    // Sliding window
    int sliding_window;
    int sliding_window_consecutive_loss;
    int sliding_window_size;


} mic_tcp_sock;

/*
 * Structure des données utiles d’un PDU MIC-TCP
 */
typedef struct mic_tcp_payload
{
    char *data; /* données applicatives */
    int size;   /* taille des données */
} mic_tcp_payload;

/*
 * Structure de l'entête d'un PDU MIC-TCP
 */
typedef struct mic_tcp_header
{
    unsigned short source_port; /* numéro de port source */
    unsigned short dest_port;   /* numéro de port de destination */
    unsigned int seq_num;       /* numéro de séquence */
    unsigned int ack_num;       /* numéro d'acquittement */
    unsigned char syn;          /* flag SYN (valeur 1 si activé et 0 si non) */
    unsigned char ack;          /* flag ACK (valeur 1 si activé et 0 si non) */
    unsigned char fin;          /* flag FIN (valeur 1 si activé et 0 si non) */
} mic_tcp_header;

/*
 * Structure d'un PDU MIC-TCP
 */
typedef struct mic_tcp_pdu
{
    mic_tcp_header header;   /* entête du PDU */
    mic_tcp_payload payload; /* charge utile du PDU */
} mic_tcp_pdu;

typedef struct app_buffer
{
    mic_tcp_payload packet;
    struct app_buffer *next;
    unsigned short id;
} app_buffer;

/****************************
 * Fonctions de l'interface *
 ****************************/
/**
 * @brief Creates a new MIC-TCP socket
 * @param sm Start mode (client or server)
 * @return Socket descriptor on success, -1 on failure
 */
int mic_tcp_socket(start_mode sm);

/**
 * @brief Binds a socket to a specific address
 * @param socket Socket descriptor
 * @param addr Address to bind to
 * @return 0 on success, -1 on failure
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr);

/**
 * @brief Places socket in accepting state for incoming connections
 * @param socket Socket descriptor
 * @param addr Pointer to store remote address
 * @return 0 on success, -1 on failure
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr *addr);

/**
 * @brief Initiates a connection to a remote address
 * @param socket Socket descriptor
 * @param addr Remote address to connect to
 * @return 0 on success, -1 on failure
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr);

/**
 * @brief Sends an acknowledgment for connection establishment
 * @param socket Socket to send ACK on
 * @return 0 on success, -1 on failure
 */
int send_connection_acknowledgement(mic_tcp_sock* socket);
/**
 * @brief Sends application data over the socket
 * @param mic_sock Socket descriptor
 * @param msg Data to send
 * @param msg_size Size of data
 * @return Number of bytes sent, -1 on error
 */
int mic_tcp_send(int mic_sock, char *msg, int msg_size);

/**
 * @brief Receives application data from the socket
 * @param socket Socket descriptor
 * @param msg Buffer to store received data
 * @param max_msg_size Maximum size to receive
 * @return Number of bytes received, -1 on error
 */
int mic_tcp_recv(int socket, char *msg, int max_msg_size);

/**
 * @brief Closes the socket and terminates the connection
 * @param socket Socket descriptor
 * @return 0 on success, -1 on failure
 */
int mic_tcp_close(int socket);

/**
 * @brief Processes a received MIC-TCP PDU
 * @param sys_socket System-interal socket descriptor
 * @param pdu Received PDU
 * @param local_addr Local address
 * @param remote_addr Remote address
 */
void process_received_PDU(int sys_socket, mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr);

#endif
