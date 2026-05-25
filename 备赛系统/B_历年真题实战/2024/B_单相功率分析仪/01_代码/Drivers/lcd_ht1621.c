#include "lcd_ht1621.h"
#include <stdio.h>

/* TODO_TI: #include <ti/devices/msp430fr5xx_6xx/driverlib/...> */

/* HT1621 命令字（节选）*/
#define HT1621_CMD_SYS_EN    0x01u
#define HT1621_CMD_LCD_ON    0x03u
#define HT1621_CMD_LCD_OFF   0x02u
#define HT1621_CMD_BIAS_1_3  0x29u  /* 1/3 偏置，4 COM 驱动 */

void LCD_Init(void)
{
    /* TODO_TI:
     *   GPIO 三引脚（CS, WR, DATA）输出
     *   按 HT1621 时序发送命令：
     *     SYS_EN → BIAS_1/3 → LCD_ON
     */
}

void LCD_Clear(void)
{
    /* TODO_TI: 写入 32 个 0 到显存 */
}

void LCD_ShowFloat(uint8_t slot, float value, const char *unit)
{
    (void)slot; (void)value; (void)unit;
    /* TODO_TI:
     *   1) 把 value 转成 4 位数字字符串（自动选小数位）
     *   2) 查段码字模，写到对应的显存地址
     *   3) 同步点亮单位标识段（V/A/W 等）
     */
}

void LCD_LowPowerMode(void)
{
    /* TODO_TI: 仅关闭 HT1621 主机时钟，段码静态保持，约 1μA */
}
