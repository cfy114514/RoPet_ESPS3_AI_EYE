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
#include <esp_lcd_gc9a01.h>
#include <wifi_station.h>
#include "esp_lvgl_port.h"
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#include <esp_lcd_gc9a01.h>
#if CONFIG_USE_EYE_STYLE_ES8311 || CONFIG_USE_EYE_STYLE_VB6824 // 如果开启魔眼显示
#include "touch_button.h"
#endif
#include "esp32_camera.h"
#define TAG "CompactWifiBoardLCD"

#if CONFIG_LCD_GC9A01_240X240
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
#elif CONFIG_LCD_GC9A01_160X160
LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);
#endif

class CompactWifiBoardLCD : public WifiBoard
{
private:
    esp_lcd_panel_io_handle_t lcd_io1 = NULL;
    esp_lcd_panel_handle_t lcd_panel1 = NULL;

    esp_lcd_panel_io_handle_t lcd_io2 = NULL;
    esp_lcd_panel_handle_t lcd_panel2 = NULL;

    Button boot_button_;
    LcdDisplay *display_;
    VbAduioCodec audio_codec;
    PowerSaveTimer *power_save_timer_;
    Esp32Camera *camera_;
    void InitializePowerSaveTimer()
    {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]()
                                            {
                                                ESP_LOGI(TAG, "Enabling sleep mode");
                                                auto display = GetDisplay();
                                                display->SetChatMessage("system", "");
                                                display->SetEmotion("sleepy");
#if CONFIG_LCD_GC9A01_160X160
                                                GetBacklight()->RestoreBrightness();
#endif
                                                // gpio_set_level(SLEEP_GOIO, 0);
                                            });
        power_save_timer_->OnExitSleepMode([this]()
                                           {
                                               auto display = GetDisplay();
                                               display->SetChatMessage("system", "");
                                               display->SetEmotion("neutral");
#if CONFIG_LCD_GC9A01_160X160
                                               GetBacklight()->RestoreBrightness();
#endif
                                               // gpio_set_level(SLEEP_GOIO, 1);
                                           });
        power_save_timer_->OnShutdownRequest([this]()
                                             {
            //pmic_->PowerOff();
            //gpio_set_level(SLEEP_GOIO, 0);
            ESP_LOGI(TAG,"Not used for a long time. Shut down. Press and hold to turn on!");
            gpio_set_level(SLEEP_GOIO, 0); });
        power_save_timer_->SetEnabled(true);
    }

    // 初始化按钮
    void InitializeButtons()
    {
        // 当boot_button_被点击时，执行以下操作
        boot_button_.OnClick([this]()
                             {
            // 获取应用程序实例
            auto& app = Application::GetInstance();
            // 如果设备状态为kDeviceStateStarting且WifiStation未连接，则重置Wifi配置
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            // 切换聊天状态
            app.ToggleChatState(); });
        boot_button_.OnPressRepeat([this](uint16_t count)
                                   {
            if(count >= 3){
                ESP_LOGI(TAG, "唤醒词升级摸式");
                // ResetWifiConfiguration();
                    if (esp_timer_get_time() > 20 * 1000 * 1000) {
                ESP_LOGI(TAG, "Long press, do not enter OTA mode %ld", (uint32_t)esp_timer_get_time());
                return;
            }
            audio_codec.OtaStart(0);
            } });
#if (defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1)
        boot_button_.OnDoubleClick([this]()
                                   {
            if (esp_timer_get_time() > 20 * 1000 * 1000) {
                ESP_LOGI(TAG, "Long press, do not enter OTA mode %ld", (uint32_t)esp_timer_get_time());
                return;
            }
            audio_codec.OtaStart(0); });
#endif
        // boot_button_.OnDoubleClick([this]() {
        //     auto& app = Application::GetInstance();
        //     app.eye_style_num = (app.eye_style_num+1) % 8;
        //     app.eye_style(app.eye_style_num);
        // });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot()
    {
        auto &thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        // thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

    // GC9A01-SPI2初始化-用于显示小智
    void InitializeSpiEye1()
    {
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
    void InitializeGc9a01DisplayEye1()
    {
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
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI1_NUM, &io_config, &lcd_io1);

        ESP_LOGD(TAG, "Install LCD1 driver");
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GC9A01_SPI1_LCD_GPIO_RST,
            .color_space = GC9A01_LCD_COLOR_SPACE,
            .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL,

        };
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        esp_lcd_new_panel_gc9a01(lcd_io1, &panel_config, &lcd_panel1);

        esp_lcd_panel_reset(lcd_panel1);
        esp_lcd_panel_init(lcd_panel1);
        esp_lcd_panel_invert_color(lcd_panel1, DISPLAY_COLOR_INVERT);
        esp_lcd_panel_disp_on_off(lcd_panel1, true);
    }

    // wang
    //  GC9A01-SPI2初始化-用于显示小智
    void InitializeSpiEye2()
    {
        const spi_bus_config_t buscfg = {
            .mosi_io_num = GC9A01_SPI2_LCD_GPIO_MOSI,
            .miso_io_num = GPIO_NUM_NC,
            .sclk_io_num = GC9A01_SPI2_LCD_GPIO_SCLK,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t), // 增大传输大小,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI2_NUM, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01-SPI2初始化-用于显示魔眼
    void InitializeGc9a01DisplayEye2()
    {
        ESP_LOGI(TAG, "Init GC9A01 display2");

        ESP_LOGI(TAG, "Install panel IO2");

        const esp_lcd_panel_io_spi_config_t io_config = {
            .cs_gpio_num = GC9A01_SPI2_LCD_GPIO_CS,
            .dc_gpio_num = GC9A01_SPI2_LCD_GPIO_DC,
            .spi_mode = 0,
            .pclk_hz = GC9A01_LCD_PIXEL_CLK_HZ,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = GC9A01_LCD_CMD_BITS,
            .lcd_param_bits = GC9A01_LCD_PARAM_BITS,

        };
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI2_NUM, &io_config, &lcd_io2);

        ESP_LOGD(TAG, "Install LCD2 driver");
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GC9A01_SPI2_LCD_GPIO_RST,
            .color_space = GC9A01_LCD_COLOR_SPACE,
            .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL,

        };
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        esp_lcd_new_panel_gc9a01(lcd_io2, &panel_config, &lcd_panel2);

        esp_lcd_panel_reset(lcd_panel2);
        esp_lcd_panel_init(lcd_panel2);
        esp_lcd_panel_invert_color(lcd_panel2, DISPLAY_COLOR_INVERT);
        esp_lcd_panel_disp_on_off(lcd_panel2, true);
    }
    void InitializeCamera()
    {
        // Open camera power

        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_0; // LEDC通道选择  用于生成XCLK时钟 但是S3不用
        config.ledc_timer = LEDC_TIMER_0;     // LEDC timer选择  用于生成XCLK时钟 但是S3不用
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD; // 这里写-1 表示使用已经初始化的I2C接口
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        // config.sccb_i2c_port = 1;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

        camera_ = new Esp32Camera(config);
    }

    // 初始化双屏
    void InitializeDualScreenEye()
    {
        // 初始化第一块屏幕
        InitializeSpiEye1();
        InitializeSpiEye2();
        InitializeGc9a01DisplayEye1();
        InitializeGc9a01DisplayEye2();

        // 创建双屏显示对象
        display_ = new DualScreenDisplay(
            lcd_io1, lcd_panel1,
            lcd_io2, lcd_panel2,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            {
                .text_font = &font_puhui_20_4,
                .icon_font = &font_puhui_20_4,
                .emoji_font = font_emoji_64_init(),
            });
    }

public:
    CompactWifiBoardLCD() : boot_button_(BOOT_BUTTON_GPIO), audio_codec(CODEC_TX_GPIO, CODEC_RX_GPIO)
    {
        // #if CONFIG_LCD_GC9A01_160X160
        //         gpio_config_t bk_gpio_config = {
        //             .pin_bit_mask = 1ULL << GC9A01_SPI1_LCD_GPIO_BL,
        //             .mode = GPIO_MODE_OUTPUT,
        //         };
        //         ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        //         ESP_ERROR_CHECK(gpio_set_level(GC9A01_SPI1_LCD_GPIO_BL, 1));
        // #endif

        InitializeDualScreenEye();
        InitializeButtons();
        InitializeIot();

        // InitializePowerSaveTimer();
        InitializeCamera();
        audio_codec.OnWakeUp([this](const std::string &command)
                             {
            if (command == std::string(vb6824_get_wakeup_word())){
                if(Application::GetInstance().GetDeviceState() != kDeviceStateListening){
                    Application::GetInstance().WakeWordInvoke("你好小智");
                }
            }else if (command == "开始配网"){
                ESP_LOGI(TAG,"开始配网");
                ResetWifiConfiguration();
            } });
    }

    // virtual Led* GetLed() override {
    //     static SingleLed led(BUILTIN_LED_GPIO);
    //     return &led;
    // }

    virtual AudioCodec *GetAudioCodec() override
    {
        return &audio_codec;
    }

    virtual Display *GetDisplay() override
    {
        return display_;
    }

    // #if CONFIG_LCD_GC9A01_160X160
    //     // 获取背光对象
    //     virtual Backlight *GetBacklight() override
    //     {
    //         if (GC9A01_SPI1_LCD_GPIO_BL != GPIO_NUM_NC)
    //         {
    //             static PwmBacklight backlight(GC9A01_SPI1_LCD_GPIO_BL, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    //             return &backlight;
    //         }
    //         return nullptr;
    //     }
    // #endif
    virtual Camera *GetCamera() override
    {
        return camera_;
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);
