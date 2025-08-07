
/*
    CSI控制器
*/

#include <string>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "mcp_server.h"
#include "doit_csi.h"

#define TAG "CSIController"

#define CALIB_TIME_SEC 10                                  /* 校准持续秒数 */
static EventGroupHandle_t csi_event_group_;                // CSI事件组
static TimerHandle_t g_start_calib_countdown_timer = NULL; /* 校准定时器*/
static uint8_t g_start_calib_countdown_left = CALIB_TIME_SEC;
static bool pre_countdown_done = false;
static bool calib_countdown_done = false;
class CSIController
{
private:
    enum CSI_State
    {
        STATE_IDLE,           // 空闲状态
        STATE_PRE_CALIB_ING,  // 预校准状态
        STATE_PRE_CALIB_DONE, // 预校准完成状态
        STATE_CALIB_ING,      // 校准状态
        STATE_CALIB_DONE,     // 校准完成状态
    };

    static inline CSI_State csi_state_ = STATE_IDLE; // 初始化状态机为空闲状态
    static const int STATE_MACHINE_DONE_BIT = 0x01;

    static void PreCountDown()
    {
        ESP_LOGI(TAG, "开始倒计时");
        std::string code = "A9876543210";

        struct digit_sound
        {
            char digit;
            const std::string_view &sound;
        };

        static const std::array<digit_sound, 11> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                               digit_sound{'1', Lang::Sounds::P3_1},
                                                               digit_sound{'2', Lang::Sounds::P3_2},
                                                               digit_sound{'3', Lang::Sounds::P3_3},
                                                               digit_sound{'4', Lang::Sounds::P3_4},
                                                               digit_sound{'5', Lang::Sounds::P3_5},
                                                               digit_sound{'6', Lang::Sounds::P3_6},
                                                               digit_sound{'7', Lang::Sounds::P3_7},
                                                               digit_sound{'8', Lang::Sounds::P3_8},
                                                               digit_sound{'9', Lang::Sounds::P3_9},
                                                               digit_sound{'A', Lang::Sounds::P3_10}}};

        auto &app = Application::GetInstance();
        app.PlaySound(Lang::Sounds::P3_PRE_CALIB);

        for (const auto &digit : code)
        {
            auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                                   [digit](const digit_sound &ds)
                                   { return ds.digit == digit; });
            if (it != digit_sounds.end())
            {

                app.PlaySound(it->sound);
            }
        }
        pre_countdown_done = true;
    }

    // 校准倒计时
    static void CalibCountDown()
    {
        // 播放开始音频
        auto &app = Application::GetInstance();
        app.PlaySound(Lang::Sounds::P3_START_CALIB);
        if (start_csi_calib()) // 开始校准
        {
            g_start_calib_countdown_left = CALIB_TIME_SEC;
            xTimerStart(g_start_calib_countdown_timer, 0);
        }
    }

    /**
     * 校准定时器回调
     */
    static void calib_done_cb(TimerHandle_t t)
    {
        ESP_LOGI(TAG, "校准完成倒计时 %d 秒...", g_start_calib_countdown_left);
        std::string code = "9876543210";

        struct digit_sound
        {
            char digit;
            const std::string_view &sound;
        };
        static const std::array<digit_sound, 10> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                               digit_sound{'1', Lang::Sounds::P3_1},
                                                               digit_sound{'2', Lang::Sounds::P3_2},
                                                               digit_sound{'3', Lang::Sounds::P3_3},
                                                               digit_sound{'4', Lang::Sounds::P3_4},
                                                               digit_sound{'5', Lang::Sounds::P3_5},
                                                               digit_sound{'6', Lang::Sounds::P3_6},
                                                               digit_sound{'7', Lang::Sounds::P3_7},
                                                               digit_sound{'8', Lang::Sounds::P3_8},
                                                               digit_sound{'9', Lang::Sounds::P3_9}}};

        auto &app = Application::GetInstance();

        // 播放对应的倒计时音频
        switch (g_start_calib_countdown_left)
        {
        case 9:
            app.PlaySound(Lang::Sounds::P3_9);
            break;
        case 8:
            app.PlaySound(Lang::Sounds::P3_8);
            break;
        case 7:
            app.PlaySound(Lang::Sounds::P3_7);
            break;
        case 6:
            app.PlaySound(Lang::Sounds::P3_6);
            break;
        case 5:
            app.PlaySound(Lang::Sounds::P3_5);
            break;
        case 4:
            app.PlaySound(Lang::Sounds::P3_4);
            break;
        case 3:
            app.PlaySound(Lang::Sounds::P3_3);
            break;
        case 2:
            app.PlaySound(Lang::Sounds::P3_2);
            break;
        case 1:
            app.PlaySound(Lang::Sounds::P3_1);
            break;
        case 0:
            app.PlaySound(Lang::Sounds::P3_0);
            break;
        default:
            break;
        }
        if (--g_start_calib_countdown_left == 0)
        {
            xTimerStop(g_start_calib_countdown_timer, 0);
            ESP_LOGI(TAG, "倒计时结束，校准已完成");

            stop_csi_calib(); // 停止校准
            // 播放结束音频
            app.PlaySound(Lang::Sounds::P3_CALIB_SUCC);
            calib_countdown_done = true; // 设置倒计时完成标志
        }
    }

    /*
        CSI状态
    */
    static void CSI_Task(void *arg)
    {
        while (true)
        {
            // CSI状态机
            switch (csi_state_)
            {
            case STATE_IDLE: // 默认空闲
                break;
            case STATE_PRE_CALIB_ING:
                pre_countdown_done = false; // 重置倒计时完成标志
                PreCountDown();
                csi_state_ = STATE_PRE_CALIB_DONE; // 切换为校准完成状态
                break;
            case STATE_PRE_CALIB_DONE:
                if (pre_countdown_done)
                {
                    csi_state_ = STATE_CALIB_ING;
                }
                break;
            case STATE_CALIB_ING:
                calib_countdown_done = false; // 重置倒计时完成标志
                CalibCountDown();
                csi_state_ = STATE_CALIB_DONE; // 切换为校准完成状态
                break;
            case STATE_CALIB_DONE:
                if (calib_countdown_done)
                {
                    xEventGroupSetBits(csi_event_group_, STATE_MACHINE_DONE_BIT); // 设置事件位
                    csi_state_ = STATE_IDLE;
                }
                break;
            default: // 默认空闲
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    CSIController();
    ~CSIController();
    void RegisterMcpTools();
    void ForceIdle();
    float GetsomeoneSensit();

public:
    // 获取CSIController实例的静态方法
    static CSIController &GetInstance()
    {
        // 定义一个静态的CSIController实例
        static CSIController instance;
        // 返回该实例
        return instance;
    }

    void Init();
};
