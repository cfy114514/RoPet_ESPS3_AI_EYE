#include "wifi_board.h"
#include "audio_codecs/vb6824_audio_codec.h"
// #include "display/lcd_display.h" // 禁用LCD显示功能，注释掉相关头文件
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h" // BUILTIN_LED_GPIO (GPIO_NUM_7) should be defined here
#include "iot/thing_manager.h"
#include "led/single_led.h" // 重新引入 SingleLed 头文件
#include "power_save_timer.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
// #include <esp_lcd_panel_vendor.h> // 禁用LCD显示功能，注释掉相关头文件
// #include <esp_lcd_panel_io.h>     // 禁用LCD显示功能，注释掉相关头文件
// #include <esp_lcd_panel_ops.h>    // 禁用LCD显示功能，注释掉相关头文件
// #include <esp_lcd_gc9a01.h>       // 禁用LCD显示功能，注释掉相关头文件
#include <driver/spi_common.h> // SPI总线可能用于其他功能，保留
#include <driver/gpio.h>       // For general GPIO (like sleep_gpio), 保留
#include <esp_err.h>           // For ESP_ERROR_CHECK, 保留

#if CONFIG_USE_EYE_STYLE_ES8311
#include "touch_button.h"
#endif

#define TAG "CompactWifiBoardLCD"

// 移除了RGB灯珠的GPIO和数量定义
// #define RGB_LED_GPIO BUILTIN_LED_GPIO
// #define RGB_LED_COUNT 1

// 禁用LCD显示功能，注释掉LVGL字体声明
// #if CONFIG_LCD_GC9A01_240X240
//     LV_FONT_DECLARE(font_puhui_20_4);
//     LV_FONT_DECLARE(font_awesome_20_4);
// #elif CONFIG_LCD_GC9A01_160X160
//     LV_FONT_DECLARE(font_puhui_14_1);
//     LV_FONT_DECLARE(font_awesome_14_1);
// #endif
// 👇 在文件顶部添加这个任务函数
void auto_wakeup_task(void *arg) {
    // 循环等待，直到应用状态变为空闲
    while (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
        vTaskDelay(pdMS_TO_TICKS(100)); // 每 100 毫秒检查一次
    }

    // 状态已就绪，执行唤醒
    ESP_LOGI("AutoWakeupTask", "Application is idle, invoking wake word.");
    Application::GetInstance().WakeWordInvoke("你好秦彻");

    // 任务完成，删除自身
    vTaskDelete(NULL);
}
class CompactWifiBoardLCD : public WifiBoard {
private:
    // 禁用LCD显示功能，注释掉LCD相关的成员变量
    // esp_lcd_panel_io_handle_t lcd_io = NULL;
    // esp_lcd_panel_handle_t lcd_panel = NULL;
    // LcdDisplay* display_; // LCD显示对象

    Button boot_button_;
    VbAduioCodec audio_codec;
    PowerSaveTimer* power_save_timer_;
    // 移除了 led_strip 句柄声明
    // led_strip_handle_t rgb_led_strip_ = NULL;
    // 新增成员变量和常量
    int64_t startup_time_ms_ = 0; // 用于记录系统启动时间（毫秒）
    // 定义一个时间窗口，在此期间不允许通过重复按键触发配网
    const int64_t WIFI_CONFIG_PROTECTION_TIME_MS = 10 * 1000; // 例如：10秒

    SingleLed* builtin_led_; // 声明 SingleLed 对象

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            // 禁用LCD显示功能，注释掉对显示和背光的操作
            // auto display = GetDisplay();
            // if (display) { // 添加空指针检查，虽然 GetDisplay() 会返回 nullptr
            //     display->SetChatMessage("system", "");
            //     display->SetEmotion("sleepy");
            // }
            // #if CONFIG_LCD_GC9A01_160X160
            //     auto backlight = GetBacklight();
            //     if (backlight) { // 添加空指针检查
            //         backlight->RestoreBrightness();
            //     }
            // #endif
            //gpio_set_level(SLEEP_GOIO, 0);

        });
        power_save_timer_->OnExitSleepMode([this]() {
            // 禁用LCD显示功能，注释掉对显示和背光的操作
            // auto display = GetDisplay();
            // if (display) { // 添加空指针检查
            //     display->SetChatMessage("system", "");
            //     display->SetEmotion("neutral");
            // }
            // #if CONFIG_LCD_GC9A01_160X160
            //     auto backlight = GetBacklight();
            //     if (backlight) { // 添加空指针检查
            //         backlight->RestoreBrightness();
            //     }
            // #endif
            //gpio_set_level(SLEEP_GOIO, 1);
        });
        power_save_timer_->OnShutdownRequest([this]() {
            //pmic_->PowerOff();
            //gpio_set_level(SLEEP_GOIO, 0);
            ESP_LOGI(TAG,"Not used for a long time. Shut down. Press and hold to turn on!");
            gpio_set_level(SLEEP_GOIO, 0);
        });
        power_save_timer_->SetEnabled(true);
    }

    // 初始化按钮
void InitializeButtons() {
    // 当boot_button_被点击时，执行以下操作
    boot_button_.OnClick([this]() {
        auto& app = Application::GetInstance();
        int64_t current_time_ms = esp_timer_get_time() / 1000;
        
        // 核心逻辑：无论是否在保护期内，单击都应该执行切换监听状态。
        app.ToggleChatState(); 

        // 仅在设备处于特定状态且不在保护期内时，才执行 Wi-Fi 配置重置。
        if (current_time_ms >= WIFI_CONFIG_PROTECTION_TIME_MS) {
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ESP_LOGI(TAG, "单击触发重置Wifi配置 (已通过保护期检查)");
                ResetWifiConfiguration();
            }
        } else {
             ESP_LOGW(TAG, "系统启动保护期内，忽略单击触发配网。(当前时间: %lldms, 保护期: %lldms)", 
                      current_time_ms, WIFI_CONFIG_PROTECTION_TIME_MS);
        }
    });

        boot_button_.OnPressRepeat([this](uint16_t count) { 
            // <-- 插入以下代码
            // 检查当前系统运行时间是否在保护期内
            if ((esp_timer_get_time() / 1000) < WIFI_CONFIG_PROTECTION_TIME_MS) {
                ESP_LOGW(TAG, "系统启动保护期内，忽略重复按键触发配网。(当前时间: %lldms, 保护期: %lldms)", 
                            esp_timer_get_time() / 1000, WIFI_CONFIG_PROTECTION_TIME_MS);
                return; // 如果在保护期内，则直接返回，不执行配网逻辑
            }
            // <-- 插入以上代码结束
            if(count >= 3){
                ESP_LOGI(TAG, "重新配网");
                ResetWifiConfiguration();
            }
        });
        #if (defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1)
            boot_button_.OnDoubleClick([this]() {
            if (esp_timer_get_time() > 20 * 1000 * 1000) {
                ESP_LOGI(TAG, "Long press, do not enter OTA mode %ld", (uint32_t)esp_timer_get_time());
                return;
            }
            audio_codec.OtaStart(0);
        });
        #endif
        // boot_button_.OnDoubleClick([this]() {
        //     auto& app = Application::GetInstance();
        //     app.eye_style_num = (app.eye_style_num+1) % 8;
        //     app.eye_style(app.eye_style_num);
        // });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        // thing_manager.AddThing(iot::CreateThing("Screen")); // 禁用LCD显示功能，注释掉屏幕相关物联网设备
        // thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

    // 禁用LCD显示功能，注释掉GC9A01-SPI2初始化-用于显示小智
    // void InitializeSpiEye1() {
    //     const spi_bus_config_t buscfg = {
    //         .mosi_io_num = GC9A01_SPI1_LCD_GPIO_MOSI,
    //         .miso_io_num = GPIO_NUM_NC,
    //         .sclk_io_num = GC9A01_SPI1_LCD_GPIO_SCLK,
    //         .quadwp_io_num = GPIO_NUM_NC,
    //         .quadhd_io_num = GPIO_NUM_NC,
    //         .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t), // 增大传输大小,
    //     };
    //     ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI1_NUM, &buscfg, SPI_DMA_CH_AUTO));
    // }

    // 禁用LCD显示功能，注释掉GC9A01-SPI2初始化-用于显示魔眼
    // void InitializeGc9a01DisplayEye1() {
    //     ESP_LOGI(TAG, "Init GC9A01 display1");

    //     ESP_LOGI(TAG, "Install panel IO1");
    //     ESP_LOGD(TAG, "Install panel IO1");
    //     const esp_lcd_panel_io_spi_config_t io_config = {
    //         .cs_gpio_num = GC9A01_SPI1_LCD_GPIO_CS,
    //         .dc_gpio_num = GC9A01_SPI1_LCD_GPIO_DC,
    //         .spi_mode = 0,
    //         .pclk_hz = GC9A01_LCD_PIXEL_CLK_HZ,
    //         .trans_queue_depth = 10,
    //         .lcd_cmd_bits = GC9A01_LCD_CMD_BITS,
    //         .lcd_param_bits = GC9A01_LCD_PARAM_BITS,

    //     };
    //     esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI1_NUM, &io_config, &lcd_io);

    //     ESP_LOGD(TAG, "Install LCD1 driver");
    //     esp_lcd_panel_dev_config_t panel_config = {
    //         .reset_gpio_num = GC9A01_SPI1_LCD_GPIO_RST,
    //         .color_space = GC9A01_LCD_COLOR_SPACE,
    //         .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL,

    //     };
    //     panel_config.rgb_endian = DISPLAY_RGB_ORDER;
    //     esp_lcd_new_panel_gc9a01(lcd_io, &panel_config, &lcd_panel);

    //     esp_lcd_panel_reset(lcd_panel);
    //     esp_lcd_panel_init(lcd_panel);
    //     esp_lcd_panel_invert_color(lcd_panel, DISPLAY_COLOR_INVERT);
    //     esp_lcd_panel_disp_on_off(lcd_panel, true);
    //     display_ = new SpiLcdDisplay(lcd_io, lcd_panel,
    //         DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
    //         {
    //             #if CONFIG_LCD_GC9A01_240X240
    //                 .text_font = &font_puhui_20_4,
    //                 .icon_font = &font_awesome_20_4,
    //             #elif CONFIG_LCD_GC9A01_160X160
    //                 .text_font = &font_puhui_14_1,
    //                 .icon_font = &font_awesome_14_1,
    //             #endif
    //             .emoji_font = font_emoji_64_init(),
    //         });
    // }

public:
    CompactWifiBoardLCD():boot_button_(BOOT_BUTTON_GPIO), audio_codec(CODEC_RX_GPIO, CODEC_TX_GPIO){
        #if CONFIG_LCD_GC9A01_160X160
            // 禁用LCD显示功能，注释掉背光GPIO配置
            // gpio_config_t bk_gpio_config = {
            //         .pin_bit_mask = 1ULL << GC9A01_SPI1_LCD_GPIO_BL,
            //         .mode = GPIO_MODE_OUTPUT,
            //     };
            // ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
            // ESP_ERROR_CHECK(gpio_set_level(GC9A01_SPI1_LCD_GPIO_BL, 1));
        #endif

        // 修改为使用 TurnOn() 方法来点亮 GPIO7 的 LED
        builtin_led_ = new SingleLed(GPIO_NUM_7); // 使用GPIO_NUM_7初始化LED
        builtin_led_->TurnOn(); // 点亮LED
        ESP_LOGI(TAG, "Built-in LED on GPIO7 turned on."); // 打印日志确认

        // 禁用LCD显示功能，注释掉LCD初始化函数调用
        // InitializeSpiEye1();
        // InitializeGc9a01DisplayEye1();
        // display_ = nullptr; // 明确将显示指针设置为nullptr，因为不再初始化LCD

        InitializeButtons();
        InitializeIot();

        gpio_set_pull_mode(SLEEP_GOIO, GPIO_PULLUP_ONLY);
        gpio_set_direction(SLEEP_GOIO, GPIO_MODE_OUTPUT);
        gpio_set_level(SLEEP_GOIO, 1);

        InitializePowerSaveTimer();

        audio_codec.OnWakeUp([this](const std::string& command) {
            if (command == std::string(vb6824_get_wakeup_word())){
                if(Application::GetInstance().GetDeviceState() != kDeviceStateListening){
                    Application::GetInstance().WakeWordInvoke("你好秦彻");
                }
            }else if (command == "开始配网"){
                ESP_LOGI(TAG,"fff");
                ResetWifiConfiguration();
            }
        });
        xTaskCreate(auto_wakeup_task, "auto_wakeup", 2048, NULL, 5, NULL);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        // 禁用LCD显示功能，始终返回nullptr
        return nullptr;
    }

    #if CONFIG_LCD_GC9A01_160X160
        // 获取背光对象
        virtual Backlight* GetBacklight() override {
            // 禁用LCD显示功能，始终返回nullptr
            // if (GC9A01_SPI1_LCD_GPIO_BL != GPIO_NUM_NC) {
            //     static PwmBacklight backlight(GC9A01_SPI1_LCD_GPIO_BL, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            //     return &backlight;
            // }
            return nullptr;
        }
    #endif
};

DECLARE_BOARD(CompactWifiBoardLCD);
