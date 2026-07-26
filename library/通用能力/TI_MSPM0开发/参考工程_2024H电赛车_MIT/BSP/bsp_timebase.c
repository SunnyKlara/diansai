#include "BSP/bsp_timebase.h"

#include "ti_msp_dl_config.h"

static volatile uint32_t g_nowMs;
static volatile uint16_t g_pendingTicks;

void bsp_timebase_init(void)
{
    g_nowMs = 0;
    g_pendingTicks = 0;

    NVIC_ClearPendingIRQ(CONTROL_TICK_INST_INT_IRQN);
    NVIC_EnableIRQ(CONTROL_TICK_INST_INT_IRQN);
    DL_TimerG_startCounter(CONTROL_TICK_INST);
}

bool bsp_timebase_take_tick(void)
{
    bool available = false;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (g_pendingTicks > 0U) {
        g_pendingTicks--;
        available = true;
    }
    if (primask == 0U) {
        __enable_irq();
    }

    return available;
}

uint32_t bsp_timebase_now_ms(void)
{
    return g_nowMs;
}

void CONTROL_TICK_INST_IRQHandler(void)
{
    if (DL_TimerG_getPendingInterrupt(CONTROL_TICK_INST) ==
        DL_TIMER_IIDX_ZERO) {
        g_nowMs++;
        if (g_pendingTicks < UINT16_MAX) {
            g_pendingTicks++;
        }
    }
}
