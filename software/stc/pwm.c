#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/timer.h"
#include "hardware/sync.h"
#include "hardware/resets.h"

#include "ST7789.h"
#include "TestParticle.h"
#include "Capture.h"
#include "Backlight.h"
#include "Setting.h"
#include "BatMonitor.h"
#include "Touch.h"

#include "build/generated/border0.h"
#include "build/generated/border1.h"


#define LE2BE(v)    ( \
    (uint16_t)((((v) >> 8) & 0xFF) | (((v) & 0xFF) << 8))\
)
#define RGB(r, g, b)    ( \
    (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | (((b) >> 3) << 0)) \
)


// 边框
static const uint16_t* borders[] = {
    border0, border1,
};
// 扫描线颜色
static const uint16_t scanlineColors[256] = {[0 ... 255] = RGB(0x80, 0x80, 0x80)};

static uint64_t idle_time_acc = 0;
static uint64_t last_report_time = 0;

static volatile int curScanline = 0;


static void callbackVBL(){
    ST7789_Prepare(
        Setting_GetSettingData()->rotation,
        Setting_GetSettingData()->colorOrder,
        Setting_GetSettingData()->winX[Setting_GetSettingData()->style],
        Setting_GetSettingData()->winY[Setting_GetSettingData()->style],
        Setting_GetSettingData()->winW[Setting_GetSettingData()->style],
        Setting_GetSettingData()->winH[Setting_GetSettingData()->style]
    );
    curScanline = 0;
    // 息屏计数器喂狗，同时点亮背光
    Backlight_Feed();
}
static void callbackHBL(uint16_t* _buffer, uint32_t _size){
    // 绘制该条扫描线
    ST7789_Transfer((void*)_buffer, _size);
    // // 奇数行的处理策略
    // if(curScanline & 1){
    //     if(Setting_GetSettingData()->style == Style_Scanline){
    //         // 绘制灰色扫描线
    //         ST7789_Transfer((void*)scanlineColors, _size);
    //     }else if(Setting_GetSettingData()->style == Style_FullScreen){
    //         // 重复绘制该条扫描线
    //         ST7789_Transfer((void*)_buffer, _size);
    //     }
    // }

    curScanline++;
}
static const struct Capture_config capture_config = {
    .callbackVBL = callbackVBL,
    .callbackHBL = callbackHBL,
};


int main()
{
    stdio_init_all();

    pico_unique_board_id_t boardId;
    pico_get_unique_board_id(&boardId);
    printf("UniqueId=%02X%02X%02X%02X%02X%02X%02X%02X\n", boardId.id[0], boardId.id[1], boardId.id[2], boardId.id[3], boardId.id[4], boardId.id[5], boardId.id[6], boardId.id[7]);

    printf("System Clock=%fkHz\n", clock_get_hz(clk_sys) / 1000.0f);

    // 超频到160MHz
    // set_sys_clock_khz(160000, true);

    stdio_init_all();

    printf("Overclocked System Clock=%fkHz\n", clock_get_hz(clk_sys) / 1000.0f);

    // 保留UART、SPI、PIO、DMA、GPIO、PWM、ADC、Timer硬件时钟，关闭USB、I2C、RTC
    clock_stop(clk_usb);
    reset_block(
        RESETS_RESET_I2C0_BITS |
        RESETS_RESET_I2C1_BITS |
        RESETS_RESET_RTC_BITS |
        RESETS_RESET_USBCTRL_BITS |
        0
    );
    clocks_hw->clk[clk_sys].ctrl &= ~RESETS_RESET_I2C0_BITS;
    clocks_hw->clk[clk_sys].ctrl &= ~RESETS_RESET_I2C1_BITS;
    clock_stop(clk_rtc);

    // 初始化存档
    Setting_Init();

    // 初始化电压监视器
    BatMonitor_Init();

    // 初始化lcd
    ST7789_Init();

    // 设置边框
    ST7789_DMAWaitForIdle();
    ST7789_Prepare(
        Setting_GetSettingData()->rotation,
        Setting_GetSettingData()->colorOrder,
        Setting_GetSettingData()->winX[Style_FullScreen],
        Setting_GetSettingData()->winY[Style_FullScreen],
        Setting_GetSettingData()->winW[Style_FullScreen],
        Setting_GetSettingData()->winH[Style_FullScreen]
    );
    ST7789_DMAWaitForIdle();
    ST7789_Transfer(borders[0], Setting_GetSettingData()->winW[Style_FullScreen] * Setting_GetSettingData()->winH[Style_FullScreen] * sizeof(uint16_t));
    ST7789_DMAWaitForIdle();
    ST7789_Prepare(
        Setting_GetSettingData()->rotation,
        Setting_GetSettingData()->colorOrder,
        Setting_GetSettingData()->winX[Style_Classic],
        Setting_GetSettingData()->winY[Style_Classic],
        Setting_GetSettingData()->winW[Style_Classic],
        Setting_GetSettingData()->winH[Style_Classic]
    );
    ST7789_DMAWaitForIdle();

    // 初始化触摸按键
    Touch_Init();

    // 初始化capture
    // Capture_Init(&capture_config);

    // 初始化背光灯
    Backlight_Init();

    // 设置背光亮度
    Backlight_Set(Setting_GetSettingData()->backlight);

    ST7789_Prepare(
        Setting_GetSettingData()->rotation,
        Setting_GetSettingData()->colorOrder,
        0,
        0,
        8,
        8
    );

    uint16_t tmp[8 * 8] = {[0 ... 63] = 0};
    uint16_t tmp2[8 * 8] = {[0 ... 63] = 0xFFFF};
    last_report_time = to_us_since_boot(get_absolute_time());
    while (true) {
        // 睡眠
        __wfi();

        if(Touch_IsTouched())
            ST7789_Transfer(tmp, 8 * 8 * sizeof(uint16_t));
        else
            ST7789_Transfer(tmp2, 8 * 8 * sizeof(uint16_t));

        // 每秒显示点啥
        uint64_t t = to_us_since_boot(get_absolute_time());
        if(t - last_report_time >= 1000000){
            last_report_time = t;
            printf("Voltage=%fV\n", BatMonitor_GetValue());
        }
    }
}
