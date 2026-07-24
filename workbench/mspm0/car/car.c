/*
 * 天猛星小车起步工程 —— 双电机 PWM 演示 + GC9A01 仪表盘。
 * 上电: 延时稳一下(修复烧后黑屏) -> 背光+屏初始化 -> 电机PWM(初始0,安全) ->
 *       循环演示 前进/停/转弯/停/后退/停, 屏显当前动作+占空比, PB22 心跳。
 *
 * 电机脚(DRV8231): M1 IN1=PA8 IN2=PA9 | M2 IN1=PB12 IN2=PB13   (TIMA0 4通道)
 * 显示脚(板载H8): SCL=PB9 SDA=PB8 RES=PB10 DC=PB11 CS=PB14 BLK=PB26
 * ⚠ 需接好 DRV8231 模块 + 电机(H1:+12V/IN1/+3.3V/IN2, H2:GND/ADC/OUT1/OUT2) 才能看到转动;
 *   没接电机时,屏上照样能看到动作切换(PWM在发,只是没负载)。
 */
#include "ti_msp_dl_config.h"
#include "gc9a01.h"
#include "motor.h"
#include "uart_dbg.h"

#define CPUCLK_HZ 32000000UL
static void delay_ms(uint32_t ms) { while (ms--) delay_cycles(CPUCLK_HZ / 1000UL); }

/* 把带符号占空(-100..100)写成 "+40"/"-40"/"+0" 形式,返回长度 */
static int put_signed(char *b, int v)
{
    int n = 0, a = (v < 0) ? -v : v;
    b[n++] = (v < 0) ? '-' : '+';
    if (a >= 100)      { b[n++] = '1'; b[n++] = '0'; b[n++] = '0'; }
    else if (a >= 10)  { b[n++] = (char)('0' + a / 10); b[n++] = (char)('0' + a % 10); }
    else               { b[n++] = (char)('0' + a); }
    return n;
}

/* 组 "M1:+40 M2:-40" */
static void build_duty_line(char *buf, int m1, int m2)
{
    int n = 0;
    buf[n++] = 'M'; buf[n++] = '1'; buf[n++] = ':';
    n += put_signed(buf + n, m1);
    buf[n++] = ' ';
    buf[n++] = 'M'; buf[n++] = '2'; buf[n++] = ':';
    n += put_signed(buf + n, m2);
    buf[n] = 0;
}

/* 演示动作表: 名称, M1占空, M2占空, 持续ms */
typedef struct { const char *name; int16_t m1; int16_t m2; uint16_t ms; } step_t;
static const step_t steps[] = {
    { "FORWARD",  40,  40, 1500 },
    { "STOP",      0,   0,  800 },
    { "TURN",     40, -40, 1200 },
    { "STOP",      0,   0,  800 },
    { "REVERSE", -40, -40, 1500 },
    { "STOP",      0,   0,  800 },
};
#define NUM_STEPS (sizeof(steps) / sizeof(steps[0]))

int main(void)
{
    SYSCFG_DL_init();
    delay_ms(200);                 /* 上电稳定 —— 让复位后显示初始化吃到干净冷启动 */

    GC9A01_Backlight(1);
    GC9A01_Init();
    motor_init();                  /* PWM 起,占空=0,电机不动(安全) */

    GC9A01_FillScreen(LCD_BLACK);
    GC9A01_DrawStringCentered(28, "CAR DEMO", LCD_GREEN, LCD_BLACK, 2);

    uart_dbg_puts("\n[car] boot ok | UART0 @115200 8N1 | demo loop start\n");

    uint32_t si = 0, nstep = 0;
    char line[24];
    while (1) {
        const step_t *s = &steps[si];

        motor_set(MOTOR_M1, s->m1);
        motor_set(MOTOR_M2, s->m2);

        /* 动作名(大字, 居中) */
        GC9A01_FillRect(10, 84, 220, 32, LCD_BLACK);
        GC9A01_DrawStringCentered(90, s->name, LCD_WHITE, LCD_BLACK, 3);

        /* 占空比行 */
        GC9A01_FillRect(10, 148, 220, 18, LCD_BLACK);
        build_duty_line(line, s->m1, s->m2);
        GC9A01_DrawStringCentered(150, line, LCD_GREEN, LCD_BLACK, 2);

        /* 本动作持续期间, PB22 心跳闪 */
        uint32_t t = 0;
        while (t < s->ms) {
            DL_GPIO_togglePins(GPIO_LCD_PORT, GPIO_LCD_STATUS_LED_PIN);
            delay_ms(150);
            t += 150;
        }

        /* 动作末尾(电流已稳)读两路 DRV8231 电流, 连同状态打一行 log */
        uint16_t i1 = 0, i2 = 0;
        motor_read_current_raw(&i1, &i2);
        uart_dbg_puts("[car] #");
        uart_dbg_put_int((int32_t)nstep++);
        uart_dbg_putc(' ');
        uart_dbg_puts(s->name);
        uart_dbg_puts(" M1=");   uart_dbg_put_int(s->m1);
        uart_dbg_puts(" M2=");   uart_dbg_put_int(s->m2);
        uart_dbg_puts(" | I1="); uart_dbg_put_int(motor_current_ma(i1));
        uart_dbg_puts("mA(r");   uart_dbg_put_int(i1); uart_dbg_putc(')');
        uart_dbg_puts(" I2=");   uart_dbg_put_int(motor_current_ma(i2));
        uart_dbg_puts("mA(r");   uart_dbg_put_int(i2); uart_dbg_putc(')');
        uart_dbg_puts("\n");

        si = (si + 1) % NUM_STEPS;
    }
}
