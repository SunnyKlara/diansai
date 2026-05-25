/**
 * SPWM正弦表模块
 * 功能：生成正弦查表值，供TIM1中断调用
 */

#include "spwm.h"

#define MAX_TABLE_LEN 800  // 最大支持的表长度

static float sine_table[MAX_TABLE_LEN];
static uint16_t table_length = 400;
static volatile uint16_t table_index = 0;

/**
 * 初始化正弦表
 * @param table_len 表长度 = 开关频率/输出频率
 *                  例: 20000/50 = 400
 */
void SPWM_Init(uint16_t table_len)
{
    if (table_len > MAX_TABLE_LEN) table_len = MAX_TABLE_LEN;
    table_length = table_len;
    table_index = 0;
    
    // 预计算正弦表
    for (uint16_t i = 0; i < table_length; i++) {
        sine_table[i] = sinf(2.0f * 3.14159265f * (float)i / (float)table_length);
    }
}

/**
 * 获取下一个正弦值（在TIM1中断中调用）
 * @return 正弦值 [-1.0, +1.0]
 */
float SPWM_GetNextValue(void)
{
    float val = sine_table[table_index];
    table_index++;
    if (table_index >= table_length) {
        table_index = 0;
    }
    return val;
}

/**
 * 动态修改输出频率
 * @param freq_hz 目标频率 (Hz)
 * 注意：开关频率不变，通过改变表长度改变输出频率
 */
void SPWM_SetFrequency(float freq_hz)
{
    uint16_t new_len = (uint16_t)(20000.0f / freq_hz);  // 假设fsw=20kHz
    if (new_len > MAX_TABLE_LEN) new_len = MAX_TABLE_LEN;
    if (new_len < 20) new_len = 20;  // 最高1kHz
    
    // 重新生成正弦表
    table_length = new_len;
    for (uint16_t i = 0; i < table_length; i++) {
        sine_table[i] = sinf(2.0f * 3.14159265f * (float)i / (float)table_length);
    }
    table_index = 0;
}

void SPWM_Reset(void)
{
    table_index = 0;
}
