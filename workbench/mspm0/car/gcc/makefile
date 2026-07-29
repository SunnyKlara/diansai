MSPM0_SDK_INSTALL_DIR ?= C:/ti/mspm0-sdk

include $(MSPM0_SDK_INSTALL_DIR)/imports.mak

CC = "$(GCC_ARMCOMPILER)/bin/arm-none-eabi-gcc"
LNK = "$(GCC_ARMCOMPILER)/bin/arm-none-eabi-gcc"

SYSCONFIG_GUI_TOOL = $(dir $(SYSCONFIG_TOOL))sysconfig_gui$(suffix $(SYSCONFIG_TOOL))
SYSCFG_CMD_STUB = $(SYSCONFIG_TOOL) --compiler gcc --product $(MSPM0_SDK_INSTALL_DIR)/.metadata/product.json
SYSCFG_GUI_CMD_STUB = $(SYSCONFIG_GUI_TOOL) --compiler gcc --product $(MSPM0_SDK_INSTALL_DIR)/.metadata/product.json
SYSCFG_FILES := $(shell $(SYSCFG_CMD_STUB) --listGeneratedFiles --listReferencedFiles --output . ../car.syscfg)

SYSCFG_C_FILES = $(filter %.c,$(SYSCFG_FILES))
SYSCFG_H_FILES = $(filter %.h,$(SYSCFG_FILES))
SYSCFG_OPT_FILES = $(filter %.opt,$(SYSCFG_FILES))

# Enable verbose output by setting VERBOSE=1
V := @
ifeq ($(VERBOSE), 1)
  V :=
endif

OBJECTS = car.obj motor.obj gc9a01.obj uart_dbg.obj control.obj encoder.obj imu.obj attitude.obj nav.obj magnet.obj uart_frame.obj line.obj linesens.obj lineframe.obj servo.obj task.obj beep.obj disp_run.obj $(patsubst %.c,%.obj,$(notdir $(SYSCFG_C_FILES)))

NAME = car


DFLAGS += -gstrict-dwarf

CFLAGS += -I.. \
    -I. \
    $(addprefix @,$(SYSCFG_OPT_FILES)) \
    -O2 \
    @device.opt \
    "-I$(MSPM0_SDK_INSTALL_DIR)/source/third_party/CMSIS/Core/Include" \
    "-I$(MSPM0_SDK_INSTALL_DIR)/source" \
    -mcpu=cortex-m0plus \
    -march=armv6-m \
    -mthumb \
    -std=c99 \
    -mfloat-abi=soft \
    -ffunction-sections \
    -fdata-sections \
    -g \
    -Wall \
    "-I$(GCC_ARMCOMPILER)/arm-none-eabi/include/newlib-nano" \
    "-I$(GCC_ARMCOMPILER)/arm-none-eabi/include"


ifneq ($(CONFIG),RELEASE)
# Add debug flags by default (excluded only when CONFIG=RELEASE)
CFLAGS += $(DFLAGS)
endif

LFLAGS += "-L$(MSPM0_SDK_INSTALL_DIR)/source/ti/driverlib/lib/gcc/m0p/mspm0g1x0x_g3x0x" \
    -nostartfiles \
    -Tdevice.lds.genlibs \
    -l:driverlib.a \
    -Wl,-T,../gcc/device_linker.lds \
    "-Wl,-Map,$(NAME).map" \
    "-L$(MSPM0_SDK_INSTALL_DIR)/source" \
    -L.. \
    -march=armv6-m \
    -mthumb \
    -static \
    -Wl,--gc-sections \
    "-L$(GCC_ARMCOMPILER)/arm-none-eabi/lib/thumb/v6-m/nofp" \
    -lgcc \
    -lc \
    -lm \
    --specs=nano.specs \
    --specs=nosys.specs

all: $(NAME).out

.INTERMEDIATE: syscfg
$(SYSCFG_FILES): syscfg
	@ echo generation complete

syscfg: ../car.syscfg
	@ echo Generating configuration files...
	$(V) $(SYSCFG_CMD_STUB) --output $(@D) $<


# Helpful hint that the user needs to use a standalone SysConfig installation
$(SYSCONFIG_GUI_TOOL):
	$(error $(dir $(SYSCONFIG_TOOL)) does not contain the GUI framework \
        necessary to launch the SysConfig GUI.  Please set SYSCONFIG_TOOL \
        (in your SDK's imports.mak) to a standalone SysConfig installation \
        rather than one inside CCS)

syscfg-gui: ../car.syscfg $(SYSCONFIG_GUI_TOOL)
	@ echo Opening SysConfig GUI
	$(V) $(SYSCFG_GUI_CMD_STUB) $<


define C_RULE
$(basename $(notdir $(1))).obj: $(1) $(SYSCFG_H_FILES)
	@ echo Building $$@
	$(V) $(CC) $(CFLAGS) $$< -c -o $$@
endef

$(foreach c_file,$(SYSCFG_C_FILES),$(eval $(call C_RULE,$(c_file))))

# ../config.h 必须在依赖里! (2026-07-27 实测漏了这一条: 改完 config.h 跑 mingw32-make 直接
# "Nothing to be done", 二进制里还是旧参数 —— 而本工程铁律恰恰是"达标即回填 config.h 再烧",
#  漏依赖会让这条铁律静默失效: 文档/源码显示新值, 芯片跑的是旧值, 且没有任何报错提示。)
car.obj: ../car.c ../config.h ../gc9a01.h ../motor.h ../uart_dbg.h ../control.h ../encoder.h ../imu.h ../attitude.h ../nav.h ../magnet.h ../uart_frame.h ../vservo.h ../cmd_gate.h ../servo.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# nav.obj 依赖 config.h: 车级导航的全部参数(巡航速度/容差/增益/标定值)都在那儿, 漏了这条依赖
# 会出现"改了 config.h §7.5 却编不进去"的静默失效 —— 与 car.obj 那条同一个理由。
nav.obj: ../nav.c ../nav.h ../attitude.h ../config.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

magnet.obj: ../magnet.c ../magnet.h ../motor.h ../uart_dbg.h ../config.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# servo.obj 依赖 config.h: 中位/限幅/每度微秒数全在 §7.9, 漏了这条依赖就会出现
# "改完中位却没编进去、芯片还在用旧中位"的静默失效 —— 与 car.obj 那条同一个理由。
servo.obj: ../servo.c ../servo.h ../config.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# linesens.obj = 循迹模块串口链路(UART1=PB4/PB5)。依赖 SYSCFG_H_FILES 是因为它用
# LINE_UART_INST（由 car.syscfg 生成）—— 改了 .syscfg 必须重生成才编得过。
linesens.obj: ../linesens.c ../linesens.h ../lineframe.h ../config.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# lineframe.obj = 八路巡线模块的串口帧解析(纯算法层, 不依赖 HAL ⇒ 已 PC 单测 test_lineframe)
# task.obj = 第2项任务层(按键/计时/到达判定/安全停)。纯算法层, 不依赖 HAL ⇒ PC 单测 test_task
task.obj: ../task.c ../task.h ../config.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# beep.obj = 蜂鸣器非阻塞状态机。纯算法层 ⇒ PC 单测 test_beep
beep.obj: ../beep.c ../beep.h ../config.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# disp_run.obj = LCD RUN 页纯排版(出 6 行文本 + 变化位掩码), 不碰 GC9A01 ⇒ PC 单测 test_disp_run
disp_run.obj: ../disp_run.c ../disp_run.h ../config.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

lineframe.obj: ../lineframe.c ../lineframe.h ../config.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# uart_frame 是纯算法层(不依赖 HAL), 但**必须真的编进固件** —— 它在 2026-07-27 之前只有 .h、
# 不在这份 makefile 里, 于是"视觉串口链已就绪"是假象。进了 OBJECTS 才叫存在。
uart_frame.obj: ../uart_frame.c ../uart_frame.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

# line.obj 目前**没有任何调用者**(传感器还没接线, car.c 里还没有 m11) —— 仍然编进来是有意的:
# 编不过的代码等于不存在, 而"等硬件到手那天再发现它编不过"是最糟的时机。
# --gc-sections 会把没人引用的段丢掉, 所以固件不会为它变大。
line.obj: ../line.c ../line.h ../config.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

imu.obj: ../imu.c ../imu.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

attitude.obj: ../attitude.c ../attitude.h
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

motor.obj: ../motor.c ../motor.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

gc9a01.obj: ../gc9a01.c ../gc9a01.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

uart_dbg.obj: ../uart_dbg.c ../uart_dbg.h ../config.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

control.obj: ../control.c ../control.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

encoder.obj: ../encoder.c ../encoder.h $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) $< -c -o $@

$(NAME).out: $(OBJECTS)
	@ echo linking $@
	$(V) $(LNK)  $(OBJECTS)  $(LFLAGS) -o $(NAME).out

clean:
	@ echo Cleaning...
	$(V) $(RM) $(OBJECTS) > $(DEVNULL) 2>&1
	$(V) $(RM) $(NAME).out > $(DEVNULL) 2>&1
	$(V) $(RM) $(NAME).map > $(DEVNULL) 2>&1
	$(V) $(RM) $(call SLASH_FIXUP,$(SYSCFG_FILES)) > $(DEVNULL) 2>&1
