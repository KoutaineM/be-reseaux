#ifndef MICTCP_SLIDING_WINDOW_H
#define MICTCP_SLIDING_WINDOW_H

#include "mictcp_config.h"

// Sliding window variables
extern int sliding_window;
extern int sliding_window_consecutive_loss;
extern int sliding_window_size;

// Macros for sliding window configuration
#define SLIDING_WINDOW_SIZE sliding_window_size
#define SLIDING_WINDOW_CONSECUTIVE_ACCEPTABLE_LOSS sliding_window_consecutive_loss

/**
 * @brief Updates the sliding window based on packet reception status
 * @param received 1 if packet was received successfully, 0 otherwise
 */
void update_sliding_window(char received);

/**
 * @brief Verifies if the current packet loss rate is within acceptable limits
 * @return 1 if loss is acceptable, 0 otherwise
 */
char verify_acceptable_loss(void);

#endif