/**
 * @file    main.c
 */
#include "../config.h"
#include "../Drivers/pair_mux.h"
#include "../Algorithm/twisted_pair.h"

static tp_result_t s_r;

int main(void)
{
    mux_init();
    tp_init();
    while (1) {
        tp_test_all(&s_r);
        /* OLED 显示每对状态 */
    }
}
