#include "user.h"
#include "global.h"
#include "config.h"
#include "tim.h"
u8 key_up,key_down,key_old,key_value = 0;

// 读当前被按下的键（上拉输入，按下为低）。返回 1~4，无键=0。
u8 KEY_read(void)
{
	if(HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_8)  == 0) return 1;
	if(HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_12) == 0) return 2;
	if(HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_11) == 0) return 3;
	if(HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_9)  == 0) return 4;
	return 0;
}

// 按键扫描（8ms）：产生 key_down 事件 = 新按下沿，或长按后的自动连发。
// 动作交给 UI 层 UI_OnKey() 按界面分发（见 main.c）。
void KEY_Process(void)
{
	static uint32_t key_tick = 0;
	static uint32_t hold_tick = 0;
	static uint8_t  repeating = 0;
	if (uwTick - key_tick < KEY_SCAN_MS) return;
	key_tick = uwTick;

	u8 v = KEY_read();
	key_down = 0;
	if (v != 0) {
		if (v != key_old) {                 // 新按下：立即触发一次
			key_down  = v;
			hold_tick = uwTick;
			repeating = 0;
		} else {                            // 持续按住：到时间则自动连发
			uint32_t hold = uwTick - hold_tick;
			if (!repeating) {
				if (hold >= KEY_REPEAT_DELAY_MS) { repeating = 1; hold_tick = uwTick; key_down = v; }
			} else {
				if (hold >= KEY_REPEAT_RATE_MS) { hold_tick = uwTick; key_down = v; }
			}
		}
	}
	key_old = v;
}






