#ifndef LINESENS_H
#define LINESENS_H
/*
 * linesens.h - 循迹传感器串口链路（UART1, TX=PB4 / RX=PB5）
 *
 * 阶段定位（**先当嗅探器，再当驱动**）：
 *   手上的模块是"亚博八路智能巡线（串口输出）"，**协议未知且查不到**（中文社区实测不可达，
 *   见 SSOT §D"查在线资料的链路"）。所以本模块第一版**不解析任何协议**，只做三件事：
 *     ① 把 UART1 收到的原始字节存进环形缓冲（带**帧间隙标记**）
 *     ② 运行时切波特率（命令 `B<baud>`）—— 一次烧录扫完全部候选
 *     ③ dump 出 HEX + ASCII 双视图（命令 `L`）
 *   拿到真实字节后再写解析器（仿 uart_frame.c，纯算法层可 PC 单测），最后接进 line.c。
 *
 * ── 为什么要"帧间隙标记" ──
 *   反解串口协议最难的一步是**找帧边界**：一串 HEX 里哪个字节是帧头？
 *   串口设备几乎总是"发一帧、停一会、再发一帧"，所以**字节间的时间空隙就是帧边界**。
 *   本模块记下每个字节前的空隙是否 >= LINESENS_GAP_MS，dump 时用 '|' 标出来 ⇒ 帧结构直接可见，
 *   不必猜文档。（这比先去翻手册快，且对"厂家文档与实物不符"免疫。）
 *
 * ── 为什么波特率做成运行时可切 ──
 *   本仓库禁忌 2：**反复快烧会把 MCU 怼进 lockup**（已发生过一次，救砖花了一小时）。
 *   为 5 个候选波特率各烧一次板 = 5 次砖化机会。故改分频寄存器即可切换，一次烧录搞定。
 *
 * 定位：本文件**依赖 HAL**（要碰 UART 寄存器），故不可 PC 单测；
 *       但波特率分频的算术抽成了下面的 static inline，pc_test/test_linesens.c 单独验它。
 *
 * 状态: 2026-07-29 新建。**编译级 / 真机零验证**。
 */
#include <stdint.h>
#include "config.h"

#ifndef LINESENS_BUF
#define LINESENS_BUF     192    /* 环形缓冲字节数。够装十几帧, 又不吃太多 RAM */
#endif
#ifndef LINESENS_GAP_MS
#define LINESENS_GAP_MS  3      /* 字节间空隙 >= 此值 ⇒ 认为是新帧的开始（dump 里打 '|'）。
                                 * 3ms 的依据: 9600bps 下一个字节约 1.04ms, 帧内相邻字节间隙
                                 * 远小于 3ms; 而模块两帧之间通常有 >=10ms 的静默。
                                 * 若实测发现帧被切碎, 调大它（是纯显示用途, 不影响数据）。*/
#endif

/*
 * BUSCLK=32MHz + 16x 过采样下的波特率分频。
 *   div = 32e6 / (16 * baud)，IBRD = 整数部分，FBRD = 小数部分 × 64（截断）。
 * ⭐ **算法的正确性有现成真值可对**: SysConfig 为 115200 生成的宏是
 *      DBG_UART_IBRD_32_MHZ_115200_BAUD = 17 / FBRD = 23
 *    本函数对 115200 必须给出同一对数（pc_test/test_linesens.c 就断言这个）。
 * 溢出说明: 32e6/16 = 2e6，×64 = 1.28e8 < 2^32，安全。
 */
static inline void linesens_baud_div(uint32_t baud, uint32_t *ibrd, uint32_t *fbrd)
{
    uint32_t d64 = (32000000u / 16u * 64u) / (baud ? baud : 9600u);
    *ibrd = d64 / 64u;
    *fbrd = d64 % 64u;
}

/* 上电初始化：把 UART1 的波特率设成 CFG_LINE_UART_BAUD 并清空缓冲。
 * （外设本身由 SYSCFG_DL_init() 建好，这里只改分频 + 清状态。） */
void linesens_init(void);

/* 运行时换波特率。会清空缓冲与统计（旧波特率下收到的都是垃圾字节，留着只会误导判读）。 */
void linesens_set_baud(uint32_t baud);
uint32_t linesens_get_baud(void);

/* 主循环里调用：把 UART1 收到的字节全部搬进环形缓冲。
 * now_ms = 当前毫秒时基（car.c 用 g_st/ST_PER_MS），用来算帧间隙。
 * 返回本次搬了几个字节。 */
int linesens_poll(uint32_t now_ms);

/* 打印统计 + HEX/ASCII 双视图。'|' = 该字节前有帧间隙。
 * 统计里 rx 总数是**判"线到底通没通"的第一判据** —— 为 0 就别急着调协议, 先查接线/共地/电平。 */
void linesens_dump(void);

/* 供遥测用的轻量计数（每拍都打不划算，所以只出数字）。 */
uint32_t linesens_rx_total(void);
uint32_t linesens_frames(void);      /* 见到过多少次帧间隙（≈收到多少帧） */

/* 只清缓冲与计数，**不动波特率**（扫查询字节时每试一个都要清一次）。 */
void linesens_clear(void);

/* 往模块发一个字节。用途有两个：
 *   ① 若模块是"主机问、模块答"，这就是查询帧的发送手段（协议未知时配合 `Q` 扫）
 *   ② 配合下面的内部回环做自测
 * ⚠ 纯监听阶段本函数**不会被自动调用** —— 不知道协议就乱发字节可能撞上模块的配置命令。 */
void linesens_tx(uint8_t b);

/*
 * 内部回环自测（UART 外设自带：TXD 在**芯片内部**接回 RXD，不经过引脚）。
 * ⭐ 它回答的是那个最要紧的分岔口：**"收不到"是我们的接收链路坏了，还是对方根本没发？**
 *   回环通 ⇒ UART1 外设 + 波特率 + 我们的轮询/环形缓冲**整条链路都是好的** ⇒ 问题在模块侧或引脚外；
 *   回环不通 ⇒ 别再查接线了，是固件/外设配置问题。
 * 注意它**不测引脚与外部走线**（内部就短接了），所以它证明不了 PB5 焊点好坏 —— 但能把嫌疑砍掉一半，
 * 且**不需要动任何一根线**，比插跳线的外部回环好用得多。
 * 返回收到的字节数（期望 == 发出的字节数）。函数内部会自行清缓冲、结束后恢复正常模式。
 */
int linesens_selftest_loopback(void);

/* 立刻发一次使能命令 CFG_LINE_ENABLE_CMD（`$0,1,1#`）。
 * 正常不用手动调 —— linesens_poll 会在"还没收到过任何合法帧"时按 CFG_LINE_ENABLE_MS 周期自动重发。
 * 留出手动入口是为了：模块刚做完校准 / 刚按了 reset 时，不必等下一个周期。 */
void linesens_send_enable(void);

#endif /* LINESENS_H */
