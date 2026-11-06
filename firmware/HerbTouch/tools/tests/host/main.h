#ifndef HERB_HOST_MAIN_H
#define HERB_HOST_MAIN_H
#include <stdint.h>
typedef enum {GPIO_PIN_RESET=0,GPIO_PIN_SET=1} GPIO_PinState;
#define TP_CS_GPIO_Port ((void*)1)
#define TP_CLK_GPIO_Port ((void*)2)
#define TP_MOSI_GPIO_Port ((void*)3)
#define TP_MISO_GPIO_Port ((void*)4)
#define TP_PEN_GPIO_Port ((void*)5)
#define TP_CS_Pin 1
#define TP_CLK_Pin 2
#define TP_MOSI_Pin 3
#define TP_MISO_Pin 4
#define TP_PEN_Pin 5
#define __NOP() ((void)0)
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(void *port,uint16_t pin,GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(void *port,uint16_t pin);
#endif
