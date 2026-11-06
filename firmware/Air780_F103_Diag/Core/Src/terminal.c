/* Text-command terminal: PC USART1 <-> Air780E USART2.
 * IRQs only enqueue bytes; transmission/parsing runs in the foreground.
 */
#include "terminal.h"
#include <string.h>

#define PC_RING 512u
#define MODEM_RING 2048u
#define LINE_SIZE 192u
typedef struct {
    uint32_t magic, pc_rx, modem_rx, commands, pc_overflow, modem_overflow;
    uint32_t pc_uart_errors, line_overflow, tx_errors, auto_line_end;
} TerminalReport;
volatile TerminalReport g_terminal;
static uint8_t pc_byte;
static volatile uint8_t pc_ring[PC_RING], modem_ring[MODEM_RING];
static volatile uint16_t pc_head, pc_tail, modem_head, modem_tail;
static volatile uint32_t pc_last_tick;
static volatile bool pc_fault;
static volatile bool dropping_line;
static unsigned used;
static char line[LINE_SIZE];

static void text(const char *s)
{
    if (HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 1000) != HAL_OK)
        g_terminal.tx_errors++;
}
void Terminal_Init(void)
{
    g_terminal.magic = 0x5445524du;
    if (HAL_UART_Receive_IT(&huart1, &pc_byte, 1) != HAL_OK) pc_fault = true;
}
bool Terminal_OnRx(UART_HandleTypeDef *u)
{
    if (u != &huart1) return false;
    uint16_t next = (pc_head + 1u) & (PC_RING - 1u);
    g_terminal.pc_rx++; pc_last_tick = HAL_GetTick();
    if (next == pc_tail) {g_terminal.pc_overflow++; dropping_line = true;}
    else {pc_ring[pc_head] = pc_byte; __DMB(); pc_head = next;}
    if (HAL_UART_Receive_IT(u, &pc_byte, 1) != HAL_OK) pc_fault = true;
    return true;
}
bool Terminal_OnError(UART_HandleTypeDef *u)
{
    if (u != &huart1) return false;
    g_terminal.pc_uart_errors++; pc_fault = true;
    return true;
}
void Terminal_OnModemByte(uint8_t byte)
{
    g_terminal.modem_rx++;
    if (g_diag.state == 2) return; /* Diagnostic suite prints its own records. */
    uint16_t next = (modem_head + 1u) & (MODEM_RING - 1u);
    if (next == modem_tail) g_terminal.modem_overflow++;
    else {modem_ring[modem_head] = byte; __DMB(); modem_head = next;}
}
static void finish_line(void)
{
    if (dropping_line) {
        dropping_line = false; used = 0;
        text("\r\n[F103-TERM] Input overflow; command discarded.\r\n"); return;
    }
    if (!used) return;
    line[used] = 0;
    if (!strcmp(line, "HELP") || !strcmp(line, "help")) {
        text("\r\n[F103-TERM] Ready: 115200 8N1.\r\n");
        text("Send an AT command. CR/LF optional with Send button.\r\n");
        text("DIAG: run the nine-command query suite. HELP: show this message.\r\n");
    } else if (!strcmp(line, "DIAG") || !strcmp(line, "diag")) {
        g_diag.request = 1;
    } else if ((line[0] == 'A' || line[0] == 'a') && used >= 2 &&
               (line[1] == 'T' || line[1] == 't')) {
        line[used++] = '\r'; line[used++] = '\n';
        if (HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)used, 100) != HAL_OK)
            {g_terminal.tx_errors++; text("\r\n[F103-TERM] Modem TX failed.\r\n");}
        else g_terminal.commands++;
    } else text("\r\n[F103-TERM] Enter an AT command, HELP, or DIAG.\r\n");
    used = 0;
}
void Terminal_Poll(void)
{
    if (pc_fault) {
        HAL_NVIC_DisableIRQ(USART1_IRQn);
        HAL_UART_AbortReceive(&huart1); __HAL_UART_CLEAR_OREFLAG(&huart1);
        pc_fault = false;
        if (HAL_UART_Receive_IT(&huart1, &pc_byte, 1) != HAL_OK) pc_fault = true;
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        dropping_line = true; used = 0;
    }
    uint8_t output[128]; unsigned n = 0;
    while (modem_tail != modem_head && n < sizeof(output)) {
        output[n++] = modem_ring[modem_tail]; __DMB();
        modem_tail = (modem_tail + 1u) & (MODEM_RING - 1u);
    }
    if (n && HAL_UART_Transmit(&huart1, output, (uint16_t)n, 100) != HAL_OK) g_terminal.tx_errors++;
    while (pc_tail != pc_head && !g_diag.request) {
        uint8_t c = pc_ring[pc_tail]; __DMB(); pc_tail = (pc_tail + 1u) & (PC_RING - 1u);
        if (c == '\r' || c == '\n') finish_line();
        else if (!dropping_line && (c == 8 || c == 127)) {if (used) used--;}
        else if (!dropping_line && c >= 32 && c <= 126) {
            if (used < LINE_SIZE - 3u) line[used++] = (char)c;
            else {dropping_line = true; g_terminal.line_overflow++;}
        }
    }
    if (!g_diag.request && pc_tail == pc_head && (used || dropping_line) &&
        HAL_GetTick() - pc_last_tick >= 150) {
        g_terminal.auto_line_end++; finish_line();
    }
}
void USART1_IRQHandler(void) {HAL_UART_IRQHandler(&huart1);}
