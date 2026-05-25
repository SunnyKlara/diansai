#ifndef __BUTTON_H
#define __BUTTON_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,
    KEY_FREQ_UP,
    KEY_FREQ_DOWN,
    KEY_START,
    KEY_STOP,
    KEY_TASK,
} KeyId;

void  Button_Init(void);
KeyId Button_Scan(void);     /* 20 ms 周期调用，返回边沿 */

#endif
