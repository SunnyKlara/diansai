/*
 * encoder.c - 双编码器 GPIO 中断 1x 解码 (GROUP1 汇聚 GPIOA+GPIOB)
 *   enc1: A=PA7(中断) B=PB19(读)   enc2: A=PB20(中断) B=PB21(读)
 *   A 上升沿触发中断 -> 读对应 B 电平 -> B 高计 -1 / B 低计 +1 (符号约定待手转校准)。
 */
#include "ti_msp_dl_config.h"
#include "encoder.h"

static volatile int32_t g_cnt[2] = {0, 0};

void encoder_init(void)
{
    g_cnt[ENC_1] = 0;
    g_cnt[ENC_2] = 0;
    /* GPIOA(含 PA7=enc1 A) 与 GPIOB(含 PB20=enc2 A) 的中断都要开; 二者共用 GROUP1 handler */
    NVIC_EnableIRQ(GPIO_ENC_GPIOA_INT_IRQN);
    NVIC_EnableIRQ(GPIO_ENC_GPIOB_INT_IRQN);
}

int32_t encoder_count(uint8_t ch) { return g_cnt[ch & 1]; }
void    encoder_reset(uint8_t ch) { g_cnt[ch & 1] = 0; }

/* GROUP1 汇聚 GPIOA + GPIOB 中断 */
void GROUP1_IRQHandler(void)
{
    /* --- GPIOA: enc1 A = PA7 --- */
    uint32_t a = DL_GPIO_getEnabledInterruptStatus(GPIOA, GPIO_ENC_ENC1_A_PIN);
    if (a & GPIO_ENC_ENC1_A_PIN) {
        if (DL_GPIO_readPins(GPIOB, GPIO_ENC_ENC1_B_PIN))   /* PB19 */
            g_cnt[ENC_1]--;
        else
            g_cnt[ENC_1]++;
        DL_GPIO_clearInterruptStatus(GPIOA, GPIO_ENC_ENC1_A_PIN);
    }

    /* --- GPIOB: enc2 A = PB20 --- */
    uint32_t b = DL_GPIO_getEnabledInterruptStatus(GPIOB, GPIO_ENC_ENC2_A_PIN);
    if (b & GPIO_ENC_ENC2_A_PIN) {
        if (DL_GPIO_readPins(GPIOB, GPIO_ENC_ENC2_B_PIN))   /* PB21 */
            g_cnt[ENC_2]--;
        else
            g_cnt[ENC_2]++;
        DL_GPIO_clearInterruptStatus(GPIOB, GPIO_ENC_ENC2_A_PIN);
    }
}
