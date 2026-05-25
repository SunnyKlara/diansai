#include "ir_sensor.h"

/* TODO_MSPM0: #include <ti/driverlib/driverlib.h> */

void IR_Init(void)
{
    /* TODO_MSPM0: PB5..PB9 配置为 GPIO 输入，弱上拉 */
}

void IR_ReadAll(uint8_t raw[IR_NUM])
{
    /* TODO_MSPM0: 读取 PB5..PB9
     *  raw[0] = DL_GPIO_readPins(GPIOB, GPIO_PIN_5) ? 1 : 0;
     *  ...
     *  注意：模块输出 1=白，0=黑（取决于型号），需要根据实测取反
     */
    for (int i = 0; i < IR_NUM; i++) raw[i] = 0;
}
