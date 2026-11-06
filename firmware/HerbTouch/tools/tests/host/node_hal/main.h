#ifndef HERB_NODE_HOST_HAL_H
#define HERB_NODE_HOST_HAL_H
#include <stdint.h>
typedef enum {GPIO_PIN_RESET=0,GPIO_PIN_SET=1} GPIO_PinState;
typedef struct {volatile uint32_t DR;} USART_TypeDef;
typedef struct {USART_TypeDef *Instance;struct {uint32_t BaudRate;} Init;unsigned gState;} UART_HandleTypeDef;
typedef struct {volatile uint32_t CR,SR,DR;} TestRNG;
extern USART_TypeDef test_usart1,test_usart2,test_usart3;
extern TestRNG test_rng;
#define USART2 (&test_usart2)
#define USART3 (&test_usart3)
#define RNG (&test_rng)
#define RNG_CR_RNGEN 1u
#define RNG_SR_DRDY 1u
#define RNG_SR_CECS 2u
#define RNG_SR_SECS 4u
#define USART2_IRQn 2
#define USART3_IRQn 3
#define UART_FLAG_RXNE 1u
#define UART_FLAG_TXE 2u
#define UART_FLAG_ORE 4u
#define HAL_UART_STATE_READY 0
#define HAL_OK 0
#define RESET 0
#define SENSOR_PWR_EN_GPIO_Port ((void*)6)
#define MODEM_PWR_EN_GPIO_Port ((void*)7)
#define SENSOR_PWR_EN_Pin 1
#define MODEM_PWR_EN_Pin 2
#define __DMB() ((void)0)
#define __HAL_RCC_RNG_CLK_ENABLE() ((void)0)
#define __HAL_UART_CLEAR_OREFLAG(u) ((void)(u))
#define __HAL_UART_GET_FLAG(u,f) TestUART_Flag(u,f)
unsigned TestUART_Flag(UART_HandleTypeDef *uart,unsigned flag);
uint32_t HAL_GetTick(void);
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
uint32_t HAL_GetUIDw2(void);
void HAL_GPIO_WritePin(void *port,uint16_t pin,GPIO_PinState state);
int HAL_UART_Init(UART_HandleTypeDef *uart);
int HAL_UART_Receive_IT(UART_HandleTypeDef *uart,uint8_t *data,uint16_t size);
int HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,uint8_t *data,uint16_t size);
int HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
void HAL_NVIC_DisableIRQ(unsigned irq);
void HAL_NVIC_EnableIRQ(unsigned irq);
#endif
