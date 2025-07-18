#include "wifi_board.h"
#include "audio_codecs/vb6824_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "power_save_timer.h"
#include <esp_timer.h>
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_lcd_gc9a01.h>
#if CONFIG_USE_EYE_STYLE_ES8311
    #include "touch_button.h"
#endif

#define TAG "CompactWifiBoardLCD"

#if CONFIG_LCD_GC9A01_240X240
    LV_FONT_DECLARE(font_puhui_20_4);   
    LV_FONT_DECLARE(font_awesome_20_4); 
#elif CONFIG_LCD_GC9A01_160X160
    LV_FONT_DECLARE(font_puhui_14_1);   
    LV_FONT_DECLARE(font_awesome_14_1); 
#endif

class CompactWifiBoardLCD : public WifiBoard {
private:
 
    esp_lcd_panel_io_handle_t lcd_io = NULL;
    esp_lcd_panel_handle_t lcd_panel = NULL;
    Button boot_button_;
    LcdDisplay* display_;
    VbAduioCodec audio_codec;
    PowerSaveTimer* power_save_timer_;
     // 新增成员变量和常量
    int64_t startup_time_ms_ = 0; // 用于记录系统启动时间（毫秒）
    // 定义一个时间窗口，在此期间不允许通过重复按键触发配网
    const int64_t WIFI_CONFIG_PROTECTION_TIME_MS = 10 * 1000; // 例如：10秒

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("sleepy");
            #if CONFIG_LCD_GC9A01_160X160
                GetBacklight()->RestoreBrightness();
            #endif
            //gpio_set_level(SLEEP_GOIO, 0);

        });
        power_save_timer_->OnExitSleepMode([this]() {
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("neutral");
            #if CONFIG_LCD_GC9A01_160X160
                GetBacklight()->RestoreBrightness();
            #endif
            //gpio_set_level(SLEEP_GOIO, 1);
        });
        power_save_timer_->OnShutdownRequest([this]() {
            //pmic_->PowerOff();
            //gpio_set_level(SLEEP_GOIO, 0);
            // ESP_LOGI(TAG,"Not used for a long time. Shut down. Press and hold to turn on!");
            // gpio_set_level(SLEEP_GOIO, 0);
        });
        power_save_timer_->SetEnabled(true);
    }

    // 初始化按钮
    void InitializeButtons() {
        // 当boot_button_被点击时，执行以下操作
    boot_button_.OnClick([this]() {
        // --- 系统保护期检查开始 ---
        // 核心逻辑：在系统启动后的指定保护期内，不执行可能导致误触配网的逻辑。
        // esp_timer_get_time() 返回的是微秒，所以除以 1000 转换为毫秒。
        // WIFI_CONFIG_PROTECTION_TIME_MS 是你定义的保护期时长（例如 10000 毫秒即 10 秒）。
        if ((esp_timer_get_time() / 1000) < WIFI_CONFIG_PROTECTION_TIME_MS) {
            // 如果当前时间仍在保护期内，则打印警告日志。
            ESP_LOGW(TAG, "系统启动保护期内，忽略单击触发配网。(当前时间: %lldms, 保护期: %lldms)", 
                     esp_timer_get_time() / 1000, WIFI_CONFIG_PROTECTION_TIME_MS);
            
            // 重要决定点：在保护期内，除了配网，单击是否还应执行其他功能？
            // 原始代码中 OnClick 还会执行 app.ToggleChatState()。
            // 1. 如果你希望在保护期内**完全禁用所有单击功能**：
            //    直接在这里 return; 即可。
            //    return; 

            // 2. 如果你希望在保护期内**只禁用配网，但允许其他功能**（例如 ToggleChatState()）：
            //    保留 app.ToggleChatState()，就像下面这样。
            //    这通常是更灵活的选择，允许用户即使在启动阶段也能进行一些基本交互。
            Application::GetInstance().ToggleChatState(); // 允许切换聊天状态
            return; // 阻止配网逻辑继续执行
        }
        // --- 系统保护期检查结束 ---
        
        // 以下是原始的 OnClick 逻辑，现在只在保护期外执行
        auto& app = Application::GetInstance();
        // 只有当设备处于 kDeviceStateStarting 状态且 WifiStation 未连接时，才重置 Wifi 配置。
        // 并且，现在这个判断只有在通过了“系统保护期”检查后才会进行。
        if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
            ESP_LOGI(TAG, "单击触发重置Wifi配置 (已通过保护期检查)");
            ResetWifiConfiguration();
        }
        // 如果在保护期外，并且你希望单击仍然能切换聊天状态，确保这里不会被上面的 if 语句中的 return 阻止。
        // 如果在保护期内已经执行过 ToggleChatState()，这里就不需要重复执行了。
        // 如果上面保护期内选择了 return; 那么这里就应该继续执行 ToggleChatState()
        // 但为了避免重复，如果保护期内执行了，这里可以不执行。根据你的设计决定。
        // 比如，为了简单，可以在保护期外，也总是执行 ToggleChatState()
        // app.ToggleChatState(); 
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
        thing_manager.AddThing(iot::CreateThing("Screen"));
        // thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

    // GC9A01-SPI2初始化-用于显示小智
    void InitializeSpiEye1() {
        const spi_bus_config_t buscfg = {       
            .mosi_io_num = GC9A01_SPI1_LCD_GPIO_MOSI,
            .miso_io_num = GPIO_NUM_NC,
            .sclk_io_num = GC9A01_SPI1_LCD_GPIO_SCLK,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t), // 增大传输大小,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI1_NUM, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01-SPI2初始化-用于显示魔眼
    void InitializeGc9a01DisplayEye1() {
        ESP_LOGI(TAG, "Init GC9A01 display1");

        ESP_LOGI(TAG, "Install panel IO1");
        ESP_LOGD(TAG, "Install panel IO1");
        const esp_lcd_panel_io_spi_config_t io_config = {
            .cs_gpio_num = GC9A01_SPI1_LCD_GPIO_CS,
            .dc_gpio_num = GC9A01_SPI1_LCD_GPIO_DC,
            .spi_mode = 0,
            .pclk_hz = GC9A01_LCD_PIXEL_CLK_HZ,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = GC9A01_LCD_CMD_BITS,
            .lcd_param_bits = GC9A01_LCD_PARAM_BITS,
            
    
        };
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI1_NUM, &io_config, &lcd_io);

        ESP_LOGD(TAG, "Install LCD1 driver");
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GC9A01_SPI1_LCD_GPIO_RST,
            .color_space = GC9A01_LCD_COLOR_SPACE,
            .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL,
            
        };
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        esp_lcd_new_panel_gc9a01(lcd_io, &panel_config, &lcd_panel);

        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, DISPLAY_COLOR_INVERT);
        esp_lcd_panel_disp_on_off(lcd_panel, true);
        display_ = new SpiLcdDisplay(lcd_io, lcd_panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            {
                #if CONFIG_LCD_GC9A01_240X240
                    .text_font = &font_puhui_20_4,
                    .icon_font = &font_awesome_20_4,
                #elif CONFIG_LCD_GC9A01_160X160
                     .text_font = &font_puhui_14_1,
                     .icon_font = &font_awesome_14_1,
                #endif
                .emoji_font = font_emoji_64_init(),
            });
    }

public:
      CompactWifiBoardLCD():boot_button_(BOOT_BUTTON_GPIO), audio_codec(CODEC_RX_GPIO, CODEC_TX_GPIO){
        #if CONFIG_LCD_GC9A01_160X160
            gpio_config_t bk_gpio_config = {
                    .pin_bit_mask = 1ULL << GC9A01_SPI1_LCD_GPIO_BL,
                    .mode = GPIO_MODE_OUTPUT,
                };
            ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
            ESP_ERROR_CHECK(gpio_set_level(GC9A01_SPI1_LCD_GPIO_BL, 1));
        #endif

        InitializeSpiEye1();
        InitializeGc9a01DisplayEye1();
        InitializeButtons();
        InitializeIot();

        gpio_set_pull_mode(SLEEP_GOIO, GPIO_PULLUP_ONLY);
        gpio_set_direction(SLEEP_GOIO, GPIO_MODE_OUTPUT);
        gpio_set_level(SLEEP_GOIO, 1);

        InitializePowerSaveTimer();

        audio_codec.OnWakeUp([this](const std::string& command) {
            if (command == std::string(vb6824_get_wakeup_word())){
                if(Application::GetInstance().GetDeviceState() != kDeviceStateListening){
                    Application::GetInstance().WakeWordInvoke("你好小智");
                }
            }else if (command == "开始配网"){
                ESP_LOGI(TAG,"fff");
                ResetWifiConfiguration();
            }
        });
        
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

   virtual AudioCodec* GetAudioCodec() override {
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }


    #if CONFIG_LCD_GC9A01_160X160
        // 获取背光对象
        virtual Backlight* GetBacklight() override {
            if (GC9A01_SPI1_LCD_GPIO_BL != GPIO_NUM_NC) {
                static PwmBacklight backlight(GC9A01_SPI1_LCD_GPIO_BL, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
                return &backlight;
            }
            return nullptr;
        }
    #endif
};

DECLARE_BOARD(CompactWifiBoardLCD);
