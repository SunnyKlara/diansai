#ifndef __BUTTON_H
#define __BUTTON_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,
    KEY_PAGE,            /* 切换显示页 */
    KEY_CALIB,           /* 进入校准模式 */
} KeyId;

void  Button_Init(void);
KeyId Button_Scan(void);

#endif
