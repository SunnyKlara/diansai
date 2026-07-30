idf_component_register(
    SRCS "fl7703_4in_lcd.c" "jd9366_10_1in_lcd.c" "jd9365_10_1in_lcd.c" "gx610_12in_lcd.c" "st7703_4in_lcd.c" "axs15260_6_2in_lcd.c" "st7701_2_76in_lcd.c" "st7701_2_1in_lcd.c" "lvgl_demo.c" "ai_panel.c" "imu_qmi8658.c" "sdcard.c" "recorder.c" "jpeg_view.c" "video_stream.c" "main.c"
    INCLUDE_DIRS "."
    # esp_hosted 走 REQUIRES(公开)：和 p4_sta_host 一致 —— 选中的各子库
    # (eh_host_core / feat_system / RPC / transport_mcu / port_host_esp_idf …)
    # 靠 esp_hosted 的 INTERFACE 链接传播进来，不用逐个列。
    REQUIRES esp_hosted
    PRIV_REQUIRES esp_lcd esp_lcd_st7703 esp_lcd_axs15260 esp_lcd_st7701 esp_lcd_gx610 esp_lcd_jd9366 esp_lcd_jd9365 esp_lcd_fl7703 esp_hw_support esp_psram esp_driver_gpio
                  lvgl esp_lvgl_port esp_lcd_touch esp_lcd_touch_gt911 esp_lcd_touch_cst826
                  esp_lcd_touch_jd9366 esp_lcd_touch_ft6336u esp_driver_i2c
                  # 板载外设自检：QMI8658A 六轴(I2C0) + microSD(SDMMC slot0 4-line + FATFS)
                  esp_driver_sdmmc sdmmc fatfs vfs esp_timer
                  # 方案 D 显示半边：P4 硬件 JPEG 解码器
                  esp_driver_jpeg
                  # 方案 D 无线半边：WiFi(经 esp_wifi_remote/esp_hosted 到 C5) + TCP + ping
                  esp_wifi nvs_flash esp_event esp_netif lwip
    # 嵌入一张 K230 硬件编码器真实产出的帧：网络没连上时的占位画面，
    # 同时也是「解码器本身好不好」的离线判据（把网络这个变量摘掉）。
    EMBED_FILES "k230_frame1.jpg"
)
