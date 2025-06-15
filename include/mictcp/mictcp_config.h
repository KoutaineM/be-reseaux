#ifndef MICTCP_CONFIG_H
#define MICTCP_CONFIG_H

#define MAX_ATTEMPTS 10              // Maximum connection attempts
#define TIMEOUT 30                   // Timeout in milliseconds
#define LOSS_RATE 2                  // Packet loss rate percentage
#define MAX_SOCKETS 20               // Maximum number of sockets
#define MESURING_RELIABILITY_PACKET_NUMBER 100 // Number of packets for reliability measurement
#define MESURING_PAYLOAD "mesure"    // Payload for reliability measurement

// ANSI color codes for logging
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_BG_MAGENTA    "\x1b[45m"
#define ANSI_BG_RED        "\x1b[41m"
#define LOG_PREFIX         ANSI_BG_MAGENTA ANSI_COLOR_BLUE " MIC-TCP " ANSI_COLOR_RESET " "

#endif