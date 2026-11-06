#ifndef HERB_NODE_HOST_USART_H
#define HERB_NODE_HOST_USART_H
#include "main.h"
extern UART_HandleTypeDef huart1,huart2,huart3;
void MX_USART2_UART_Init(void);
void MX_USART3_UART_Init(void);
#endif
