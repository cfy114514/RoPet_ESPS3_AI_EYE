
#include "wifi_board.h"
#include "audio_codecs/vb6824_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "power_save_timer.h"
#include "led/gpio_led.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#define TAG "CompactWifiBoardNoLCD"

class CompactWifiBoardNoLCD : public WifiBoard {
private:
    Button boot_button_;
    VbAduioCodec audio_codec;
    PowerSaveTimer* power_save_timer_ = nullptr;

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        boot_button_.OnPressRepeat([this](uint16_t count) {
            if (count >= 3) {
                ESP_LOGI(TAG, "重新配网");
                ResetWifiConfiguration();
            }
        });

        #if (defined(CONFIG_VB6824_OTA_SUPPORT) && CONFIG_VB6824_OTA_SUPPORT == 1)
        boot_button_.OnDoubleClick([this]() {
            if (esp_timer_get_time() > 20 * 1000 * 1000) {
                ESP_LOGI(TAG, "长按时间过长，不进入OTA模式 %ld", (uint32_t)esp_timer_get_time());
                return;
            }
            audio_codec.OtaStart(0);
        });
        #endif
    }

    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([]() {
            ESP_LOGI(TAG, "进入省电模式");
        });
        power_save_timer_->OnExitSleepMode([]() {
            ESP_LOGI(TAG, "退出省电模式");
        });
        power_save_timer_->OnShutdownRequest([]() {
            ESP_LOGI(TAG, "长时间未使用，关机。请长按按钮重新启动！");
            gpio_set_level(SLEEP_GOIO, 0);
        });
        power_save_timer_->SetEnabled(true);
    }

public:
    CompactWifiBoardNoLCD()
        : boot_button_(BOOT_BUTTON_GPIO),
          audio_codec(CODEC_RX_GPIO, CODEC_TX_GPIO) {

        InitializeButtons();
        InitializeIot();

        gpio_set_pull_mode(SLEEP_GOIO, GPIO_PULLUP_ONLY);
        gpio_set_direction(SLEEP_GOIO, GPIO_MODE_OUTPUT);
        gpio_set_level(SLEEP_GOIO, 1);

        InitializePowerSaveTimer(); // 可选

        audio_codec.OnWakeUp([this](const std::string& command) {
            if (command == std::string(vb6824_get_wakeup_word())) {
                if (Application::GetInstance().GetDeviceState() != kDeviceStateListening) {
                    Application::GetInstance().WakeWordInvoke("你好小智");
                }
            } else if (command == "开始配网") {
                ESP_LOGI(TAG, "收到配网指令");
                ResetWifiConfiguration();
            }
            
        });
    }
    virtual Led* GetLed() override {
        static GpioLed led(BUILTIN_LED_GPIO, 0, LEDC_TIMER_1, LEDC_CHANNEL_1);
        return &led;
    }
    virtual AudioCodec* GetAudioCodec() override {
        return &audio_codec;
    }
};

DECLARE_BOARD(CompactWifiBoardNoLCD);
