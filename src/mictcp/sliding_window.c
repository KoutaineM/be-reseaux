#include "mictcp/mictcp.h"
#include "mictcp/mictcp_config.h"
#include <stdio.h>

/**
 * @brief Updates the sliding window with packet reception status and displays it
 * @param sock MIC-TCP socket containing sliding window fields
 * @param received 1 if packet was received, 0 otherwise
 */
void update_sliding_window(mic_tcp_sock *sock, char received) {
    // Shift window left and add new bit
    sock->sliding_window <<= 1;
    if (received) {
        sock->sliding_window |= 1;
    }
    sock->sliding_window &= (1 << sock->sliding_window_size) - 1;

    // Display sliding window status with colored indicators
    printf(LOG_PREFIX ANSI_COLOR_CYAN "Sliding window status: " ANSI_COLOR_RESET);
    for (int i = 0; i < sock->sliding_window_size; i++) {
        printf("%s%s" ANSI_COLOR_RESET, 
               (sock->sliding_window >> i) & 1 ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
               (sock->sliding_window >> i) & 1 ? "●" : "○");
    }
    printf("\n");
}

/**
 * @brief Checks if the number of lost packets is within acceptable limits
 * @param sock MIC-TCP socket containing sliding window fields
 * @return 1 if acceptable, 0 if too many losses
 */
char verify_acceptable_loss(mic_tcp_sock *sock) {
    unsigned int count = 0;
    int window = sock->sliding_window;
    
    // Count successful transmissions
    while (window) {
        count += window & 1;
        window >>= 1;
    }
    
    // Verify if loss rate is acceptable
    char result = count > (sock->sliding_window_size - sock->sliding_window_consecutive_loss);
    printf(LOG_PREFIX "%sLoss verification: %s (successes: %u, minimum: %d)" ANSI_COLOR_RESET "\n",
           result ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
           result ? "ACCEPTABLE" : "UNACCEPTABLE",
           count, sock->sliding_window_size - sock->sliding_window_consecutive_loss);
    
    return result;
}