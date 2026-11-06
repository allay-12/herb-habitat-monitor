#ifndef DIAG_MAIN_H
#define DIAG_MAIN_H
#include "stm32f1xx_hal.h"
#define DIAG_MAGIC 0x41495231u
#define DIAG_VERSION 1u
#define DIAG_RECORDS 9u
#define DIAG_RX_CAPACITY 768u
typedef struct {
    char command[32];
    uint32_t length, ok, elapsed_ms, errors_before, errors_after;
    uint8_t response[DIAG_RX_CAPACITY];
} DiagRecord;
typedef struct {
    uint32_t magic, version, request, state, run_number, completed;
    uint32_t at_ok, sim_ready, registration, attached, csq;
    uint32_t uart_errors, framing_errors, noise_errors, overrun_errors;
    uint32_t capture_overflow, tx_errors, tick, cpu_hz, rx_total;
    uint32_t last_hal_error, capture_length, fatal;
    DiagRecord records[DIAG_RECORDS];
} DiagReport;
extern volatile DiagReport g_diag;
extern UART_HandleTypeDef huart1, huart2;
void Error_Handler(void);
#endif
