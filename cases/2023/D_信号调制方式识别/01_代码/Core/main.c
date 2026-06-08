/**
 * @file    main.c
 */
#include "../config.h"
#include "../Drivers/rf_input.h"
#include "../Algorithm/modulation_detect.h"

static float s_i[MOD_FRAME_LEN], s_q[MOD_FRAME_LEN];
static mod_result_t s_r;

int main(void)
{
    rf_init(); mod_init();
    while (1) {
        if (rf_get_iq(s_i, s_q, MOD_FRAME_LEN)) {
            mod_analyze(s_i, s_q, MOD_FRAME_LEN, &s_r);
            /* OLED 显示调制类型 */
        }
    }
}
