/**
 * @file    main.c
 */
#include "../config.h"
#include "../Drivers/tdr_capture.h"
#include "../Algorithm/tdr_analyze.h"

static float s_wave[TDR_FRAME_LEN];
static tdr_result_t s_r;

int main(void)
{
    tdr_init();
    while (1) {
        tdr_trigger();
        if (tdr_collect(s_wave, TDR_FRAME_LEN)) {
            tdr_analyze(s_wave, TDR_FRAME_LEN, &s_r);
            /* OLED: 长度、终端态、阻抗 */
        }
    }
}
