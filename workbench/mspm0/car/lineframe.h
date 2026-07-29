#ifndef LINEFRAME_H
#define LINEFRAME_H
/*
 * lineframe.h - 八路巡线模块的串口帧解析（亚博 8 路红外/灰度模块）
 *
 * 定位：**纯算法层，不依赖 HAL/driverlib/newlib**，只吃字节 + 吃调用方给的时间戳 → 出结构体。
 *       因此可在 PC 上 gcc 编译并单元测试（pc_test/test_lineframe.c）。
 *       与邻居的分工：linesens.c = 收发字节(HAL) → 本文件 = 解析成 8 路读数 → line.c = 算横向偏差。
 *
 * ── 帧格式（厂家 PDF `0.8路巡线模块的使用方法` 第 3 节，权威真源在仓库内）──
 *
 *     数字型:  $D,x1:0,x2:0,x3:0,x4:0,x5:0,x6:0,x7:0,x8:0#
 *     模拟型:  $A,x1:4096,x2:4096,x3:4096,x4:4096,x5:4096,x6:4096,x7:4096,x8:4096#
 *
 *   极性：**0 = 在黑线上**（探头灯亮），1 = 白底。（源码注释与 I2C 示例一致。）
 *   模拟值范围 0..4096。
 *
 * ── ⚠ 这个协议**没有校验和** ⇒ 结构校验就是唯一防线 ──
 *   K230 那条链路有 `*HH` 异或校验，这里没有。所以本解析器**故意**把结构查得很死：
 *     · 前缀必须是 'D' 或 'A'
 *     · 必须恰好 8 组，且标签必须依次是 x1..x8（不是"有 8 个冒号"就算）
 *     · 数值必须在合法范围内（数字型 0/1；模拟型 0..4096）
 *     · 必须以 '#' 正常收尾，中途再遇 '$' 一律丢弃重新开始
 *   任何一条不满足就计 bad 并丢弃。宁可丢一帧，也不能把半截/串台的数据喂进控制环 ——
 *   循迹的输入错一帧，车就往错方向猛拐一下。
 *
 * ── 两条安全默认（沿用 uart_frame.h 的教训）──
 *   1. **"拿不到新鲜数据"必须让上层知道**：lf_get_* 只在"帧新鲜"时返 1，过期数据绝不冒充有效。
 *      否则模块掉线后车会按最后一帧偏差一直拐。
 *   2. **失败要有名字**：lf_status() 分 NO_DATA / STALE / OK，现场排障不用猜。
 *
 * 状态: 2026-07-29 新建。**PC 单测已过；真机未验**
 */
#include <stdint.h>

#define LF_CH        8       /* 固定 8 路（协议就是 x1..x8） */
#define LF_BUF      96       /* 单帧最大长度。模拟型满帧约 74 字节，留余量 */
#define LF_ANA_MAX  4096     /* 模拟值上限（协议规定） */

typedef enum {
    LF_OK = 0,        /* 有新鲜数据 */
    LF_NO_DATA,       /* 从未收到过合法帧 —— 查接线/使能命令有没有发出去 */
    LF_STALE          /* 收到过, 但已过期 —— 模块掉线/被复位/使能命令丢了 */
} lf_status_t;

typedef struct {
    /* --- 运行态（外部只读）--- */
    int      dig[LF_CH];      /* 最近一帧数字值 0/1（**0 = 在黑线上**）；从未收到则全 1(白底) */
    int      ana[LF_CH];      /* 最近一帧模拟值 0..4096；从未收到则全 -1 */
    uint32_t t_dig_ms;        /* 最近一帧数字型的时间戳 */
    uint32_t t_ana_ms;        /* 最近一帧模拟型的时间戳 */
    uint32_t n_dig, n_ana;    /* 收到的合法帧计数（分类型，便于判"只发了一种"） */
    uint32_t n_bad;           /* 结构不合法而被丢弃的帧数 —— 长期非 0 说明波特率/干扰有问题 */
    uint32_t n_ovf;           /* 帧长溢出次数（多半是没收到 '#'，说明链路在丢字节） */
    /* --- 内部 --- */
    char     buf[LF_BUF];
    int      len;
    int      in_frame;
} lf_t;

void lf_init(lf_t *L);

/* 喂一个字节。返回 1 = 本字节刚好收完一帧**合法**帧（可据此立即跑一次控制）。 */
int  lf_push(lf_t *L, char c, uint32_t now_ms);

/* 取最近一帧数字值。仅当"收到过 且 未超过 max_age_ms"时返 1 并填 out[8]；否则返 0 且不动 out。 */
int  lf_get_digital(const lf_t *L, uint32_t now_ms, uint32_t max_age_ms, int out[LF_CH]);
/* 取最近一帧模拟值。同上。 */
int  lf_get_analog(const lf_t *L, uint32_t now_ms, uint32_t max_age_ms, int out[LF_CH]);

/* 链路状态（把两种帧里较新的那个当依据）。 */
lf_status_t lf_status(const lf_t *L, uint32_t now_ms, uint32_t max_age_ms);

#endif /* LINEFRAME_H */
