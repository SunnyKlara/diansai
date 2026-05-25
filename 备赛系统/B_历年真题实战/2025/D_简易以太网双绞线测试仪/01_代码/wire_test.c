/**
 * 2025D 双绞线测试仪 - 线序检测模块
 * 
 * 原理：端口1逐线施加高电平，端口2逐线检测
 * 8根线扫描一遍即可判断直连/交叉/断线
 * 
 * RJ45引脚定义 (T568B标准):
 *   Pin1: 白橙  Pin2: 橙   (Pair 2)
 *   Pin3: 白绿  Pin4: 蓝   (Pair 1/3)
 *   Pin5: 白蓝  Pin6: 绿   (Pair 1/3)
 *   Pin7: 白棕  Pin8: 棕   (Pair 4)
 */

#include "stm32f4xx_hal.h"

// 端口1引脚定义 (输出)
#define PORT1_GPIO  GPIOB
#define PORT1_PINS  {GPIO_PIN_0,GPIO_PIN_1,GPIO_PIN_2,GPIO_PIN_3, \
                     GPIO_PIN_4,GPIO_PIN_5,GPIO_PIN_6,GPIO_PIN_7}

// 端口2引脚定义 (输入，上拉)
#define PORT2_GPIO  GPIOC
#define PORT2_PINS  {GPIO_PIN_0,GPIO_PIN_1,GPIO_PIN_2,GPIO_PIN_3, \
                     GPIO_PIN_4,GPIO_PIN_5,GPIO_PIN_6,GPIO_PIN_7}

static const uint16_t port1_pins[8] = {
    GPIO_PIN_0,GPIO_PIN_1,GPIO_PIN_2,GPIO_PIN_3,
    GPIO_PIN_4,GPIO_PIN_5,GPIO_PIN_6,GPIO_PIN_7
};
static const uint16_t port2_pins[8] = {
    GPIO_PIN_0,GPIO_PIN_1,GPIO_PIN_2,GPIO_PIN_3,
    GPIO_PIN_4,GPIO_PIN_5,GPIO_PIN_6,GPIO_PIN_7
};

// 线序映射结果: mapping[i] = j 表示端口1的第i根线连接到端口2的第j根线
// -1表示断线
static int8_t mapping[8];

typedef enum {
    WIRE_STRAIGHT,   // 直连 (T568B-T568B)
    WIRE_CROSSOVER,  // 交叉 (T568A-T568B)
    WIRE_FAULT,      // 故障（断线或错线）
    WIRE_UNKNOWN
} WireType_t;

/**
 * 检测线序
 * @return 线缆类型
 */
WireType_t WireTest_CheckMapping(void)
{
    // 1. 所有端口1引脚设为低电平
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(PORT1_GPIO, port1_pins[i], GPIO_PIN_RESET);
    }
    HAL_Delay(1);
    
    // 2. 逐线扫描
    for (int i = 0; i < 8; i++) {
        // 端口1第i根线设为高电平
        HAL_GPIO_WritePin(PORT1_GPIO, port1_pins[i], GPIO_PIN_SET);
        HAL_Delay(1);  // 等待信号稳定
        
        // 端口2检测哪根线为高
        mapping[i] = -1;  // 默认断线
        for (int j = 0; j < 8; j++) {
            if (HAL_GPIO_ReadPin(PORT2_GPIO, port2_pins[j]) == GPIO_PIN_SET) {
                mapping[i] = j;
                break;
            }
        }
        
        // 恢复低电平
        HAL_GPIO_WritePin(PORT1_GPIO, port1_pins[i], GPIO_PIN_RESET);
        HAL_Delay(1);
    }
    
    // 3. 判断线缆类型
    // 直连: 1-1, 2-2, 3-3, 4-4, 5-5, 6-6, 7-7, 8-8
    uint8_t is_straight = 1;
    for (int i = 0; i < 8; i++) {
        if (mapping[i] != i) { is_straight = 0; break; }
    }
    if (is_straight) return WIRE_STRAIGHT;
    
    // 交叉: 1-3, 2-6, 3-1, 4-4, 5-5, 6-2, 7-7, 8-8
    const int8_t cross_map[8] = {2, 5, 0, 3, 4, 1, 6, 7};
    uint8_t is_crossover = 1;
    for (int i = 0; i < 8; i++) {
        if (mapping[i] != cross_map[i]) { is_crossover = 0; break; }
    }
    if (is_crossover) return WIRE_CROSSOVER;
    
    return WIRE_FAULT;
}

/**
 * 检测UTP/SFTP类型
 * SFTP的RJ45外壳与屏蔽层导通
 * 检测方法：端口1外壳引脚施加信号，检测是否与内部线芯隔离
 */
typedef enum {
    CABLE_UTP,    // 非屏蔽
    CABLE_SFTP    // 双屏蔽
} CableType_t;

// 假设外壳连接到额外的GPIO引脚
#define SHELL_OUT_PIN   GPIO_PIN_8   // 端口1外壳
#define SHELL_OUT_GPIO  GPIOB
#define SHELL_IN_PIN    GPIO_PIN_8   // 端口2外壳
#define SHELL_IN_GPIO   GPIOC

CableType_t WireTest_CheckCableType(void)
{
    // 端口1外壳施加高电平
    HAL_GPIO_WritePin(SHELL_OUT_GPIO, SHELL_OUT_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    
    // 检测端口2外壳
    if (HAL_GPIO_ReadPin(SHELL_IN_GPIO, SHELL_IN_PIN) == GPIO_PIN_SET) {
        // 外壳导通 = SFTP
        HAL_GPIO_WritePin(SHELL_OUT_GPIO, SHELL_OUT_PIN, GPIO_PIN_RESET);
        return CABLE_SFTP;
    }
    
    HAL_GPIO_WritePin(SHELL_OUT_GPIO, SHELL_OUT_PIN, GPIO_PIN_RESET);
    return CABLE_UTP;
}

/**
 * 检测线对间短路（单端模式）
 * 在端口1逐对施加信号，检测是否有串扰到其他线对
 */
uint8_t WireTest_CheckShort(void)
{
    // 对每对线施加差分信号，检测其他线对是否有响应
    // 简化版：逐线施加高电平，检测是否有多根线同时为高
    
    for (int i = 0; i < 8; i++) {
        // 所有线先拉低
        for (int k = 0; k < 8; k++) {
            HAL_GPIO_WritePin(PORT1_GPIO, port1_pins[k], GPIO_PIN_RESET);
        }
        
        // 第i根线拉高
        HAL_GPIO_WritePin(PORT1_GPIO, port1_pins[i], GPIO_PIN_SET);
        HAL_Delay(1);
        
        // 检测其他线是否也变高（短路）
        for (int j = 0; j < 8; j++) {
            if (j == i) continue;
            if (HAL_GPIO_ReadPin(PORT1_GPIO, port1_pins[j]) == GPIO_PIN_SET) {
                // 线i和线j短路！
                return 1;  // 有短路
            }
        }
        
        HAL_GPIO_WritePin(PORT1_GPIO, port1_pins[i], GPIO_PIN_RESET);
    }
    
    return 0;  // 无短路
}
