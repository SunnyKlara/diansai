#ifndef __TWISTED_PAIR_H__
#define __TWISTED_PAIR_H__
#include <stdint.h>

typedef enum {
    TP_OK         = 0,    /* 直通     */
    TP_OPEN       = 1,    /* 开路     */
    TP_SHORT      = 2,    /* 短路     */
    TP_CROSS      = 3,    /* 交错连接 */
    TP_REVERSED   = 4
} tp_status_t;

typedef struct {
    tp_status_t pair[4];   /* 4 对线状态 */
    float length_m[4];     /* 每对线长度 */
} tp_result_t;

void tp_init(void);
void tp_test_all(tp_result_t *out);

#endif
