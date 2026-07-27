#ifndef UART_FRAME_H
#define UART_FRAME_H
/*
 * uart_frame.h - 外部智能模块 -> MCU 的串口帧解析(视觉坐标链)
 *
 * 用途: 收 K230(或任意"可带处理器的摄像头")吐出的目标坐标帧, 交给上层做视觉伺服。
 * 定位: **纯算法层, 不依赖 HAL/driverlib/newlib**, 只吃字节 + 吃调用方给的时间戳 ->
 *       出结构体。因此可在 PC 上 gcc 编译并单元测试(见 pc_test/test_uart_frame.c)。
 *
 * ── 帧格式(定死后写进 SSOT, 别再改; ASCII 是为了赛场能在串口助手里肉眼读) ──
 *
 *     $V,<id>,<cx>,<cy>,<area>*<HH>\n
 *
 *   V      固定标识(vision)
 *   id     目标类别/编号; **-1(UF_ID_NONE) = 本帧没看到目标**
 *          —— 显式表达"无目标", 别让上层靠超时去猜"是没看到还是模块挂了"
 *   cx,cy  目标中心像素坐标(整数, 图像左上为原点)
 *   area   目标面积(像素数), 可当"距离"的粗代理(越大越近)
 *   HH     校验: '$' 与 '*' 之间所有字符的**异或**, 两位大写 HEX
 *          (异或而非 CRC: K230 侧 MicroPython 一行算得出, 赛场可手验)
 *   \n     帧结束('\r' 被忽略, 所以 "\r\n" 也能吃)
 *
 *   例: $V,1,320,240,1500*<HH>\n    看到 1 号目标, 中心(320,240), 面积 1500
 *       $V,-1,0,0,0*<HH>\n          没看到目标
 *
 * ── 设计上的两条安全默认(踩过的坑决定的) ──
 *   1. **"拿不到新鲜目标"必须让上层知道**: uf_get() 只在"帧新鲜 且 确实有目标"时返 1。
 *      过期数据绝不冒充有效 —— 否则 K230 掉线后车会按最后一帧坐标一直冲。
 *   2. **失败要有名字**: uf_status() 把失败分成 NO_DATA / STALE / NO_TARGET 三类,
 *      分别对应"链路没通/模块掉线/只是没看到" —— 现场排障不用猜。
 *
 * 状态: 2026-07-27 新建。**PC 单测已过; 真机未验**
 *       (K230 侧发送脚本 + MCU 第二路 UART 都还没上板, 见 README/SSOT)。
 */
#include <stdint.h>

/* ── 可调参数(允许 config.h 先 define 来覆盖) ────────────────────────── */
#ifndef UF_BUF_LEN
#define UF_BUF_LEN      48      /* 单帧最大字符数(不含 '$'), 超长即整帧丢弃 */
#endif
#ifndef UF_STALE_MS
#define UF_STALE_MS     200     /* 超过这么久没收到有效帧 => 数据判为不新鲜 */
#endif

#define UF_ID_NONE      (-1)    /* id 字段约定值: 本帧没看到目标 */

/* ── 一帧目标数据 ─────────────────────────────────────────────────── */
typedef struct {
    int32_t  id;        /* 目标类别/编号; UF_ID_NONE = 没看到 */
    int32_t  cx, cy;    /* 中心像素坐标 */
    int32_t  area;      /* 面积(像素) */
    uint32_t stamp_ms;  /* 收到该帧的时刻(调用方传入的时间戳) */
} uf_target_t;

/* ── 取数结果分类(失败也要有名字) ──────────────────────────────────── */
typedef enum {
    UF_OK = 0,      /* 新鲜, 且确实有目标 -> 可以伺服 */
    UF_NO_DATA,     /* 从没收到过任何有效帧 -> 查接线/TX-RX 交叉/共地/波特率/模块有没有跑 */
    UF_STALE,       /* 收到过但已过期     -> 模块掉线/死机/帧率低于 UF_STALE_MS */
    UF_NO_TARGET    /* 新鲜但 id=NONE     -> 链路是好的, 只是视野里没有目标 */
} uf_status_t;

/* ── 解析器状态 ───────────────────────────────────────────────────── */
typedef struct {
    uf_target_t last;           /* 最近一帧有效数据 */
    uint8_t     have_frame;     /* 是否曾收到过至少一帧有效帧 */

    /* 反馈健康统计 —— "先证反馈可信, 再碰控制器"的量化依据 */
    uint32_t n_ok;              /* 校验+格式都通过的帧数 */
    uint32_t n_bad_csum;        /* 校验和不匹配(线上有干扰/发端算错) */
    uint32_t n_bad_form;        /* 格式错(字段缺失/非数字/无'*'/帧被截断) */
    uint32_t n_overflow;        /* 超长没等到 '\n'(波特率不匹配的典型症状) */

    /* 内部接收状态机 */
    char     buf[UF_BUF_LEN];
    uint16_t len;
    uint8_t  in_frame;
} uf_parser_t;

/* 初始化(清零) */
void uf_init(uf_parser_t *p);

/*
 * 喂一个收到的字节。now_ms = 当前毫秒时钟(MCU 上用 SysTick 计数, PC 测试里给假值)。
 * 返回 1 = 本字节刚好收完并解析成功一帧(可用于"来新数据了"的触发), 否则 0。
 * 在 UART RX 中断或主循环轮询里逐字节调用即可。
 */
int uf_push(uf_parser_t *p, char c, uint32_t now_ms);

/* 数据新鲜吗(收到过 且 未超过 UF_STALE_MS)。不关心有没有目标。 */
int uf_fresh(const uf_parser_t *p, uint32_t now_ms);

/* 当前取数状态(见 uf_status_t)。排障/打印用。 */
uf_status_t uf_status(const uf_parser_t *p, uint32_t now_ms);

/*
 * 取一个"可以拿去做控制"的目标。
 * 返回 1 并填 out(out 可为 NULL) 仅当 uf_status()==UF_OK; 其余情况返 0 且不动 out。
 * 上层照这样写就自然安全: if (uf_get(...)) { 伺服 } else { 停车/搜索 }
 */
int uf_get(const uf_parser_t *p, uint32_t now_ms, uf_target_t *out);

/* 异或校验(供发端/测试复用): body = '$' 与 '*' 之间的 n 个字符 */
uint8_t uf_checksum(const char *body, uint16_t n);

#endif /* UART_FRAME_H */
