// -*- coding: utf-8 -*-
// microSD（SDMMC slot0, 4-line）挂载 + 顺序读写测速。
#include "sdcard.h"

#include <dirent.h>          // opendir/readdir —— sdcard_list_root()
#include <stdio.h>
#include <stdlib.h>          // calloc/free —— SDMMC 身份识别用
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>          // fsync()

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_cpu.h"         // esp_cpu_get_cycle_count() —— RC 上升时间用
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"            // VolToPart[] —— 强制挂指定 MBR 分区
#include "sdmmc_cmd.h"
#include "soc/soc_caps.h"
#if SOC_SDMMC_IO_POWER_EXTERNAL
// ESP32-P4 的 SDMMC IO 供电域是"外部供电"（soc_caps.h: SOC_SDMMC_IO_POWER_EXTERNAL=1），
// 必须显式用片上 LDO 给它上电，否则那几个 pad 推不出有效电平。见下面 s_pwr_ctrl 处。
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

static const char *TAG = "sdcard";
// 用宏而非变量：下面要和字面量做字符串拼接（MOUNT_POINT "/_bench.bin"）
#define MOUNT_POINT "/sdcard"

// P4 SDMMC slot0 IOMUX 固定脚（见 .h 注释）
#define PIN_CLK 43
#define PIN_CMD 44
#define PIN_D0  39
#define PIN_D1  40
#define PIN_D2  41
#define PIN_D3  42

static sdmmc_card_t *s_card;
static sdcard_info_t s_info;
static sdcard_bench_t s_bench;
#if SOC_SDMMC_IO_POWER_EXTERNAL
static sd_pwr_ctrl_handle_t s_pwr_ctrl;   // LDO_VO4 → SDMMC IO 供电句柄
#endif

const sdcard_bench_t *sdcard_bench_result(void) { return &s_bench; }

esp_err_t sdcard_mount(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    // 20 MHz 实测顺序写只有 499 KB/s，而图传码率约 460 KB/s（§10.12）—— 录像几乎没余量，
    // 故提到 40 MHz（上游 sila-p4c5/sd_scanner.c 用的也是 SDMMC_FREQ_HIGHSPEED）。
    // 卡或走线不支持时 IDF 会在协商阶段自己回退，不会因此挂载失败。
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.command_timeout_ms = 5000;      // 与上游一致，给慢卡留够时间
    // ⛔ 2026-07-30 全部推翻：原注释（"SDMMC 与 C5 无线不能共存 ⇒ 必须走 SPI"）是我自己
    //    编的因果，理由见 README §10.13 ⑧。真正的根因在下面这段 LDO：**ESP32-P4 的 SDMMC
    //    IO 供电域必须显式上电**，我此前从未做过这一步，所以 SDMMC/SPI/bitbang 三条路
    //    全都推不出有效电平 —— 与 C5 无关（07-27 那份无 C5 的日志里失败在同一行）。
    //    原始出处：上游 gitee.com/Ergou-/esp32-p4-c5-aibox 分支 xiaozhi-p4c5,
    //    main/boards/sila-p4c5/sd_scanner.c（引脚定义与本文件逐个一致）。

#if SOC_SDMMC_IO_POWER_EXTERNAL
    // 🔑 这就是缺失的那一步。P4 把 SDMMC 的 IO 电源交给片上 LDO_VO4 管，默认不供电。
    // 不做这一步的症状极具误导性：GPIO 自回读正常（内部逻辑自洽）、外部上拉扫描也正常
    // （那是板载 3.3V 经 51kΩ 干的，与 IO 域无关），但 pad 上推不出真实电平 ⇒ 卡收不到
    // 任何命令 ⇒ MISO 被上拉钉在高 = 全 0xFF / send_op_cond 超时。整晚就栽在这里。
    // 通道 4 的依据：IDF 头文件原话 "set to 4 is the LDO_VO4 is connected to power the
    // SDMMC IO"；本工程 sdkconfig 里 VO1=Flash、VO2=PSRAM，**通道 4 空闲**，不冲突。
    if (s_pwr_ctrl == NULL) {
        const sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = 4 };
        const esp_err_t perr = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_pwr_ctrl);
        if (perr != ESP_OK) {
            ESP_LOGE(TAG, "SDMMC IO 供电(LDO_VO4)创建失败: %s —— 卡必然初始化不了",
                     esp_err_to_name(perr));
            s_info.mounted = false;
            return perr;
        }
        ESP_LOGI(TAG, "SDMMC IO 供电已就绪 (on-chip LDO_VO4)");
    }
    host.pwr_ctrl_handle = s_pwr_ctrl;
#else
    ESP_LOGW(TAG, "本目标无 SOC_SDMMC_IO_POWER_EXTERNAL —— 若 P4 上看到这行，说明 soc_caps 变了");
#endif

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = PIN_CLK;
    slot.cmd = PIN_CMD;
    slot.d0  = PIN_D0;
    slot.d1  = PIN_D1;
    slot.d2  = PIN_D2;
    slot.d3  = PIN_D3;
    // 板上未接 CD/WP 信号；内部上拉当兜底(正式设计应在板上加 10k 外部上拉)
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,   // 绝不动用户卡里的数据
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    // 🔑 强制挂第 N 个 MBR 分区。这张卡有三个 FAT32 分区（实测 MBR）：
    //      p1 = 10 MB（GC2093 摄像头配置，已用 7 MB，只剩 2 MB）
    //      p2 = 511 MB
    //      p3 = 58613 MB  ← 录像要的就是它
    // FatFs 在 pt=0（自动）时只挑**第一个** FAT 分区，所以一直挂到那个 10 MB 上，
    // 表现为"59638 MB 的卡只有 9 MB 可用"，还害我把写失败误当驱动问题。
    // IDF 已经开了 FF_MULTI_PARTITION=1，且 VolToPart 是非 static 全局（ff.h 有 extern），
    // 于是把 pt 改成分区号即可 —— **不需要格式化，不动卡上任何现有数据**。
#if P4_SD_PARTITION > 0
    {
        // esp_vfs_fat_sdmmc_mount 内部自己 ff_diskio_get_drive()，拿不到它的 pdrv；
        // 本工程没有别的 FATFS 使用者（NVS 不是 FATFS），所以必然是 0。
        // 万一将来多了一个 FAT 卷，这里会挂错卷 —— 判据是日志里的容量对不上分区表。
        VolToPart[0].pd = 0;
        VolToPart[0].pt = P4_SD_PARTITION;
        ESP_LOGI(TAG, "强制挂载 MBR 分区 p%d（pt=0 会挑到只有 10MB 的 p1）", P4_SD_PARTITION);
    }
#endif

    ESP_LOGI(TAG, "挂载 SDMMC slot0 4-line (CLK=%d CMD=%d D0..D3=%d,%d,%d,%d)...",
             PIN_CLK, PIN_CMD, PIN_D0, PIN_D1, PIN_D2, PIN_D3);
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "挂载失败：卡里没有可识别的 FAT 文件系统"
                          "（本驱动刻意不自动格式化，请在电脑上格成 FAT32 再试）");
        } else {
            ESP_LOGE(TAG, "初始化卡失败: %s —— 最常见就是**没插卡**；"
                          "其次是卡座接触/卡不支持 4-line", esp_err_to_name(err));
        }
        s_info.mounted = false;
        return err;
    }

    s_info.mounted = true;
    snprintf(s_info.name, sizeof(s_info.name), "%s", s_card->cid.name);
    s_info.size_mb = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
    s_info.speed_khz = s_card->max_freq_khz;
    s_info.bus_width = slot.width;
    s_info.is_sdio = s_card->is_sdio;
    s_info.is_mmc = s_card->is_mmc;

    ESP_LOGI(TAG, "挂载成功: %s  %llu MB  %d kHz  %d-line",
             s_info.name, s_info.size_mb, s_info.speed_khz, s_info.bus_width);
    sdmmc_card_print_info(stdout, s_card);

    // 剩余空间必须打出来：卡容量(59638MB) 与可用空间是两件事，2026-07-30 就因为只看容量
    // 而把"写到 2.4MB 失败"误当成驱动问题。录像的"卡满即停"也要用这个数。
    uint64_t tot = 0, fre = 0;
    if (esp_vfs_fat_info(MOUNT_POINT, &tot, &fre) == ESP_OK) {
        s_info.total_mb = tot / (1024 * 1024);
        s_info.free_mb  = fre / (1024 * 1024);
        ESP_LOGI(TAG, "文件系统: 总 %llu MB / 可用 %llu MB", s_info.total_mb, s_info.free_mb);
        if (s_info.free_mb < 64) {
            ESP_LOGW(TAG, "⚠️ 可用空间只剩 %llu MB —— 录像会很快写满，先清一下卡",
                     s_info.free_mb);
        }
    } else {
        ESP_LOGW(TAG, "esp_vfs_fat_info 失败，拿不到可用空间");
    }
    return ESP_OK;
}

// ------------------------------------------------------------------
// SPI 模式挂载 —— 与 C5 无线共存的唯一可行路径（2026-07-29）
//
// 为什么必须走 SPI：见 sdcard_mount() 顶部那段实测留档 —— slot0/slot1 共用同一颗
// SDMMC 控制器与时钟，C5 的 SDIO 先占住它之后，SD 侧拿不到自己需要的时钟，两轮
// 单变量实验分别死在 read_sectors_dma 和 send_op_cond。SPI 走的是完全独立的
// GPSPI 外设，不碰 SDMMC，所以冲突在构造上不存在。
//
// 零硬件改动：SD 卡原生支持 SPI 模式，把卡座那几个脚按 SD-SPI 标准映射即可
// （P4 的 GPIO matrix 允许任意脚接到 GPSPI）：
//     SCLK = CLK(43)   MOSI = CMD(44)   MISO = D0(39)   CS = D3(42)
// 注意 D1/D2(40/41) 在 SPI 模式下不用，保持悬空即可。
//
// 速率预期：SPI 模式典型 20~40 MHz、实际吞吐约 1~2 MB/s，而图传码率实测仅约
// 460 KB/s（57 fps × 约 8 KB/帧）⇒ 余量够。**但吞吐必须真机测过才算数。**
// 原始 SPI 探针：绕过 sdspi 驱动，手发一条 CMD0(GO_IDLE_STATE) 并把 MISO 上收到的
// 原始字节打出来。目的是把「卡完全不应答」和「应答了但协议/驱动层出问题」一刀分开 ——
// 实测 sdspi 挂载只花 21 ms 就 TIMEOUT，怀疑 MISO 上一个字节都没有。
// 判读（全 ASCII 打印，因为本板 console 不是 UTF-8，中文会变 ????）：
//   R1=0x01            -> 卡在 SPI 模式且应答正常 => 问题在驱动/后续流程，不是信号
//   全 0xFF            -> 卡完全没驱动 MISO（上拉把线拉高）=> 卡/接线/CS/上拉 问题
//   全 0x00            -> MISO 被钉在低 => 线接错或被别的外设占着
// 注意：CS 由 spi_device 自动管理，故 CMD0 前的 74 个 dummy clock 用一次空传输近似给出。
static void sdcard_spi_raw_probe(void)
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 400 * 1000,   // 卡上电后只认 <=400kHz
        .mode = 0,
        .spics_io_num = PIN_D3,
        .queue_size = 1,
    };
    spi_device_handle_t dev = NULL;
    if (spi_bus_add_device(SDCARD_SPI_HOST, &devcfg, &dev) != ESP_OK) {
        ESP_LOGE(TAG, "RAWPROBE spi_bus_add_device failed");
        return;
    }
    // 1) 先给一串 dummy clock，让卡完成上电同步
    uint8_t dummy_tx[10];
    uint8_t dummy_rx[10] = {0};
    memset(dummy_tx, 0xFF, sizeof(dummy_tx));
    spi_transaction_t t1 = {
        .length = sizeof(dummy_tx) * 8,
        .tx_buffer = dummy_tx,
        .rx_buffer = dummy_rx,
    };
    spi_device_transmit(dev, &t1);

    // 2) CMD0 = 0x40 + 32bit arg(0) + CRC7 0x95，随后连读 8 字节等 R1
    uint8_t tx[14];
    uint8_t rx[14] = {0};
    memset(tx, 0xFF, sizeof(tx));
    tx[0] = 0x40; tx[1] = 0; tx[2] = 0; tx[3] = 0; tx[4] = 0; tx[5] = 0x95;
    spi_transaction_t t2 = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    const esp_err_t err = spi_device_transmit(dev, &t2);
    spi_bus_remove_device(dev);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RAWPROBE transmit failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "RAWPROBE dummy_rx: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             dummy_rx[0], dummy_rx[1], dummy_rx[2], dummy_rx[3], dummy_rx[4],
             dummy_rx[5], dummy_rx[6], dummy_rx[7], dummy_rx[8], dummy_rx[9]);
    ESP_LOGW(TAG, "RAWPROBE cmd0_rx : %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6],
             rx[7], rx[8], rx[9], rx[10], rx[11], rx[12], rx[13]);
    int r1 = -1;
    for (int i = 6; i < (int)sizeof(rx); ++i) {
        if (rx[i] != 0xFF) { r1 = rx[i]; break; }
    }
    if (r1 < 0) {
        ESP_LOGE(TAG, "RAWPROBE VERDICT: no response on MISO (all 0xFF) "
                      "=> suspect card / socket / CS / missing pullup, NOT the driver");
    } else if (r1 == 0x01) {
        ESP_LOGW(TAG, "RAWPROBE VERDICT: R1=0x01 card IS in SPI idle state "
                      "=> signals fine, problem is further up the stack");
    } else {
        ESP_LOGW(TAG, "RAWPROBE VERDICT: R1=0x%02X (unexpected but card DOES drive MISO)", r1);
    }
}

// ------------------------------------------------------------------
// 纯 GPIO bitbang 的 SD-SPI 上电探针（2026-07-29）
//
// 为什么还要写一个：上面 sdcard_spi_raw_probe() 有个**致命缺陷** —— `spi_device` 会
// 自动拉低 CS，所以 SD 规范要求的「CS 保持高电平时先给 >=74 个时钟」根本没做到。
// 于是"MISO 全 FF"可能只是探针不合规，而非硬件不通。本函数完全不碰 GPSPI 外设，
// 手工按规范时序翻引脚，把「卡/引脚」与「GPSPI 外设路由」一刀切开：
//   R1=0x01  -> 卡和引脚都好 => 问题在 GPSPI 路由（换 host / 调 IOMUX 还有救）
//   全 0xFF  -> 这几个脚在非 SDMMC 外设下确实不通（或卡没电/CS 没接）
// 只碰 39/42/43/44，绝不动 C5 SDIO 的 14-19。速度约 200 kHz，只做判读、不做数据传输。
#define BB_DELAY_US 2

static inline void bb_clk(gpio_num_t clk, int level)
{
    gpio_set_level(clk, level);
    esp_rom_delay_us(BB_DELAY_US);
}

// 发 8 位、同时采回 8 位（SPI mode0：上升沿采样）
static uint8_t bb_xfer(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 7; i >= 0; --i) {
        gpio_set_level((gpio_num_t)PIN_CMD, (out >> i) & 1);   // MOSI
        bb_clk((gpio_num_t)PIN_CLK, 0);
        bb_clk((gpio_num_t)PIN_CLK, 1);
        in = (uint8_t)((in << 1) | (gpio_get_level((gpio_num_t)PIN_D0) & 1));  // MISO
    }
    return in;
}

static void sdcard_spi_bitbang_probe(void)
{
    const gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_CLK) | (1ULL << PIN_CMD) | (1ULL << PIN_D3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << PIN_D0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,     // SPI 模式 MISO 必须上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&out_cfg) != ESP_OK || gpio_config(&in_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "BITBANG gpio_config failed");
        return;
    }

    // 1) 规范要求：CS 高 + MOSI 高，给 >=74 个时钟让卡完成上电同步（这里给 80）
    gpio_set_level((gpio_num_t)PIN_D3, 1);
    gpio_set_level((gpio_num_t)PIN_CMD, 1);
    bb_clk((gpio_num_t)PIN_CLK, 1);
    for (int i = 0; i < 10; ++i) {
        (void)bb_xfer(0xFF);
    }

    // 2) CS 拉低，发 CMD0(GO_IDLE_STATE)，随后读 R1
    gpio_set_level((gpio_num_t)PIN_D3, 0);
    esp_rom_delay_us(10);
    static const uint8_t cmd0[6] = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x95 };
    for (int i = 0; i < 6; ++i) {
        (void)bb_xfer(cmd0[i]);
    }
    uint8_t r[10];
    for (int i = 0; i < 10; ++i) {
        r[i] = bb_xfer(0xFF);
    }
    gpio_set_level((gpio_num_t)PIN_D3, 1);
    (void)bb_xfer(0xFF);

    ESP_LOGW(TAG, "BITBANG cmd0_rx: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9]);
    int r1 = -1;
    for (int i = 0; i < 10; ++i) {
        if (r[i] != 0xFF) { r1 = r[i]; break; }
    }
    if (r1 < 0) {
        ESP_LOGE(TAG, "BITBANG VERDICT: still all 0xFF -> these pads really do not talk "
                      "outside the SDMMC peripheral (or card unpowered / CS not wired)");
    } else if (r1 == 0x01) {
        ESP_LOGW(TAG, "BITBANG VERDICT: R1=0x01 -> CARD AND PADS ARE FINE. "
                      "The failure is GPSPI routing, keep fixing software");
    } else {
        ESP_LOGW(TAG, "BITBANG VERDICT: R1=0x%02X -> card DOES drive MISO", r1);
    }
}

// 找出哪些引脚挂着外部上拉 —— 用来客观定位 SD 卡座到底接在哪几个脚。
//
// 依据（来自原理图 MK-MicroSD(P4) 页）：SD_D0/D1/D2/D3/CMD/CLK 六条线**都有 51 kΩ 上拉到
// +3.3V**（R12、R20~R24）。而 IO39~IO54 这一组在本固件里没有任何用途。于是：
//   把某脚驱动为低 -> 切成"无内部上下拉的输入" -> 若它被快速拉回 1，说明有外部上拉
//   （51k + 引脚电容，回升是微秒级）；若长时间停在 0，则该脚悬空、不是 SD 线。
// 只扫 39..54，绝不碰 C5 SDIO(14-19)/屏(26,30)/触摸(27,28,29)/串口(37,38)/USB/IMU。
static void sdcard_scan_external_pullups(void)
{
    char line[256];
    int n = 0;
    n += snprintf(line + n, sizeof(line) - n, "PULLUPSCAN pins_with_ext_pullup:");
    for (int pin = 39; pin <= 54; ++pin) {
        const gpio_config_t o = {
            .pin_bit_mask = (1ULL << pin), .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&o) != ESP_OK) { continue; }
        gpio_set_level((gpio_num_t)pin, 0);
        esp_rom_delay_us(50);
        const gpio_config_t i = {
            .pin_bit_mask = (1ULL << pin), .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&i);
        esp_rom_delay_us(200);          // 51k 上拉足够在这段时间内把线拉高
        const int v = gpio_get_level((gpio_num_t)pin);
        if (v) {
            n += snprintf(line + n, sizeof(line) - n, " %d", pin);
        }
        gpio_reset_pin((gpio_num_t)pin);
        if (n > (int)sizeof(line) - 8) { break; }
    }
    ESP_LOGW(TAG, "%s", line);
    ESP_LOGW(TAG, "PULLUPSCAN note: expect exactly 6 pins = SD_CLK/CMD/D0/D1/D2/D3 "
                  "(schematic has 51k pullups on all six)");
}

// ============================================================================
// 引脚级物理层诊断（2026-07-29 新增，README §10.13 ⑥）
//
// 为什么需要：此前 5 轮"全 FF"的证据，全部压在两个**从未验证**的前提上 ——
//   A. 我真的在驱动那 6 个引脚（若 CLK 从未翻转 / CS 从未拉低，全 FF 是必然结果）
//   B. 卡与卡座之间真的有电气接触
// 下面两个函数分别验这两条，都**不依赖 SD 协议、不依赖时序、不依赖卡是否正常**。
// 前置安全性由原理图核对保证：这 6 个脚只连卡座（page 9 MK-MicroSD(P4)），
// 无串阻、无电平转换、无其它负载，VDD 直连 +3.3V 无使能开关 ⇒ 驱动它们不会打架。
// ============================================================================

// —— A. 输出自回读：证明"我真的能驱动这个脚" ——
//
// ⚠️ 关键细节：必须用 GPIO_MODE_INPUT_OUTPUT。GPIO_MODE_OUTPUT 会关掉输入通路，
//    gpio_get_level() 读不到 pad 真实电平，测出来的"失败"是假的 —— 与 ⑤ 里
//    "探针放在 spi_bus_initialize 之后"属同一类自伤，别再踩。
// 判据：拉低必须读回 0、拉高必须读回 1。任一脚不跟随 ⇒ 该脚没被我驱动
//      （被外设占用 / 驱动失效），此前所有涉及它的 FF 证据对该脚作废。
static bool sdcard_pad_drive_selftest(void)
{
    static const int      pins[6]  = { PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_CLK, PIN_CMD };
    static const char    *names[6] = { "D0/39", "D1/40", "D2/41", "D3/42", "CLK/43", "CMD/44" };
    char line[256];
    int  n = 0, bad = 0;
    n += snprintf(line + n, sizeof(line) - n, "PADDRIVE");
    for (int k = 0; k < 6; ++k) {
        const gpio_config_t io = {
            .pin_bit_mask = (1ULL << pins[k]),
            .mode = GPIO_MODE_INPUT_OUTPUT,          // 见上：必须可回读
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&io) != ESP_OK) {
            n += snprintf(line + n, sizeof(line) - n, " %s=CFGERR", names[k]);
            ++bad;
            continue;
        }
        gpio_set_level((gpio_num_t)pins[k], 0);
        esp_rom_delay_us(20);
        const int lo = gpio_get_level((gpio_num_t)pins[k]);
        gpio_set_level((gpio_num_t)pins[k], 1);
        esp_rom_delay_us(20);
        const int hi = gpio_get_level((gpio_num_t)pins[k]);
        const bool ok = (lo == 0) && (hi == 1);
        if (!ok) { ++bad; }
        n += snprintf(line + n, sizeof(line) - n, " %s=%d%d%s",
                      names[k], lo, hi, ok ? "" : "!");
        gpio_reset_pin((gpio_num_t)pins[k]);
    }
    ESP_LOGW(TAG, "%s  (want every pin 01)", line);
    if (bad) {
        ESP_LOGE(TAG, "PADDRIVE VERDICT: %d pin(s) do NOT follow -> I was never driving "
                      "them; every earlier all-FF result involving those pins is VOID", bad);
    } else {
        ESP_LOGW(TAG, "PADDRIVE VERDICT: all 6 pads follow -> premise A holds, "
                      "the earlier all-FF evidence stands");
    }
    return bad == 0;
}

// —— B. RC 上升时间：测线上等效上拉电阻，用来判卡到底有没有接触 ——
//
// 机理：先把脚拉低放电，再切成"输入 + 关掉内部上下拉"，此时只有外部上拉给线充电，
//   上升时间 t ≈ R_eff × C_line。
//     · 不插卡：R_eff = 板上 51 kΩ（原理图 R12/R20~R24）
//     · 插卡：SD 卡在 CD/DAT3（=IO42）上带**卡内约 50 kΩ 上拉**（上电即使能，ACMD42 可关），
//       与板上 51 kΩ 并联 ⇒ R_eff ≈ 25 kΩ ⇒ t 掉到约一半。其余 5 脚卡内没有这个上拉。
//   ⇒ 判据 = t(D3/42) / t(D2/41)：无卡时 ≈1.0，有卡时应 ≈0.5。
//     取比值的好处：走线寄生电容与卡的输入电容在比值里**被约掉**，不必知道 C 是多少，
//     也不受 CPU 频率刻度影响。比值仍 ≈1.0 ⇒ 卡与卡座无电气接触。
// ⚠️ 卡内那个电阻是**上拉不是下拉**（极易记反）。它和板上 51 kΩ 都通到 3.3 V，
//    所以"读电平高低"插卡前后恒为高、做不了卡检测 —— 必须测时间，这是本函数存在的理由。
// ⚠️ 多轮取**最小值**而不是平均：中断只会让轮询变慢、不会变快，min 天然抗中断干扰。
static uint32_t sdcard_pad_rise_cycles(int pin, int rounds)
{
    const gpio_config_t drv = {
        .pin_bit_mask = (1ULL << pin), .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t inp = {
        .pin_bit_mask = (1ULL << pin), .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    uint32_t best = UINT32_MAX;
    for (int k = 0; k < rounds; ++k) {
        if (gpio_config(&drv) != ESP_OK) { return 0; }
        gpio_set_level((gpio_num_t)pin, 0);
        esp_rom_delay_us(50);                 // 充分放电
        if (gpio_config(&inp) != ESP_OK) { return 0; }
        const uint32_t t0 = esp_cpu_get_cycle_count();
        uint32_t dt = 0;
        while (gpio_get_level((gpio_num_t)pin) == 0) {
            dt = esp_cpu_get_cycle_count() - t0;
            if (dt > 400000u) { break; }      // ~1 ms：判为没有上拉/悬空
        }
        if (dt == 0) { dt = esp_cpu_get_cycle_count() - t0; }  // 一次迭代内就到高
        if (dt < best) { best = dt; }
    }
    gpio_reset_pin((gpio_num_t)pin);
    return (best == UINT32_MAX) ? 0 : best;
}

// 分辨率自检：同一个轮询循环空转一次要多少 cycle。
// 必须打出来 —— 若上升时间只有几个 cycle 量级，说明分辨率不足，比值判据无意义，
// 那时要改用外部示波器而不是继续相信这组数（"让数据自己证明分辨率够不够"）。
static uint32_t sdcard_poll_overhead_cycles(int pin)
{
    const gpio_config_t inp = {
        .pin_bit_mask = (1ULL << pin), .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&inp) != ESP_OK) { return 0; }
    esp_rom_delay_us(200);                    // 让外部上拉把线充到高，循环立刻退出
    const uint32_t t0 = esp_cpu_get_cycle_count();
    for (int k = 0; k < 64; ++k) { (void)gpio_get_level((gpio_num_t)pin); }
    const uint32_t d = esp_cpu_get_cycle_count() - t0;
    gpio_reset_pin((gpio_num_t)pin);
    return d / 64u;
}

static void sdcard_pad_rc_report(bool first)
{
    static const int   pins[6]  = { PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_CLK, PIN_CMD };
    static const char *names[6] = { "D0/39", "D1/40", "D2/41", "D3/42", "CLK/43", "CMD/44" };
    const uint32_t ovh = sdcard_poll_overhead_cycles(PIN_D2);

    uint32_t t[6];
    char line[256];
    int  n = 0;
    n += snprintf(line + n, sizeof(line) - n, "PADRC cycles");
    for (int k = 0; k < 6; ++k) {
        t[k] = sdcard_pad_rise_cycles(pins[k], 64);
        n += snprintf(line + n, sizeof(line) - n, " %s=%lu", names[k], (unsigned long)t[k]);
    }
    ESP_LOGW(TAG, "%s  (min of 64, poll_overhead=%lu cyc @%d MHz nominal)",
             line, (unsigned long)ovh, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    // 判据：D3(42) 上有卡内 ~50k 上拉时，其上升时间应约为 D2(41) 的一半
    const uint32_t t_d3 = t[3], t_d2 = t[2];
    if (t_d2 == 0 || t_d3 == 0) {
        ESP_LOGE(TAG, "PADRC VERDICT: INCONCLUSIVE - a pin never rose (no pullup?)");
        return;
    }
    const int ratio_x100 = (int)((t_d3 * 100ULL) / t_d2);
    if (t_d3 < ovh * 4 || t_d2 < ovh * 4) {
        ESP_LOGE(TAG, "PADRC VERDICT: INCONCLUSIVE - rise time (%lu/%lu cyc) too close to "
                      "poll overhead (%lu) to resolve 2x; need a scope",
                 (unsigned long)t_d3, (unsigned long)t_d2, (unsigned long)ovh);
    } else if (ratio_x100 <= 70) {
        ESP_LOGW(TAG, "PADRC VERDICT: ratio D3/D2 = %d%% (<=70) -> card IS in contact "
                      "(its internal ~50k pullup on CD/DAT3 is loading the line). "
                      "The all-FF root cause is NOT a dead socket", ratio_x100);
    } else {
        ESP_LOGE(TAG, "PADRC VERDICT: ratio D3/D2 = %d%% (~100 expected with NO card) -> "
                      "no card loading detected: card not seated or socket not connected",
                 ratio_x100);
    }
    if (first) {
        ESP_LOGW(TAG, "PADRC howto: run once with the card OUT (baseline, expect ~100%%), "
                      "then again with the card IN (expect ~50%%). The change is the "
                      "judgement, not the absolute number");
    }
}

// —— C. SDMMC 身份识别：把"card init 成功"落实成"到底是谁在应答" ——
//
// 为什么必须做：之前那轮 SDMMC 实验我记成"card init 成功、CID/CSD 读到"，回查原始日志
// (.tmp_pdf/esp32p4/sdtest_boot2.txt) **根本没有任何 CID/CSD 输出** —— "读到 CID/CSD"
// 是我自己加的修饰（本轮第 3 个同类自伤，都是"把推断写成实测"）。日志真正能支撑的只有：
// esp_vfs_fat_sdmmc_mount **没有在 card init 阶段报错**，一路走到 read_sectors 才失败。
// 而 card init 要走完 CMD0/CMD8/ACMD41/CMD2/CMD3/CMD9，全靠 CMD 线双向应答 ——
// 卡座若完全不通，这一串走不完。⇒ 必须区分两种解释，判据 = 打印应答者的身份：
//     真卡 => is_sdio=0、有厂商/OEM/序列号、容量与用户手上的卡吻合
//     C5   => is_sdio=1，或容量/CID 明显异常
// ⚠️ 副作用：slot0 与 slot1 共用时钟分频器，本函数会动它 ⇒ 可能短暂打断 C5 无线/图传。
//    只在诊断固件里调用；重启即恢复，之前两轮 SDMMC 实验做过同样的事，无永久影响。
// ⚠️ 必须放在**所有 GPIO 级探针之后** —— sdmmc_host_init_slot() 会把这几个脚交给 SDMMC
//    外设，之后 gpio_set_level 就动不了它们了（⑤ 的假结论 #1 就是这么产生的）。
static void sdcard_sdmmc_identify(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_PROBING;   // 400 kHz = 规范的卡识别阶段速率

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;              // 识别只需 CMD + D0，变量最少
    slot.clk = PIN_CLK;
    slot.cmd = PIN_CMD;
    slot.d0  = PIN_D0;
    slot.d1  = PIN_D1;
    slot.d2  = PIN_D2;
    slot.d3  = PIN_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = sdmmc_host_init();
    ESP_LOGW(TAG, "IDENT host_init -> %s", esp_err_to_name(err));
    err = sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot);
    ESP_LOGW(TAG, "IDENT init_slot(0) -> %s", esp_err_to_name(err));
    if (err != ESP_OK) { return; }

    sdmmc_card_t *card = calloc(1, sizeof(sdmmc_card_t));
    if (card == NULL) { return; }
    err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IDENT card_init FAILED: %s (0x%x) -> nothing answered on slot0's CMD "
                      "line at 400 kHz. Combined with PADDRIVE=OK this points at socket/card",
                 esp_err_to_name(err), err);
        free(card);
        return;
    }
    const uint64_t mb = ((uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size) >> 20;
    ESP_LOGW(TAG, "IDENT card_init OK: name='%s' kind=%s size=%lluMB freq=%dkHz width=%d "
                  "mfg=0x%02x oem=0x%04x serial=0x%08lx",
             card->cid.name,
             card->is_sdio ? "SDIO" : (card->is_mmc ? "MMC" : "SD"),
             (unsigned long long)mb, card->max_freq_khz, card->log_bus_width,
             card->cid.mfg_id, card->cid.oem_id, (unsigned long)card->cid.serial);
    if (card->is_sdio) {
        ESP_LOGE(TAG, "IDENT VERDICT: is_sdio=1 -> this is the ESP32-C5 on slot1, NOT the "
                      "microSD. The earlier 'card init succeeded' proves nothing about the card");
    } else {
        ESP_LOGW(TAG, "IDENT VERDICT: a real SD/MMC card answered on slot0 -> SOCKET AND CARD "
                      "ARE ALIVE. Then the SPI-mode all-FF is a GPSPI routing problem, not dead "
                      "hardware. Cross-check the size above against the actual card");
    }
    free(card);
}

// 循环版 bitbang 探针：每 2 s 试一次，人可以边插拔卡边看，不必和串口采集对时间。
// 触发条件（见 sdcard.h 的 P4_SD_PROBE_LOOP_SEC）：只在诊断期开启。
// 标 unused：诊断关闭（宏=0）时它不被引用，否则会报 -Wunused-function。
__attribute__((unused))
static void sdcard_probe_loop_task(void *arg)
{
    (void)arg;
    const int total = P4_SD_PROBE_LOOP_SEC;
    // 前提 A 与插拔无关，只需验一次；不通过则后面几轮的 FF 都不必再解释
    (void)sdcard_pad_drive_selftest();
    for (int t = 0; t < total; t += 2) {
        sdcard_pad_rc_report(t == 0);      // 前提 B：随插拔变化，每轮都测
        sdcard_spi_bitbang_probe();
        gpio_reset_pin((gpio_num_t)PIN_CLK);
        gpio_reset_pin((gpio_num_t)PIN_CMD);
        gpio_reset_pin((gpio_num_t)PIN_D0);
        gpio_reset_pin((gpio_num_t)PIN_D3);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGW(TAG, "PROBELOOP done (%d s). If every round was all-FF, the bus was never "
                  "driven by anything outside the SDMMC peripheral", total);
    // 放在最后：它会把这几个脚交给 SDMMC 外设，之后 GPIO 级探针全部失效
    sdcard_sdmmc_identify();
    vTaskDelete(NULL);
}

esp_err_t sdcard_mount_spi(void)
{
    sdcard_scan_external_pullups();
    (void)sdcard_pad_drive_selftest();   // 前提 A：先证"我真的能驱动这几个脚"

#if P4_SD_PROBE_LOOP_SEC > 0
    // 诊断期：起一个低优先级任务反复探测，让人有充足时间重新插拔卡
    xTaskCreate(sdcard_probe_loop_task, "sd_probe", 4096, NULL, 2, NULL);
    return ESP_ERR_NOT_FINISHED;   // 诊断模式下不继续挂载，避免两条路径抢引脚
#endif

    // ⚠️ bitbang 探针**必须在 spi_bus_initialize 之前**跑。
    // 第一版把它放在 spi_bus_initialize 之后 —— 那时 43/44/39 已被 GPSPI 外设接管，
    // gpio_set_level 根本驱动不了引脚，于是"bitbang 也全 FF"是**探针自己没动引脚**造成的
    // 假结论。（同类教训：审计/探针工具自身出错会伪造出最容易误导人的那种结果。）
    sdcard_spi_bitbang_probe();
    // 探针用完把这几个脚交还默认态，免得残留配置干扰随后的 GPSPI 初始化
    gpio_reset_pin((gpio_num_t)PIN_CLK);
    gpio_reset_pin((gpio_num_t)PIN_CMD);
    gpio_reset_pin((gpio_num_t)PIN_D0);
    gpio_reset_pin((gpio_num_t)PIN_D3);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SDCARD_SPI_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;   // 20 MHz；先求通，通了再提速

    const spi_bus_config_t bus = {
        .mosi_io_num = PIN_CMD,
        .miso_io_num = PIN_D0,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SDCARD_SPI_HOST, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize 失败: %s", esp_err_to_name(err));
        s_info.mounted = false;
        return err;
    }

    // SPI 模式必须有上拉：SDMMC 路径是靠 SDMMC_SLOT_FLAG_INTERNAL_PULLUP 给的，
    // sdspi 不会自动加。板上卡座没有外部上拉（见 .h 注释），首次实测就死在
    // ESP_ERR_INVALID_RESPONSE —— 卡有回应但进不了 SPI 模式，正是缺上拉的典型症状。
    // 正式设计应在板上给 MISO/CS 加 10k 外部上拉；这里用内部上拉兜底。
    for (int pin = 0; pin < 4; ++pin) {
        static const gpio_num_t pu_pins[4] = { PIN_D0, PIN_D3, PIN_CMD, PIN_CLK };
        gpio_set_pull_mode(pu_pins[pin], GPIO_PULLUP_ONLY);
    }

    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = PIN_D3;
    dev.host_id = SDCARD_SPI_HOST;

    const esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,   // 绝不动用户卡里的数据
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "挂载 SD(SPI) SCLK=%d MOSI=%d MISO=%d CS=%d ...",
             PIN_CLK, PIN_CMD, PIN_D0, PIN_D3);
    sdcard_spi_raw_probe();
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &dev, &mcfg, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "SD(SPI) 挂载失败：卡里没有可识别的 FAT 文件系统"
                          "（刻意不自动格式化，请在电脑上格成 FAT32 再试）");
        } else {
            ESP_LOGE(TAG, "SD(SPI) 初始化卡失败: %s —— 依次怀疑：没插卡 / 卡座接触 / "
                          "该卡不支持 SPI 模式(极少见)", esp_err_to_name(err));
        }
        spi_bus_free(SDCARD_SPI_HOST);
        s_info.mounted = false;
        return err;
    }

    s_info.mounted = true;
    snprintf(s_info.name, sizeof(s_info.name), "%s", s_card->cid.name);
    s_info.size_mb = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
    s_info.speed_khz = s_card->max_freq_khz;
    s_info.bus_width = 1;                    // SPI 模式恒为 1 bit
    s_info.is_sdio = s_card->is_sdio;
    s_info.is_mmc = s_card->is_mmc;

    ESP_LOGI(TAG, "SD(SPI) 挂载成功: %s  %llu MB  %d kHz  1-line(SPI)",
             s_info.name, s_info.size_mb, s_info.speed_khz);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

const sdcard_info_t *sdcard_info(void) { return &s_info; }

// 只读地 dump MBR 分区表。为什么需要：卡容量 59638 MB 但挂出来的文件系统只有 9 MB
// ⇒ 剩下的 59 GB 在别的分区里，而 esp_vfs_fat_sdmmc_mount 只挂第一个分区。
// 先看清分区布局，才能判断"能不能挂第二个分区"还是"必须重新分区"（后者不可逆，由人拍板）。
void sdcard_dump_mbr(void)
{
    if (!s_info.mounted || s_card == NULL) {
        ESP_LOGW(TAG, "MBR 跳过：卡未挂载");
        return;
    }
    uint8_t *sec = heap_caps_malloc(512, MALLOC_CAP_DMA);   // sdmmc 读扇区要求 DMA 内存
    if (sec == NULL) {
        ESP_LOGE(TAG, "MBR 缓冲分配失败");
        return;
    }
    const esp_err_t err = sdmmc_read_sectors(s_card, sec, 0, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MBR 读扇区0失败: %s", esp_err_to_name(err));
        free(sec);
        return;
    }
    ESP_LOGW(TAG, "MBR sig=%02X%02X (期望 55AA)", sec[510], sec[511]);
    for (int i = 0; i < 4; ++i) {
        const uint8_t *p = sec + 0x1BE + 16 * i;
        const uint8_t  type = p[4];
        const uint32_t lba = (uint32_t)p[8] | ((uint32_t)p[9] << 8) |
                             ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
        const uint32_t cnt = (uint32_t)p[12] | ((uint32_t)p[13] << 8) |
                             ((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24);
        if (type == 0 && cnt == 0) {
            ESP_LOGW(TAG, "MBR p%d: <empty>", i + 1);
            continue;
        }
        // 常见 type：0B/0C=FAT32  06/0E=FAT16  07=NTFS/exFAT  83=Linux  05/0F=extended
        const char *tn = (type == 0x0B || type == 0x0C) ? "FAT32"
                       : (type == 0x06 || type == 0x0E) ? "FAT16"
                       : (type == 0x07) ? "NTFS/exFAT"
                       : (type == 0x83) ? "Linux"
                       : (type == 0x05 || type == 0x0F) ? "extended" : "other";
        ESP_LOGW(TAG, "MBR p%d: type=0x%02X(%s) lba=%lu sectors=%lu (%lu MB) boot=%02X",
                 i + 1, type, tn, (unsigned long)lba, (unsigned long)cnt,
                 (unsigned long)((uint64_t)cnt * 512 / 1048576), p[0]);
    }
    free(sec);
}

// 只读地列出根目录。目的：这张卡第一个分区只有 9 MB 且已用 7 MB，得先看清里面是什么
// 才能判断能不能重新分区/格式化（那是不可逆操作，必须由人拍板）。
void sdcard_list_root(void)
{
    if (!s_info.mounted) {
        ESP_LOGW(TAG, "LSROOT 跳过：卡未挂载");
        return;
    }
    DIR *d = opendir(MOUNT_POINT);
    if (d == NULL) {
        ESP_LOGE(TAG, "LSROOT opendir 失败");
        return;
    }
    ESP_LOGW(TAG, "LSROOT ==== %s 根目录 ====", MOUNT_POINT);
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        // 288 = MOUNT_POINT(7) + '/' + d_name 最长 255 + '\0'，不然 -Werror=format-truncation
        char full[288];
        snprintf(full, sizeof(full), MOUNT_POINT "/%s", e->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            ESP_LOGW(TAG, "LSROOT %-24s %s %lu B", e->d_name,
                     S_ISDIR(st.st_mode) ? "<DIR> " : "<FILE>", (unsigned long)st.st_size);
        } else {
            ESP_LOGW(TAG, "LSROOT %-24s <stat failed>", e->d_name);
        }
        ++n;
    }
    closedir(d);
    ESP_LOGW(TAG, "LSROOT ==== 共 %d 项，可用 %llu MB / 总 %llu MB ====",
             n, s_info.free_mb, s_info.total_mb);
}

esp_err_t sdcard_benchmark(uint32_t total_kb, sdcard_bench_t *out)
{
    if (!s_info.mounted || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));

    // 按可用空间自适应：2026-07-30 踩过 —— 这张卡第一个分区只有 9MB/可用 2MB，
    // 固定测 8MB 会在第 78 块失败，看着像驱动坏了，其实是没地方写。只用可用空间的一半。
    if (s_info.free_mb > 0) {
        const uint32_t cap_kb = (uint32_t)((s_info.free_mb * 1024) / 2);
        if (cap_kb < total_kb) {
            ESP_LOGW(TAG, "测速量 %lu KB -> %lu KB（可用仅 %llu MB）",
                     (unsigned long)total_kb, (unsigned long)cap_kb, s_info.free_mb);
            total_kb = cap_kb;
        }
    }
    if (total_kb < 64) {
        ESP_LOGW(TAG, "可用空间不足，跳过测速");
        return ESP_ERR_INVALID_STATE;
    }
    const size_t chunk = 32 * 1024;
    uint8_t *buf = malloc(chunk);
    if (buf == NULL) {
        ESP_LOGE(TAG, "测速缓冲分配失败");
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < chunk; i++) {
        buf[i] = (uint8_t)i;
    }

    const char *path = MOUNT_POINT "/_bench.bin";
    const uint32_t rounds = (total_kb * 1024) / chunk;
    esp_err_t ret = ESP_OK;

    // ---- 写 ----
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "创建 %s 失败", path);
        free(buf);
        return ESP_FAIL;
    }
    int64_t t0 = esp_timer_get_time();
    for (uint32_t i = 0; i < rounds; i++) {
        if (fwrite(buf, 1, chunk, f) != chunk) {
            ESP_LOGE(TAG, "写第 %lu 块失败", (unsigned long)i);
            ret = ESP_FAIL;
            break;
        }
    }
    fflush(f);
    fsync(fileno(f));
    int64_t t1 = esp_timer_get_time();
    fclose(f);

    if (ret == ESP_OK) {
        out->bytes = rounds * chunk;
        out->write_kbps = (float)out->bytes / 1024.0f / ((float)(t1 - t0) / 1e6f);

        // ---- 读 ----
        f = fopen(path, "rb");
        if (f == NULL) {
            ESP_LOGE(TAG, "回读打开失败");
            ret = ESP_FAIL;
        } else {
            t0 = esp_timer_get_time();
            for (uint32_t i = 0; i < rounds; i++) {
                if (fread(buf, 1, chunk, f) != chunk) {
                    ESP_LOGE(TAG, "读第 %lu 块失败", (unsigned long)i);
                    ret = ESP_FAIL;
                    break;
                }
            }
            t1 = esp_timer_get_time();
            fclose(f);
            out->read_kbps = (float)out->bytes / 1024.0f / ((float)(t1 - t0) / 1e6f);
        }
    }

    remove(path);   // 别在用户卡上留垃圾
    free(buf);

    if (ret == ESP_OK) {
        out->done = true;
        ESP_LOGI(TAG, "测速 %lu KB: 写 %.0f KB/s, 读 %.0f KB/s",
                 (unsigned long)(out->bytes / 1024), out->write_kbps, out->read_kbps);
    }
    s_bench = *out;   // 供 UI 轮询
    return ret;
}
