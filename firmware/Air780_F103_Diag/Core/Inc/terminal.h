#ifndef DIAG_TERMINAL_H
#define DIAG_TERMINAL_H
#include "main.h"
#include <stdbool.h>
void Terminal_Init(void);
void Terminal_Poll(void);
bool Terminal_OnRx(UART_HandleTypeDef *uart);
bool Terminal_OnError(UART_HandleTypeDef *uart);
void Terminal_OnModemByte(uint8_t byte);
#endif
