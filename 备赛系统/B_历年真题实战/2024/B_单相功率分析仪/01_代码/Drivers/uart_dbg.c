/**
 * @file    uart_dbg.c
 * @brief   UART 调试通道实现（命令解析 + 校准接口 + 原始数据导出）
 */

#include "uart_dbg.h"
#include "calibration.h"
#include "adc_dual.h"
#include "../config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* TODO_TI: #include <ti/devices/msp430fr5xx_6xx/driverlib/...> */

/* 命令缓冲：不应过大，UART 中断 ISR 内部追加字符即可 */
#define CMD_BUF_LEN     64u
static volatile char    s_cmd_buf[CMD_BUF_LEN];
static volatile uint8_t s_cmd_idx = 0u;
static volatile uint8_t s_cmd_ready = 0u;     /* 收到 \n 时置 1 */

/* 临时记录"上次完整测量结果"，校准命令时可直接拿来反推 gain */
typedef struct {
    float v_rms;
    float i_rms;
    float p, pf, thd;
} LastResult;

static LastResult s_last;

/* ============================================================
 *  低层字节操作（TODO_TI 待填）
 * ============================================================ */
void UART_Init(uint32_t baud)
{
    (void)baud;
    /* TODO_TI:
     *   eUSCI_A0：SMCLK 16MHz，UCBRx = 8，UCBRSx = 0xD6，UCBRFx = 10 → 115200 bps
     *   P2SEL1 |= BIT0 | BIT1; (UART 复用)
     *   使能 RX 中断
     */
    s_cmd_idx   = 0u;
    s_cmd_ready = 0u;
}

void UART_PutChar(char c)
{
    (void)c;
    /* TODO_TI:
     *   while (!(UCA0IFG & UCTXIFG));
     *   UCA0TXBUF = c;
     */
}

void UART_PutString(const char *s)
{
    if (!s) return;
    while (*s) UART_PutChar(*s++);
}

void UART_Printf(const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UART_PutString(buf);
}

/* ============================================================
 *  ISR：每收到一个字节追加到 cmd_buf，遇到 \n 标记完成
 *  TODO_TI：把 RX 中断挂到这个函数
 * ============================================================ */
void UART_OnRxByte(char c)
{
    if (s_cmd_ready) return;             /* 上一条还没处理完，丢弃 */
    if (c == '\r') return;               /* 忽略 CR */
    if (c == '\n' || s_cmd_idx >= CMD_BUF_LEN - 1u) {
        s_cmd_buf[s_cmd_idx] = '\0';
        s_cmd_ready = 1u;
        return;
    }
    s_cmd_buf[s_cmd_idx++] = c;
}

/* ============================================================
 *  把"上一次测量结果"喂给 UART 模块
 *  main.c 在每次 run_measure_cycle 末尾调用
 * ============================================================ */
void UART_UpdateLastResult(float v_rms, float i_rms,
                           float p, float pf, float thd)
{
    s_last.v_rms = v_rms;
    s_last.i_rms = i_rms;
    s_last.p     = p;
    s_last.pf    = pf;
    s_last.thd   = thd;
}

/* ============================================================
 *  命令解析
 * ============================================================ */
static void handle_stat(void)
{
    UART_Printf("V=%.3f I=%.4f P=%.3f PF=%.4f THD=%.3f\r\n",
                (double)s_last.v_rms, (double)s_last.i_rms,
                (double)s_last.p,     (double)s_last.pf,
                (double)s_last.thd);
}

static void handle_cal(const char *args)
{
    /* 期望格式：CAL V=220.0 I=2.20 */
    float v_real = 0.0f, i_real = 0.0f;
    const char *pv = strstr(args, "V=");
    const char *pi = strstr(args, "I=");
    if (pv) v_real = (float)atof(pv + 2);
    if (pi) i_real = (float)atof(pi + 2);

    if (v_real <= 0.0f || i_real <= 0.0f) {
        UART_PutString("ERR CAL: need V=xxx I=xxx\r\n");
        return;
    }
    if (s_last.v_rms <= 0.5f || s_last.i_rms <= 0.005f) {
        UART_PutString("ERR CAL: no fresh measurement\r\n");
        return;
    }
    Calib_ComputeAndApply(s_last.v_rms, v_real, s_last.i_rms, i_real);

    CalibrationData d;
    Calib_GetCurrent(&d);
    UART_Printf("OK CAL v_gain=%.6f i_gain=%.6f\r\n",
                (double)d.v_gain, (double)d.i_gain);
}

static void handle_dump(void)
{
    /* 把 DMA 原始缓冲以 CSV 格式吐出，PC 跑 algo_reference.py 验证 */
    const uint16_t *raw = ADCDual_GetRawBuffer();
    if (!raw) { UART_PutString("ERR DUMP: no buffer\r\n"); return; }

    UART_PutString("DUMP_START\r\n");
    for (uint16_t k = 0; k < (uint16_t)DFT_N; k++) {
        UART_Printf("%u,%u\r\n",
                    (unsigned)raw[2u * k],         /* V_adc */
                    (unsigned)raw[2u * k + 1u]);   /* I_adc */
    }
    UART_PutString("DUMP_END\r\n");
}

static void handle_rst(void)
{
    Calib_Reset();
    UART_PutString("OK RST: factory defaults restored\r\n");
}

void UART_PollCommands(void)
{
    if (!s_cmd_ready) return;

    const char *cmd = (const char *)s_cmd_buf;

    if (strncmp(cmd, "STAT", 4) == 0)        handle_stat();
    else if (strncmp(cmd, "CAL ", 4) == 0)   handle_cal(cmd + 4);
    else if (strncmp(cmd, "DUMP", 4) == 0)   handle_dump();
    else if (strncmp(cmd, "RST",  3) == 0)   handle_rst();
    else                                     UART_PutString("ERR: unknown cmd\r\n");

    s_cmd_idx   = 0u;
    s_cmd_ready = 0u;
}
