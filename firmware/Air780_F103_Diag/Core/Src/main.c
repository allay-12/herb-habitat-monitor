/* Diagnostic adaptation of the supplied STM32 Air780E SMS example.
 * Uses the supplied ST HAL. Original sample remains under /资料.
 * PA2/PA3: USART2 to carrier TXD/RXD. PA1: analog, never driven.
 * No SMS, factory reset, operator/APN selection, or radio reset commands.
 */
#include "main.h"
#include "terminal.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

UART_HandleTypeDef huart1, huart2;
volatile DiagReport g_diag;
static uint8_t rx_byte;
static volatile uint8_t capture[DIAG_RX_CAPACITY];
static volatile uint32_t capture_used;
static volatile bool receive_fault;
static volatile uint32_t irq_errors;
static const char *const commands[DIAG_RECORDS] = {
    "AT", "AT+CGMR", "AT+CPIN?", "AT+CFUN?", "AT+COPS?",
    "AT+CSQ", "AT+CEREG=3", "AT+CEREG?", "AT+CGATT?"
};

static void log_text(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 1000);
}
static void log_rx(const uint8_t *s, uint32_t n)
{
    char output[81]; unsigned used = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t c = s[i];
        if (used > 72) {output[used] = 0; log_text(output); used = 0;}
        if (c == '\r') {output[used++] = '\\'; output[used++] = 'r';}
        else if (c == '\n') {output[used++] = '\\'; output[used++] = 'n';}
        else if (c == '\\') {output[used++] = '\\'; output[used++] = '\\';}
        else if (c >= 32 && c <= 126) output[used++] = (char)c;
        else {used += (unsigned)sprintf(output + used, "\\x%02X", c);}
    }
    output[used] = 0; log_text(output); log_text("\r\n");
}
static void clock_init(void)
{
    RCC_OscInitTypeDef o = {0}; RCC_ClkInitTypeDef c = {0};
    o.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    o.HSIState = RCC_HSI_ON; o.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    o.PLL.PLLState = RCC_PLL_ON; o.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    o.PLL.PLLMUL = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&o) != HAL_OK) Error_Handler();
    c.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; c.AHBCLKDivider = RCC_SYSCLK_DIV1;
    c.APB1CLKDivider = RCC_HCLK_DIV2; c.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&c, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}
static void uart_init(UART_HandleTypeDef *u, USART_TypeDef *instance)
{
    u->Instance = instance; u->Init.BaudRate = 115200;
    u->Init.WordLength = UART_WORDLENGTH_8B; u->Init.StopBits = UART_STOPBITS_1;
    u->Init.Parity = UART_PARITY_NONE; u->Init.Mode = UART_MODE_TX_RX;
    u->Init.HwFlowCtl = UART_HWCONTROL_NONE; u->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(u) != HAL_OK) Error_Handler();
}
void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE(); __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG(); /* SWD PA13/PA14 remains enabled. */
}
void HAL_UART_MspInit(UART_HandleTypeDef *u)
{
    GPIO_InitTypeDef p = {0}; __HAL_RCC_GPIOA_CLK_ENABLE();
    if (u->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE(); p.Pin = GPIO_PIN_9;
        p.Mode = GPIO_MODE_AF_PP; p.Speed = GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOA, &p);
        p.Pin = GPIO_PIN_10; p.Mode = GPIO_MODE_INPUT; p.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOA, &p);
        HAL_NVIC_SetPriority(USART1_IRQn, 2, 0); HAL_NVIC_EnableIRQ(USART1_IRQn);
    } else if (u->Instance == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE(); p.Pin = GPIO_PIN_2;
        p.Mode = GPIO_MODE_AF_PP; p.Speed = GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOA, &p);
        p.Pin = GPIO_PIN_3; p.Mode = GPIO_MODE_INPUT; p.Pull = GPIO_NOPULL; HAL_GPIO_Init(GPIOA, &p);
        HAL_NVIC_SetPriority(USART2_IRQn, 1, 0); HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *u)
{
    if (Terminal_OnRx(u)) return;
    if (u != &huart2) return;
    g_diag.rx_total++;
    Terminal_OnModemByte(rx_byte);
    if (g_diag.state == 2) {
        if (capture_used < DIAG_RX_CAPACITY) capture[capture_used++] = rx_byte;
        else g_diag.capture_overflow++;
        g_diag.capture_length = capture_used;
    }
    if (HAL_UART_Receive_IT(u, &rx_byte, 1) != HAL_OK) receive_fault = true;
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *u)
{
    if (Terminal_OnError(u)) return;
    if (u != &huart2) return;
    /* Re-arming one-byte RX in RxCpltCallback can clear HAL ErrorCode before
       ErrorCallback runs. Preserve the flags latched on IRQ entry as well. */
    uint32_t error = HAL_UART_GetError(u) | irq_errors;
    g_diag.uart_errors++; g_diag.last_hal_error = error;
    if (error & HAL_UART_ERROR_FE) g_diag.framing_errors++;
    if (error & HAL_UART_ERROR_NE) g_diag.noise_errors++;
    if (error & HAL_UART_ERROR_ORE) g_diag.overrun_errors++;
    receive_fault = true;
}
static void rx_recover(void)
{
    if (!receive_fault) return;
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    HAL_UART_AbortReceive(&huart2); __HAL_UART_CLEAR_OREFLAG(&huart2);
    receive_fault = false;
    if (HAL_UART_Receive_IT(&huart2, &rx_byte, 1) != HAL_OK) receive_fault = true;
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}
static bool line_equals(const uint8_t *s, uint32_t n, const char *word)
{
    unsigned size = (unsigned)strlen(word);
    for (uint32_t pos = 0; pos < n;) {
        while (pos < n && (s[pos] == '\r' || s[pos] == '\n')) pos++;
        uint32_t end = pos; while (end < n && s[end] != '\r' && s[end] != '\n') end++;
        if (end - pos == size && !memcmp(s + pos, word, size)) return true;
        pos = end;
    }
    return false;
}
static void query(unsigned index)
{
    char tx[40], log[100]; DiagRecord *r = (DiagRecord *)&g_diag.records[index];
    memset(r, 0, sizeof(*r)); strcpy(r->command, commands[index]);
    snprintf(tx, sizeof(tx), "%s\r\n", commands[index]);
    HAL_NVIC_DisableIRQ(USART2_IRQn); capture_used = 0; g_diag.capture_length = 0;
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    r->errors_before = g_diag.uart_errors;
    uint32_t started = HAL_GetTick();
    if (HAL_UART_Transmit(&huart2, (uint8_t *)tx, (uint16_t)strlen(tx), 100) != HAL_OK) g_diag.tx_errors++;
    while (HAL_GetTick() - started < 3000) {rx_recover(); g_diag.tick = HAL_GetTick();}
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    r->length = capture_used;
    for (uint32_t i = 0; i < r->length; ++i) r->response[i] = capture[i];
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    r->elapsed_ms = HAL_GetTick() - started; r->errors_after = g_diag.uart_errors;
    r->ok = line_equals(r->response, r->length, "OK");
    snprintf(log, sizeof(log), "[F103-DIAG] TX %s | RX %lu bytes | OK %lu | ERR %lu\r\n",
             r->command, (unsigned long)r->length, (unsigned long)r->ok, (unsigned long)r->errors_after);
    log_text(log); log_rx(r->response, r->length);
    g_diag.completed = index + 1;
}
static void decode_results(void)
{
    char text[DIAG_RX_CAPACITY + 1];
    g_diag.at_ok = g_diag.records[0].ok;
    for (unsigned i = 0; i < DIAG_RECORDS; ++i) {
        const DiagRecord *r = (const DiagRecord *)&g_diag.records[i];
        memcpy(text, r->response, r->length); text[r->length] = 0;
        if (i == 2 && line_equals(r->response, r->length, "+CPIN: READY")) g_diag.sim_ready = 1;
        char *p; unsigned first, second;
        if (i == 5 && (p = strstr(text, "+CSQ:")) != 0 && sscanf(p + 5, "%u", &first) == 1) g_diag.csq = first;
        if (i == 7 && (p = strstr(text, "+CEREG:")) != 0) {
            int count = sscanf(p + 7, "%u,%u", &first, &second);
            if (count > 0) g_diag.registration = count == 2 ? second : first;
        }
        if (i == 8 && (p = strstr(text, "+CGATT:")) != 0 && sscanf(p + 7, "%u", &first) == 1) g_diag.attached = first;
    }
}
int main(void)
{
    g_diag.magic = DIAG_MAGIC; g_diag.version = DIAG_VERSION;
    HAL_Init(); clock_init();
    GPIO_InitTypeDef p = {0}; __HAL_RCC_GPIOA_CLK_ENABLE();
    p.Pin = GPIO_PIN_1; p.Mode = GPIO_MODE_ANALOG; HAL_GPIO_Init(GPIOA, &p);
    uart_init(&huart1, USART1); uart_init(&huart2, USART2);
    if (HAL_UART_Receive_IT(&huart2, &rx_byte, 1) != HAL_OK) Error_Handler();
    Terminal_Init();
    g_diag.cpu_hz = HAL_RCC_GetHCLKFreq(); g_diag.registration = 0xffffffffu;
    g_diag.attached = 0xffffffffu; g_diag.csq = 0xffffffffu;
    log_text("[F103-DIAG] C8T6 HSI/PLL 64MHz UART1/UART2 115200 8N1\r\n");
    log_text("[F103-TERM] Ready. Send HELP, DIAG, or an AT command. PA1/PWRKEY untouched.\r\n");
    g_diag.state = 1;
    for (;;) {
        rx_recover(); g_diag.tick = HAL_GetTick();
        Terminal_Poll();
        if (g_diag.request == 1) {
            g_diag.request = 0; g_diag.state = 2; g_diag.run_number++; g_diag.completed = 0;
            g_diag.at_ok = 0; g_diag.sim_ready = 0; g_diag.registration = 0xffffffffu;
            g_diag.attached = 0xffffffffu; g_diag.csq = 0xffffffffu;
            memset((void *)g_diag.records, 0, sizeof(g_diag.records));
            while (HAL_GetTick() < 5000) rx_recover();
            /* Query AT three times at most; keep the final attempt for diagnosis. */
            for (unsigned attempt = 0; attempt < 3; ++attempt) {query(0); if (g_diag.records[0].ok) break;}
            if (g_diag.records[0].ok) for (unsigned i = 1; i < DIAG_RECORDS; ++i) query(i);
            decode_results(); g_diag.state = 3;
            log_text("[F103-DIAG] Complete; inspect g_diag for raw replies.\r\n");
        }
        HAL_Delay(1);
    }
}
void SysTick_Handler(void) {HAL_IncTick();}
void USART2_IRQHandler(void)
{
    uint32_t sr = USART2->SR;
    irq_errors = ((sr & USART_SR_FE) ? HAL_UART_ERROR_FE : 0u)
               | ((sr & USART_SR_NE) ? HAL_UART_ERROR_NE : 0u)
               | ((sr & USART_SR_ORE) ? HAL_UART_ERROR_ORE : 0u)
               | ((sr & USART_SR_PE) ? HAL_UART_ERROR_PE : 0u);
    HAL_UART_IRQHandler(&huart2); irq_errors = 0;
}
void NMI_Handler(void) {}
void HardFault_Handler(void) {g_diag.fatal = 2; g_diag.state = 4; while (1) {}}
void MemManage_Handler(void) {HardFault_Handler();}
void BusFault_Handler(void) {HardFault_Handler();}
void UsageFault_Handler(void) {HardFault_Handler();}
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
void Error_Handler(void) {g_diag.fatal = 1; g_diag.state = 4; __disable_irq(); while (1) {}}
