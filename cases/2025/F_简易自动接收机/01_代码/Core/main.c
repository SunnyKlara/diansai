/**
 * @file    main.c
 */
#include "../config.h"
#include "../Drivers/si5351.h"
#include "../Algorithm/auto_tune.h"

int main(void)
{
    si5351_init();
    auto_tune_init();
    si5351_set_freq(0, RX_F_MIN_HZ + RX_IF_HZ);
    while (1) { /* 主循环：扫频 + 锁台 */ }
}
