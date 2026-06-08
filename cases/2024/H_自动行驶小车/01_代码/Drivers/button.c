/**
 * @file    button.c
 * @brief   MSPM0G3507 4 按键扫描 + 软件去抖
 */

#include "button.h"
#include <ti/driverlib/driverlib.h>
#include "ti_msp_dl_config.h"

/* SysConfig 已配置 PA18..PA21 为输入 + 上拉。
 * 实例宏（按你的 .syscfg 命名调整）：
 *   GPIO_KEY_PORT, GPIO_KEY_KEY1_PIN, KEY2_PIN, KEY3_PIN, KEY4_PIN
 */

static uint8_t s_last_state[4] = {1, 1, 1, 1};

static const uint32_t s_pin_mask[4] = {
    GPIO_KEY_KEY1_PIN, GPIO_KEY_KEY2_PIN, GPIO_KEY_KEY3_PIN, GPIO_KEY_KEY4_PIN
};

static uint8_t read_pin(uint8_t idx)
{
    if (idx >= 4) return 1;
    /* 上拉输入：按下接 GND → 0；释放 → 1 */
    return (DL_GPIO_readPins(GPIO_KEY_PORT, s_pin_mask[idx]) != 0) ? 1 : 0;
}

void Button_Init(void)
{
    /* SysConfig 已配置好。这里清初值。 */
    for (uint8_t i = 0; i < 4; i++) s_last_state[i] = 1;
}

KeyId Button_Scan(void)
{
    /* 50 Hz 扫描节奏调用：检测下降沿（释放→按下）即触发一次事件。 */
    KeyId result = KEY_NONE;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t now = read_pin(i);
        if ((s_last_state[i] == 1) && (now == 0)) {
            result = (KeyId)(i + 1);   /* 1=START, 2=STOP, 3=TASK, 4=RESET */
        }
        s_last_state[i] = now;
    }
    return result;
}
