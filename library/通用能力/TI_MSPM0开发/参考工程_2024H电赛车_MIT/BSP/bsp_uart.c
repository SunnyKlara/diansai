#include "BSP/bsp_uart.h"

#include "ti_msp_dl_config.h"

#define UART_RX_BUFFER_SIZE (128U)
#define UART_LINE_BUFFER_SIZE (96U)

static volatile uint8_t g_rxBuffer[UART_RX_BUFFER_SIZE];
static volatile uint8_t g_rxHead;
static volatile uint8_t g_rxTail;
static volatile uint32_t g_rxCount;
static char g_lineBuffer[UART_LINE_BUFFER_SIZE];
static size_t g_lineLength;

void bsp_uart_init(void)
{
    g_rxHead = 0;
    g_rxTail = 0;
    g_rxCount = 0;
    g_lineLength = 0;
    NVIC_ClearPendingIRQ(UART_DEBUG_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_DEBUG_INST_INT_IRQN);
}

void bsp_uart_write_char(char value)
{
    DL_UART_Main_transmitDataBlocking(UART_DEBUG_INST, (uint8_t) value);
}

void bsp_uart_write(const char *text)
{
    while (*text != '\0') {
        bsp_uart_write_char(*text++);
    }
}

void bsp_uart_write_u32(uint32_t value)
{
    char digits[10];
    uint8_t count = 0;

    do {
        digits[count++] = (char) ('0' + (value % 10U));
        value /= 10U;
    } while ((value != 0U) && (count < sizeof(digits)));

    while (count > 0U) {
        bsp_uart_write_char(digits[--count]);
    }
}

void bsp_uart_write_i32(int32_t value)
{
    if (value < 0) {
        bsp_uart_write_char('-');
        bsp_uart_write_u32((uint32_t) (-(value + 1)) + 1U);
    } else {
        bsp_uart_write_u32((uint32_t) value);
    }
}

bool bsp_uart_read_line(char *destination, size_t capacity)
{
    while (g_rxTail != g_rxHead) {
        char value = (char) g_rxBuffer[g_rxTail];
        g_rxTail = (uint8_t) ((g_rxTail + 1U) % UART_RX_BUFFER_SIZE);

        if (value == '\r') {
            continue;
        }
        if (value == '\n') {
            size_t copyLength = g_lineLength;
            if (copyLength >= capacity) {
                copyLength = capacity - 1U;
            }
            for (size_t i = 0; i < copyLength; i++) {
                destination[i] = g_lineBuffer[i];
            }
            destination[copyLength] = '\0';
            g_lineLength = 0;
            return true;
        }
        if ((value == '\b') || (value == 0x7F)) {
            if (g_lineLength > 0U) {
                g_lineLength--;
            }
            continue;
        }
        if (g_lineLength < (UART_LINE_BUFFER_SIZE - 1U)) {
            g_lineBuffer[g_lineLength++] = value;
        }
    }

    return false;
}

uint32_t bsp_uart_rx_count(void)
{
    return g_rxCount;
}

void UART_DEBUG_INST_IRQHandler(void)
{
    if (DL_UART_Main_getPendingInterrupt(UART_DEBUG_INST) ==
        DL_UART_MAIN_IIDX_RX) {
        uint8_t value = DL_UART_Main_receiveData(UART_DEBUG_INST);
        uint8_t next = (uint8_t) ((g_rxHead + 1U) % UART_RX_BUFFER_SIZE);
        g_rxCount++;
        if (next != g_rxTail) {
            g_rxBuffer[g_rxHead] = value;
            g_rxHead = next;
        }
    }
}
