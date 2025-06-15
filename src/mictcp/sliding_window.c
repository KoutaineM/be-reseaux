#include "mictcp/sliding_window.h"
#include <stdio.h>

/*
            ╔══════════════════════════╦═══════════════════════════════╦═════════════════════════════╗
            ║ Measured Loss Rate       ║ Accepted Losses in Window     ║ Comment                     ║
            ╚══════════════════════════╩═══════════════════════════════╩═════════════════════════════╝
            ║ x < 2%                   ║ 0 out of 10                   ║ Maximum reliability         ║
            ║ 2% <= x < 5%             ║ 1 out of 10                   ║ Low losses                  ║
            ║ 5% <= x < 12%            ║ 2 out of 10                   ║ Moderate losses             ║
            ║ 12% <= x <= 20%          ║ 3 out of 10                   ║ Low quality channel         ║
            ║ x > 20%                  ║ Rejected                      ║ Channel too degraded        ║
            ╚══════════════════════════╩═══════════════════════════════╩═════════════════════════════╝
*/

int sliding_window = 0b00000;
int sliding_window_consecutive_loss = 0;
int sliding_window_size = 0;

/**
 * @brief Updates the sliding window with packet reception status and displays it
 * @param received 1 if packet was received, 0 otherwise
 */
void update_sliding_window(char received) {
    // Shift window left and add new bit
    sliding_window <<= 1;
    if (received) {
        sliding_window |= 1;
    }
    sliding_window &= (1 << SLIDING_WINDOW_SIZE) - 1;

    // Display sliding window status with colored indicators
    printf(LOG_PREFIX ANSI_COLOR_CYAN "Sliding window status: " ANSI_COLOR_RESET);
    for (int i = 0; i < SLIDING_WINDOW_SIZE; i++) {
        printf("%s%s" ANSI_COLOR_RESET, 
               (sliding_window >> i) & 1 ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
               (sliding_window >> i) & 1 ? "●" : "○");
    }
    printf("\n");
}

/**
 * @brief Checks if the number of lost packets is within acceptable limits
 * @return 1 if acceptable, 0 if too many losses
 */
char verify_acceptable_loss(void) {
    unsigned int count = 0;
    int window = sliding_window;
    
    // Count successful transmissions
    while (window) {
        count += window & 1;
        window >>= 1;
    }
    
    // Verify if loss rate is acceptable
    char result = count > (SLIDING_WINDOW_SIZE - SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS);
    printf(LOG_PREFIX "%sLoss verification: %s (successes: %u/%d)" ANSI_COLOR_RESET "\n",
           result ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
           result ? "ACCEPTABLE" : "UNACCEPTABLE",
           count, SLIDING_WINDOW_SIZE - SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS);
    
    return result;
}