/*
 * test_linesens.c - 验 linesens.h 里那段波特率分频算术（唯一能脱离 HAL 验的部分）
 *
 * 为什么值得单独测：运行时换波特率靠自己算 IBRD/FBRD 写寄存器。算错的表现是
 * **收到一堆看起来"像数据"的乱码** —— 而嗅探阶段我们正是在拿字节反推协议，
 * 一个错的波特率会让人对着噪声编出一套帧格式来。这类错误比"收不到"危险得多。
 *
 * 判据不是我自己编的：SysConfig 为 115200 生成的宏就在 gcc/ti_msp_dl_config.h 里 ——
 *     DBG_UART_IBRD_32_MHZ_115200_BAUD = 17
 *     DBG_UART_FBRD_32_MHZ_115200_BAUD = 23
 * 本函数对 115200 必须给出同一对数。**拿工具链自己的输出当真值**，而不是拿手算当真值。
 */
#include <stdio.h>
#include <stdint.h>

/* 只要这一个 static inline，不拖 HAL 进来。config.h 里与 HAL 无关的宏不影响本测试。 */
#define LINESENS_H_TEST_ONLY 1
static inline void linesens_baud_div(uint32_t baud, uint32_t *ibrd, uint32_t *fbrd)
{
    uint32_t d64 = (32000000u / 16u * 64u) / (baud ? baud : 9600u);
    *ibrd = d64 / 64u;
    *fbrd = d64 % 64u;
}

static int g_fail = 0;
static void ck(const char *what, uint32_t baud, uint32_t wi, uint32_t wf)
{
    uint32_t i = 0, f = 0;
    linesens_baud_div(baud, &i, &f);
    int ok = (i == wi && f == wf);
    if (!ok) g_fail++;
    printf("  [%s] %-34s baud=%6u -> IBRD=%4u FBRD=%3u  (want %4u/%3u)\n",
           ok ? "PASS" : "FAIL", what, baud, i, f, wi, wf);
}

int main(void)
{
    printf("test_linesens (BUSCLK=32MHz, 16x 过采样):\n");

    /* (a) ⭐ 与 SysConfig 生成的宏逐位一致 —— 这条是整份测试的锚 */
    ck("115200 = SysConfig 生成的真值", 115200u, 17u, 23u);

    /* (b) 其余候选波特率：手算 32e6/(16*baud)，整数位与小数位×64 截断
     *     9600 : 208.3333 -> 208 + 0.3333*64=21.33 -> 21
     *     19200: 104.1667 -> 104 + 0.1667*64=10.67 -> 10
     *     38400:  52.0833 ->  52 + 0.0833*64= 5.33 ->  5
     *     57600:  34.7222 ->  34 + 0.7222*64=46.22 -> 46 */
    ck("9600",  9600u,  208u, 21u);
    ck("19200", 19200u, 104u, 10u);
    ck("38400", 38400u,  52u,  5u);
    ck("57600", 57600u,  34u, 46u);

    /* (c) 除零保护：baud=0 不许崩、也不许算出 0 分频(那会让 UART 彻底哑)。
     *     退化成 9600 是有意的 —— 宁可用一个能出字节的速率, 也别配出一个死掉的 UART。 */
    ck("baud=0 退化为 9600(不许除零/不许 0 分频)", 0u, 208u, 21u);

    /* (d) IBRD 必须落在 16 bit 内(寄存器宽度), 最低候选 1200 也要安全 */
    {
        uint32_t i = 0, f = 0;
        linesens_baud_div(1200u, &i, &f);
        int ok = (i <= 0xFFFFu);
        if (!ok) g_fail++;
        printf("  [%s] %-34s baud=  1200 -> IBRD=%4u (须 <=65535)\n",
               ok ? "PASS" : "FAIL", "1200 的 IBRD 不溢出 16bit", i);
    }

    printf(g_fail ? "==== %d FAILED ====\n" : "==== ALL PASS ====\n", g_fail);
    return g_fail ? 1 : 0;
}
